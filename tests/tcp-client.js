import { connect, resolve } from "sturnkey:net";
import { environment } from "sturnkey:runtime";

const echoPort = Number(environment("STURNKEY_ECHO_PORT"));
const refusedPort = Number(environment("STURNKEY_REFUSED_PORT"));
const halfClosePort = Number(environment("STURNKEY_HALF_CLOSE_PORT"));
const localhostAddresses = await resolve("localhost");
if (localhostAddresses.length === 0) throw new Error("localhost did not resolve");
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

const halfClosed = await connect({ hostname: "127.0.0.1", port: halfClosePort });
await halfClosed.write(new TextEncoder().encode("request until eof"));
await halfClosed.shutdown("write");
let halfCloseReply = "";
const decoder = new TextDecoder();
while (true) {
  const chunk = await halfClosed.read();
  if (chunk === null) break;
  halfCloseReply += decoder.decode(chunk, { stream: true });
}
halfCloseReply += decoder.decode();
if (halfCloseReply !== "reply after request eof") {
  throw new Error(`unexpected half-close reply: ${halfCloseReply}`);
}
await halfClosed.close();

try {
  await connect({ hostname: "127.0.0.1", port: refusedPort });
  throw new Error("refused connection unexpectedly succeeded");
} catch (error) {
  if (error.code !== "ECONNREFUSED") throw error;
}

console.log("tcp client test completed");
