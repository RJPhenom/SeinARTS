# Replays

Every networked match can record a streaming replay journal. Because the simulation is
deterministic, a replay is just the recorded command stream plus periodic checkpoints — playback
re-runs the actual simulation, so it is exact by construction.

## Recording

The server records automatically while networking is active (and `Sein.Net.SaveReplay` exists
for manual control). Journals are written incrementally to `Saved/Replays/` as
`<Map>_<timestamp>.seinreplay`, with a `.partial` file promoted atomically on completion — an
interrupted session preserves a valid, replayable partial up to the last durable turn.

Recording is tamper-evident: every frame of the journal is hash-chained, and checkpoints bind
the same compatibility digests the live match enforced. Periodic checkpoints (interval
configurable in the SeinARTS settings page) bound how far playback must simulate to reach any
seek target.

## Playback

Load a journal from a pristine lobby world (standalone):

```
Sein.Net.LoadReplay <path>
Sein.Net.StopReplay
```

Playback validates the journal's digest chain and compatibility identities against your build,
adopts the nearest checkpoint at or before the seek target, and feeds the recorded turns through
the same gate the network uses. Seeking far past a checkpoint bursts the simulation at
accelerated speed.

Replays also record **observer streams** — camera movement and selection changes for every
player — so spectating tools can reconstruct each player's point of view.

## Compatibility

A replay plays only on a build whose protocol, snapshot, content, and settings identities match
the recording; mismatches are refused up front with the differing identity named. Ship your
release's identities in its notes so players know which build a replay belongs to.
