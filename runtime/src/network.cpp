#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "bindings.h"
#include "builtin.h"
#include "encode.h"
#include "extension-api.h"

namespace {

constexpr int32_t INVALID_HANDLE = -1;

const char *socket_error_code(wasi_sockets_tcp_error_code_t error) {
  switch (error) {
  case WASI_SOCKETS_NETWORK_ERROR_CODE_ACCESS_DENIED:
    return "EACCES";
  case WASI_SOCKETS_NETWORK_ERROR_CODE_TIMEOUT:
    return "ETIMEDOUT";
  case WASI_SOCKETS_NETWORK_ERROR_CODE_ADDRESS_IN_USE:
    return "EADDRINUSE";
  case WASI_SOCKETS_NETWORK_ERROR_CODE_REMOTE_UNREACHABLE:
    return "ENETUNREACH";
  case WASI_SOCKETS_NETWORK_ERROR_CODE_CONNECTION_REFUSED:
    return "ECONNREFUSED";
  case WASI_SOCKETS_NETWORK_ERROR_CODE_CONNECTION_RESET:
    return "ECONNRESET";
  case WASI_SOCKETS_NETWORK_ERROR_CODE_CONNECTION_ABORTED:
    return "ECONNABORTED";
  case WASI_SOCKETS_NETWORK_ERROR_CODE_WOULD_BLOCK:
    return "EWOULDBLOCK";
  default:
    return "EIO";
  }
}

bool reject_error(JSContext *cx, JS::HandleObject promise, const char *message,
                  const char *code) {
  JS_ReportErrorUTF8(cx, "%s", message);
  JS::RootedValue exception(cx);
  if (!JS_GetPendingException(cx, &exception)) {
    return false;
  }
  JS_ClearPendingException(cx);
  if (exception.isObject()) {
    JS::RootedObject object(cx, &exception.toObject());
    JS::RootedString code_string(cx, JS_NewStringCopyZ(cx, code));
    if (!code_string ||
        !JS_DefineProperty(cx, object, "code", code_string,
                           JSPROP_ENUMERATE | JSPROP_READONLY)) {
      return false;
    }
  }
  return JS::RejectPromise(cx, promise, exception);
}

bool reject_socket_error(JSContext *cx, JS::HandleObject promise,
                         const char *operation,
                         wasi_sockets_tcp_error_code_t error) {
  char message[96];
  std::snprintf(message, sizeof(message), "%s failed (%s)", operation,
                socket_error_code(error));
  return reject_error(cx, promise, message, socket_error_code(error));
}

void drop_pollable(int32_t &handle) {
  if (handle != INVALID_HANDLE) {
    wasi_io_poll_pollable_drop_own({handle});
    handle = INVALID_HANDLE;
  }
}

struct NativeConnection {
  wasi_sockets_tcp_own_tcp_socket_t socket{INVALID_HANDLE};
  wasi_io_streams_own_input_stream_t input{INVALID_HANDLE};
  wasi_io_streams_own_output_stream_t output{INVALID_HANDLE};
  bool closed = false;
  bool reading = false;
  bool writing = false;

