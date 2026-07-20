import http from "node:http";
import net from "node:net";
import { spawn } from "node:child_process";

export function reservePort() {
  const server = net.createServer();
  return new Promise((resolve, reject) => {
    server.once("error", reject);
    server.listen(0, "127.0.0.1", () => {
      const port = server.address().port;
      server.close((error) => error ? reject(error) : resolve(port));
    });
  });
}

export function startRuntime({ wasmtime, wasm, script, directories = [], environment = {} }) {
  const args = ["-S", "http", "-S", "tcp", "-S", "inherit-network"];
  for (const directory of directories) args.push("--dir", directory);
  for (const [name, value] of Object.entries(environment)) {
    args.push("--env", `${name}=${value}`);
  }
  args.push(wasm, script);

  const child = spawn(wasmtime, args, { stdio: ["ignore", "pipe", "pipe"] });
  const output = { stdout: "", stderr: "" };
  child.stdout.on("data", (data) => { output.stdout += data; });
  child.stderr.on("data", (data) => { output.stderr += data; });
  return { child, output };
}

export function waitForOutput(child, output, expected, timeoutMs = 10000) {
  return new Promise((resolve, reject) => {
    const timeout = setTimeout(() => reject(new Error(`timed out waiting for ${expected}`)), timeoutMs);
    const inspect = () => {
      if (output.stdout.includes(expected)) {
        clearTimeout(timeout);
        child.stdout.off("data", inspect);
        resolve();
      }
    };
    child.stdout.on("data", inspect);
    child.once("exit", (code) => {
      clearTimeout(timeout);
      reject(new Error(`runtime exited before becoming ready: ${code}\n${output.stderr}`));
    });
    inspect();
  });
}

export function request(port, { method = "GET", path = "/", headers = {}, body } = {}) {
  return new Promise((resolve, reject) => {
    const request = http.request({
      host: "127.0.0.1",
      port,
      method,
      path,
      headers,
      agent: false,
    }, (response) => {
      const chunks = [];
      response.on("data", (chunk) => chunks.push(chunk));
      response.once("end", () => resolve({
        status: response.statusCode,
        headers: response.headers,
        body: Buffer.concat(chunks),
      }));
    });
    request.once("error", reject);
    request.end(body);
  });
}

export function fragmentedRequest(port, fragments) {
  return new Promise((resolve, reject) => {
    const socket = net.createConnection({ host: "127.0.0.1", port });
    const response = [];
    socket.once("error", reject);
    socket.on("data", (data) => response.push(data));
    socket.once("end", () => resolve(Buffer.concat(response).toString()));
    socket.once("connect", async () => {
      for (const fragment of fragments) {
        socket.write(fragment);
        await new Promise((resolve) => setTimeout(resolve, 5));
      }
      socket.end();
    });
  });
}

export async function finishRuntime(child, output, timeoutMs = 10000) {
  const timeout = setTimeout(() => child.kill("SIGKILL"), timeoutMs);
  const code = await new Promise((resolve) => child.once("exit", resolve));
  clearTimeout(timeout);
  process.stdout.write(output.stdout);
  process.stderr.write(output.stderr);
  if (code !== 0) throw new Error(`runtime exited with status ${code}`);
}
