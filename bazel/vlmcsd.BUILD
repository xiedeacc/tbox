load("@tbox//bazel:common.bzl", "GLOBAL_COPTS", "GLOBAL_DEFINES", "GLOBAL_LINKOPTS")

package(default_visibility = ["//visibility:public"])

VLMCSD_DEFINES = [
    "SIMPLE_SOCKETS",
    "NO_TIMEOUT",
    "NO_SIGHUP",
    "NO_CL_PIDS",
    "NO_LOG",
    "NO_RANDOM_EPID",
    "NO_INI_FILE",
    "NO_HELP",
    "NO_CUSTOM_INTERVALS",
    "NO_PID_FILE",
    "NO_USER_SWITCH",
    "NO_VERBOSE_LOG",
    "NO_LIMIT",
    "NO_VERSION_INFORMATION",
    "NO_PRIVATE_IP_DETECT",
    "NO_STRICT_MODES",
    "NO_CLIENT_LIST",
    "NO_TAP",
    "NO_EXTERNAL_DATA",
    "UNSAFE_DATA_LOAD",
    "SIMPLE_RPC",
    "IS_LIBRARY=1",
    "VERSION=\\\"vlmcsd-embedded\\\"",
    "_CRYPTO_INTERNAL",
]

cc_library(
    name = "libkms",
    srcs = [
        "src/crypto.c",
        "src/crypto_internal.c",
        "src/endian.c",
        "src/helpers.c",
        "src/kms.c",
        "src/libkms.c",
        "src/network.c",
        "src/output.c",
        "src/rpc.c",
        "src/shared_globals.c",
        "src/vlmcs.c",
    ],
    hdrs = glob(["src/*.h"]),
    copts = GLOBAL_COPTS + [
        "-Wno-discarded-qualifiers",
        "-Wno-incompatible-pointer-types",
        "-Wno-missing-field-initializers",
        "-Wno-sign-compare",
        "-Wno-unused-function",
        "-Wno-unused-parameter",
    ],
    defines = GLOBAL_DEFINES + VLMCSD_DEFINES,
    includes = ["src"],
    linkopts = GLOBAL_LINKOPTS,
)
