package main

import (
	"context"
	"crypto/rand"
	"crypto/sha256"
	"crypto/subtle"
	"crypto/x509"
	"database/sql"
	"encoding/base64"
	"encoding/hex"
	"encoding/json"
	"encoding/pem"
	"errors"
	"flag"
	"fmt"
	"io"
	"log"
	"mime"
	"net"
	"net/http"
	"os"
	"os/signal"
	"path/filepath"
	"reflect"
	"strconv"
	"strings"
	"sync"
	"syscall"
	"time"

	"github.com/grandcat/zeroconf"
)

type App struct {
	store          *Store
	tokenHash      [32]byte
	origins        map[string]bool
	fetcher        *secureFetcher
	writeMu        sync.Mutex
	serverID       string
	tlsFingerprint string
	// Developer API (backend stage api-v2): optional scopes for the single
	// bearer token, plus the staging area of chunked uploads.
	scopes     apiScopes
	scopeNames []string
	uploadDir  string
	uploads    map[string]*uploadSession
	uploadMu   sync.Mutex
	// MCP over HTTP (backend stage mcp-server): the streamable handler is
	// built once per App and mounted at /mcp by registerMCPRoutes.
	mcpOnce    sync.Once
	mcpHandler http.Handler
}

type tombstone struct {
	ID        string `json:"id"`
	Revision  uint64 `json:"revision"`
	DeletedAt string `json:"deleted_at"`
}

func NewApp(store *Store, token string, origins []string) (*App, error) {
	if len(token) < 32 {
		return nil, errors.New("bearer token must contain at least 32 characters")
	}
	a := &App{store: store, tokenHash: sha256.Sum256([]byte(token)), origins: map[string]bool{}, fetcher: newSecureFetcher(),
		scopeNames: []string{"full_access"},
		uploadDir:  env("LUMEN_UPLOAD_DIR", filepath.Join(os.TempDir(), "lumen-uploads")),
		uploads:    map[string]*uploadSession{}}
	for _, o := range origins {
		if o = strings.TrimSpace(o); o != "" {
			a.origins[o] = true
		}
	}
	var identity struct {
		ID string `json:"id"`
	}
	if err := store.Get("system", "server-id", &identity); err != nil || identity.ID == "" {
		identity.ID = newID()
		if err := store.Put("system", "server-id", identity); err != nil {
			return nil, err
		}
	}
	a.serverID = identity.ID
	for _, typ := range defaultTypes {
		existing, err := store.Type(typ.ID)
		if errors.Is(err, sql.ErrNoRows) {
			if err = store.Put("type", typ.ID, typ); err != nil {
				return nil, err
			}
			continue
		} else if err != nil {
			return nil, err
		}
		// Ensure newly added default properties on built-in types are present.
		existingKeys := map[string]bool{}
		for _, raw := range existing.Properties {
			switch v := raw.(type) {
			case string:
				existingKeys[v] = true
			case map[string]any:
				existingKeys[schemaKey(v)] = true
			}
		}
		updated := false
		for _, defProp := range typ.Properties {
			key := ""
			switch v := defProp.(type) {
			case string:
				key = v
			case map[string]any:
				key = schemaKey(v)
			}
			if key != "" && !existingKeys[key] {
				existing.Properties = append(existing.Properties, defProp)
				existingKeys[key] = true
				updated = true
			}
		}
		if updated {
			if err = store.Put("type", typ.ID, existing); err != nil {
				return nil, err
			}
		}
	}
	// Custom types created before templates existed also receive the reserved
	// is_template definition. The write is idempotent and only happens when the
	// stored schema really differs.
	for _, typ := range store.Types() {
		normalized := ensureTemplateProperty(typ.Properties)
		if reflect.DeepEqual(normalized, typ.Properties) {
			continue
		}
		typ.Properties = normalized
		if err := store.Put("type", typ.ID, typ); err != nil {
			return nil, err
		}
	}
	return a, nil
}

