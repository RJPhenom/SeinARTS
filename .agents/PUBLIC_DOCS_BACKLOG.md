# Public Docs Backlog

Pending updates to the public documentation website (`docs.seinarts.gg`, served from
`Docs/`). Per Workflow Policy §4.2, every commit with `public` documentation impact adds an
entry here; RJ clears entries when the website is updated during a website task. Agents never
edit `Docs/` directly.

Entry format: date, commit(s), what changed designer-facing, which docs pages are affected.

## Pending

### 2026-09-02 — Formation preview opt-in moved render-side

Commits: `f10107c` (merged `f20a9e8`), headers `403b086`.

Designer-facing changes any preview/setup docs must reflect:

- Preview opt-in is now the **Formation Preview Component** added to a unit Blueprint
  (`USeinFormationPreviewComponent`, SeinARTSFramework). No component = no markers for that
  unit. On a squad's actor Blueprint it opts in every member with one renderer.
- The component's **Preview Actor Class** picks the renderer per unit; None falls back to the
  project default, then the framework mesh-quad renderer.
- **Removed settings**: `Enable Formation Preview` (master switch) is gone. **Formation
  Preview Actor Class** remains but is now only the project-default renderer, not an
  enable/disable.
- **Removed fields**: `Show Navigation Preview` (navigation component) and
  `Show Formation Preview` (squad component) no longer exist. Migration: add the Formation
  Preview Component to units/squads that should show markers.
- New dev console toggle: `Sein.Preview.Disable 1` suppresses marker drawing (render-only).
- Compatibility: component schema change; old replays/snapshots rejected via epoch
  `SeinARTS.Replay.8`.

Likely affected pages: formation preview / destination preview setup, plugin settings
reference, squad authoring, navigation component reference, any migration/changelog page.
