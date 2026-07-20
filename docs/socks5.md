# Experimental SOCKS5 proxy

`examples/socks5.js` is an application proof built entirely from dynamic
JavaScript plus `sturnkey:net`. It implements unauthenticated TCP `CONNECT` for
numeric IPv4 and domain-name destinations.

```console
wasmtime -S http -S tcp -S inherit-network -S allow-ip-name-lookup \
  --dir . --env PORT=1080 \
  sturnkey.wasm examples/socks5.js
```

The proxy parses fragmented SOCKS5 messages with `lib/io.js`, resolves domains
through WASI, opens the target connection, and relays both directions
concurrently. EOF is propagated with TCP half-close so the opposite direction
can finish before resources are released.

The proof intentionally omits authentication, IPv6 requests, UDP `ASSOCIATE`,
`BIND`, access policy, timeouts, cancellation, and production resource limits.
