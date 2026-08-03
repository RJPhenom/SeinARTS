/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinNavigationSubsystem.cpp
 */

#include "SeinNavigationSubsystem.h"
#include "SeinNavigation.h"
#include "SeinNavigationAStar.h"
#include "Settings/PluginSettings.h"
#include "Core/SeinParallel.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Simulation/Systems/SeinNavBlockerStampSystem.h"
#include "SeinLevelData.h"
#include "SeinLevelDataSubsystem.h"
#include "SeinLevelLayerProvider.h"
#include "Serialization/SeinCanonicalInitialStateDigest.h"

#include "Engine/World.h"
#include "Types/FixedPoint.h"
#include "Types/Vector.h"

#include "SeinARTSNavigationLog.h"

namespace
{
	constexpr int32 MaxNavigationStateContributors = 64;

	void AppendFramed(FString& Out, const FString& Value)
	{
		const FTCHARToUTF8 Utf8(*Value);
		Out += FString::Printf(TEXT("%d:"), Utf8.Length());
		Out += Value;
		Out += TEXT("\n");
	}

	bool BuildDisabledStaticEnvironmentDigest(
		FGuid& OutDigest,
		FString& OutError)
	{
		FSeinCanonicalDigestWriter Writer(
			TEXT("SeinARTS.Navigation.DisabledStaticEnvironment"), 1);
		return Writer.WriteString(TEXT("disabled"))
			&& Writer.Finalize(OutDigest, OutError);
	}
}

void USeinNavigationSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency(USeinWorldSubsystem::StaticClass());
	bNavigationConfigured = false;
	bInitialStaticEnvironmentPrepared = false;
	bStateBindingFrozen = false;
	InitialStaticEnvironmentAdoptionResult =
		FSeinStaticEnvironmentAdoptionResult::NotApplicable(
			TEXT("Initial navigation substrate adoption has not run."));
	ConfiguredNavigationClassPath.Reset();
	StateBindingFailureReason.Reset();
	FrozenStateBindingFrame.Reset();
	FrozenStaticEnvironmentDigest.Invalidate();
	FrozenStaticEnvironmentGeneration = 0;

	// Resolve the configured nav class (WYSIWYG). None/empty => navigation is intentionally OFF
	// (Navigation stays null — every Move order fails and the nav wall-barrier is disabled). A
	// set-but-unloadable/abstract class is a mistake, not an off-switch: it falls back to the shipped
	// A* default with a logged error. A valid class is used as-is. Every consumer is null-guarded.
	const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>();
	UClass* NavClass = nullptr;
	if (Settings && !Settings->NavigationClass.IsNull())
	{
		NavClass = Settings->NavigationClass.TryLoadClass<USeinNavigation>();
		if (!NavClass || NavClass->HasAnyClassFlags(CLASS_Abstract))
		{
			UE_LOG(LogSeinNavSubsystem, Error,
				TEXT("NavigationClass '%s' could not be loaded or is abstract — falling back to the shipped A* default."),
				*Settings->NavigationClass.ToString());
			NavClass = USeinNavigationAStar::StaticClass();
		}
	}

	if (NavClass)
	{
		bNavigationConfigured = true;
		ConfiguredNavigationClassPath = NavClass->GetPathName();
		Navigation = NewObject<USeinNavigation>(this, NavClass, TEXT("SeinNavigation"));
		if (Navigation)
		{
			Navigation->InitializeForWorld(GetWorld());
		}
		else
		{
			UE_LOG(LogSeinNavSubsystem, Error, TEXT("Failed to instantiate nav class %s"),
				*NavClass->GetName());
		}
	}
	else
	{
		// NavigationClass is None → navigation off (WYSIWYG). Leave Navigation null; the provider
		// registration below and all downstream consumers already guard on it.
		USeinARTSCoreSettings::ReportDisabledSystem(TEXT("Navigation"),
			TEXT("Move orders will fail and the nav wall-barrier is disabled (collision may push units through walls)."), /*bHighSeverity*/ true);
	}

	// CP1.1 unified level-data pipeline. Force the substrate subsystem up first
	// (InitializeDependency → it exists in editor + PIE for the bake button path),
	// then — if this nav participates (returns a provider face) — register it as the
	// "Nav" layer provider and subscribe to rebake/reload so the runtime grid tracks
	// the shared bake. Non-participating navs (the default base) skip all of this.
	Collection.InitializeDependency(USeinLevelDataSubsystem::StaticClass());
	if (UWorld* World = GetWorld())
	{
		if (USeinLevelDataSubsystem* LevelSubsystem =
				World->GetSubsystem<USeinLevelDataSubsystem>())
		{
			LevelDataSubsystem = LevelSubsystem;
			InitialLevelDataPreparedHandle =
				LevelSubsystem->OnInitialLevelDataPrepared.AddUObject(
					this,
					&USeinNavigationSubsystem::
						HandleInitialLevelDataPrepared);
			if (USeinLevelData* Substrate = LevelSubsystem->GetLevelData())
			{
				LevelData = Substrate;
				LevelDataMutatedHandle = Substrate->OnLevelDataMutated.AddUObject(
					this, &USeinNavigationSubsystem::OnLevelDataChanged);
				if (Navigation)
				{
					if (ISeinLevelLayerProvider* Provider =
							Navigation->GetLevelDataProvider())
					{
						Substrate->RegisterLayerProvider(Provider);
					}
				}
			}
			if (LevelSubsystem->IsInitialRuntimeDataPrepared())
			{
				HandleInitialLevelDataPrepared();
			}
		}
	}

	if (Navigation)
	{
		if (UWorld* World = GetWorld())
		{
			if (USeinWorldSubsystem* Sim =
					World->GetSubsystem<USeinWorldSubsystem>())
			{
				NavBlockerStampSystem =
					new FSeinNavBlockerStampSystem(this, Navigation);
				Sim->RegisterSystem(NavBlockerStampSystem);
			}
		}
	}
}

