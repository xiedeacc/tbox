load("@tbox//bazel:common.bzl", "GLOBAL_COPTS", "GLOBAL_LINKOPTS", "GLOBAL_LOCAL_DEFINES")
load("//bazel:cpplint.bzl", "cpplint")

package(default_visibility = ["//visibility:public"])

cpplint()

cc_library(
    name = "tcp_handler",
    srcs = ["vlmcsd_handler.cc"],
    hdrs = ["vlmcsd_handler.h"],
    copts = GLOBAL_COPTS,
    linkopts = GLOBAL_LINKOPTS,
    local_defines = GLOBAL_LOCAL_DEFINES,
    deps = [
        "//src/common:logging",
        "@vlmcsd//:libkms",
    ],
)
