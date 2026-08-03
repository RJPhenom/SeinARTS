/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinFogOfWarCanonicalStateProvider.cpp
 */

#include "Serialization/SeinFogOfWarCanonicalStateProvider.h"

#include "Serialization/SeinFogOfWarCanonicalState.h"
#include "Serialization/SeinCanonicalInitialStateDigest.h"
#include "SeinFogOfWar.h"
#include "SeinFogOfWarSubsystem.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Engine/World.h"

namespace
{
	const FName OwnerModuleId(TEXT("SeinARTSFogOfWar"));
	const FString DisabledClassPath(TEXT("<disabled>"));
	const FString DisabledImplementationId(
		TEXT("seinarts.fog.disabled"));

	USeinFogOfWarSubsystem* ResolveSubsystem(
		const USeinWorldSubsystem& Services)
	{
		UWorld* World = Services.GetWorld();
		return World
			? World->GetSubsystem<USeinFogOfWarSubsystem>()
			: nullptr;
	}
}

struct FSeinFogOfWarCanonicalStateProvider
{
	struct FRestoreStage final
		: ISeinCanonicalStateRestoreStage
	{
		TWeakObjectPtr<USeinFogOfWarSubsystem> Subsystem;
		TWeakObjectPtr<USeinFogOfWar> Fog;
		uint64 CodecToken = 0;
		bool bDisabled = false;
		FString BindingFrame;
		FGuid StaticEnvironmentDigest;
		TUniquePtr<ISeinFogOfWarStateRestoreStage> ConcreteStage;

		virtual void GatherReferencedObjects(
			TArray<UObject*>& OutObjects) const override
		{
			if (ConcreteStage)
			{
				ConcreteStage->GatherReferencedObjects(OutObjects);
			}
		}

		virtual bool VerifyExternalLeases(
			FString& OutError) const override
		{
			return FSeinFogOfWarCanonicalStateProvider::
				VerifyStageLease(*this, OutError);
		}
	};

	static bool PrepareWorldBinding(
		const FSeinCanonicalStateWorldBindingContext& Context,
		FString& OutError)
	{
		USeinFogOfWarSubsystem* Subsystem =
			ResolveSubsystem(Context.Services);
		if (!Subsystem)
		{
			OutError =
				TEXT("Fog canonical state could not resolve its world subsystem.");
			return false;
		}
		return Subsystem->
			PrepareInitialCanonicalStateEnvironment(OutError);
	}

	static bool FreezeWorldBinding(
		const FSeinCanonicalStateWorldBindingContext& Context,
		FString& OutFrame,
		FString& OutError)
	{
		USeinFogOfWarSubsystem* Subsystem =
			ResolveSubsystem(Context.Services);
		if (!Subsystem)
		{
			OutError =
				TEXT("Fog canonical state could not resolve its world subsystem.");
			return false;
		}
		FGuid StaticEnvironmentDigest;
		return Subsystem->FreezeCanonicalStateBinding(
			Context.BindingDisposition
				== ESeinCanonicalStateWorldBindingDisposition::
					BootstrapCommit,
			OutFrame,
			StaticEnvironmentDigest,
			OutError);
	}

