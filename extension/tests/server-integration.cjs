/* Opt-in integration: real HTTP API, mocked WebExtension APIs. Never prints the token. */
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");
const core = require("../core.js");

(async () => {
  const tokenFile = process.env.LUMEN_TEST_TOKEN_FILE;
  if (!tokenFile) throw new Error("Define LUMEN_TEST_TOKEN_FILE con la ruta del archivo de token.");
  const api = core.normalizeApi(process.env.LUMEN_TEST_API || core.DEFAULT_API);
  const token = fs.readFileSync(tokenFile, "utf8").trim();
  let date;
  let journalID;
  let objectID;
  let journalExisted;
  const headers = {Authorization: `Bearer ${token}`, "Content-Type": "application/json"};
  const apiFetch = async (route, options = {}) => fetch(`${api}${route}`, {cache: "no-store", credentials: "omit", redirect: "error", ...options, headers: {...headers, ...options.headers}});
  try {
    // Never add links to an existing journal: use an unused date reserved for
    // this invocation, then remove only the objects the invocation created.
    for (let attempt = 0; attempt < 96; attempt++) {
      const candidate = new Date(Date.UTC(2090 + Math.floor(Math.random() * 10), Math.floor(Math.random() * 12), 1 + Math.floor(Math.random() * 28))).toISOString().slice(0, 10);
      const before = await apiFetch(`/v1/objects/journal-${candidate}`);
      assert.equal([200, 404].includes(before.status), true);
      if (before.status === 404) { date = candidate; journalID = `journal-${date}`; journalExisted = false; break; }
    }
    assert.ok(date, "No se encontró una fecha de diario temporal libre.");
    let listener;
    const ext = {
      runtime: {id: "lumen-integration", getURL: file => `chrome-extension://lumen-integration/${file}`, onMessage: {addListener: fn => {listener = fn;}}},
      storage: {session: {get: async () => ({connection: {api, token}})}},
      permissions: {contains: async () => true}
    };
    const context = vm.createContext({browser: ext, URL, AbortSignal, fetch});
    for (const filename of ["core.js", "background.js"]) vm.runInContext(fs.readFileSync(path.join(__dirname, "..", filename), "utf8"), context);
    const payload = {
      url: "https://example.com/?utm_source=lumen-extension-test#private-fragment",
      title: "Prueba temporal de captura de extensión",
      selection: "Texto de prueba: captura al servidor real, sin datos personales.",
      type_id: "web", date, source: "browser-extension"
    };
    const reply = await new Promise(resolve => listener({action: "capture", payload}, {id: ext.runtime.id, url: ext.runtime.getURL("popup.html")}, resolve));
    assert.equal(reply.ok, true, reply.error);
    objectID = reply.result.id;
    const savedResponse = await apiFetch(`/v1/objects/${objectID}`);
    assert.equal(savedResponse.status, 200);
    const saved = await savedResponse.json();
    assert.equal(saved.title, payload.title);
    assert.equal(saved.content, payload.selection);
    assert.equal(saved.type_id, "web");
    assert.equal(saved.properties.url, "https://example.com/");
    assert.equal(saved.properties.source, "browser-extension");
    const journalResponse = await apiFetch(`/v1/objects/${journalID}`);
    assert.equal(journalResponse.status, 200);
    const journal = await journalResponse.json();
    assert.equal(journal.links.includes(objectID), true);
    const anonymous = await fetch(`${api}/v1/objects/${objectID}`);
    assert.equal(anonymous.status, 401);
    const origin = "chrome-extension://lumen-integration";
    const cors = await fetch(`${api}/v1/capture`, {method: "OPTIONS", headers: {Origin: origin, "Access-Control-Request-Method": "POST", "Access-Control-Request-Headers": "authorization,content-type"}});
    console.log(JSON.stringify({
      capture: "passed", authenticated_readback: "passed", journal_link: "passed", anonymous_access: "rejected",
      preview_status: saved.properties.preview_status || "none", sync_status: saved.sync_status || "not_reported", simulated_extension_origin_status: cors.status,
      browser_installation_tested: false,
      note: "Worker en VM con fetch real; no valida permisos, instalación ni CORS de un navegador real."
    }, null, 2));
  } finally {
    if (objectID) {
      const cleanup = await apiFetch(`/v1/objects/${objectID}`, {method: "DELETE"});
      assert.equal(cleanup.ok, true, "No se pudo limpiar la captura temporal.");
    }
    if (journalExisted === false) {
      const cleanup = await apiFetch(`/v1/objects/${journalID}`, {method: "DELETE"});
      assert.equal(cleanup.ok, true, "No se pudo limpiar el diario temporal.");
    }
  }
})().catch(error => { console.error(error.message); process.exitCode = 1; });
