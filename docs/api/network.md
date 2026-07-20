# Network API

The `sturnkey:net` module provides Promise-based TCP operations over
`wasi:sockets`. M3 supports numeric IPv4 client addresses; name lookup and IPv6
will be added after the base lifecycle is hardened.

```js
import { connect } from "sturnkey:net";

const connection = await connect({ hostname: "127.0.0.1", port: 9000 });
await connection.write(new TextEncoder().encode("hello"));
const reply = await connection.read();
await connection.close();
```

`read()` returns a `Uint8Array`, or `null` at end-of-stream. `write(data)`
accepts an `ArrayBuffer` or array-buffer view and handles partial writes. Only
one read and one write may be pending on a connection at a time. `close()` is
idempotent, but currently rejects a close attempted while I/O is pending.

Socket failures reject with errors carrying stable codes such as
`ECONNREFUSED`, `ECONNRESET`, `ETIMEDOUT`, or `EACCES`.

`resolve(hostname)` performs asynchronous `wasi:sockets/ip-name-lookup` and
returns numeric IPv4 and IPv6 address strings. `connection.shutdown(direction)`
half-closes `read`, `write`, or `both`, allowing proxy applications to forward
EOF without discarding traffic still flowing in the opposite direction.

Wasmtime must receive explicit TCP and network grants:

```console
wasmtime -S http -S tcp -S inherit-network -S allow-ip-name-lookup \
  sturnkey.wasm client.js
```

`inherit-network` grants the component access to the host network namespace;
it is materially broader than a directory preopen. Name resolution additionally
requires `allow-ip-name-lookup`.

## TCP listeners

`listen({ hostname, port })` asynchronously binds a numeric IPv4 address and
returns a listener. A pending `accept()` keeps the CLI event loop alive. The
listener is also an async iterator, which makes a foreground daemon concise:

```js
import { listen } from "sturnkey:net";

const listener = await listen({ hostname: "127.0.0.1", port: 9000 });
for await (const connection of listener) {
  void handle(connection);
}
```

Only one accept may be pending at a time. `listener.close()` is idempotent and
causes the process to exit naturally after other pending work completes. M4
requires the pending accept to complete before close; cancellation semantics
are deferred to M5.
