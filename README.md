# tbox

## 功能

* 集成常用 C/C++ 库，例如 grpc、protobuf、boost、abseil、folly、proxygen、mvfst、zstd、curl、openssl 等。
* 包含 grpc server/client 示例。
* 包含 http server/client 示例。
* 包含 protobuf plugin 示例。
* 包含 Java/Python swig 示例。
* 支持多操作系统，例如 Linux、macOS、Windows。
* 支持多 CPU 架构，例如 x86_64、aarch64。
* 支持 clang、gcc、msvc-cl 编译。
* 支持生成 `compile_commands.json`。
* 支持 CPU 和内存性能分析。
* 支持内存泄漏、内存破坏检测。
* 支持自动代码风格检查。

## DDNS 后端与 API Token

DDNS 支持多个 DNS 托管后端，实现位于 `src/impl/dns/`，每个后端一个文件：

| 后端 | 配置值 | 实现文件 |
| --- | --- | --- |
| Cloudflare | `cloudflare` | `src/impl/dns/cloudflare_provider.cc` |
| AWS Route 53 | `route53` | `src/impl/dns/route53_provider.cc` |

在 `conf/*.json` 中通过 `dns_provider` 选择后端，留空时默认为 `route53`。

### Cloudflare API Token

创建路径：

```
https://dash.cloudflare.com/profile/api-tokens
```

即 Cloudflare 控制台右上角头像 → **My Profile** → **API Tokens** → **Create Token**。

创建步骤：

1. 选择 **Create Custom Token**，填写便于识别的名称，例如 `tbox-ddns`。
2. **Permissions** 选择 `Zone` → `DNS` → `Edit`。
3. **Zone Resources** 选择 `Include` → `Specific zone`，逐个添加需要更新记录的域名。
   仅授予实际需要的 zone，避免使用账号级全局权限。
4. 可选：在 **Client IP Address Filtering** 中限制来源 IP；在 **TTL** 中设置有效期。
5. 创建后 token 只显示一次，请立即保存。

对应配置项：

```json
{
    "dns_provider": "cloudflare",
    "cloudflare_api_token": "<token>",
    "cloudflare_zone_id": ""
}
```

`cloudflare_zone_id` 留空时，程序会按域名逐级向上查询所属 zone，例如
`home.example.com` 会自动定位到 `example.com`，查询结果会缓存。

验证 token 是否可用：

```sh
curl -s -H "Authorization: Bearer <token>" \
  https://api.cloudflare.com/client/v4/user/tokens/verify
```

### AWS Route 53 API Token

Route 53 使用 IAM access key，创建路径：

```
https://console.aws.amazon.com/iam/home#/users
```

即 AWS 控制台 → **IAM** → **Users** → 选择用户 → **Security credentials** →
**Create access key**。

创建步骤：

1. 新建或选择一个专用 IAM 用户，例如 `tbox-ddns`。
2. 附加最小权限策略，仅允许操作目标 hosted zone：

```json
{
    "Version": "2012-10-17",
    "Statement": [
        {
            "Effect": "Allow",
            "Action": [
                "route53:ListResourceRecordSets",
                "route53:ChangeResourceRecordSets"
            ],
            "Resource": "arn:aws:route53:::hostedzone/<HOSTED_ZONE_ID>"
        },
        {
            "Effect": "Allow",
            "Action": "route53:ListHostedZones",
            "Resource": "*"
        }
    ]
}
```

3. 在 **Security credentials** 中创建 access key，secret 只显示一次。

对应配置项：

```json
{
    "dns_provider": "route53",
    "route53_hosted_zone_id": "<HOSTED_ZONE_ID>",
    "aws_access_key_id": "<access key id>",
    "aws_secret_access_key": "<secret access key>",
    "aws_region": "ap-southeast-1"
}
```

`route53_hosted_zone_id` 留空时，程序会调用 `ListHostedZones` 按域名查找。
`aws_access_key_id` 与 `aws_secret_access_key` 同时留空时，改用 AWS 默认凭证链
（环境变量、`~/.aws/credentials`、实例角色等）。

### 凭证注意事项

