import { fileURLToPath as diagnosticsFileURLToPath } from 'node:url';
const diagnosticsRepoRoot = diagnosticsFileURLToPath(new URL('../../../', import.meta.url));
process.chdir(diagnosticsRepoRoot);

// Test 64-bit bind() on Unix socket (control)
import { chromium } from "@playwright/test";
import { serve } from "../../../tools/browser/serve.mjs";
import { writeFile, mkdir } from "node:fs/promises";

const TAG = "20261007-bind64";
const dir = (diagnosticsRepoRoot + '/test-results/') + TAG;
await mkdir(dir, { recursive: true });
const server = await serve((diagnosticsRepoRoot + '/dist'));
const browser = await chromium.launch({
  executablePath: "/usr/local/bin/chromium", headless: true,
  args: ["--no-sandbox", "--disable-dev-shm-usage"],
});
const page = await browser.newPage();
const logs = [];
page.on("console", m => logs.push(m.type() + ": " + m.text()));
try {
  const pycode = "import socket, os; s=socket.socket(socket.AF_UNIX); s.bind('/tmp/bindtest64.sock'); print('bind_ok'); print('exists:'+str(os.path.exists('/tmp/bindtest64.sock')))";
  const url = "http://127.0.0.1:" + server.address().port + "/runtime/index.html?p=python3%20-c%20%22" + encodeURIComponent(pycode).replace(/%20/g, "%20") + "%22&arch=x86_64&session=1&novideo=1&persist=0";
  await page.goto(url);
  await page.waitForTimeout(30000);
  const bindOk = logs.some(l => l.includes("bind_ok"));
  const exists = logs.some(l => l.includes("exists:True"));
  console.log("64-bit bind_ok:", bindOk, "exists:", exists);
  await writeFile(dir + "/console.log", logs.join("\n"));
} finally {
  await browser.close();
  server.close();
}