	static bool Capture(
		const FSeinCanonicalStateCaptureContext& Context,
		FInstancedStruct& OutState,
		FString& OutError)
	{
		OutState.Reset();
		USeinFogOfWarSubsystem* Subsystem =
			ResolveSubsystem(Context.World);
		if (!Subsystem)
		{
			OutError =
				TEXT("Fog canonical-state capture could not resolve its world subsystem.");
			return false;
		}

		FString BindingFrame;
		FGuid StaticEnvironmentDigest;
		if (!Subsystem->FreezeCanonicalStateBinding(
			false,
			BindingFrame,
			StaticEnvironmentDigest,
			OutError))
		{
			return false;
		}

		FSeinFogOfWarCanonicalStateEnvelope Envelope;
		Envelope.StaticEnvironmentDigest =
			StaticEnvironmentDigest;
		if (!Subsystem->bFogConfigured)
		{
			Envelope.bEnabled = false;
			Envelope.ImplementationClassPath =
				DisabledClassPath;
			Envelope.StableImplementationId =
				DisabledImplementationId;
			OutState = FInstancedStruct::Make(
				MoveTemp(Envelope));
			return true;
		}

		USeinFogOfWar* Fog = Subsystem->FogOfWar;
		FSeinFogOfWarStateCodecRegistry::FResolvedClaim Claim;
		if (!Fog
			|| Subsystem->StateCodecToken == 0
			|| !FSeinFogOfWarStateCodecRegistry::ResolveForClass(
				Subsystem->StateCodecToken,
				Fog->GetClass(),
				Claim,
				OutError))
		{
			if (OutError.IsEmpty())
			{
				OutError =
					TEXT("Configured fog implementation lost its exact codec binding.");
			}
			return false;
		}

		FGuid CurrentStaticDigest;
		if (!FSeinFogOfWarStateCodecRegistry::
				ComputeStaticEnvironmentDigest(
					Subsystem->StateCodecToken,
					*Fog,
					CurrentStaticDigest,
					OutError)
			|| CurrentStaticDigest
				!= StaticEnvironmentDigest)
		{
			if (OutError.IsEmpty())
			{
				OutError =
					TEXT("Fog static environment no longer matches the frozen StateContract.");
			}
			return false;
		}

		FInstancedStruct ConcretePayload;
		if (!FSeinFogOfWarStateCodecRegistry::CapturePayload(
			Subsystem->StateCodecToken,
			{ Context.World, *Fog, Context.Tick },
			ConcretePayload,
			OutError)
			|| !FSeinFogOfWarStateCodecRegistry::EncodePayload(
				Claim,
				ConcretePayload,
				Envelope.PayloadBytes,
				OutError))
		{
			return false;
		}

		Envelope.bEnabled = true;
		Envelope.ImplementationClassPath =
			Fog->GetClass()->GetPathName();
		Envelope.StableImplementationId =
			Claim.Descriptor.StableImplementationId;
		Envelope.StateSchemaVersion =
			Claim.Descriptor.StateSchemaVersion;
		Envelope.BehaviorRevision =
			Claim.Descriptor.BehaviorRevision;
		Envelope.CodecRevision =
			Claim.Descriptor.CodecRevision;
		Envelope.PayloadSchemaDigest =
			Claim.Descriptor.PayloadSchemaDigest;
		Envelope.CodecDescriptorDigest =
			Claim.CodecDescriptorDigest;
		Envelope.MaxRecursionDepth =
			Claim.Descriptor.Limits.MaxRecursionDepth;
		Envelope.MaxPayloadBytes =
			Claim.Descriptor.Limits.MaxEncodedBytes;
		Envelope.MaxAggregateElements =
			Claim.Descriptor.Limits.MaxAggregateElements;
		OutState = FInstancedStruct::Make(MoveTemp(Envelope));
		return true;
	}

	static bool MatchesClaim(
		const FSeinFogOfWarCanonicalStateEnvelope& Envelope,
		const USeinFogOfWar& Fog,
		const FSeinFogOfWarStateCodecRegistry::FResolvedClaim& Claim)
	{
		return Envelope.bEnabled
			&& Envelope.ImplementationClassPath
				== Fog.GetClass()->GetPathName()
			&& Envelope.StableImplementationId
				== Claim.Descriptor.StableImplementationId
			&& Envelope.StateSchemaVersion
				== Claim.Descriptor.StateSchemaVersion
			&& Envelope.BehaviorRevision
				== Claim.Descriptor.BehaviorRevision
			&& Envelope.CodecRevision
				== Claim.Descriptor.CodecRevision
			&& Envelope.PayloadSchemaDigest
				== Claim.Descriptor.PayloadSchemaDigest
			&& Envelope.CodecDescriptorDigest
				== Claim.CodecDescriptorDigest
			&& Envelope.MaxRecursionDepth
				== Claim.Descriptor.Limits.MaxRecursionDepth
			&& Envelope.MaxPayloadBytes
				== Claim.Descriptor.Limits.MaxEncodedBytes
			&& Envelope.MaxAggregateElements
				== Claim.Descriptor.Limits.MaxAggregateElements
			&& Envelope.PayloadBytes.Num()
				<= Claim.Descriptor.Limits.MaxEncodedBytes;
	}