void USeinNavigationSubsystem::Deinitialize()
{
	ReleaseModuleOwnedStateForModuleUnload();
	Super::Deinitialize();
}

void USeinNavigationSubsystem::ReleaseModuleOwnedStateForModuleUnload()
{
	check(IsInGameThread());
	if (UWorld* World = GetWorld())
	{
		if (USeinWorldSubsystem* Sim =
				World->GetSubsystem<USeinWorldSubsystem>())
		{
			Sim->TerminateAndReleaseForModuleUnload(
				FName(TEXT("SeinARTSNavigation")),
				TEXT("navigation systems and canonical continuation state are unloading"));
		}
	}
	AsyncQueue.Reset();
	AsyncResults.Reset();

	// Weak UObject bindings protect against object destruction, not DLL
	// unload. Their lambda bodies live in this module, so sever them while
	// that code is still executable.
	UnbindSimDelegates();

	// Unhook from the shared substrate (CP1.1) before Navigation is torn down — we
	// reference Navigation->GetLevelDataProvider() to unregister.
	if (USeinLevelData* Substrate = LevelData.Get())
	{
		if (LevelDataMutatedHandle.IsValid())
		{
			Substrate->OnLevelDataMutated.Remove(LevelDataMutatedHandle);
			LevelDataMutatedHandle.Reset();
		}
		if (Navigation)
		{
			if (ISeinLevelLayerProvider* Provider = Navigation->GetLevelDataProvider())
			{
				Substrate->UnregisterLayerProvider(Provider);
			}
		}
	}
	if (USeinLevelDataSubsystem* LevelSubsystem =
			LevelDataSubsystem.Get())
	{
		if (InitialLevelDataPreparedHandle.IsValid())
		{
			LevelSubsystem->OnInitialLevelDataPrepared.Remove(
				InitialLevelDataPreparedHandle);
		}
	}
	LevelDataMutatedHandle.Reset();
	InitialLevelDataPreparedHandle.Reset();
	LevelData = nullptr;
	LevelDataSubsystem = nullptr;

	// Tear down the stamping system before nulling Navigation — the system
	// holds a weak nav ref but would still leak its own memory if dropped
	// without unregister-from-world.
	if (NavBlockerStampSystem)
	{
		if (UWorld* World = GetWorld())
		{
			if (USeinWorldSubsystem* Sim = World->GetSubsystem<USeinWorldSubsystem>())
			{
				Sim->UnregisterSystem(NavBlockerStampSystem);
			}
		}
		delete NavBlockerStampSystem;
		NavBlockerStampSystem = nullptr;
	}

	if (Navigation)
	{
		Navigation->DeinitializeFromWorld();
	}
	Navigation = nullptr;
	bNavigationConfigured = false;
	bInitialStaticEnvironmentPrepared = false;
	bStateBindingFrozen = false;
	InitialStaticEnvironmentAdoptionResult =
		FSeinStaticEnvironmentAdoptionResult::NotApplicable(
			TEXT("The navigation subsystem is not initialized."));
	ConfiguredNavigationClassPath.Reset();
	StateBindingFailureReason.Reset();
	FrozenStateBindingFrame.Reset();
	FrozenStaticEnvironmentDigest.Invalidate();
	FrozenStaticEnvironmentGeneration = 0;
}

void USeinNavigationSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	if (USeinLevelDataSubsystem* LevelSubsystem =
			LevelDataSubsystem.Get())
	{
		LevelSubsystem->EnsureInitialRuntimeDataPrepared(InWorld);
	}
	BindSimDelegates(InWorld);
}

void USeinNavigationSubsystem::HandleInitialLevelDataPrepared()
{
	if (bInitialStaticEnvironmentPrepared)
	{
		return;
	}
	if (UWorld* World = GetWorld())
	{
		InitialStaticEnvironmentAdoptionResult =
			LoadBakedAssetIntoNav(*World);
		bInitialStaticEnvironmentPrepared =
			!InitialStaticEnvironmentAdoptionResult.IsRejected();
	}
	else
	{
		InitialStaticEnvironmentAdoptionResult =
			FSeinStaticEnvironmentAdoptionResult::Rejected(
				TEXT("Navigation initial substrate adoption could not resolve its owning world."));
	}
}

bool USeinNavigationSubsystem::PrepareInitialCanonicalStateEnvironment(
	FString& OutError)
{
	OutError.Reset();
	if (bInitialStaticEnvironmentPrepared)
	{
		return true;
	}

	UWorld* World = GetWorld();
	USeinLevelDataSubsystem* LevelSubsystem =
		LevelDataSubsystem.Get();
	if (!World || !LevelSubsystem)
	{
		OutError =
			TEXT("Navigation could not resolve the Level Data readiness service.");
		return false;
	}
	if (!LevelSubsystem->EnsureInitialRuntimeDataPrepared(*World))
	{
		OutError =
			TEXT("Navigation could not prepare the initial Level Data substrate.");
		return false;
	}
	if (!bInitialStaticEnvironmentPrepared)
	{
		// Robust when the Level Data barrier completed before our delegate was
		// bound, or after an earlier rejected attempt was corrected.
		HandleInitialLevelDataPrepared();
	}
	if (!bInitialStaticEnvironmentPrepared)
	{
		OutError = InitialStaticEnvironmentAdoptionResult.IsRejected()
			? FString::Printf(
				TEXT("Navigation rejected the initial static environment: %s"),
				*InitialStaticEnvironmentAdoptionResult.Detail)
			: TEXT("Navigation did not receive the completed Level Data readiness barrier.");
		return false;
	}
	return true;
}

