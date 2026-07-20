# Experimental HTTP applications

Sturnkey's first HTTP server layer is ordinary dynamic JavaScript built on
`sturnkey:net`; it does not add an HTTP-specific native builtin. The reusable
module lives at `lib/http.js` and currently exports:

- `BufferedReader`, with bounded `readExactly()` and `readUntil()` operations;
- `readRequest()` and `writeResponse()` for one HTTP/1.1 request per TCP
  connection;
- `serve(options, handler)` for a foreground TCP listener;
- `html()` and `text()` response helpers.

This is a deliberately small proof layer. It supports `Content-Length` request
bodies and closes each connection after one response. Chunked transfer coding,
keep-alive, streaming responses, upgrade, cancellation, and production-grade
HTTP parsing remain out of scope.

## Static file server

```console
wasmtime -S http -S tcp -S inherit-network \
  --dir . --dir ./public::/public \
  --env PORT=8000 --env STURNKEY_FILE_ROOT=/public \
  sturnkey.wasm examples/file-server.js
```

The example supports `GET` and `HEAD`, directory index files, basic MIME types,
404/405 responses, and rejects decoded `..` path segments.

## Server-rendered Web App

```console
wasmtime -S http -S tcp -S inherit-network \
  --dir . --env PORT=8000 \
  sturnkey.wasm examples/web-app.js
```

The Web App renders HTML, routes GET and POST requests, parses an
`application/x-www-form-urlencoded` body, and escapes user-controlled output.
It demonstrates that a small SSR application can remain dynamic JavaScript
while the runtime and application both execute inside Wasmtime.
