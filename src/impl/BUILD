load("@tbox//bazel:common.bzl", "GLOBAL_COPTS", "GLOBAL_LOCAL_DEFINES")
load("//bazel:build.bzl", "cc_test")
load("//bazel:cpplint.bzl", "cpplint")

package(default_visibility = ["//visibility:public"])

COPTS = GLOBAL_COPTS + select({
    "//conditions:default": [
        "-isystem external/double-conversion",
        "-isystem external/folly",
        "-isystem external/libsodium/src/libsodium/include",
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
    "//conditions:default": ["BUILD_TRACING=0"],
}) + select({
    "@platforms//os:linux": [],
    "@platforms//os:osx": [],
    "@platforms//os:windows": [],
    "//conditions:default": [],
})

cpplint()

cc_library(
    name = "config_manager",
    srcs = ["config_manager.cc"],
    hdrs = ["config_manager.h"],
    copts = COPTS,
    local_defines = LOCAL_DEFINES,
    deps = [
        "//src/proto:cc_config",
        "//src/util",
        "@folly",
    ],
)

cc_library(
    name = "ddns_manager",
    srcs = ["ddns_manager.cc"],
    hdrs = ["ddns_manager.h"],
    copts = COPTS,
    local_defines = LOCAL_DEFINES,
    deps = [
        ":config_manager",
        "//src/util",
        "@aws-sdk-cpp//:aws-cpp-sdk-core",
        "@aws-sdk-cpp//:aws-cpp-sdk-route53",
        "@com_github_glog_glog//:glog",
        "@folly",
    ],
)

cc_test(
    name = "config_manager_test",
    srcs = [
        "config_manager_test.cc",
    ],
    copts = COPTS,
    data = ["//conf:server_config.json"],
    local_defines = LOCAL_DEFINES,
    deps = [":config_manager"],
)

cc_library(
    name = "sqlite_manager",
    srcs = ["sqlite_manager.cc"],
    hdrs = ["sqlite_manager.h"],
    copts = COPTS,
    local_defines = LOCAL_DEFINES,
    deps = [
        "//src/common:defs",
        "//src/common:error_code",
        "//src/proto:cc_error",
        "//src/util",
        "@com_github_glog_glog//:glog",
        "@folly",
        "@sqlite",
    ],
)

cc_library(
    name = "user_manager",
    srcs = ["user_manager.cc"],
    hdrs = ["user_manager.h"],
    copts = COPTS,
    local_defines = LOCAL_DEFINES,
    deps = [
        ":session_manager",
        ":sqlite_manager",
        "//src/common:defs",
        "//src/proto:cc_grpc_service",
        "//src/util",
        "@folly",
        "@sqlite",
    ],
)

cc_library(
    name = "session_manager",
    srcs = ["session_manager.cc"],
    hdrs = ["session_manager.h"],
    copts = COPTS,
    local_defines = LOCAL_DEFINES,
    deps = [
        "//src/common:defs",
        "//src/util",
        "@folly",
    ],
)

cc_test(
    name = "user_manager_test",
    srcs = ["user_manager_test.cc"],
    copts = COPTS,
    data = ["//test_data/config_test:client_config.json"],
    local_defines = LOCAL_DEFINES,
    deps = [":user_manager"],
)

cc_test(
    name = "ddns_manager_test",
    srcs = ["ddns_manager_test.cc"],
    copts = COPTS,
    local_defines = LOCAL_DEFINES,
    deps = [
        ":ddns_manager",
        "@com_github_glog_glog//:glog",
        "@com_google_googletest//:gtest_main",
    ],
)