FSeinStaticEnvironmentAdoptionResult
USeinNavigationSubsystem::LoadBakedAssetIntoNav(UWorld& World)
{
	if (&World != GetWorld())
	{
		return FSeinStaticEnvironmentAdoptionResult::Rejected(
			TEXT("Navigation substrate adoption targeted a world other than the owning world."));
	}
	if (!Navigation)
	{
		return bNavigationConfigured
			? FSeinStaticEnvironmentAdoptionResult::Rejected(
				TEXT("The configured navigation implementation is unavailable."))
			: FSeinStaticEnvironmentAdoptionResult::NotApplicable(
				TEXT("Navigation is disabled."));
	}

	// Unified pipeline (CP1.1): if the shared substrate carries a baked grid + a
	// "Nav" channel, adopt it. If the substrate isn't loaded yet (subsystem
	// begin-play order isn't guaranteed), our OnLevelDataMutated subscription
	// re-adopts it the moment it loads.
	if (USeinLevelData* Substrate = LevelData.Get())
	{
		const FSeinStaticEnvironmentAdoptionResult Result =
			Navigation->LoadFromSubstrate(*Substrate);
		if (Result.IsAdopted())
		{
			UE_LOG(LogSeinNavSubsystem, Log,
				TEXT("Nav: loaded grid from the unified level-data substrate (CP1.1 substrate path)."));
			return Result;
		}
		if (Result.IsRejected())
		{
			UE_LOG(LogSeinNavSubsystem, Error,
				TEXT("Nav: rejected the unified Level Data substrate: %s"),
				*Result.Detail);
			return Result;
		}
		UE_LOG(LogSeinNavSubsystem, Log,
			TEXT("Nav: no applicable baked topology (%s). FindPath returns no-path unless the implementation has another runtime-data source."),
			*Result.Detail);
		return Result;
	}
	UE_LOG(LogSeinNavSubsystem, Log,
		TEXT("Nav: Level Data is disabled or unavailable. FindPath returns no-path unless the implementation has another runtime-data source."));
	return FSeinStaticEnvironmentAdoptionResult::NotApplicable(
		TEXT("Level Data is disabled or unavailable."));
}

void USeinNavigationSubsystem::OnLevelDataChanged()
{
	// Shared substrate rebaked / swapped — re-resolve adoption. An absent/empty
	// substrate keeps the valid no-data fallback, while a participating nav that
	// rejects present baked data makes readiness false until a corrected bake lands.
	if (const USeinLevelDataSubsystem* LevelSubsystem =
			LevelDataSubsystem.Get();
		LevelSubsystem
			&& !LevelSubsystem->IsInitialRuntimeDataPrepared())
	{
		// Initial asset adoption broadcasts before the shared readiness bit is
		// published. The one-shot prepared callback performs the sole O(N)
		// grid adoption after that transaction succeeds.
		return;
	}
	if (bStateBindingFrozen)
	{
		InvalidateCommittedCanonicalStateBinding(
			TEXT("The shared level-data substrate mutated after the navigation StateContract froze."));
		return;
	}
	if (UWorld* World = GetWorld())
	{
		InitialStaticEnvironmentAdoptionResult =
			LoadBakedAssetIntoNav(*World);
		bInitialStaticEnvironmentPrepared =
			!InitialStaticEnvironmentAdoptionResult.IsRejected();
	}
}

