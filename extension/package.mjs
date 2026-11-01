import {mkdir, readFile, writeFile, copyFile, cp} from "node:fs/promises";
import {fileURLToPath} from "node:url";
import path from "node:path";
const dir = path.dirname(fileURLToPath(import.meta.url));
const chromeManifest = JSON.parse(await readFile(path.join(dir, "manifest.json"), "utf8"));
const firefoxManifest = structuredClone(chromeManifest);
delete firefoxManifest.minimum_chrome_version;
firefoxManifest.background = {scripts: ["core.js", "background.js"]};
firefoxManifest.browser_specific_settings = {gecko: {id: "capture@lumen.local", strict_min_version: "140.0", data_collection_permissions: {required: ["authenticationInfo", "websiteContent"]}}};
for (const [name, manifest] of [["chromium", chromeManifest], ["firefox", firefoxManifest]]) {
  const destination = path.join(dir, "dist", name);
  await mkdir(destination, {recursive: true});
  for (const file of ["core.js", "background.js", "popup.html", "popup.css", "popup.js"]) await copyFile(path.join(dir, file), path.join(destination, file));
  await cp(path.join(dir, "assets"), path.join(destination, "assets"), {recursive: true});
  await writeFile(path.join(destination, "manifest.json"), JSON.stringify(manifest, null, 2) + "\n");
}
console.log("Extensiones preparadas en dist/chromium y dist/firefox; sin dependencias ni red.");
