load("//bazel:build.bzl", "cc_test")
load("//bazel:cpplint.bzl", "cpplint")
load("//bazel:pb_code_gen_plugin.bzl", "cc_proto_plugin")

package(default_visibility = ["//visibility:public"])

cpplint()

cc_binary(
    name = "pb_code_generator",
    srcs = [
        "pb_code_generator.cc",
        "pb_code_generator.h",
    ],
    linkstatic = True,
    deps = [
        "@boost//:algorithm",
        "@com_github_glog_glog//:glog",
        "@com_google_absl//absl/strings",
        "@com_google_protobuf//:protoc_lib",
    ],
)
