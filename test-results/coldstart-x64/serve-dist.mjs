#!/usr/bin/env node
// serve-dist.mjs — serves a built dist directory on 127.0.0.1 for the
// standard live measurement driver, using the project's own static
// server and the same symlink-root shape as web/tests/boot-rate.mjs.
// Usage: node serve-dist.mjs <distDir>   (prints BASE=<url>, stays up)
import { serve } from "../../tools/browser/serve.mjs";
import { mkdtemp, readdir, symlink } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join, resolve } from "node:path";
const DIST = resolve(process.argv[2]);
const root = await mkdtemp(join(tmpdir(), "bw-coldstart-serve-"));
for (const f of await readdir(DIST)) await symlink(join(DIST, f), join(root, f));
const server = await serve(root);
console.log("BASE=http://127.0.0.1:" + server.address().port);
await new Promise(() => {});
