/* Shared validation: no remote scripts, HTML rendering or persistent capture cache. */
(function (root) {
  "use strict";
  const DEFAULT_API = "http://127.0.0.1:8765";
  function cleanText(value, limit = 12000) {
    if (typeof value !== "string") return "";
    return value.normalize("NFC").replace(/[\u0000-\u0008\u000b\u000c\u000e-\u001f\u007f\u202a-\u202e\u2066-\u2069]/g, "").trim().slice(0, limit);
  }
  function normalizeApi(value) {
    let url;
    try { url = new URL(value.trim()); } catch { throw new Error("Escribe una dirección válida para tu servidor."); }
    if (url.username || url.password || url.search || url.hash) throw new Error("La dirección del servidor no puede incluir credenciales, parámetros ni fragmentos.");
    const loopback = ["127.0.0.1", "localhost"].includes(url.hostname);
    if (url.protocol !== "https:" && !(url.protocol === "http:" && loopback)) {
      throw new Error("Usa HTTPS para un servidor remoto. HTTP solo se admite en localhost o 127.0.0.1.");
    }
    return url.href.replace(/\/+$/, "");
  }
  function hostPattern(api) {
    const url = new URL(normalizeApi(api));
    return `${url.protocol}//${url.hostname}/*`;
  }
  function cleanWebUrl(value) {
    let url;
    try { url = new URL(value.trim()); } catch { throw new Error("El enlace debe ser una dirección web válida."); }
    if (!["https:", "http:"].includes(url.protocol)) throw new Error("Solo puedes guardar enlaces HTTP o HTTPS.");
    if (url.username || url.password) throw new Error("No guardes enlaces que incluyan usuario o contraseña.");
    url.hash = "";
    for (const key of [...url.searchParams.keys()]) {
      if (/^(utm_.+|fbclid|gclid|mc_cid|mc_eid|access_token|token|api_key|apikey|authorization|password|secret)$/i.test(key)) url.searchParams.delete(key);
    }
    if (url.href.length > 4096) throw new Error("El enlace es demasiado largo.");
    return url.href;
  }
  function localDate(date = new Date()) {
    return `${date.getFullYear()}-${String(date.getMonth() + 1).padStart(2, "0")}-${String(date.getDate()).padStart(2, "0")}`;
  }
  function capturePayload(values) {
    const payload = {
      url: cleanWebUrl(values.url),
      title: cleanText(values.title, 300),
      selection: cleanText(values.selection, 12000),
      type_id: values.type_id === "book" ? "book" : "web",
      source: "browser-extension"
    };
    if (values.date) {
      if (!/^\d{4}-\d{2}-\d{2}$/.test(values.date) || Number.isNaN(Date.parse(values.date))) throw new Error("La fecha del diario no es válida.");
      payload.date = values.date;
    }
    return payload;
  }
  function httpError(status) {
    if (status === 401) return "El token no es válido. Conecta de nuevo con el token de tu servidor.";
    if (status === 403) return "El servidor rechazó el acceso. Comprueba el token y LUMEN_ALLOWED_ORIGINS.";
    if (status === 413) return "La captura supera el tamaño permitido por el servidor.";
    if (status === 429) return "El servidor recibió demasiadas peticiones. Prueba en unos segundos.";
    return `El servidor no pudo completar la captura (HTTP ${status}).`;
  }
  root.LumenCore = Object.freeze({DEFAULT_API, cleanText, normalizeApi, hostPattern, cleanWebUrl, localDate, capturePayload, httpError});
  if (typeof module !== "undefined" && module.exports) module.exports = root.LumenCore;
})(globalThis);
