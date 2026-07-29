/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinSnapshotRestoreAuthority.h
 * @brief   Native trust boundary for authoritative snapshot adoption.
 */

#pragma once

#include "CoreMinimal.h"

class USeinWorldSubsystem;

/**
 * Opaque, process-local capability for one trusted snapshot-restore attempt.
 *
 * The adapter that claims this handle has already authenticated and authorized
 * the complete snapshot envelope for its workflow. That adapter may be a local
 * save service, an authenticated multiplayer resync service, a host-migration
 * coordinator, or a campaign-checkpoint service. CoreEntity remains neutral to
 * those topologies and validates the deterministic snapshot body itself.
 * Native modules are inside this trust boundary: the handle is procedural
 * authorization, not cryptographic proof and not an artifact-digest binding.
 *
 * The token is deliberately inaccessible to callers and is never serialized,
 * hashed, replicated, or exposed to Blueprint.
 */
class SEINARTSCOREENTITY_API FSeinSnapshotRestoreAuthorityHandle
{
public:
	FSeinSnapshotRestoreAuthorityHandle() = default;
	FSeinSnapshotRestoreAuthorityHandle(
		FSeinSnapshotRestoreAuthorityHandle&& Other) noexcept
		: StableAuthorityID(MoveTemp(Other.StableAuthorityID))
		, Token(Other.Token)
	{
		Other.StableAuthorityID = NAME_None;
		Other.Token.Invalidate();
	}
	FSeinSnapshotRestoreAuthorityHandle& operator=(
		FSeinSnapshotRestoreAuthorityHandle&& Other) noexcept
	{
		if (this != &Other)
		{
			StableAuthorityID = MoveTemp(Other.StableAuthorityID);
			Token = Other.Token;
			Other.StableAuthorityID = NAME_None;
			Other.Token.Invalidate();
		}
		return *this;
	}

	FSeinSnapshotRestoreAuthorityHandle(
		const FSeinSnapshotRestoreAuthorityHandle&) = delete;
	FSeinSnapshotRestoreAuthorityHandle& operator=(
		const FSeinSnapshotRestoreAuthorityHandle&) = delete;

	bool IsValid() const { return Token.IsValid(); }

private:
	FName StableAuthorityID;
	FGuid Token;

	friend class USeinWorldSubsystem;
};

/**
 * Policy for local-only state carried beside the authoritative simulation.
 * Callers must choose explicitly because save/load and peer resync have
 * different player-facing behavior.
 */
enum class ESeinSnapshotLocalStateRestorePolicy : uint8
{
	/** Local save/load: apply the captured local presentation state. */
	RestoreCaptured,

	/** Resync/migration/catch-up: retain this peer's current presentation. */
	PreserveCurrent,
};

/** Whether adoption resumes fixed ticks immediately or awaits outer catch-up. */
enum class ESeinSnapshotResumePolicy : uint8
{
	/** Standalone load/rewind: resume the restored simulation immediately. */
	ResumeImmediately,

	/**
	 * Resync/migration: keep the restored simulation stopped while the outer
	 * coordinator installs the authenticated command tail and verifies
	 * readiness. A later native StartSimulation call resumes it.
	 */
	RemainStopped,
};

/**
 * Explicit, topology-neutral adoption choices. No default constructor is
 * provided: every caller must decide both player-facing local behavior and
 * catch-up timing.
 */
struct SEINARTSCOREENTITY_API FSeinSnapshotRestoreOptions final
{
	FSeinSnapshotRestoreOptions(
		ESeinSnapshotLocalStateRestorePolicy InLocalStatePolicy,
		ESeinSnapshotResumePolicy InResumePolicy)
		: LocalStatePolicy(InLocalStatePolicy)
		, ResumePolicy(InResumePolicy)
	{
	}

	ESeinSnapshotLocalStateRestorePolicy LocalStatePolicy;
	ESeinSnapshotResumePolicy ResumePolicy;
};
