load("@tbox//bazel:common.bzl", "GLOBAL_COPTS")
load("//bazel:build.bzl", "cc_test")
load("//bazel:cpplint.bzl", "cpplint")

package(default_visibility = ["//visibility:public"])

cpplint()

COPTS = GLOBAL_COPTS + select({
    "@tbox//bazel:not_cross_compiling_on_windows": [
        "/Iexternal/double-conversion",
        "/Iexternal/folly",
        "/I$(GENDIR)/external/folly",
        "/Iexternal/proxygen",
        "/I$(GENDIR)/external/proxygen",
        "/Iexternal/wangle",
    ],
    "//conditions:default": [
        "-isystem external/double-conversion",
        "-isystem external/folly",
        "-isystem $(GENDIR)/external/folly",
        "-isystem external/proxygen",
        "-isystem $(GENDIR)/external/proxygen",
        "-isystem external/wangle",
    ],
}) + select({
    "@platforms//os:linux": [],
    "@platforms//os:osx": [],
    "@platforms//os:windows": [],
    "//conditions:default": [],
})

cc_library(
    name = "http_handler",
    srcs = [],
    hdrs = [
        "http_handler_factory.h",
        "server_handler.h",
        "user_handler.h",
        "util.h",
    ],
    copts = COPTS,
    deps = [
        "//src/server/handler_proxy",
        "//src/util",
        "//src/util:config_manager",
        "//src/util:timer",
        "@multipart-parser",
        "@proxygen",
    ],
)

cc_test(
    name = "util_test",
    srcs = ["util_test.cc"],
    copts = COPTS,
    deps = [":http_handler"],
)