  void close() {
    if (closed) {
      return;
    }
    closed = true;
    wasi_sockets_tcp_error_code_t ignored;
    wasi_sockets_tcp_method_tcp_socket_shutdown(
        wasi_sockets_tcp_borrow_tcp_socket(socket),
        WASI_SOCKETS_TCP_SHUTDOWN_TYPE_BOTH, &ignored);
    wasi_io_streams_output_stream_drop_own(output);
    wasi_io_streams_input_stream_drop_own(input);
    wasi_sockets_tcp_tcp_socket_drop_own(socket);
    socket.__handle = INVALID_HANDLE;
    input.__handle = INVALID_HANDLE;
    output.__handle = INVALID_HANDLE;
  }
};

void finalize_connection(JS::GCContext *, JSObject *object) {
  JS::Value value = JS::GetReservedSlot(object, 0);
  if (value.isUndefined()) {
    return;
  }
  auto *connection = static_cast<NativeConnection *>(value.toPrivate());
  connection->close();
  delete connection;
}

static constexpr JSClassOps connection_class_ops = {
    .finalize = finalize_connection,
};
static constexpr JSClass connection_class = {
    "TcpConnection",
    JSCLASS_HAS_RESERVED_SLOTS(1) | JSCLASS_BACKGROUND_FINALIZE,
    &connection_class_ops,
};

NativeConnection *get_connection(JSContext *cx, JS::CallArgs &args,
                                 const char *method) {
  if (!args.thisv().isObject() ||
      JS::GetClass(&args.thisv().toObject()) != &connection_class) {
    JS_ReportErrorUTF8(cx, "%s called with an incompatible receiver", method);
    return nullptr;
  }
  auto value = JS::GetReservedSlot(&args.thisv().toObject(), 0);
  auto *connection = static_cast<NativeConnection *>(value.toPrivate());
  if (connection->closed) {
    JS_ReportErrorUTF8(cx, "%s: connection is closed", method);
    return nullptr;
  }
  return connection;
}

JSObject *bytes_to_uint8_array(JSContext *cx, bindings_list_u8_t &bytes) {
  if (bytes.len == 0) {
    bindings_list_u8_free(&bytes);
    return JS_NewUint8Array(cx, 0);
  }
  JS::RootedObject buffer(
      cx, JS::NewArrayBufferWithContents(
              cx, bytes.len, bytes.ptr,
              JS::NewArrayBufferOutOfMemory::CallerMustFreeMemory));
  if (!buffer) {
    return nullptr;
  }
  size_t length = bytes.len;
  bytes.ptr = nullptr;
  bytes.len = 0;
  return JS_NewUint8ArrayWithBuffer(cx, buffer, 0, length);
}

bool connection_read(JSContext *cx, unsigned argc, JS::Value *vp);
bool connection_write(JSContext *cx, unsigned argc, JS::Value *vp);
bool connection_close(JSContext *cx, unsigned argc, JS::Value *vp);

JSObject *new_connection_object(JSContext *cx, NativeConnection *connection) {
  JS::RootedObject object(cx, JS_NewObject(cx, &connection_class));
  if (!object) {
    return nullptr;
  }
  JS::SetReservedSlot(object, 0, JS::PrivateValue(connection));
  if (!JS_DefineFunction(cx, object, "read", connection_read, 0, 0) ||
      !JS_DefineFunction(cx, object, "write", connection_write, 1, 0) ||
      !JS_DefineFunction(cx, object, "close", connection_close, 0, 0)) {
    return nullptr;
  }
  return object;
}

class ConnectionTask : public api::AsyncTask {
protected:
  JS::Heap<JSObject *> promise_;
  JS::Heap<JSObject *> connection_object_;
  NativeConnection *connection_;
  bool interested_ = true;

  void complete() {
    drop_pollable(handle_);
    if (interested_) {
      api::Engine::decr_event_loop_interest();
      interested_ = false;
    }
  }

public:
  ConnectionTask(JS::HandleObject promise, JS::HandleObject connection_object,
                 NativeConnection *connection)
      : promise_(promise), connection_object_(connection_object),
        connection_(connection) {
    api::Engine::incr_event_loop_interest();
  }

  bool cancel(api::Engine *) override {
    complete();
    return true;
  }

  void trace(JSTracer *tracer) override {
    JS::TraceEdge(tracer, &promise_, "Sturnkey network promise");
    JS::TraceEdge(tracer, &connection_object_, "Sturnkey connection");
  }
};

class ReadTask final : public ConnectionTask {
public:
  ReadTask(JS::HandleObject promise, JS::HandleObject connection_object,
           NativeConnection *connection)
      : ConnectionTask(promise, connection_object, connection) {
    handle_ = wasi_io_streams_method_input_stream_subscribe(
                  wasi_io_streams_borrow_input_stream(connection_->input))
                  .__handle;
  }