func main() {
	initDir := flag.String("init-secrets", "", "create a new key and bearer token in this directory and exit")
	healthcheck := flag.Bool("healthcheck", false, "probe the local /health endpoint and exit")
	fsck := flag.Bool("fsck", false, "verify the encrypted store invariants and exit")
	flag.Parse()
	if *healthcheck {
		if err := runHealthcheck(); err != nil {
			log.Fatal(err)
		}
		return
	}
	if *initDir != "" {
		if err := initSecrets(*initDir); err != nil {
			log.Fatal(err)
		}
		fmt.Println("Secret files created. Keep encryption.key separately from backups; configure token through a local secret file.")
		return
	}
	keyPath := env("LUMEN_KEY_FILE", "/run/secrets/encryption.key")
	key, err := os.ReadFile(keyPath)
	if err != nil {
		log.Fatal("Cannot read encryption key; run -init-secrets and set LUMEN_KEY_FILE")
	}
	store, err := OpenStore(filepath.Join(env("LUMEN_DATA_DIR", "/data/lumen"), "lumen-cache.sqlite"), key)
	if err != nil {
		log.Fatal(err)
	}
	defer store.Close()
	if *fsck {
		if err := store.Fsck(); err != nil {
			log.Fatal(err)
		}
		fmt.Println("fsck: ok")
		return
	}
	tokenPath := env("LUMEN_TOKEN_FILE", "/run/secrets/api.token")
	token, err := os.ReadFile(tokenPath)
	if err != nil {
		log.Fatal("Cannot read API token; configure LUMEN_TOKEN_FILE")
	}
	app, err := NewApp(store, strings.TrimSpace(string(token)), strings.Split(os.Getenv("LUMEN_ALLOWED_ORIGINS"), ","))
	if err != nil {
		log.Fatal(err)
	}
	// LUMEN_TOKEN_SCOPES optionally restricts the bearer token to the
	// developer API scopes (api:read, api:write). Unset keeps full access.
	if raw := os.Getenv("LUMEN_TOKEN_SCOPES"); raw != "" {
		if err := app.SetScopes(raw); err != nil {
			log.Fatalf("LUMEN_TOKEN_SCOPES: %v", err)
		}
		log.Printf("API token restricted to scopes: %s", strings.Join(app.scopeNames, ", "))
	}
	ctx, stop := signal.NotifyContext(context.Background(), os.Interrupt, syscall.SIGTERM)
	defer stop()
	server := &http.Server{Addr: env("LUMEN_LISTEN", "127.0.0.1:8765"), Handler: app.Handler(), ReadHeaderTimeout: 5 * time.Second, ReadTimeout: 45 * time.Second, WriteTimeout: 45 * time.Second, IdleTimeout: 60 * time.Second, MaxHeaderBytes: 16 << 10}
	certFile, keyFile := os.Getenv("LUMEN_TLS_CERT"), os.Getenv("LUMEN_TLS_KEY")
	if (certFile == "") != (keyFile == "") {
		log.Fatal("LUMEN_TLS_CERT and LUMEN_TLS_KEY must be configured together")
	}
	if certFile != "" {
		fingerprint, err := certificateFingerprint(certFile)
		if err != nil {
			log.Fatal(err)
		}
		app.tlsFingerprint = fingerprint
		log.Printf("TLS enabled; certificate SHA-256 fingerprint: %s", fingerprint)
	}
	if mdnsEnabled(server.Addr) {
		port := 8765
		if _, p, err := net.SplitHostPort(server.Addr); err == nil {
			_, _ = fmt.Sscanf(p, "%d", &port)
		}
		txt := []string{"id=" + app.serverID, "protocol=" + syncProtocolVersion}
		if app.tlsFingerprint != "" {
			txt = append(txt, "tls=1", "fingerprint="+app.tlsFingerprint)
		}
		mdns, err := zeroconf.Register("Lumen", "_lumen-sync._tcp", "local.", port, txt, nil)
		if err != nil {
			log.Printf("mDNS registration unavailable: %v", err)
		} else {
			defer mdns.Shutdown()
			log.Printf("mDNS advertised as _lumen-sync._tcp.local. on port %d", port)
		}
	}
	go func() {
		<-ctx.Done()
		shutdownCtx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
		defer cancel()
		_ = server.Shutdown(shutdownCtx)
	}()
	log.Printf("Lumen listening on %s; encrypted local store, health available at /health", server.Addr)
	var serveErr error
	if certFile != "" {
		serveErr = server.ListenAndServeTLS(certFile, keyFile)
	} else {
		serveErr = server.ListenAndServe()
	}
	if serveErr != nil && !errors.Is(serveErr, http.ErrServerClosed) {
		log.Fatal(serveErr)
	}
	stop()
}

func runHealthcheck() error {
	url := strings.TrimSpace(os.Getenv("LUMEN_HEALTH_URL"))
	if url == "" {
		listen := env("LUMEN_LISTEN", "127.0.0.1:8765")
		host, port, err := net.SplitHostPort(listen)
		if err != nil {
			return fmt.Errorf("invalid LUMEN_LISTEN: %w", err)
		}
		if host == "" || host == "0.0.0.0" || host == "::" || host == "[::]" {
			host = "127.0.0.1"
		}
		url = "http://" + net.JoinHostPort(host, port) + "/health"
	}
	client := &http.Client{Timeout: 5 * time.Second}
	resp, err := client.Get(url)
	if err != nil {
		return err
	}
	defer resp.Body.Close()
	var body struct {
		Status string `json:"status"`
	}
	if err = json.NewDecoder(resp.Body).Decode(&body); err != nil {
		return err
	}
	if resp.StatusCode != http.StatusOK || body.Status != "ready" {
		return fmt.Errorf("healthcheck returned HTTP %d status %q", resp.StatusCode, body.Status)
	}
	return nil
}

func certificateFingerprint(path string) (string, error) {
	raw, err := os.ReadFile(path)
	if err != nil {
		return "", err
	}
	block, _ := pem.Decode(raw)
	if block == nil || block.Type != "CERTIFICATE" {
		return "", errors.New("TLS certificate is not valid PEM")
	}
	cert, err := x509.ParseCertificate(block.Bytes)
	if err != nil {
		return "", err
	}
	sum := sha256.Sum256(cert.Raw)
	return hex.EncodeToString(sum[:]), nil
}

