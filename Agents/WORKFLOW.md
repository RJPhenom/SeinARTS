# SeinARTS Agent Workflow

This is the local operational mirror of the human [Workflow Policy](https://docs.google.com/document/d/1pb3Z0DdQKAIJ610cMOy1yOP9_RQj1jtzMupfhkyrlfw), source policy version 1.0. The human policy owns contributor workflow. Update both in the same task when workflow changes.

## Worktrees

Git worktrees are banned across all branches.

- Work in the primary checkout at `D:/Projects/Unreal Engine/SeinARTS`.
- Do not create, enter, or delegate work through another worktree.
- If a session starts outside the primary checkout, stop and return to it before changing files.
- One author writes to the checkout at a time. Preserve work and complete the handoff review before taking over.

## Handoffs

WIP may be committed or uncommitted. It must be preserved and understandable.

Before continuing inherited work:

- Adversarially review it for bugs.
- Confirm the direction, plan, and implementation remain sound.
- Confirm it is safe to continue.

## Development

Before changing code or documentation, understand the requested result, inspect live code, and check current Git state. Notes provide context; live code and current evidence take priority when they disagree.

Proceed autonomously when the result and constraints are clear. Ask for input when work materially changes product direction, player experience, public APIs or authoring workflows, compatibility or migration policy, or agreed scope or order.

## Validation and documentation

Match validation to risk. A successful build proves only that the project compiles.

Record completed work, remaining work, risks or decisions, validation, and the next action at meaningful handoffs. Every completed code task declares its documentation impact: `none`, `internal`, `public`, or `both`.
