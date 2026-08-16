# Multiplayer

SeinARTS multiplayer is deterministic lockstep carried over Unreal's ordinary reliable RPC
transport. Every peer runs the complete simulation; only commands cross the wire.

## The model in five sentences

Commands you issue are batched into **turns** (several simulation ticks per turn) and executed a
fixed input delay later, identically on every peer. The host aggregates every player's turn batch
— including mandatory empty "heartbeat" batches — and fans the assembled turn to everyone; no
peer's simulation advances past a turn until every player's input for it has arrived. Match start
is a distributed commitment: every simulating peer materializes tick zero locally and must produce
a byte-identical five-digest receipt before anything launches. During play, peers exchange 128-bit
canonical state roots on a fixed cadence; any divergence is announced on every screen with each
peer's root. Losing the *ability* to prove agreement (a peer that cannot compute its root on time)
is treated as seriously as proven disagreement and terminates the session rather than continuing
unproven.

## Setting up a match

1. **Lobby** — the shipped lobby (subsystem + replicated lobby state, with a Blueprint view-model
   and verb library) handles slot claims, ready state, teams, map selection, and start. The
   host's start publishes the frozen match settings and travels everyone to the gameplay map.
2. **Direct PIE** — for development, a map with Sein Player Starts synthesizes its own match
   manifest; set your PIE net mode to listen server with the desired player count and press Play.
3. **Compatibility** — before tick zero, peers must agree on: protocol version, world package,
   match settings, simulation-content manifest, command-protocol schema, the canonical
   state-contract, and (optionally enforced) the sim-settings config fingerprint. Any mismatch
   kicks with a targeted message instead of desyncing later.

## Teams and shared vision

Lobby team assignment seeds shared vision at match start: teammates consume each other's vision
(fog, seen areas, minimap) automatically. Under the hood this is a directional per-player-pair
grant — "A shares to B" and "B shares to A" are independent — so asymmetric arrangements are
first-class. To change relationships mid-match (alliances, treaties, scripted reveals), author an
ability or effect that calls **Grant Pair Capability** / **Revoke Pair Capability** from the Sim
Mutation Library; like every gameplay mutation, it executes deterministically on all peers
through the ordinary command flow. Grants are refcounted per source, so overlapping agreements
compose and revoking one never tears down another's.

## Drop, reconnect, and late join

- A disconnected player's units keep obeying their last orders; the host injects heartbeats so
  the match never stalls, and after a grace window the slot can hand over to an AI controller
  (configurable, including "keep units alive with no AI").
- A returning player (or a late joiner) receives a **checkpoint** of the live simulation plus
  the retained turn tail, catches up at accelerated speed, and must reproduce the host's exact
  state root at an activation boundary before regaining command authority. Reconnect transport
  uses compressed, paced, acknowledged chunks and has been qualified under adversarial
  packet loss, duplication, and reordering.

## What your game code must do

Nothing special, if you follow the [Determinism Rules](../Reference/Determinism.md): issue
commands through the provided controller/Blueprint surfaces, keep simulation logic in abilities
and simulation components, and keep presentation read-only. Host AI emits commands through the
same lockstep pipeline automatically.

## Diagnostics

Console commands under `Sein.Net.*` cover status, latency and straggler reports, simulated
desyncs and disconnects, forced resync, snapshot dump/load, and replay save/load. The on-screen
red banner during play always means a determinism-critical event; the log line above it names
the exact cause.
