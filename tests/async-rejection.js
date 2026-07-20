import { sleep } from "sturnkey:runtime";

await sleep(1);
throw new Error("expected top-level rejection");
