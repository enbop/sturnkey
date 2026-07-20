import { environment } from "sturnkey:runtime";
import { html, serve, text } from "../lib/http.js";

const decoder = new TextDecoder();
const port = Number(environment("PORT") ?? "8000");
const maxConnections = Number(environment("STURNKEY_MAX_CONNECTIONS") ?? "Infinity");

function escapeHtml(value) {
  return value.replaceAll("&", "&amp;")
    .replaceAll("<", "&lt;")
    .replaceAll(">", "&gt;")
    .replaceAll('"', "&quot;")
    .replaceAll("'", "&#39;");
}

await serve({
  hostname: "127.0.0.1",
  port,
  maxConnections,
  onListen: () => console.log(`web app ready on ${port}`),
}, async (request) => {
  if (request.method === "GET" && request.url.pathname === "/") {
    return html(`<!doctype html>
<html><head><title>Sturnkey Web App</title></head>
<body><h1>Sturnkey Web App</h1>
<form method="post" action="/greet"><input name="name"><button>Greet</button></form>
</body></html>`);
  }
  if (request.method === "POST" && request.url.pathname === "/greet") {
    const form = new URLSearchParams(decoder.decode(request.body));
    const name = escapeHtml(form.get("name") ?? "world");
    return html(`<!doctype html><html><body><h1>Hello, ${name}!</h1></body></html>`);
  }
  return text("Not Found\n", 404);
});