	static bool CaptureRoutineRoot(
		const FSeinCanonicalStateCaptureContext& Context,
		bool bForceFullRebuild,
		FSeinCanonicalStateRoutineRootRecord& OutRecord,
		FString& OutError)
	{
		OutRecord = {};
		USeinFogOfWarSubsystem* Subsystem =
			ResolveSubsystem(Context.World);
		if (!Subsystem)
		{
			OutError =
				TEXT("Fog routine root could not resolve its world subsystem.");
			return false;
		}

		FString BindingFrame;
		FGuid StaticEnvironmentDigest;
		if (!Subsystem->FreezeCanonicalStateBinding(
			false,
			BindingFrame,
			StaticEnvironmentDigest,
			OutError))
		{
			return false;
		}

		FSeinCanonicalDigestWriter Writer(
			TEXT("SeinARTS.Fog.Routine.Contributor"), 1);
		if (!Writer.WriteGuid(StaticEnvironmentDigest)
			|| !Writer.WriteBool(Subsystem->bFogConfigured))
		{
			OutError = Writer.GetError();
			return false;
		}
		if (!Subsystem->bFogConfigured)
		{
			if (!Writer.WriteString(DisabledClassPath)
				|| !Writer.WriteString(DisabledImplementationId)
				|| !Writer.Finalize(OutRecord.LeafDigest, OutError))
			{
				return false;
			}
			OutRecord.ProjectedPayloadBytes = 2 * sizeof(FGuid);
			return true;
		}

		USeinFogOfWar* Fog = Subsystem->FogOfWar;
		FSeinFogOfWarStateCodecRegistry::FResolvedClaim Claim;
		if (!Fog
			|| Subsystem->StateCodecToken == 0
			|| !FSeinFogOfWarStateCodecRegistry::ResolveForClass(
				Subsystem->StateCodecToken,
				Fog->GetClass(),
				Claim,
				OutError))
		{
			if (OutError.IsEmpty())
			{
				OutError =
					TEXT("Configured fog implementation lost its exact codec binding.");
			}
			return false;
		}

		FGuid CurrentStaticDigest;
		if (!FSeinFogOfWarStateCodecRegistry::ComputeStaticEnvironmentDigest(
				Subsystem->StateCodecToken,
				*Fog,
				CurrentStaticDigest,
				OutError)
			|| CurrentStaticDigest != StaticEnvironmentDigest)
		{
			if (OutError.IsEmpty())
			{
				OutError =
					TEXT("Fog static environment no longer matches the frozen StateContract.");
			}
			return false;
		}

		FGuid PayloadDigest;
		uint64 ProjectedBytes = 0;
		uint64 MutationRevision = 0;
		if (!FSeinFogOfWarStateCodecRegistry::CaptureRoutineRoot(
			Subsystem->StateCodecToken,
			{ Context.World, *Fog, Context.Tick },
			bForceFullRebuild,
			PayloadDigest,
			ProjectedBytes,
			MutationRevision,
			OutError))
		{
			return false;
		}

		if (!Writer.WriteString(Fog->GetClass()->GetPathName())
			|| !Writer.WriteString(Claim.Descriptor.StableImplementationId)
			|| !Writer.WriteUInt32(Claim.Descriptor.StateSchemaVersion)
			|| !Writer.WriteUInt32(Claim.Descriptor.BehaviorRevision)
			|| !Writer.WriteUInt32(Claim.Descriptor.CodecRevision)
			|| !Writer.WriteGuid(Claim.Descriptor.PayloadSchemaDigest)
			|| !Writer.WriteGuid(Claim.CodecDescriptorDigest)
			|| !Writer.WriteInt32(Claim.Descriptor.Limits.MaxRecursionDepth)
			|| !Writer.WriteInt32(Claim.Descriptor.Limits.MaxEncodedBytes)
			|| !Writer.WriteInt32(Claim.Descriptor.Limits.MaxAggregateElements)
			|| !Writer.WriteGuid(PayloadDigest)
			|| !Writer.Finalize(OutRecord.LeafDigest, OutError))
		{
			return false;
		}
		OutRecord.ProjectedPayloadBytes =
			ProjectedBytes + 8 * sizeof(FGuid);
		OutRecord.MutationRevision = MutationRevision;
		return true;
	}

