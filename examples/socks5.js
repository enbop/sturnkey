import { connect, listen, resolve } from "sturnkey:net";
import { environment } from "sturnkey:runtime";
import { BufferedReader } from "../lib/io.js";

const decoder = new TextDecoder();
const port = Number(environment("PORT") ?? "1080");
const maxConnections = Number(environment("STURNKEY_MAX_CONNECTIONS") ?? "Infinity");

async function reply(connection, code) {
  await connection.write(new Uint8Array([5, code, 0, 1, 0, 0, 0, 0, 0, 0]));
}

async function negotiate(reader, client) {
  const greeting = await reader.readExactly(2);
  if (greeting[0] !== 5) throw new Error("unsupported SOCKS version");
  const methods = await reader.readExactly(greeting[1]);
  if (!methods.includes(0)) {
    await client.write(new Uint8Array([5, 255]));
    return false;
  }
  await client.write(new Uint8Array([5, 0]));
  return true;
}

async function readTarget(reader, client) {
  const request = await reader.readExactly(4);
  if (request[0] !== 5 || request[2] !== 0) {
    await reply(client, 1);
    return null;
  }
  if (request[1] !== 1) {
    await reply(client, 7);
    return null;
  }

  let hostname;
  if (request[3] === 1) {
    hostname = Array.from(await reader.readExactly(4)).join(".");
  } else if (request[3] === 3) {
    const length = (await reader.readExactly(1))[0];
    const domain = decoder.decode(await reader.readExactly(length));
    try {
      const addresses = await resolve(domain);
      hostname = addresses.find((address) => address.includes("."));
    } catch {
      await reply(client, 4);
      return null;
    }
    if (hostname === undefined) {
      await reply(client, 4);
      return null;
    }
  } else {
    await reply(client, 8);
    return null;
  }

  const portBytes = await reader.readExactly(2);
  return { hostname, port: (portBytes[0] << 8) | portBytes[1] };
}

function connectReplyCode(error) {
  if (error.code === "ENETUNREACH") return 3;
  if (error.code === "ECONNREFUSED") return 5;
  if (error.code === "ETIMEDOUT") return 6;
  return 1;
}

async function relay(source, destination) {
  try {
    while (true) {
      const chunk = await source.read();
      if (chunk === null) break;
      await destination.write(chunk);
    }
  } finally {
    try {
      await destination.shutdown("write");
    } catch {
      // The other relay may already have observed a reset or closed peer.
    }
  }
}

async function handle(client) {
  let target;
  try {
    const reader = new BufferedReader(client);
    if (!await negotiate(reader, client)) return;
    const endpoint = await readTarget(reader, client);
    if (endpoint === null) return;
    try {
      target = await connect(endpoint);
    } catch (error) {
      await reply(client, connectReplyCode(error));
      return;
    }
    await reply(client, 0);
    await Promise.allSettled([
      relay(reader, target),
      relay(target, client),
    ]);
  } finally {
    if (target !== undefined) {
      try { await target.close(); } catch {}
    }
    try { await client.close(); } catch {}
  }
}

const listener = await listen({ hostname: "127.0.0.1", port });
console.log(`SOCKS5 proxy ready on ${port}`);
const active = new Set();
let accepted = 0;
for await (const client of listener) {
  const task = handle(client);
  active.add(task);
  task.finally(() => active.delete(task));
  if (++accepted === maxConnections) break;
}
await Promise.all(active);
await listener.close();