* 配置文件中的凭证为明文，请确保文件权限为 `600` 且不要提交到版本库。
* 迁移或停用某个后端后，及时到对应控制台吊销不再使用的 token 或 access key。
* 泄露后立即吊销并重新创建，两个平台都不支持查看已创建凭证的原文。

## 后续事项

1. compiler 使用 `@bazel_tools//tools/cpp:compiler`。
2. 使用 aspect 查找最合适的相对搜索路径。
3. 支持 transitive usage。
4. 增加 `renovate.json`。
5. 为 IWYU 准备匹配 LLVM 22 的 include-what-you-use binary，再接入 Bazel。

## 生成新仓库

```sh
./generate.sh repo_name
```

## 构建

```sh
bazel build //...
bazel build --config=gcc_aarch64_linux_musl //...
bazel build --config=gcc_aarch64_linux_gnu //...
bazel build --config=clang_aarch64_linux_gnu //...
bazel build --config=clang_aarch64_linux_musl //...
bazel build --config=clang_x86_64_linux_gnu //...
bazel build --config=clang_musl //...
bazel build --config=clang_gnu //...
```

当前支持的主要构建配置：

* `//...`：当前主机默认构建。
* `--config=gcc_aarch64_linux_musl`：GCC 交叉编译 AArch64 Linux musl，部署到 OpenWrt/AArch64 musl。
* `--config=gcc_aarch64_linux_gnu`：GCC 交叉编译 AArch64 Linux glibc。
* `--config=clang_aarch64_linux_gnu`：Clang 交叉编译 AArch64 Linux glibc。
* `--config=clang_aarch64_linux_musl`：Clang 交叉编译 AArch64 Linux musl。
* `--config=clang_x86_64_linux_gnu`：Clang 构建 x86_64 Linux glibc。
* `--config=clang_gnu`：Clang 构建当前主机架构的 Linux glibc。
* `--config=clang_musl`：Clang 构建当前主机架构的 Linux musl。

当前不支持 GCC Windows、GCC macOS 构建配置；Windows 只保留 MSVC 工具链方向，macOS 只保留 Clang/Apple 工具链方向。

常用检查：

```sh
bazel test --config=cpplint //...
bazel test --config=unit_test //...
bazel test --config=asan //...
bazel coverage //...
```

## 生成 compile_commands.json

```sh
clear && bazel run @hedron_compile_commands//:refresh_all
```

## KMS 激活

tbox_server 内嵌的 vlmcsd/KMS 服务是原生 TCP 流量，不是 HTTP 流量。即使把 vlmcsd 协议完整改成自己实现，Windows/Office KMS 客户端发送的仍然是 KMS RPC/TCP 协议，不会携带 HTTP path。因此不能通过 `/kms` 这类 path 与普通 HTTPS/HTTP 请求区分，也不能放在 nginx `http { location ... }` 中代理。

当前部署采用直连 KMS 默认端口 `1688` 的方式，不经过 nginx。AWS 上需要在 security group 中放行 `1688/TCP`，并确保 tbox_server 正在监听该端口。

Windows 激活：

```cmd
slmgr /skms kms.example.com
slmgr /ato
slmgr /dlv
```

Office 激活：

```cmd
cd "C:\Program Files\Microsoft Office\Office16"
cscript ospp.vbs /sethst:kms.example.com
cscript ospp.vbs /act
cscript ospp.vbs /dstatus
```

如果没有 DNS 名称，也可以直接使用 AWS 公网 IP：

```cmd
slmgr /skms <aws-public-ip>
cscript ospp.vbs /sethst:<aws-public-ip>
```

如果以后确实要使用非默认端口，可以通过命令显式指定端口，但仍然是 TCP 直连，不是 HTTP path 代理：

```cmd
slmgr /skms kms.example.com:16880
cscript ospp.vbs /sethst:kms.example.com
cscript ospp.vbs /setprt:16880
```

## OpenSSL AArch64 musl CPU 100% 排查方法

静态链接的 AArch64 musl 版本曾经出现过 OpenSSL CPU 特性探测导致的 100% CPU 自旋。现象是部署后的 `tbox_client` 或 `tbox_server` 持续占用 CPU，但日志没有继续推进。

排查步骤如下。