  bool run(api::Engine *engine) override {
    JSContext *cx = engine->cx();
    JS::RootedObject promise(cx, promise_);
    bindings_list_u8_t bytes{};
    wasi_io_streams_stream_error_t error{};
    bool success = wasi_io_streams_method_input_stream_read(
        wasi_io_streams_borrow_input_stream(connection_->input), 65536, &bytes,
        &error);
    connection_->reading = false;
    complete();

    if (!success) {
      if (error.tag == WASI_IO_STREAMS_STREAM_ERROR_CLOSED) {
        return JS::ResolvePromise(cx, promise, JS::NullHandleValue);
      }
      wasi_io_streams_stream_error_free(&error);
      return reject_error(cx, promise, "read failed", "EIO");
    }
    JS::RootedObject result(cx, bytes_to_uint8_array(cx, bytes));
    if (!result) {
      return RejectPromiseWithPendingError(cx, promise);
    }
    JS::RootedValue value(cx, JS::ObjectValue(*result));
    return JS::ResolvePromise(cx, promise, value);
  }
};

class WriteTask final : public ConnectionTask {
  std::vector<uint8_t> bytes_;
  size_t offset_ = 0;

public:
  WriteTask(JS::HandleObject promise, JS::HandleObject connection_object,
            NativeConnection *connection, std::span<uint8_t> bytes)
      : ConnectionTask(promise, connection_object, connection),
        bytes_(bytes.begin(), bytes.end()) {
    handle_ = wasi_io_streams_method_output_stream_subscribe(
                  wasi_io_streams_borrow_output_stream(connection_->output))
                  .__handle;
  }

