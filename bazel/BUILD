package(default_visibility = ["//visibility:public"])

filegroup(
    name = "exported_testdata",
    srcs = glob(["**"]),
)

py_binary(
    name = "gen_local_config_git",
    srcs = ["gen_local_config_git.py"],
    python_version = "PY3",
    srcs_version = "PY3",
    tags = ["no-remote-exec"],
)

config_setting(
    name = "clang",
    values = {"compiler": "clang"},
)

config_setting(
    name = "gcc",
    values = {"compiler": "gcc"},
)

config_setting(
    name = "msvc",
    values = {"compiler": "msvc-cl"},
)

config_setting(
    name = "x64_small",
    define_values = {"cpu_tier": "small"},
)

config_setting(
    name = "x64_medium",
    define_values = {"cpu_tier": "medium"},
)

config_setting(
    name = "x64_large",
    define_values = {"cpu_tier": "large"},
)

config_setting(
    name = "windows_x86_64",
    constraint_values = [
        "@platforms//os:windows",
        "@platforms//cpu:x86_64",
    ],
)

config_setting(
    name = "windows_aarch64",
    constraint_values = [
        "@platforms//os:windows",
        "@platforms//cpu:aarch64",
    ],
)

config_setting(
    name = "linux_x86_64",
    constraint_values = [
        "@platforms//os:linux",
        "@platforms//cpu:x86_64",
    ],
)

config_setting(
    name = "linux_aarch64",
    constraint_values = [
        "@platforms//os:linux",
        "@platforms//cpu:aarch64",
    ],
)

config_setting(
    name = "osx_x86_64",
    constraint_values = [
        "@platforms//os:osx",
        "@platforms//cpu:x86_64",
    ],
)

config_setting(
    name = "osx_aarch64",
    constraint_values = [
        "@platforms//os:osx",
        "@platforms//cpu:aarch64",
    ],
)

platform(
    name = "windows_x86_64_platform",
    constraint_values = [
        "@platforms//os:windows",
        "@platforms//cpu:x86_64",
    ],
)

platform(
    name = "windows_aarch64_platform",
    constraint_values = [
        "@platforms//os:windows",
        "@platforms//cpu:aarch64",
    ],
)

platform(
    name = "linux_x86_64_platform",
    constraint_values = [
        "@platforms//os:linux",
        "@platforms//cpu:x86_64",
    ],
)

platform(
    name = "linux_aarch64_platform",
    constraint_values = [
        "@platforms//os:linux",
        "@platforms//cpu:aarch64",
    ],
)

platform(
    name = "osx_x86_64_platform",
    constraint_values = [
        "@platforms//os:osx",
        "@platforms//cpu:x86_64",
    ],
)

platform(
    name = "osx_aarch64_platform",
    constraint_values = [
        "@platforms//os:osx",
        "@platforms//cpu:aarch64",
    ],
)

config_setting(
    name = "jemalloc",
    define_values = {"profiling_type": "jemalloc"},
)

config_setting(
    name = "tcmalloc",
    define_values = {"profiling_type": "tcmalloc"},
)
