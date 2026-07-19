# Sturnkey

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

Sturnkey is in early development. The first milestone is a reproducible
runtime build that loads JavaScript dynamically. Promise-based WASI filesystem,
TCP, CLI, and HTTP server APIs will follow in small, tested increments.

The current runtime already loads JavaScript at execution time and exposes a
small `sturnkey:runtime` builtin. It does not yet expose Sturnkey filesystem or
socket APIs.

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

## Run dynamic JavaScript

```js
// main.js
import { version } from "sturnkey:runtime";

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

## License

Apache-2.0 WITH LLVM-exception. See [LICENSE](LICENSE) and [NOTICE](NOTICE).
