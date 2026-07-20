import { join } from "node:path";
import {
  finishRuntime,
  fragmentedRequest,
  request,
  reservePort,
  startRuntime,
  waitForOutput,
} from "./http-test-utils.mjs";

const [wasmtime, wasm, sourceRoot] = process.argv.slice(2);
const port = await reservePort();
const { child, output } = startRuntime({
  wasmtime,
  wasm,
  script: join(sourceRoot, "examples/web-app.js"),
  directories: [sourceRoot],
  environment: { PORT: port, STURNKEY_MAX_CONNECTIONS: 3 },
});
await waitForOutput(child, output, "web app ready");

const home = await request(port);
if (home.status !== 200 || !home.body.toString().includes("Sturnkey Web App")) {
  throw new Error("home route failed");
}

const greeting = await fragmentedRequest(port, [
  "POST /greet HTTP/1.1\r\nHost: 127.0.0.1\r\nCont",
  "ent-Type: application/x-www-form-urlencoded\r\nContent-Length: 20\r\n\r\nname=%3C",
  "WASI%3E+User",
]);
if (!greeting.startsWith("HTTP/1.1 200 OK\r\n") ||
    !greeting.includes("Hello, &lt;WASI&gt; User!")) {
  throw new Error("form route or HTML escaping failed");
}

if ((await request(port, { path: "/missing" })).status !== 404) {
  throw new Error("not-found route failed");
}

await finishRuntime(child, output);
console.log("HTTP web app test completed");
