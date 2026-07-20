import { readDir } from "sturnkey:fs";
import { arguments as runtimeArguments } from "sturnkey:runtime";

async function printTree(path, prefix = "") {
  const entries = await readDir(path);
  entries.sort((left, right) => left.name.localeCompare(right.name));
  for (const entry of entries) {
    console.log(`${prefix}${entry.name}`);
    if (entry.kind === "directory") {
      await printTree(`${path}/${entry.name}`, `${prefix}  `);
    }
  }
}

await printTree(runtimeArguments()[2] ?? ".");
