# SeinARTS Agent Engineering Records

This dot-prefixed directory contains durable internal engineering context for contributors and future maintainers. It is not the public product documentation tree.

Use `WORKFLOW.md` for operational rules. Use the repository and plugin-level guides for technical invariants. Use the remaining files here for current state, accepted evidence, remaining work, and cross-session handoff context.

## Files

- `PROJECT_STATE.md` — current Git/build/test/runtime posture and the exact stabilization boundary.
- `WORKFLOW.md` — local operational mirror of the human Workflow Policy.
- `STYLE_GUIDE.md` — local operational mirror of the human Style Guide.
- `FRAMEWORK_MAP.md` — concise live architecture and algorithm map grounded in the current source.
- `PERFORMANCE_BASELINE.md` — repeatable benchmark contract, measured baseline, and profiling rules.
- `CONSUMER_VERIFICATION.md` — clean downstream project/build/cook/package verification contract.
- `READINESS_ROADMAP.md` — ordered path from the stabilized framework to a game-ready, online-capable SDK.
- `OPEN_RISKS.md` — unresolved correctness, state, extensibility, gameplay, and release risks.
- `PUBLIC_DOCS_BACKLOG.md` — pending public documentation updates awaiting a website task.

## Artifact policy

- Root `Docs/` is reserved for RJ's GitHub Pages documentation website. Keep it empty unless RJ
  explicitly opens a website task; agents must not independently author or restore content there.
- Do not recreate the website hierarchy under `.agents/`. Consolidate only durable engineering
  contracts into the existing records above.
- Agent-authored Markdown belongs here when it is durable. Short-lived exploration should stay out of Git.
- Generated PDFs do not belong in the repository. Put requested PDFs in the requesting user's Downloads directory.
- Raw Automation, profiling, logs, replays, and Unreal build products remain under ignored `Saved/`, `Binaries/`, `Intermediate/`, or external temporary capture directories.

Every status claim here should name its evidence boundary. Build-green is not a substitute for PIE behavior, and old audit prose is not a substitute for live code.
