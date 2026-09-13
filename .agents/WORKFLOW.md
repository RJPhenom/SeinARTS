# SeinARTS Agent Workflow

Local operational policy, revision 3.3. The linked human [Workflow Policy](https://docs.google.com/document/d/1pb3Z0DdQKAIJ610cMOy1yOP9_RQj1jtzMupfhkyrlfw) was last mirrored at version 3.0. Revision 3.3 records RJ's 2026-09-13 release-version and public-repository boundary decisions; human-source synchronization is pending. Do not imply the human document was updated. Synchronize it when that document is in the authorized scope.

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

### 2.3 Internal context maintenance

Agents own routine repository-context maintenance; do it within the authorized work without asking
RJ to review summaries, choose document locations, or manage history. Escalate real product decisions,
not bookkeeping. These rules apply to repository records, not external memory stores or human documents.
Leave Claude-specific files and settings to Claude.

Use `README.md` to select the owning reference. Read only the relevant section; the read-only
`Scripts/Read-AgentContext.ps1` helper lists headings and returns bounded excerpts. It is optional,
not an additional per-task gate. Do not reread unchanged guidance already available in the task.

Update a fact in its owning record only when it changes. Keep task-specific restrictions in the
scoped task handoff; cross-project preferences and architecture contracts belong in their existing
owners. Link instead of copying explanations or execution logs across records. Keep active summaries
short; preserve replaced history under `history/` and mark it as historical, without carrying old
instructions forward as current policy. Do not archive active work or remove unresolved decisions
merely to meet a size target. Do not schedule maintenance, create another agent workflow, or re-audit
the repository just to keep these records tidy.

## 3. Development

### 3.1 Starting work

Before changing code, understand the requested result, inspect the live implementation, and check the current Git state. Notes and comments provide context; live code and current evidence take priority when they disagree.

When continuing existing work, confirm the requested scope, current diff, last relevant evidence,
and remaining failures or decisions. Preserve unrelated work. Recheck changed inputs after a handoff;
do not restart an entire audit merely because the conversation resumed.

### 3.2 Decisions

Proceed autonomously when the result and constraints are clear. Ask for input when work changes:

- Product direction or player experience.
- Public APIs or authoring workflows.
- Compatibility or migration policy.
- The agreed order or scope of work.

Routine implementation choices should not create unnecessary pauses.

### 3.3 Validation

Select checks by changed behavior, not file or module count. A successful build proves compilation,
not runtime correctness. Review the final diff and declare documentation impact once per completed change.

| Change | Development validation |
|---|---|
| Prose only | `Validate.ps1 -Preset Documentation` plus relevant links/content review. Website source also needs its site build. |
| Local behavior, editor, or mechanical code change | `-Preset Focused -Suite <relevant prefixes> -Profile Framework` (or `All` for extension coverage). Add editor/visual checks when applicable. |
| Simulation behavior, state, or timing | `-Preset Simulation -Profile <profile>`; add affected extension, snapshot/next-tick, replay/peer/resync tests and PIE evidence as required by the change. |
| Broad integration checkpoint | `-Preset Full`: both profiles, six suites, fresh-process collision A/B, and Shipping build. |
| Release | `Scripts/Release/Invoke-ReleaseGate.ps1`; retain all qualification and human acceptance gates in §6.2. |

Presets are explicit scopes, not automatic proof that all relevant behavior is covered. Unknown impact
requires inspection or the broader applicable tier. Script/tooling changes use meaningful pass/fail
fixtures and the script self-test (`Scripts/Validation/Invoke-ValidationSelfTest.ps1`); Unreal tests are
needed when their engine integration or runtime behavior changes. Do not create implementation-mirroring
tests for prose or trivial mechanical edits.

Independent adversarial review is required for changes to deterministic behavior, simulation timing,
network/replay/persistence contracts, compatibility, module dependency boundaries, critical performance,
or validation/release admission. Default to one focused independent reviewer. Supply the relevant diff,
contracts, and evidence; ask for defects and missing coverage. Additional reviewers or repeat reviews
need a distinct unresolved risk, substantive new change, or finding. Changing several modules alone
does not trigger extra review. Verify load-bearing findings against source without repeating the whole audit.

Scripts own discovery/count checks, build provenance, trace comparisons, and release receipt validation.
Read compact receipts first; inspect full logs or implementations for failures, unexpected results,
changed tooling, or missing evidence. Keep logs on disk. `Build.ps1 -Quiet` records build actions;
`Validate.ps1` stores step results under `Saved/Validation`. Use guarded `-SkipBuild` only when appropriate;
never bypass a stale-build rejection. After checks pass, rerun only for changed inputs, unresolved
failures, newly identified coverage gaps, or a distinct qualification boundary. No repeated polling of
unchanged output or repeated re-analysis of passing evidence.

Fresh-process `RunDeterminismAB.ps1` proves its 120-tick collision workload, not every simulation system.
Changes to state require fresh-world restore/continuation; tick/network changes require peer/replay
evidence. Preserve performance baselines and human PIE/editor/feel gates separately. Never promote
development validation or a partial gate to full release qualification.

## 4. Documentation

The public documentation website is served from `Docs/` at `docs.seinarts.gg` through GitHub Pages.

### 4.1 Document types

Documentation has three audiences:

- Public documentation: customer and developer documentation published at `docs.seinarts.gg` from `Docs/`.
- Private-human documentation: design, strategy, planning, and progress documents in Google Drive.
- Private-agent documentation: agent instructions, handoffs, and engineering records under `.agents/`, issues, or pull requests.

Agent reports and temporary working notes do not belong in the public documentation tree.

### 4.2 Keeping documentation current

At each completed change, declare documentation impact: `none`, `private-human`, `private-agent`,
`public`, or a combination. Carry this decision into its commit/handoff; reassess only when scope changes.
Update documents within the authorized scope. For affected public pages outside that scope, record a
concise entry in `PUBLIC_DOCS_BACKLOG.md` and report it. Preserve existing `Docs/` content.
Human-authored Google documents require scope authorization; a local mirror edit is not a remote edit.
Keep one current evidence summary with receipt paths instead of repeating test histories across records.

Project documents such as the design document, Workflow Policy, and Style Guide use a `major.minor` version on their cover. Increment the minor version whenever the document changes. Only the authoritative decision-maker increments the major version.

During release, review public APIs, Blueprint workflows, setup requirements, compatibility changes, and migration steps, and confirm their documentation is current.

## 5. Git

### 5.1 Commits

Commits are not limited to completed features or functions. They mark clear points in history and serve as restore points during lengthy WIP refactors and feature work.

Files must be committed or deliberately ignored. Do not leave unexplained pending changes at a commit boundary.

### 5.2 Branches

Local agents may not create or switch branches without an explicit request from the user. Cloud sessions create branches by platform necessity.

`main` is the shared integration branch. Merge work once it has been reviewed and validated appropriately for its risk.

### 5.3 Worktrees

Git worktrees are banned across all environments for this project.

- Work only in the primary checkout at `D:/Projects/Unreal Engine/SeinARTS`.
- Do not create, enter, or delegate through another worktree.
- If a session starts elsewhere, stop and return to the primary checkout before changing files.
- Only one author writes to the checkout at a time.

### 5.4 Cloud agent sessions

Cloud agent sessions (Claude Code on the web, Codex cloud) always create their own auto-generated working branch. This is a platform behavior, not a choice; session branches follow the same rules as 5.2 and belong to the task.

The goal is for testers to have up-to-date local repositories with the latest cloud session work, safe to merge. Guiding principles:

- Sync-on-start: fetch and prune, fast-forward `main`, verify your branch still exists on the remote, and do not trust inherited claims after a resume or context compaction.
- Cross-agent rule: no agent builds on another agent's live session branch.
- Make every effort to regularly merge into `main` at safe completion points — not necessarily at the end of the session.
- Keep a clear, current understanding of which branches exist on the remote versus locally. If a branch does not yet exist on the remote (other than in the moment right after creation), keep the user aware. Branches are ideally published on creation; `https://github.com/RJPhenom/SeinARTS/branches` gives a complete view of active branches across every desktop and cloud environment together.
- If you see something, say something: if you suspect a branch is stale or abandoned, alert the user.

## 6. Versioning and releases

### 6.1 Versioning

SeinARTS uses the project version scheme `MAJOR.MINOR.UPDATE[.HOTFIX]` (RJ, 2026-09-13):

- Only RJ changes the first and second digits. Agents never advance either automatically.
- The active release line is `0.2`. Normal releases increment the third digit: `0.2.1`, `0.2.2`, and so on.
- A crash or bug-only hotfix may increment the fourth digit: `0.2.1.1`, `0.2.1.2`, and so on.
- Published tags reserve their number. Commit counts and hashes never determine public versions.
- The fourth numeric part is a project extension to three-part SemVer; packaging and installation validators must accept it.
- GitHub releases remain prereleases until RJ explicitly approves going live. A plain numeric version does not imply production approval.

`0.2.0` introduced entity data authoring through ActorComponents instead of directly authoring
payload structures in the bridge array. The deterministic payload array remains the runtime backend.

The production plugin suite normally ships as one coordinated version. Extensions identify the framework versions they support.

Version numbers do not guarantee multiplayer compatibility. Released builds also identify their network, simulation, replay, snapshot, persistent-data, simulation-content, engine, and plugin-build compatibility.

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
