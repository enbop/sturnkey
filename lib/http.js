import { listen } from "sturnkey:net";
import { BufferedReader } from "./io.js";

export { BufferedReader } from "./io.js";

const encoder = new TextEncoder();
const decoder = new TextDecoder();

export class HttpError extends Error {
  constructor(status, message) {
    super(message);
    this.status = status;
  }
}

export async function readRequest(connection, options = {}) {
  const reader = new BufferedReader(connection);
  const headerBytes = await reader.readUntil("\r\n\r\n", options.maxHeaderBytes ?? 16 * 1024);
  const lines = decoder.decode(headerBytes).split("\r\n");
  const [method, target, protocol, extra] = lines.shift().split(" ");
  if (!method || !target || protocol !== "HTTP/1.1" || extra !== undefined) {
    throw new HttpError(400, "invalid HTTP/1.1 request line");
  }

  const headers = Object.create(null);
  for (const line of lines) {
    const separator = line.indexOf(":");
    if (separator <= 0) throw new HttpError(400, "invalid HTTP header");
    const name = line.slice(0, separator).trim().toLowerCase();
    const value = line.slice(separator + 1).trim();
    if (!name || headers[name] !== undefined) {
      throw new HttpError(400, "duplicate or empty HTTP header");
    }
    headers[name] = value;
  }

  if (headers["transfer-encoding"] !== undefined) {
    throw new HttpError(501, "transfer encoding is not supported");
  }
  const contentLengthText = headers["content-length"] ?? "0";
  if (!/^\d+$/.test(contentLengthText)) {
    throw new HttpError(400, "invalid content-length");
  }
  const contentLength = Number(contentLengthText);
  const maxBodyBytes = options.maxBodyBytes ?? 1024 * 1024;
  if (!Number.isSafeInteger(contentLength) || contentLength > maxBodyBytes) {
    throw new HttpError(413, "request body is too large");
  }

  return {
    method,
    target,
    url: new URL(target, `http://${headers.host ?? "localhost"}`),
    protocol,
    headers,
    body: await reader.readExactly(contentLength),
  };
}

const statusText = {
  200: "OK",
  201: "Created",
  204: "No Content",
  400: "Bad Request",
  403: "Forbidden",
  404: "Not Found",
  405: "Method Not Allowed",
  413: "Content Too Large",
  500: "Internal Server Error",
  501: "Not Implemented",
};

function responseBody(body) {
  if (body === undefined || body === null) return new Uint8Array();
  if (typeof body === "string") return encoder.encode(body);
  if (body instanceof Uint8Array) return body;
  if (body instanceof ArrayBuffer) return new Uint8Array(body);
  throw new TypeError("response body must be a string, Uint8Array, or ArrayBuffer");
}

export async function writeResponse(connection, response = {}, method = "GET") {
  const status = response.status ?? 200;
  const reason = statusText[status] ?? "Unknown";
  const body = responseBody(response.body);
  const headers = Object.assign(Object.create(null), response.headers ?? {});
  headers["content-length"] = String(body.length);
  headers.connection = "close";
  if (body.length > 0 && headers["content-type"] === undefined) {
    headers["content-type"] = "application/octet-stream";
  }

  let head = `HTTP/1.1 ${status} ${reason}\r\n`;
  for (const [name, value] of Object.entries(headers)) {
    head += `${name}: ${value}\r\n`;
  }
  await connection.write(encoder.encode(`${head}\r\n`));
  if (method !== "HEAD" && body.length > 0) await connection.write(body);
}

async function handleConnection(connection, handler, options) {
  try {
    const request = await readRequest(connection, options);
    const response = await handler(request);
    await writeResponse(connection, response, request.method);
  } catch (error) {
    const status = error instanceof HttpError ? error.status : 500;
    try {
      await writeResponse(connection, {
        status,
        headers: { "content-type": "text/plain; charset=utf-8" },
        body: status === 500 ? "Internal Server Error\n" : `${error.message}\n`,
      });
    } catch {
      // The peer may have disconnected before an error response was possible.
    }
  } finally {
    await connection.close();
  }
}

export async function serve(options, handler) {
  const listener = await listen({
    hostname: options.hostname ?? "127.0.0.1",
    port: options.port,
  });
  options.onListen?.(listener);

  const active = new Set();
  let accepted = 0;
  for await (const connection of listener) {
    const task = handleConnection(connection, handler, options);
    active.add(task);
    task.finally(() => active.delete(task));
    accepted++;
    if (accepted === options.maxConnections) break;
  }

  await Promise.all(active);
  await listener.close();
}

export function html(body, status = 200) {
  return {
    status,
    headers: { "content-type": "text/html; charset=utf-8" },
    body,
  };
}

export function text(body, status = 200) {
  return {
    status,
    headers: { "content-type": "text/plain; charset=utf-8" },
    body,
  };
}
