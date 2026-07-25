# Toolchain And Sysroot Migration Plan

## Target layout

Keep the three independently versioned layers separate:

1. Host compiler toolchain
   - GCC 16.1.0 cross compiler binaries, target-specific GCC headers and binutils.
   - Clang/LLVM 22.1.2 host binaries, LLD and compiler resource headers.
2. Target system sysroot
   - Linux UAPI headers.
   - glibc or musl headers.
   - libc startup objects (`crt1.o`, `Scrt1.o`, `crti.o`, `crtn.o`).
   - libc and target system libraries.
   - No GCC, libstdc++, Clang, libc++, compiler-rt or versioned compiler headers.
3. Target C++ runtime
   - GCC variants: target libstdc++, libsupc++ and libgcc plus target-specific
     libstdc++ configuration headers.
   - Clang variants: target libc++, libc++abi, libunwind and compiler-rt.
   - One runtime archive per architecture, libc and compiler combination.

## Published archives

System sysroots:

- `linux-x86_64-gnu_sysroot.tar.gz`
- `linux-x86_64-musl_sysroot.tar.gz`
- `linux-aarch64-gnu_sysroot.tar.gz`
- `linux-aarch64-musl_sysroot.tar.gz`

Target runtimes:

- `gcc16.1.0-linux-x86_64-gnu_runtime.tar.gz`
- `gcc16.1.0-linux-x86_64-musl_runtime.tar.gz`
- `gcc16.1.0-linux-aarch64-gnu_runtime.tar.gz`
- `gcc16.1.0-linux-aarch64-musl_runtime.tar.gz`
- `clang22.1.2-linux-x86_64-gnu_runtime.tar.gz`
- `clang22.1.2-linux-x86_64-musl_runtime.tar.gz`
- `clang22.1.2-linux-aarch64-gnu_runtime.tar.gz`
- `clang22.1.2-linux-aarch64-musl_runtime.tar.gz`

All archives are published under `/opt/www/files` on `nas` and consumed from
`https://rgit.xiedeacc.com/files/` with pinned SHA-256 checksums.

## Execution checklist

- [x] Confirm host Bazel 9.2.0, GCC 16 and Clang 22.
- [x] Audit old sysroots and identify mixed GCC 14 runtime files.
- [x] Add separate sysroot/runtime support to `cc_toolchains`.
- [x] Build and publish the four system sysroots.
- [x] Build and publish the four GCC 16.1.0 runtimes.
- [x] Build and publish the four Clang 22.1.2 runtimes.
- [x] Update tbox sysroot declarations and checksums.
- [x] Run focused C and C++ compile/link smoke tests for every combination.
- [x] Run the complete eight-command Bazel build matrix.
- [x] Replace local aws-sdk and cc_toolchains overrides with shallow Git fetches.
- [x] Re-run the default build with remote dependencies.
- [x] Deploy `tbox_client` to NAS.
- [x] Build `clang_aarch64_linux_musl` and deploy `tbox_client` to OpenWrt.
- [x] Deploy `tbox_server` to AWS.
- [x] Verify services and binaries on all three hosts.
- [x] Commit and push tbox. `tbox` is pushed at
  `199dc9939e0dfc42d13b035ce3b24d65b179690a`; `cc_toolchains` is already
  pushed at `053ccff396d7f2a3be29b8e426d8c6df081d8221`.

## Build acceptance targets

Run these commands serially from `/root/src/cpp/tbox`. Record the exit status,
elapsed time and final log path for each command.

- [x] `bazel build //...` (4524s, `/tmp/tbox-build-default-post-vlmcsd.log`)
- [x] `bazel build --config=gcc_aarch64_linux_musl //...` (2810s, `/tmp/tbox-build-gcc_aarch64_linux_musl-post-vlmcsd.log`)
- [x] `bazel build --config=gcc_aarch64_linux_gnu //...` (4903s, `/tmp/tbox-build-gcc_aarch64_linux_gnu-post-vlmcsd.log`)
- [x] `bazel build --config=clang_aarch64_linux_gnu //...` (4009s, `/tmp/tbox-build-clang_aarch64_linux_gnu-post-vlmcsd.log`)
- [x] `bazel build --config=clang_aarch64_linux_musl //...` (4158s, `/tmp/tbox-build-clang_aarch64_linux_musl-post-vlmcsd.log`)
- [x] `bazel build --config=clang_x86_64_linux_gnu //...` (638s, `/tmp/tbox-build-clang_x86_64_linux_gnu-post-vlmcsd.log`)
- [x] `bazel build --config=clang_musl //...` (4169s, `/tmp/tbox-build-clang_musl-post-vlmcsd.log`)
- [x] `bazel build --config=clang_gnu //...` (4264s, `/tmp/tbox-build-clang_gnu-post-vlmcsd.log`)

## Deployment acceptance targets

Deploy only binaries produced by the matching successful build.

- [x] NAS client
  - Build: `bazel build //src/client:tbox_client`
  - Deploy with `deploy/deploy_nas_client.sh`.
  - Verified `tbox_client.service` is active, the deployed process remains
    running, logs are written under `/opt/usr/local/tbox/logs`, and five CPU
    samples remained between 0.1% and 0.2%.
- [x] OpenWrt client
  - Build: `bazel build --config=clang_aarch64_linux_musl //src/client:tbox_client`
  - Deploy with `deploy/deploy_openwrt_client.sh`.
  - Verified the procd service is running from `/usr/local/tbox/bin`, the
    configuration is under `/usr/local/tbox/conf`, logs are under
    `/usr/local/tbox/logs`, the binary has no interpreter or `DT_NEEDED`
    entries, and five CPU samples remained at 0%.
- [x] AWS server
  - Build: `bazel build --config=gcc_aarch64_linux_musl //src/server:tbox_server`
  - Deploy with `deploy/deploy_server.sh`.
  - Verified `tbox_server.service` is active, logs are under
    `/usr/local/tbox/logs`, and the AArch64 binary has no interpreter or
    `DT_NEEDED` entries. The process maps no shared libraries, five CPU samples
    remained between 0.4% and 0.5%, HTTP and gRPC listeners are active, and the
    integrated vlmcsd endpoint accepts TCP connections on port 1688.
  - Rebuilt embedded vlmcsd with `USE_THREADS` and `-pthread` so activation
    requests are handled by pthread workers instead of forked child processes.
    After redeploying to AWS, repeated local TCP connects to port 1688 showed
    `zombies_before=0 zombies_after=0`.
