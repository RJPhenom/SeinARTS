# SeinARTS Cover + Squad Bridge — Plugin Guide

This opt-in production plugin is the only package allowed to depend on both the Cover and Squad
extensions. Read the project-root `AGENTS.md` first. It exists so Cover and Squad remain independently
strippable while games that enable both can select a cover-aware squad dispatch resolver.

## Boundary

- Runtime module: `SeinARTSCoverSquad`.
- Required plugins: Framework, Cover, and Squad.
- Enabling this plugin enables those dependencies through its descriptor.
- Cover and Squad must never depend back on this plugin.
- No bridge source belongs in either parent plugin.

The module name intentionally did not change when it moved out of Cover. Reflected assets and config
therefore continue to use
`/Script/SeinARTSCoverSquad.SeinCoverAwareSquadDispatchResolver` without redirects.

## Runtime ownership

`USeinCoverAwareSquadDispatchResolver` derives from `USeinSquadDispatchResolver` and applies Cover's
slot query/post-processing policy to Squad's inner formation positions. Designers opt in per squad
or through the Squad default resolver setting.

The module owns the stable simulation-content contributor `seinarts.coversquad` and the reflected
command-broker-resolver pool codec provider
`seinarts.coversquad.pool.dispatch-resolver.reflection`. These identities, revisions, class anchor,
and module name are replay/snapshot/peer compatibility state. Do not change them merely because the
plugin directory changed.

Module unload terminates active worlds before withdrawing the codec/contributor registrations.
Preserve that fail-closed ordering.

## Verification

Changes require all of:

- Cover-only consumer build/cook/package with Squad and this bridge physically absent.
- Squad-only or Framework-only stripping where relevant.
- Full consumer proof with the resolver header/class linked through this plugin.
- Extension Unit/Integration/Determinism/Editor tests.
- Manifest generation with the contributor present only when the bridge is enabled.
- PIE confirmation that a squad explicitly configured with the bridge resolver previews and submits
  the same cover-adjusted first destinations.

Allocation quality and reservations remain Cover gameplay work. Do not solve them by coupling
Cover or Squad back to this bridge.
