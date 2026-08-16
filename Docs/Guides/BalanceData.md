# Balance Data

Balance Data is an editor-only bulk-editing tool. It gathers tunable fields off the entity or
ability Blueprints you choose into one flat DataTable, lets you edit values in a spreadsheet-like
grid, and writes edits back into those Blueprints. The generated table is never read by the
running simulation — your Blueprints stay the single source of truth; the table is a convenience
view over them.

## Create

Content Browser → right-click → SeinARTS → Balance Data. This creates a **Balance Data** asset.
Make one profile per tuning concern (e.g. "vehicle movement", "infantry combat") rather than one
giant profile — narrower scopes keep each generated table dense and readable.

## Choose what to tune

`Target Kind` switches the whole profile between two modes; the relevant fields below show or
hide to match:

- **Entities (units / buildings)** — `Included Roots` lists root `SeinActor` classes; every
  concrete subclass under each root is matched, loaded or not, native or Blueprint.
  `Excluded Classes` removes a class and its whole subtree from the match.
- **Abilities (cost / cooldown / range)** — `Ability Roots` / `Excluded Abilities` work the same
  way over `SeinAbility` subclasses. Both an ability's activation cost and its production/build
  cost live on the same `Resource Cost`, so this mode tunes both together.

`Include Abstract` (off by default) opts abstract base classes into the match; leave it off since
abstract bases carry no shippable tuning.

Click **Preview Matched Targets** at any time to list the exact classes the profile currently
resolves, without writing anything.

## Choose which fields (Entities only)

`Tracked Components` is an explicit picker of component struct types to expand into columns.
Leave it empty to track every eligible component found across the matched entities — this is the
default and normal starting point. The picker (Add Component Type) offers both native
`SeinComponent`-descended structs from loaded framework/extension modules and designer-authored
struct components actually found on the matched entities' authored component data; per-class
sub-data (e.g. a movement class's own tuning struct) is not directly pickable but is still pulled
in automatically as nested columns wherever it's authored (see below).

Only designer-facing fields become columns — runtime/simulation state fields on a component are
excluded automatically, as are non-deterministic field types.

Ability mode has no tracking picker: `Preview Matched Targets`'s classes automatically surface
their own deterministic fields (cooldown, range, area radius, and so on) plus one cost column per
resource in your project's resource catalog.

## Output

`Output Directory` is where the generated table (and its paired row-struct asset) are written.
Leave it empty to generate alongside the profile asset itself. Whatever you set, it must resolve
to a path under a mounted content root (your project or a plugin's `Content` folder) — an
unmounted or otherwise invalid path fails Gather with an explicit error rather than silently
writing somewhere unexpected. `Generated Table` is filled in automatically after the first
successful Gather; you don't set it by hand.

## The workflow buttons

- **Preview Matched Targets** — lists matched classes. Read-only, no side effects.
- **Gather → Table** — builds (or rebuilds) the table from the current source Blueprints. This is
  **destructive**: if a table already exists, gathering discards any edits made directly in the
  table and confirms with a Yes/No prompt first. Rows are keyed to source classes by name;
  Blueprints with the same name in different folders are legal, and only a genuine collision after
  disambiguation fails Gather with an explicit error naming both classes.
- **Push Table → Source** — writes edited table values back into the matched source Blueprints,
  after a confirmation prompt. Only cells that actually differ from the current authored value are
  written, so an unedited Gather → Push round-trip is a no-op and never perturbs a fixed-point
  value through an unnecessary float conversion. Pushing marks the touched Blueprints modified —
  you still save them yourself (Ctrl+S) to persist the change.
- **Check Sync** — compares the table to the live source without writing anything. Reports how
  many tracked cells differ out of how many were checked, so you can tell drift from an already-
  in-sync table before deciding whether to Gather (pull source in) or Push (write table out).

## Editing the table

- Fixed-point fields (`FFixedPoint`) display as ordinary float columns — the raw fixed-point
  struct is unreadable in a grid, so it's converted at Gather and converted back at Push.
- Identity columns (unit display name, identity tag) are read-only labels for orienting yourself
  in the table; they are never written back on Push.
- A tracked component's own per-class sub-data (for example, a movement mode's tuning struct)
  surfaces as its own set of nested columns wherever it's actually authored on a matched class. A
  unit whose sub-data is a different type, or has none, simply gets a blank cell there — it's
  skipped on both read and write rather than treated as an error.

## Stale tables and rebinding

The generated table's row set and column schema are validated against what the profile currently
resolves every time you Push or Check Sync — matched classes, tracked components, or column
identities can all drift out from under an existing table (a renamed class, an added/removed
tracked component, a new field). When that validation fails, both operations fail closed with a
message telling you to Gather first, rather than guessing at a stale mapping and writing to the
wrong place. Re-running Gather rebuilds the table and its paired row schema from scratch and
clears the staleness.

## Determinism notes

The values you edit here land in the same `SeinDeterministic`-marked component data (or ability
defaults) that any other Blueprint edit would — Balance Data is a bulk-editing convenience layered
on top of the ordinary authoring surface, not a separate data path. Because the table only ever
writes back into the source Blueprints (and you still save and package those Blueprints normally),
every peer in a lockstep match loads the identical authored values from the identical asset; the
DataTable itself never ships as, or substitutes for, runtime simulation state.
