#include "builtin.h"
#include "extension-api.h"

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

  JS::RootedValue module_value(cx, JS::ObjectValue(*module));
  return engine->define_builtin_module("sturnkey:runtime", module_value);
}

} // namespace sturnkey::runtime