func mdnsEnabled(listen string) bool {
	switch strings.ToLower(strings.TrimSpace(os.Getenv("LUMEN_MDNS"))) {
	case "1", "true", "yes":
		return true
	case "0", "false", "no":
		return false
	}
	host, _, err := net.SplitHostPort(listen)
	if err != nil {
		return false
	}
	host = strings.Trim(host, "[]")
	return host != "" && host != "127.0.0.1" && host != "::1" && host != "localhost"
}
func env(name, fallback string) string {
	if v := os.Getenv(name); v != "" {
		return v
	}
	return fallback
}
func initSecrets(dir string) error {
	if err := os.MkdirAll(dir, 0700); err != nil {
		return err
	}
	for name, size := range map[string]int{"encryption.key": 32, "api.token": 32} {
		data := make([]byte, size)
		if _, err := rand.Read(data); err != nil {
			return err
		}
		if name == "api.token" {
			data = []byte(hex.EncodeToString(data))
		}
		f, err := os.OpenFile(filepath.Join(dir, name), os.O_WRONLY|os.O_CREATE|os.O_EXCL, 0600)
		if err != nil {
			return err
		}
		_, err = f.Write(data)
		closeErr := f.Close()
		if err != nil {
			return err
		}
		if closeErr != nil {
			return closeErr
		}
	}
	return nil
}
func (a *App) Handler() http.Handler {
	mux := http.NewServeMux()
	mux.HandleFunc("GET /health", a.health)
	mux.HandleFunc("GET /v1/types", a.listTypes)
	mux.HandleFunc("POST /v1/types", a.createType)
	mux.HandleFunc("PATCH /v1/types/{id}", a.updateType)
	mux.HandleFunc("GET /v1/objects", a.listObjects)
	mux.HandleFunc("POST /v1/objects", a.createObject)
	mux.HandleFunc("POST /v1/objects/from-template", a.fromTemplate)
	mux.HandleFunc("GET /v1/objects/{id}", a.getObject)
	mux.HandleFunc("PATCH /v1/objects/{id}", a.updateObject)
	mux.HandleFunc("DELETE /v1/objects/{id}", a.deleteObject)
	mux.HandleFunc("GET /v1/objects/{id}/backlinks", a.backlinks)
	a.registerBlockRoutes(mux)
	a.registerHabitRoutes(mux)
	a.registerTrashRoutes(mux)
	a.registerCollectionRoutes(mux)
	// Recurring tasks (backend parity stage 8): completing a task may spawn its
	// next occurrence in the same transaction.
	mux.HandleFunc("POST /v1/tasks/{id}/complete", a.completeTask)
	mux.HandleFunc("POST /v1/tasks/{id}/skip", a.skipTask)
	mux.HandleFunc("POST /v1/tasks/{id}/excuse", a.excuseTask)
	a.registerConvertMergeRoutes(mux)
	mux.HandleFunc("GET /v1/journal/{date}", a.journal)
	mux.HandleFunc("GET /v1/search", a.searchObjects)
	mux.HandleFunc("POST /v1/capture", a.capture)
	// Weblink reader stage: POST /v1/objects/{id}/refresh-reader.
	a.registerWeblinkReaderRoutes(mux)
	mux.HandleFunc("POST /v1/assets", a.uploadAsset)
	mux.HandleFunc("POST /v1/assets/{id}/promote", a.promoteAsset)
	mux.HandleFunc("GET /v1/assets", a.listAssets)
	mux.HandleFunc("GET /v1/assets/{id}/content", a.assetContent)
	mux.HandleFunc("GET /v1/sync/status", a.syncStatus)
	mux.HandleFunc("POST /v1/sync/pull", a.syncPull)
	mux.HandleFunc("POST /v1/sync/push", a.syncPush)
	// Kept as a compatibility no-op for older clients. There is no background
	// exporter to flush now; the durable store is already the source of truth.
	mux.HandleFunc("POST /v1/sync", a.health)
	// Object version history: server-side snapshots that never enter the sync
	// journal.
	a.registerVersionHistoryRoutes(mux)
	a.registerMarkdownRoutes(mux)
	// Full space export: GET /v1/export and POST /v1/export.
	a.registerExportRoutes(mux)
	// Developer API v2: discovery, from-url, Markdown create/append, journal
	// append, chunked uploads.
	a.registerAPIv2Routes(mux)
	// MCP over HTTP (backend parity stage mcp-server): streamable transport
	// behind the same bearer token and scope model.
	a.registerMCPRoutes(mux)
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		// X-Lumen-Api-Version (modeled on Capacities' X-Capacities-Api-Version)
		// is echoed on every response. An absent or unknown request header
		// never fails: the server always answers with the version it speaks.
		w.Header().Set("X-Lumen-Api-Version", apiVersion)
		w.Header().Set("Cache-Control", "no-store")
		w.Header().Set("X-Content-Type-Options", "nosniff")
		w.Header().Set("Referrer-Policy", "no-referrer")
		if origin := r.Header.Get("Origin"); origin != "" {
			if !a.origins[origin] {
				jsonError(w, 403, "This browser origin is not allowed. Add its exact origin to LUMEN_ALLOWED_ORIGINS.")
				return
			}
			w.Header().Set("Access-Control-Allow-Origin", origin)
			w.Header().Set("Vary", "Origin")
			w.Header().Set("Access-Control-Allow-Methods", "GET,POST,PUT,PATCH,DELETE,OPTIONS")
			w.Header().Set("Access-Control-Allow-Headers", "Authorization,Content-Type,X-Lumen-Api-Version")
			w.Header().Set("Access-Control-Expose-Headers", "X-Lumen-Api-Version,X-Lumen-Next-Cursor")
			w.Header().Set("Access-Control-Max-Age", "600")
		}
		if r.Method == http.MethodOptions {
			w.WriteHeader(204)
			return
		}
		if r.URL.Path != "/health" {
			bearer := strings.TrimPrefix(r.Header.Get("Authorization"), "Bearer ")
			candidate := sha256.Sum256([]byte(bearer))
			if !strings.HasPrefix(r.Header.Get("Authorization"), "Bearer ") || subtle.ConstantTimeCompare(candidate[:], a.tokenHash[:]) != 1 {
				w.Header().Set("WWW-Authenticate", "Bearer")
				jsonError(w, 401, "Authentication required")
				return
			}
			// Optional token scopes (api:read/api:write); a token without
			// scopes keeps full access.
			if err := a.authorizeScope(r); err != nil {
				jsonError(w, 403, err.Error())
				return
			}
		}
		mux.ServeHTTP(w, r)
	})
}
func jsonResponse(w http.ResponseWriter, status int, value any) {
	w.Header().Set("Content-Type", "application/json; charset=utf-8")
	w.WriteHeader(status)
	_ = json.NewEncoder(w).Encode(value)
}
func jsonError(w http.ResponseWriter, status int, message string) {
	jsonResponse(w, status, map[string]string{"error": message})
}
func decode(w http.ResponseWriter, r *http.Request, out any) bool {
	return decodeLimit(w, r, out, 3<<20)
}

