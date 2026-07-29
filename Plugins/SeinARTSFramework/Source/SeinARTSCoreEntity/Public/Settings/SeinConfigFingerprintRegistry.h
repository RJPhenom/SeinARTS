/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinConfigFingerprintRegistry.h
 * @brief   Extensible lockstep config-parity fingerprint seam.
 *
 *          The base owns the fingerprint mechanism (USeinARTSCoreSettings::
 *          ComputeConfigFingerprint) but must NOT know about extension plugins.
 *          An EXTENSION with sim-affecting settings REGISTERS them here on module
 *          startup; ComputeConfigFingerprint folds every registered contributor
 *          into the hash the net layer exchanges at match join, so a client whose
 *          config differs (or who is missing the extension entirely) is rejected
 *          up front instead of silently desyncing mid-match.
 *
 *          DETERMINISM: contributors are folded in a STABLE order (sorted by
 *          StableId via FName::LexicalLess — a content-based compare, NOT the
 *          registration-order-dependent FName comparison index), so two clients
 *          whose modules loaded in different orders compute the IDENTICAL
 *          fingerprint for identical config. Scalars use ExportText; reflected
 *          containers and structs are recursively length-framed, with every map/set
 *          sorted because UE's default ExportText follows hash order.
 *
 *          The StableId is a FROZEN cross-client wire identifier: renaming it
 *          changes its sort position and thus every client's fingerprint. Never
 *          change a shipped id. Contributors MUST pass a CDO (GetDefault<T>()) —
 *          a transient UObject could be GC'd mid-session and silently drop its
 *          chunk.
 */

#pragma once

#include "CoreMinimal.h"
/**
 * Move-only lease for one module generation's fingerprint contribution.
 *
 * Exact duplicate generations may overlap during Live Coding or module reload.
 * Releasing an older lease removes only that generation, never the replacement
 * generation which claimed the same frozen contributor ID.
 */
class SEINARTSCOREENTITY_API FSeinConfigFingerprintRegistrationHandle
{
public:
	FSeinConfigFingerprintRegistrationHandle() = default;
	~FSeinConfigFingerprintRegistrationHandle();

	FSeinConfigFingerprintRegistrationHandle(
		const FSeinConfigFingerprintRegistrationHandle&) = delete;
	FSeinConfigFingerprintRegistrationHandle& operator=(
		const FSeinConfigFingerprintRegistrationHandle&) = delete;

	FSeinConfigFingerprintRegistrationHandle(
		FSeinConfigFingerprintRegistrationHandle&& Other) noexcept;
	FSeinConfigFingerprintRegistrationHandle& operator=(
		FSeinConfigFingerprintRegistrationHandle&& Other) noexcept;

	bool IsValid() const
	{
		return Token != 0;
	}

	void Reset();

private:
	explicit FSeinConfigFingerprintRegistrationHandle(uint64 InToken)
		: Token(InToken)
	{
	}

	uint64 Token = 0;
	friend class FSeinConfigFingerprintRegistry;
};

/** Process-global registry (plain static singleton — no GC, available independent
 *  of any CDO). Register/unregister on module startup/shutdown. */
class SEINARTSCOREENTITY_API FSeinConfigFingerprintRegistry
{
public:
	static constexpr int32 MaxReloadClaimsPerContributor = 64;

	/**
	 * Register an extension's fingerprint contribution. Rejects invalid schemas
	 * and conflicting reuse of a frozen StableId. Exact duplicate generations
	 * receive independent leases so they may overlap safely during reload.
	 */
	static FSeinConfigFingerprintRegistrationHandle RegisterContributor(
		FName StableId,
		const UObject* SettingsCDO,
		TArray<FName> FieldNames);

	/** Append `<StableId>|<field>=<canonical value>;` chunks for every contributor,
	 *  folded in StableId LexicalLess order (load-order independent). A stale CDO is
	 *  re-resolved from its frozen class path; an invalid registered schema is fatal
	 *  rather than silently omitted from lockstep parity. */
	static void AppendContributors(FString& OutFp);

private:
	static void UnregisterContributor(uint64 Token);
	friend class FSeinConfigFingerprintRegistrationHandle;
};