  bool run(api::Engine *engine) override {
    JSContext *cx = engine->cx();
    JS::RootedObject promise(cx, promise_);
    auto output = wasi_io_streams_borrow_output_stream(connection_->output);
    wasi_io_streams_stream_error_t error{};
    uint64_t capacity = 0;
    if (!wasi_io_streams_method_output_stream_check_write(output, &capacity,
                                                          &error)) {
      connection_->writing = false;
      complete();
      wasi_io_streams_stream_error_free(&error);
      return reject_error(cx, promise, "write failed", "EIO");
    }

    size_t count = std::min<size_t>(capacity, bytes_.size() - offset_);
    bindings_list_u8_t chunk{bytes_.data() + offset_, count};
    if (count &&
        !wasi_io_streams_method_output_stream_write(output, &chunk, &error)) {
      connection_->writing = false;
      complete();
      wasi_io_streams_stream_error_free(&error);
      return reject_error(cx, promise, "write failed", "EIO");
    }
    offset_ += count;
    if (offset_ < bytes_.size()) {
      api::Engine::queue_async_task(RefPtr<WriteTask>(this));
      return true;
    }

    connection_->writing = false;
    complete();
    return JS::ResolvePromise(cx, promise, JS::UndefinedHandleValue);
  }
};

bool connection_read(JSContext *cx, unsigned argc, JS::Value *vp) {
  JS::CallArgs args = JS::CallArgsFromVp(argc, vp);
  NativeConnection *connection = get_connection(cx, args, "read");
  if (!connection) {
    return false;
  }
  if (connection->reading) {
    JS_ReportErrorUTF8(cx, "read: another read is already pending");
    return false;
  }
  JS::RootedObject promise(cx, JS::NewPromiseObject(cx, nullptr));
  if (!promise) {
    return false;
  }
  JS::RootedObject self(cx, &args.thisv().toObject());
  connection->reading = true;
  api::Engine::queue_async_task(js_new<ReadTask>(promise, self, connection));
  args.rval().setObject(*promise);
  return true;
}

bool connection_write(JSContext *cx, unsigned argc, JS::Value *vp) {
  JS::CallArgs args = JS::CallArgsFromVp(argc, vp);
  if (!args.requireAtLeast(cx, "write", 1)) {
    return false;
  }
  NativeConnection *connection = get_connection(cx, args, "write");
  if (!connection) {
    return false;
  }
  if (connection->writing) {
    JS_ReportErrorUTF8(cx, "write: another write is already pending");
    return false;
  }
  auto bytes = value_to_buffer(cx, args[0], "write: data");
  if (!bytes) {
    return false;
  }
  JS::RootedObject promise(cx, JS::NewPromiseObject(cx, nullptr));
  if (!promise) {
    return false;
  }
  if (bytes->empty()) {
    args.rval().setObject(*promise);
    return JS::ResolvePromise(cx, promise, JS::UndefinedHandleValue);
  }
  JS::RootedObject self(cx, &args.thisv().toObject());
  connection->writing = true;
  api::Engine::queue_async_task(
      js_new<WriteTask>(promise, self, connection, *bytes));
  args.rval().setObject(*promise);
  return true;
}

bool connection_close(JSContext *cx, unsigned argc, JS::Value *vp) {
  JS::CallArgs args = JS::CallArgsFromVp(argc, vp);
  if (!args.thisv().isObject() ||
      JS::GetClass(&args.thisv().toObject()) != &connection_class) {
    JS_ReportErrorUTF8(cx, "close called with an incompatible receiver");
    return false;
  }
  auto value = JS::GetReservedSlot(&args.thisv().toObject(), 0);
  auto *connection = static_cast<NativeConnection *>(value.toPrivate());
  if (connection->reading || connection->writing) {
    JS_ReportErrorUTF8(cx, "close: an operation is still pending");
    return false;
  }
  connection->close();
  JS::RootedObject promise(cx, JS::NewPromiseObject(cx, nullptr));
  if (!promise || !JS::ResolvePromise(cx, promise, JS::UndefinedHandleValue)) {
    return false;
  }
  args.rval().setObject(*promise);
  return true;
}

bool parse_ipv4(const std::string &hostname, std::array<uint8_t, 4> &address) {
  unsigned values[4];
  char tail;
  if (std::sscanf(hostname.c_str(), "%u.%u.%u.%u%c", &values[0], &values[1],
                  &values[2], &values[3], &tail) != 4) {
    return false;
  }
  for (size_t index = 0; index < address.size(); index++) {
    if (values[index] > 255) {
      return false;
    }
    address[index] = values[index];
  }
  return true;
}

bool get_endpoint(JSContext *cx, JS::HandleValue value,
                  std::array<uint8_t, 4> &address, uint16_t &port) {
  if (!value.isObject()) {
    JS_ReportErrorUTF8(cx, "connect: endpoint must be an object");
    return false;
  }
  JS::RootedObject object(cx, &value.toObject());
  JS::RootedValue hostname_value(cx);
  JS::RootedValue port_value(cx);
  if (!JS_GetProperty(cx, object, "hostname", &hostname_value) ||
      !JS_GetProperty(cx, object, "port", &port_value)) {
    return false;
  }
  if (!hostname_value.isString() || !port_value.isNumber()) {
    JS_ReportErrorUTF8(cx, "connect: hostname and port are required");
    return false;
  }
  auto hostname = core::encode(cx, hostname_value);
  if (!hostname.ptr) {
    return false;
  }
  std::string hostname_string(hostname.ptr.get(), hostname.len);
  if (!parse_ipv4(hostname_string, address)) {
    JS_ReportErrorUTF8(cx, "connect: M3 requires an IPv4 address");
    return false;
  }
  double port_number = port_value.toNumber();
  if (port_number < 1 || port_number > 65535 ||
      port_number != uint16_t(port_number)) {
    JS_ReportErrorUTF8(cx, "connect: port must be an integer from 1 to 65535");
    return false;
  }
  port = uint16_t(port_number);
  return true;
}

class ConnectTask final : public api::AsyncTask {
  JS::Heap<JSObject *> promise_;
  wasi_sockets_tcp_own_tcp_socket_t socket_;
  wasi_sockets_instance_network_own_network_t network_;
  bool interested_ = true;