func decodeLimit(w http.ResponseWriter, r *http.Request, out any, max int64) bool {
	r.Body = http.MaxBytesReader(w, r.Body, max)
	d := json.NewDecoder(r.Body)
	if err := d.Decode(out); err != nil {
		jsonError(w, 400, fmt.Sprintf("Invalid JSON or document exceeds %d MB", max/(1<<20)))
		return false
	}
	var extra any
	if d.Decode(&extra) != io.EOF {
		jsonError(w, 400, "Only one JSON document is allowed")
		return false
	}
	return true
}
func (a *App) health(w http.ResponseWriter, r *http.Request) {
	status := map[string]any{
		"status":           "ready",
		"pending":          0,
		"source_of_truth":  "lumen-store",
		"cache_encryption": "AES-256-GCM",
		"asset_encryption": "AES-256-GCM",
		"objects":          a.store.ObjectCount(),
	}
	jsonResponse(w, 200, status)
}
func (a *App) listTypes(w http.ResponseWriter, r *http.Request) {
	jsonResponse(w, 200, a.store.Types())
}
func (a *App) createType(w http.ResponseWriter, r *http.Request) {
	var t ObjectType
	if !decode(w, r, &t) {
		return
	}
	// A client re-pairing against a fresh server may pin the id so the
	// objects that reference it keep working. The id is optional, trimmed,
	// and 4..64 characters without whitespace. Re-pairing an id that already
	// exists returns the stored definition untouched (idempotent).
	if requested := strings.TrimSpace(t.ID); requested != "" {
		if len(requested) < 4 || len(requested) > 64 || strings.ContainsAny(requested, " \t\r\n") {
			jsonError(w, 400, "Invalid type id")
			return
		}
		if existing, err := a.store.Type(requested); err == nil {
			jsonResponse(w, 200, existing)
			return
		}
		t.ID = requested
	} else {
		t.ID = newID()
	}
	if t.Icon == "" {
		t.Icon = "text-x-generic"
	}
	if t.Color == "" {
		t.Color = "#7b91ae"
	}
	if t.Properties == nil {
		t.Properties = []any{}
	}
	t.DefaultTemplateID = strings.TrimSpace(t.DefaultTemplateID)
	if t.DefaultTemplateID != "" {
		jsonError(w, 400, "A new type cannot reference a default template")
		return
	}
	if err := validateType(&t); err != nil {
		jsonError(w, 400, err.Error())
		return
	}
	a.writeMu.Lock()
	defer a.writeMu.Unlock()
	if err := a.validateTypeReferences(t); err != nil {
		jsonError(w, 400, err.Error())
		return
	}
	if err := a.store.Put("type", t.ID, t); err != nil {
		jsonError(w, 500, "Could not save object type")
		return
	}
	jsonResponse(w, 201, t)
}
func (a *App) objects() ([]Object, error) {
	return a.store.Objects()
}
func (a *App) listObjects(w http.ResponseWriter, r *http.Request) {
	filters, err := parseObjectFilters(r.URL.Query())
	if err != nil {
		jsonError(w, 400, err.Error())
		return
	}
	objects, err := a.applyObjectFilters(filters)
	if err != nil {
		var invalid filterError
		if errors.As(err, &invalid) {
			jsonError(w, 400, invalid.Error())
			return
		}
		jsonError(w, 500, "Could not read objects")
		return
	}
	includeTemplates, err := queryBool(r, "include_templates")
	if err != nil {
		jsonError(w, 400, err.Error())
		return
	}
	filtered := []Object{}
	for _, o := range objects {
		if !includeTemplates && isTemplateObject(o) {
			continue
		}
		filtered = append(filtered, o)
	}
	// Pagination runs after every filter (see pagination.go). A request
	// without limit and cursor keeps the full list.
	if !writePageSlice(w, r, filtered, func(start, end int) {
		jsonResponse(w, 200, filtered[start:end])
	}) {
		return
	}
}
func (a *App) getObject(w http.ResponseWriter, r *http.Request) {
	o, err := a.store.Object(r.PathValue("id"))
	if err != nil {
		jsonError(w, 404, "Object not found")
		return
	}
	jsonResponse(w, 200, o)
}
func (a *App) validateObject(o *Object) error {
	o.Title = strings.TrimSpace(o.Title)
	if len([]rune(o.Title)) > 1000 {
		return errors.New("An object title supports up to 1000 characters")
	}
	// Templates live in their own title namespace: applying a template whose
	// title has no date variables must not collide with the template itself.
	if !isTemplateObject(*o) {
		if owner := a.store.TitleOwner(o.Title); owner != "" && owner != o.ID {
			return errors.New("An object with this title already exists; give this object a distinct name")
		}
	}
	t, err := a.store.Type(o.TypeID)
	if err != nil {
		return errors.New("Unknown object type")
	}
	if o.Properties == nil {
		o.Properties = map[string]any{}
	}
	// `aliases` is a default property owned by the server (see aliases.go), so
	// it is validated here instead of through the per-type schema.
	if err := normalizeAliases(o.Properties); err != nil {
		return err
	}
	// `summary` es la descripción canónica; `description` solo se conserva
	// como alias heredado mientras no haya un summary editable.
	if summary, ok := o.Properties["summary"].(string); ok && strings.TrimSpace(summary) != "" {
		delete(o.Properties, "description")
	}
	if o.Links == nil {
		o.Links = []string{}
	}
	if err := a.validatePropertyValues(o, t); err != nil {
		return err
	}
	if o.TypeID == "collection" {
		if err := a.validateCollection(o); err != nil {
			return err
		}
	}
	// Recurrence is a reserved task property. The helper rejects it on other
	// types instead of silently ignoring a rule the engine would never run.
	if err := validateHabit(o); err != nil {
		return err
	}
	if err := validateTaskRecurrence(o); err != nil {
		return err
	}
	dedup := map[string]bool{}
	links := []string{}
	for _, id := range o.Links {
		if id == o.ID || dedup[id] {
			continue
		}
		// A trashed target is allowed: it is restorable, and keeping the
		// reference is what makes restore bring backlinks back intact.
		if !a.referenceExists(id) {
			return errors.New("Linked object does not exist")
		}
		dedup[id] = true
		links = append(links, id)
	}
	o.Links = links
	return nil
}

// prepareSave is called with writeMu held. The object, its version snapshot and
// its tombstone cleanup are committed together; there is no external worker.
func (a *App) prepareSave(o *Object) []Record {
	var previous Object
	hadPrevious := a.store.Get("object", o.ID, &previous) == nil
	if hadPrevious {
		o.Incarnation = previous.Incarnation
		if previous.Revision > o.Revision {
			o.Revision = previous.Revision
		}
	}
	var deleted tombstone
	if a.store.Get("tombstone", o.ID, &deleted) == nil {
		if deleted.Revision > o.Revision {
			o.Revision = deleted.Revision
		}
		// Fixed journal IDs can be explicitly recreated after deletion.
		o.Incarnation = newID()
	}
	o.Revision++
	o.SyncStatus, o.SyncError = "synced", ""
	records := []Record{{Kind: "object", ID: o.ID, Value: o}}
	if hadPrevious {
		// Snapshot the state being replaced in the same transaction as the new
		// revision. The snapshot is keyed by the previous revision, so the
		// current state is always GET /v1/objects/{id}, never a version row.
		records = append(records, Record{Kind: versionRecordKind, ID: o.ID, Value: VersionRecord{
			ObjectID: o.ID,
			Revision: previous.Revision,
			Value:    snapshotFromObject(previous),
		}})
	}
	return records
}

