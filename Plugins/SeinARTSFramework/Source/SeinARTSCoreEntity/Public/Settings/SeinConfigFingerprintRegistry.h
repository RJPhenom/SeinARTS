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
#include "UObject/WeakObjectPtr.h"

/** One extension's registered contribution to the config-parity fingerprint. */
struct SEINARTSCOREENTITY_API FSeinConfigFingerprintContributor
{
	/** Stable, frozen cross-client identifier (e.g. "SquadExtension"). Drives the
	 *  deterministic fold order and namespaces the field chunks. */
	FName StableId;

	/** The extension's settings CDO (GetDefault<UMySettings>()); its named fields
	 *  are reflected into a canonical value representation. */
	TWeakObjectPtr<const UObject> SettingsCDO;

	/** Stable class path retained independently of the weak CDO so a stale hot-
	 *  reload entry cannot let another schema claim the same frozen ID. */
	FString SettingsClassPath;

	/** Sim-affecting property names on that CDO, in the extension's own authored
	 *  order (compiled into the extension → identical across clients). */
	TArray<FName> FieldNames;
};

/** Process-global registry (plain static singleton — no GC, available independent
 *  of any CDO). Register/unregister on module startup/shutdown. */
class SEINARTSCOREENTITY_API FSeinConfigFingerprintRegistry
{
public:
	/** Register an extension's fingerprint contribution. Rejects invalid schemas
	 *  and conflicting reuse of a frozen StableId; an identical hot-reload
	 *  registration refreshes its CDO. Call from StartupModule. */
	static bool RegisterContributor(FName StableId, const UObject* SettingsCDO, TArray<FName> FieldNames);

	/** Remove a contribution. Call from the extension's ShutdownModule. */
	static void UnregisterContributor(FName StableId);

	/** Append `<StableId>|<field>=<canonical value>;` chunks for every contributor,
	 *  folded in StableId LexicalLess order (load-order independent). A stale CDO is
	 *  re-resolved from its frozen class path; an invalid registered schema is fatal
	 *  rather than silently omitted from lockstep parity. */
	static void AppendContributors(FString& OutFp);

private:
	static TArray<FSeinConfigFingerprintContributor>& Get();
	static FCriticalSection& Mutex();
};
