load("@bazel_skylib//lib:selects.bzl", "selects")
load("@rules_cc//cc:defs.bzl", "cc_binary")
load("@rules_java//java:defs.bzl", "java_library")

selects.config_setting_group(
    name = "linux_musl",
    match_all = [
        "@platforms//os:linux",
        "@tbox//bazel:libc_musl",
    ],
)

cc_binary(
    name = "libjni_config_manger.so",
    linkshared = True,
    tags = ["ignore"],
    target_compatible_with = select({
        ":linux_musl": ["@platforms//:incompatible"],
        "@platforms//os:linux": [],
        "//conditions:default": ["@platforms//:incompatible"],
    }),
    visibility = ["//visibility:public"],
    deps = [
        "//src/swig:config_manager_swig_java_cc",
    ],
)

cc_binary(
    name = "libjni_config_manger.dylib",
    linkshared = True,
    tags = ["ignore"],
    target_compatible_with = select({
        "@platforms//os:osx": [],
        "//conditions:default": ["@platforms//:incompatible"],
    }),
    visibility = ["//visibility:public"],
    deps = [
        "//src/swig:config_manager_swig_java_cc",
    ],
)

cc_binary(
    name = "libjni_config_manger.dll",
    linkshared = True,
    tags = ["ignore"],
    target_compatible_with = select({
        "@platforms//os:windows": [],
        "//conditions:default": ["@platforms//:incompatible"],
    }),
    visibility = ["//visibility:public"],
    deps = [
        "//src/swig:config_manager_swig_java_cc",
    ],
)

java_library(
    name = "Loader",
    srcs = ["Loader.java"],
    tags = ["ignore"],
    target_compatible_with = select({
        ":linux_musl": ["@platforms//:incompatible"],
        "@platforms//os:linux": [],
        "@platforms//os:osx": [],
        "@platforms//os:windows": [],
        "//conditions:default": ["@platforms//:incompatible"],
    }),
    runtime_deps = select({
        "@platforms//os:linux": [":libjni_config_manger.so"],
        "@platforms//os:windows": [":libjni_config_manger.dll"],
        "@platforms//os:osx": [":libjni_config_manger.dylib"],
    }),
    deps = [
        "@maven//:net_java_dev_jna_jna",
    ],
)