func (a *App) saveObject(_ context.Context, o *Object) error {
	// Recreating an object supersedes its tombstone and any stale trash payload.
	return a.store.PutBatch(a.prepareSave(o), saveCleanup(o.ID))
}
func (a *App) createObject(w http.ResponseWriter, r *http.Request) {
	var o Object
	if !decode(w, r, &o) {
		return
	}
	a.writeMu.Lock()
	defer a.writeMu.Unlock()
	o.ID = newID()
	o.Revision = 0 // revisions are assigned by this store, never by API callers
	o.Incarnation = ""
	o.CreatedAt = now()
	o.UpdatedAt = o.CreatedAt
	if err := a.validateObject(&o); err != nil {
		jsonError(w, 400, err.Error())
		return
	}
	if o.TypeID == "web" {
		preserveReaderMetadata(&o, nil)
	}
	if err := a.saveObject(r.Context(), &o); err != nil {
		jsonError(w, 500, "Could not persist encrypted object")
		return
	}
	jsonResponse(w, 201, o)
}
func (a *App) updateObject(w http.ResponseWriter, r *http.Request) {
	var patch map[string]json.RawMessage
	if !decode(w, r, &patch) {
		return
	}
	var baseRevision uint64
	if raw, ok := patch["base_revision"]; ok {
		if err := json.Unmarshal(raw, &baseRevision); err != nil {
			jsonError(w, 400, "Invalid field: base_revision")
			return
		}
		delete(patch, "base_revision")
	}
	a.writeMu.Lock()
	defer a.writeMu.Unlock()
	var o Object
	if err := a.store.Get("object", r.PathValue("id"), &o); err != nil {
		jsonError(w, 404, "Object not found")
		return
	}
	if baseRevision > 0 && baseRevision != o.Revision {
		jsonResponse(w, 409, map[string]any{"error": "conflict", "conflict": true, "current": o})
		return
	}
	apply := func(key string, value any) bool {
		if raw, ok := patch[key]; ok {
			if json.Unmarshal(raw, value) != nil {
				jsonError(w, 400, "Invalid field: "+key)
				return false
			}
		}
		return true
	}
	before := cloneObject(o)
	// JSON unmarshalling reuses a non-nil map. Preserve its managed values
	// before the incoming property map can overwrite those same entries.
	previousProperties := make(map[string]any, len(o.Properties))
	for key, value := range o.Properties {
		previousProperties[key] = value
	}
	previousContent := o.Content
	if _, present := patch["properties"]; present {
		// properties is an explicit replacement document. Unmarshal into a
		// fresh map so omitted editable keys really disappear.
		o.Properties = nil
	}
	if !apply("title", &o.Title) || !apply("type_id", &o.TypeID) || !apply("content", &o.Content) || !apply("properties", &o.Properties) || !apply("links", &o.Links) {
		return
	}
	// Add relations against the latest saved object while holding writeMu. A
	// delayed capture can link its original journal without resending stale
	// document text or overwriting another concurrently added relation.
	var additions []string
	if !apply("add_links", &additions) {
		return
	}
	o.Links = append(o.Links, additions...)
	a.preserveManagedAssets(&o, previousProperties, patch["properties"])
	preserveReaderMetadata(&o, previousProperties)
	invalidateImplicitFormats(&o, previousContent, patch)
	if err := a.validateObject(&o); err != nil {
		jsonError(w, 400, err.Error())
		return
	}
	if before.Title == o.Title && before.TypeID == o.TypeID && before.Content == o.Content &&
		sameStrings(before.Links, o.Links) && sameStrings(before.PropertyLinks, o.PropertyLinks) &&
		reflect.DeepEqual(before.Properties, o.Properties) {
		o.SyncStatus, o.SyncError = "synced", ""
		jsonResponse(w, 200, o)
		return
	}
	o.UpdatedAt = now()
	records := a.prepareSave(&o)
	// A type must never keep pointing at an object that stopped being a
	// template of that type. The cleanup is committed with the object change.
	records = append(records, a.templateDefaultCleanup(before, o)...)
	if err := a.store.PutBatch(records, [][2]string{{"tombstone", o.ID}}); err != nil {
		jsonError(w, 500, "Could not persist encrypted object")
		return
	}
	jsonResponse(w, 200, o)
}
func (a *App) deleteObject(w http.ResponseWriter, r *http.Request) {
	a.writeMu.Lock()
	defer a.writeMu.Unlock()
	id := r.PathValue("id")
	records, deletes, deleted, err := a.trashObjectData(id)
	if errors.Is(err, sql.ErrNoRows) {
		jsonError(w, 404, "Object not found")
		return
	}
	if err != nil {
		jsonError(w, 500, "Could not read object")
		return
	}
	// The tombstone keeps the anti-resurrection guarantee and the mobile
	// deletion signal; the trash payload keeps the object recoverable. Both
	// commit together with the removal of the live row.
	if err = a.store.PutBatchBlobs(records, deletes, nil); err != nil {
		jsonError(w, 500, "Could not persist deletion")
		return
	}
	jsonResponse(w, 200, map[string]any{
		"deleted": true, "trashed": true, "id": id,
		"deleted_at": deleted.DeletedAt, "sync_status": "synced",
	})
}
func (a *App) backlinks(w http.ResponseWriter, r *http.Request) {
	id := r.PathValue("id")
	if a.store.Has("trash", id) {
		// A trashed object is not a live card: do not expose its inbound
		// relations until it is restored.
		jsonError(w, 404, "Object not found")
		return
	}
	includeTemplates, err := queryBool(r, "include_templates")
	if err != nil {
		jsonError(w, 400, err.Error())
		return
	}
	objects, err := a.store.BacklinksFiltered(id, includeTemplates)
	if err != nil {
		jsonError(w, 500, "Could not read connections")
		return
	}
	// Optional type filter: `?type_id=collection` answers "which collections
	// contain this object?" without making the client scan every backlink.
	if typ := r.URL.Query().Get("type_id"); typ != "" {
		filtered := make([]Object, 0, len(objects))
		for _, o := range objects {
			if o.TypeID == typ {
				filtered = append(filtered, o)
			}
		}
		objects = filtered
	}
	jsonResponse(w, 200, objects)
}
func (a *App) ensureJournal(ctx context.Context, date string, applyDefaultTemplate bool) (Object, error) {
	if parsed, err := time.Parse("2006-01-02", date); err != nil || parsed.Format("2006-01-02") != date {
		return Object{}, errors.New("Date must be YYYY-MM-DD")
	}
	id := "journal-" + date
	var o Object
	err := a.store.Get("object", id, &o)
	if err == nil {
		return o, nil
	}
	if !errors.Is(err, sql.ErrNoRows) {
		return Object{}, err
	}
	// Opening a date whose journal is in the trash restores it instead of
	// creating an empty replacement. Otherwise the next app start would
	// silently discard the trashed text.
	if a.store.Has("trash", id) {
		restored, records, deletes, restoreErr := a.restoreObjectData(id)
		if restoreErr != nil {
			return Object{}, restoreErr
		}
		if restoreErr = a.store.PutBatch(records, deletes); restoreErr != nil {
			return Object{}, restoreErr
		}
		return restored, nil
	}
	o = Object{ID: id, TypeID: "journal", Title: date, Content: "", Properties: map[string]any{"date": date}, Links: []string{}, CreatedAt: now(), UpdatedAt: now()}
	if applyDefaultTemplate {
		if err := a.applyJournalDefaultTemplate(&o, date); err != nil {
			return Object{}, err
		}
	}
	err = a.saveObject(ctx, &o)
	return o, err
}
func (a *App) journal(w http.ResponseWriter, r *http.Request) {
	applyDefaultTemplate, err := queryBool(r, "apply_default_template")
	if err != nil {
		jsonError(w, 400, err.Error())
		return
	}
	a.writeMu.Lock()
	defer a.writeMu.Unlock()
	o, err := a.ensureJournal(r.Context(), r.PathValue("date"), applyDefaultTemplate)
	if err != nil {
		jsonError(w, 400, err.Error())
		return
	}
	jsonResponse(w, 200, o)
}
func (a *App) capture(w http.ResponseWriter, r *http.Request) {
	var req struct {
		URL          string `json:"url"`
		Title        string `json:"title"`
		Selection    string `json:"selection"`
		Source       string `json:"source"`
		TypeID       string `json:"type_id"`
		ObjectType   string `json:"object_type"`
		Date         string `json:"date"`
		Journal      bool   `json:"journal"`
		FetchPreview *bool  `json:"fetch_preview"`
		Status       string `json:"status"`
		Preview      *struct {
			Title       string `json:"title"`
			Description string `json:"description"`
			SiteName    string `json:"site_name"`
			ImageBase64 string `json:"image_base64"`
			ImageMIME   string `json:"image_mime"`
		} `json:"preview"`
	}
	if !decodeLimit(w, r, &req, 12<<20) {
		return
	}
	if req.URL != "" {
		cleaned, err := a.normalizeCaptureURL(r.Context(), req.URL)
		if err != nil {
			jsonError(w, 400, err.Error())
			return
		}
		req.URL = cleaned
	}
	if req.Date != "" {
		if d, err := time.Parse("2006-01-02", req.Date); err != nil || d.Format("2006-01-02") != req.Date {
			jsonError(w, 400, "Date must be YYYY-MM-DD")
			return
		}
	}
	if req.URL == "" && strings.TrimSpace(req.Selection) == "" {
		jsonError(w, 400, "Provide a URL or selected text")
		return
	}
	if req.TypeID == "" {
		req.TypeID = req.ObjectType
	}
	if req.TypeID == "" || req.TypeID == "link" {
		req.TypeID = "web"
	}
	if req.TypeID == "web" && strings.TrimSpace(req.Status) == "" {
		req.Status = "Para revisar"
	}
	if req.Status == "" {
		req.Status = "Para revisar"
	}
	// Duplicate links are a common mobile share-sheet accident. Return the
	// existing object (and optionally link it to today's journal) instead of
	// creating a second card for the same canonical URL.
	if req.URL != "" {
		if existing, found := a.findWebObjectByURL(req.URL); found {
			a.writeMu.Lock()
			defer a.writeMu.Unlock()
			journalID := ""
			if req.Date != "" || req.Journal {
				date := req.Date
				if date == "" {
					date = time.Now().Format("2006-01-02")
				}
				if linked, err := a.linkJournalObject(r.Context(), date, existing.ID); err == nil {
					journalID = linked
				}
			}
			raw, _ := json.Marshal(existing)
			response := map[string]any{}
			_ = json.Unmarshal(raw, &response)
			response["duplicate"] = true
			if journalID != "" {
				response["journal_id"] = journalID
			}
			jsonResponse(w, 200, response)
			return
		}
	}
	o := Object{ID: newID(), TypeID: req.TypeID, Title: req.Title, Content: req.Selection, Properties: map[string]any{"url": req.URL, "source": req.Source}, Links: []string{}, CreatedAt: now(), UpdatedAt: now()}
	if req.TypeID == "web" {
		o.Properties["status"] = req.Status
	}
	var previewAsset *Asset
	var previewBlob []byte
	previewReady := false
	if req.Preview != nil {
		previewReady = true
		o.Properties["preview_status"] = "ready"
		o.Properties["og_title"] = req.Preview.Title
		o.Properties["og_description"] = req.Preview.Description
		o.Properties["summary"] = req.Preview.Description
		o.Properties["og_site_name"] = req.Preview.SiteName
		if o.Title == "" {
			o.Title = req.Preview.Title
		}
		if req.Preview.ImageBase64 != "" {
			data, err := base64.StdEncoding.DecodeString(req.Preview.ImageBase64)
			if err == nil && len(data) > 0 && len(data) <= 8<<20 {
				mimeType := req.Preview.ImageMIME
				if mimeType == "" {
					mimeType = http.DetectContentType(data)
				}
				if strings.HasPrefix(mimeType, "image/") {
					asset := Asset{ID: newID(), ObjectID: o.ID, Name: "preview.webp", MIME: mimeType, Size: len(data), CreatedAt: now()}
					previewAsset, previewBlob = &asset, data
					o.Properties["image_asset_id"] = asset.ID
				}
			}
		}
	}
	if req.URL != "" && !previewReady && (req.FetchPreview == nil || *req.FetchPreview) {
		preview, err := a.capturePreview(r.Context(), req.URL)
		if err != nil {
			o.Properties["preview_status"] = "unavailable"
			o.Properties["preview_error"] = "The public preview could not be fetched; the link is saved."
			if req.TypeID == "web" {
				o.Properties["reader_content"] = ""
			}
		} else {
			o.Properties["preview_status"] = "ready"
			o.Properties["og_title"] = preview.Title
			o.Properties["og_description"] = preview.Description
			o.Properties["summary"] = preview.Description
			o.Properties["og_site_name"] = preview.SiteName
			if req.TypeID == "web" {
				o.Properties["reader_content"] = preview.ReaderContent
				if preview.Favicon != "" {
					o.Properties["favicon"] = preview.Favicon
				}
			}
			if o.Title == "" {
				o.Title = preview.Title
			}
			if len(preview.ImageData) > 0 {
				asset := Asset{ID: newID(), ObjectID: o.ID, Name: "preview.webp", MIME: preview.ImageMIME, Size: len(preview.ImageData), CreatedAt: now()}
				previewAsset, previewBlob = &asset, preview.ImageData
				o.Properties["image_asset_id"] = asset.ID
			}
		}
	}
	if strings.TrimSpace(o.Title) == "" {
		if u, err := safeURL(req.URL); err == nil {
			o.Title = u.Hostname()
		} else {
			o.Title = "Texto guardado"
		}
	}
	a.writeMu.Lock()
	defer a.writeMu.Unlock()
	if err := a.validateObject(&o); err != nil {
		jsonError(w, 400, err.Error())
		return
	}
	records := a.prepareSave(&o)
	var blobs []BlobRecord
	if previewAsset != nil {
		records = append(records, Record{Kind: "asset", ID: previewAsset.ID, Value: *previewAsset})
		blobs = append(blobs, BlobRecord{ID: previewAsset.ID, Data: previewBlob})
	}
	if err := a.store.PutBatchBlobs(records, nil, blobs); err != nil {
		jsonError(w, 500, "Could not save capture")
		return
	}
	if req.Journal && req.Date == "" {
		req.Date = time.Now().Format("2006-01-02")
	}
	journalID := ""
	if req.Date != "" {
		journal, err := a.ensureJournal(r.Context(), req.Date, false)
		if err != nil {
			jsonError(w, 400, "Capture saved, but journal date is invalid")
			return
		}
		found := false
		for _, id := range journal.Links {
			if id == o.ID {
				found = true
				break
			}
		}
		if !found {
			journal.Links = append(journal.Links, o.ID)
			journal.UpdatedAt = now()
			if err = a.saveObject(r.Context(), &journal); err != nil {
				jsonError(w, 500, "Capture saved, but could not link journal")
				return
			}
		}
		journalID = journal.ID
	}
	if journalID != "" {
		raw, _ := json.Marshal(o)
		response := map[string]any{}
		_ = json.Unmarshal(raw, &response)
		response["journal_id"] = journalID
		jsonResponse(w, 201, response)
	} else {
		jsonResponse(w, 201, o)
	}
}
func (a *App) saveAsset(objectID, name, mimeType string, data []byte) (Asset, error) {
	asset := Asset{ID: newID(), ObjectID: objectID, Name: filepath.Base(name), MIME: mimeType, Size: len(data), CreatedAt: now()}
	if err := a.store.PutBatchBlobs([]Record{{Kind: "asset", ID: asset.ID, Value: asset}}, nil, []BlobRecord{{ID: asset.ID, Data: data}}); err != nil {
		return Asset{}, err
	}
	return asset, nil
}
func (a *App) uploadAsset(w http.ResponseWriter, r *http.Request) {
	r.Body = http.MaxBytesReader(w, r.Body, 51<<20)
	reader, err := r.MultipartReader()
	if err != nil {
		jsonError(w, 400, "Asset exceeds 50 MB or multipart is invalid")
		return
	}
	// Read one bounded attachment in memory: never spill plaintext uploads into /tmp.
	var id, name string
	var data []byte
	for {
		part, err := reader.NextPart()
		if errors.Is(err, io.EOF) {
			break
		}
		if err != nil {
			jsonError(w, 400, "Invalid multipart body")
			return
		}
		switch part.FormName() {
		case "object_id":
			value, err := io.ReadAll(io.LimitReader(part, 256))
			if err != nil {
				jsonError(w, 400, "Invalid object ID")
				return
			}
			id = string(value)
		case "file":
			if data != nil {
				jsonError(w, 400, "Upload one attachment at a time")
				return
			}
			name = part.FileName()
			data, err = io.ReadAll(io.LimitReader(part, (50<<20)+1))
			if err != nil || len(data) > 50<<20 {
				jsonError(w, 400, "Asset exceeds 50 MB")
				return
			}
		default:
			jsonError(w, 400, "Unknown multipart field")
			return
		}
		part.Close()
	}
	if data == nil || name == "" {
		jsonError(w, 400, "Provide a file")
		return
	}
	a.writeMu.Lock()
	defer a.writeMu.Unlock()
	var o Object
	if id != "" {
		if err := a.store.Get("object", id, &o); err != nil {
			jsonError(w, 404, "Object not found")
			return
		}
	}
	mimeType := detectAssetMIME(data, name, "")
	asset := Asset{ID: newID(), ObjectID: id, Name: filepath.Base(name), MIME: mimeType, Size: len(data), CreatedAt: now()}
	records := []Record{}
	if mediaType(asset.MIME) != "" {
		media, err := a.newMediaObject(&asset)
		if err != nil {
			jsonError(w, 400, err.Error())
			return
		}
		records = append(records, a.prepareSave(&media)...)
		asset.MediaObject = &media
		if id != "" {
			o.Links = append(o.Links, media.ID)
		}
	}
	if id != "" {
		if o.Properties == nil {
			o.Properties = map[string]any{}
		}
		existing, _ := o.Properties["assets"].([]any)
		o.Properties["assets"] = append(existing, assetProperties(asset))
		if strings.HasPrefix(mimeType, "image/") {
			o.Properties["image_asset_id"] = asset.ID
		}
		o.UpdatedAt = now()
		records = append(records, a.prepareSave(&o)...)
	}
	storedAsset := asset
	storedAsset.MediaObject = nil
	records = append(records, Record{Kind: "asset", ID: asset.ID, Value: storedAsset})
	if err = a.store.PutBatchBlobs(records, nil, []BlobRecord{{ID: asset.ID, Data: data}}); err != nil {
		jsonError(w, 500, "Could not persist encrypted attachment and objects")
		return
	}
	jsonResponse(w, 201, asset)
}

