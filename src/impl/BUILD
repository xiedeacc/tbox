load("@tbox//bazel:common.bzl", "GLOBAL_COPTS", "GLOBAL_LOCAL_DEFINES")
load("//bazel:build.bzl", "cc_test")
load("//bazel:cpplint.bzl", "cpplint")

package(default_visibility = ["//visibility:public"])

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

cpplint()

cc_library(
    name = "user_manager",
    srcs = ["user_manager.cc"],
    hdrs = ["user_manager.h"],
    copts = COPTS,
    local_defines = LOCAL_DEFINES,
    deps = [
        ":session_manager",
        "//src/common:defs",
        "//src/proto:cc_grpc_service",
        "//src/util",
        "//src/util:sqlite_manager",
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
    data = ["//test_data/config_test:base_config.json"],
    local_defines = LOCAL_DEFINES,
    deps = [":user_manager"],
)
