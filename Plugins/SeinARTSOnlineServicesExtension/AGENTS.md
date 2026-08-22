# SeinARTS Online Services Extension - Plugin Guide

Read the project-root `AGENTS.md` first. This optional production plugin owns backend-neutral
online-product contracts and the local loopback reference provider. The Framework and Net modules
must never depend on it.

## Boundary

- Runtime module: `SeinARTSOnlineServices`.
- Required plugin: Framework. The module consumes stable Net identities but does not own lockstep.
- No Steamworks, EOS, or other vendor SDK dependency belongs in this plugin. Vendor adapters are
  separate optional plugins that subclass the provider contract.
- SOS data is game-instance/backend state, never canonical simulation state. It must not affect a
  tick, state root, replay execution, command admission, or config fingerprint.

## Contract

- `USeinOnlineServicesSubsystem` owns one configured provider per game instance and exposes the
  provider-neutral asynchronous request surface.
- Every operation has an exact reflected request/result schema. Mismatched or out-of-bounds request
  values fail before the provider runs; schema-correct but invalid provider result values also fail
  closed.
- Request handles are monotonic and process-local. Completion is deferred onto the game thread,
  generation-guarded across provider reset, cancellable, bounded, and mechanically limited to one
  accepted callback per provider request. The base provider owns dispatch and shutdown wrappers;
  adapters must synchronously join workers and destroy every retained completion in
  `ShutdownProvider` before module unload can proceed.
- Ranked result/stat mutation and replay-evidence publication require trusted-server authority. The
  request marker is routing context, not proof; real adapters must validate backend credentials
  independently.
- Operations classified by `SeinOnlineContract::IsDurableMutation` require idempotency keys. Same
  key + same payload returns the original result; same key + different payload is a conflict.
- Credential and reconnect-token values are secrets. Never log or place them in simulation state,
  replay data, telemetry attributes, or public error text.
- Unreal logs connection URL options. `SeinAdmission` carries only a provider-issued non-secret
  correlation ID; providers resolve account, match, participant, and slot from server-side state and
  authenticated transport identity. Allocation lease secrets and reconnect credentials never enter
  the URL.
- Idempotency keys provide retry safety, not semantic uniqueness. Providers must independently bind
  one matchmaking ticket to one allocation, one allocation to one match, and one terminal result to
  one match even when callers change retry keys.

## Loopback

`USeinOnlineLoopbackProvider` is an in-memory reference and automated-test provider. It implements
the full contract without pretending to provide security, durability, WAN behavior, platform
identity, or service availability. Its purpose is API qualification and local game integration.
Authentication sessions are provider-local. Supplying the same explicit loopback credential from
another provider instance recovers the same backend account but does not inherit authentication.
Loopback is permitted only for local non-dedicated Development use when authenticated connection
admission is not required. Shipping, dedicated-server, and authenticated-admission configurations
refuse it even when it is selected explicitly.

## Verification

Changes require focused lifecycle, schema, cancellation, bounds, idempotency/conflict, ranked
authority, save-revision, reconnect-expiry, and module-stripping tests; Development and Shipping
builds; a standalone SOS consumer profile; and independent review of any public contract change.
