import net from "node:net";
import { join } from "node:path";
import {
  finishRuntime,
  reservePort,
  startRuntime,
  waitForOutput,
} from "./http-test-utils.mjs";

class SocketReader {
  #buffer = Buffer.alloc(0);
  #ended = false;
  #waiting = [];

  constructor(socket) {
    socket.on("data", (data) => {
      this.#buffer = Buffer.concat([this.#buffer, data]);
      this.#notify();
    });
    socket.once("end", () => {
      this.#ended = true;
      this.#notify();
    });
  }

  #notify() {
    for (const resolve of this.#waiting.splice(0)) resolve();
  }

  #wait() {
    return new Promise((resolve) => this.#waiting.push(resolve));
  }

  async readExactly(length) {
    while (this.#buffer.length < length) {
      if (this.#ended) throw new Error("unexpected SOCKS response EOF");
      await this.#wait();
    }
    const result = this.#buffer.subarray(0, length);
    this.#buffer = this.#buffer.subarray(length);
    return result;
  }

  async readToEnd() {
    while (!this.#ended) await this.#wait();
    return this.#buffer;
  }
}

function connectSocket(port) {
  return new Promise((resolve, reject) => {
    const socket = net.createConnection({ host: "127.0.0.1", port });
    socket.once("error", reject);
    socket.once("connect", () => resolve(socket));
  });
}

async function socksConnect(proxyPort, targetPort, address, fragmented = false) {
  const socket = await connectSocket(proxyPort);
  const reader = new SocketReader(socket);
  if (fragmented) {
    socket.write(Buffer.from([5]));
    await new Promise((resolve) => setTimeout(resolve, 5));
    socket.write(Buffer.from([1, 0]));
  } else {
    socket.write(Buffer.from([5, 1, 0]));
  }
  const greeting = await reader.readExactly(2);
  if (!greeting.equals(Buffer.from([5, 0]))) throw new Error("SOCKS greeting failed");

  let encodedAddress;
  if (address === "127.0.0.1") {
    encodedAddress = Buffer.from([1, 127, 0, 0, 1]);
  } else {
    const domain = Buffer.from(address);
    encodedAddress = Buffer.concat([Buffer.from([3, domain.length]), domain]);
  }
  socket.write(Buffer.concat([
    Buffer.from([5, 1, 0]),
    encodedAddress,
    Buffer.from([targetPort >> 8, targetPort & 255]),
  ]));
  const reply = await reader.readExactly(10);
  if (reply[1] !== 0) throw new Error(`SOCKS CONNECT failed with ${reply[1]}`);
  return { socket, reader };
}

async function relayTest(proxyPort, targetPort, address, message, fragmented) {
  const { socket, reader } = await socksConnect(
    proxyPort, targetPort, address, fragmented);
  socket.end(message);
  const response = await reader.readToEnd();
  if (response.toString() !== message) {
    throw new Error(`unexpected relayed response: ${response}`);
  }
}

const [wasmtime, wasm, sourceRoot] = process.argv.slice(2);
const targetServer = net.createServer((socket) => socket.pipe(socket));
const targetPort = await new Promise((resolve, reject) => {
  targetServer.once("error", reject);
  targetServer.listen(0, "127.0.0.1", () => resolve(targetServer.address().port));
});
const proxyPort = await reservePort();
const { child, output } = startRuntime({
  wasmtime,
  wasm,
  script: join(sourceRoot, "examples/socks5.js"),
  directories: [sourceRoot],
  environment: { PORT: proxyPort, STURNKEY_MAX_CONNECTIONS: 3 },
  wasiOptions: ["allow-ip-name-lookup"],
});
await waitForOutput(child, output, "SOCKS5 proxy ready");

await Promise.all([
  relayTest(proxyPort, targetPort, "127.0.0.1", "IPv4 through WASI", true),
  relayTest(proxyPort, targetPort, "localhost", "domain through WASI", false),
]);

const unsupported = await connectSocket(proxyPort);
const unsupportedReader = new SocketReader(unsupported);
unsupported.write(Buffer.from([5, 1, 0]));
await unsupportedReader.readExactly(2);
unsupported.write(Buffer.from([5, 2, 0, 1, 127, 0, 0, 1, 0, 80]));
const unsupportedReply = await unsupportedReader.readExactly(10);
if (unsupportedReply[1] !== 7) throw new Error("unsupported command did not return code 7");
unsupported.end();
await unsupportedReader.readToEnd();

await finishRuntime(child, output);
await new Promise((resolve, reject) =>
  targetServer.close((error) => error ? reject(error) : resolve()));
console.log("SOCKS5 proxy test completed");