1. 确认高 CPU 进程和线程：

```sh
pidof tbox_client
ps -o pid,ppid,stat,pcpu,pmem,args -p "$(pidof tbox_client)"
top -H -p "$(pidof tbox_client)"
```

2. 判断是 syscall 阻塞还是用户态自旋。如果 `strace` 里几乎没有 syscall 推进，但 CPU 持续很高，通常说明进程在用户态循环：

```sh
strace -tt -f -p "$(pidof tbox_client)"
```

3. 使用调试器或 profiler 查看热点调用栈。问题版本的热点栈落在 OpenSSL AArch64 能力探测函数 `_armv8_sm3_probe`：

```sh
gdb -p "$(pidof tbox_client)"
(gdb) set pagination off
(gdb) thread apply all bt
```

如果目标机器上有 `perf`，也可以直接看热点符号：

```sh
perf top -p "$(pidof tbox_client)" -g
```

4. 确认二进制形态。这个问题出现在静态 AArch64 musl 路径上，因此需要确认二进制没有 interpreter，也没有 `DT_NEEDED` 动态依赖：

```sh
file /usr/local/tbox/bin/tbox_client
readelf -l /usr/local/tbox/bin/tbox_client | grep INTERP || true
readelf -d /usr/local/tbox/bin/tbox_client | grep NEEDED || true
```

5. 通过 A/B 测试确认问题来源。设置 `OPENSSL_armcap=0` 禁用 OpenSSL AArch64 能力探测后重新启动，如果 CPU 降下来，就可以确认问题在 OpenSSL ARM capability detection 路径，而不是 tbox 自己的事件循环：

```sh
OPENSSL_armcap=0 /usr/local/tbox/bin/tbox_client
```

部署到 systemd 或 procd 时，将同样的环境变量写入服务配置后重启服务。当前静态 AArch64 musl 部署保留这个 workaround；glibc 版本在验证中没有复现同样的 CPU 自旋。

## 单测、代码风格和静态分析

`clang-format`、`clang-tidy`、`clang-check`、Clang Static Analyzer 和 IWYU 不是替代关系：

* `clang-format` 只负责格式化代码，不理解项目语义，适合在提交前统一排版。
* `clang-tidy` 基于编译命令做静态分析，可以发现潜在 bug、性能问题、现代 C++ 迁移建议和可读性问题。项目已经通过 `bazel_clang_tidy` 接入 Bazel，并使用仓库根目录的 `.clang-tidy` 配置。
* Clang Static Analyzer 已通过 `.clang-tidy` 中的 `clang-analyzer-*` checks 纳入 `clang-tidy`。需要单独跑 analyzer 时，也可以使用 `scan-build`。
* `clang-check` 更偏向 AST/语法和编译命令排查，适合对单个文件做诊断，不建议作为全量默认检查入口。
* IWYU 用于清理 include 依赖。C++ 项目值得集成 IWYU，尤其是头文件膨胀、增量编译慢、include 依赖混乱时。不过当前 `storypku/bazel_iwyu` 的预编译 IWYU 0.20 基于旧 Clang，无法正确解析本项目 LLVM/libc++ 22 头文件；因此暂不启用 `--config=iwyu`，后续应先自建或获取匹配 LLVM 22 的 include-what-you-use，再接入 Bazel。
* `cpplint` 是当前 Bazel 已集成的轻量风格检查，仍然可以继续作为快速检查使用。

格式化代码：

```sh
./format.sh
```

推荐通过 Bazel aspect 运行 `clang-tidy`。由于默认 GCC 配置下 clang-tidy 对当前 GCC/sysroot include 组合会产生系统头诊断，建议和 Clang GNU 配置一起使用：

```sh
bazel build --config=clang_gnu --config=clang-tidy //src/util:util
bazel build --config=clang_gnu --config=clang-tidy //...
```

`clang-tidy` 报告会输出到 `bazel-bin` 下对应 target 的 `*.clang-tidy.yaml` 文件。

也可以生成或刷新 `compile_commands.json` 后，直接按文件运行本机 `clang-tidy`。注意本仓库的编译选项主要由 Bazel 生成，`compile_commands.json` 过期时会缺少外部依赖 include 路径并产生误报；全量检查优先使用上面的 Bazel aspect 命令。

