import { fileURLToPath as diagnosticsFileURLToPath } from 'node:url';
const diagnosticsRepoRoot = diagnosticsFileURLToPath(new URL('../../../', import.meta.url));
process.chdir(diagnosticsRepoRoot);

// Test 32-bit bind() + connect() + listen() + accept()
import { chromium } from "@playwright/test";
import { serve } from "../../../tools/browser/serve.mjs";
import { writeFile, mkdir } from "node:fs/promises";

const TAG = "20261007-bindconnect";
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
  const pycode = "import socket, os, threading; s=socket.socket(socket.AF_UNIX); s.bind('/tmp/bctest.sock'); s.listen(1); print('server_listening'); c=socket.socket(socket.AF_UNIX); c.connect('/tmp/bctest.sock'); print('client_connected'); a, _ = s.accept(); print('server_accepted'); c.send(b'hello'); print('client_sent'); d=a.recv(5); print('server_got:'+d.decode())";
  const url = "http://127.0.0.1:" + server.address().port + "/runtime/index.html?p=python3%20-c%20%22" + encodeURIComponent(pycode) + "%22&arch=x86&session=1&novideo=1&persist=0";
  await page.goto(url);
  await page.waitForTimeout(30000);
  const results = logs.filter(l => l.includes("server_") || l.includes("client_")).join("\n");
  console.log(results);
  await writeFile(dir + "/console.log", logs.join("\n"));
} finally {
  await browser.close();
  server.close();
}
