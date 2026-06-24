# SeinARTS — Style Guide

This is the **style layer**: how to write code, comments, and Blueprint-facing surface so the
codebase stays consistent and designer-friendly. It does **not** repeat architecture or invariants —
those live in the root `CLAUDE.md` and each plugin's `CLAUDE.md` (determinism, sim/render
separation, module topology, the "code over comments" rule). Read those first; this covers *how to
write*, not *what the systems are*.

---

## Blueprint-facing tooltips (doc comments)

Every `UFUNCTION` / `UPROPERTY` / `BlueprintNativeEvent` a designer can see gets a doc comment in
this format. The doc comment **is** the tooltip UE shows in-graph, so write it for the designer, not
the maintainer.

**Format:** a 1–3 sentence plain-English ELI5, then a blank line, then the complete description.

```cpp
/** Decides how fast the unit wants to go this frame. Return its target cruise speed.
 *
 *  Called by the default loop. Whatever you return is then capped so the unit can still brake to a
 *  clean stop at its goal (from its Deceleration) — so this sets cruise speed, not the brake curve.
 *  The default returns the unit's terrain-adjusted top speed. Override to slow for sharp turns. */
UFUNCTION(BlueprintNativeEvent, Category = "SeinARTS|Movement", meta = (DisplayName = "Compute Speed"))
FFixedPoint ComputeSpeed(USeinMoverHandle* Mover);
```

Rules:
- **Plain text only — no Markdown.** UE tooltips do not parse it: `**bold**` and `` `backticks` ``
  render literally as noise. No `**`, no backtick code spans, no bullet syntax.
- **Lead with what it does for the user**, not the implementation. "How fast the unit wants to go,"
  not "returns the terrain-scaled `TopSpeed` before the arrival cap."
- **Trivial getters get one clear sentence** — the ELI5 *is* the complete description, so no second
  paragraph. (`/** Where the unit is right now. */`)
- **Refer to other nodes by their DisplayName**, not the C++ symbol: "feed into Set Rotation," not
  "pass to `SetRotation_Implementation`."
- **State units and sign conventions**: radians vs degrees, world units, "positive = uphill,"
  "0 = ground-bound," what a sentinel/zero return means.
- **For override hooks**, cover: what to return, when it's called, what the default does, and when
  you'd override it.

---

## Naming

**C++ prefixes:** sim USTRUCTs `FSein…`, sim UObjects `USein…`, actors `ASein…`, fixed-point types
`FFixed…`. Component **payload** structs carry the `Component` suffix (`FSeinExtentsComponent`).
Blueprint function libraries carry the `BPFL` suffix.

**Blueprint Category:** `SeinARTS|<Subsystem>[|<Subgroup>]` — singular nouns (`Tags` is the only
plural exception). Examples: `SeinARTS|Movement`, `SeinARTS|Movement|Toolkit`, `SeinARTS|Navigation`.
**Drop a subsystem qualifier that's redundant with the asset's own type.** A movement-mode
Blueprint's own Class-Defaults properties use `SeinARTS`, not `SeinARTS|Movement` — every property on
it is already movement, so the extra level only hides them behind a collapse. (A *component* among
many on a unit still qualifies — there the subsystem name disambiguates.)

**Blueprint DisplayName:** drop the `Sein` prefix (`DisplayName = "Has Tag"`, not `"Sein Has Tag"`).
Add an explicit `DisplayName` whenever the C++ symbol starts with `Sein`, to suppress UE's
auto-derivation. BPFL `UCLASS` DisplayName = `"SeinARTS X Library"`; ActorComponents use plain
`"X Component"` + `ClassGroup = (SeinARTS)`. UPROPERTY field names never carry the `Sein` prefix.

**Write the Category + DisplayName before the body**, not as a cleanup pass later — retroactive
naming cleanup is a whole separate session of work.

---

## Blueprint exposure

- **Lean toward BP-authorable / overridable** where it has real utility — simplifies a real
  workflow, or is intuitive — not gratuitously. A `BlueprintNativeEvent` hook the C++ default routes
  to (keep the C++ virtual, route its base body to the `BP_` hook) lets designers go lazy (tune) or
  power-route (override) by choice, without breaking C++ overriders.
- **A node must do EXACTLY what its name says — never more.** Over-broad scope behind a narrow name
  is unacceptable. If you need a broad variant and a selective one, make them **separate, named**
  nodes; don't widen one behind its name.
- **Offer variants as a clear picker set**, not a pile of boolean flags.
- **Keep the API lean** — no near-duplicate nodes; expose the one general tool (e.g. a `Draw Debug`
  node an author calls themselves) over many narrow built-ins.
- Constant per-class traits belong as **properties** (a checkbox), not no-arg computation hooks;
  per-unit values that vary belong as **hooks** that read hydrated tuning.

---

## Components & data

- **Components are pure data.** No event graphs, no state-mutating methods. Logic lives in abilities,
  effects, AI controllers, command brokers, and sim systems.
- **`SeinDeterministic` meta** on every sim USTRUCT (`USTRUCT(meta = (SeinDeterministic))`) — the
  marker the editor uses to accept a struct as a valid `ComponentData` entry, and the marker the
  determinism validators trust to whitelist a type/library.
- **`FInstancedStruct` ships in `CoreUObject`** — do not add `StructUtils` as a module dependency
  (the standalone plugin is deprecated in UE 5.5+).

---

## Determinism (style implications)

Sim code uses fixed-point types only (`FFixedPoint` / `FFixedVector` / `FFixedTransform` /
`FFixedQuaternion`), `FSeinEntityHandle` (never raw `AActor*` / `UObject*`), and the deterministic
PRNG (`FFixedRandom`). No `float`, `FVector`, `FMath::`, or `rand()` in sim. Float↔fixed conversions
are **editor/debug-only** and must never feed back into hashed sim state — e.g. a `Draw Debug` node
converting `FFixedVector → FVector` to render is fine; using a float result to drive sim is not.

---

## Settings

Keep the settings tree lean (`USeinARTSCoreSettings`): no redundant deep nesting, no duplicated
qualifiers. Put a qualifier in the category path **or** the DisplayName, not both. Base-module
settings go under the shared `SeinARTS` page (Category = `"<Subsystem>"`); only extensions get their
own settings page.

---

## Comments & docs

- **Trust code over comments** — the architecture has stabilized but some docstrings lag the
  implementation. When you find a docstring that contradicts the code, fix the docstring as part of
  your change (don't leave a known-stale comment behind).
- **Don't reference retired docs** (`DESIGN.md`, `PLAN.md`, `API_Cleanup_Pass.md`, BAR-program
  planning docs) — they're gone; cite live code or the current plan docs instead.
- **No dead outputs / always-on log spam.** A BP-exposed field that's never populated, or a
  per-tick `UE_LOG`, erodes trust — either make it real or remove it; gate diagnostics behind a
  verbosity level or a show-flag.
