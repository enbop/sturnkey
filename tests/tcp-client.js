import { connect } from "sturnkey:net";
import { environment } from "sturnkey:runtime";

const echoPort = Number(environment("STURNKEY_ECHO_PORT"));
const refusedPort = Number(environment("STURNKEY_REFUSED_PORT"));
const connection = await connect({ hostname: "127.0.0.1", port: echoPort });
const message = new Uint8Array(256 * 1024);
for (let index = 0; index < message.length; index++) message[index] = index % 251;
await connection.write(message);

let received = 0;
while (received < message.length) {
  const reply = await connection.read();
  if (reply === null) throw new Error("unexpected EOF");
  for (let index = 0; index < reply.length; index++) {
    if (reply[index] !== (received + index) % 251) {
      throw new Error(`corrupt byte at ${received + index}`);
    }
  }
  received += reply.length;
}
if ((await connection.read()) !== null) {
  throw new Error("expected EOF");
}
await connection.close();

try {
  await connect({ hostname: "127.0.0.1", port: refusedPort });
  throw new Error("refused connection unexpectedly succeeded");
} catch (error) {
  if (error.code !== "ECONNREFUSED") throw error;
}

console.log("tcp client test completed");
