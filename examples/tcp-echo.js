import { listen } from "sturnkey:net";
import { environment } from "sturnkey:runtime";

const listener = await listen({
  hostname: environment("HOST") ?? "127.0.0.1",
  port: Number(environment("PORT") ?? "9000"),
});
console.log(`listening on ${listener.hostname}:${listener.port}`);

for await (const connection of listener) {
  void (async () => {
    try {
      while (true) {
        const data = await connection.read();
        if (data === null) break;
        await connection.write(data);
      }
    } finally {
      await connection.close();
    }
  })();
}