  void cleanup(bool drop_socket) {
    drop_pollable(handle_);
    wasi_sockets_network_network_drop_own(network_);
    network_.__handle = INVALID_HANDLE;
    if (drop_socket) {
      wasi_sockets_tcp_tcp_socket_drop_own(socket_);
      socket_.__handle = INVALID_HANDLE;
    }
    if (interested_) {
      api::Engine::decr_event_loop_interest();
      interested_ = false;
    }
  }

public:
  ConnectTask(JS::HandleObject promise,
              wasi_sockets_tcp_own_tcp_socket_t socket,
              wasi_sockets_instance_network_own_network_t network)
      : promise_(promise), socket_(socket), network_(network) {
    handle_ = wasi_sockets_tcp_method_tcp_socket_subscribe(
                  wasi_sockets_tcp_borrow_tcp_socket(socket_))
                  .__handle;
    api::Engine::incr_event_loop_interest();
  }

  bool run(api::Engine *engine) override {
    JSContext *cx = engine->cx();
    JS::RootedObject promise(cx, promise_);
    wasi_sockets_tcp_tuple2_own_input_stream_own_output_stream_t streams{};
    wasi_sockets_tcp_error_code_t error{};
    if (!wasi_sockets_tcp_method_tcp_socket_finish_connect(
            wasi_sockets_tcp_borrow_tcp_socket(socket_), &streams, &error)) {
      cleanup(true);
      return reject_socket_error(cx, promise, "connect", error);
    }
    auto *connection = new NativeConnection{socket_, streams.f0, streams.f1};
    JS::RootedObject result(cx, new_connection_object(cx, connection));
    if (!result) {
      connection->close();
      delete connection;
      cleanup(false);
      return RejectPromiseWithPendingError(cx, promise);
    }
    cleanup(false);
    JS::RootedValue value(cx, JS::ObjectValue(*result));
    return JS::ResolvePromise(cx, promise, value);
  }

  bool cancel(api::Engine *) override {
    cleanup(true);
    return true;
  }

  void trace(JSTracer *tracer) override {
    JS::TraceEdge(tracer, &promise_, "Sturnkey connect promise");
  }
};

bool connect(JSContext *cx, unsigned argc, JS::Value *vp) {
  JS::CallArgs args = JS::CallArgsFromVp(argc, vp);
  if (!args.requireAtLeast(cx, "connect", 1)) {
    return false;
  }
  std::array<uint8_t, 4> address;
  uint16_t port;
  if (!get_endpoint(cx, args[0], address, port)) {
    return false;
  }
  JS::RootedObject promise(cx, JS::NewPromiseObject(cx, nullptr));
  if (!promise) {
    return false;
  }
  args.rval().setObject(*promise);

  wasi_sockets_tcp_own_tcp_socket_t socket;
  wasi_sockets_tcp_error_code_t error;
  if (!wasi_sockets_tcp_create_socket_create_tcp_socket(
          WASI_SOCKETS_NETWORK_IP_ADDRESS_FAMILY_IPV4, &socket, &error)) {
    return reject_socket_error(cx, promise, "create socket", error);
  }
  auto network = wasi_sockets_instance_network_instance_network();
  wasi_sockets_tcp_ip_socket_address_t socket_address{
      WASI_SOCKETS_NETWORK_IP_SOCKET_ADDRESS_IPV4,
      {.ipv4 = {port, {address[0], address[1], address[2], address[3]}}}};
  if (!wasi_sockets_tcp_method_tcp_socket_start_connect(
          wasi_sockets_tcp_borrow_tcp_socket(socket),
          wasi_sockets_network_borrow_network(network), &socket_address,
          &error)) {
    wasi_sockets_network_network_drop_own(network);
    wasi_sockets_tcp_tcp_socket_drop_own(socket);
    return reject_socket_error(cx, promise, "connect", error);
  }
  api::Engine::queue_async_task(js_new<ConnectTask>(promise, socket, network));
  return true;
}

} // namespace

namespace sturnkey::network {

bool install(api::Engine *engine) {
  JSContext *cx = engine->cx();
  JS::RootedObject module(cx, JS_NewPlainObject(cx));
  if (!module ||
      !JS_DefineFunction(cx, module, "connect", connect, 1, JSPROP_ENUMERATE)) {
    return false;
  }
  JS::RootedValue module_value(cx, JS::ObjectValue(*module));
  return engine->define_builtin_module("sturnkey:net", module_value);
}

} // namespace sturnkey::network
