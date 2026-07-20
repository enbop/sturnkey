# Sturnkey implementation roadmap

## Goal

Sturnkey aims to make dynamic JavaScript useful for CLI tools, long-running
daemons, and small local web applications inside a stock Wasmtime sandbox.
Applications remain JavaScript files at deployment time:

```console
wasmtime [capability flags] sturnkey.wasm main.js
```

The runtime is built downstream from StarlingMonkey. Its application APIs are
inspired by Deno's async-first design, but are independently specified and do
not promise Deno, Node.js, or npm compatibility.

The primary usability milestone is running a TCP echo server, a static file
server, and a SOCKS5 proxy as dynamic JavaScript under `wasi:cli/run`.

## Principles

- Treat `wasi:cli/run` as the application lifetime, including for foreground
  daemons.
- Keep capability grants visible in the Wasmtime command line.
- Build one event-loop and resource-lifetime model shared by every I/O API.
- Keep native C++ focused on SpiderMonkey, Promise, resource, pollable, and
  WASI bridging.
- Implement protocols and convenience helpers in JavaScript where practical.
- Stabilize APIs through working applications before expanding their surface.
- Terminate inbound TLS outside Sturnkey initially; add outbound HTTPS later.

## Milestones

### M1: CLI asynchronous lifecycle

Establish the execution model required by every later I/O API.

Deliverables:

- keep `wasi:cli/run` alive while registered asynchronous work exists;
- complete top-level `await` backed by Sturnkey asynchronous operations;
- exit naturally after the last operation finishes;
- report rejected top-level operations as failures;
- expose a minimal Promise-based `sleep(milliseconds)` API;
- test successful waits, zero-duration waits, invalid arguments, and failures.

Initial API:

```js
import { sleep, version } from "sturnkey:runtime";

await sleep(100);
console.log(version);
```

M1 is complete when this program waits, prints, and exits without relying on an
HTTP request handler.

### M2: Filesystem

Status: implemented and covered by a capability-scoped dynamic test.

Expose the smallest useful capability-based filesystem API:

```js
import {
  mkdir,
  readDir,
  readFile,
  readTextFile,
  remove,
  stat,
  writeFile,
  writeTextFile,
} from "sturnkey:fs";
```

Requirements:

- restrict access to Wasmtime preopened directories;
- support bytes and UTF-8 text without hidden host paths;
- map WASI errors to stable JavaScript errors with machine-readable codes;
- close descriptors and streams deterministically;
- cover file copying and recursive directory listing with examples.

### M3: TCP client

Status: implemented for numeric IPv4, including partial I/O, EOF, connection
errors, and deterministic close.

Add the required `wasi:sockets` interfaces to the component world and expose a
Promise-based client API:

```js
import { connect } from "sturnkey:net";

const connection = await connect({ hostname: "127.0.0.1", port: 9000 });
await connection.write(bytes);
const reply = await connection.read();
await connection.close();
```

Start with IP addresses, then add WASI name lookup. Cover partial writes,
end-of-stream, connection errors, timeouts, and deterministic cleanup.

### M4: TCP listener and daemon lifecycle

Status: implemented for numeric IPv4 with `accept()` and async iteration.

Expose a listener that owns event-loop interest for its lifetime:

```js
import { listen } from "sturnkey:net";

const listener = await listen({ hostname: "127.0.0.1", port: 9000 });

for await (const connection of listener) {
  void handle(connection);
}
```

Sturnkey daemons remain foreground processes. Service managers such as systemd
or launchd are responsible for detaching, restart policy, logs, and process
supervision.

M4 is complete when a dynamic JavaScript TCP echo server accepts sequential and
concurrent connections, survives client failures, and exits after its listener
is closed.

### M5: Networking semantics

Harden the API before building larger applications:

- backpressure and partial writes;
- half-close behavior;
- concurrent read, write, and accept rules;
- idempotent close operations;
- listener-close behavior for pending accepts;
- timeouts and `AbortSignal`-style cancellation;
- resource limits and garbage-collection fallback cleanup.

### M6: Application proofs

Build three dynamic JavaScript applications:

1. A TCP echo server covering the complete listener lifecycle.
2. A static file server using the filesystem and a small HTTP/1.1
   implementation over TCP.
3. A SOCKS5 proxy supporting unauthenticated TCP `CONNECT`, IPv4, domain-name
   lookup, bidirectional forwarding, and half-close. UDP and `BIND` are out of
   scope initially.

These applications define the first "usable runtime" release criterion.

### M7: AI development support

Once the API has survived the application proofs, publish focused reference
documentation and a Sturnkey application-development skill. It must teach an
AI developer to:

- recognize Sturnkey as Deno-inspired but independently specified;
- avoid Node.js builtins and npm assumptions;
- discover the filesystem, networking, error, and deployment references;
- grant only the required Wasmtime capabilities;
- use structured resource cleanup for every file, connection, and listener;
- build and test a Sturnkey application without host development frameworks.

The skill is accepted when an AI can produce the M6-style applications from
the skill and API references rather than undocumented project knowledge.

### M8: HTTP, HTTPS, and TLS

After raw I/O and daemon operation are stable, make StarlingMonkey's Fetch API
usable from CLI scripts through `wasi:http/outgoing-handler`:

- remove the HTTP-request-handler-only restriction for outbound `fetch()`;
- integrate fetch operations with the CLI event loop;
- support streaming bodies, redirects, cancellation, and timeouts;
- document Wasmtime's outbound-network grant and certificate behavior.

Wasmtime supplies outbound HTTPS and certificate verification. Sturnkey should
not embed its own TLS stack for this path. Inbound TLS should initially be
terminated by a reverse proxy. Direct inbound TLS is a later, separate design
decision.

### M9: TypeScript workflow

Keep the runtime JavaScript-only while adding an optional development-time
TypeScript-to-JavaScript workflow. CI or a small standalone tool should perform
compilation; runtime TypeScript compilation and npm compatibility are not
initial goals.

## Capability notes

Filesystem programs receive only explicitly preopened directories. Raw WASIp2
networking in the stock Wasmtime CLI currently requires broader grants such as
TCP support and inherited host networking, so networking examples must show
those grants explicitly and document their security implications.

Adding an imported WASI interface can also make a corresponding Wasmtime flag
necessary at component instantiation even when an application does not use the
API. Before M3 is finalized, the project must decide whether to publish one
full-capability runtime or separate core and network artifacts. The decision
should favor predictable invocation and clear capability boundaries over a
large implicit API surface.

## Delivery policy

Each milestone should be delivered as small commits with:

- an API or lifecycle note before stabilizing behavior;
- focused native implementation changes;
- dynamic JavaScript end-to-end tests under stock Wasmtime;
- component validation with `wasm-tools`;
- a runnable example demonstrating the new capability.
