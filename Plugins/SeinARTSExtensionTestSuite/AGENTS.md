# SeinARTS Extension Test Suite — Local Guide

Read the root guide and `Plugins/SeinARTSTestSuite/AGENTS.md` first. This disabled non-shipping
companion owns tests that intentionally link the Squad, Cover, CoverSquad, and Movement+ modules.
Keeping it separate lets the base test suite compile and run with every extension stripped.

`SeinARTSExtensionTests` owns runtime/cross-extension CQTests.
`SeinARTSExtensionEditorTests` owns Cover authoring, extension Blueprint/asset, scripted-map, and
network-PIE coverage that needs extension editor APIs.

Do not add framework-only fixtures here. Do not make a production module depend on this plugin,
the base test suite, or `CQTest`. Add future extension-combination test plugins/modules only when a
specific packaging combination cannot be proven through this all-extensions profile.
