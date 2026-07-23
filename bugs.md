# Known Bugs

## fbthrift `thrift1` crashes when built as an x86_64 musl host tool

- Date observed: 2026-07-23
- Status: worked around in Bazel toolchain/platform configuration
- Affected path: fbthrift compiler host tool, especially `thrift1`
- Observed with: `bazel build --config=clang_musl //...` before separating target and exec libc constraints
- Not observed with: `bazel build --config=gcc_aarch64_linux_musl //...` in the earlier setup, because musl was only the target libc while host/exec tools were not built as runnable x86_64 musl binaries

### Symptom

When `clang_musl` allowed Bazel to build host/exec tools with the x86_64 musl C++ toolchain, the generated fbthrift compiler binary crashed while running during code generation:

```text
thrift1 ... type_system.thrift
Segmentation fault
```

The local gdb backtrace pointed at:

```text
std::__1::ctype<char>::do_toupper(char) const
apache::thrift::compiler::is_reserved_identifier(...)
```

The crash path comes from fbthrift's `thrift/compiler/sema/reserved_identifier.h`, which currently checks the reserved prefix with:

```cpp
boost::algorithm::istarts_with(after_underscores, prefix, std::locale::classic())
```

That call enters the C++ locale/ctype implementation. The crash appears specific to the x86_64 musl-built host tool and the current clang/libc++/musl runtime combination.

### Public reports checked

No exact upstream report was found at the time of writing:

- fbthrift issues search for `musl`: https://github.com/facebook/fbthrift/issues?q=musl
- fbthrift issues search for `do_toupper`: https://github.com/facebook/fbthrift/issues?q=do_toupper
- llvm-project issues search for `libc++ musl ctype do_toupper`: https://github.com/llvm/llvm-project/issues?q=libc%2B%2B+musl+ctype+do_toupper

This does not prove the issue is unknown; it only means no directly matching public GitHub issue was found by these searches.

### Current workaround

The Bazel configuration now distinguishes libc at the platform/toolchain level:

- target platform for `clang_musl`: x86_64 Linux musl
- exec/host platform for `clang_musl`: x86_64 Linux glibc

This keeps final build outputs on musl while ensuring build-time tools such as `thrift1` are built and executed with the glibc toolchain. With this workaround:

```text
bazel build --config=clang_musl //...
bazel build --config=clang_gnu //...
```

both complete successfully.

### Possible code patch

A small fbthrift patch is plausible. `is_reserved_identifier` only needs to detect the ASCII prefix `fbthrift` after leading underscores, so it does not need locale-aware case conversion. Replace the Boost locale-based comparison with a fixed ASCII case-insensitive prefix check.

Sketch:

```cpp
inline char ascii_tolower(char c) {
  return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

inline bool ascii_istarts_with(std::string_view value, std::string_view prefix) {
  if (value.size() < prefix.size()) {
    return false;
  }
  for (size_t i = 0; i < prefix.size(); ++i) {
    if (ascii_tolower(value[i]) != prefix[i]) {
      return false;
    }
  }
  return true;
}
```

Then:

```cpp
return ascii_istarts_with(after_underscores, prefix);
```

This should avoid `std::locale::classic()` and `std::__1::ctype<char>::do_toupper` entirely. It is a narrower behavioral change than disabling musl host tools globally, but it would need to be carried as an fbthrift patch and revalidated whenever fbthrift is upgraded.