```sh
bazel run @hedron_compile_commands//:refresh_all
clang-tidy -p . src/util/util.cc
```

批量检查 `src` 下的 C/C++ 文件：

```sh
find src -type f \( -name "*.cc" -o -name "*.cpp" -o -name "*.h" -o -name "*.hpp" \) \
  -print0 | xargs -0 -n1 clang-tidy -p .
```

如果只想看诊断、不自动改代码，直接使用上面的命令即可。如果确认某些 modernize/performance 修复安全，可以对单个文件加 `-fix`：

```sh
clang-tidy -p . src/util/util.cc -fix
```

使用 `clang-check` 对单个文件做 AST/编译命令诊断。这个命令同样依赖刚刷新过的 `compile_commands.json`，适合排查某个文件的编译命令是否完整，不作为默认全量检查入口：

```sh
/usr/lib/llvm-22/bin/clang-check -p . src/util/util.cc
```

单独运行 Clang Static Analyzer：

```sh
/usr/lib/llvm-22/bin/scan-build bazel build --config=clang_gnu //src/util:util
```

IWYU 后续接入目标：

```sh
# 等 include-what-you-use 与 LLVM/libc++ 22 匹配后再启用
bazel build --config=clang_gnu --config=iwyu //src/util:util
```

现有 Bazel 检查：

```sh
bazel test //... --config=unit_test # 单测
bazel test //... --config=cpplint   # 只跑 cpplint 检查
```

## 单测覆盖率

```sh
bazel coverage //... --test_tag_filters=-cpplint
genhtml bazel-out/k8-fastbuild/testlogs/src/common/host_spec_test/coverage.dat -o /zfs/www/coverage
genhtml --ignore-errors source bazel-out/k8-fastbuild/testlogs/src/common/host_spec_test/coverage.dat -o /zfs/www/test_coverage
https://coverage.xiamu.com
```

## 内存泄漏检测

通过 sanitizer 配置运行 ASan/LSan：

```sh
bazel test --config=sanitize //...
```

## 内存破坏检测

```sh
bazel test --config=sanitize //...
```

## CPU 性能分析

```sh
bazel test --test_env="CPUPROFILE=prof.out" //src/common:host_spec_test # prof.out 在 bazel 构建根目录下
CPUPROFILE=prof.out bazel-bin/src/common/host_spec_test

pprof --web bazel-bin/src/common/host_spec_test prof.out
pprof --text ./bazel-bin/src/common/host_spec_test prof.out
pprof --pdf ./bazel-bin/src/common/host_spec_test prof.out > profile.pdf

perf record -F 99 -g bazel-bin/src/demo 10000
perf script | /root/src/software/FlameGraph/stackcollapse-perf.pl | /root/src/software/FlameGraph/flamegraph.pl > flamegraph.svg

https://gperftools.github.io/gperftools/heapprofile.html
https://gperftools.github.io/gperftools/cpuprofile.html
https://gperftools.github.io/gperftools/heap_checker.html
```

## 其它 Bazel 命令

```sh
bazel query --notool_deps --noimplicit_deps "deps(//src/server:grpc_server)" --output graph
bazel query 'attr(visibility, "//visibility:public", //:*)'
bazel query "rdeps(//..., //src/util:util)"
bazel query "rdeps(//..., @com_google_protobuf//:protobuf)"
bazel query 'deps(//src/server:grpc_server)' --output graph > graph.in
bazel query --noimplicit_deps 'deps(//:main)' --output graph > simplified_graph.in
dot -Tpng < graph.in > graph.png
```

## strace

```sh
cat /proc/self/stack
cat /proc/21880/stack
strace -Ff -tt -p 56509 2>&1 | tee strace.log
pstack 56509
```

## 注意事项

* 如果需要交叉编译，需要准备对应目标平台的编译工具、sysroot 和运行时库。
* 将 `code.xiamu.com` 替换为 `github.com`。
* 如需禁用自定义下载器配置，可注释 `.bazelrc` 中的 `common --experimental_downloader_config=.bazel_downloader.cfg`。
* 需要安装 `gperf`。
