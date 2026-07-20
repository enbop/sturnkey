# Filesystem API

The `sturnkey:fs` module is a small, Deno-inspired asynchronous filesystem API.
It is independently specified and is not a Deno compatibility layer.

Every path is interpreted inside the WASI sandbox. A host directory is
invisible unless it is explicitly preopened by Wasmtime, for example:

```console
wasmtime -S http --dir ./data::/data sturnkey.wasm main.js
```

All functions return promises. M2 performs each bounded libc/WASI operation
synchronously before settling its promise; streaming and cancellation are
future additions.

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

- `readFile(path)` returns a `Uint8Array`.
- `readTextFile(path)` decodes the file as UTF-8.
- `writeFile(path, data)` accepts an `ArrayBuffer` or an array-buffer view.
- `writeTextFile(path, text)` encodes text as UTF-8.
- `readDir(path)` returns `{ name, kind }[]`; kind is `file`, `directory`,
  `symlink`, or `other`.
- `stat(path)` returns `{ kind, size }`.
- `mkdir(path)` creates one directory. It is not recursive.
- `remove(path)` removes one file or one empty directory. It is not recursive.

Rejected operations use `Error` objects with a stable `code` property such as
`ENOENT`, `EACCES`, `EEXIST`, `ENOTDIR`, or `ENOTEMPTY`.