	static bool StageRestore(
		const FSeinCanonicalStateStageContext& Context,
		const FInstancedStruct& State,
		TUniquePtr<ISeinCanonicalStateRestoreStage>& OutStage,
		FString& OutError)
	{
		OutStage.Reset();
		const FSeinFogOfWarCanonicalStateEnvelope* Envelope =
			State.GetPtr<FSeinFogOfWarCanonicalStateEnvelope>();
		if (!Envelope || !Context.Services)
		{
			OutError =
				TEXT("Fog canonical restore requires its exact envelope and read-only world services.");
			return false;
		}

		USeinFogOfWarSubsystem* Subsystem =
			ResolveSubsystem(*Context.Services);
		if (!Subsystem)
		{
			OutError =
				TEXT("Fog canonical restore could not resolve its world subsystem.");
			return false;
		}

		FString BindingFrame;
		FGuid StaticEnvironmentDigest;
		if (!Subsystem->FreezeCanonicalStateBinding(
			false,
			BindingFrame,
			StaticEnvironmentDigest,
			OutError)
			|| Envelope->StaticEnvironmentDigest
				!= StaticEnvironmentDigest)
		{
			if (OutError.IsEmpty())
			{
				OutError =
					TEXT("Fog envelope static environment does not match the local StateContract.");
			}
			return false;
		}

		TUniquePtr<FRestoreStage> Stage =
			MakeUnique<FRestoreStage>();
		Stage->Subsystem = Subsystem;
		Stage->BindingFrame = MoveTemp(BindingFrame);
		Stage->StaticEnvironmentDigest =
			StaticEnvironmentDigest;
		if (!Envelope->bEnabled)
		{
			if (Subsystem->bFogConfigured
				|| Envelope->ImplementationClassPath
					!= DisabledClassPath
				|| Envelope->StableImplementationId
					!= DisabledImplementationId
				|| Envelope->StateSchemaVersion != 0
				|| Envelope->BehaviorRevision != 0
				|| Envelope->CodecRevision != 0
				|| Envelope->PayloadSchemaDigest.IsValid()
				|| Envelope->CodecDescriptorDigest.IsValid()
				|| Envelope->MaxRecursionDepth != 0
				|| Envelope->MaxPayloadBytes != 0
				|| Envelope->MaxAggregateElements != 0
				|| !Envelope->PayloadBytes.IsEmpty())
			{
				OutError =
					TEXT("Disabled fog canonical envelope is malformed or locally enabled.");
				return false;
			}
			Stage->bDisabled = true;
			OutStage = MoveTemp(Stage);
			return true;
		}

		USeinFogOfWar* Fog = Subsystem->FogOfWar;
		FSeinFogOfWarStateCodecRegistry::FResolvedClaim Claim;
		if (!Subsystem->bFogConfigured
			|| !Fog
			|| Subsystem->StateCodecToken == 0
			|| !FSeinFogOfWarStateCodecRegistry::ResolveForClass(
				Subsystem->StateCodecToken,
				Fog->GetClass(),
				Claim,
				OutError)
			|| !MatchesClaim(*Envelope, *Fog, Claim))
		{
			if (OutError.IsEmpty())
			{
				OutError =
					TEXT("Fog canonical envelope does not match the world's exact implementation codec.");
			}
			return false;
		}

		FGuid CurrentStaticDigest;
		if (!FSeinFogOfWarStateCodecRegistry::
				ComputeStaticEnvironmentDigest(
					Subsystem->StateCodecToken,
					*Fog,
					CurrentStaticDigest,
					OutError)
			|| CurrentStaticDigest
				!= Envelope->StaticEnvironmentDigest)
		{
			if (OutError.IsEmpty())
			{
				OutError =
					TEXT("Fog canonical envelope targets a different static environment.");
			}
			return false;
		}

		FInstancedStruct ConcretePayload;
		if (!FSeinFogOfWarStateCodecRegistry::DecodePayload(
			Claim,
			Envelope->PayloadBytes,
			ConcretePayload,
			OutError)
			|| !FSeinFogOfWarStateCodecRegistry::StagePayload(
				Subsystem->StateCodecToken,
				{ Context.Tick,
					Context.Candidate,
					*Context.Services,
					*Fog },
				ConcretePayload,
				Stage->ConcreteStage,
				OutError))
		{
			return false;
		}

		Stage->Fog = Fog;
		Stage->CodecToken = Subsystem->StateCodecToken;
		OutStage = MoveTemp(Stage);
		return true;
	}

