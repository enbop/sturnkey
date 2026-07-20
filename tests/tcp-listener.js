import { listen } from "sturnkey:net";
import { environment } from "sturnkey:runtime";

const listener = await listen({
  hostname: "127.0.0.1",
  port: Number(environment("STURNKEY_LISTEN_PORT")),
});
console.log("tcp listener ready");

async function handle(connection) {
  const request = await connection.read();
  if (request !== null) await connection.write(request);
  await connection.close();
}

const handlers = [];
let accepted = 0;
for await (const connection of listener) {
  handlers.push(handle(connection));
  if (++accepted === 3) break;
}
await Promise.all(handlers);
await listener.close();
await listener.close();
console.log("tcp listener test completed");
