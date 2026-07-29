load("@rules_cc//cc:defs.bzl", "cc_library")
load("//bazel:cpplint.bzl", "cpplint")

package(
    default_visibility = ["//visibility:public"],
    features = ["layering_check"],
)

cpplint()

cc_library(
    name = "test_main",
    srcs = ["test_main.cc"],
    copts = [
        "-isystem external/double-conversion",
        "-isystem external/folly",
        "-isystem $(GENDIR)/external/folly",
    ],
    deps = [
        "//src/common:logging",
        "@com_google_googletest//:gtest",
        "@folly",
        "@folly//:common",
        "@gflags",
    ],
)
