# Claude Code Rules

- For deployments that touch AWS, OpenWrt, and NAS, build and deploy AWS and OpenWrt first. Both AWS and OpenWrt are `aarch64` targets, so let those builds run before the NAS build to reuse cross-build cache where possible.
- After AWS and OpenWrt are complete, build and deploy NAS.
- Project operating rules must support both Codex and Claude Code. When changing these rules, update both `AGENTS.md` and `CLAUDE.md` in the same change.

## Shared Assistant And Deployment Rules

- Deployment scripts must use explicit `ssh user@hostname` and `scp user@hostname:path` forms with hostnames or host aliases. Do not hard-code raw IP addresses in deployment commands; put host aliases in SSH config or project configuration instead.
- Deployment scripts must not generate long-lived systemd units or OpenWrt procd init scripts from heredocs, checked-in templates, or checked-in init files. If a service file must be created or migrated once, generate it with a temporary command and install it directly on the target host, then remove the generator/template from the repository.
