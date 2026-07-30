/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinFogOfWarSubsystem.cpp
 */

#include "SeinFogOfWarSubsystem.h"
#include "SeinFogOfWar.h"
#include "SeinARTSFogOfWarLog.h"
#include "Default/SeinFogOfWarDefault.h"
#include "Core/SeinSystemPriority.h"
#include "Settings/PluginSettings.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "SeinLevelData.h"
#include "SeinLevelDataSubsystem.h"
#include "SeinLevelLayerProvider.h"
#include "Serialization/SeinCanonicalInitialStateDigest.h"
#include "Serialization/SeinFogOfWarStateCodecRegistry.h"

#include "Engine/World.h"
#include "Types/FixedPoint.h"
#include "Types/Vector.h"

// LogSeinFogOfWarSubsystem is module-declared (SeinARTSFogOfWarLog.h) so it is
// reliably filterable in the Output Log — do not re-introduce a _STATIC define here.

namespace
{
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
			TEXT("SeinARTS.FogOfWar.DisabledStaticEnvironment"), 1);
		return Writer.WriteString(TEXT("disabled"))
			&& Writer.Finalize(OutDigest, OutError);
	}

	class FSeinFogOfWarStampSystem final : public ISeinSystem
	{
	public:
		FSeinFogOfWarStampSystem(
			USeinFogOfWarSubsystem* InOwner,
			USeinFogOfWar* InFogOfWar)
			: Owner(InOwner)
			, FogOfWar(InFogOfWar)
		{
		}

		virtual void Tick(
			FFixedPoint /*DeltaTime*/,
			USeinWorldSubsystem& World) override
		{
			USeinFogOfWar* Fog = FogOfWar.Get();
			if (!Fog)
			{
				return;
			}
			USeinFogOfWarSubsystem* OwnerPtr = Owner.Get();
			if (!OwnerPtr
				|| !OwnerPtr->
					ValidateCommittedCanonicalStateBinding())
			{
				return;
			}

			const USeinARTSCoreSettings* Settings =
				GetDefault<USeinARTSCoreSettings>();
			const int32 Interval =
				Settings && Settings->VisionTickInterval > 0
					? Settings->VisionTickInterval
					: 1;
			if ((World.GetCurrentTick() % Interval) == 0)
			{
				Fog->TickStamps(World.GetWorld());
			}
		}

		virtual FSeinSystemDescriptor DescribeSystem() const override
		{
			return FSeinSystemDescriptor::WithCanonicalState(
				FName(TEXT("seinarts.fog_of_war.stamp")),
				1u,
				ESeinTickPhase::PostTick,
				SeinSystemPriority::FogOfWar,
				{ FName(TEXT(
					"seinarts.fog-of-war/canonical-state")) });
		}

	private:
		TWeakObjectPtr<USeinFogOfWarSubsystem> Owner;
		TWeakObjectPtr<USeinFogOfWar> FogOfWar;
	};
}

void USeinFogOfWarSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency(USeinWorldSubsystem::StaticClass());
	StateCodecToken = 0;
	bSimDelegatesBound = false;
	bFogConfigured = false;
	bInitialStaticEnvironmentPrepared = false;
	bStateBindingFrozen = false;
	InitialStaticEnvironmentAdoptionResult =
		FSeinStaticEnvironmentAdoptionResult::NotApplicable(
			TEXT("Initial fog substrate adoption has not run."));
	ConfiguredFogClassPath.Reset();
	StateCodecFailureReason.Reset();
	FrozenStateBindingFrame.Reset();
	FrozenStaticEnvironmentDigest.Invalidate();

	// Resolve the configured fog class. Fall back to the shipped default if
	// the setting is empty or points to a stale / abstract class.
	// WYSIWYG. None/empty => fog is intentionally OFF (FogOfWar stays null; nothing is hidden and
	// every unit is always visible). A set-but-unloadable/abstract class is a mistake, not an
	// off-switch: it falls back to the shipped default with a logged error. Every consumer, and the
	// layer-provider registration below, already null-guard FogOfWar.
	const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>();
	UClass* FogClass = nullptr;
	if (Settings && !Settings->FogOfWarClass.IsNull())
	{
		FogClass = Settings->FogOfWarClass.TryLoadClass<USeinFogOfWar>();
		if (!FogClass || FogClass->HasAnyClassFlags(CLASS_Abstract))
		{
			UE_LOG(LogSeinFogOfWarSubsystem, Error,
				TEXT("FogOfWarClass '%s' could not be loaded or is abstract — falling back to the shipped default."),
				*Settings->FogOfWarClass.ToString());
			FogClass = USeinFogOfWarDefault::StaticClass();
		}
	}

	if (FogClass)
	{
		bFogConfigured = true;
		ConfiguredFogClassPath = FogClass->GetPathName();
		FogOfWar = NewObject<USeinFogOfWar>(this, FogClass, TEXT("SeinFogOfWar"));
		if (FogOfWar)
		{
			FogOfWar->InitializeForWorld(GetWorld());
			FString CodecError;
			if (!FSeinFogOfWarStateCodecRegistry::FreezeForClass(
				FogOfWar->GetClass(),
				StateCodecToken,
				CodecError))
			{
				StateCodecFailureReason = MoveTemp(CodecError);
				UE_LOG(LogSeinFogOfWarSubsystem, Error,
					TEXT("FogOfWarClass '%s' cannot participate in deterministic state: %s"),
					*ConfiguredFogClassPath,
					*StateCodecFailureReason);
				FogOfWar->DeinitializeFromWorld();
				FogOfWar = nullptr;
			}
		}
		else
		{
			StateCodecFailureReason = FString::Printf(
				TEXT("Failed to instantiate fog class %s."),
				*FogClass->GetName());
			UE_LOG(LogSeinFogOfWarSubsystem, Error, TEXT("%s"),
				*StateCodecFailureReason);
		}
	}
	else
	{
		USeinARTSCoreSettings::ReportDisabledSystem(TEXT("Fog Of War"),
			TEXT("Nothing is hidden; every unit is always visible."), /*bHighSeverity*/ false);
	}

	// CP1.1 unified level-data pipeline. Force the substrate subsystem up first
	// (InitializeDependency → it exists in editor + PIE for the bake-button path),
	// then — if this fog participates (returns a provider face) — register it as
	// the "FogOfWar" layer provider and subscribe to rebake/reload so the runtime
	// grid tracks the shared bake. Non-participating fogs (the base) skip all of this.
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
					&USeinFogOfWarSubsystem::
						HandleInitialLevelDataPrepared);
			if (USeinLevelData* Substrate = LevelSubsystem->GetLevelData())
			{
				LevelData = Substrate;
				LevelDataMutatedHandle = Substrate->OnLevelDataMutated.AddUObject(
					this, &USeinFogOfWarSubsystem::OnLevelDataChanged);
				if (FogOfWar)
				{
					if (ISeinLevelLayerProvider* Provider =
							FogOfWar->GetLevelDataProvider())
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

	if (UWorld* World = GetWorld())
	{
		RegisterStampSystem(*World);
	}
}

void USeinFogOfWarSubsystem::Deinitialize()
{
	ReleaseModuleOwnedState();
	StateCodecToken = 0;
	Super::Deinitialize();
}

void USeinFogOfWarSubsystem::ReleaseModuleOwnedStateForModuleUnload()
{
	check(IsInGameThread());
	if (bFogConfigured && StateCodecFailureReason.IsEmpty())
	{
		StateCodecFailureReason =
			TEXT("The fog-of-war module unloaded while this world was alive.");
	}
	StateCodecToken = 0;
	ReleaseModuleOwnedState();
}

void USeinFogOfWarSubsystem::ReleaseModuleOwnedState()
{
	if (UWorld* World = GetWorld())
	{
		if (USeinWorldSubsystem* Sim =
			World->GetSubsystem<USeinWorldSubsystem>())
		{
			Sim->TerminateAndReleaseForModuleUnload(
				FName(TEXT("SeinARTSFogOfWar")),
				TEXT("Fog-of-war executable state is being released."));
		}
	}
	UnbindSimDelegates();

	// Unhook from the shared substrate (CP1.1) before FogOfWar is torn down — we
	// reference FogOfWar->GetLevelDataProvider() to unregister.
	if (USeinLevelData* Substrate = LevelData.Get())
	{
		if (LevelDataMutatedHandle.IsValid())
		{
			Substrate->OnLevelDataMutated.Remove(LevelDataMutatedHandle);
			LevelDataMutatedHandle.Reset();
		}
		if (FogOfWar)
		{
			if (ISeinLevelLayerProvider* Provider = FogOfWar->GetLevelDataProvider())
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
	bInitialStaticEnvironmentPrepared = false;
	InitialStaticEnvironmentAdoptionResult =
		FSeinStaticEnvironmentAdoptionResult::NotApplicable(
			TEXT("The fog subsystem is not initialized."));

	if (StampSystem)
	{
		if (UWorld* World = GetWorld())
		{
			if (USeinWorldSubsystem* Sim = World->GetSubsystem<USeinWorldSubsystem>())
			{
				Sim->UnregisterSystem(StampSystem.Get());
			}
		}
		StampSystem.Reset();
	}
	if (FogOfWar)
	{
		FogOfWar->DeinitializeFromWorld();
	}
	FogOfWar = nullptr;
}

void USeinFogOfWarSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	if (USeinLevelDataSubsystem* LevelSubsystem =
			LevelDataSubsystem.Get())
	{
		LevelSubsystem->EnsureInitialRuntimeDataPrepared(InWorld);
	}
	BindSimDelegates(InWorld);
}

void USeinFogOfWarSubsystem::HandleInitialLevelDataPrepared()
{
	if (bInitialStaticEnvironmentPrepared)
	{
		return;
	}
	if (UWorld* World = GetWorld())
	{
		InitialStaticEnvironmentAdoptionResult =
			LoadBakedAssetIntoFogOfWar(*World);
		if (!InitialStaticEnvironmentAdoptionResult.IsRejected())
		{
			InitGridIfUnbaked(*World);
			bInitialStaticEnvironmentPrepared = true;
		}
	}
	else
	{
		InitialStaticEnvironmentAdoptionResult =
			FSeinStaticEnvironmentAdoptionResult::Rejected(
				TEXT("Fog initial substrate adoption could not resolve its owning world."));
	}
}

bool USeinFogOfWarSubsystem::PrepareInitialCanonicalStateEnvironment(
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
			TEXT("Fog could not resolve the Level Data readiness service.");
		return false;
	}
	if (!LevelSubsystem->EnsureInitialRuntimeDataPrepared(*World))
	{
		OutError =
			TEXT("Fog could not prepare the initial Level Data substrate.");
		return false;
	}
	if (!bInitialStaticEnvironmentPrepared)
	{
		HandleInitialLevelDataPrepared();
	}
	if (!bInitialStaticEnvironmentPrepared)
	{
		OutError = InitialStaticEnvironmentAdoptionResult.IsRejected()
			? FString::Printf(
				TEXT("Fog rejected the initial static environment: %s"),
				*InitialStaticEnvironmentAdoptionResult.Detail)
			: TEXT("Fog did not receive the completed Level Data readiness barrier.");
		return false;
	}
	return true;
}

FSeinStaticEnvironmentAdoptionResult
USeinFogOfWarSubsystem::LoadBakedAssetIntoFogOfWar(UWorld& World)
{
	if (&World != GetWorld())
	{
		return FSeinStaticEnvironmentAdoptionResult::Rejected(
			TEXT("Fog substrate adoption targeted a world other than the owning world."));
	}
	if (!FogOfWar)
	{
		if (bFogConfigured)
		{
			return FSeinStaticEnvironmentAdoptionResult::Rejected(
				StateCodecFailureReason.IsEmpty()
					? FString(TEXT(
						"The configured fog implementation is unavailable."))
					: StateCodecFailureReason);
		}
		return FSeinStaticEnvironmentAdoptionResult::NotApplicable(
			TEXT("Fog of war is disabled."));
	}

	// Unified pipeline (CP1.1): if the shared substrate carries a baked grid
	// + a "FogOfWar" channel, adopt it and we're done. If the substrate isn't
	// loaded yet (subsystem begin-play order isn't guaranteed), our
	// OnLevelDataMutated subscription re-adopts it the moment it loads.
	if (USeinLevelData* Substrate = LevelData.Get())
	{
		const FSeinStaticEnvironmentAdoptionResult Result =
			FogOfWar->LoadFromSubstrate(*Substrate);
		if (Result.IsAdopted())
		{
			UE_LOG(LogSeinFogOfWarSubsystem, Log,
				TEXT("FoW: loaded grid from the unified level-data substrate (CP1.1 substrate path)."));
			return Result;
		}
		if (Result.IsRejected())
		{
			UE_LOG(LogSeinFogOfWarSubsystem, Error,
				TEXT("FoW: rejected the unified Level Data substrate: %s"),
				*Result.Detail);
			return Result;
		}
		UE_LOG(LogSeinFogOfWarSubsystem, Log,
			TEXT("FoW: no applicable baked channel (%s); no-bake grid initialization may follow."),
			*Result.Detail);
		return Result;
	}

	UE_LOG(LogSeinFogOfWarSubsystem, Log,
		TEXT("FoW: Level Data is disabled or unavailable; no-bake grid initialization may follow."));
	return FSeinStaticEnvironmentAdoptionResult::NotApplicable(
		TEXT("Level Data is disabled or unavailable."));
}

void USeinFogOfWarSubsystem::OnLevelDataChanged()
{
	// Shared substrate rebaked / swapped — re-resolve adoption. An absent/empty
	// substrate keeps the valid no-bake fallback, while a participating fog that
	// rejects present baked data makes readiness false until a corrected bake lands.
	if (const USeinLevelDataSubsystem* LevelSubsystem =
			LevelDataSubsystem.Get();
		LevelSubsystem
			&& !LevelSubsystem->IsInitialRuntimeDataPrepared())
	{
		return;
	}
	if (bStateBindingFrozen)
	{
		InvalidateCommittedCanonicalStateBinding(
			TEXT("The shared level-data substrate mutated after the fog StateContract froze."));
		return;
	}
	if (UWorld* World = GetWorld())
	{
		InitialStaticEnvironmentAdoptionResult =
			LoadBakedAssetIntoFogOfWar(*World);
		if (!InitialStaticEnvironmentAdoptionResult.IsRejected())
		{
			InitGridIfUnbaked(*World);
		}
		bInitialStaticEnvironmentPrepared =
			!InitialStaticEnvironmentAdoptionResult.IsRejected();
	}
}

void USeinFogOfWarSubsystem::InitGridIfUnbaked(UWorld& World)
{
	if (!FogOfWar) return;
	if (FogOfWar->HasRuntimeData()) return; // bake already loaded — skip auto-init
	FogOfWar->InitGridFromVolumes(&World);
}

void USeinFogOfWarSubsystem::RegisterStampSystem(UWorld& World)
{
	if (!FogOfWar) return;
	USeinWorldSubsystem* Sim = World.GetSubsystem<USeinWorldSubsystem>();
	if (!Sim) return;
	if (StampSystem)
	{
		Sim->UnregisterSystem(StampSystem.Get());
	}
	StampSystem =
		MakeUnique<FSeinFogOfWarStampSystem>(this, FogOfWar);
	Sim->RegisterSystem(StampSystem.Get());
}

void USeinFogOfWarSubsystem::BindSimDelegates(UWorld& World)
{
	USeinWorldSubsystem* Sim = World.GetSubsystem<USeinWorldSubsystem>();
	if (!Sim || !FogOfWar) return;

	TWeakObjectPtr<USeinFogOfWar> FogWeak = FogOfWar;
	TWeakObjectPtr<USeinFogOfWarSubsystem> OwnerWeak = this;

	// Note: TargetWorld is FFixedVector end-to-end — sim callers pass fixed
	// point directly; no FVector round-trip at the boundary.
	Sim->LineOfSightResolver.BindWeakLambda(this,
		[OwnerWeak, FogWeak](
			FSeinPlayerID ObserverPlayer,
			const FFixedVector& TargetWorld) -> bool
		{
			USeinFogOfWarSubsystem* Owner = OwnerWeak.Get();
			if (!Owner
				|| !Owner->
					ValidateCommittedCanonicalStateBinding())
			{
				// Contract drift is terminalized by validation. Reject this
				// same-tick gameplay query instead of consuming drifted fog.
				return false;
			}
			USeinFogOfWar* Fog = FogWeak.Get();
			if (!Fog || !Fog->HasRuntimeData()) return true; // no data = permit (tests, fog-less games)
			return Fog->IsCellVisible(ObserverPlayer, TargetWorld, SEIN_FOW_BIT_NORMAL);
		});
	bSimDelegatesBound = true;
}

void USeinFogOfWarSubsystem::UnbindSimDelegates()
{
	if (!bSimDelegatesBound)
	{
		return;
	}
	if (UWorld* World = GetWorld())
	{
		if (USeinWorldSubsystem* Sim =
			World->GetSubsystem<USeinWorldSubsystem>())
		{
			if (Sim->LineOfSightResolver.IsBoundToObject(this))
			{
				Sim->LineOfSightResolver.Unbind();
			}
		}
	}
	bSimDelegatesBound = false;
}

bool USeinFogOfWarSubsystem::FreezeCanonicalStateBinding(
	bool bCommit,
	FString& OutFrame,
	FGuid& OutStaticDigest,
	FString& OutError)
{
	OutFrame.Reset();
	OutStaticDigest.Invalidate();
	OutError.Reset();
	if (!bInitialStaticEnvironmentPrepared)
	{
		OutError = InitialStaticEnvironmentAdoptionResult.IsRejected()
			? FString::Printf(
				TEXT("Fog static-environment adoption was rejected: %s"),
				*InitialStaticEnvironmentAdoptionResult.Detail)
			: TEXT("Fog static-environment preparation has not completed. Prepare initial Level Data before bootstrap or restore.");
		if (bStateBindingFrozen)
		{
			InvalidateCommittedCanonicalStateBinding(OutError);
		}
		return false;
	}

	FString CandidateFrame =
		TEXT("SeinARTS.FogOfWar.WorldBinding\n");
	AppendFramed(CandidateFrame, TEXT("1"));
	if (!bFogConfigured)
	{
		FGuid DisabledDigest;
		if (!BuildDisabledStaticEnvironmentDigest(
			DisabledDigest, OutError))
		{
			if (bStateBindingFrozen)
			{
				InvalidateCommittedCanonicalStateBinding(OutError);
			}
			return false;
		}
		AppendFramed(CandidateFrame, TEXT("disabled"));
		AppendFramed(CandidateFrame, TEXT("<disabled>"));
		AppendFramed(
			CandidateFrame, TEXT("seinarts.fog.disabled"));
		AppendFramed(CandidateFrame, TEXT("0"));
		AppendFramed(CandidateFrame, TEXT("0"));
		AppendFramed(CandidateFrame, TEXT("0"));
		AppendFramed(
			CandidateFrame,
			DisabledDigest.ToString(EGuidFormats::Digits));

		if (bStateBindingFrozen
			&& (FrozenStateBindingFrame != CandidateFrame
				|| FrozenStaticEnvironmentDigest
					!= DisabledDigest))
		{
			OutError =
				TEXT("Disabled fog state binding changed after freeze.");
			InvalidateCommittedCanonicalStateBinding(OutError);
			return false;
		}
		if (bCommit)
		{
			bStateBindingFrozen = true;
			FrozenStateBindingFrame = CandidateFrame;
			FrozenStaticEnvironmentDigest = DisabledDigest;
		}
		OutFrame = MoveTemp(CandidateFrame);
		OutStaticDigest = DisabledDigest;
		return true;
	}

	if (!StateCodecFailureReason.IsEmpty()
		|| StateCodecToken == 0
		|| !FogOfWar)
	{
		OutError = StateCodecFailureReason.IsEmpty()
			? TEXT("Configured fog implementation has no live exact-state codec binding.")
			: StateCodecFailureReason;
		if (bStateBindingFrozen)
		{
			InvalidateCommittedCanonicalStateBinding(OutError);
		}
		return false;
	}

	FSeinFogOfWarStateCodecRegistry::FResolvedClaim Claim;
	if (!FSeinFogOfWarStateCodecRegistry::ResolveForClass(
		StateCodecToken,
		FogOfWar->GetClass(),
		Claim,
		OutError))
	{
		if (OutError.IsEmpty())
		{
			OutError =
				TEXT("Frozen fog codec no longer matches the active implementation class.");
		}
		if (bStateBindingFrozen)
		{
			InvalidateCommittedCanonicalStateBinding(OutError);
		}
		return false;
	}

	FGuid StaticDigest;
	if (!FSeinFogOfWarStateCodecRegistry::
		ComputeStaticEnvironmentDigest(
			StateCodecToken,
			*FogOfWar,
			StaticDigest,
			OutError))
	{
		if (bStateBindingFrozen)
		{
			InvalidateCommittedCanonicalStateBinding(OutError);
		}
		return false;
	}
	if (!StaticDigest.IsValid())
	{
		OutError =
			TEXT("Fog state codec returned an invalid static-environment digest.");
		if (bStateBindingFrozen)
		{
			InvalidateCommittedCanonicalStateBinding(OutError);
		}
		return false;
	}

	AppendFramed(CandidateFrame, TEXT("enabled"));
	AppendFramed(
		CandidateFrame, FogOfWar->GetClass()->GetPathName());
	AppendFramed(
		CandidateFrame,
		Claim.Descriptor.StableImplementationId);
	AppendFramed(
		CandidateFrame,
		LexToString(Claim.Descriptor.StateSchemaVersion));
	AppendFramed(
		CandidateFrame,
		LexToString(Claim.Descriptor.BehaviorRevision));
	AppendFramed(
		CandidateFrame,
		LexToString(Claim.Descriptor.CodecRevision));
	AppendFramed(
		CandidateFrame,
		Claim.Descriptor.PayloadSchemaDigest.ToString(
			EGuidFormats::Digits));
	AppendFramed(
		CandidateFrame,
		Claim.CodecDescriptorDigest.ToString(
			EGuidFormats::Digits));
	AppendFramed(
		CandidateFrame,
		LexToString(
			Claim.Descriptor.Limits.MaxRecursionDepth));
	AppendFramed(
		CandidateFrame,
		LexToString(
			Claim.Descriptor.Limits.MaxEncodedBytes));
	AppendFramed(
		CandidateFrame,
		LexToString(
			Claim.Descriptor.Limits.MaxAggregateElements));
	AppendFramed(
		CandidateFrame,
		StaticDigest.ToString(EGuidFormats::Digits));

	if (bStateBindingFrozen
		&& (FrozenStateBindingFrame != CandidateFrame
			|| FrozenStaticEnvironmentDigest != StaticDigest))
	{
		OutError =
			TEXT("Fog implementation or static environment changed after the match StateContract froze.");
		InvalidateCommittedCanonicalStateBinding(OutError);
		return false;
	}

	if (bCommit)
	{
		bStateBindingFrozen = true;
		FrozenStateBindingFrame = CandidateFrame;
		FrozenStaticEnvironmentDigest = StaticDigest;
	}
	OutFrame = MoveTemp(CandidateFrame);
	OutStaticDigest = StaticDigest;
	return true;
}

bool USeinFogOfWarSubsystem::RevalidateCanonicalStateBindingCandidate(
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
			TEXT("Fog static environment changed during restore staging.");
		return false;
	}
	return true;
}

