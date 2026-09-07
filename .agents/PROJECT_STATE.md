# Project orientation

This is an entrypoint, not a continuously refreshed build dashboard or a queue of instructions.
Check the current task, live Git diff, and relevant source before resuming work. A recorded pass
applies to its producing source/build inputs; do not infer that the entire checkout is currently green.

## Find current context

- [Root guide](../AGENTS.md): framework boundaries, component/payload authoring, plugin ownership.
- [Framework map](FRAMEWORK_MAP.md): source navigation; read the affected plugin guide as needed.
- [Risk register](OPEN_RISKS.md): unresolved decisions and acceptance gaps. Dated evidence is historical
  until verified for the present change; it is not an instruction to reopen completed work.
- [Roadmap](READINESS_ROADMAP.md): capability sequencing, not authorization to start unrelated work.
- [Consumer verification](CONSUMER_VERIFICATION.md) and [performance baseline](PERFORMANCE_BASELINE.md):
  qualification contracts and evidence boundaries.
- [Demo progress](DEMO_GUIDE_PROGRESS.md): the active tutorial's scoped handoff, only for that task.
- [Context index](README.md): remaining specialized references.

## Evidence boundaries to preserve

Compilation does not prove gameplay, determinism, or release readiness. Relevant serial/parallel,
restore/continuation, replay/peer, and consumer evidence is required as specified in the workflow.
Human editor/PIE feel, multi-world behavior, configured-game scale, and true Client/Dedicated Server
qualification remain separate from a local Game-target build or a small automated workload.
Do not close any outstanding gate without its own evidence. Read the owning record for its latest
recorded status; verify only the boundary relevant to the requested work.

## Historical evidence

The former long project-state record is preserved byte-for-byte in
[the 2026-09-07 capture](history/2026-09-07-project-state.md). Its dates, “latest” labels, commands,
and instructions describe their original sessions. They are not current policy or proof for today's
checkout. Search it only for a specific prior result, rationale, or regression investigation.

Maintain current facts in the owning references above. Do not append routine task transcripts or
repeat build/test histories here. No recurring maintenance or user action is required.
