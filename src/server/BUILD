load("@tbox//bazel:common.bzl", "GLOBAL_COPTS", "GLOBAL_LINKOPTS", "GLOBAL_LOCAL_DEFINES", "version_info")
load("//bazel:build.bzl", "cc_test")
load("//bazel:cpplint.bzl", "cpplint")

package(default_visibility = ["//visibility:public"])

cpplint()

COPTS = GLOBAL_COPTS + select({
    "@platforms//os:windows": [
        "/Iexternal/double-conversion",
        "/Iexternal/folly",
        "/Iexternal/double-conversion",
        "/Iexternal/proxygen",
        "/I$(GENDIR)/external/proxygen",
        "/I$(GENDIR)/external/fizz",
        "/Iexternal/libsodium/src/libsodium/include",
        "/Iexternal/fizz",
        "/Iexternal/wangle",
        "/I$(GENDIR)/external/folly",
    ],
    "//conditions:default": [
        "-isystem external/double-conversion",
        "-isystem external/folly",
        "-isystem external/double-conversion",
        "-isystem external/proxygen",
        "-isystem $(GENDIR)/external/proxygen",
        "-isystem $(GENDIR)/external/fizz",
        "-isystem external/libsodium/src/libsodium/include",
        "-isystem external/fizz",
        "-isystem external/wangle",
        "-isystem $(GENDIR)/external/folly",
        "-I$(GENDIR)/external/aws-sdk-cpp/crt/aws-c-common/generated/include",
        "-I$(GENDIR)/external/aws-sdk-cpp/crt/aws-crt-cpp/generated/include",
        "-Iexternal/aws-sdk-cpp/crt/aws-crt-cpp/crt/aws-c-auth/include",
        "-Iexternal/aws-sdk-cpp/crt/aws-crt-cpp/crt/aws-c-cal/include",
        "-Iexternal/aws-sdk-cpp/crt/aws-crt-cpp/crt/aws-c-common/include",
        "-Iexternal/aws-sdk-cpp/crt/aws-crt-cpp/crt/aws-c-compression/include",
        "-Iexternal/aws-sdk-cpp/crt/aws-crt-cpp/crt/aws-c-event-stream/include",
        "-Iexternal/aws-sdk-cpp/crt/aws-crt-cpp/crt/aws-c-http/include",
        "-Iexternal/aws-sdk-cpp/crt/aws-crt-cpp/crt/aws-c-io/include",
        "-Iexternal/aws-sdk-cpp/crt/aws-crt-cpp/crt/aws-c-mqtt/include",
        "-Iexternal/aws-sdk-cpp/crt/aws-crt-cpp/crt/aws-c-s3/include",
        "-Iexternal/aws-sdk-cpp/crt/aws-crt-cpp/crt/aws-c-sdkutils/include",
        "-Iexternal/aws-sdk-cpp/crt/aws-crt-cpp/crt/aws-checksums/include",
        "-I$(GENDIR)/external/aws-sdk-cpp/crt/aws-c-common/generated/include",
        "-I$(GENDIR)/external/aws-sdk-cpp/crt/aws-crt-cpp/generated/include",
        "-Iexternal/aws-sdk-cpp/crt/aws-crt-cpp/crt/aws-c-common/source",
        "-Iexternal/aws-sdk-cpp/crt/aws-crt-cpp/crt/aws-c-common/source/external/libcbor",
        "-Iexternal/aws-sdk-cpp/crt/aws-crt-cpp/crt",
        "-Iexternal/aws-sdk-cpp/crt/aws-crt-cpp/include",
        "-Iexternal/aws-sdk-cpp/src/aws-cpp-sdk-core/include/aws/core",
    ],
}) + select({
    "@platforms//os:linux": [],
    "@platforms//os:osx": [],
    "@platforms//os:windows": [],
    "//conditions:default": [],
})

LOCAL_DEFINES = GLOBAL_LOCAL_DEFINES + select({
    "@platforms//os:linux": [],
    "@platforms//os:osx": [],
    "@platforms//os:windows": [],
    "//conditions:default": [],
})

LINKOPTS = GLOBAL_LINKOPTS + select({
    "@platforms//os:windows": [],
    "//conditions:default": [],
}) + select({
    "@platforms//os:linux": [],
    "@platforms//os:osx": [],
    "@platforms//os:windows": [],
    "//conditions:default": [],
})

cc_library(
    name = "server_context",
    hdrs = ["server_context.h"],
    local_defines = LOCAL_DEFINES,
    deps = [
        ":version_info",
        "//src/impl:config_manager",
        "@fmt",
    ],
)

version_info(
    name = "version_info_h",
    out = "version_info.h",
    exec_tool = "//bazel:gen_local_config_git",
)

cc_library(
    name = "version_info",
    hdrs = [":version_info_h"],
    visibility = ["//visibility:public"],
)

cc_test(
    name = "version_info_test",
    srcs = ["version_info_test.cc"],
    deps = [":version_info"],
)

cc_library(
    name = "grpc_server_impl",
    hdrs = ["grpc_server_impl.h"],
    copts = COPTS,
    local_defines = LOCAL_DEFINES,
    deps = [
        "//src/async_grpc",
        "//src/impl:config_manager",
        "//src/proto:cc_grpc_service",
        "//src/server:server_context",
        "//src/server/grpc_handler",
    ],
)

cc_library(
    name = "http_server_impl",
    hdrs = ["http_server_impl.h"],
    copts = COPTS,
    local_defines = LOCAL_DEFINES,
    deps = [
        "//src/impl:config_manager",
        "//src/server:server_context",
        "@proxygen",
    ],
)

cc_binary(
    name = "tbox_server",
    srcs = ["server.cc"],
    copts = COPTS,
    linkopts = LINKOPTS,
    local_defines = LOCAL_DEFINES,
    deps = [
        ":grpc_server_impl",
        ":http_server_impl",
        ":server_context",
        "//src/impl:ddns_manager",
        "//src/impl:user_manager",
        "//src/proto:cc_grpc_service",
        "//src/server/http_handler",
        "@boost//:url",
        "@folly",
        "@libsodium//:sodium",
        "@openssl//:crypto",
        "@openssl//:ssl",
        "@xz//:lzma",
        #"@jemalloc",
    ],
)
