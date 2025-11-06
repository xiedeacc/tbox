load("@tbox//bazel:common.bzl", "GLOBAL_COPTS", "GLOBAL_DEFINES", "GLOBAL_LINKOPTS", "GLOBAL_LOCAL_DEFINES")

COPTS = GLOBAL_COPTS + select({
    "@platforms//os:windows": [],
    "//conditions:default": [],
}) + select({
    "@platforms//os:linux": [],
    "@platforms//os:osx": [],
    "@platforms//os:windows": [],
    "//conditions:default": [],
})

DEFINES = GLOBAL_DEFINES

LOCAL_DEFINES = GLOBAL_LOCAL_DEFINES + select({
    "@platforms//os:windows": [],
    "//conditions:default": [],
}) + select({
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

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "mimalloc",
    srcs = [
        "src/static.c",
    ],
    hdrs = glob([
        "include/**/*.h",
        "src/*.h",
    ]),
    copts = COPTS + [
        "-fPIC",
    ] + select({
        # Disable C++ operator override for musl builds to avoid symbol conflicts
        "@tbox//bazel:musl_libc": [
            "-DMI_MALLOC_OVERRIDE=0",
        ],
        "//conditions:default": [
            "-DMI_MALLOC_OVERRIDE=1",
        ],
    }),
    defines = DEFINES,
    includes = [
        "include",
        "src",
    ],
    linkopts = LINKOPTS,
    local_defines = LOCAL_DEFINES,
    textual_hdrs = glob(
        [
            "src/**/*.c",
        ],
        exclude = [
            "src/static.c",
        ],
    ),
)
