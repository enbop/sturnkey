import { sleep } from "sturnkey:runtime";

const invalidDurations = [
  -1,
  Infinity,
  NaN,
  Number.MAX_VALUE,
  "1",
  undefined,
];

for (const duration of invalidDurations) {
  let threw = false;
  try {
    sleep(duration);
  } catch {
    threw = true;
  }

  if (!threw) {
    throw new Error(`sleep(${String(duration)}) did not throw`);
  }
}

console.log("invalid sleep arguments rejected");
