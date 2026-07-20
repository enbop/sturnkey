import { readFile, writeFile } from "sturnkey:fs";
import { arguments as runtimeArguments } from "sturnkey:runtime";

const [, , source, destination] = runtimeArguments();
if (!source || !destination) {
  throw new Error("usage: copy.js SOURCE DESTINATION");
}

await writeFile(destination, await readFile(source));
