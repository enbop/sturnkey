import net from "node:net";
import { spawn } from "node:child_process";

const [wasmtime, wasm, script] = process.argv.slice(2);

function listen(server) {
  return new Promise((resolve, reject) => {
    server.once("error", reject);
    server.listen(0, "127.0.0.1", () => resolve(server.address().port));
  });
}

function echoClient(port, message) {
  return new Promise((resolve, reject) => {
    const socket = net.createConnection({ host: "127.0.0.1", port });
    const chunks = [];
    socket.once("error", reject);
    socket.on("data", (data) => chunks.push(data));
    socket.once("end", () => {
      const reply = Buffer.concat(chunks).toString();
      if (reply !== message) reject(new Error(`unexpected echo: ${reply}`));
      else resolve();
    });
    socket.end(message);
  });
}

function disconnectClient(port) {
  return new Promise((resolve, reject) => {
    const socket = net.createConnection({ host: "127.0.0.1", port });
    socket.once("error", reject);
    socket.once("connect", () => socket.end(resolve));
  });
}

const reservation = net.createServer();
const port = await listen(reservation);
await new Promise((resolve, reject) =>
  reservation.close((error) => error ? reject(error) : resolve()));

const child = spawn(wasmtime, [
  "-S", "http",
  "-S", "tcp",
  "-S", "inherit-network",
  "--dir", new URL(".", `file://${script}`).pathname,
  "--env", `STURNKEY_LISTEN_PORT=${port}`,
  wasm,
  script,
], { stdio: ["ignore", "pipe", "pipe"] });

let stdout = "";
let stderr = "";
child.stdout.on("data", (data) => { stdout += data; });
child.stderr.on("data", (data) => { stderr += data; });

await new Promise((resolve, reject) => {
  const timeout = setTimeout(() => reject(new Error("listener did not start")), 10000);
  child.stdout.on("data", () => {
    if (stdout.includes("tcp listener ready")) {
      clearTimeout(timeout);
      resolve();
    }
  });
  child.once("exit", (code) => reject(new Error(`listener exited early: ${code}`)));
});

await Promise.all([
  echoClient(port, "first"),
  echoClient(port, "second"),
  disconnectClient(port),
]);

const timeout = setTimeout(() => child.kill("SIGKILL"), 15000);
const exitCode = await new Promise((resolve) => child.once("exit", resolve));
clearTimeout(timeout);
process.stdout.write(stdout);
process.stderr.write(stderr);
if (exitCode !== 0) throw new Error(`Sturnkey exited with status ${exitCode}`);
if (!stdout.includes("tcp listener test completed")) {
  throw new Error("Sturnkey did not complete the TCP listener test");
}