bool USeinNavigationSubsystem::FreezeCanonicalStateBinding(
	bool bCommit,
	FString& OutFrame,
	FGuid& OutStaticDigest,
	FString& OutError)
{
	OutFrame.Reset();
	OutStaticDigest.Invalidate();
	OutError.Reset();
	if (!StateBindingFailureReason.IsEmpty())
	{
		OutError = StateBindingFailureReason;
		return false;
	}
	if (!bInitialStaticEnvironmentPrepared)
	{
		OutError = InitialStaticEnvironmentAdoptionResult.IsRejected()
			? FString::Printf(
				TEXT("Navigation static-environment adoption was rejected: %s"),
				*InitialStaticEnvironmentAdoptionResult.Detail)
			: TEXT("Navigation static-environment preparation has not completed. Prepare initial Level Data before bootstrap or restore.");
		if (bStateBindingFrozen)
		{
			InvalidateCommittedCanonicalStateBinding(OutError);
		}
		return false;
	}

	FString CandidateFrame =
		TEXT("SeinARTS.Navigation.WorldBinding\n");
	AppendFramed(CandidateFrame, TEXT("3"));

	FGuid StaticDigest;
	if (!bNavigationConfigured)
	{
		if (!BuildDisabledStaticEnvironmentDigest(
			StaticDigest, OutError))
		{
			return false;
		}
		AppendFramed(CandidateFrame, TEXT("disabled"));
		AppendFramed(CandidateFrame, TEXT("<disabled>"));
		AppendFramed(
			CandidateFrame, TEXT("seinarts.navigation.disabled"));
		AppendFramed(CandidateFrame, TEXT("1"));
		AppendFramed(CandidateFrame, TEXT("1"));
		AppendFramed(CandidateFrame, TEXT("stateless"));
		AppendFramed(CandidateFrame, TEXT("0"));
		AppendFramed(
			CandidateFrame,
			StaticDigest.ToString(EGuidFormats::Digits));
	}
	else
	{
		if (!Navigation)
		{
			OutError = FString::Printf(
				TEXT("Configured navigation implementation '%s' has no live world instance."),
				*ConfiguredNavigationClassPath);
			if (bStateBindingFrozen)
			{
				InvalidateCommittedCanonicalStateBinding(OutError);
			}
			return false;
		}
		if (!Navigation->ComputeStaticEnvironmentDigest(
				StaticDigest, OutError)
			|| !StaticDigest.IsValid())
		{
			if (OutError.IsEmpty())
			{
				OutError = FString::Printf(
					TEXT("Navigation implementation '%s' returned an invalid static-environment digest."),
					*Navigation->GetClass()->GetPathName());
			}
			if (bStateBindingFrozen)
			{
				InvalidateCommittedCanonicalStateBinding(OutError);
			}
			return false;
		}

		FSeinNavigationStateCoverageClaim Coverage;
		bool bCoverageValid =
			Navigation->ComputeStateCoverageClaim(
				Coverage, OutError)
			&& !Coverage.StableImplementationId.IsEmpty()
			&& Coverage.StableImplementationId
				== Coverage.StableImplementationId.TrimStartAndEnd()
			&& Coverage.BehaviorRevision != 0
			&& Coverage.CoverageRevision != 0;

		FString CoverageKind;
		TArray<FString> CanonicalRequiredKeys;
		if (bCoverageValid)
		{
			switch (Coverage.StateCoverage)
			{
			case ESeinNavigationStateCoverage::Stateless:
				CoverageKind = TEXT("stateless");
				if (!Coverage.RequiredCanonicalStateContributors.IsEmpty())
				{
					OutError = FString::Printf(
						TEXT("Stateless navigation implementation '%s' names supplemental canonical-state contributors."),
						*Navigation->GetClass()->GetPathName());
					bCoverageValid = false;
				}
				break;

			case ESeinNavigationStateCoverage::
				CanonicalStateContributors:
				CoverageKind = TEXT("canonical-state-contributors");
				if (Coverage.RequiredCanonicalStateContributors.IsEmpty()
					|| Coverage.RequiredCanonicalStateContributors.Num()
						> MaxNavigationStateContributors)
				{
					OutError = FString::Printf(
						TEXT("Stateful navigation implementation '%s' names an invalid supplemental canonical-state contributor count."),
						*Navigation->GetClass()->GetPathName());
					bCoverageValid = false;
					break;
				}

				if (const USeinWorldSubsystem* Sim =
						GetWorld()
							? GetWorld()->GetSubsystem<
								USeinWorldSubsystem>()
							: nullptr)
				{
					CanonicalRequiredKeys.Reserve(
						Coverage.RequiredCanonicalStateContributors.Num());
					for (const FSeinCanonicalStateKey& Required :
						Coverage.RequiredCanonicalStateContributors)
					{
						const FString CanonicalKey =
							FSeinCanonicalStateRegistry::CanonicalKey(
								Required);
						if (CanonicalKey.IsEmpty())
						{
							OutError = FString::Printf(
								TEXT("Navigation implementation '%s' names an invalid supplemental canonical-state contributor."),
								*Navigation->GetClass()->GetPathName());
							bCoverageValid = false;
							break;
						}
						if (!Sim->HasFrozenCanonicalStateContributor(
								Required,
								ESeinCanonicalStateRole::Authoritative))
						{
							OutError = FString::Printf(
								TEXT("Navigation implementation '%s' requires missing authoritative canonical-state contributor '%s'."),
								*Navigation->GetClass()->GetPathName(),
								*CanonicalKey);
							bCoverageValid = false;
							break;
						}
						CanonicalRequiredKeys.Add(CanonicalKey);
					}
				}
				else
				{
					OutError = FString::Printf(
						TEXT("Navigation implementation '%s' cannot verify supplemental canonical-state contributors without a frozen Core world schema."),
						*Navigation->GetClass()->GetPathName());
					bCoverageValid = false;
				}

				CanonicalRequiredKeys.Sort();
				for (int32 Index = 1;
					bCoverageValid
						&& Index < CanonicalRequiredKeys.Num();
					++Index)
				{
					if (CanonicalRequiredKeys[Index - 1]
						== CanonicalRequiredKeys[Index])
					{
						OutError = FString::Printf(
							TEXT("Navigation implementation '%s' names duplicate canonical-state contributor '%s'."),
							*Navigation->GetClass()->GetPathName(),
							*CanonicalRequiredKeys[Index]);
						bCoverageValid = false;
					}
				}
				break;

			case ESeinNavigationStateCoverage::Unspecified:
			default:
				OutError = FString::Printf(
					TEXT("Navigation implementation '%s' did not declare whether its mutable state is stateless or restored by canonical contributors."),
					*Navigation->GetClass()->GetPathName());
				bCoverageValid = false;
				break;
			}
		}

		if (!bCoverageValid)
		{
			if (OutError.IsEmpty())
			{
				OutError = FString::Printf(
					TEXT("Navigation implementation '%s' returned an invalid exact-state coverage claim."),
					*Navigation->GetClass()->GetPathName());
			}
			if (bStateBindingFrozen)
			{
				InvalidateCommittedCanonicalStateBinding(OutError);
			}
			return false;
		}

		AppendFramed(CandidateFrame, TEXT("enabled"));
		AppendFramed(
			CandidateFrame, Navigation->GetClass()->GetPathName());
		AppendFramed(
			CandidateFrame, Coverage.StableImplementationId);
		AppendFramed(
			CandidateFrame,
			LexToString(Coverage.BehaviorRevision));
		AppendFramed(
			CandidateFrame,
			LexToString(Coverage.CoverageRevision));
		AppendFramed(CandidateFrame, CoverageKind);
		AppendFramed(
			CandidateFrame,
			LexToString(CanonicalRequiredKeys.Num()));
		for (const FString& RequiredKey : CanonicalRequiredKeys)
		{
			AppendFramed(CandidateFrame, RequiredKey);
		}
		AppendFramed(
			CandidateFrame,
			StaticDigest.ToString(EGuidFormats::Digits));
	}

	const uint64 CurrentGeneration =
		bNavigationConfigured && Navigation
			? Navigation->GetStaticEnvironmentGeneration()
			: 0;
	if (bStateBindingFrozen
		&& (FrozenStateBindingFrame != CandidateFrame
			|| FrozenStaticEnvironmentDigest != StaticDigest))
	{
		InvalidateCommittedCanonicalStateBinding(
			TEXT("Navigation implementation or static environment changed after the match StateContract froze."));
		OutError = StateBindingFailureReason;
		return false;
	}
	// Generation drift catches an in-place topology write that a cached
	// content digest cannot see. Local-only: the counter never enters the
	// peer-compared frame, so identical bakes re-adopted a different number
	// of times still agree across peers.
	if (bStateBindingFrozen
		&& FrozenStaticEnvironmentGeneration != CurrentGeneration)
	{
		InvalidateCommittedCanonicalStateBinding(
			TEXT("Navigation static topology mutated in place after the match StateContract froze."));
		OutError = StateBindingFailureReason;
		return false;
	}

	if (bCommit)
	{
		bStateBindingFrozen = true;
		FrozenStateBindingFrame = CandidateFrame;
		FrozenStaticEnvironmentDigest = StaticDigest;
		FrozenStaticEnvironmentGeneration = CurrentGeneration;
	}
	OutFrame = MoveTemp(CandidateFrame);
	OutStaticDigest = StaticDigest;
	return true;
}

