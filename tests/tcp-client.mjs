import net from "node:net";
import { spawn } from "node:child_process";

const [wasmtime, wasm, script] = process.argv.slice(2);

function listen(server) {
  return new Promise((resolve, reject) => {
    server.once("error", reject);
    server.listen(0, "127.0.0.1", () => resolve(server.address().port));
  });
}

const echoServer = net.createServer((socket) => {
  let received = 0;
  socket.on("data", (data) => {
    received += data.length;
    socket.write(data);
    if (received === 256 * 1024) socket.end();
  });
});
const echoPort = await listen(echoServer);

const halfCloseServer = net.createServer((socket) => {
  const chunks = [];
  socket.on("data", (data) => chunks.push(data));
  socket.once("end", () => {
    if (Buffer.concat(chunks).toString() !== "request until eof") {
      socket.destroy(new Error("unexpected half-close request"));
      return;
    }
    socket.end("reply after request eof");
  });
});
const halfClosePort = await listen(halfCloseServer);

const unusedServer = net.createServer();
const refusedPort = await listen(unusedServer);
await new Promise((resolve, reject) =>
  unusedServer.close((error) => error ? reject(error) : resolve()));

const child = spawn(wasmtime, [
  "-S", "http",
  "-S", "tcp",
  "-S", "inherit-network",
  "-S", "allow-ip-name-lookup",
  "--dir", new URL(".", `file://${script}`).pathname,
  "--env", `STURNKEY_ECHO_PORT=${echoPort}`,
  "--env", `STURNKEY_REFUSED_PORT=${refusedPort}`,
  "--env", `STURNKEY_HALF_CLOSE_PORT=${halfClosePort}`,
  wasm,
  script,
], { stdio: ["ignore", "pipe", "pipe"] });

let stdout = "";
let stderr = "";
child.stdout.on("data", (data) => { stdout += data; });
child.stderr.on("data", (data) => { stderr += data; });

const timeout = setTimeout(() => child.kill("SIGKILL"), 15000);
const exitCode = await new Promise((resolve) => child.once("exit", resolve));
clearTimeout(timeout);
await new Promise((resolve, reject) =>
  echoServer.close((error) => error ? reject(error) : resolve()));
await new Promise((resolve, reject) =>
  halfCloseServer.close((error) => error ? reject(error) : resolve()));

process.stdout.write(stdout);
process.stderr.write(stderr);
if (exitCode !== 0) {
  throw new Error(`Sturnkey exited with status ${exitCode}`);
}
if (!stdout.includes("tcp client test completed")) {
  throw new Error("Sturnkey did not complete the TCP client test");
}
