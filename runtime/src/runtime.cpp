#include <cmath>
#include <cstdint>
#include <limits>
#include <string>

#include "bindings.h"
#include "builtin.h"
#include "extension-api.h"
#include "host_api.h"

namespace {

constexpr double NANOSECONDS_PER_MILLISECOND = 1'000'000.0;

class SleepTask final : public api::AsyncTask {
  JS::Heap<JSObject *> promise_;
  bool interested_ = true;

  void complete() {
    if (handle_ != INVALID_POLLABLE_HANDLE) {
      host_api::MonotonicClock::unsubscribe(handle_);
      handle_ = INVALID_POLLABLE_HANDLE;
    }

    if (interested_) {
      api::Engine::decr_event_loop_interest();
      interested_ = false;
    }
  }

public:
  SleepTask(JS::HandleObject promise, uint64_t deadline)
      : promise_(promise), deadline_(deadline) {
    handle_ = host_api::MonotonicClock::subscribe(deadline_, true);
    api::Engine::incr_event_loop_interest();
  }

  [[nodiscard]] bool run(api::Engine *engine) override {
    JSContext *cx = engine->cx();
    JS::RootedObject promise(cx, promise_);
    complete();
    return JS::ResolvePromise(cx, promise, JS::UndefinedHandleValue);
  }

  [[nodiscard]] bool cancel(api::Engine *engine) override {
    complete();
    return true;
  }

  [[nodiscard]] uint64_t deadline() override { return deadline_; }

  void trace(JSTracer *trc) override {
    JS::TraceEdge(trc, &promise_, "Sturnkey sleep promise");
  }

private:
  uint64_t deadline_;
};

bool sleep(JSContext *cx, unsigned argc, JS::Value *vp) {
  JS::CallArgs args = JS::CallArgsFromVp(argc, vp);
  if (!args.requireAtLeast(cx, "sleep", 1)) {
    return false;
  }

  if (!args[0].isNumber()) {
    JS_ReportErrorUTF8(cx, "sleep: milliseconds must be a number");
    return false;
  }

  double milliseconds = args[0].toNumber();
  if (!std::isfinite(milliseconds) || milliseconds < 0) {
    JS_ReportErrorUTF8(
        cx, "sleep: milliseconds must be a finite, non-negative number");
    return false;
  }

  uint64_t now = host_api::MonotonicClock::now();
  uint64_t remaining_ns = std::numeric_limits<uint64_t>::max() - now;
  long double duration_ns_value = std::ceil(
      static_cast<long double>(milliseconds) * NANOSECONDS_PER_MILLISECOND);
  if (duration_ns_value > static_cast<long double>(remaining_ns)) {
    JS_ReportErrorUTF8(cx, "sleep: milliseconds exceeds the clock range");
    return false;
  }

  JS::RootedObject promise(cx, JS::NewPromiseObject(cx, nullptr));
  if (!promise) {
    return false;
  }

  uint64_t duration_ns = static_cast<uint64_t>(duration_ns_value);
  api::Engine::queue_async_task(js_new<SleepTask>(promise, now + duration_ns));
  args.rval().setObject(*promise);
  return true;
}

bool arguments(JSContext *cx, unsigned argc, JS::Value *vp) {
  JS::CallArgs args = JS::CallArgsFromVp(argc, vp);
  auto host_arguments = host_api::environment_get_arguments();
  JS::RootedObject result(cx, JS::NewArrayObject(cx, 0));
  if (!result) {
    return false;
  }

  for (size_t index = 0; index < host_arguments.size(); index++) {
    const auto &argument = host_arguments[index];
    JS::RootedString value(
        cx, JS_NewStringCopyUTF8N(
                cx, JS::UTF8Chars(argument.data(), argument.size())));
    if (!value || !JS_SetElement(cx, result, index, value)) {
      return false;
    }
  }

  args.rval().setObject(*result);
  return true;
}

bool environment(JSContext *cx, unsigned argc, JS::Value *vp) {
  JS::CallArgs args = JS::CallArgsFromVp(argc, vp);
  if (!args.requireAtLeast(cx, "environment", 1)) {
    return false;
  }
  if (!args[0].isString()) {
    JS_ReportErrorUTF8(cx, "environment: name must be a string");
    return false;
  }

  JS::RootedString name_value(cx, args[0].toString());
  JS::UniqueChars name = JS_EncodeStringToUTF8(cx, name_value);
  if (!name) {
    return false;
  }
  bindings_list_tuple2_string_string_t entries{};
  wasi_cli_environment_get_environment(&entries);
  for (size_t index = 0; index < entries.len; index++) {
    auto &entry = entries.ptr[index];
    std::string entry_name(reinterpret_cast<char *>(entry.f0.ptr),
                           entry.f0.len);
    if (entry_name != name.get()) {
      continue;
    }
    JS::RootedString value(
        cx, JS_NewStringCopyUTF8N(
                cx, JS::UTF8Chars(reinterpret_cast<char *>(entry.f1.ptr),
                                  entry.f1.len)));
    bindings_list_tuple2_string_string_free(&entries);
    if (!value) {
      return false;
    }
    args.rval().setString(value);
    return true;
  }
  bindings_list_tuple2_string_string_free(&entries);
  args.rval().setUndefined();
  return true;
}

} // namespace

namespace sturnkey::runtime {

bool install(api::Engine *engine) {
  JSContext *cx = engine->cx();
  JS::RootedObject module(cx, JS_NewPlainObject(cx));
  if (!module) {
    return false;
  }

  JS::RootedString version(cx, JS_NewStringCopyZ(cx, STURNKEY_VERSION));
  if (!version) {
    return false;
  }

  JS::RootedValue version_value(cx, JS::StringValue(version));
  if (!JS_DefineProperty(cx, module, "version", version_value,
                         JSPROP_ENUMERATE | JSPROP_READONLY |
                             JSPROP_PERMANENT)) {
    return false;
  }

  JS::RootedFunction sleep_function(cx,
                                    JS_NewFunction(cx, sleep, 1, 0, "sleep"));
  if (!sleep_function) {
    return false;
  }

  JS::RootedValue sleep_value(
      cx, JS::ObjectValue(*JS_GetFunctionObject(sleep_function)));
  if (!JS_DefineProperty(cx, module, "sleep", sleep_value,
                         JSPROP_ENUMERATE | JSPROP_READONLY |
                             JSPROP_PERMANENT)) {
    return false;
  }

  JS::RootedFunction arguments_function(
      cx, JS_NewFunction(cx, arguments, 0, 0, "arguments"));
  if (!arguments_function) {
    return false;
  }

  JS::RootedValue arguments_value(
      cx, JS::ObjectValue(*JS_GetFunctionObject(arguments_function)));
  if (!JS_DefineProperty(cx, module, "arguments", arguments_value,
                         JSPROP_ENUMERATE | JSPROP_READONLY |
                             JSPROP_PERMANENT)) {
    return false;
  }

  JS::RootedFunction environment_function(
      cx, JS_NewFunction(cx, environment, 1, 0, "environment"));
  if (!environment_function) {
    return false;
  }
  JS::RootedValue environment_value(
      cx, JS::ObjectValue(*JS_GetFunctionObject(environment_function)));
  if (!JS_DefineProperty(cx, module, "environment", environment_value,
                         JSPROP_ENUMERATE | JSPROP_READONLY |
                             JSPROP_PERMANENT)) {
    return false;
  }

  JS::RootedValue module_value(cx, JS::ObjectValue(*module));
  return engine->define_builtin_module("sturnkey:runtime", module_value);
}

} // namespace sturnkey::runtime