bool USeinNavigationSubsystem::RevalidateCanonicalStateBindingCandidate(
	const FString& ExpectedFrame,
	const FGuid& ExpectedStaticDigest,
	FString& OutError)
{
	FString CurrentFrame;
	FGuid CurrentStaticDigest;
	if (!FreezeCanonicalStateBinding(
			false, CurrentFrame, CurrentStaticDigest, OutError))
	{
		return false;
	}
	if (CurrentFrame != ExpectedFrame
		|| CurrentStaticDigest != ExpectedStaticDigest)
	{
		OutError =
			TEXT("Navigation static environment changed during restore staging.");
		return false;
	}
	return true;
}

void USeinNavigationSubsystem::CommitCanonicalStateBinding(
	const FString& Frame,
	const FGuid& StaticDigest)
{
	FString Error;
	const bool bCandidateStillValid =
		RevalidateCanonicalStateBindingCandidate(
			Frame, StaticDigest, Error);
	checkf(bCandidateStillValid,
		TEXT("Navigation world binding changed after final restore lease verification: %s"),
		*Error);
	if (!bCandidateStillValid)
	{
		InvalidateCommittedCanonicalStateBinding(Error);
		return;
	}
	bStateBindingFrozen = true;
	FrozenStateBindingFrame = Frame;
	FrozenStaticEnvironmentDigest = StaticDigest;
	FrozenStaticEnvironmentGeneration =
		bNavigationConfigured && Navigation
			? Navigation->GetStaticEnvironmentGeneration()
			: 0;
}

bool USeinNavigationSubsystem::ValidateCommittedCanonicalStateBinding()
{
	if (!bStateBindingFrozen)
	{
		return true;
	}
	FString Frame;
	FGuid StaticDigest;
	FString Error;
	return FreezeCanonicalStateBinding(
		false, Frame, StaticDigest, Error);
}

void USeinNavigationSubsystem::InvalidateCanonicalStateBinding(
	const FString& Reason)
{
	if (StateBindingFailureReason.IsEmpty())
	{
		StateBindingFailureReason = Reason.IsEmpty()
			? TEXT("The frozen navigation static-environment contract became invalid.")
			: Reason;
		UE_LOG(LogSeinNavSubsystem, Error, TEXT("%s"),
			*StateBindingFailureReason);
	}
}

void USeinNavigationSubsystem::InvalidateCommittedCanonicalStateBinding(
	const FString& Reason)
{
	InvalidateCanonicalStateBinding(Reason);
	if (UWorld* World = GetWorld())
	{
		if (USeinWorldSubsystem* Sim =
				World->GetSubsystem<USeinWorldSubsystem>())
		{
			Sim->InvalidateDeterministicExecutionContract(
				StateBindingFailureReason);
		}
	}
}

