# SeinARTS Style Guide

This is the local operational mirror of the human [Style Guide](https://docs.google.com/document/d/1-IT4RRpU2jR3yT5RI_bOM4Iq3s54Y9Fgy9gtBAJjshs), source guide version 1.4. The human guide owns writing and presentation style. Architecture, ownership, and invariants remain in the repository and plugin guides.

## 1. Code

### 1.1 File comment headers

Each production C++ source file begins with a header containing the copyright notice, filename, author, creation date, latest update date, concise file purpose, and an AI-assistance disclaimer when applicable.

```cpp
/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file         FileName.h
 * @author       RJ Macklem
 * @created      10 Aug 2026
 * @latest       10 Aug 2026
 * @brief        One-sentence purpose of the file.
 *
 *               Add short architectural context only when it helps a reader
 *               understand ownership, boundaries, or non-obvious behavior.
 *
 * @disclaimer   This code was generated in whole or in part with the assistance
 *               of an AI language model.
 */
```

Keep `@brief` concise. Update it when the file's purpose changes. Add a missing header when substantially editing an existing production file; do not mass-retrofit untouched files.

### 1.2 Components and data

- Components are pure data.
- Every simulation USTRUCT uses `USTRUCT(meta = (SeinDeterministic))`.
- `FInstancedStruct` ships in `CoreUObject`; do not add `StructUtils` as a module dependency.

### 1.3 Deterministic simulation

Simulation code remains bit-deterministic across peers and platforms.

- Use fixed-point types for simulation math.
- Use `FSeinEntityHandle` instead of raw `AActor` or `UObject` pointers.
- Use `FFixedRandom` for simulation randomness.
- Do not use `float`, `FVector`, `FMath`, or `rand()` in simulation code.
- Float/fixed conversions are editor or debug boundaries only and never feed hashed simulation state.

### 1.4 Comments and documentation

Comments explain current behavior and useful intent. They do not compete with code as a second implementation.

- Correct stale or contradictory docstrings as part of the change.
- Do not name external games, engines, or franchises in tooltips, code comments, or product documentation. Describe behavior or technique instead.
- `AoE` may mean Area of Effect. Technical names such as A*, boids, Gauss-Seidel, Jacobi, Reeds-Shepp, Dubins, Bresenham, and Xorshift are allowed.
- Do not reference retired documents. Cite live code or current documentation.
- Do not leave dead Blueprint outputs or always-on log spam. Make outputs real or remove them, and gate diagnostics behind verbosity or show flags.

## 2. Editor

### 2.1 Designer-facing comments

Every editor-visible `UPROPERTY`, `UFUNCTION`, and `BlueprintNativeEvent` has a doc comment. It is a designer-facing tooltip first and a maintainer reference second.

Begin with one to three plain-English sentences explaining what the property or node does. Add a blank line and fuller technical detail only when needed.

- Use plain text; Unreal tooltips do not parse Markdown.
- Lead with the user-facing effect, not implementation details.
- Give trivial getters one clear sentence.
- Refer to other nodes by Blueprint `DisplayName`, not C++ symbol.
- State units, sign conventions, and zero or sentinel meanings.
- For override hooks, explain what to return, when it runs, the default, and when to override it.

```cpp
/** Decides how fast the unit wants to go this frame. Return its target cruise speed.
 *
 *  Called by the default loop. Whatever you return is then capped so the unit can
 *  still brake to a clean stop at its goal, so this sets cruise speed rather than
 *  the brake curve. Override it to slow for sharp turns. */
UFUNCTION(BlueprintNativeEvent, Category = "SeinARTS|Movement")
FFixedPoint ComputeSpeed(USeinMoverHandle* Mover);
```

### 2.2 Naming

Type names are predictable in C++ and clean in Blueprint.

- Prefix simulation USTRUCTs with `FSein`, simulation UObjects with `USein`, actors with `ASein`, and fixed-point types with `FFixed`.
- Component payload structs use the `Component` suffix. Blueprint function libraries use `BPFL`.
- Blueprint categories use `SeinARTS|<Subsystem>[|<Subgroup>]`; category nouns are singular except `Tags`.
- Drop a subsystem qualifier when the asset type already supplies that context.
- Drop the `Sein` prefix from Blueprint `DisplayName`. Add an explicit `DisplayName` when the C++ symbol begins with `Sein`.
- Blueprint function-library classes use `SeinARTS X Library`. Actor components use `X Component` and `ClassGroup = (SeinARTS)`.
- UPROPERTY field names never carry the `Sein` prefix.
- Set `Category` and `DisplayName` when creating the API.

### 2.3 Blueprint exposure

Expose APIs when they simplify a real workflow or provide an intuitive designer override. Exposure is deliberate, not automatic.

- Use a `BlueprintNativeEvent` with a functional C++ default when designers need an override without replacing the surrounding system.
- A node does exactly what its name says. Broad and selective behaviors use separate, clearly named nodes.
- Offer variants through a clear picker rather than boolean collections.
- Prefer one general tool over near-duplicate nodes.
- Use properties for constant per-class traits and hooks for per-unit runtime values.

### 2.4 Settings

Keep settings lean and easy to scan. Avoid redundant nesting and repeated qualifiers. Put a qualifier in the category path or `DisplayName`, not both. Base-module settings use the shared SeinARTS page; opt-in extensions use their own pages.

## 3. Font

### 3.1 Font

SeinARTS Framework documentation uses Google Urbanist with Light weighting.

### 3.2 Colours

Documentation uses white (`#FFFFFF`) on black (`#000000`). Highlight colours are Phenom Studios Light Blue (`#0095FF`) and Red (`#FF0000`).

## 4. Git

### 4.1 Commits

Commits are major code-wave completions, safe WIP checkpoints, or major feature completions, which are usually merge commits.

- Keep titles to approximately 50 characters.
- Omit descriptions unless needed.
- Use direct language.
- For large or cross-cutting changes, use short line-broken bullets.
- Explain what changed and why.
- Do not inventory every line edit.
