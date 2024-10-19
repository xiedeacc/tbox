systemctl enable tbox.service
systemctl start tbox.service
systemctl daemon-reload

bazel run @hedron_compile_commands//:refresh_all
bazel run //:refresh_compile_commands