void USeinNavigationSubsystem::BindSimDelegates(UWorld& World)
{
	USeinWorldSubsystem* Sim = World.GetSubsystem<USeinWorldSubsystem>();
	if (!Sim || !Navigation) return;

	TWeakObjectPtr<USeinNavigation> NavWeak = Navigation;

	Sim->PathableTargetResolver.BindWeakLambda(this,
		[NavWeak](const FFixedVector& FromWorld, const FFixedVector& ToWorld, const FGameplayTagContainer& AgentTags) -> bool
		{
			USeinNavigation* Nav = NavWeak.Get();
			if (!Nav || !Nav->HasRuntimeData()) return true; // no data = permit (tests, nav-less games)
			return Nav->IsReachable(FromWorld, ToWorld, AgentTags);
		});

	// Footprint placement resolver — gates abilities that have bRequiresFreeFootprint
	// set (typically building-placement abilities). Same permit-on-no-data
	// fallback as the pathable resolver so tests / nav-less games don't block
	// placement spuriously. The nav's IsPlacementValid does the actual cell
	// check (default impl is a center-only sample; subclasses rasterize).
	Sim->FootprintPlacementResolver.BindWeakLambda(this,
		[NavWeak](const FFixedVector& CenterWorld, const FFixedPoint& YawDegrees,
			const FSeinExtentsShape& Shape, uint8 AgentLayerMask) -> bool
		{
			USeinNavigation* Nav = NavWeak.Get();
			if (!Nav || !Nav->HasRuntimeData()) return true;
			return Nav->IsPlacementValid(CenterWorld, YawDegrees, Shape, AgentLayerMask);
		});

	// Per-cell passability resolver — used by hot-path systems (penetration
	// resolution, etc.) to gate proposed positions against nav blockers.
	// Same permit-on-no-data fallback so tests / nav-less games don't gate
	// behavior unexpectedly.
	Sim->PassableResolver.BindWeakLambda(this,
		[NavWeak](const FFixedVector& WorldPos) -> bool
		{
			USeinNavigation* Nav = NavWeak.Get();
			if (!Nav || !Nav->HasRuntimeData()) return true;
			return Nav->IsPassable(WorldPos);
		});

	// Dynamic-aware per-cell passability — like PassableResolver, but ALSO rejects
	// cells under runtime dynamic blockers (bBlocksNav). Cover slot selection uses
	// this so units aren't dispatched onto a cell a dynamically-blocking wall sits
	// on (PassableResolver, static-only, misses those entirely). Default ground
	// agent layer (0x01). Same permit-on-no-data fallback.
	Sim->DynamicPassableResolver.BindWeakLambda(this,
		[NavWeak](const FFixedVector& WorldPos) -> bool
		{
			USeinNavigation* Nav = NavWeak.Get();
			if (!Nav || !Nav->HasRuntimeData()) return true;
			return Nav->IsWorldPositionClear(WorldPos, /*AgentNavLayerMask=*/ 0x01);
		});

	// Ground-height resolver — used by penetration resolution's step-height
	// gate. Walkable-only ON so impassable cells don't produce phantom height
	// reads. No-data fallback = false (height unknown → callers skip check).
	Sim->HeightResolver.BindWeakLambda(this,
		[NavWeak](const FFixedVector& WorldPos, FFixedPoint& OutZ) -> bool
		{
			USeinNavigation* Nav = NavWeak.Get();
			if (!Nav || !Nav->HasRuntimeData()) return false;
			return Nav->GetCellHeightAt(WorldPos, OutZ, /*bWalkableOnly=*/ true);
		});

	// Nav projection resolver — used by formation resolvers to snap slot
	// positions onto walkable terrain. No-op (identity + true) when no nav
	// data is loaded; otherwise routes through Nav->ProjectPointToNavOnElevation
	// so projection is Z-biased (prefers cells whose stored height matches
	// the input's Z). Without the elevation bias, slots that fan over the
	// edge of a raised platform would project to the floor below — producing
	// "stragglers running off cliffsides" when a click on a platform top
	// places some destinations off the platform.
	//
	// No early-out for "already-passable input" — `IsPassable` checks only
	// the cell's XY (cells are XY-indexed), so a slot landing on a floor-
	// level cell IS reported passable even when the click target is on a
	// platform overhead. Always routing through `ProjectPointToNavOnElevation`
	// lets the impl's quick-check (cell-at-input-XY-on-matching-elevation)
	// short-circuit when appropriate, and ring-scans for an elevation-
	// matching cell otherwise.
	Sim->NavProjectResolver.BindWeakLambda(this,
		[NavWeak](const FFixedVector& InWorld, FFixedVector& OutPassable) -> bool
		{
			USeinNavigation* Nav = NavWeak.Get();
			if (!Nav || !Nav->HasRuntimeData())
			{
				OutPassable = InWorld;
				return true;
			}
			return Nav->ProjectPointToNavOnElevation(InWorld, OutPassable);
		});

	// Occupancy-aware projection — same as above but rejects cells already claimed by peer footprints,
	// so a formation that overflows the play area packs onto the inside edge without piling. Used by
	// USeinFormation::ProjectPositionsToNavigable. Same permit-on-no-data fallback.
	Sim->NavProjectFreeResolver.BindWeakLambda(this,
		[NavWeak](const FFixedVector& InWorld, FFixedPoint SelfRadius,
			const TArray<FFixedVector>& AvoidCentres, const TArray<FFixedPoint>& AvoidRadii,
			FFixedVector& OutProjected) -> bool
		{
			USeinNavigation* Nav = NavWeak.Get();
			if (!Nav || !Nav->HasRuntimeData())
			{
				OutProjected = InWorld;
				return true;
			}
			return Nav->ProjectPointToNavFree(InWorld, SelfRadius, AvoidCentres, AvoidRadii, OutProjected);
		});

	// Reset the path budget tracker. Self-checking reset in RequestPath
	// handles the per-tick boundary going forward; we just zero state here
	// at world-begin so the first tick after world load starts clean
	// regardless of any leftover counter from a prior PIE session.
	PathRequestsThisTick = 0;
	LastResetTick = -1;
	MarkCanonicalStateDirty();
}

