"use strict";
const ext = globalThis.browser || globalThis.chrome;
const core = globalThis.LumenCore;
const $ = id => document.getElementById(id);
let connected = false;
let saving = false;

function status(message, error = false) {
  $("status").textContent = message;
  $("status").classList.toggle("error", error);
  $("status").hidden = !message;
  if (message) requestAnimationFrame(() => $("status").scrollIntoView({block: "nearest"}));
}
function showPanel(settings) {
  $("capture-panel").hidden = settings;
  $("settings-panel").hidden = !settings;
  $("capture-tab").classList.toggle("selected", !settings);
  $("settings-tab").classList.toggle("selected", settings);
  $("capture-tab").setAttribute("aria-pressed", String(!settings));
  $("settings-tab").setAttribute("aria-pressed", String(settings));
  status("");
  $(settings ? "api-token" : "title").focus();
}
function setConnected(value) {
  connected = value;
  $("connection-pill").textContent = value ? "Sesión conectada" : "Sin conectar";
  $("connection-pill").classList.toggle("connected", value);
  $("disconnect-button").hidden = !value;
  $("save-button").disabled = !value || saving;
}
function siteName() {
  try { $("site-name").textContent = new URL($("url").value).hostname.replace(/^www\./, "").toUpperCase(); }
  catch { $("site-name").textContent = "ENLACE WEB"; }
}

$("capture-tab").addEventListener("click", () => showPanel(false));
$("settings-tab").addEventListener("click", () => showPanel(true));
$("url").addEventListener("input", siteName);
$("extension-origin").value = ext.runtime.getURL("").replace(/\/$/, "");

$("connection-form").addEventListener("submit", async event => {
  event.preventDefault();
  status("");
  let api, token;
  try {
    api = core.normalizeApi($("api-url").value);
    token = $("api-token").value.trim();
    if (!token || token.length > 4096 || /[\r\n\u0000]/.test(token)) throw new Error("Introduce un token válido.");
    if (!ext.storage.session) throw new Error("Tu navegador debe admitir storage.session. Actualízalo para mantener el token en memoria.");
    // The permission request is the first asynchronous call, preserving the user gesture.
    const allowed = await ext.permissions.request({origins: [core.hostPattern(api)]});
    if (!allowed) throw new Error("No se concedió permiso para conectar con ese servidor.");
    const previous = await ext.storage.session.get("connection");
    await ext.storage.session.set({connection: {api, token}});
    await ext.storage.local.set({api});
    if (previous.connection?.api && core.hostPattern(previous.connection.api) !== core.hostPattern(api)) await ext.permissions.remove({origins: [core.hostPattern(previous.connection.api)]});
    $("api-token").value = "";
    $("api-url").value = api;
    setConnected(true);
    showPanel(false);
    status("Sesión preparada. La conexión se verificará al guardar.");
  } catch (error) { status(error.message, true); }
});

$("disconnect-button").addEventListener("click", async () => {
  const {connection} = await ext.storage.session.get("connection");
  await ext.storage.session.remove("connection");
  if (connection?.api) await ext.permissions.remove({origins: [core.hostPattern(connection.api)]});
  $("api-token").value = "";
  setConnected(false);
  status("Token olvidado y permiso del servidor retirado.");
});

$("capture-form").addEventListener("submit", async event => {
  event.preventDefault();
  if (saving) return;
  if (!connected) { showPanel(true); status("Conecta tu espacio para guardar."); return; }
  let payload;
  try {
    payload = core.capturePayload({
      url: $("url").value, title: $("title").value, selection: $("selection").value,
      type_id: document.querySelector('input[name="type_id"]:checked').value,
      date: $("journal").checked ? core.localDate() : undefined
    });
  } catch (error) { status(error.message, true); return; }
  saving = true;
  setConnected(connected);
  $("save-label").textContent = "Guardando…";
  status("");
  try {
    const response = await ext.runtime.sendMessage({action: "capture", payload});
    if (!response?.ok) throw new Error(response?.error || "No se pudo confirmar la captura.");
    $("selection").value = "";
    status(payload.date ? "Guardado en tu espacio y enlazado al diario de hoy." : "Guardado en tu espacio.");
    if (["error", "unavailable"].includes(response.result?.preview_status)) status("Enlace guardado. El sitio no permitió completar su vista previa.");
    if (response.result?.sync_status === "pending") status("Guardado en el servidor. La sincronización con Logseq sigue pendiente.");
    $("save-label").textContent = "Guardado ✓";
  } catch (error) {
    status(error.message, true);
    $("save-label").textContent = "Volver a intentar";
  } finally { saving = false; setConnected(connected); }
});

document.addEventListener("keydown", event => {
  if (event.key === "Escape") window.close();
  if ((event.ctrlKey || event.metaKey) && event.key === "Enter") {
    event.preventDefault();
    $($("settings-panel").hidden ? "capture-form" : "connection-form").requestSubmit();
  }
});

async function initialize() {
  try {
    const {api} = await ext.storage.local.get("api");
    $("api-url").value = api || core.DEFAULT_API;
    if (!ext.storage.session) throw new Error("Actualiza tu navegador: esta extensión requiere storage.session.");
    const {connection} = await ext.storage.session.get("connection");
    setConnected(Boolean(connection?.token));
    const [tab] = await ext.tabs.query({active: true, currentWindow: true});
    if (tab?.url && /^https?:/.test(tab.url)) {
      $("url").value = core.cleanWebUrl(tab.url);
      $("title").value = core.cleanText(tab.title, 300);
      try {
        const results = await ext.scripting.executeScript({
          target: {tabId: tab.id},
          func: () => window.getSelection()?.toString().slice(0, 12000) || ""
        });
        $("selection").value = core.cleanText(results[0]?.result, 12000);
      } catch { /* Restricted pages can still be saved as a URL. */ }
      siteName();
    }
    showPanel(!connected);
  } catch (error) { setConnected(false); showPanel(true); status(error.message, true); }
}
initialize();
