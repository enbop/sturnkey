#include <cerrno>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

#include "builtin.h"
#include "encode.h"
#include "extension-api.h"

namespace {

const char *error_code(int error) {
  switch (error) {
  case EACCES:
    return "EACCES";
  case EEXIST:
    return "EEXIST";
  case EINVAL:
    return "EINVAL";
  case EISDIR:
    return "EISDIR";
  case ENOENT:
    return "ENOENT";
  case ENOTDIR:
    return "ENOTDIR";
  case ENOTEMPTY:
    return "ENOTEMPTY";
  case EPERM:
    return "EPERM";
  default:
    return "EIO";
  }
}

bool reject_errno(JSContext *cx, JS::HandleObject promise,
                  const char *operation, const std::string &path, int error) {
  JS_ReportErrorUTF8(cx, "%s '%s': %s", operation, path.c_str(),
                     std::strerror(error));
  JS::RootedValue exception(cx);
  if (!JS_GetPendingException(cx, &exception)) {
    return false;
  }
  JS_ClearPendingException(cx);

  if (exception.isObject()) {
    JS::RootedObject object(cx, &exception.toObject());
    JS::RootedString code(cx, JS_NewStringCopyZ(cx, error_code(error)));
    if (!code || !JS_DefineProperty(cx, object, "code", code,
                                    JSPROP_ENUMERATE | JSPROP_READONLY)) {
      return false;
    }
  }
  return JS::RejectPromise(cx, promise, exception);
}

bool get_path(JSContext *cx, JS::HandleValue value, const char *operation,
              std::string &path) {
  if (!value.isString()) {
    JS_ReportErrorUTF8(cx, "%s: path must be a string", operation);
    return false;
  }
  auto encoded = core::encode(cx, value);
  if (!encoded.ptr) {
    return false;
  }
  if (std::memchr(encoded.ptr.get(), '\0', encoded.len)) {
    JS_ReportErrorUTF8(cx, "%s: path must not contain NUL", operation);
    return false;
  }
  path.assign(encoded.ptr.get(), encoded.len);
  return true;
}

JSObject *new_promise(JSContext *cx, JS::CallArgs &args) {
  JS::RootedObject promise(cx, JS::NewPromiseObject(cx, nullptr));
  if (promise) {
    args.rval().setObject(*promise);
  }
  return promise;
}

bool resolve(JSContext *cx, JS::HandleObject promise,
             JS::HandleValue value = JS::UndefinedHandleValue) {
  return JS::ResolvePromise(cx, promise, value);
}

JSObject *bytes_to_uint8_array(JSContext *cx,
                               const std::vector<uint8_t> &bytes) {
  if (bytes.empty()) {
    return JS_NewUint8Array(cx, 0);
  }
  auto contents = JS::UniqueChars(js_pod_malloc<char>(bytes.size()));
  if (!contents) {
    JS_ReportOutOfMemory(cx);
    return nullptr;
  }
  std::memcpy(contents.get(), bytes.data(), bytes.size());
  JS::RootedObject buffer(
      cx, JS::NewArrayBufferWithContents(
              cx, bytes.size(), contents.get(),
              JS::NewArrayBufferOutOfMemory::CallerMustFreeMemory));
  if (!buffer) {
    return nullptr;
  }
  static_cast<void>(contents.release());
  return JS_NewUint8ArrayWithBuffer(cx, buffer, 0, bytes.size());
}

bool read_bytes(const std::string &path, std::vector<uint8_t> &bytes,
                int &error) {
  FILE *file = std::fopen(path.c_str(), "rb");
  if (!file) {
    error = errno;
    return false;
  }
  uint8_t chunk[8192];
  while (true) {
    size_t count = std::fread(chunk, 1, sizeof(chunk), file);
    bytes.insert(bytes.end(), chunk, chunk + count);
    if (count < sizeof(chunk)) {
      if (std::ferror(file)) {
        error = errno;
        std::fclose(file);
        return false;
      }
      break;
    }
  }
  if (std::fclose(file) != 0) {
    error = errno;
    return false;
  }
  return true;
}

bool read_file(JSContext *cx, unsigned argc, JS::Value *vp) {
  JS::CallArgs args = JS::CallArgsFromVp(argc, vp);
  if (!args.requireAtLeast(cx, "readFile", 1)) {
    return false;
  }
  JS::RootedObject promise(cx, new_promise(cx, args));
  if (!promise) {
    return false;
  }
  std::string path;
  if (!get_path(cx, args[0], "readFile", path)) {
    return RejectPromiseWithPendingError(cx, promise);
  }
  std::vector<uint8_t> bytes;
  int error = 0;
  if (!read_bytes(path, bytes, error)) {
    return reject_errno(cx, promise, "readFile", path, error);
  }
  JS::RootedObject result(cx, bytes_to_uint8_array(cx, bytes));
  if (!result) {
    return RejectPromiseWithPendingError(cx, promise);
  }
  JS::RootedValue value(cx, JS::ObjectValue(*result));
  return resolve(cx, promise, value);
}

bool read_text_file(JSContext *cx, unsigned argc, JS::Value *vp) {
  JS::CallArgs args = JS::CallArgsFromVp(argc, vp);
  if (!args.requireAtLeast(cx, "readTextFile", 1)) {
    return false;
  }
  JS::RootedObject promise(cx, new_promise(cx, args));
  if (!promise) {
    return false;
  }
  std::string path;
  if (!get_path(cx, args[0], "readTextFile", path)) {
    return RejectPromiseWithPendingError(cx, promise);
  }
  std::vector<uint8_t> bytes;
  int error = 0;
  if (!read_bytes(path, bytes, error)) {
    return reject_errno(cx, promise, "readTextFile", path, error);
  }
  JS::RootedString text(
      cx, JS_NewStringCopyUTF8N(
              cx, JS::UTF8Chars(reinterpret_cast<const char *>(bytes.data()),
                                bytes.size())));
  if (!text) {
    return RejectPromiseWithPendingError(cx, promise);
  }
  JS::RootedValue value(cx, JS::StringValue(text));
  return resolve(cx, promise, value);
}

bool write_bytes(const std::string &path, const uint8_t *bytes, size_t length,
                 int &error) {
  FILE *file = std::fopen(path.c_str(), "wb");
  if (!file) {
    error = errno;
    return false;
  }
  if (length && std::fwrite(bytes, 1, length, file) != length) {
    error = errno;
    std::fclose(file);
    return false;
  }
  if (std::fclose(file) != 0) {
    error = errno;
    return false;
  }
  return true;
}

bool write_file(JSContext *cx, unsigned argc, JS::Value *vp) {
  JS::CallArgs args = JS::CallArgsFromVp(argc, vp);
  if (!args.requireAtLeast(cx, "writeFile", 2)) {
    return false;
  }
  JS::RootedObject promise(cx, new_promise(cx, args));
  if (!promise) {
    return false;
  }
  std::string path;
  if (!get_path(cx, args[0], "writeFile", path)) {
    return RejectPromiseWithPendingError(cx, promise);
  }
  auto bytes = value_to_buffer(cx, args[1], "writeFile: data");
  if (!bytes) {
    return RejectPromiseWithPendingError(cx, promise);
  }
  int error = 0;
  if (!write_bytes(path, bytes->data(), bytes->size(), error)) {
    return reject_errno(cx, promise, "writeFile", path, error);
  }
  return resolve(cx, promise);
}

bool write_text_file(JSContext *cx, unsigned argc, JS::Value *vp) {
  JS::CallArgs args = JS::CallArgsFromVp(argc, vp);
  if (!args.requireAtLeast(cx, "writeTextFile", 2)) {
    return false;
  }
  JS::RootedObject promise(cx, new_promise(cx, args));
  if (!promise) {
    return false;
  }
  std::string path;
  if (!get_path(cx, args[0], "writeTextFile", path)) {
    return RejectPromiseWithPendingError(cx, promise);
  }
  auto text = core::encode(cx, args[1]);
  if (!text.ptr) {
    return RejectPromiseWithPendingError(cx, promise);
  }
  int error = 0;
  if (!write_bytes(path, reinterpret_cast<const uint8_t *>(text.ptr.get()),
                   text.len, error)) {
    return reject_errno(cx, promise, "writeTextFile", path, error);
  }
  return resolve(cx, promise);
}

const char *file_kind(mode_t mode) {
  if (S_ISREG(mode)) {
    return "file";
  }
  if (S_ISDIR(mode)) {
    return "directory";
  }
  if (S_ISLNK(mode)) {
    return "symlink";
  }
  return "other";
}

bool stat_path(JSContext *cx, unsigned argc, JS::Value *vp) {
  JS::CallArgs args = JS::CallArgsFromVp(argc, vp);
  if (!args.requireAtLeast(cx, "stat", 1)) {
    return false;
  }
  JS::RootedObject promise(cx, new_promise(cx, args));
  if (!promise) {
    return false;
  }
  std::string path;
  if (!get_path(cx, args[0], "stat", path)) {
    return RejectPromiseWithPendingError(cx, promise);
  }
  struct stat info {};
  if (::stat(path.c_str(), &info) != 0) {
    return reject_errno(cx, promise, "stat", path, errno);
  }
  JS::RootedObject result(cx, JS_NewPlainObject(cx));
  JS::RootedString kind(cx, JS_NewStringCopyZ(cx, file_kind(info.st_mode)));
  if (!result || !kind ||
      !JS_DefineProperty(cx, result, "kind", kind, JSPROP_ENUMERATE) ||
      !JS_DefineProperty(cx, result, "size", double(info.st_size),
                         JSPROP_ENUMERATE)) {
    return RejectPromiseWithPendingError(cx, promise);
  }
  JS::RootedValue value(cx, JS::ObjectValue(*result));
  return resolve(cx, promise, value);
}

bool read_dir(JSContext *cx, unsigned argc, JS::Value *vp) {
  JS::CallArgs args = JS::CallArgsFromVp(argc, vp);
  if (!args.requireAtLeast(cx, "readDir", 1)) {
    return false;
  }
  JS::RootedObject promise(cx, new_promise(cx, args));
  if (!promise) {
    return false;
  }
  std::string path;
  if (!get_path(cx, args[0], "readDir", path)) {
    return RejectPromiseWithPendingError(cx, promise);
  }
  DIR *directory = ::opendir(path.c_str());
  if (!directory) {
    return reject_errno(cx, promise, "readDir", path, errno);
  }
  JS::RootedObject result(cx, JS::NewArrayObject(cx, 0));
  if (!result) {
    ::closedir(directory);
    return RejectPromiseWithPendingError(cx, promise);
  }
  size_t index = 0;
  errno = 0;
  while (dirent *entry = ::readdir(directory)) {
    if (!std::strcmp(entry->d_name, ".") || !std::strcmp(entry->d_name, "..")) {
      continue;
    }
    std::string child = path;
    if (!child.empty() && child.back() != '/') {
      child.push_back('/');
    }
    child += entry->d_name;
    struct stat info {};
    if (::lstat(child.c_str(), &info) != 0) {
      int error = errno;
      ::closedir(directory);
      return reject_errno(cx, promise, "readDir", child, error);
    }
    JS::RootedObject item(cx, JS_NewPlainObject(cx));
    JS::RootedString name(cx, JS_NewStringCopyZ(cx, entry->d_name));
    JS::RootedString kind(cx, JS_NewStringCopyZ(cx, file_kind(info.st_mode)));
    if (!item || !name || !kind ||
        !JS_DefineProperty(cx, item, "name", name, JSPROP_ENUMERATE) ||
        !JS_DefineProperty(cx, item, "kind", kind, JSPROP_ENUMERATE) ||
        !JS_SetElement(cx, result, index++, item)) {
      ::closedir(directory);
      return RejectPromiseWithPendingError(cx, promise);
    }
  }
  int error = errno;
  if (::closedir(directory) != 0 && error == 0) {
    error = errno;
  }
  if (error != 0) {
    return reject_errno(cx, promise, "readDir", path, error);
  }
  JS::RootedValue value(cx, JS::ObjectValue(*result));
  return resolve(cx, promise, value);
}

bool make_dir(JSContext *cx, unsigned argc, JS::Value *vp) {
  JS::CallArgs args = JS::CallArgsFromVp(argc, vp);
  if (!args.requireAtLeast(cx, "mkdir", 1)) {
    return false;
  }
  JS::RootedObject promise(cx, new_promise(cx, args));
  if (!promise) {
    return false;
  }
  std::string path;
  if (!get_path(cx, args[0], "mkdir", path)) {
    return RejectPromiseWithPendingError(cx, promise);
  }
  if (::mkdir(path.c_str(), 0777) != 0) {
    return reject_errno(cx, promise, "mkdir", path, errno);
  }
  return resolve(cx, promise);
}

bool remove_path(JSContext *cx, unsigned argc, JS::Value *vp) {
  JS::CallArgs args = JS::CallArgsFromVp(argc, vp);
  if (!args.requireAtLeast(cx, "remove", 1)) {
    return false;
  }
  JS::RootedObject promise(cx, new_promise(cx, args));
  if (!promise) {
    return false;
  }
  std::string path;
  if (!get_path(cx, args[0], "remove", path)) {
    return RejectPromiseWithPendingError(cx, promise);
  }
  if (::remove(path.c_str()) != 0) {
    return reject_errno(cx, promise, "remove", path, errno);
  }
  return resolve(cx, promise);
}

} // namespace

