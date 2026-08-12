# SeinARTS Agent Workflow

This is the local operational mirror of the human [Workflow Policy](https://docs.google.com/document/d/1pb3Z0DdQKAIJ610cMOy1yOP9_RQj1jtzMupfhkyrlfw), source policy version 2.3. The human policy owns contributor workflow. Update both in the same task when workflow changes.

## 1. Agents

### 1.1 Markdown files

Durable agent-authored Markdown belongs in `.agents/`. Keep the repository root clear. Toolchain-loaded `AGENTS.md` and `CLAUDE.md` files at repository and plugin scopes are the exceptions.

### 1.2 PowerShell

Persistent agent PowerShell belongs in `Scripts/`. Scripts must resolve and operate on the repository root from their own location.

- Reuse persistent utilities instead of creating duplicates.
- Delete one-time scripts after use.

## 2. Development

### 2.1 Starting work

Before changing code or documentation, understand the requested result, inspect the live implementation, and check current Git state. Notes and comments provide context; live code and current evidence take priority when they disagree.

Complete the handoff review in 4.2 before continuing inherited work.

### 2.2 Decisions

Proceed autonomously when the result and constraints are clear. Ask for input when work changes product direction or player experience, public APIs or authoring workflows, compatibility or migration policy, or the agreed order or scope. Routine implementation choices should not create unnecessary pauses.

### 2.3 Validation

Validation must match risk. A successful build proves only that the project compiles. Routine changes require final diff review, relevant builds and tests, and a documentation-impact decision.

Changes affecting determinism, simulation timing, networking, replay, persistence, public APIs, module boundaries, or critical performance require stronger evidence as appropriate:

- Independent adversarial review.
- Development and Shipping builds.
- Serial-versus-parallel state-hash comparison.
- Peer, replay, resynchronization, or persistence testing.
- Profiling against the accepted baseline.
- PIE validation.

## 3. Documentation

### 3.1 Document types

Documentation has three audiences:

- Public documentation: customer and developer documentation published through the documentation website.
- Private human documentation: design, strategy, planning, and progress documents in Google Drive.
- Operational documentation: agent instructions, handoffs, and engineering records in `.agents/`, issues, or pull requests.

Agent reports and temporary working notes do not belong in the public documentation tree.

### 3.2 Keeping documentation current

Every completed code task declares its documentation impact: `none`, `internal`, `public`, or `both`.

Public APIs, Blueprint workflows, setup requirements, compatibility changes, and migration steps are documented with the associated release. Link documentation changes in another repository to the code change so they can be reviewed together.

Project documents use `major.minor` cover versions. Increment the minor version whenever a document changes. Only an authoritative decision-maker increments the major version.

## 4. Git

### 4.1 Commits

Commits mark clear history and may also serve as restore points during long WIP refactors or feature work. They are not limited to completed features.

Files must be committed or deliberately ignored. Do not leave unexplained pending changes at a commit boundary.

### 4.2 Handoffs

WIP may be committed or uncommitted. It must be preserved and understandable.

Before continuing inherited work:

- Adversarially review the current state for bugs and confirm the direction still makes sense.
- Confirm the plan and implementation are sound.
- Confirm it is safe to continue.

Correct unsafe work or request authoritative input when a real product or architectural decision is required.

### 4.3 Branches

Branches belong to tasks, not authors. Authors may continue an existing branch after completing the handoff review.

`main` is the shared integration branch. Merge work once it has been reviewed and validated appropriately for its risk.

Use branches for:

- New features.
- Major refactors involving one or more modules.
- Extended bug-fixing or performance work.
- Release preparation.
- Work that must remain isolated from `main` or another active task.

### 4.4 Worktrees

Git worktrees are banned across all environments.

- Work only in the primary checkout at `D:/Projects/Unreal Engine/SeinARTS`.
- Do not create, enter, or delegate through another worktree.
- If a session starts elsewhere, stop and return to the primary checkout before changing files.
- Only one author writes to the checkout at a time.

## 5. Versioning and releases

### 5.1 Versioning

SeinARTS uses Semantic Versioning:

- Major: breaking public API, authoring, saved-data, replay, snapshot, or compatibility changes.
- Minor: backward-compatible features and capability additions.
- Patch: backward-compatible fixes, tuning, performance improvements, and documentation corrections.

The production plugin suite normally ships as one coordinated version. Extensions identify the framework versions they support. Semantic versions do not replace exact multiplayer compatibility checks for network, simulation, replay, snapshot, persistent data, simulation content, engine, and plugin builds.

### 5.2 Releases

`main` is the shared integration branch. Immutable Git tags identify releases. Use a release branch only when stabilization must continue separately from new work.

A production release requires:

- An agreed version and scope.
- Clean Development and Shipping builds.
- Required automated and deterministic checks.
- Appropriate multiplayer, replay, resynchronization, and persistence checks.
- Performance comparison against the accepted baseline.
- Installation and packaging validation in a clean consumer project.
- Updated documentation, changelog, and migration notes.
- Versioned plugin artifacts and an immutable Git tag.

Critical fixes branch from the affected release and produce a patch version.
