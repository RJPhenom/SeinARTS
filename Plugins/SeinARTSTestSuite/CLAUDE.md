# SeinARTS Test Suite — Local Guide

Read the project-root `AGENTS.md` first. This plugin is test-only infrastructure; it must never
become a runtime dependency of a production SeinARTS module.

## Packaging boundary

- The plugin is `EnabledByDefault: false`.
- Every module denies the `Shipping` configuration.
- Production `Build.cs` files must not depend on this plugin or `CQTest`.
- Test maps, Blueprint fixtures, doubles, reports, and scenario data stay here.
- An ordinary build or package does not enable this plugin. The runner enables it explicitly.

## Module ownership

| Module | Type | Responsibility |
|---|---|---|
| `SeinARTSTestSupport` | DeveloperTool | Reusable worlds, scenarios, deterministic traces, doubles, and assertions |
| `SeinARTSFrameworkTests` | DeveloperTool | Core, entity, ability/effect, nav, movement, FoW, net, and framework tests |
| `SeinARTSEditorTests` | Editor | Authoring, Blueprint compilation, bake, map, visual, and network-PIE tests |

The sibling disabled `SeinARTSExtensionTestSuite` plugin owns `SeinARTSExtensionTests` and links
all opt-in extensions. This split is intentional: the `Framework` runner profile disables every
extension and proves the base framework/test consumer independently.

## Suite names

Use stable Automation prefixes so the runner can select cost tiers:

- `SeinARTS.Unit` — pure data/math and small isolated behavior.
- `SeinARTS.Sim` — transient-world fixed-tick scenarios.
- `SeinARTS.Integration` — cross-module or asset-backed behavior.
- `SeinARTS.Determinism` — snapshot/replay and serial/parallel digest comparisons.
- `SeinARTS.Network.PIE` — authority and lockstep transport in multi-client PIE.
- `SeinARTS.Editor` — authoring and bake behavior.
- `SeinARTS.Perf` — counters, budgets, and soak tests.

Prefer CQTest for concise fixtures. Test observable public behavior. Only place a private white-box
test beside production code when no public observation can prove the invariant; guard any such test
with `WITH_DEV_AUTOMATION_TESTS`.

## Determinism tests

- Step the exact production fixed-tick path; never approximate it with wall-clock sleeps.
- Use a fixed session seed and commands indexed by tick.
- Compare a canonical digest at every tick, not only the existing final 32-bit StateHash.
- Snapshot tests must restore into a fresh world and continue both worlds with identical commands.
- Exercise future state after restore: allocate IDs, reuse entity slots, expire effects, repath,
  update FoW, and reserve/release tactical destinations.
- Serial-versus-parallel proof uses fresh worlds and, for the gate, fresh processes. Vary insertion
  and preload order to expose hidden address/FName/container-order coupling.
- A deterministic claim is not accepted until the relevant A/B StateHash or digest trace agrees.

## Scenario and asset policy

Code/text fixtures are the default. Add tiny purpose-built assets only when the behavior inherently
depends on Blueprint compilation, actor construction, baked geometry, rendering, or replication.
Do not turn example/gameplay maps into correctness fixtures.

Planned base-map naming:

- `L_SeinAutomation_Grid`
- `L_SeinAutomation_Fog`
- `L_SeinAutomation_Network`

Extension maps such as `L_SeinAutomation_Cover` live in the sibling extension test plugin so the
Framework profile never mounts assets referencing stripped classes.

Tests should assert mechanics: exact destination, deterministic allocation, digest equality,
authority rejection, stable fingerprint, and explicit performance counters. RJ's PIE A/B remains
the oracle for steering feel, tactical quality, formation appearance, preview legibility, and
Blueprint ergonomics.

## Running

Use `RunTests.ps1` in this plugin. Its `Framework` profile strips the extensions; `All` also enables
the sibling extension-test plugin. It builds with the selected plugins explicitly enabled, launches
`UnrealEditor-Cmd` headlessly, exports an Automation report, and fails if tests fail or never finish.
Known pre-test errors may be accepted only through the exact checked-in signature baseline; an
unlisted error or count change still fails. Remove baseline entries as their tracked defects land.
Keep generated reports under the project `Saved/Automation` tree; never commit them.