func assetProperties(asset Asset) map[string]any {
	value := map[string]any{"id": asset.ID, "name": asset.Name, "mime": asset.MIME, "size": asset.Size}
	if asset.MediaObjectID != "" {
		value["media_object_id"] = asset.MediaObjectID
	}
	return value
}

// newMediaObject is only invoked for a new upload or an explicit promotion.
// Older attachments remain untouched until the user chooses to promote one.
// The caller owns writeMu and commits this object together with the asset.
func (a *App) newMediaObject(asset *Asset) (Object, error) {
	typ := mediaType(asset.MIME)
	base := strings.TrimSpace(asset.Name)
	if base == "" {
		base = "Archivo"
	}
	if len(base) > 800 {
		base = string([]rune(base)[:min(200, len([]rune(base)))])
	}
	title := base
	for suffix := 2; a.store.TitleExists(title); suffix++ {
		title = fmt.Sprintf("%s (%d)", base, suffix)
	}
	asset.MediaObjectID = newID()
	sourceID := asset.ObjectID
	if sourceID == "" {
		asset.ObjectID = asset.MediaObjectID
	}
	props := map[string]any{"asset_id": asset.ID, "mime": asset.MIME, "file_name": asset.Name, "size": asset.Size, "assets": []any{assetProperties(*asset)}}
	if sourceID != "" {
		props["source_object_id"] = sourceID
	}
	if typ == "image" {
		props["image_asset_id"] = asset.ID
	}
	o := Object{ID: asset.MediaObjectID, TypeID: typ, Title: title, Properties: props, Links: []string{}, CreatedAt: now(), UpdatedAt: now()}
	return o, a.validateObject(&o)
}

