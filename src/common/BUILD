load("//bazel:build.bzl", "cc_test")
load("//bazel:cpplint.bzl", "cpplint")
load("//bazel:pb_code_gen_plugin.bzl", "cc_proto_plugin")

package(default_visibility = ["//visibility:public"])

cpplint()

cc_library(
    name = "defs",
    hdrs = ["defs.h"],
    deps = [
        ":blocking_queue",
        ":error_code",
        "//src/proto:cc_error",
        "//src/proto:cc_service",
    ],
)

cc_library(
    name = "blocking_queue",
    hdrs = ["blocking_queue.h"],
)

cc_proto_plugin(
    name = "error_code",
    src = "//src/proto:error.proto",
    outs = ["error.h"],
    data = ["//src/common/template:error.h"],
    plugin = "//src/common/proto_plugin:pb_code_generator",
    proto_deps = [
        "//src/proto:error.proto",
    ],
    proto_paths = [
        ".",
    ],
    protoc = "@com_google_protobuf//:protoc",
    template_dir = "src/common/template",
)
