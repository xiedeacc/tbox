# Agent Rules

- For deployments that touch AWS, OpenWrt, and NAS, build and deploy AWS and OpenWrt first. Both AWS and OpenWrt are `aarch64` targets, so let those builds run before the NAS build to reuse cross-build cache where possible.
- After AWS and OpenWrt are complete, build and deploy NAS.
- Project operating rules must support both Codex and Claude Code. When changing these rules, update both `AGENTS.md` and `CLAUDE.md` in the same change.