func (a *App) promoteAsset(w http.ResponseWriter, r *http.Request) {
	a.writeMu.Lock()
	defer a.writeMu.Unlock()
	var asset Asset
	if err := a.store.Get("asset", r.PathValue("id"), &asset); err != nil {
		jsonError(w, 404, "Asset not found")
		return
	}
	var media Object
	if asset.MediaObjectID != "" && a.store.Get("object", asset.MediaObjectID, &media) == nil {
		jsonResponse(w, 200, map[string]any{"asset": asset, "media_object_id": media.ID, "object": media})
		return
	}
	media, err := a.newMediaObject(&asset)
	if err != nil {
		jsonError(w, 400, err.Error())
		return
	}
	storedAsset := asset
	storedAsset.MediaObject = nil
	records := append(a.prepareSave(&media), Record{Kind: "asset", ID: asset.ID, Value: storedAsset})
	var source Object
	if a.store.Get("object", asset.ObjectID, &source) == nil {
		if source.Properties == nil {
			source.Properties = map[string]any{}
		}
		list, _ := source.Properties["assets"].([]any)
		found := false
		for i, item := range list {
			if itemMap, ok := item.(map[string]any); ok && itemMap["id"] == asset.ID {
				list[i] = assetProperties(asset)
				found = true
			}
		}
		if !found {
			list = append(list, assetProperties(asset))
		}
		source.Properties["assets"] = list
		source.Links = append(source.Links, media.ID)
		source.UpdatedAt = now()
		records = append(records, a.prepareSave(&source)...)
	}
	if err := a.store.PutBatch(records, nil); err != nil {
		jsonError(w, 500, "Could not persist media object")
		return
	}
	jsonResponse(w, 201, map[string]any{"asset": asset, "media_object_id": media.ID, "object": media})
}

