# Runtime API

The `sturnkey:runtime` module contains process-wide utilities that do not
belong to a specific I/O subsystem.

## `version`

```ts
const version: string;
```

The Sturnkey runtime version.

## `arguments()`

```ts
function arguments(): string[];
```

Return a fresh array containing the WASI command-line arguments. The runtime
executable and JavaScript script path are included; applications should treat
any later values as their own arguments.

## `sleep(milliseconds)`

```ts
function sleep(milliseconds: number): Promise<void>;
```

Wait for at least `milliseconds` using the WASI monotonic clock. The duration
must be a finite, non-negative number. Fractional milliseconds are accepted
and rounded up to the next nanosecond.

The returned Promise registers interest in the CLI event loop until it settles,
so a top-level `await sleep(...)` keeps `wasi:cli/run` alive. When the last
registered asynchronous operation completes, the process may exit naturally.

```js
import { sleep } from "sturnkey:runtime";

console.log("waiting");
await sleep(100);
console.log("done");
```

Invalid durations throw synchronously:

```js
await sleep(-1);       // throws
await sleep(Infinity); // throws
await sleep("100");    // throws; values are not coerced
```