void USeinNavigationSubsystem::UnbindSimDelegates()
{
	UWorld* World = GetWorld();
	USeinWorldSubsystem* Sim =
		World ? World->GetSubsystem<USeinWorldSubsystem>() : nullptr;
	if (!Sim)
	{
		return;
	}

	Sim->PathableTargetResolver.Unbind();
	Sim->FootprintPlacementResolver.Unbind();
	Sim->PassableResolver.Unbind();
	Sim->DynamicPassableResolver.Unbind();
	Sim->HeightResolver.Unbind();
	Sim->NavProjectResolver.Unbind();
	Sim->NavProjectFreeResolver.Unbind();
}

ESeinPathResult USeinNavigationSubsystem::RequestPath(const FSeinPathRequest& Request, FSeinPath& OutPath)
{
	if (!Navigation)
	{
		OutPath.Clear();
		return ESeinPathResult::NoNavigation;
	}
	// RequestPath owns every runtime write to the path budget and async
	// continuation queues. One barrier covers all branches below.
	MarkCanonicalStateDirty();

	// Self-checking per-tick reset. Read the sim tick from the world
	// subsystem; if it advanced since our last reset, clear the budget.
	// Bulletproof against subscription-failure scenarios (live coding,
	// world reload edge cases) that the previous OnSimTickCompleted
	// binding could miss. Cost is one TMap<UClass*, T> lookup +
	// int compare per call — negligible.
	int32 CurrentSimTick = -1;
	if (UWorld* World = GetWorld())
	{
		if (USeinWorldSubsystem* Sim = World->GetSubsystem<USeinWorldSubsystem>())
		{
			CurrentSimTick = Sim->GetCurrentTick();
		}
	}
	// ── Async pathfinding (opt-in) ────────────────────────────────────────────
	// When Sein.Sim.AsyncPathfinding is on, requests don't run inline — they queue
	// and run as a deterministic BATCH one tick later (header + SeinParallel.h). Deliberately NOT
	// gated on Sein.Sim.Parallel: the determinism is the fixed 1-tick defer + in-tick join, not the
	// threading. RunPathBatch runs the batch parallel when Parallel is on and byte-identically serial
	// when off, so the deferred (sim-affecting, fingerprinted) timing is identical on every peer no
	// matter the per-machine Parallel toggle; gating on it would let a Parallel-off peer run paths
	// inline same-tick and desync. Needs a valid Requester to key results; one-shot queries (BPFL,
	// reachability) call Navigation->FindPath directly and never reach this branch.
	if (SeinSimAsyncPathfindingEnabled() && Request.Requester.IsValid())
	{
		// Drain LAST tick's queue ONCE per tick, at the first async request of the
		// tick (deterministic: latent actions tick in a fixed order across clients).
		if (CurrentSimTick != LastDrainTick)
		{
			DrainAsyncPathQueue(CurrentSimTick);
			LastDrainTick = CurrentSimTick;
		}

		// Result ready (from this or a prior drain)? Deliver + consume — but ONLY if it was computed
		// for THIS request. The maps key on Requester alone, so a unit re-ordered to a new destination
		// since it queued could otherwise be handed its PRIOR order's path (wrong goal), or a stale
		// NotFound could fail a valid new order. The identity check (End + agent params, NOT Start)
		// rejects a mismatched cached result and falls through to (re)queue the live request.
		if (FSeinAsyncPathResult* Ready = AsyncResults.Find(Request.Requester))
		{
			if (PathRequestIdentityMatches(Ready->Request, Request))
			{
				const bool bValid = Ready->Path.bIsValid;
				OutPath = MoveTemp(Ready->Path);
				AsyncResults.Remove(Request.Requester);
				if (!bValid) { OutPath.Clear(); return ESeinPathResult::NotFound; }
				return ESeinPathResult::Found;
			}
			// Stale (requester re-ordered since queueing): drop it, fall through to queue the live request.
			AsyncResults.Remove(Request.Requester);
		}

		// Not ready → queue (dedup by Requester) and wait. The move action treats
		// Throttled as "retry next tick", which re-enqueues — so the queue self-
		// populates with exactly the still-waiting units each tick.
		AsyncQueue.Add(Request.Requester, Request);
		OutPath.Clear();
		return ESeinPathResult::Throttled;
	}

	if (CurrentSimTick != LastResetTick)
	{
#if !UE_BUILD_SHIPPING
		// First-reset confirmation logs at Log level so designers can verify
		// the budget tracker is wired without enabling Verbose. Per-tick
		// resets log at Verbose only — flip with `Log LogSeinNavSubsystem
		// Verbose` when investigating budget pressure.
		if (LastResetTick == -1)
		{
			UE_LOG(LogSeinNavSubsystem, Log,
				TEXT("Path budget tracker initialized — first reset at tick %d (used %d/%d in pre-init)"),
				CurrentSimTick, PathRequestsThisTick,
				GetDefault<USeinARTSCoreSettings>() ? GetDefault<USeinARTSCoreSettings>()->PathRequestsPerTickBudget : 32);
		}
		else if (PathRequestsThisTick > 0)
		{
			UE_LOG(LogSeinNavSubsystem, Verbose,
				TEXT("Path budget reset (tick %d → %d, used %d last tick)"),
				LastResetTick, CurrentSimTick, PathRequestsThisTick);
		}
#endif
		PathRequestsThisTick = 0;
		LastResetTick = CurrentSimTick;
	}

	// Read budget per-call rather than caching — settings can change at
	// dev-time and should take effect on the next request without restart.
	const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>();
	const int32 Budget = Settings ? Settings->PathRequestsPerTickBudget : 32;

	if (PathRequestsThisTick >= Budget)
	{
		OutPath.Clear();
#if !UE_BUILD_SHIPPING
		// Verbose by default — only loud when investigating budget pressure.
		UE_LOG(LogSeinNavSubsystem, Verbose,
			TEXT("RequestPath throttled at tick %d (this tick: %d / budget: %d, requester %s)"),
			CurrentSimTick, PathRequestsThisTick, Budget,
			*Request.Requester.ToString());
#endif
		return ESeinPathResult::Throttled;
	}

	// Budget consumed regardless of FindPath success — both Found and NotFound
	// represent "A* ran this tick." Throttled is the only outcome that doesn't
	// consume budget (it short-circuits before A*).
	++PathRequestsThisTick;

	if (Navigation->FindPath(Request, OutPath) && OutPath.bIsValid)
	{
		return ESeinPathResult::Found;
	}
	OutPath.Clear();
	return ESeinPathResult::NotFound;
}

