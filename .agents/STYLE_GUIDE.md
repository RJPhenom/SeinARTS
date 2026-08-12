# SeinARTS Style Guide

This is the local operational mirror of the human [Style Guide](https://docs.google.com/document/d/1-IT4RRpU2jR3yT5RI_bOM4Iq3s54Y9Fgy9gtBAJjshs), source guide version 1.3. The human guide owns writing and presentation style. Architecture, module ownership, and system invariants remain in the repository and plugin guides.

## About

This guide defines how to write commits, code, comments, and Blueprint-facing editor surfaces.

## Code

### File comment headers

Each production C++ source file begins with a header containing:

- Copyright notice.
- Filename.
- Author.
- Creation date.
- Last updated date.
- A one-sentence description of the file.
- After an empty line, brief architectural context only when it helps a reader understand the file in a broader context.
- An AI-assistance disclaimer when applicable.

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

Keep `@brief` concise: one to three small or medium paragraphs. Do not turn the header into a running design document.

Update `@brief` whenever the file's purpose changes. When substantially editing a production file that lacks its header, add the header as part of that change. Do not mass-retrofit headers across otherwise untouched files.

### Components and data

- Components are pure data.
- Every simulation USTRUCT uses `USTRUCT(meta = (SeinDeterministic))`. This is the marker used by the editor and determinism validators.
- `FInstancedStruct` ships in `CoreUObject`. Do not add `StructUtils` as a module dependency.

### Deterministic simulation

Simulation code remains bit-deterministic across peers and platforms.

- Use fixed-point types for all simulation math.
- Use `FSeinEntityHandle` instead of raw `AActor` or `UObject` pointers.
- Use `FFixedRandom` for simulation randomness.
- Do not use `float`, `FVector`, `FMath`, or `rand()` in simulation code.
- Float-to-fixed and fixed-to-float conversions are editor or debug boundaries only. Their results never feed hashed simulation state.

### Comments and documentation

Comments explain current behavior and useful intent. They do not compete with code as a second, drifting implementation.

- Correct a docstring that contradicts the implementation as part of the change.
- Update or remove stale docstring information.
- Keep comments current with the codebase.
- Do not name external games, engines, or franchises in tooltips, code comments, or product documentation. Describe behavior or technique instead.
- `AoE` may mean Area of Effect.
- Technical names such as A*, boids, Gauss-Seidel, Jacobi, Reeds-Shepp, Dubins, Bresenham, and Xorshift are allowed and encouraged.
- Do not reference retired documents. Cite live code or current documentation.
- Do not leave dead Blueprint outputs or always-on log spam. Make outputs real or remove them, and gate diagnostics behind verbosity or show flags.

## Editor

### Designer-facing comments

Every editor-visible `UPROPERTY`, `UFUNCTION`, and `BlueprintNativeEvent` has a doc comment. It is a designer-facing tooltip first and a maintainer reference second.

Begin with one to three plain-English sentences explaining what the property or node does for the designer. Add a blank line and the complete technical description only when more detail is needed.

- Use plain text. Unreal tooltips do not parse Markdown.
- Describe behavior without naming external games, engines, or intellectual property.
- Lead with the user-facing effect, not the implementation.
- Give trivial getters one clear sentence.
- Refer to other nodes by their Blueprint `DisplayName`, not their C++ symbol.
- State units, sign conventions, and the meaning of zero or sentinel values.
- For override hooks, explain what to return, when the hook runs, what the default does, and when to override it.

```cpp
/** Decides how fast the unit wants to go this frame. Return its target cruise
 *  speed.
 *
 *  Called by the default loop. Whatever you return is then capped so the unit
 *  can still brake to a clean stop at its goal (from its Deceleration) - so this
 *  sets cruise speed, not the brake curve. The default returns the unit's
 *  terrain-adjusted top speed. Override to slow for sharp turns. */
UFUNCTION(BlueprintNativeEvent, Category = "SeinARTS|Movement")
FFixedPoint ComputeSpeed(USeinMoverHandle* Mover);
```

### Naming

Type names are predictable in C++ and clean in Blueprint.

Prefixes:

- `FSein` for simulation USTRUCTs.
- `USein` for simulation UObjects.
- `ASein` for actors.
- `FFixed` for fixed-point types.

Suffixes:

- Component payload structs use the `Component` suffix.
- Blueprint function libraries use the `BPFL` suffix.

Metadata:

- Use `SeinARTS|<Subsystem>[|<Subgroup>]` for Blueprint categories. Category nouns are singular; `Tags` is the only plural exception.
- Drop a subsystem qualifier when the asset type already supplies that context.
- Drop the `Sein` prefix from Blueprint `DisplayName`. Add an explicit `DisplayName` when a C++ symbol begins with `Sein` so Unreal does not auto-derive it.
- Blueprint function-library classes use `SeinARTS X Library`. Actor components use `X Component` and `ClassGroup = (SeinARTS)`.
- UPROPERTY field names never carry the `Sein` prefix.
- Set `Category` and `DisplayName` when creating the API. Do not leave naming consistency for a later cleanup pass.

### Blueprint exposure

Expose APIs to Blueprint when they simplify a real workflow or provide an intuitive designer override. Exposure is deliberate, not automatic.

- Use a `BlueprintNativeEvent` with a functional C++ default when designers need an override without replacing the surrounding system.
- A node does exactly what its name says. Broad and selective behaviors use separate, clearly named nodes.
- Offer variants through a clear picker rather than boolean collections.
- Keep the API lean. Prefer one general tool over several near-duplicate nodes.
- Use properties for constant per-class traits.
- Use hooks for per-unit values that vary at runtime.

### Settings

Keep the settings tree lean and easy to scan. Avoid redundant nesting and repeated qualifiers. Put a qualifier in the category path or `DisplayName`, not both.

Base-module settings belong on the shared SeinARTS settings page. Opt-in extensions use their own settings pages.

## Font

### Font

SeinARTS Framework documentation uses Google Urbanist with Light weighting.

### Colours

Documentation uses white (`#FFFFFF`) on black (`#000000`). Highlight colours are Phenom Studios Light Blue (`#0095FF`) and Red (`#FF0000`).

## Git

### Commits

Commits are:

- Major code-wave completions.
- WIP checkpoints for safe rollback.
- Major feature completions, usually merge commits.

Commit title and description rules:

- Keep titles to approximately 50 characters.
- Omit descriptions unless needed.
- Use direct language.
- For large, foundational, or cross-cutting changes, use short line-broken bullets.
- Explain what changed and why.
- Do not inventory every line edit.

Example:

```text
Add Reeds-Shepp Curves to Tracked Movement

- Adds RS algorithm for vehicle turning behaviours
- Includes 3pt turns, reverse escapes, etc
- Modifies the base A* path in a post-process step
```
