import { sleep } from "sturnkey:runtime";

const started = Date.now();
await sleep(20);
const elapsed = Date.now() - started;

if (elapsed < 15) {
  throw new Error(`sleep resolved too early after ${elapsed}ms`);
}

await sleep(0);

const completionOrder = [];
await Promise.all([
  sleep(20).then(() => completionOrder.push("slow")),
  sleep(1).then(() => completionOrder.push("fast")),
]);

if (completionOrder.join(",") !== "fast,slow") {
  throw new Error(`unexpected completion order: ${completionOrder.join(",")}`);
}

console.log("async sleep completed");
