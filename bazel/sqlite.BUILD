load("@tbox//bazel:common.bzl", "GLOBAL_COPTS", "GLOBAL_LINKOPTS", "GLOBAL_LOCAL_DEFINES")

package(default_visibility = ["//visibility:public"])

COPTS = GLOBAL_COPTS + select({
    "@tbox//bazel:not_cross_compiling_on_windows": [],
    "//conditions:default": [],
}) + select({
    "@platforms//os:linux": [],
    "@platforms//os:osx": [],
    "@platforms//os:windows": [],
    "//conditions:default": [],
})

LOCAL_DEFINES = GLOBAL_LOCAL_DEFINES + select({
    "@tbox//bazel:not_cross_compiling_on_windows": [],
    "//conditions:default": [],
}) + select({
    "@platforms//os:linux": [],
    "@platforms//os:osx": [],
    "@platforms//os:windows": [],
    "//conditions:default": [],
})

LINKOPTS = GLOBAL_LINKOPTS + select({
    "@tbox//bazel:not_cross_compiling_on_windows": [],
    "//conditions:default": [],
}) + select({
    "@platforms//os:linux": [],
    "@platforms//os:osx": [],
    "@platforms//os:windows": [],
    "//conditions:default": [],
})

cc_library(
    name = "sqlite",
    srcs = ["sqlite3.c"],
    hdrs = [
        "sqlite3.h",
        "sqlite3ext.h",
    ],
    copts = COPTS,
    linkopts = LINKOPTS + select({
        "@platforms//os:freebsd": ["-lpthread"],
        "@platforms//os:linux": [
            "-lpthread",
            "-ldl",
        ],
        "@platforms//os:netbsd": ["-lpthread"],
        "@platforms//os:openbsd": ["-lpthread"],
        "//conditions:default": [],
    }),
    local_defines = LOCAL_DEFINES,
)
