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
