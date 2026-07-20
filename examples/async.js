import { sleep, version } from "sturnkey:runtime";

console.log(`Sturnkey ${version} is waiting`);
await sleep(100);
console.log("done");