void USeinFogOfWarSubsystem::CommitCanonicalStateBinding(
	const FString& Frame,
	const FGuid& StaticDigest)
{
	FString Error;
	const bool bCandidateStillValid =
		RevalidateCanonicalStateBindingCandidate(
			Frame, StaticDigest, Error);
	checkf(bCandidateStillValid,
		TEXT("Fog world binding changed after final restore lease verification: %s"),
		*Error);
	if (!bCandidateStillValid)
	{
		InvalidateCommittedCanonicalStateBinding(Error);
		return;
	}
	bStateBindingFrozen = true;
	FrozenStateBindingFrame = Frame;
	FrozenStaticEnvironmentDigest = StaticDigest;
}

bool USeinFogOfWarSubsystem::
	ValidateCommittedCanonicalStateBinding()
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

void USeinFogOfWarSubsystem::InvalidateCommittedCanonicalStateBinding(
	const FString& Reason)
{
	if (StateCodecFailureReason.IsEmpty())
	{
		StateCodecFailureReason = Reason.IsEmpty()
			? TEXT("The frozen fog static-environment contract became invalid.")
			: Reason;
		UE_LOG(LogSeinFogOfWarSubsystem, Error, TEXT("%s"),
			*StateCodecFailureReason);
	}
	if (UWorld* World = GetWorld())
	{
		if (USeinWorldSubsystem* Sim =
				World->GetSubsystem<USeinWorldSubsystem>())
		{
			Sim->InvalidateDeterministicExecutionContract(
				StateCodecFailureReason);
		}
	}
}

void USeinFogOfWarSubsystem::InvalidateCanonicalStateCodecLease(
	uint64 Token,
	const FString& Reason)
{
	if (Token == 0 || StateCodecToken != Token)
	{
		return;
	}
	StateCodecFailureReason = Reason.IsEmpty()
		? TEXT("The frozen fog state codec generation became unavailable.")
		: Reason;
	StateCodecToken = 0;
	ReleaseModuleOwnedState();
}

USeinFogOfWar* USeinFogOfWarSubsystem::GetFogOfWarForWorld(const UObject* WorldContextObject)
{
	if (!WorldContextObject) return nullptr;
	if (UWorld* World = WorldContextObject->GetWorld())
	{
		if (USeinFogOfWarSubsystem* Sub = World->GetSubsystem<USeinFogOfWarSubsystem>())
		{
			return Sub->FogOfWar;
		}
	}
	return nullptr;
}
