# SeinARTS Framework Documentation

SeinARTS is a deterministic lockstep real-time-strategy framework for Unreal Engine 5.8,
delivered as one core plugin and three optional extensions:

| Plugin | Provides |
|---|---|
| SeinARTS Framework | The deterministic simulation, entities, abilities, navigation, movement, fog of war, lockstep networking, replays, and the gameplay shell |
| SeinARTS Squads | Persistent squads with heterogeneous slots, formation dispatch, and reinforcement |
| SeinARTS Cover | Cover providers, cover-aware destinations, and formation-preview cover quality |
| SeinARTS Movement+ | Infantry, Wheeled, Tracked, Hover, and Flight movement modes |

The Cover + Squads bridge (SeinARTS Cover Squad) is a fourth optional plugin that activates only
when both parents are installed.

## Where to start

1. [Installation](GettingStarted/Installation.md) — install the plugins and validate the setup.
2. [Your First Skirmish](GettingStarted/FirstSkirmish.md) — an empty project to a playable
   two-player match.
3. [Authoring Units](Guides/Units.md) — the unit, ability, and formation authoring workflow.
4. [Determinism Rules](Reference/Determinism.md) — the rules your Blueprints and C++ must follow
   in a lockstep simulation, and the tooling that enforces them.

## The one rule that explains everything else

Every player's machine runs the complete simulation, and every machine must produce **bit-identical
results every tick**. Only player commands cross the network. Almost every convention in this
framework — fixed-point math, the component picker that refuses floats, the validators that block
certain Blueprint nodes, the settings that freeze at match start — exists to protect that property.
When a rule seems strict, this is why.

## Versioning

Documentation in this tree ships with, and describes, the plugin release it accompanies.
Compatibility identities (network protocol, snapshot, replay, and simulation-content versions) are
listed in each release's notes; peers on mismatched identities refuse to play together rather than
desync.
