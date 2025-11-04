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
        "-isystem external/fizz",
        "-isystem external/libsodium/src/libsodium/include",
        "-isystem external/fizz",
        "-isystem external/wangle",
        "-isystem $(GENDIR)/external/folly",
        "-Iexternal/aws-sdk-cpp/crt/aws-crt-cpp/include",
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
    name = "websocket_client",
    srcs = ["websocket_client.cc"],
    hdrs = ["websocket_client.h"],
    copts = COPTS,
    linkopts = LINKOPTS,
    local_defines = LOCAL_DEFINES,
    deps = [
        "//src/proto:cc_service",
        "@boost//:asio",
        "@boost//:beast",
        "@boost//:json",
        "@com_github_glog_glog//:glog",
        "@folly",
        "@simdjson",
    ],
)

cc_library(
    name = "authentication_manager",
    srcs = ["authentication_manager.cc"],
    hdrs = ["authentication_manager.h"],
    copts = COPTS,
    linkopts = LINKOPTS,
    local_defines = LOCAL_DEFINES,
    deps = [
        "//src/impl:config_manager",
        "//src/proto:cc_grpc_service",
        "//src/util",
        "@com_github_glog_glog//:glog",
        "@com_github_grpc_grpc//:grpc++",
        "@folly",
    ],
)

cc_library(
    name = "report_manager",
    srcs = ["report_manager.cc"],
    hdrs = ["report_manager.h"],
    copts = COPTS,
    linkopts = LINKOPTS,
    local_defines = LOCAL_DEFINES,
    deps = [
        ":authentication_manager",
        ":ssl_config_manager",
        "//src/async_grpc",
        "//src/impl:config_manager",
        "//src/proto:cc_grpc_service",
        "//src/server/grpc_handler:meta",
        "//src/util",
        "@com_github_glog_glog//:glog",
        "@com_github_grpc_grpc//:grpc++",
        "@folly",
    ],
)

cc_library(
    name = "ssl_config_manager",
    srcs = ["ssl_config_manager.cc"],
    hdrs = ["ssl_config_manager.h"],
    copts = COPTS,
    linkopts = LINKOPTS,
    local_defines = LOCAL_DEFINES,
    deps = [
        "//src/util",
        "@com_github_glog_glog//:glog",
        "@folly",
    ],
)

cc_library(
    name = "grpc_client",
    srcs = ["grpc_client.cc"],
    hdrs = ["grpc_client.h"],
    copts = COPTS,
    linkopts = LINKOPTS,
    local_defines = LOCAL_DEFINES,
    deps = [
        ":authentication_manager",
        ":report_manager",
        ":ssl_config_manager",
        "//src/impl:config_manager",
        "//src/impl:ddns_manager",
        "//src/proto:cc_grpc_service",
        "//src/util",
        "@com_github_glog_glog//:glog",
        "@com_github_grpc_grpc//:grpc++",
        "@folly",
    ],
)

cc_test(
    name = "websocket_client_test",
    srcs = ["websocket_client_test.cc"],
    copts = COPTS,
    linkopts = LINKOPTS,
    local_defines = LOCAL_DEFINES,
    deps = [
        ":websocket_client",
        "//src/proto:cc_service",
        "//src/util",
        "@boost//:asio",
        "@boost//:beast",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "ssl_config_manager_test",
    srcs = ["ssl_config_manager_test.cc"],
    copts = COPTS,
    linkopts = LINKOPTS,
    local_defines = LOCAL_DEFINES,
    deps = [
        ":ssl_config_manager",
        "//src/util",
        "@com_github_glog_glog//:glog",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "authentication_manager_test",
    srcs = ["authentication_manager_test.cc"],
    copts = COPTS,
    linkopts = LINKOPTS,
    local_defines = LOCAL_DEFINES,
    deps = [
        ":authentication_manager",
        "//src/impl:config_manager",
        "//src/proto:cc_grpc_service",
        "//src/proto:cc_service",
        "@com_github_glog_glog//:glog",
        "@com_github_grpc_grpc//:grpc++",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "report_manager_test",
    srcs = ["report_manager_test.cc"],
    copts = COPTS,
    linkopts = LINKOPTS,
    local_defines = LOCAL_DEFINES,
    deps = [
        ":report_manager",
        "@com_github_glog_glog//:glog",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "grpc_client_test",
    srcs = ["grpc_client_test.cc"],
    copts = COPTS,
    linkopts = LINKOPTS,
    local_defines = LOCAL_DEFINES,
    deps = [
        ":grpc_client",
        "@com_github_glog_glog//:glog",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_binary(
    name = "tbox_client",
    srcs = ["client.cc"],
    copts = COPTS,
    linkopts = LINKOPTS,
    local_defines = LOCAL_DEFINES,
    deps = [
        ":grpc_client",
        ":websocket_client",
        "//src/impl:config_manager",
        "//src/proto:cc_grpc_service",
        "@boost//:url",
        "@folly",
        "@libsodium//:sodium",
        "@openssl//:crypto",
        "@openssl//:ssl",
        "@xz//:lzma",
        "@mimalloc//:mimalloc",
    ],
)
