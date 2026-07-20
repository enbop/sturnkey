const encoder = new TextEncoder();

function concat(left, right) {
  const result = new Uint8Array(left.length + right.length);
  result.set(left);
  result.set(right, left.length);
  return result;
}

function findSequence(buffer, sequence) {
  outer: for (let index = 0; index <= buffer.length - sequence.length; index++) {
    for (let offset = 0; offset < sequence.length; offset++) {
      if (buffer[index + offset] !== sequence[offset]) continue outer;
    }
    return index;
  }
  return -1;
}

export class BufferedReader {
  #connection;
  #buffer = new Uint8Array();

  constructor(connection) {
    this.#connection = connection;
  }

  async #fill() {
    const chunk = await this.#connection.read();
    if (chunk === null) return false;
    this.#buffer = concat(this.#buffer, chunk);
    return true;
  }

  async readExactly(length) {
    if (!Number.isInteger(length) || length < 0) {
      throw new TypeError("readExactly length must be a non-negative integer");
    }
    while (this.#buffer.length < length) {
      if (!await this.#fill()) throw new Error("unexpected end of stream");
    }
    const result = this.#buffer.slice(0, length);
    this.#buffer = this.#buffer.slice(length);
    return result;
  }

  async read() {
    if (this.#buffer.length === 0) return this.#connection.read();
    const result = this.#buffer;
    this.#buffer = new Uint8Array();
    return result;
  }

  async readUntil(delimiter, limit = 16 * 1024) {
    const sequence = typeof delimiter === "string" ? encoder.encode(delimiter) : delimiter;
    if (!(sequence instanceof Uint8Array) || sequence.length === 0) {
      throw new TypeError("readUntil delimiter must not be empty");
    }
    while (true) {
      const index = findSequence(this.#buffer, sequence);
      if (index !== -1) {
        const result = this.#buffer.slice(0, index);
        this.#buffer = this.#buffer.slice(index + sequence.length);
        return result;
      }
      if (this.#buffer.length >= limit) throw new Error("buffer limit exceeded");
      if (!await this.#fill()) throw new Error("unexpected end of stream");
    }
  }
}