	static bool VerifyStageLease(
		const FRestoreStage& Stage,
		FString& OutError)
	{
		OutError.Reset();
		USeinFogOfWarSubsystem* Subsystem =
			Stage.Subsystem.Get();
		if (!Subsystem)
		{
			OutError =
				TEXT("Fog world subsystem disappeared before commit.");
			return false;
		}
		if (!Subsystem->RevalidateCanonicalStateBindingCandidate(
				Stage.BindingFrame,
				Stage.StaticEnvironmentDigest,
				OutError))
		{
			return false;
		}
		if (Stage.bDisabled)
		{
			if (Subsystem->GetFogOfWar())
			{
				OutError =
					TEXT("Disabled fog binding became enabled before commit.");
				return false;
			}
			return true;
		}
		if (Stage.CodecToken == 0
			|| !FSeinFogOfWarStateCodecRegistry::
				IsTokenAvailable(Stage.CodecToken)
			|| Stage.Fog.Get() != Subsystem->GetFogOfWar())
		{
			OutError =
				TEXT("Frozen fog codec generation became unavailable before commit.");
			return false;
		}
		return true;
	}

	static void CommitRestore(
		FSeinCanonicalStateCommitContext& Context,
		TUniquePtr<ISeinCanonicalStateRestoreStage>&& OpaqueStage)
	{
		FRestoreStage* Stage =
			static_cast<FRestoreStage*>(OpaqueStage.Get());
		check(Stage);
		USeinFogOfWarSubsystem* Subsystem =
			ResolveSubsystem(Context.World);
		check(Subsystem);
		Subsystem->CommitCanonicalStateBinding(
			Stage->BindingFrame,
			Stage->StaticEnvironmentDigest);
		if (Stage->bDisabled)
		{
			return;
		}

		USeinFogOfWar* Fog =
			Subsystem ? Subsystem->GetFogOfWar() : nullptr;
		check(Fog && Fog == Stage->Fog.Get());
		check(Subsystem->StateCodecToken == Stage->CodecToken);

		FSeinFogOfWarStateCommitContext FogContext{
			Context.World, *Fog, Context.Tick
		};
		FSeinFogOfWarStateCodecRegistry::CommitPayload(
			Stage->CodecToken,
			FogContext,
			MoveTemp(Stage->ConcreteStage));
	}
};

FSeinCanonicalStateRegistrationHandle
SeinRegisterFogOfWarCanonicalStateProvider(FString& OutError)
{
	FSeinCanonicalStateDescriptor Descriptor;
	Descriptor.Key.StableDomainId =
		TEXT("seinarts.fog-of-war");
	Descriptor.Key.StableContributorId =
		TEXT("canonical-state");
	Descriptor.SchemaVersion = 1;
	Descriptor.ImplementationRevision = 2;
	Descriptor.Role = ESeinCanonicalStateRole::Authoritative;
	Descriptor.PayloadStruct =
		FSeinFogOfWarCanonicalStateEnvelope::StaticStruct();
	Descriptor.Limits.MaxRecursionDepth = 32;
	Descriptor.Limits.MaxEncodedBytes = 40 * 1024 * 1024;
	Descriptor.Limits.MaxAggregateElements = 40 * 1024 * 1024;
	// The FoW subsystem owns this contributor's lifecycle: the stamp system
	// that names it registers only in FoW-enabled worlds, and a FoW-disabled
	// world must still bootstrap with the contributor present.
	Descriptor.bExternallyOwned = true;

	FSeinCanonicalStateContributorOps Ops;
	Ops.PrepareWorldBinding =
		&FSeinFogOfWarCanonicalStateProvider::
			PrepareWorldBinding;
	Ops.FreezeWorldBinding =
		&FSeinFogOfWarCanonicalStateProvider::
			FreezeWorldBinding;
	Ops.Capture =
		&FSeinFogOfWarCanonicalStateProvider::Capture;
	Ops.CaptureRoutineRoot =
		&FSeinFogOfWarCanonicalStateProvider::CaptureRoutineRoot;
	Ops.StageRestore =
		&FSeinFogOfWarCanonicalStateProvider::StageRestore;
	Ops.CommitRestore =
		&FSeinFogOfWarCanonicalStateProvider::CommitRestore;
	return FSeinCanonicalStateRegistry::Register(
		OwnerModuleId, Descriptor, MoveTemp(Ops), &OutError);
}
