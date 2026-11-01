/* Isolated visual fixture. Serves only extension assets on loopback; never uses the API. */
const http = require("node:http");
const fs = require("node:fs");
const path = require("node:path");
const base = path.join(__dirname, "..");
const mime = {".html": "text/html; charset=utf-8", ".js": "text/javascript; charset=utf-8", ".css": "text/css; charset=utf-8", ".otf": "font/otf"};
http.createServer((req, res) => {
  const pathname = new URL(req.url, "http://localhost").pathname;
  if (pathname === "/preview") {
    res.writeHead(200, {"Content-Type": mime[".html"], "Cache-Control": "no-store"});
    const html = fs.readFileSync(path.join(base, "popup.html"), "utf8").replace('<script src="core.js" defer>', '<script src="tests/preview-mock.js" defer></script>\n  <script src="core.js" defer>');
    res.end(html);
    return;
  }
  const relative = pathname.slice(1);
  const allowed = ["popup.css", "core.js", "popup.js", "tests/preview-mock.js", "assets/fonts/Inter-Regular.otf", "assets/fonts/Inter-Medium.otf", "assets/fonts/Inter-SemiBold.otf"];
  if (!allowed.includes(relative)) {res.writeHead(404); res.end(); return;}
  res.writeHead(200, {"Content-Type": mime[path.extname(relative)], "Cache-Control": "no-store"});
  fs.createReadStream(path.join(base, relative)).pipe(res);
}).listen(8789, "127.0.0.1", () => console.log("Vista previa aislada: http://127.0.0.1:8789/preview (datos ficticios, sin API)."));
