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

Wasmtime must receive explicit TCP and network grants:

```console
wasmtime -S http -S tcp -S inherit-network sturnkey.wasm client.js
```

`inherit-network` grants the component access to the host network namespace;
it is materially broader than a directory preopen.