func (a *App) listAssets(w http.ResponseWriter, r *http.Request) {
	rows, err := a.store.List("asset")
	if err != nil {
		jsonError(w, 500, "Could not read attachments")
		return
	}
	out := []Asset{}
	for _, raw := range rows {
		var asset Asset
		_ = json.Unmarshal(raw, &asset)
		if id := r.URL.Query().Get("object_id"); id == "" || id == asset.ObjectID || id == asset.MediaObjectID {
			out = append(out, asset)
		}
	}
	jsonResponse(w, 200, out)
}
func (a *App) assetContent(w http.ResponseWriter, r *http.Request) {
	id := r.PathValue("id")
	var asset Asset
	if err := a.store.Get("asset", id, &asset); err != nil {
		jsonError(w, 404, "Asset not found")
		return
	}
	w.Header().Set("Content-Type", asset.MIME)
	disposition := "inline"
	if mediaType(asset.MIME) == "file" {
		disposition = "attachment"
	}
	w.Header().Set("X-Content-Type-Options", "nosniff")
	w.Header().Set("Content-Disposition", mime.FormatMediaType(disposition, map[string]string{"filename": asset.Name}))
	w.Header().Set("Content-Security-Policy", "sandbox; default-src 'none'")
	if count, err := a.store.BlobChunkCount(id); err == nil && count > 0 {
		// Chunked attachments (uploads above 50 MB) stream piece by piece:
		// a multi-hundred-MB video must never sit decrypted in RAM.
		w.Header().Set("Content-Length", strconv.Itoa(asset.Size))
		w.WriteHeader(200)
		for seq := 0; seq < count; seq++ {
			data, err := a.store.BlobChunk(id, seq)
			if err != nil {
				return // headers are already sent; abort the stream
			}
			if _, err = w.Write(data); err != nil {
				return
			}
		}
		return
	}
	data, err := a.store.Blob(id)
	if err != nil {
		jsonError(w, 500, "Could not decrypt attachment")
		return
	}
	w.WriteHeader(200)
	_, _ = w.Write(data)
}
