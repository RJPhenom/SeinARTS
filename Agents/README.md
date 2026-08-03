# SeinARTS Agent Engineering Records

This directory contains durable internal engineering context for Codex, Claude, and future maintainers. It is not the public product documentation tree.

Use the repository and plugin-level `AGENTS.md` files for hard working rules and module-specific invariants. Use the files here for current state, accepted evidence, remaining work, and cross-session handoff context.

## Files

- `PROJECT_STATE.md` — current Git/build/test/runtime posture and the exact stabilization boundary.
- `FRAMEWORK_MAP.md` — concise live architecture and algorithm map grounded in the current source.
- `PERFORMANCE_BASELINE.md` — repeatable benchmark contract, measured baseline, and profiling rules.
- `CONSUMER_VERIFICATION.md` — clean downstream project/build/cook/package verification contract.
- `READINESS_ROADMAP.md` — ordered path from the stabilized framework to a game-ready, online-capable SDK.
- `OPEN_RISKS.md` — unresolved correctness, state, extensibility, gameplay, and release risks.

## Artifact policy

- `Docs/` is reserved for deliberate user/developer documentation built for the product. Do not put audit scratch, agent handoffs, or generated reports there.
- Agent-authored Markdown belongs here when it is durable. Short-lived exploration should stay out of Git.
- Generated PDFs do not belong in the repository. Put requested PDFs in the requesting user's Downloads directory.
- Raw Automation, profiling, logs, replays, and Unreal build products remain under ignored `Saved/`, `Binaries/`, `Intermediate/`, or external temporary capture directories.

Every status claim here should name its evidence boundary. Build-green is not a substitute for PIE behavior, and old audit prose is not a substitute for live code.
