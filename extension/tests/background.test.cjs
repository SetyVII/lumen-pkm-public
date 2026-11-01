const test = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");

function worker({connection = {api: "https://notes.example.com", token: "test-token"}, permission = true, fetchImpl} = {}) {
  const calls = [];
  let listener;
  const ext = {
    runtime: {
      id: "extension-test",
      getURL: file => `chrome-extension://extension-test/${file}`,
      onMessage: {addListener: fn => { listener = fn; }}
    },
    storage: {session: {get: async () => ({connection})}},
    permissions: {contains: async () => permission}
  };
  const context = vm.createContext({
    browser: ext, URL, AbortSignal,
    fetch: async (url, options) => {
      calls.push({url, options});
      return fetchImpl ? fetchImpl(url, options) : {ok: true, status: 201, json: async () => ({id: "capture-id", title: "Saved title", type_id: "web", properties: {preview_status: "ok"}})};
    }
  });
  vm.runInContext(fs.readFileSync(path.join(__dirname, "../core.js"), "utf8"), context);
  vm.runInContext(fs.readFileSync(path.join(__dirname, "../background.js"), "utf8"), context);
  return {
    calls, listener,
    send: payload => new Promise(resolve => listener({action: "capture", payload}, {id: ext.runtime.id, url: ext.runtime.getURL("popup.html")}, resolve))
  };
}

test("trusted popup sends a bounded capture with session token and no browser credentials", async () => {
  const runner = worker();
  const result = await runner.send({url: "https://article.example.com?utm_source=mail#private", title: "A title", selection: "My note", date: "2026-10-09", type_id: "book"});
  assert.equal(result.ok, true);
  assert.equal(result.result.id, "capture-id");
  assert.equal(runner.calls.length, 1);
  const call = runner.calls[0];
  assert.equal(call.url, "https://notes.example.com/v1/capture");
  assert.equal(call.options.headers.Authorization, "Bearer test-token");
  assert.equal(call.options.credentials, "omit");
  assert.equal(call.options.redirect, "error");
  assert.equal(call.options.referrerPolicy, "no-referrer");
  assert.equal(call.options.cache, "no-store");
  assert.equal(JSON.parse(call.options.body).url, "https://article.example.com/");
  assert.equal(JSON.parse(call.options.body).type_id, "book");
});
test("external pages cannot request a capture", () => {
  const runner = worker();
  assert.equal(runner.listener({action: "capture", payload: {url: "https://example.com"}}, {id: "extension-test", url: "https://attacker.example"}, () => assert.fail("response to external page")), false);
  assert.equal(runner.calls.length, 0);
});
test("missing session, revoked permission and tampered remote HTTP fail before network", async () => {
  for (const options of [{connection: {}}, {permission: false}, {connection: {api: "http://remote.example.com", token: "test-token"}}]) {
    const runner = worker(options);
    const result = await runner.send({url: "https://article.example.com"});
    assert.equal(result.ok, false);
    assert.equal(runner.calls.length, 0);
  }
});
test("invalid token returns a useful error without server response content", async () => {
  const runner = worker({fetchImpl: async () => ({ok: false, status: 401, text: async () => "sensitive-server-details"})});
  const result = await runner.send({url: "https://article.example.com"});
  assert.equal(result.ok, false);
  assert.match(result.error, /token/);
  assert.doesNotMatch(result.error, /sensitive-server-details/);
});
test("successful local save retains the pending Logseq state for the popup", async () => {
  const runner = worker({fetchImpl: async () => ({ok: true, status: 201, json: async () => ({id: "capture-id", title: "Saved", type_id: "web", sync_status: "pending", properties: {preview_status: "unavailable"}})})});
  const result = await runner.send({url: "https://article.example.com"});
  assert.equal(result.ok, true);
  assert.equal(result.result.sync_status, "pending");
  assert.equal(result.result.preview_status, "unavailable");
});
