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
import { arguments as runtimeArguments } from "sturnkey:runtime";

const encoder = new TextEncoder();
const decoder = new TextDecoder();
const root = "/sandbox/sturnkey-fs-test";

try {
  await remove(`${root}/bytes.bin`);
  await remove(`${root}/message.txt`);
  await remove(root);
} catch (error) {
  if (error.code !== "ENOENT" && error.code !== "ENOTEMPTY") throw error;
}

await mkdir(root);
await writeTextFile(`${root}/message.txt`, "hello filesystem");
if ((await readTextFile(`${root}/message.txt`)) !== "hello filesystem") {
  throw new Error("text round trip failed");
}
await writeFile(`${root}/bytes.bin`, encoder.encode("bytes"));
if (decoder.decode(await readFile(`${root}/bytes.bin`)) !== "bytes") {
  throw new Error("byte round trip failed");
}
const info = await stat(`${root}/message.txt`);
if (info.kind !== "file" || info.size !== 16) {
  throw new Error(`unexpected stat: ${JSON.stringify(info)}`);
}
const entries = await readDir(root);
entries.sort((left, right) => left.name.localeCompare(right.name));
if (entries.map(({ name, kind }) => `${name}:${kind}`).join(",") !==
    "bytes.bin:file,message.txt:file") {
  throw new Error(`unexpected directory entries: ${JSON.stringify(entries)}`);
}
try {
  await readTextFile(`${root}/missing`);
  throw new Error("missing file unexpectedly succeeded");
} catch (error) {
  if (error.code !== "ENOENT") throw error;
}
if (!runtimeArguments().some((argument) => argument.endsWith("filesystem.js"))) {
  throw new Error("runtime arguments do not include the script path");
}
await remove(`${root}/bytes.bin`);
await remove(`${root}/message.txt`);
await remove(root);
console.log("filesystem capability test completed");
