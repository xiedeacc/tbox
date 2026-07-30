load("@rules_cc//cc:defs.bzl", "cc_library")
load("@tbox//bazel:common.bzl", "GLOBAL_COPTS", "GLOBAL_LOCAL_DEFINES")
load("//bazel:cpplint.bzl", "cpplint")

package(
    default_visibility = ["//visibility:public"],
    features = ["layering_check"],
)

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

# Backend independent interface plus the factory that selects an
# implementation. The factory pulls in every backend, so a binary that links
# ":dns" gets all of them.
cc_library(
    name = "dns",
    srcs = ["dns_provider_factory.cc"],
    hdrs = ["dns_provider.h"],
    copts = COPTS,
    local_defines = LOCAL_DEFINES,
    deps = [
        ":cloudflare_provider",
        ":route53_provider",
        "//src/common:logging",
        "//src/impl:config_manager",
    ],
)

cc_library(
    name = "route53_provider",
    srcs = ["route53_provider.cc"],
    hdrs = [
        "dns_provider.h",
        "route53_provider.h",
    ],
    copts = COPTS,
    local_defines = LOCAL_DEFINES,
    deps = [
        "//src/common:logging",
        "//src/impl:config_manager",
        "@aws-sdk-cpp//:aws-cpp-sdk-core",
        "@aws-sdk-cpp//:aws-cpp-sdk-route53",
    ],
)

cc_library(
    name = "cloudflare_provider",
    srcs = ["cloudflare_provider.cc"],
    hdrs = [
        "cloudflare_provider.h",
        "dns_provider.h",
    ],
    copts = COPTS,
    local_defines = LOCAL_DEFINES,
    deps = [
        "//src/common:logging",
        "//src/impl:config_manager",
        "@curl",
        "@folly",
        "@folly//:common",
    ],
)
