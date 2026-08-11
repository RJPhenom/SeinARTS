# Agent Workflow

This is the local operational mirror of the human [Workflow Policy](https://docs.google.com/document/d/1pb3Z0DdQKAIJ610cMOy1yOP9_RQj1jtzMupfhkyrlfw), source policy version 1.0. The human policy owns contributor workflow. This file contains the rules agents must apply while working in this repository. Update both in the same task when workflow changes.

## Start of work

Before changing code or documentation:

- Identify the task, branch, worktree, current status, expected validation, and overlap with active work.
- Inspect the live implementation and current Git state.
- Treat notes and handoffs as context. Confirm the live branch, diff, and validation evidence before continuing.
- When taking over work, adversarially review it for bugs. Confirm the direction, plan, and implementation remain sound and it is safe to continue.

## Branches and worktrees

Branches and worktrees belong to tasks, not contributors. Use a task branch when work needs isolation from `main` or another active task. Do not develop unrelated work directly on `main`.

Use task-based branch names. Do not name new branches after a model or contributor.

Use a worktree when concurrent tasks need isolation or WIP must remain in place while other work continues. A new worktree lacks ignored build artifacts and baked level data; rebuild and re-bake before applicable code or runtime validation.

Work that touches the same subsystem, public API, generated assets, build configuration, or deterministic state must be sequenced or explicitly coordinated. One worktree has one active branch and one coherent task.

Before removing a completed worktree, confirm that its work is committed, merged, deliberately abandoned, or otherwise preserved.

## Handoffs and decisions

Commit intentional work at stable restore points. Before a session ends or a usage limit interrupts work, commit intentional changes, including WIP when necessary.

Record what completed, what remains, known risks or unresolved decisions, validation performed, and the recommended next action. Use a pull request, issue, or a task record under `Agents/Tasks`.

Proceed autonomously within an approved direction. Ask for user input when a change materially affects product direction, player experience, public APIs or authoring workflows, compatibility or migration policy, or the agreed order or scope of work.

## Validation

Validation must match the risk. A successful build proves only that the project compiles.

Routine changes require focused inspection and the smallest relevant check. Normal code changes require a final diff review, relevant build, applicable tests, and a documentation-impact decision.

Changes affecting determinism, simulation timing, networking, replay, persistence, public APIs, module boundaries, or critical performance require stronger evidence. Use the applicable state-hash, peer, replay, resynchronization, persistence, profiling, adversarial-review, and PIE checks.

## Documentation

Documentation has three audiences:

- Public documentation: the customer and developer documentation website.
- Private human documentation: design, strategy, planning, and progress documents in Google Drive.
- Operational documentation: agent instructions, handoffs, and engineering records under `Agents/`, issues, or pull requests.

Do not put temporary notes or agent reports in the public documentation tree. Declare the documentation impact of every completed code task as `none`, `internal`, `public`, or `both`.

Project documents such as the design document, Workflow Policy, and Style Guide use a major.minor version on their cover. Increment the minor version whenever the document changes. Only RJ increments the major version.

## Releases

Use Semantic Versioning for the coordinated production plugin suite. Semantic versions do not guarantee multiplayer compatibility; releases must also identify network, simulation, replay, snapshot, persistent-data, simulation-content, engine, and plugin-build compatibility.

Released versions are immutable Git tags. A production release requires the agreed scope, required builds and tests, appropriate runtime qualification, performance comparison, clean consumer validation, current documentation, versioned artifacts, and an immutable tag.
