"use strict";
if (typeof importScripts === "function") importScripts("core.js");
const ext = globalThis.browser || globalThis.chrome;
const core = globalThis.LumenCore;

async function capture(values) {
  const {connection} = await ext.storage.session.get("connection");
  if (!connection?.token) throw new Error("Conecta tu espacio antes de guardar.");
  const api = core.normalizeApi(connection.api);
  if (!(await ext.permissions.contains({origins: [core.hostPattern(api)]}))) throw new Error("Falta el permiso para conectar con tu servidor. Conecta de nuevo.");
  const payload = core.capturePayload(values);
  let response;
  try {
    response = await fetch(`${api}/v1/capture`, {
      method: "POST",
      headers: {"Content-Type": "application/json", "Authorization": `Bearer ${connection.token}`},
      body: JSON.stringify(payload),
      credentials: "omit",
      redirect: "error",
      cache: "no-store",
      referrerPolicy: "no-referrer",
      signal: AbortSignal.timeout(20000)
    });
  } catch (error) {
    if (error.name === "TimeoutError" || error.name === "AbortError") throw new Error("El servidor tardó demasiado. Comprueba la conexión antes de volver a guardar.");
    throw new Error("No se pudo conectar. Comprueba que el servidor esté encendido, la dirección y los orígenes permitidos.");
  }
  if (!response.ok) throw new Error(core.httpError(response.status));
  let object;
  try { object = await response.json(); } catch { throw new Error("El servidor devolvió una respuesta no válida. La captura podría haberse guardado; comprueba Lumen antes de repetirla."); }
  if (typeof object?.id !== "string") throw new Error("No se pudo confirmar la captura. Comprueba Lumen antes de repetirla.");
  return {id: object.id, title: core.cleanText(object.title, 300), type_id: object.type_id, preview_status: core.cleanText(object.properties?.preview_status, 80), sync_status: core.cleanText(object.sync_status, 80)};
}

ext.runtime.onMessage.addListener((message, sender, sendResponse) => {
  const trusted = sender.id === ext.runtime.id && sender.url === ext.runtime.getURL("popup.html");
  if (!trusted || message?.action !== "capture") return false;
  capture(message.payload).then(
    result => sendResponse({ok: true, result}),
    error => sendResponse({ok: false, error: error.message})
  );
  return true;
});
