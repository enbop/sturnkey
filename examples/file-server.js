import { readFile, stat } from "sturnkey:fs";
import { environment } from "sturnkey:runtime";
import { serve, text } from "../lib/http.js";

const root = environment("STURNKEY_FILE_ROOT") ?? "/public";
const port = Number(environment("PORT") ?? "8000");
const maxConnections = Number(environment("STURNKEY_MAX_CONNECTIONS") ?? "Infinity");

const contentTypes = {
  css: "text/css; charset=utf-8",
  html: "text/html; charset=utf-8",
  js: "text/javascript; charset=utf-8",
  json: "application/json; charset=utf-8",
  svg: "image/svg+xml",
  txt: "text/plain; charset=utf-8",
};

function safePath(pathname) {
  let decoded;
  try {
    decoded = decodeURIComponent(pathname);
  } catch {
    return null;
  }
  const segments = decoded.split("/").filter(Boolean);
  if (decoded.includes("\0") || segments.some((segment) => segment === "..")) {
    return null;
  }
  return `${root}/${segments.join("/")}`;
}

await serve({
  hostname: "127.0.0.1",
  port,
  maxConnections,
  onListen: () => console.log(`file server ready on ${port}`),
}, async (request) => {
  if (request.method !== "GET" && request.method !== "HEAD") {
    const response = text("Method Not Allowed\n", 405);
    response.headers.allow = "GET, HEAD";
    return response;
  }
  let path = safePath(request.target.split("?", 1)[0]);
  if (path === null) return text("Forbidden\n", 403);

  try {
    let info = await stat(path);
    if (info.kind === "directory") {
      path = `${path}/index.html`;
      info = await stat(path);
    }
    if (info.kind !== "file") return text("Not Found\n", 404);
    const extension = path.includes(".") ? path.slice(path.lastIndexOf(".") + 1) : "";
    return {
      headers: { "content-type": contentTypes[extension] ?? "application/octet-stream" },
      body: await readFile(path),
    };
  } catch (error) {
    if (error.code === "ENOENT" || error.code === "ENOTDIR") {
      return text("Not Found\n", 404);
    }
    throw error;
  }
});
