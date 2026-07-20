import { mkdtemp, mkdir, rm, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";
import {
  finishRuntime,
  request,
  reservePort,
  startRuntime,
  waitForOutput,
} from "./http-test-utils.mjs";

const [wasmtime, wasm, sourceRoot] = process.argv.slice(2);
const temporaryRoot = await mkdtemp(join(tmpdir(), "sturnkey-http-files-"));
const publicRoot = join(temporaryRoot, "public");
await mkdir(publicRoot);
await writeFile(join(publicRoot, "index.html"), "<h1>file index</h1>\n");
await writeFile(join(publicRoot, "hello.txt"), "hello from wasi\n");
await writeFile(join(temporaryRoot, "secret.txt"), "not public\n");

try {
  const port = await reservePort();
  const { child, output } = startRuntime({
    wasmtime,
    wasm,
    script: join(sourceRoot, "examples/file-server.js"),
    directories: [sourceRoot, `${publicRoot}::/public`],
    environment: {
      PORT: port,
      STURNKEY_FILE_ROOT: "/public",
      STURNKEY_MAX_CONNECTIONS: 6,
    },
  });
  await waitForOutput(child, output, "file server ready");

  const index = await request(port);
  if (index.status !== 200 || index.body.toString() !== "<h1>file index</h1>\n") {
    throw new Error("index response failed");
  }
  const file = await request(port, { path: "/hello.txt" });
  if (file.status !== 200 || file.headers["content-type"] !== "text/plain; charset=utf-8" ||
      file.body.toString() !== "hello from wasi\n") {
    throw new Error("text file response failed");
  }
  const head = await request(port, { method: "HEAD", path: "/hello.txt" });
  if (head.status !== 200 || head.body.length !== 0 || head.headers["content-length"] !== "16") {
    throw new Error("HEAD response failed");
  }
  if ((await request(port, { path: "/missing" })).status !== 404) {
    throw new Error("missing file response failed");
  }
  if ((await request(port, { method: "POST", path: "/hello.txt" })).status !== 405) {
    throw new Error("method rejection failed");
  }
  if ((await request(port, { path: "/%2e%2e/secret.txt" })).status !== 403) {
    throw new Error("path traversal rejection failed");
  }

  await finishRuntime(child, output);
  if (!output.stdout.includes("file server ready")) throw new Error("missing ready output");
  console.log("HTTP file server test completed");
} finally {
  await rm(temporaryRoot, { recursive: true, force: true });
}
