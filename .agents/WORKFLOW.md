# SeinARTS Agent Workflow

This is the local operational mirror of the human [Workflow Policy](https://docs.google.com/document/d/1pb3Z0DdQKAIJ610cMOy1yOP9_RQj1jtzMupfhkyrlfw), source policy version 2.5. The human policy owns contributor workflow. Update both in the same task when workflow changes.

## 1. About

This policy defines the workflows and restrictions for developing, testing, and using the SeinARTS Framework.

## 2. Agents

### 2.1 Markdown files

Agent-authored Markdown belongs in `.agents/`. Keep the repository root clear. Toolchain-loaded `AGENTS.md` and `CLAUDE.md` files at repository and plugin scopes are the exceptions.

### 2.2 PowerShell

SeinARTS uses PowerShell (`.ps1`) for agent development because Windows is the primary development environment and target platform.

Persistent scripts belong in `Scripts/` under the repository root.

- Keep PowerShell scripts in `Scripts/`.
- Make scripts resolve and operate on the repository root from their own location.
- Delete one-time scripts after use.
- Reuse persistent utilities such as `Build.ps1` instead of creating duplicates.

## 3. Development

### 3.1 Starting work

Before changing code, understand the requested result, inspect the live implementation, and check the current Git state. Notes and comments provide context; live code and current evidence take priority when they disagree.

Before continuing inherited work, complete the handoff review in 5.2.

### 3.2 Decisions

Proceed autonomously when the result and constraints are clear. Ask for input when work changes:

- Product direction or player experience.
- Public APIs or authoring workflows.
- Compatibility or migration policy.
- The agreed order or scope of work.

Routine implementation choices should not create unnecessary pauses.

### 3.3 Validation

Validation must match the risk of the change. A successful build proves only that the project compiles.

Routine code changes require a final diff review, the relevant build and tests, and a documentation-impact decision.

Changes affecting determinism, simulation timing, networking, replay, persistence, public APIs, module boundaries, or critical performance require stronger evidence as appropriate:

- Independent adversarial review.
- Development and Shipping builds.
- Serial-versus-parallel state-hash comparison.
- Peer, replay, resynchronization, or persistence testing.
- Profiling against the accepted baseline.
- PIE validation.

## 4. Documentation

The public documentation website is live and under construction at `docs.seinarts.gg`. It is served from `Docs/` through GitHub Pages. Changes must keep public-facing documentation current.

### 4.1 Document types

Documentation has three audiences:

- Public documentation: customer and developer documentation published at `docs.seinarts.gg` from `Docs/`.
- Private-human documentation: design, strategy, planning, and progress documents in Google Drive.
- Private-agent documentation: agent instructions, handoffs, and engineering records under `.agents/`, issues, or pull requests.

Agent reports and temporary working notes do not belong in the public documentation tree.

### 4.2 Keeping documentation current

Every completed code task and commit declares its documentation impact: `none`, `private-human`, `private-agent`, `public`, or any applicable combination.

After every commit or code change, check the impact on public documentation. Alert the authoritative decision-maker when public documentation is affected, and make updates commensurate with the change so the website does not fall behind.

Project documents such as the design document, Workflow Policy, and Style Guide use a `major.minor` version on their cover. Increment the minor version whenever the document changes. Only the authoritative decision-maker increments the major version.

During release, review public APIs, Blueprint workflows, setup requirements, compatibility changes, and migration steps, and confirm their documentation is current.

## 5. Git

### 5.1 Commits

Commits are not limited to completed features or functions. They mark clear points in history and serve as restore points during lengthy WIP refactors and feature work.

Files must be committed or deliberately ignored. Do not leave unexplained pending changes at a commit boundary.

### 5.2 Handoffs

WIP does not need to be cleaned up before another author takes over. It may be committed or uncommitted; what matters is that the work is preserved and its current state can be understood.

Picking up another author's work does not mean assuming it is correct. Before continuing:

- Adversarially review the current state for bugs and confirm that the direction still makes sense.
- Confirm the plan and implementation are sound.
- Confirm it is safe to continue.

Correct unsafe work or request authoritative input when a real product or architectural decision is required.

### 5.3 Branches

Branches belong to tasks, not authors. Authors may continue an existing branch after completing the handoff review.

`main` is the shared integration branch. Merge work once it has been reviewed and validated appropriately for its risk.

Use branches for:

- New features.
- Major refactors involving one or more modules.
- Extended bug-fixing or performance work.
- Release preparation.
- Work that must remain isolated from `main` or another active task.

### 5.4 Worktrees

Git worktrees are banned across all environments for this project.

- Work only in the primary checkout at `D:/Projects/Unreal Engine/SeinARTS`.
- Do not create, enter, or delegate through another worktree.
- If a session starts elsewhere, stop and return to the primary checkout before changing files.
- Only one author writes to the checkout at a time.

Worktrees were tried more than once in the past and consistently produced bad merge experiences. The ban is a considered, re-instated decision as of v2.0, not an untested default.

### 5.5 Cloud agent sessions

The policy on keeping cloud agent work from being orphaned or merged into `main` without enough thought is still being refined. If you are an agent reading this mirror, act with care and keep the user informed of git status in your cloud environment.

Cloud agent sessions (Claude Code on the web, Codex cloud) always create their own auto-generated working branch. This is a platform behavior, not a choice; session branches follow the same rules as 5.3 and belong to the task.

The goal is for testers to have up-to-date local repositories with the latest cloud session work, safe to merge. Guiding principles:

- Make every effort to regularly merge into `main` at safe completion points — not necessarily at the end of the session.
- Merge conflicts can occur when multiple agents are working, committing, and merging. Do not attempt to resolve them automatically. Report back to the user and await instructions.
- Keep a clear, current understanding of which branches exist on the remote versus locally. If a branch does not yet exist on the remote (other than in the moment right after creation), keep the user aware. Branches are ideally published on creation; `https://github.com/RJPhenom/SeinARTS/branches` gives a complete view of active branches across every desktop and cloud environment together.

## 6. Versioning and releases

### 6.1 Versioning

SeinARTS uses Semantic Versioning:

- Major: breaking public API, authoring, saved-data, replay, snapshot, or compatibility changes.
- Minor: backward-compatible features and capability additions.
- Patch: backward-compatible fixes, tuning, performance improvements, and documentation corrections.

The production plugin suite normally ships as one coordinated version. Extensions identify the framework versions they support.

Semantic versions do not guarantee multiplayer compatibility. Released builds also identify their network, simulation, replay, snapshot, persistent-data, simulation-content, engine, and plugin-build compatibility.

### 6.2 Releases

`main` is the shared integration branch. Immutable Git tags identify released versions, never a moving branch. Use a release branch only when stabilization must continue separately from new work.

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
