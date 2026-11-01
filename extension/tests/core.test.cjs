const test = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const core = require("../core.js");

test("remote API requires HTTPS; credentials and misleading loopback hosts are refused", () => {
  assert.equal(core.normalizeApi("http://127.0.0.1:8765/"), "http://127.0.0.1:8765");
  assert.equal(core.normalizeApi("https://notes.example.com/pkm/"), "https://notes.example.com/pkm");
  for (const url of ["http://notes.example.com", "http://127.0.0.1.attacker.test", "https://user:secret@example.com", "https://example.com?token=secret", "file:///tmp/notes", "javascript:alert(1)"]) assert.throws(() => core.normalizeApi(url));
  assert.equal(core.hostPattern("https://notes.example.com:8443/pkm"), "https://notes.example.com/*");
});
test("capture strips URL secrets, tracking and fragments while preserving content query", () => {
  const payload = core.capturePayload({url: "https://www.youtube.com/watch?v=abc&token=secret&utm_source=mail#access_token=secret", title: "Title\u0000", selection: "plain\u202etext", type_id: "book", date: "2026-10-09"});
  assert.deepEqual(payload, {url: "https://www.youtube.com/watch?v=abc", title: "Title", selection: "plaintext", type_id: "book", source: "browser-extension", date: "2026-10-09"});
  for (const url of ["file:///tmp/notes", "javascript:alert(1)", "https://name:password@example.com"]) assert.throws(() => core.capturePayload({url}));
});
test("captured notes are bounded plain strings; unsupported object types become web", () => {
  const payload = core.capturePayload({url: "https://example.com", title: "x".repeat(500), selection: "s".repeat(20000), type_id: "admin"});
  assert.equal(payload.type_id, "web");
  assert.equal(payload.title.length, 300);
  assert.equal(payload.selection.length, 12000);
  assert.equal(payload.date, undefined);
  assert.equal(core.localDate(new Date(2026, 9, 9, 23)), "2026-10-09");
});
test("manifest and source do not grant broad persistent hosts or persist captures and tokens", () => {
  const dir = path.join(__dirname, "..");
  const manifest = JSON.parse(fs.readFileSync(path.join(dir, "manifest.json")));
  assert.deepEqual(manifest.permissions, ["activeTab", "scripting", "storage"]);
  assert.equal(manifest.host_permissions, undefined);
  assert.equal(manifest.content_scripts, undefined);
  const popup = fs.readFileSync(path.join(dir, "popup.js"), "utf8");
  const background = fs.readFileSync(path.join(dir, "background.js"), "utf8");
  assert.equal(popup.includes("storage.sync"), false);
  assert.equal(popup.includes("storage.local.set({api})"), true);
  assert.equal(popup.includes("storage.session.set({connection: {api, token}})"), true);
  assert.equal(popup.includes("innerHTML"), false);
  assert.equal(background.includes('credentials: "omit"'), true);
  assert.equal(background.includes('redirect: "error"'), true);
});