bool USeinNavigationSubsystem::PathRequestIdentityMatches(const FSeinPathRequest& A, const FSeinPathRequest& B)
{
	// Deliberately EXCLUDES Start (re-sampled to the unit's live position every repath tick, so a
	// Start-inclusive check would never match a moving unit's cached result) and Requester (the map
	// key, always equal here). Everything else that changes the computed route is compared.
	return A.End.X == B.End.X && A.End.Y == B.End.Y && A.End.Z == B.End.Z
		&& A.AgentNavLayerMask         == B.AgentNavLayerMask
		&& A.AgentFootprintRadius      == B.AgentFootprintRadius
		&& A.AgentWallPaddingCells     == B.AgentWallPaddingCells
		&& A.AgentMaxSearchNodes       == B.AgentMaxSearchNodes
		&& A.bAuthoritativeDestination == B.bAuthoritativeDestination
		&& A.GroupId                   == B.GroupId
		&& A.BlockedTerrainTags        == B.BlockedTerrainTags;
}

void USeinNavigationSubsystem::DrainAsyncPathQueue(int32 CurrentTick)
{
	// Drop any un-consumed results from the previous drain: each is normally consumed
	// the same tick it's produced (the requesting action retries that tick), so a
	// leftover belongs to a unit that canceled/died — safe to discard. Keeps it bounded.
	AsyncResults.Reset();

	if (!Navigation || AsyncQueue.Num() == 0)
	{
		AsyncQueue.Reset();
		return;
	}

	const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>();
	const int32 Budget = FMath::Max(0, Settings ? Settings->PathRequestsPerTickBudget : 32);

	// Canonical order: serve queued requesters sorted by handle so EVERY client serves
	// the same subset under budget (TMap iteration order is nondeterministic).
	TArray<FSeinEntityHandle> Keys;
	AsyncQueue.GetKeys(Keys);
	Keys.Sort([](const FSeinEntityHandle& A, const FSeinEntityHandle& B)
	{
		if (A.Index != B.Index) return A.Index < B.Index;
		return A.Generation < B.Generation;
	});

	const int32 Count = FMath::Min(Keys.Num(), Budget);

	TArray<FSeinPathRequest> Batch;
	Batch.Reserve(Count);
	for (int32 i = 0; i < Count; ++i)
	{
		Batch.Add(AsyncQueue.FindChecked(Keys[i]));
	}

	// RunPathBatch joins before it returns (parallel inside the AStar override; serial
	// otherwise), so the whole burst completes within THIS tick — no cross-tick spread,
	// and the result is bit-identical to the serial path. Determinism by construction.
	TArray<FSeinPath> Results;
	Navigation->RunPathBatch(Batch, Results);

	for (int32 i = 0; i < Count && i < Results.Num(); ++i)
	{
		// Pair the result with the request that produced it (Batch[i], Keys[i], Results[i] share
		// index i) so delivery can reject it if the requester was re-ordered to a new destination.
		FSeinAsyncPathResult Entry;
		Entry.Request = Batch[i];
		Entry.Path    = MoveTemp(Results[i]);
		AsyncResults.Add(Keys[i], MoveTemp(Entry));
	}

	// Clear the whole queue: served units consume their result (and stop requesting);
	// the unserved tail re-enqueues next tick via the action's Throttled retry — so no
	// stale/dead entries accumulate and the per-tick serve order stays canonical.
	AsyncQueue.Reset();
}

void USeinNavigationSubsystem::MarkCanonicalStateDirty()
{
	++CanonicalStateMutationRevision;
	if (CanonicalStateMutationRevision == 0)
	{
		++CanonicalStateMutationRevision;
	}
}

USeinNavigation* USeinNavigationSubsystem::GetNavigationForWorld(const UObject* WorldContextObject)
{
	if (!WorldContextObject) return nullptr;
	if (UWorld* World = WorldContextObject->GetWorld())
	{
		if (USeinNavigationSubsystem* Sub = World->GetSubsystem<USeinNavigationSubsystem>())
		{
			return Sub->Navigation;
		}
	}
	return nullptr;
}
