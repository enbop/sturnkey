# Sturnkey

[![CI](https://github.com/enbop/sturnkey/actions/workflows/ci.yml/badge.svg)](https://github.com/enbop/sturnkey/actions/workflows/ci.yml)

Sturnkey is a lightweight JavaScript runtime for CLI, daemon, and local web
applications on WASI. It is built as a downstream extension of
[StarlingMonkey](https://github.com/bytecodealliance/StarlingMonkey) and is
designed to run dynamic JavaScript with stock Wasmtime:

```console
wasmtime sturnkey.wasm main.js
```

Sturnkey's system APIs are inspired by Deno's async-first design, but are
independently specified. Sturnkey is not a Deno compatibility layer and does
not provide Node.js or npm compatibility.

## Status

Sturnkey is in early development. It currently loads JavaScript dynamically,
supports the CLI asynchronous lifecycle, and exposes capability-based
filesystem and asynchronous TCP client/listener APIs. HTTP server APIs and
network hardening will follow in tested increments.

The `sturnkey:runtime` builtin provides command-line arguments and a
Promise-based monotonic `sleep()` API. The `sturnkey:fs` builtin provides byte
and UTF-8 file I/O plus basic directory operations inside Wasmtime preopens.
The `sturnkey:net` builtin supports numeric-IPv4 TCP clients and foreground
listeners over `wasi:sockets`.

## Build

Prerequisites are CMake 3.27 or newer, a C++ toolchain, Rust, and Node.js. The
build downloads StarlingMonkey's pinned build dependencies; it does not modify
the upstream submodule.

```console
git clone --recurse-submodules https://github.com/enbop/sturnkey.git
cd sturnkey
cmake --preset dev
cmake --build --preset dev --target sturnkey
```

The result is `build/dev/sturnkey.wasm`.

CI builds and tests the same artifact on every pull request and push to `main`,
then makes `sturnkey.wasm` available as a workflow artifact. Tags beginning
with `v` build an optimized component and publish it in a GitHub release.

## Run dynamic JavaScript

```js
// main.js
import { sleep, version } from "sturnkey:runtime";

await sleep(100);
console.log(`Hello from Sturnkey ${version}`);
```

```console
wasmtime -S http --dir . build/dev/sturnkey.wasm main.js
```

The `-S http` flag enables the WASI HTTP proposal required by the underlying
runtime. Directory and network access remain Wasmtime capabilities and must be
granted explicitly as Sturnkey adds the corresponding APIs.

Run the current checks with:

```console
cmake --build --preset dev --target format-check
ctest --preset dev --output-on-failure
wasm-tools validate build/dev/sturnkey.wasm
```

## Design principles

- Keep application JavaScript dynamic; rebuilding the runtime must not be
  required for each application.
- Use Wasmtime capabilities for filesystem and network isolation.
- Keep the C++ layer limited to SpiderMonkey, Promise, resource, and WASI
  bridging.
- Build higher-level protocols and application helpers in JavaScript.
- Follow established Web APIs where they exist and document every Sturnkey API.
- Depend on a pinned upstream StarlingMonkey revision without patching it.

## Repository layout

```text
runtime/                 Sturnkey native builtins
examples/                Executable JavaScript examples
tests/                   Runtime and capability tests
vendor/StarlingMonkey/   Pinned upstream submodule
```

See [UPSTREAM.md](UPSTREAM.md) for dependency and update policy.
See [docs/ROADMAP.md](docs/ROADMAP.md) for the staged implementation plan and
acceptance criteria.
See [docs/api/runtime.md](docs/api/runtime.md) for the current JavaScript API.
See [docs/api/filesystem.md](docs/api/filesystem.md) for the filesystem API and
[docs/api/network.md](docs/api/network.md) for TCP capabilities.

## License

Apache-2.0 WITH LLVM-exception. See [LICENSE](LICENSE) and [NOTICE](NOTICE).
