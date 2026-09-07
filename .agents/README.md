# Agent context index

Read the row relevant to the task, not this entire directory. Existing project/plugin guides own
technical invariants; live code and the current user request outrank historical descriptions.

| Need | Owning reference | When to read |
|---|---|---|
| Project boundaries and plugin selection | [Root guide](../AGENTS.md) | Normally already loaded |
| Work process, validation, internal maintenance | [Workflow](WORKFLOW.md) | Before implementation; reuse within a task |
| Code comments, Blueprint labels, presentation | [Style](STYLE_GUIDE.md) | Relevant writing or editor changes |
| Current orientation and evidence limits | [Project state](PROJECT_STATE.md) | Resuming work or checking overall posture |
| Source navigation and subsystem contracts | [Framework map](FRAMEWORK_MAP.md) and affected plugin's AGENTS.md | Affected subsystem only |
| Unresolved risks and owner decisions | [Risk register](OPEN_RISKS.md) | Relevant risk or genuine product fork |
| Product capability sequencing | [Roadmap](READINESS_ROADMAP.md) | Planning framework capability work |
| Performance evidence | [Baseline](PERFORMANCE_BASELINE.md) | Profiling or performance changes |
| Consumer/install/release contracts | [Consumer verification](CONSUMER_VERIFICATION.md) | Downstream or release work |
| Movement+ human acceptance | [Vehicle gym](VEHICLE_GYM.md) | Vehicle behavior or presentation |
| Public documentation backlog | [Backlog](PUBLIC_DOCS_BACKLOG.md) | Public-facing impact or website work |
| Demo tutorial task handoff | [Demo progress](DEMO_GUIDE_PROGRESS.md) | Resuming that tutorial task only |
| Accepted formation conflict policy | [Conflict decision](DECISION_FROZEN_CONFLICT_POLICY.md) | Changing formation authority/allocation |
| Accepted targeter semantics | [Targeter decision](DECISION_TARGETER_LINE_CORRIDOR.md) | Line/corridor targeting changes |
| Online-services product boundary | [Online decision](DECISION_ONLINE_SERVICES_SCOPE.md) | Backend/provider scope changes |
| Old evidence or decision provenance | [History index](history/README.md) | A specific historical question only |

`Scripts/Read-AgentContext.ps1 -File OPEN_RISKS.md` lists section locations without loading prose.
Add `-Section 'Explicit product decisions still required'` or `-Find 'reconnect'` for bounded reads.

Agents maintain these records as part of relevant work. Store a current fact once, preserve real
open decisions, and reference receipts for evidence. Do not ask RJ to maintain this index or history.
`Docs/` is the public website; edits follow authorized scope. Temporary exploration, build products,
and logs remain ignored under `Saved/`. Requested PDFs go to Downloads, not a repository output tree.