namespace sturnkey::filesystem {

bool install(api::Engine *engine) {
  JSContext *cx = engine->cx();
  JS::RootedObject module(cx, JS_NewPlainObject(cx));
  if (!module) {
    return false;
  }
  static constexpr JSFunctionSpec functions[] = {
      JS_FN("mkdir", make_dir, 1, JSPROP_ENUMERATE),
      JS_FN("readDir", read_dir, 1, JSPROP_ENUMERATE),
      JS_FN("readFile", read_file, 1, JSPROP_ENUMERATE),
      JS_FN("readTextFile", read_text_file, 1, JSPROP_ENUMERATE),
      JS_FN("remove", remove_path, 1, JSPROP_ENUMERATE),
      JS_FN("stat", stat_path, 1, JSPROP_ENUMERATE),
      JS_FN("writeFile", write_file, 2, JSPROP_ENUMERATE),
      JS_FN("writeTextFile", write_text_file, 2, JSPROP_ENUMERATE),
      JS_FS_END,
  };
  if (!JS_DefineFunctions(cx, module, functions)) {
    return false;
  }
  JS::RootedValue module_value(cx, JS::ObjectValue(*module));
  return engine->define_builtin_module("sturnkey:fs", module_value);
}

} // namespace sturnkey::filesystem
