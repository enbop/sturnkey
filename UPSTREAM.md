# Upstream policy

Sturnkey is a downstream distribution of StarlingMonkey. The upstream source
is included as a pinned Git submodule at `vendor/StarlingMonkey`.

Current revision:

```text
5c671dd86db596ac2ea88a60bebf8d4505b4b8fa
```

Sturnkey extensions live outside the submodule and use StarlingMonkey's public
`add_builtin` and `define_builtin_module` extension points. Upstream source is
not patched as part of normal development.

Upgrades should be made in dedicated pull requests that:

1. update the submodule revision;
2. rebuild both debug and release configurations;
3. run the complete integration and capability test suite;
4. record the tested Wasmtime and WASI versions;
5. describe any extension API changes.
