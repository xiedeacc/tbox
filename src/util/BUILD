load("@tbox//bazel:common.bzl", "GLOBAL_COPTS", "GLOBAL_LOCAL_DEFINES")
load("//bazel:build.bzl", "cc_test")
load("//bazel:cpplint.bzl", "cpplint")

package(default_visibility = ["//visibility:public"])

cpplint()

COPTS = GLOBAL_COPTS + select({
    "@tbox//bazel:not_cross_compiling_on_windows": [
        "/Iexternal/double-conversion",
        "/Iexternal/folly",
        "/Iexternal/libsodium/src/libsodium/include",
        "/I$(GENDIR)/external/folly",
    ],
    "//conditions:default": [
        "-isystem external/double-conversion",
        "-isystem external/folly",
        "-isystem external/libsodium/src/libsodium/include",
        "-isystem $(GENDIR)/external/folly",
    ],
}) + select({
    "@platforms//os:linux": [],
    "@platforms//os:osx": [],
    "@platforms//os:windows": [],
    "//conditions:default": [],
})

LOCAL_DEFINES = GLOBAL_LOCAL_DEFINES + select({
    "@tbox//bazel:not_cross_compiling_on_windows": [
        "GLOG_NO_ABBREVIATED_SEVERITIES",
    ],
    "//conditions:default": ["BUILD_TRACING=0"],
}) + select({
    "@platforms//os:linux": [],
    "@platforms//os:osx": [],
    "@platforms//os:windows": [],
    "//conditions:default": [],
})

cc_library(
    name = "config_manager",
    srcs = ["config_manager.cc"],
    hdrs = ["config_manager.h"],
    copts = COPTS,
    local_defines = LOCAL_DEFINES,
    deps = [
        ":util",
        "//src/proto:cc_config",
        "@folly",
    ],
)

cc_library(
    name = "sqlite_manager",
    srcs = ["sqlite_manager.cc"],
    hdrs = ["sqlite_manager.h"],
    copts = COPTS,
    local_defines = LOCAL_DEFINES,
    deps = [
        ":util",
        "//src/common:defs",
        "@com_github_glog_glog//:glog",
        "@folly",
        "@sqlite",
    ],
)

cc_library(
    name = "thread_pool",
    srcs = ["thread_pool.cc"],
    hdrs = ["thread_pool.h"],
    copts = COPTS,
    local_defines = LOCAL_DEFINES,
    deps = [
        ":config_manager",
        "@boost//:asio",
        "@com_github_glog_glog//:glog",
    ],
)

cc_test(
    name = "thread_pool_test",
    srcs = ["thread_pool_test.cc"],
    copts = COPTS,
    data = ["//conf:base_config.json"],
    local_defines = LOCAL_DEFINES,
    deps = [":thread_pool"],
)

cc_library(
    name = "util",
    srcs = ["util.cc"],
    hdrs = ["util.h"],
    copts = COPTS,
    local_defines = LOCAL_DEFINES,
    deps = [
        "//src/common:defs",
        "//src/common:error_code",
        "//src/proto:cc_service",
        "@blake3",
        "@boost//:algorithm",
        "@boost//:beast",
        "@boost//:iostreams",
        "@boost//:uuid",
        "@com_github_glog_glog//:glog",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/synchronization",
        "@com_google_absl//absl/time",
        "@com_google_protobuf//:protobuf",
        "@crc32c",
        "@fmt",
        "@folly",
        "@openssl",
        "@rapidjson",
        "@smhasher",
        "@xz//:lzma",
    ] + select({
        "@tbox//bazel:jemalloc": ["@jemalloc"],
        "@tbox//bazel:tcmalloc": ["@tcmalloc//tcmalloc"],
        "//conditions:default": [],
    }),
)

cc_library(
    name = "timer",
    hdrs = ["timer.h"],
    deps = [":util"],
)

cc_test(
    name = "util_test",
    timeout = "short",
    srcs = ["util_test.cc"],
    data = [
        "//conf:base_config.json",
        "//test_data/util_test",
    ],
    deps = [
        ":util",
        "//src/test:test_util",
    ],
)

cc_test(
    name = "config_manager_test",
    srcs = [
        "config_manager_test.cc",
    ],
    copts = COPTS,
    data = ["//conf:base_config.json"],
    local_defines = LOCAL_DEFINES,
    deps = [":config_manager"],
)
