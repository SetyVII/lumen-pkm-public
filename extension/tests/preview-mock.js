/* VISUAL FIXTURE ONLY: no real API, token or browser access. */
globalThis.chrome = {
  runtime: {
    id: "visual-preview",
    getURL: file => `chrome-extension://visual-preview/${file}`,
    sendMessage: async () => ({ok: true, result: {id: "visual-fixture", preview_status: "ready"}})
  },
  storage: {
    local: {get: async () => ({api: "http://127.0.0.1:8765"}), set: async () => {}},
    session: {get: async () => ({connection: {api: "http://127.0.0.1:8765", token: "VISUAL_FIXTURE_NOT_A_TOKEN"}}), set: async () => {}, remove: async () => {}}
  },
  tabs: {query: async () => [{id: 1, url: "https://example.com/lecturas/aprender", title: "Cómo aprender cosas difíciles"}]},
  scripting: {executeScript: async () => [{result: "Aprender es conectar una idea nueva con algo que ya comprendemos."}]},
  permissions: {request: async () => true, remove: async () => true}
};
document.addEventListener("DOMContentLoaded", () => {
  document.getElementById("footer-context").textContent = "Vista previa · datos ficticios";
});
