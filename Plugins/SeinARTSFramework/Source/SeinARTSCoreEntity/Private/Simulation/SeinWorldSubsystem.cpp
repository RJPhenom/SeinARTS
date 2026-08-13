/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinWorldSubsystem.cpp
 * @brief   Implementation of the core simulation subsystem.
 */

#include "Simulation/SeinWorldSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Logging/MessageLog.h"
#include "Simulation/SeinActorBridgeSubsystem.h"
#include "Actor/SeinActor.h"
#include "AI/SeinAIController.h"
#include "Engine/GameInstance.h"
#include "HAL/IConsoleManager.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"
#include "Serialization/SeinDeterministicValueDigest.h"
#include "Serialization/SeinLatentActionCodecRegistry.h"
#include "Serialization/SeinPoolObjectCodecRegistry.h"
#include "Serialization/SeinSimulationContentManifest.h"
#include "Serialization/SeinSimulationContentRegistry.h"
#include "Simulation/SeinCanonicalStateRecipeRegistry.h"
#include "Simulation/SeinContainmentStateValidation.h"
#include "Components/SeinIdentityComponent.h"
#include "Data/SeinFaction.h"
#include "Data/SeinMatchBootstrapRules.h"
#include "Data/SeinReplayHeader.h"
#include "Data/SeinWorldSnapshot.h"
#include "Settings/PluginSettings.h"
#include "Core/SeinSimContext.h"
#include "Core/SeinParallel.h"
#include "Lib/SeinMatchSettingsBPFL.h"
#include "Input/SeinCommandAuthorityPolicy.h"
#include "Input/SeinCommandSchemaRegistry.h"
#include "Abilities/SeinAbility.h"
#include "Abilities/SeinAbilityValidation.h"
#include "Abilities/SeinLatentActionManager.h"
#include "Abilities/SeinTargeterSpec.h"
#include "Components/SeinExtentsHelpers.h"
#include "Components/SeinAbilityComponent.h"
#include "Components/SeinActiveEffectsComponent.h"
#include "Components/SeinAttachmentSpec.h"
#include "Components/SeinBrokerMembershipData.h"
#include "Components/SeinCommandBrokerData.h"
#include "Components/SeinConstructionComponent.h"
#include "Components/SeinContainmentData.h"
#include "Components/SeinContainmentMemberData.h"
#include "Components/SeinNavigationComponent.h"
#include "Components/SeinProductionComponent.h"
#include "Components/SeinSquadComponent.h"
#include "Components/SeinSquadMemberComponent.h"
#include "Components/SeinTransportSpec.h"
#include "Actor/SeinEntityComponent.h"
#include "Brokers/SeinCommandBrokerResolver.h"
#include "Brokers/SeinDefaultCommandBrokerResolver.h"
#include "Attributes/SeinModifier.h"
#include "Attributes/SeinAttributeResolver.h"
#include "Effects/SeinEffect.h"
#include "Lib/SeinAbilityBPFL.h"   // for runtime Grant/Revoke during effect fan-out
#include "Lib/SeinResourceBPFL.h"
#include "Lib/SeinCommandBrokerBPFL.h"   // ComputeMultiBrokerAnchors (shared with the preview)
#include "Tags/SeinARTSGameplayTags.h"
#include "Containers/Ticker.h"
#include "StructUtils/InstancedStruct.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/StructOnScope.h"
#include "UObject/GCObject.h"
#include "Hash/Blake3.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

// Built-in systems
#include "Simulation/Systems/SeinEffectTickSystem.h"
#include "Simulation/Systems/SeinCooldownSystem.h"
#include "Simulation/Systems/SeinAbilityTickSystem.h"
#include "Simulation/Systems/SeinCommandBrokerSystem.h"
#include "Simulation/Systems/SeinProductionSystem.h"
#include "Simulation/Systems/SeinCollisionResolutionSystem.h"
#include "Simulation/Systems/SeinCollisionBroadphaseSystem.h"
#include "Collision/SeinCollisionResolver.h"
#include "Collision/SeinCollisionResolverDefault.h"
#include "Simulation/Systems/SeinLifespanSystem.h"

#include "Brokers/SeinBrokerTypes.h"

#include "SeinARTSCoreEntityLog.h"  // LogSeinSim (module-shared)
#include "SeinARTSCoreEntityModule.h"

namespace
{
	constexpr int32 MaxAuthoritativeDestinationProviders = 128;

	void AppendUtf8Framed(FString& Out, const FString& Value)
	{
		const FTCHARToUTF8 Utf8(*Value);
		Out += FString::Printf(TEXT("%d:"), Utf8.Length());
		Out += Value;
		Out += TEXT("\n");
	}

	void AppendUInt32BigEndian(TArray<uint8>& Bytes, uint32 Value)
	{
		Bytes.Add(static_cast<uint8>((Value >> 24) & 0xff));
		Bytes.Add(static_cast<uint8>((Value >> 16) & 0xff));
		Bytes.Add(static_cast<uint8>((Value >> 8) & 0xff));
		Bytes.Add(static_cast<uint8>(Value & 0xff));
	}

	uint32 ReadUInt32BigEndian(const uint8* Bytes)
	{
		return (static_cast<uint32>(Bytes[0]) << 24)
			| (static_cast<uint32>(Bytes[1]) << 16)
			| (static_cast<uint32>(Bytes[2]) << 8)
			| static_cast<uint32>(Bytes[3]);
	}

	FGuid ComputeCommandProtocolDigestImpl(
		const FGuid& SchemaDigest,
		const FString& AuthorityPolicyPath,
		int32 AuthorityPolicyRevision,
		int32 MaxCommandsPerSubmission)
	{
		TArray<uint8> CanonicalBytes;
		CanonicalBytes.Reserve(64 + AuthorityPolicyPath.Len());
		AppendUInt32BigEndian(CanonicalBytes, 6); // protocol + wire-layout format
		AppendUInt32BigEndian(CanonicalBytes, SchemaDigest.A);
		AppendUInt32BigEndian(CanonicalBytes, SchemaDigest.B);
		AppendUInt32BigEndian(CanonicalBytes, SchemaDigest.C);
		AppendUInt32BigEndian(CanonicalBytes, SchemaDigest.D);
		FTCHARToUTF8 PolicyUtf8(*AuthorityPolicyPath);
		AppendUInt32BigEndian(
			CanonicalBytes, static_cast<uint32>(PolicyUtf8.Length()));
		CanonicalBytes.Append(
			reinterpret_cast<const uint8*>(PolicyUtf8.Get()), PolicyUtf8.Length());
		AppendUInt32BigEndian(
			CanonicalBytes, static_cast<uint32>(AuthorityPolicyRevision));
		AppendUInt32BigEndian(CanonicalBytes, static_cast<uint32>(
			FMath::Clamp(
				MaxCommandsPerSubmission,
				1,
				SeinCommandProtocolLimits::MaxCommandsPerAuthor)));

		const FBlake3Hash Hash = FBlake3::HashBuffer(
			CanonicalBytes.GetData(), CanonicalBytes.Num());
		const uint8* HashBytes = Hash.GetBytes();
		return FGuid(
			ReadUInt32BigEndian(HashBytes),
			ReadUInt32BigEndian(HashBytes + 4),
			ReadUInt32BigEndian(HashBytes + 8),
			ReadUInt32BigEndian(HashBytes + 12));
	}

	FSeinDeterministicValueDigestOptions MakeRuntimeDigestOptions()
	{
		FSeinDeterministicValueDigestOptions Options;
#if !WITH_METADATA
		// Cooked UField metadata no longer contains SeinDeterministic. Concrete
		// match-extension paths are still framed into the digest, compared before
		// simulation, and admitted through the frozen command/schema boundary.
		Options.bTrustCookedTypesWithoutMetadata = true;
#endif
		return Options;
	}

	bool IsPairCapabilityTag(const FGameplayTag Tag)
	{
		return Tag.IsValid()
			&& Tag != SeinARTSTags::Relationship_Capability
			&& Tag.MatchesTag(SeinARTSTags::Relationship_Capability);
	}

	bool IsPairCapabilitySourceKindTag(const FGameplayTag Tag)
	{
		return Tag.IsValid()
			&& Tag != SeinARTSTags::Relationship_Source
			&& Tag.MatchesTag(SeinARTSTags::Relationship_Source);
	}

	const TCHAR* MatchBootstrapStateName(ESeinMatchBootstrapState State)
	{
		switch (State)
		{
		case ESeinMatchBootstrapState::Awaiting: return TEXT("Awaiting");
		case ESeinMatchBootstrapState::Applying: return TEXT("Applying");
		case ESeinMatchBootstrapState::LocallyReady: return TEXT("LocallyReady");
		case ESeinMatchBootstrapState::Authorized: return TEXT("Authorized");
		case ESeinMatchBootstrapState::Failed: return TEXT("Failed");
		case ESeinMatchBootstrapState::Consumed: return TEXT("Consumed");
		default: return TEXT("Invalid");
		}
	}

	bool CanonicalizeSystemStateContributorKey(
		FName RawKey,
		FString& OutCanonicalKey,
		FString& OutError)
	{
		OutCanonicalKey.Reset();
		OutError.Reset();
		if (RawKey.IsNone() || RawKey.GetNumber() != 0)
		{
			OutError =
				TEXT("Canonical-state contributor keys must be unnumbered names.");
			return false;
		}

		const FString Raw = RawKey.ToString();
		int32 Separator = INDEX_NONE;
		if (!Raw.FindChar(TEXT('/'), Separator)
			|| Separator <= 0
			|| Separator >= Raw.Len() - 1
			|| Raw.Find(
				TEXT("/"),
				ESearchCase::CaseSensitive,
				ESearchDir::FromEnd) != Separator)
		{
			OutError =
				TEXT("Expected exactly one stable-domain/stable-contributor separator.");
			return false;
		}

		FSeinCanonicalStateKey StructuredKey;
		StructuredKey.StableDomainId =
			FName(*Raw.Left(Separator));
		StructuredKey.StableContributorId =
			FName(*Raw.Mid(Separator + 1));
		OutCanonicalKey =
			FSeinCanonicalStateRegistry::CanonicalKey(StructuredKey);
		if (OutCanonicalKey.IsEmpty())
		{
			OutError =
				TEXT("Both key segments must be stable lowercase-compatible ASCII identifiers.");
			return false;
		}
		return true;
	}

	class FSeinStructOnScopeGCGuard final : public FGCObject
	{
	public:
		explicit FSeinStructOnScopeGCGuard(FStructOnScope& InScope)
			: Scope(InScope)
		{
		}

		virtual void AddReferencedObjects(
			FReferenceCollector& Collector) override
		{
			Scope.AddReferencedObjects(Collector);
		}

		virtual FString GetReferencerName() const override
		{
			return TEXT("SeinSnapshotStructOnScope");
		}

	private:
		FStructOnScope& Scope;
	};

	template<typename StructType>
	class TSeinStackStructGCGuard final : public FGCObject
	{
	public:
		explicit TSeinStackStructGCGuard(StructType& InValue)
			: Value(InValue)
		{
		}

		virtual void AddReferencedObjects(
			FReferenceCollector& Collector) override
		{
			Collector.AddPropertyReferencesWithStructARO(
				StructType::StaticStruct(), &Value);
		}

		virtual FString GetReferencerName() const override
		{
			return TEXT("SeinSnapshotStackStruct");
		}

	private:
		StructType& Value;
	};

}

FGuid SeinComputeCommandProtocolDigest(
	const FGuid& SchemaDigest,
	const FString& AuthorityPolicyPath,
	int32 AuthorityPolicyRevision,
	int32 MaxCommandsPerSubmission)
{
	return ComputeCommandProtocolDigestImpl(
		SchemaDigest,
		AuthorityPolicyPath,
		AuthorityPolicyRevision,
		MaxCommandsPerSubmission);
}

bool USeinWorldSubsystem::InitializeSimulationContent(
	const USeinARTSCoreSettings* Settings)
{
	ShutdownSimulationContent();
	if (!Settings || Settings->SimulationContentManifest.IsNull())
	{
		SimulationContentFailureReason =
			TEXT("No project-owned Simulation Content Manifest is configured. Generate one from Project Settings before starting a deterministic match.");
		UE_LOG(LogSeinSim, Error, TEXT("Simulation protocol disabled: %s"),
			*SimulationContentFailureReason);
		return false;
	}

	USeinSimulationContentManifest* Manifest =
		Settings->SimulationContentManifest.LoadSynchronous();
	if (!Manifest)
	{
		SimulationContentFailureReason = FString::Printf(
			TEXT("Configured Simulation Content Manifest '%s' could not be loaded."),
			*Settings->SimulationContentManifest.ToString());
		UE_LOG(LogSeinSim, Error, TEXT("Simulation protocol disabled: %s"),
			*SimulationContentFailureReason);
		return false;
	}

	FString Error;
	FSeinSimulationContentRegistrySnapshot RegistrySnapshot;
	TArray<FSeinSimulationContentContributorRecord> ActiveContributors;
	FSeinSimulationContentManifestProfile SelectedProfile;
	if (!FSeinSimulationContentRegistry::CaptureSnapshot(
			RegistrySnapshot, Error)
		|| !FSeinSimulationContentRegistry::BuildManifestContributorRecords(
			RegistrySnapshot, ActiveContributors, Error)
		|| !FSeinSimulationContentManifestCodec::SelectExactProfile(
			*Manifest,
			FSeinSimulationContentManifestCodec::CurrentBuilderRevision,
			ActiveContributors,
			SelectedProfile,
			Error))
	{
		SimulationContentFailureReason = Error.IsEmpty()
			? TEXT("The configured Simulation Content Manifest is invalid.")
			: MoveTemp(Error);
		UE_LOG(LogSeinSim, Error, TEXT("Simulation protocol disabled: %s"),
			*SimulationContentFailureReason);
		return false;
	}

	if (!SelectedProfile.RootDigest.IsValid())
	{
		SimulationContentFailureReason =
			TEXT("The selected Simulation Content profile has no valid root digest.");
		UE_LOG(LogSeinSim, Error, TEXT("Simulation protocol disabled: %s"),
			*SimulationContentFailureReason);
		return false;
	}

	SimulationContentManifestAsset = Manifest;
	SimulationContentProfile = MoveTemp(SelectedProfile);
	SimulationContentDigest = SimulationContentProfile.RootDigest;
	bSimulationContentReady = true;
	UE_LOG(LogSeinSim, Log,
		TEXT("Simulation content initialized (%d contributors, %d records, digest=%s)."),
		SimulationContentProfile.Contributors.Num(),
		SimulationContentProfile.Records.Num(),
		*SimulationContentDigest.ToString(EGuidFormats::Digits));
	return true;
}

void USeinWorldSubsystem::ShutdownSimulationContent()
{
	bSimulationContentReady = false;
	SimulationContentDigest.Invalidate();
	SimulationContentProfile = {};
	SimulationContentManifestAsset = nullptr;
	SimulationContentFailureReason.Reset();
}

bool USeinWorldSubsystem::IsCurrentWorldCoveredBySimulationContent(
	FString& OutError) const
{
	OutError.Reset();
	if (!IsSimulationContentReady())
	{
		OutError = SimulationContentFailureReason.IsEmpty()
			? TEXT("Simulation content is unavailable.")
			: SimulationContentFailureReason;
		return false;
	}
	const UWorld* World = GetWorld();
	const UPackage* WorldPackage = World ? World->GetOutermost() : nullptr;
	if (!WorldPackage)
	{
		OutError =
			TEXT("The simulation world has no package identity to verify.");
		return false;
	}

	const FString WorldPackageName =
		UWorld::RemovePIEPrefix(WorldPackage->GetName());
	for (const FSeinSimulationContentRecord& Record :
		SimulationContentProfile.Records)
	{
		if (Record.StableRecordKindId == TEXT("unreal.package")
			&& Record.CanonicalRecordId == WorldPackageName)
		{
			return true;
		}
	}

	OutError = FString::Printf(
		TEXT("World package '%s' is absent from the selected Simulation Content profile. Add it to Available Maps or Additional Simulation Content Roots, then regenerate the manifest."),
		*WorldPackageName);
	return false;
}

bool USeinWorldSubsystem::BuildAuthoritativeDestinationProviderBindingFrame(
	FString& OutFrame,
	FString& OutError) const
{
	OutFrame.Reset();
	OutError.Reset();
	if (AuthoritativeDestinationResolver.IsBound())
	{
		OutError =
			TEXT("The legacy authoritative-destination resolver cannot enter a deterministic match because it has no stable provider identity or behavior revision. Migrate it to RegisterAuthoritativeDestinationProvider.");
		return false;
	}
	if (AuthoritativeDestinationProviders.Num()
		> MaxAuthoritativeDestinationProviders)
	{
		OutError =
			TEXT("The authoritative-destination provider count exceeds the deterministic contract limit.");
		return false;
	}

	FString PreviousStableID;
	OutFrame = TEXT("SeinARTS.AuthoritativeDestinationProviders\n");
	AppendUtf8Framed(OutFrame, TEXT("1"));
	AppendUtf8Framed(
		OutFrame,
		FString::FromInt(AuthoritativeDestinationProviders.Num()));
	for (const FRegisteredAuthoritativeDestinationProvider& Provider :
		AuthoritativeDestinationProviders)
	{
		FString CanonicalStableID;
		FString StableIDError;
		if (Provider.RegistrationToken == 0
			|| Provider.BehaviorRevision == 0
			|| !Provider.Resolver.IsBound()
			|| !FSeinSimulationContentManifestCodec::CanonicalizeStableId(
				Provider.CanonicalStableID,
				CanonicalStableID,
				StableIDError)
			|| CanonicalStableID != Provider.CanonicalStableID
			|| (!PreviousStableID.IsEmpty()
				&& PreviousStableID.Compare(CanonicalStableID) >= 0))
		{
			OutError = FString::Printf(
				TEXT("Authoritative-destination provider '%s' has an invalid, unbound, duplicated, or non-canonical registration."),
				*Provider.CanonicalStableID);
			return false;
		}
		AppendUtf8Framed(OutFrame, CanonicalStableID);
		AppendUtf8Framed(
			OutFrame,
			LexToString(Provider.BehaviorRevision));
		PreviousStableID = MoveTemp(CanonicalStableID);
	}

	AppendUtf8Framed(OutFrame, TEXT("legacy-unbound"));
	return true;
}

bool USeinWorldSubsystem::BuildLocallyDeclaredCanonicalState(
	const FSeinMatchSettings& MatchSettings,
	bool bMaterializeInitialValues,
	const FString& TopologyManifest,
	FSeinCanonicalStateValueStore& OutStore,
	TArray<FString>& OutWorldBindingFrames,
	FString& OutError)
{
	OutError.Reset();
	OutWorldBindingFrames.Reset();
	if (!bExecutionTopologyValid || TopologyManifest.IsEmpty())
	{
		OutError = ExecutionTopologyFailureReason.IsEmpty()
			? TEXT("The deterministic execution topology contract is unavailable.")
			: ExecutionTopologyFailureReason;
		return false;
	}
	if (!NativeCanonicalStateSchema.IsValid())
	{
		OutError =
			TEXT("The native canonical-state schema is unavailable.");
		return false;
	}
	if (!FModuleManager::GetModuleChecked<FSeinARTSCoreEntity>(
			TEXT("SeinARTSCoreEntity"))
			.ValidateConfiguredCanonicalStateRecipes(OutError))
	{
		return false;
	}

	const ESeinCanonicalStateWorldBindingDisposition BindingDisposition =
		bMaterializeInitialValues
			? ESeinCanonicalStateWorldBindingDisposition::BootstrapCommit
			: ESeinCanonicalStateWorldBindingDisposition::Provisional;
	{
		// Preparation may load provider-local immutable data, but it receives
		// the world-services surface while bootstrap materialization is active.
		// Keep every authoritative Core mutation gate closed for the callback.
		TGuardValue<bool> PreparationReadOnlyGuard(
			bReadOnlyCallbackInProgress, true);
		TGuardValue<bool> PreparationObserverGuard(
			bObserverCallbackInProgress, true);
		if (!FSeinCanonicalStateRegistry::PrepareWorldBindings(
			NativeCanonicalStateSchema,
			{*this, BindingDisposition},
			OutError))
		{
			return false;
		}
	}

	// Recipes are passive schema/value composers. Even during Applying they do
	// not inherit the gameplay materializer's mutation or command capability.
	TGuardValue<bool> ReadOnlyGuard(
		bReadOnlyCallbackInProgress, true);
	TGuardValue<bool> ObserverGuard(
		bObserverCallbackInProgress, true);

	FSeinCanonicalStateRecipeSnapshot RecipeSnapshot =
		FSeinCanonicalStateRecipeRegistry::Freeze(&OutError);
	if (!RecipeSnapshot.IsValid()
		|| !RecipeSnapshot.GetContractDigest().IsValid())
	{
		if (OutError.IsEmpty())
		{
			OutError =
				TEXT("The canonical-state recipe registry could not freeze.");
		}
		return false;
	}

	// Project Settings is the Blueprint composition surface. Prove that every
	// configured class actually reached the process registry; a load or
	// registration failure must not silently degrade to a smaller state model.
	const USeinARTSCoreSettings* CoreSettings =
		GetDefault<USeinARTSCoreSettings>();
	TSet<FString> FrozenRecipePaths;
	for (const FSeinCanonicalStateRecipeDescriptor& Recipe :
		RecipeSnapshot.GetRecipes())
	{
		FrozenRecipePaths.Add(Recipe.RecipeClassPath);
	}
	TSet<FString> ConfiguredRecipePaths;
	if (CoreSettings)
	{
		for (const TSoftClassPtr<USeinCanonicalStateRecipe>& Recipe :
			CoreSettings->CanonicalStateRecipes)
		{
			const FString RecipePath =
				Recipe.ToSoftObjectPath().ToString();
			if (RecipePath.IsEmpty()
				|| ConfiguredRecipePaths.Contains(RecipePath)
				|| !FrozenRecipePaths.Contains(RecipePath))
			{
				OutError = FString::Printf(
					TEXT("Configured canonical-state recipe '%s' is null, duplicated, or not registered."),
					*RecipePath);
				return false;
			}
			ConfiguredRecipePaths.Add(RecipePath);
		}
	}

	TArray<FSeinCanonicalStateRecipeDeclaration> Declarations;
	if (!FSeinCanonicalStateRecipeRegistry::DeclareFrozenRecipes(
		RecipeSnapshot,
		MatchSettings,
		Declarations,
		OutError))
	{
		return false;
	}

	TArray<FSeinCanonicalStateRecipeMaterialization> Materializations;
	if (bMaterializeInitialValues
		&& !FSeinCanonicalStateRecipeRegistry::MaterializeFrozenRecipes(
			RecipeSnapshot,
			MatchSettings,
			Declarations,
			Materializations,
			OutError))
	{
		return false;
	}
	if (bMaterializeInitialValues
		&& Materializations.Num() != Declarations.Num())
	{
		OutError =
			TEXT("Canonical-state recipe materialization changed the frozen recipe count.");
		return false;
	}

	FSeinCanonicalStateValueStore Candidate;
	for (int32 RecipeIndex = 0;
		RecipeIndex < Declarations.Num();
		++RecipeIndex)
	{
		const FSeinCanonicalStateRecipeDeclaration& Declaration =
			Declarations[RecipeIndex];
		const FSeinCanonicalStateRecipeMaterialization* Materialization =
			bMaterializeInitialValues
				? &Materializations[RecipeIndex]
				: nullptr;
		if (Materialization
			&& Materialization->Values.Num()
				!= Declaration.Slots.Num())
		{
			OutError =
				TEXT("Canonical-state recipe materialization changed its declared slot count.");
			return false;
		}

		for (int32 SlotIndex = 0;
			SlotIndex < Declaration.Slots.Num();
			++SlotIndex)
		{
			const FSeinCanonicalStateRecipeSlotDeclaration& Slot =
				Declaration.Slots[SlotIndex];
			const FInstancedStruct& InitialValue = Materialization
				? Materialization->Values[SlotIndex].Value
				: Slot.DefaultValue;
			if (!Candidate.RegisterSlot(
				NativeCanonicalStateSchema,
				Slot.Definition,
				InitialValue,
				OutError))
			{
				return false;
			}
		}
	}

	TArray<FString> AdditionalContractFrames;
	AdditionalContractFrames.Add(
		TEXT("SeinARTS.CanonicalState.RecipeBinding\n")
		+ RecipeSnapshot.GetCanonicalManifest());
	AdditionalContractFrames.Add(TopologyManifest);
	if (!LatentActionCodecManifest.IsValid()
		|| !LatentActionCodecManifest.GetDigest().IsValid())
	{
		OutError =
			TEXT("The latent-action codec manifest is unavailable.");
		return false;
	}
	AdditionalContractFrames.Add(
		LatentActionCodecManifest.GetCanonicalManifest());
	if (!PoolObjectCodecManifest.IsValid()
		|| !PoolObjectCodecManifest.GetDigest().IsValid())
	{
		OutError =
			TEXT("The pool-object codec manifest is unavailable.");
		return false;
	}
	AdditionalContractFrames.Add(
		PoolObjectCodecManifest.GetCanonicalManifest());
	TArray<FString> NativeWorldBindingFrames;
	if (!FSeinCanonicalStateRegistry::CaptureWorldBindingFrames(
		NativeCanonicalStateSchema,
		{*this, BindingDisposition},
		NativeWorldBindingFrames,
		OutError))
	{
		return false;
	}
	FString AuthoritativeDestinationProviderFrame;
	if (!BuildAuthoritativeDestinationProviderBindingFrame(
			AuthoritativeDestinationProviderFrame,
			OutError))
	{
		return false;
	}
	NativeWorldBindingFrames.Add(
		MoveTemp(AuthoritativeDestinationProviderFrame));
	AdditionalContractFrames.Append(NativeWorldBindingFrames);
	if (!Candidate.Seal(
		NativeCanonicalStateSchema,
		AdditionalContractFrames,
		OutError))
	{
		return false;
	}

	OutStore = MoveTemp(Candidate);
	OutWorldBindingFrames = MoveTemp(NativeWorldBindingFrames);
	return true;
}

void USeinWorldSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	EntityPool.Initialize(1024);
	CurrentTick = 0;
	SimSessionSeed = 0;
	bSimSessionSeedInstalled = false;
	NextEffectInstanceID = 1;
	NextAbilityActivationID = 1;
	TimeAccumulator = 0.0f;
	Systems.Reset();
	AuthoritativeDestinationProviders.Reset();
	NextAuthoritativeDestinationProviderToken = 1;
	bAuthoritativeDestinationQueryInProgress = false;
	ExecutionTopologyManifest.Reset();
	ExecutionTopologyFailureReason.Reset();
	ExecutionTopologyDigest.Invalidate();
	bExecutionTopologyFrozen = false;
	bExecutionTopologyValid = true;
	bExecutionTopologyTeardown = false;
	bModuleUnloadStateReleased = false;
	MatchBootstrapState = ESeinMatchBootstrapState::Awaiting;
	MatchBootstrapReceipt = FSeinMatchBootstrapReceipt();
	MatchBootstrapAuthorizationContextDigest.Invalidate();
	MatchBootstrapFailureReason.Reset();
	MatchBootstrapAuthorityID = NAME_None;
	MatchBootstrapAuthorityToken.Invalidate();
	MatchBootstrapAuthorityOwner.Reset();
	ClearSnapshotRestoreAuthority();
	bMatchBootstrapMaterializerInvocationActive = false;
	MatchBootstrapNativeContributors.Reset();
	MatchBootstrapValueContributions.Reset();
	NativeCanonicalStateSchema = FSeinCanonicalStateSchemaSnapshot();
	LatentActionCodecManifest = FSeinLatentActionCodecManifest();
	PoolObjectCodecManifest = FSeinPoolObjectCodecManifest();
	CanonicalStateValues.Reset();
	FrozenCanonicalStateWorldBindingFrames.Reset();
	bMatchBootstrapClosedBroadcast = false;
	bSnapshotRestoreMutationAuthorized = false;
	bSnapshotCaptureInProgress = false;
	bSnapshotRestoreInProgress = false;
	bResyncCatchUpInProgress = false;
	bReadOnlyCallbackInProgress = false;
	bObserverCallbackInProgress = false;
	ActiveAICommandEmitter = nullptr;
	FormationExecutionScratch.Reset();
	ActiveFormationExecutionScratch.Reset();
	bDestroyNotificationInProgress = false;
	DeferredTeardownHandle = FSeinEntityHandle::Invalid();
	bSimulationTickDispatchInProgress = false;
	bSimSessionSeedInstalled = false;
	bIsRunning = false;
	bSimulationSchedulerReserved = false;
	bSimPaused = false;
	bSimPausedHard = false;
	PauseEpoch = 0;
	PauseFrozenTick = INDEX_NONE;
	LastAppliedPauseControlSequence = -1;
	PendingStandalonePauseControlCommands.Reset();
	bReplayOwnsExternalCommandIngress = false;
	MatchState = ESeinMatchState::Lobby;
	CurrentMatchSettings = FSeinMatchSettings();
	MatchSettingsDigest.Invalidate();
	SimulationTraceScopeName = FString::Printf(
		TEXT("Sein TickSimulation [%s]"), *GetPathNameSafe(GetWorld()));

	// Create latent action manager
	LatentActionManager = NewObject<USeinLatentActionManager>(this);

	// Read settings
	const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>();
	ConfigFingerprint = Settings ? Settings->ComputeConfigFingerprint() : 0;
	FixedDeltaTimeSeconds = 1.0f / static_cast<float>(Settings->SimulationTickRate);
	FSeinDeterministicValueDigestError EmptyMatchDigestError;
	const bool bMatchDigestReady = SeinCanonicalizeAndDigestMatchSettings(
		CurrentMatchSettings, MatchSettingsDigest, &EmptyMatchDigestError);
	if (!bMatchDigestReady)
	{
		UE_LOG(LogSeinSim, Error,
			TEXT("Simulation protocol disabled: default match settings cannot be digested (%s: %s)."),
			*EmptyMatchDigestError.FieldPath, *EmptyMatchDigestError.Message);
	}
	FString StateSchemaError;
	NativeCanonicalStateSchema =
		FSeinCanonicalStateRegistry::CaptureSchemaSnapshot(
			&StateSchemaError);
	const bool bNativeStateSchemaReady =
		NativeCanonicalStateSchema.IsValid()
		&& NativeCanonicalStateSchema.GetContractDigest().IsValid();
	if (!bNativeStateSchemaReady)
	{
		UE_LOG(LogSeinSim, Error,
			TEXT("Simulation protocol disabled: canonical state schema could not freeze (%s)."),
			*StateSchemaError);
	}
	FString LatentCodecError;
	LatentActionCodecManifest =
		FSeinLatentActionCodecRegistry::CaptureManifest(
			NativeCanonicalStateSchema,
			&LatentCodecError);
	const bool bLatentCodecManifestReady =
		LatentActionCodecManifest.IsValid()
		&& LatentActionCodecManifest.GetDigest().IsValid()
		&& FModuleManager::GetModuleChecked<FSeinARTSCoreEntity>(
			TEXT("SeinARTSCoreEntity")).IsWaitActionCodecReady();
	if (!bLatentCodecManifestReady)
	{
		UE_LOG(LogSeinSim, Error,
			TEXT("Simulation protocol disabled: latent-action codec manifest could not freeze (%s)."),
			*LatentCodecError);
	}
	const bool bConfiguredRecipesReady =
		FModuleManager::GetModuleChecked<FSeinARTSCoreEntity>(
			TEXT("SeinARTSCoreEntity"))
		.AreConfiguredCanonicalStateRecipesReady();
	if (!bConfiguredRecipesReady)
	{
		UE_LOG(LogSeinSim, Error,
			TEXT("Simulation protocol disabled: configured canonical-state recipe registration failed during CoreEntity module startup."));
	}
	const bool bSimulationContentReadyForWorld =
		InitializeSimulationContent(Settings);
	FString PoolCodecError;
	if (bSimulationContentReadyForWorld)
	{
		PoolObjectCodecManifest =
			FSeinPoolObjectCodecRegistry::CaptureManifest(
				SimulationContentProfile,
				&PoolCodecError);
	}
	const bool bPoolCodecManifestReady =
		PoolObjectCodecManifest.IsValid()
		&& PoolObjectCodecManifest.GetDigest().IsValid()
		&& FModuleManager::GetModuleChecked<FSeinARTSCoreEntity>(
			TEXT("SeinARTSCoreEntity")).ArePoolObjectCodecsReady();
	if (!bPoolCodecManifestReady)
	{
		UE_LOG(LogSeinSim, Error,
			TEXT("Simulation protocol disabled: pool-object codec manifest could not freeze (%s)."),
			*PoolCodecError);
	}
	const bool bStateSchemaReady =
		bNativeStateSchemaReady
		&& bConfiguredRecipesReady
		&& bLatentCodecManifestReady
		&& bPoolCodecManifestReady;
	bCommandProtocolReady = bMatchDigestReady
		&& bStateSchemaReady
		&& bSimulationContentReadyForWorld
		&& InitializeCommandProtocol();
	if (!bCommandProtocolReady)
	{
		// Each failed leg already logged its own detailed error above; this is
		// the on-screen shout so the disabled protocol can't hide in the log.
		TArray<FString> DisabledReasons;
		if (!bSimulationContentReadyForWorld)
		{
			DisabledReasons.Add(SimulationContentFailureReason);
		}
		if (!bMatchDigestReady)
		{
			DisabledReasons.Add(TEXT("default match settings cannot be digested"));
		}
		if (!bNativeStateSchemaReady)
		{
			DisabledReasons.Add(TEXT("canonical state schema could not freeze"));
		}
		if (!bLatentCodecManifestReady)
		{
			DisabledReasons.Add(TEXT("latent-action codec manifest could not freeze"));
		}
		if (!bConfiguredRecipesReady)
		{
			DisabledReasons.Add(TEXT("canonical-state recipe registration failed"));
		}
		if (!bPoolCodecManifestReady)
		{
			DisabledReasons.Add(TEXT("pool-object codec manifest could not freeze"));
		}
		if (DisabledReasons.IsEmpty())
		{
			DisabledReasons.Add(TEXT("command protocol initialization failed (see LogSeinSim)"));
		}
		ShowSimulationErrorOnScreen(FString::Printf(
			TEXT("Simulation protocol disabled: %s"),
			*FString::Join(DisabledReasons, TEXT(" | "))));
	}

	// Collision broadphase — cell size 200 cm balances bucket fan-out cost
	// against query precision. Origin = world (0,0,0); levels offset from origin
	// just produce sparse buckets at high indices, no correctness impact.
	CollisionSpatialHash.Initialize(FFixedPoint::FromInt(200), FFixedVector::ZeroVector);

	// A collider spawning/dying changes the static set; flag the broadphase's
	// static tier for rebuild (cheap bool; the broadphase system rebuilds it on
	// the next PreTick if set). These delegates are multicast, so this composes
	// with other spawn/destroy listeners (e.g. extension subsystems).
	OnEntitySpawned.AddLambda([this](FSeinEntityHandle) { CollisionSpatialHash.MarkStaticDirty(); });
	OnEntityDestroyed.AddLambda([this](FSeinEntityHandle) { CollisionSpatialHash.MarkStaticDirty(); });

	// Register built-in systems
	BuiltInSystems.Add(new FSeinEffectTickSystem());
	BuiltInSystems.Add(new FSeinCollisionBroadphaseSystem());
	BuiltInSystems.Add(new FSeinCooldownSystem());
	BuiltInSystems.Add(new FSeinAbilityTickSystem());
	BuiltInSystems.Add(new FSeinProductionSystem());
	BuiltInSystems.Add(new FSeinLifespanSystem());
	BuiltInSystems.Add(new FSeinCommandBrokerSystem());
	BuiltInSystems.Add(new FSeinCollisionResolutionSystem());

	for (ISeinSystem* Sys : BuiltInSystems)
	{
		RegisterSystem(Sys);
	}

	// Instantiate the pluggable collision resolver. The PostTick
	// FSeinCollisionResolutionSystem delegates its per-tick work to this object.
	// Resolve the configured class, falling back to the shipped Gauss-Seidel
	// default if the setting is empty or points to a stale / abstract class —
	// EXACTLY mirroring USeinNavigationSubsystem::Initialize's NavigationClass path.
	{
		// WYSIWYG. None/empty => collision resolution is intentionally OFF (CollisionResolver stays
		// null; solid bodies overlap freely and overlap events stop firing). A set-but-unloadable/
		// abstract class is a mistake, not an off-switch: it falls back to the shipped Gauss-Seidel
		// default with a logged error. The one consumer (FSeinCollisionResolutionSystem) null-guards.
		UClass* ResolverClass = nullptr;
		if (Settings && !Settings->CollisionResolverClass.IsNull())
		{
			ResolverClass = Settings->CollisionResolverClass.TryLoadClass<USeinCollisionResolver>();
			if (!ResolverClass || ResolverClass->HasAnyClassFlags(CLASS_Abstract))
			{
				UE_LOG(LogSeinSim, Error,
					TEXT("CollisionResolverClass '%s' could not be loaded or is abstract — falling back to the shipped default."),
					*Settings->CollisionResolverClass.ToString());
				ResolverClass = USeinCollisionResolverDefault::StaticClass();
			}
		}

		if (ResolverClass)
		{
			CollisionResolver = NewObject<USeinCollisionResolver>(this, ResolverClass, TEXT("SeinCollisionResolver"));
			if (CollisionResolver)
			{
				CollisionResolver->OnInitialized(GetWorld());
			}
			else
			{
				UE_LOG(LogSeinSim, Error, TEXT("Failed to instantiate collision resolver class %s"),
					*ResolverClass->GetName());
			}
		}
		else
		{
			USeinARTSCoreSettings::ReportDisabledSystem(TEXT("Collision"),
				TEXT("Solid bodies overlap freely and overlap events stop firing."), /*bHighSeverity*/ false);
		}
	}

	// Auto-register the Neutral player (ID 0). Entities that logically have "no
	// player" (neutral capture points, resource piles, scenario owners, environmental
	// hazards) resolve to this sentinel. See §1 Entities, "Ownership" decision.
	RegisterPlayer(FSeinPlayerID::Neutral(), FSeinFactionID(), /*TeamID=*/0);

	UE_LOG(LogSeinSim, Log, TEXT("SeinWorldSubsystem initialized (tick rate: %d Hz, %d systems)"),
		Settings->SimulationTickRate, Systems.Num());
}

void USeinWorldSubsystem::PreDeinitialize()
{
	// UWorld invokes PreDeinitialize on every subsystem before the arbitrarily
	// ordered Deinitialize pass. Extension hosts may now unregister without
	// looking like a live topology mutation. Module/hot reload does not enter
	// this window and therefore remains fail-closed.
	bExecutionTopologyTeardown = true;
	StopSimulation();
	ReleaseSimulationScheduler();
	Super::PreDeinitialize();
}

void USeinWorldSubsystem::Deinitialize()
{
	ReleaseAllModuleOwnedState();
	FormationExecutionScratch.Reset();
	ActiveFormationExecutionScratch.Reset();
	MatchBootstrapState = ESeinMatchBootstrapState::Awaiting;
	MatchBootstrapReceipt = FSeinMatchBootstrapReceipt();
	MatchBootstrapAuthorizationContextDigest.Invalidate();
	MatchBootstrapFailureReason.Reset();
	MatchBootstrapAuthorityID = NAME_None;
	MatchBootstrapAuthorityToken.Invalidate();
	MatchBootstrapAuthorityOwner.Reset();
	ClearSnapshotRestoreAuthority();
	bMatchBootstrapClosedBroadcast = false;
	ExecutionTopologyManifest.Reset();
	ExecutionTopologyFailureReason.Reset();
	ExecutionTopologyDigest.Invalidate();
	bExecutionTopologyFrozen = false;
	bExecutionTopologyValid = true;
	bModuleUnloadStateReleased = false;

	Super::Deinitialize();

	UE_LOG(LogSeinSim, Log, TEXT("SeinWorldSubsystem deinitialized"));
}

void USeinWorldSubsystem::ReleaseAllModuleOwnedState()
{
	bIsRunning = false;
	ReleaseSimulationScheduler();

	MatchBootstrapMaterializer.Unbind();
	StandaloneBootstrapLauncher.Unbind();
	ReplayCommandBoundaryNotifier.Clear();
	ClearAIEmitInterceptor();
	ClearLocalCommandSubmitter();

	OnSimTickCompleted.Clear();
	OnSimFrameCompleted.Clear();
	OnEntitySpawned.Clear();
	OnEntityDestroyed.Clear();
	OnCommandsProcessing.Clear();
	OnBrokerOrderDispatched.Clear();
	PathableTargetResolver.Unbind();
	LineOfSightResolver.Unbind();
	FootprintPlacementResolver.Unbind();
	PassableResolver.Unbind();
	DynamicPassableResolver.Unbind();
	AgentDynamicPassableResolver.Unbind();
	AgentDynamicPassableIgnoringResolver.Unbind();
	NavProjectResolver.Unbind();
	NavProjectFreeResolver.Unbind();
	NavProjectAgentFreeResolver.Unbind();
	NavProjectAgentFreeIgnoringResolver.Unbind();
	AuthoritativeDestinationProviders.Reset();
	NextAuthoritativeDestinationProviderToken = 1;
	bAuthoritativeDestinationQueryInProgress = false;
	AuthoritativeDestinationResolver.Unbind();
	AgentAuthoritativeDestinationSafetyResolver.Unbind();
	PreviewQualityProvider.Unbind();
	HeightResolver.Unbind();
	SpatialGridRegisterCallback.Unbind();
	SpatialGridUnregisterCallback.Unbind();
	TurnReadyResolver.Unbind();
	TurnConsumeNotifier.Unbind();
	PauseControlFrameResolver.Unbind();
	PauseControlAppliedNotifier.Unbind();
	OnCaptureSnapshotPostSim.Clear();
	OnRestoreSnapshotPostSim.Clear();
	OnAuthoritativeStateRestored.Clear();

	if (LatentActionManager)
	{
		LatentActionManager->AbandonAllForSnapshotRestore();
		LatentActionManager = nullptr;
	}
	TArray<TObjectPtr<USeinAIController>> ControllersToRelease =
		MoveTemp(AIControllers);
	AIControllers.Reset();
	for (USeinAIController* Controller : ControllersToRelease)
	{
		if (Controller && Controller->WorldSubsystem == this)
		{
			TGuardValue<bool> ReadOnlyGuard(bReadOnlyCallbackInProgress, true);
			TGuardValue<bool> ObserverGuard(bObserverCallbackInProgress, true);
			Controller->OnUnregistered();
			Controller->WorldSubsystem = nullptr;
		}
	}
	ActiveAICommandEmitter = nullptr;

	ShutdownCommandProtocol();
	ShutdownSimulationContent();

	MatchBootstrapAuthorityOwner.Reset();
	MatchBootstrapAuthorityID = NAME_None;
	MatchBootstrapAuthorityToken.Invalidate();
	ClearSnapshotRestoreAuthority();
	bMatchBootstrapMaterializerInvocationActive = false;
	MatchBootstrapNativeContributors.Reset();
	MatchBootstrapValueContributions.Reset();
	NativeCanonicalStateSchema = FSeinCanonicalStateSchemaSnapshot();
	LatentActionCodecManifest = FSeinLatentActionCodecManifest();
	PoolObjectCodecManifest = FSeinPoolObjectCodecManifest();
	CanonicalStateValues.Reset();
	FrozenCanonicalStateWorldBindingFrames.Reset();
	CurrentMatchSettings = FSeinMatchSettings();
	MatchSettingsDigest.Invalidate();

	PendingCommands.Clear();
	PendingReplayCommands.Clear();
	PendingStandalonePauseControlCommands.Reset();
	bReplayOwnsExternalCommandIngress = false;
	PendingDestroy.Reset();
	PendingEffectApplies.Reset();
	ActiveVotes.Reset();
	VisualEventQueue.Events.Reset();

	for (USeinAbility* Ability : AbilityPool)
	{
		if (Ability)
		{
			if (Ability->WorldSubsystem == this)
			{
				Ability->WorldSubsystem = nullptr;
			}
			Ability->RuntimePoolID = INDEX_NONE;
		}
	}
	AbilityPool.Reset();
	AbilityPoolFreeList.Reset();
	AbilityPoolStateRevisions.Reset();
	AbilityPoolMutationRevision = 0;
	AbilityPoolTopologyRevision = 1;
	CommandBrokerResolverPool.Reset();
	CommandBrokerResolverPoolFreeList.Reset();
	CommandBrokerResolverPoolStateRevisions.Reset();
	CommandBrokerResolverPoolMutationRevision = 0;
	CommandBrokerResolverPoolTopologyRevision = 1;
	CanonicalStateRootCache.Reset();
	if (CollisionResolver)
	{
		TGuardValue<bool> ReadOnlyGuard(bReadOnlyCallbackInProgress, true);
		TGuardValue<bool> ObserverGuard(bObserverCallbackInProgress, true);
		CollisionResolver->OnDeinitialized();
	}
	CollisionResolver = nullptr;
	PlayerStates.Reset();
	PairCapabilitySourceRefCounts.Reset();
	PairCapabilityEffectiveRefCounts.Reset();
	Factions.Reset();
	EntityActorClassMap.Reset();

	for (auto& Pair : ComponentStorages)
	{
		delete Pair.Value;
	}
	ComponentStorages.Reset();
	ComponentStorageSnapshotCache.Reset();
	ComponentStorageSnapshotCacheBytes = 0;
#if WITH_DEV_AUTOMATION_TESTS
	ComponentStorageSnapshotCacheHitCount = 0;
	ComponentStorageSnapshotCacheMissCount = 0;
	ComponentStorageSnapshotCacheBudgetForTests =
		MaxComponentStorageSnapshotCacheBytes;
#endif

	Systems.Reset();
	for (ISeinSystem* System : BuiltInSystems)
	{
		delete System;
	}
	BuiltInSystems.Reset();

	EntityPool.Reset();
	CollisionSpatialHash.ClearStatic();
	CollisionSpatialHash.ClearDynamic();
	EntityTagStates.Reset();
	EntityTagIndex.Reset();
	NamedEntityRegistry.Reset();
	OwnerTransitionRevisions.Reset();
	OwnerTransitionDepth = 0;
	DeferredTeardownHandle = FSeinEntityHandle::Invalid();
	bDestroyNotificationInProgress = false;

	bSnapshotRestoreMutationAuthorized = false;
	bSnapshotCaptureInProgress = false;
	bSnapshotRestoreInProgress = false;
	bResyncCatchUpInProgress = false;
	bReadOnlyCallbackInProgress = false;
	bObserverCallbackInProgress = false;
	bSimulationTickDispatchInProgress = false;
	bSimulationSchedulerReserved = false;
	bSimPaused = false;
	bSimPausedHard = false;
	PauseEpoch = 0;
	PauseFrozenTick = INDEX_NONE;
	LastAppliedPauseControlSequence = -1;
	bDispatchingPauseControlFrame = false;
	bPauseControlDispatchProtocolViolation = false;
	ActivePauseControlCommandIndex = INDEX_NONE;
	ActivePauseControlCommandCount = 0;
	TimeAccumulator = 0.0f;
	CurrentTick = 0;
	SimSessionSeed = 0;
	bSimSessionSeedInstalled = false;
	NextEffectInstanceID = 1;
	NextAbilityActivationID = 1;
	MatchState = ESeinMatchState::Lobby;
	StartingStateDeadlineTick = 0;
	MatchStartTick = 0;
	CommandCohesionOrderSequence = 0;

	FSeinAttributeResolver::ClearPropertyCache();
	OnMatchBootstrapClosed.Clear();
	OnExecutionTopologyInvalidated.Clear();
}

void USeinWorldSubsystem::AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector)
{
	Super::AddReferencedObjects(InThis, Collector);

	USeinWorldSubsystem* Self = CastChecked<USeinWorldSubsystem>(InThis);
	for (auto& Pair : Self->ComponentStorages)
	{
		if (ISeinComponentStorage* Storage = Pair.Value)
		{
			Storage->CollectReferences(Collector, Self);
		}
	}
	for (FSeinPendingEffectApply& Pending : Self->PendingEffectApplies)
	{
		Collector.AddReferencedObject(
			Pending.EffectClass.GetGCPtr(), Self);
	}
	Self->CanonicalStateValues.AddReferencedObjects(Collector);
}

bool USeinWorldSubsystem::InitializeCommandProtocol()
{
	ShutdownCommandProtocol();
	if (!FModuleManager::GetModuleChecked<FSeinARTSCoreEntity>(
		TEXT("SeinARTSCoreEntity")).AreBuiltInCommandSchemasReady())
	{
		UE_LOG(LogSeinSim, Error,
			TEXT("Command protocol disabled: built-in schema registration failed."));
		return false;
	}

	const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>();
	if (!Settings || Settings->CommandAuthorityPolicyClass.IsNull())
	{
		UE_LOG(LogSeinSim, Error,
			TEXT("Command protocol disabled: Authority Policy Class is None."));
		return false;
	}

	UClass* PolicyClass =
		Settings->CommandAuthorityPolicyClass.TryLoadClass<USeinCommandAuthorityPolicy>();
	if (!PolicyClass
		|| PolicyClass->HasAnyClassFlags(
			CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists)
		|| !PolicyClass->HasAnyClassFlags(CLASS_Const))
	{
		UE_LOG(LogSeinSim, Error,
			TEXT("Command protocol disabled: authority policy '%s' is missing, abstract, deprecated, or mutable."),
			*Settings->CommandAuthorityPolicyClass.ToString());
		return false;
	}

	CommandAuthorityPolicy = Cast<USeinCommandAuthorityPolicy>(PolicyClass->GetDefaultObject());
	if (!CommandAuthorityPolicy || CommandAuthorityPolicy->ImplementationRevision <= 0)
	{
		UE_LOG(LogSeinSim, Error,
			TEXT("Command protocol disabled: authority policy '%s' has no valid CDO or positive implementation revision."),
			*PolicyClass->GetPathName());
		return false;
	}
	CommandAuthorityView = NewObject<USeinCommandAuthorityView>(this);
	if (!CommandAuthorityView)
	{
		UE_LOG(LogSeinSim, Error,
			TEXT("Command protocol disabled: failed to allocate authority view."));
		ShutdownCommandProtocol();
		return false;
	}
	CommandAuthorityView->Initialize(this);

	static const FName SettingsOwnerId(TEXT("SeinARTS.ProjectSettings.Commands"));
	for (const FSoftClassPath& HandlerPath : Settings->CommandHandlerClasses)
	{
		UClass* HandlerClass = HandlerPath.IsNull()
			? nullptr
			: HandlerPath.TryLoadClass<USeinCommandHandler>();
		if (!HandlerClass)
		{
			UE_LOG(LogSeinSim, Error,
				TEXT("Command protocol disabled: configured handler '%s' could not be loaded."),
				*HandlerPath.ToString());
			ShutdownCommandProtocol();
			return false;
		}

		FSeinCommandSchemaRegistrationHandle Handle =
			FSeinCommandSchemaRegistry::RegisterHandlerClass(SettingsOwnerId, HandlerClass);
		if (!Handle.IsValid())
		{
			UE_LOG(LogSeinSim, Error,
				TEXT("Command protocol disabled: configured handler '%s' has an invalid or conflicting schema."),
				*HandlerClass->GetPathName());
			ShutdownCommandProtocol();
			return false;
		}
		ConfiguredCommandSchemaHandles.Add(MoveTemp(Handle));
	}

	TArray<const UScriptStruct*> AdditionalDynamicPayloadStructs;
	auto AppendDynamicTypes = [&AdditionalDynamicPayloadStructs](
		const TArray<FInstancedStruct>& Schemas)
	{
		for (const FInstancedStruct& Schema : Schemas)
		{
			if (Schema.IsValid())
			{
				AdditionalDynamicPayloadStructs.AddUnique(Schema.GetScriptStruct());
			}
		}
	};
	AppendDynamicTypes(Settings->CommandDynamicPayloadSchemas);
	AppendDynamicTypes(Settings->DefaultMatchExtensions);
	// This framework-owned optional extension must remain decodable even when
	// the project defaults omit it and a lobby supplies it per match.
	AdditionalDynamicPayloadStructs.AddUnique(
		FSeinMatchBootstrapRules::StaticStruct());
	CommandSchemaSnapshot = FSeinCommandSchemaRegistry::CaptureSnapshot(
		AdditionalDynamicPayloadStructs,
		Settings->AdditionalWireNames);
	if (!CommandSchemaSnapshot.IsValid()
		|| CommandSchemaSnapshot.GetSchemaCount() == 0)
	{
		UE_LOG(LogSeinSim, Error,
			TEXT("Command protocol disabled: failed to freeze the schema registry."));
		ShutdownCommandProtocol();
		return false;
	}
	CommandProtocolMaxCommandsPerSubmission = FMath::Clamp(
		Settings->MaxCommandsPerSubmission,
		1,
		SeinCommandProtocolLimits::MaxCommandsPerAuthor);
	CommandProtocolDigest = SeinComputeCommandProtocolDigest(
		CommandSchemaSnapshot.GetCanonicalManifestDigest(),
		PolicyClass->GetPathName(),
		CommandAuthorityPolicy->ImplementationRevision,
		CommandProtocolMaxCommandsPerSubmission);

	bCommandProtocolReady = true;
	UE_LOG(LogSeinSim, Log,
		TEXT("Command protocol initialized (%d frozen schemas, %d configured handlers, policy=%s, max-commands=%d, digest=%s)."),
		CommandSchemaSnapshot.GetSchemaCount(),
		ConfiguredCommandSchemaHandles.Num(), *PolicyClass->GetPathName(),
		CommandProtocolMaxCommandsPerSubmission,
		*CommandProtocolDigest.ToString(EGuidFormats::Digits));
	return true;
}

void USeinWorldSubsystem::ShutdownCommandProtocol()
{
	for (int32 Index = ConfiguredCommandSchemaHandles.Num() - 1; Index >= 0; --Index)
	{
		FSeinCommandSchemaRegistry::UnregisterSchema(
			ConfiguredCommandSchemaHandles[Index]);
	}
	ConfiguredCommandSchemaHandles.Reset();
	CommandSchemaSnapshot = FSeinCommandSchemaSnapshot();
	CommandProtocolDigest.Invalidate();
	CommandProtocolMaxCommandsPerSubmission = 0;
	if (CommandAuthorityView)
	{
		CommandAuthorityView->Initialize(nullptr);
	}
	CommandAuthorityView = nullptr;
	CommandAuthorityPolicy = nullptr;
	bCommandProtocolReady = false;
}

bool USeinWorldSubsystem::IsStateMutationAuthorized() const
{
	if (bReadOnlyCallbackInProgress)
	{
		return false;
	}
	const bool bBootstrapMaterialization =
		MatchBootstrapState == ESeinMatchBootstrapState::Applying
		&& bMatchBootstrapMaterializerInvocationActive
		&& !bIsRunning && CurrentTick == 0;
	const bool bDeterministicRuntimeMutation =
		bIsRunning && SeinIsInSimContext(this);
	const bool bValidatedSnapshotRestore =
		bSnapshotRestoreMutationAuthorized && !bIsRunning;
	return bBootstrapMaterialization || bDeterministicRuntimeMutation
		|| bValidatedSnapshotRestore;
}

bool USeinWorldSubsystem::RequireStateMutationAuthorization(
	const TCHAR* Operation) const
{
	if (IsStateMutationAuthorized())
	{
		return true;
	}
	UE_LOG(LogSeinSim, Error,
		TEXT("%s rejected outside bootstrap Applying or deterministic simulation context (bootstrap=%s running=%d tick=%d)."),
		Operation ? Operation : TEXT("Simulation mutation"),
		MatchBootstrapStateName(MatchBootstrapState),
		bIsRunning ? 1 : 0,
		CurrentTick);
	return false;
}

bool USeinWorldSubsystem::RequireMutableStateAccess(
	const TCHAR* Operation) const
{
	if (!bReadOnlyCallbackInProgress && !bObserverCallbackInProgress)
	{
		return true;
	}
	UE_LOG(LogSeinSim, Error,
		TEXT("%s rejected from a read-only callback."),
		Operation ? Operation : TEXT("Mutable simulation-state access"));
	return false;
}

// ==================== Simulation Control ====================

bool USeinWorldSubsystem::ClaimMatchBootstrapAuthority(
	FName StableAuthorityID,
	const UObject* AuthorityOwner,
	FSeinMatchBootstrapAuthorityHandle& OutHandle,
	FString& OutError)
{
	OutError.Reset();
	if (MatchBootstrapAuthorityToken.IsValid())
	{
		if (StableAuthorityID == MatchBootstrapAuthorityID
			&& AuthorityOwner
			&& MatchBootstrapAuthorityOwner.IsValid()
			&& MatchBootstrapAuthorityOwner.Get() == AuthorityOwner)
		{
			OutHandle.StableAuthorityID = MatchBootstrapAuthorityID;
			OutHandle.Token = MatchBootstrapAuthorityToken;
			return true;
		}

		OutHandle = FSeinMatchBootstrapAuthorityHandle();
		OutError = FString::Printf(
			TEXT("Match bootstrap authority is already claimed by '%s'."),
			*MatchBootstrapAuthorityID.ToString());
		return false;
	}

	OutHandle = FSeinMatchBootstrapAuthorityHandle();
	if (StableAuthorityID.IsNone() || !AuthorityOwner)
	{
		OutError = TEXT("Match bootstrap authority requires a stable ID and concrete UObject owner.");
		return false;
	}
	const UWorld* ThisWorld = GetWorld();
	const UGameInstance* ThisGameInstance = ThisWorld
		? ThisWorld->GetGameInstance()
		: nullptr;
	const bool bOwnerBelongsToWorld = AuthorityOwner == this
		|| AuthorityOwner == ThisWorld
		|| AuthorityOwner == ThisGameInstance
		|| AuthorityOwner->GetWorld() == ThisWorld
		|| (ThisGameInstance
			&& AuthorityOwner->GetTypedOuter<UGameInstance>()
				== ThisGameInstance);
	if (!bOwnerBelongsToWorld)
	{
		OutError = TEXT("Match bootstrap authority owner does not belong to this world or its game instance.");
		return false;
	}
	if (MatchBootstrapState != ESeinMatchBootstrapState::Awaiting
		|| bIsRunning || TickerHandle.IsValid() || CurrentTick != 0
		|| MatchState != ESeinMatchState::Lobby)
	{
		OutError = FString::Printf(
			TEXT("Match bootstrap authority can be claimed only by a pristine Awaiting world (state=%s running=%d tick=%d)."),
			MatchBootstrapStateName(MatchBootstrapState),
			bIsRunning ? 1 : 0, CurrentTick);
		return false;
	}

	const FGuid Token = FGuid::NewGuid();
	if (!Token.IsValid())
	{
		OutError = TEXT("Match bootstrap authority token allocation failed.");
		return false;
	}
	MatchBootstrapAuthorityID = StableAuthorityID;
	MatchBootstrapAuthorityToken = Token;
	MatchBootstrapAuthorityOwner = AuthorityOwner;
	OutHandle.StableAuthorityID = StableAuthorityID;
	OutHandle.Token = Token;
	return true;
}

bool USeinWorldSubsystem::IsExactMatchBootstrapAuthority(
	const FSeinMatchBootstrapAuthorityHandle& Authority) const
{
	return MatchBootstrapAuthorityToken.IsValid()
		&& MatchBootstrapAuthorityOwner.IsValid()
		&& Authority.Token == MatchBootstrapAuthorityToken
		&& Authority.StableAuthorityID == MatchBootstrapAuthorityID;
}

bool USeinWorldSubsystem::BeginMatchBootstrap(
	const FGuid& ContractDigest,
	const FGuid& AuthorizationContextDigest,
	FString& OutError)
{
	OutError.Reset();
	if (!bMatchBootstrapMaterializerInvocationActive)
	{
		OutError = TEXT("Match bootstrap begin is available only inside the authority-gated materializer invocation.");
		return false;
	}
	if (bReadOnlyCallbackInProgress)
	{
		OutError = TEXT(
			"Match bootstrap begin is unavailable from a read-only callback.");
		return false;
	}
	if (MatchBootstrapState != ESeinMatchBootstrapState::Awaiting)
	{
		OutError = FString::Printf(
			TEXT("Match bootstrap cannot begin from state %s."),
			MatchBootstrapStateName(MatchBootstrapState));
		return false;
	}
	if (!ContractDigest.IsValid() || !AuthorizationContextDigest.IsValid())
	{
		OutError = TEXT("Match bootstrap requires valid contract and authorization-context digests.");
		FailMatchBootstrapInternal(OutError);
		return false;
	}
	if (bIsRunning || TickerHandle.IsValid() || CurrentTick != 0
		|| MatchState != ESeinMatchState::Lobby)
	{
		OutError = FString::Printf(
			TEXT("Match bootstrap requires a stopped tick-zero Lobby world (running=%d tick=%d match-state=%d)."),
			bIsRunning ? 1 : 0, CurrentTick, static_cast<int32>(MatchState));
		FailMatchBootstrapInternal(OutError);
		return false;
	}
	if (!IsSimulationContentReady())
	{
		OutError = SimulationContentFailureReason.IsEmpty()
			? TEXT("Match bootstrap cannot begin because simulation content is unavailable.")
			: SimulationContentFailureReason;
		FailMatchBootstrapInternal(OutError);
		return false;
	}
	if (!bCommandProtocolReady || !CommandAuthorityPolicy || !CommandAuthorityView)
	{
		OutError = TEXT("Match bootstrap cannot begin because the command protocol is unavailable.");
		FailMatchBootstrapInternal(OutError);
		return false;
	}
	if (!IsCurrentWorldCoveredBySimulationContent(OutError))
	{
		FailMatchBootstrapInternal(OutError);
		return false;
	}
	if (!NativeCanonicalStateSchema.IsValid()
		|| !NativeCanonicalStateSchema.GetContractDigest().IsValid())
	{
		OutError =
			TEXT("Match bootstrap cannot begin because the canonical state schema is unavailable.");
		FailMatchBootstrapInternal(OutError);
		return false;
	}
	if (!bSimSessionSeedInstalled)
	{
		OutError = TEXT("Match bootstrap cannot begin until its deterministic session seed is installed.");
		FailMatchBootstrapInternal(OutError);
		return false;
	}

	TArray<FSeinCanonicalInitialStateNativeContribution> NativeContributors;
	if (!FSeinCanonicalInitialStateDigest::CaptureNativeContributors(
		NativeContributors, OutError))
	{
		FailMatchBootstrapInternal(OutError);
		return false;
	}

	MatchBootstrapReceipt = FSeinMatchBootstrapReceipt();
	MatchBootstrapReceipt.ContractDigest = ContractDigest;
	MatchBootstrapReceipt.SimulationContentDigest =
		SimulationContentDigest;
	MatchBootstrapAuthorizationContextDigest = AuthorizationContextDigest;
	MatchBootstrapFailureReason.Reset();
	MatchBootstrapNativeContributors = MoveTemp(NativeContributors);
	MatchBootstrapValueContributions.Reset();
	MatchBootstrapState = ESeinMatchBootstrapState::Applying;
	return true;
}

bool USeinWorldSubsystem::SealLocalMatchBootstrap(
	const FGuid& PlanDigest,
	FSeinMatchBootstrapReceipt& OutReceipt,
	FString& OutError)
{
	OutReceipt = FSeinMatchBootstrapReceipt();
	OutError.Reset();
	if (!bMatchBootstrapMaterializerInvocationActive)
	{
		OutError = TEXT("Match bootstrap seal is available only inside the authority-gated materializer invocation.");
		return false;
	}
	if (MatchBootstrapState != ESeinMatchBootstrapState::Applying)
	{
		OutError = FString::Printf(
			TEXT("Local match bootstrap cannot seal from state %s."),
			MatchBootstrapStateName(MatchBootstrapState));
		return false;
	}
	if (!RequireStateMutationAuthorization(TEXT("SealLocalMatchBootstrap")))
	{
		OutError = TEXT(
			"Local match bootstrap sealing requires the active writable materializer invocation.");
		return false;
	}
	if (OwnerTransitionDepth != 0)
	{
		OutError = TEXT(
			"Local match bootstrap cannot seal during an active entity ownership transition.");
		return false;
	}
	if (!PlanDigest.IsValid())
	{
		OutError = TEXT("Local match bootstrap cannot seal an invalid plan digest.");
		FailMatchBootstrapInternal(OutError);
		return false;
	}
	if (!ValidateContainmentState(OutError))
	{
		OutError = FString::Printf(
			TEXT("Local match bootstrap has invalid containment state (%s)."),
			*OutError);
		FailMatchBootstrapInternal(OutError);
		return false;
	}
	if (!FreezeMatchBootstrapNativeContributions(OutError))
	{
		FailMatchBootstrapInternal(OutError);
		return false;
	}
	if (!CanonicalStateValues.IsSealed()
		|| !CanonicalStateValues.GetContractDigest().IsValid())
	{
		OutError =
			TEXT("Local match bootstrap cannot seal without its locally declared canonical-state contract.");
		FailMatchBootstrapInternal(OutError);
		return false;
	}
	MatchBootstrapReceipt.StateContractDigest =
		CanonicalStateValues.GetContractDigest();
	if (bIsRunning || TickerHandle.IsValid() || CurrentTick != 0
		|| MatchState != ESeinMatchState::Starting)
	{
		OutError = FString::Printf(
			TEXT("Local match bootstrap seal requires a stopped tick-zero Starting world (running=%d tick=%d match-state=%d)."),
			bIsRunning ? 1 : 0, CurrentTick, static_cast<int32>(MatchState));
		FailMatchBootstrapInternal(OutError);
		return false;
	}

	FGuid InitialStateDigest;
	if (!ComputeCanonicalInitialStateDigest(InitialStateDigest, OutError)
		|| !InitialStateDigest.IsValid())
	{
		if (OutError.IsEmpty())
		{
			OutError = TEXT("Canonical initial-state digest computation failed.");
		}
		FailMatchBootstrapInternal(OutError);
		return false;
	}

	MatchBootstrapReceipt.FormatVersion =
		FSeinMatchBootstrapReceipt::CurrentFormatVersion;
	MatchBootstrapReceipt.PlanDigest = PlanDigest;
	MatchBootstrapReceipt.InitialStateDigest = InitialStateDigest;
	if (!MatchBootstrapReceipt.IsValid())
	{
		OutError = TEXT("Local match bootstrap produced an invalid receipt.");
		FailMatchBootstrapInternal(OutError);
		return false;
	}

	MatchBootstrapState = ESeinMatchBootstrapState::LocallyReady;
	OutReceipt = MatchBootstrapReceipt;
	return true;
}

bool USeinWorldSubsystem::AuthorizeMatchBootstrap(
	const FSeinMatchBootstrapAuthorityHandle& Authority,
	const FSeinMatchBootstrapReceipt& Receipt,
	const FGuid& AuthorizationContextDigest,
	FString& OutError)
{
	OutError.Reset();
	if (bReadOnlyCallbackInProgress)
	{
		OutError = TEXT(
			"Match bootstrap authorization is unavailable from a read-only callback.");
		return false;
	}
	if (!IsExactMatchBootstrapAuthority(Authority))
	{
		OutError = TEXT("Match bootstrap authorization requires the exact claimed authority.");
		return false;
	}
	if (!bExecutionTopologyFrozen || !bExecutionTopologyValid
		|| !ExecutionTopologyDigest.IsValid())
	{
		OutError = ExecutionTopologyFailureReason.IsEmpty()
			? TEXT("Match bootstrap authorization requires a valid frozen execution topology.")
			: ExecutionTopologyFailureReason;
		FailMatchBootstrapInternal(OutError);
		return false;
	}
	if (MatchBootstrapState == ESeinMatchBootstrapState::Failed)
	{
		OutError = MatchBootstrapFailureReason.IsEmpty()
			? TEXT("Match bootstrap previously failed.")
			: MatchBootstrapFailureReason;
		return false;
	}

	const bool bExactIdentity = Receipt.IsValid()
		&& Receipt == MatchBootstrapReceipt
		&& AuthorizationContextDigest.IsValid()
		&& AuthorizationContextDigest == MatchBootstrapAuthorizationContextDigest;
	if (MatchBootstrapState == ESeinMatchBootstrapState::Authorized
		&& bExactIdentity)
	{
		if (bSimulationSchedulerReserved && TickerHandle.IsValid())
		{
			return true;
		}
		OutError = TEXT("Authorized match bootstrap lost its scheduler reservation.");
		FailMatchBootstrapInternal(OutError);
		return false;
	}
	if (MatchBootstrapState == ESeinMatchBootstrapState::Consumed
		&& bExactIdentity)
	{
		return true;
	}
	if (MatchBootstrapState != ESeinMatchBootstrapState::LocallyReady)
	{
		OutError = FString::Printf(
			TEXT("Match bootstrap authorization arrived in state %s."),
			MatchBootstrapStateName(MatchBootstrapState));
		FailMatchBootstrapInternal(OutError);
		return false;
	}
	if (!bExactIdentity)
	{
		OutError = TEXT("Match bootstrap authorization does not match the sealed receipt/context.");
		FailMatchBootstrapInternal(OutError);
		return false;
	}

	FGuid CurrentInitialStateDigest;
	if (!ComputeCanonicalInitialStateDigest(CurrentInitialStateDigest, OutError)
		|| CurrentInitialStateDigest != MatchBootstrapReceipt.InitialStateDigest)
	{
		if (OutError.IsEmpty())
		{
			OutError = TEXT("Tick-zero state changed after local bootstrap readiness.");
		}
		FailMatchBootstrapInternal(OutError);
		return false;
	}

	// Authorization is the prepare phase of the launch commit. Reserve the
	// fixed-tick callback while the world is still stopped so every peer that
	// reports AuthorizedReady has no fallible scheduler work left at commit.
	if (!ReserveSimulationScheduler(OutError))
	{
		FailMatchBootstrapInternal(OutError);
		return false;
	}

	MatchBootstrapState = ESeinMatchBootstrapState::Authorized;
	if (!bMatchBootstrapClosedBroadcast)
	{
		bMatchBootstrapClosedBroadcast = true;
		TGuardValue<bool> ReadOnlyGuard(bReadOnlyCallbackInProgress, true);
		TGuardValue<bool> ObserverGuard(bObserverCallbackInProgress, true);
		OnMatchBootstrapClosed.Broadcast(true);
	}
	return true;
}

bool USeinWorldSubsystem::FailMatchBootstrap(
	const FSeinMatchBootstrapAuthorityHandle& Authority,
	const FString& Reason,
	FString& OutError)
{
	OutError.Reset();
	if (bReadOnlyCallbackInProgress)
	{
		OutError = TEXT(
			"Match bootstrap failure is unavailable from a read-only callback.");
		return false;
	}
	if (!IsExactMatchBootstrapAuthority(Authority))
	{
		OutError = TEXT("Match bootstrap failure requires the exact claimed authority.");
		return false;
	}
	if (MatchBootstrapState == ESeinMatchBootstrapState::Consumed)
	{
		OutError = TEXT("Match bootstrap failure arrived after authorization was consumed.");
		return false;
	}
	FailMatchBootstrapInternal(Reason);
	return true;
}

void USeinWorldSubsystem::FailMatchBootstrapInternal(const FString& Reason)
{
	if (MatchBootstrapState == ESeinMatchBootstrapState::Consumed)
	{
		UE_LOG(LogSeinSim, Error,
			TEXT("Ignoring bootstrap failure after authorization was consumed: %s"),
			*Reason);
		return;
	}
	if (MatchBootstrapState == ESeinMatchBootstrapState::Failed)
	{
		return;
	}

	MatchBootstrapFailureReason = Reason.IsEmpty()
		? TEXT("Match bootstrap failed without a diagnostic.")
		: Reason;
	MatchBootstrapState = ESeinMatchBootstrapState::Failed;
	if (!bIsRunning)
	{
		ReleaseSimulationScheduler();
	}
	// A failed transaction can never seal. Drop module-owned callbacks now so
	// editor/module reload cannot leave executable code retained by the world.
	MatchBootstrapNativeContributors.Reset();
	MatchBootstrapValueContributions.Reset();
	CanonicalStateValues.Reset();
	FrozenCanonicalStateWorldBindingFrames.Reset();
	UE_LOG(LogSeinSim, Error, TEXT("Match bootstrap failed closed: %s"),
		*MatchBootstrapFailureReason);
	ShowSimulationErrorOnScreen(FString::Printf(
		TEXT("Match bootstrap failed: %s"), *MatchBootstrapFailureReason));
	if (!bMatchBootstrapClosedBroadcast)
	{
		bMatchBootstrapClosedBroadcast = true;
		TGuardValue<bool> ReadOnlyGuard(bReadOnlyCallbackInProgress, true);
		TGuardValue<bool> ObserverGuard(bObserverCallbackInProgress, true);
		OnMatchBootstrapClosed.Broadcast(false);
	}
}

void USeinWorldSubsystem::ShowSimulationErrorOnScreen(
	const FString& Message) const
{
#if !UE_BUILD_SHIPPING
	// The automation suites trigger these failures deliberately; an extra
	// error surface would fail every such test on unexpected error output.
	if (GIsAutomationTesting)
	{
		return;
	}
	const FString Banner = FString::Printf(TEXT("SEINARTS: %s"), *Message);
	if (GEngine)
	{
		// Keyed to this subsystem instance: reposts replace instead of stack,
		// and multi-client PIE worlds each keep their own line.
		GEngine->AddOnScreenDebugMessage(
			static_cast<uint64>(reinterpret_cast<UPTRINT>(this)),
			60.0f, FColor::Red, Banner);
	}
#if WITH_EDITOR
	// The UE_LOG at each call site is the canonical record; without
	// suppression the Message Log would mirror a duplicate Error line into
	// the output log.
	FMessageLog PIEMessageLog(TEXT("PIE"));
	PIEMessageLog.SuppressLoggingToOutputLog(true);
	PIEMessageLog.Error(FText::FromString(Banner));
#endif
#endif
}

bool USeinWorldSubsystem::FreezeMatchBootstrapNativeContributions(
	FString& OutError)
{
	OutError.Reset();
	if (MatchBootstrapState != ESeinMatchBootstrapState::Applying)
	{
		OutError = TEXT("Native initial-state contributors can freeze only while bootstrap is Applying.");
		return false;
	}

	TArray<FSeinCanonicalInitialStateValueContribution> Frozen;
	Frozen.Reserve(MatchBootstrapNativeContributors.Num());
	for (const FSeinCanonicalInitialStateNativeContribution& Native :
		MatchBootstrapNativeContributors)
	{
		FSeinCanonicalDigestWriter PayloadWriter(
			TEXT("SeinARTS.InitialState.Contributor"), Native.SchemaVersion);
		FString CaptureError;
		if (!Native.Capture
			|| !Native.Capture(*this, PayloadWriter, CaptureError))
		{
			OutError = CaptureError.IsEmpty()
				? FString::Printf(
					TEXT("Initial-state contributor '%s' failed without a diagnostic."),
					*Native.StableContributorID.ToString())
				: MoveTemp(CaptureError);
			return false;
		}

		FSeinCanonicalInitialStateValueContribution& Value =
			Frozen.AddDefaulted_GetRef();
		Value.StableContributorID = Native.StableContributorID;
		Value.SchemaVersion = Native.SchemaVersion;
		if (!PayloadWriter.Finalize(Value.ValueDigest, OutError))
		{
			return false;
		}
	}

	MatchBootstrapValueContributions.Append(MoveTemp(Frozen));
	MatchBootstrapNativeContributors.Reset();
	return true;
}

bool USeinWorldSubsystem::RegisterCanonicalBootstrapEvidenceValue(
	FName StableContributorID,
	uint32 SchemaVersion,
	const FInstancedStruct& Value,
	FString& OutError)
{
	OutError.Reset();
	if (MatchBootstrapState != ESeinMatchBootstrapState::Applying)
	{
		OutError = FString::Printf(
			TEXT("Bootstrap evidence may be registered only while bootstrap is Applying (state=%s)."),
			MatchBootstrapStateName(MatchBootstrapState));
		return false;
	}
	if (!RequireStateMutationAuthorization(
			TEXT("RegisterCanonicalBootstrapEvidenceValue")))
	{
		OutError = TEXT(
			"Bootstrap evidence requires the active authority-gated materializer invocation.");
		return false;
	}
	if (StableContributorID.IsNone() || SchemaVersion == 0 || !Value.IsValid())
	{
		OutError = TEXT("Bootstrap evidence requires a stable ID, non-zero schema version, and valid deterministic value.");
		FailMatchBootstrapInternal(OutError);
		return false;
	}

	const FString CanonicalID =
		FSeinCanonicalInitialStateDigest::CanonicalContributorID(
			StableContributorID);
	for (const FSeinCanonicalInitialStateNativeContribution& Existing :
		MatchBootstrapNativeContributors)
	{
		if (FSeinCanonicalInitialStateDigest::CanonicalContributorID(
			Existing.StableContributorID) == CanonicalID)
		{
			OutError = FString::Printf(
				TEXT("Initial-state contributor ID '%s' is already claimed by a native contributor."),
				*CanonicalID);
			FailMatchBootstrapInternal(OutError);
			return false;
		}
	}
	for (const FSeinCanonicalInitialStateValueContribution& Existing :
		MatchBootstrapValueContributions)
	{
		if (FSeinCanonicalInitialStateDigest::CanonicalContributorID(
			Existing.StableContributorID) == CanonicalID)
		{
			OutError = FString::Printf(
				TEXT("Initial-state contributor ID '%s' was registered more than once."),
				*CanonicalID);
			FailMatchBootstrapInternal(OutError);
			return false;
		}
	}

	FGuid ValueDigest;
	FSeinDeterministicValueDigestError DigestError;
	if (FSeinDeterministicValueDigest::Compute(
		Value,
		ValueDigest,
		&DigestError,
		MakeRuntimeDigestOptions())
		!= ESeinDeterministicValueDigestResult::Success
		|| !ValueDigest.IsValid())
	{
		OutError = FString::Printf(
			TEXT("Initial-state contributor '%s' is not a canonical deterministic value (%s: %s)."),
			*CanonicalID, *DigestError.FieldPath, *DigestError.Message);
		FailMatchBootstrapInternal(OutError);
		return false;
	}

	FSeinCanonicalInitialStateValueContribution& Contribution =
		MatchBootstrapValueContributions.AddDefaulted_GetRef();
	Contribution.StableContributorID = StableContributorID;
	Contribution.SchemaVersion = SchemaVersion;
	Contribution.ValueDigest = ValueDigest;
	return true;
}

bool USeinWorldSubsystem::SetCanonicalStateValue(
	const FSeinCanonicalStateKey& Key,
	const FInstancedStruct& Value,
	FString& OutError)
{
	OutError.Reset();
	if (!RequireStateMutationAuthorization(
		TEXT("SetCanonicalStateValue")))
	{
		OutError =
			TEXT("Canonical state values may change only during bootstrap or deterministic simulation.");
		return false;
	}
	return CanonicalStateValues.SetValue(Key, Value, OutError);
}

bool USeinWorldSubsystem::GetCanonicalStateValue(
	const FSeinCanonicalStateKey& Key,
	FInstancedStruct& OutValue) const
{
	return CanonicalStateValues.GetValue(Key, OutValue);
}

bool USeinWorldSubsystem::HasFrozenCanonicalStateContributor(
	const FSeinCanonicalStateKey& Key,
	ESeinCanonicalStateRole RequiredRole) const
{
	if (!NativeCanonicalStateSchema.IsValid() || !Key.IsValid())
	{
		return false;
	}
	for (const FSeinFrozenCanonicalStateContributor& Contributor :
		NativeCanonicalStateSchema.GetContributors())
	{
		if (Contributor.Descriptor.Key == Key
			&& Contributor.Descriptor.Role == RequiredRole)
		{
			return true;
		}
	}
	return false;
}

bool USeinWorldSubsystem::EnsureMatchBootstrapLocallyReady(
	const FSeinMatchBootstrapAuthorityHandle& Authority,
	const FSeinMatchSettings& Settings,
	const FGuid& AuthorizationContextDigest,
	FSeinMatchBootstrapReceipt& OutReceipt,
	FString& OutError)
{
	OutReceipt = FSeinMatchBootstrapReceipt();
	OutError.Reset();
	if (bReadOnlyCallbackInProgress)
	{
		OutError = TEXT(
			"Local match bootstrap is unavailable from a read-only callback.");
		return false;
	}
	if (!IsExactMatchBootstrapAuthority(Authority))
	{
		OutError = TEXT("Local match bootstrap requires the exact claimed authority.");
		return false;
	}
	if (bMatchBootstrapMaterializerInvocationActive)
	{
		OutError = TEXT("Local match bootstrap materializer invocation is already active.");
		return false;
	}
	FString TopologyError;
	if (!FreezeExecutionTopology(TopologyError))
	{
		OutError = TopologyError.IsEmpty()
			? TEXT("Local match bootstrap could not freeze its execution topology.")
			: MoveTemp(TopologyError);
		if (MatchBootstrapState == ESeinMatchBootstrapState::Awaiting)
		{
			FailMatchBootstrapInternal(OutError);
		}
		return false;
	}
	if (!AuthorizationContextDigest.IsValid())
	{
		OutError = TEXT("Local match bootstrap requires a valid authorization-context digest.");
		if (MatchBootstrapState == ESeinMatchBootstrapState::Awaiting)
		{
			FailMatchBootstrapInternal(OutError);
		}
		return false;
	}

	FGameplayTag RejectionReason;
	FSeinMatchSettings CanonicalSettings = Settings;
	FGuid ExpectedSettingsDigest;
	if (!ValidateMatchSettings(Settings, RejectionReason)
		|| !SeinCanonicalizeAndDigestMatchSettings(
			CanonicalSettings, ExpectedSettingsDigest))
	{
		OutError = FString::Printf(
			TEXT("Local match bootstrap settings are invalid (%s)."),
			*RejectionReason.ToString());
		if (MatchBootstrapState == ESeinMatchBootstrapState::Awaiting)
		{
			FailMatchBootstrapInternal(OutError);
		}
		return false;
	}

	const auto VerifyClosedState = [&]()
	{
		const bool bAtLeastLocallyReady =
			MatchBootstrapState == ESeinMatchBootstrapState::LocallyReady
			|| MatchBootstrapState == ESeinMatchBootstrapState::Authorized
			|| MatchBootstrapState == ESeinMatchBootstrapState::Consumed;
		return bAtLeastLocallyReady
			&& MatchBootstrapReceipt.IsValid()
			&& MatchBootstrapAuthorizationContextDigest
				== AuthorizationContextDigest
			&& MatchSettingsDigest == ExpectedSettingsDigest
			&& FSeinMatchSettings::StaticStruct()->CompareScriptStruct(
				&CurrentMatchSettings, &CanonicalSettings, PPF_None);
	};

	if (VerifyClosedState())
	{
		OutReceipt = MatchBootstrapReceipt;
		return true;
	}
	if (MatchBootstrapState == ESeinMatchBootstrapState::Failed)
	{
		OutError = MatchBootstrapFailureReason.IsEmpty()
			? TEXT("Local match bootstrap previously failed.")
			: MatchBootstrapFailureReason;
		return false;
	}
	if (MatchBootstrapState != ESeinMatchBootstrapState::Awaiting)
	{
		OutError = FString::Printf(
			TEXT("Local match bootstrap request conflicts with state %s."),
			MatchBootstrapStateName(MatchBootstrapState));
		return false;
	}
	if (!MatchBootstrapMaterializer.IsBound())
	{
		// World-subsystem initialization order may make an early request race the
		// gameplay-shell binding. This is the one retryable readiness condition.
		OutError = TEXT("No local match bootstrap materializer is bound yet.");
		return false;
	}

	FSeinMatchBootstrapMaterializer Materializer = MatchBootstrapMaterializer;
	FSeinMatchBootstrapReceipt MaterializedReceipt;
	TArray<FString> MaterializedWorldBindingFrames;
	FString MaterializerError;
	bool bMaterialized = false;
	{
		TGuardValue<bool> MaterializerCapability(
			bMatchBootstrapMaterializerInvocationActive, true);
		if (BeginMatchBootstrap(
				ExpectedSettingsDigest,
				AuthorizationContextDigest,
				MaterializerError)
			&& BuildLocallyDeclaredCanonicalState(
				CanonicalSettings,
				true,
				ExecutionTopologyManifest,
				CanonicalStateValues,
				MaterializedWorldBindingFrames,
				MaterializerError))
		{
			FrozenCanonicalStateWorldBindingFrames =
				MoveTemp(MaterializedWorldBindingFrames);
			bMaterialized = Materializer.Execute(
				CanonicalSettings,
				AuthorizationContextDigest,
				MaterializedReceipt,
				MaterializerError);
		}
	}
	if (!bMaterialized)
	{
		OutError = MaterializerError.IsEmpty()
			? TEXT("The local match bootstrap materializer failed.")
			: MoveTemp(MaterializerError);
		FailMatchBootstrapInternal(OutError);
		return false;
	}
	if (!VerifyClosedState()
		|| MaterializedReceipt != MatchBootstrapReceipt)
	{
		OutError = TEXT("The local materializer returned without a matching sealed Core receipt.");
		FailMatchBootstrapInternal(OutError);
		return false;
	}

	OutReceipt = MatchBootstrapReceipt;
	return true;
}

bool USeinWorldSubsystem::LaunchAuthorizedMatchBootstrap(
	const FSeinMatchBootstrapAuthorityHandle& Authority,
	FString& OutError)
{
	OutError.Reset();
	if (bReadOnlyCallbackInProgress)
	{
		OutError = TEXT(
			"Match bootstrap launch is unavailable from a read-only callback.");
		return false;
	}
	if (bSnapshotRestoreInProgress)
	{
		OutError = TEXT("Match bootstrap launch is unavailable during snapshot restore.");
		return false;
	}
	if (!IsExactMatchBootstrapAuthority(Authority))
	{
		OutError = TEXT("Match bootstrap launch requires the exact claimed authority.");
		return false;
	}
	if (!bExecutionTopologyFrozen || !bExecutionTopologyValid
		|| !ExecutionTopologyDigest.IsValid())
	{
		OutError = ExecutionTopologyFailureReason.IsEmpty()
			? TEXT("Match bootstrap launch requires a valid frozen execution topology.")
			: ExecutionTopologyFailureReason;
		FailMatchBootstrapInternal(OutError);
		return false;
	}
	if (MatchBootstrapState == ESeinMatchBootstrapState::Consumed && bIsRunning)
	{
		if (bSimulationSchedulerReserved && TickerHandle.IsValid())
		{
			return true;
		}
		OutError = TEXT("Running simulation lost its scheduler reservation.");
		return false;
	}
	if (MatchBootstrapState != ESeinMatchBootstrapState::Authorized)
	{
		OutError = FString::Printf(
			TEXT("Match bootstrap launch requires Authorized state (state=%s)."),
			MatchBootstrapStateName(MatchBootstrapState));
		return false;
	}
	if (!bSimulationSchedulerReserved || !TickerHandle.IsValid())
	{
		OutError = TEXT("Authorized match bootstrap is not scheduler-ready.");
		FailMatchBootstrapInternal(OutError);
		return false;
	}

	// Authorization performed every fallible check and reserved the dormant
	// scheduler. The distributed launch commit is therefore only a state flip.
	TimeAccumulator = 0.0f;
	bIsRunning = true;
	MatchBootstrapState = ESeinMatchBootstrapState::Consumed;
	UE_LOG(LogSeinSim, Log, TEXT("Simulation started"));
	return true;
}

bool USeinWorldSubsystem::StartSimulation()
{
	FString Error;
	return StartSimulationInternal(Error);
}

bool USeinWorldSubsystem::ReserveSimulationScheduler(FString& OutError)
{
	OutError.Reset();
	if (bSimulationSchedulerReserved)
	{
		if (TickerHandle.IsValid()) return true;
		OutError = TEXT("Simulation scheduler reservation lost its ticker handle.");
		return false;
	}
	if (TickerHandle.IsValid())
	{
		OutError = TEXT("Simulation scheduler has an untracked ticker handle.");
		return false;
	}

	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(
			this, &USeinWorldSubsystem::TickSimulation));
	if (!TickerHandle.IsValid())
	{
		OutError = TEXT("Failed to reserve the fixed-tick scheduler.");
		return false;
	}
	bSimulationSchedulerReserved = true;
	return true;
}

void USeinWorldSubsystem::ReleaseSimulationScheduler()
{
	if (TickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
		TickerHandle.Reset();
	}
	bSimulationSchedulerReserved = false;
}

bool USeinWorldSubsystem::StartSimulationInternal(FString& OutError)
{
	OutError.Reset();
	if (!bExecutionTopologyFrozen || !bExecutionTopologyValid
		|| !ExecutionTopologyDigest.IsValid())
	{
		OutError = ExecutionTopologyFailureReason.IsEmpty()
			? TEXT("Simulation start requires a valid frozen execution topology.")
			: ExecutionTopologyFailureReason;
		UE_LOG(LogSeinSim, Error, TEXT("%s"), *OutError);
		return false;
	}
	if (bSnapshotCaptureInProgress || bSnapshotRestoreInProgress)
	{
		OutError = bSnapshotCaptureInProgress
			? TEXT("Simulation start is unavailable during snapshot capture.")
			: TEXT("Simulation start is unavailable during snapshot restore.");
		UE_LOG(LogSeinSim, Error, TEXT("%s"), *OutError);
		return false;
	}
	if (bIsRunning)
	{
		if (MatchBootstrapState == ESeinMatchBootstrapState::Consumed)
		{
			if (bSimulationSchedulerReserved && TickerHandle.IsValid())
			{
				return true;
			}
			OutError = TEXT("Running simulation lost its scheduler reservation.");
			UE_LOG(LogSeinSim, Error, TEXT("%s"), *OutError);
			return false;
		}
		OutError = TEXT("Simulation is running without a consumed bootstrap.");
		UE_LOG(LogSeinSim, Error, TEXT("%s"), *OutError);
		return false;
	}
	if (!bCommandProtocolReady || !CommandAuthorityPolicy || !CommandAuthorityView)
	{
		OutError = TEXT("Simulation start refused: command protocol initialization failed.");
		UE_LOG(LogSeinSim, Error, TEXT("%s"), *OutError);
		return false;
	}

	const bool bTickZeroResume =
		MatchBootstrapState == ESeinMatchBootstrapState::Consumed
		&& CurrentTick == 0;
	if (MatchBootstrapState != ESeinMatchBootstrapState::Consumed)
	{
		OutError = FString::Printf(
			TEXT("Simulation start refused by match bootstrap barrier (state=%s)."),
			MatchBootstrapStateName(MatchBootstrapState));
		UE_LOG(LogSeinSim, Error, TEXT("%s"), *OutError);
		return false;
	}
	if (bTickZeroResume)
	{
		FString DigestError;
		FGuid CurrentInitialStateDigest;
		if (CurrentTick != 0 || MatchState != ESeinMatchState::Starting
			|| !MatchBootstrapReceipt.IsValid()
			|| !MatchBootstrapAuthorizationContextDigest.IsValid()
			|| !ComputeCanonicalInitialStateDigest(
				CurrentInitialStateDigest, DigestError)
			|| CurrentInitialStateDigest
				!= MatchBootstrapReceipt.InitialStateDigest)
		{
			if (DigestError.IsEmpty())
			{
				DigestError = TEXT("Authorized tick-zero state changed before first launch.");
			}
			UE_LOG(LogSeinSim, Error,
				TEXT("Simulation tick-zero resume refused: %s"),
				*DigestError);
			OutError = MoveTemp(DigestError);
			return false;
		}
	}

	TimeAccumulator = 0.0f;
	if (!ReserveSimulationScheduler(OutError))
	{
		UE_LOG(LogSeinSim, Error,
			TEXT("Simulation start refused: %s"), *OutError);
		return false;
	}
	bIsRunning = true;
	UE_LOG(LogSeinSim, Log, TEXT("Simulation resumed"));
	return true;
}

void USeinWorldSubsystem::StopSimulation()
{
	if (bSnapshotCaptureInProgress || bSnapshotRestoreInProgress)
	{
		UE_LOG(LogSeinSim, Error, TEXT("Simulation stop is unavailable during snapshot %s."),
			bSnapshotCaptureInProgress ? TEXT("capture") : TEXT("restore"));
		return;
	}
	if (!bIsRunning)
	{
		// RemainStopped snapshot adoption reserves a dormant callback so the
		// coordinator can activate without a fallible scheduler acquisition.
		// An explicit stop is the abort/release operation for that readiness.
		if (MatchBootstrapState == ESeinMatchBootstrapState::Consumed
			&& bSimulationSchedulerReserved)
		{
			ReleaseSimulationScheduler();
			UE_LOG(LogSeinSim, Log,
				TEXT("Dormant simulation scheduler released at tick %d"),
				CurrentTick);
		}
		return;
	}
	bIsRunning = false;
	ReleaseSimulationScheduler();

	UE_LOG(LogSeinSim, Log, TEXT("Simulation stopped at tick %d"), CurrentTick);
}

float USeinWorldSubsystem::GetInterpolationAlpha() const
{
	if (FixedDeltaTimeSeconds > 0.0f)
	{
		return FMath::Clamp(TimeAccumulator / FixedDeltaTimeSeconds, 0.0f, 1.0f);
	}
	return 0.0f;
}

bool USeinWorldSubsystem::TickSimulation(float DeltaTime)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_TEXT(*SimulationTraceScopeName);
	// A scheduler reservation is intentionally persistent while the simulation
	// is stopped. Authorized bootstrap keeps it dormant until launch; stopped
	// snapshot adoption keeps it dormant while an outer reconnect/catch-up
	// workflow installs its command tail and agrees the activation root.
	// StopSimulation and bootstrap failure explicitly release the reservation.
	if (!bIsRunning)
	{
		return bSimulationSchedulerReserved;
	}
	TGuardValue<bool> TickDispatchGuard(
		bSimulationTickDispatchInProgress, true);

	// Frozen sim time has a separate canonical control lane. It may dispatch one
	// exact frame (initially Resume V1 only), but never advances CurrentTick,
	// systems, AI, votes, latent actions, deferred work, or the ordinary buffer.
	// Resetting the accumulator prevents wall-clock catch-up after resume.
	if (bSimPaused)
	{
		TimeAccumulator = 0.0f;
		PumpPauseControlFrame();
		return true;
	}

	const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>();
	const int32 MaxTicks = Settings->MaxTicksPerFrame;

	// TicksPerTurn: how many sim ticks make up one network turn. Derived from
	// the two settings; integer division so a misconfiguration doesn't yield
	// a fractional gate (better to round down and re-check sooner). Only
	// consulted when the lockstep resolver is bound (USeinNetSubsystem
	// registers it once the local slot is assigned).
	const int32 TicksPerTurn = (Settings->TurnRate > 0)
		? FMath::Max(1, Settings->SimulationTickRate / Settings->TurnRate)
		: 1;

	TimeAccumulator += DeltaTime;

	// Catch-up burst: a resyncing peer must CLOSE a wall-clock deficit, which
	// real-time accumulation can never do (the deficit would stay constant
	// forever). While the catch-up window is open, top the accumulator up to
	// a full frame's tick budget so the pump runs MaxTicksPerFrame whenever
	// the lockstep gate has turns available. Sim-safe by design: the
	// accumulator is a wall-clock scheduler, not sim state — peers are
	// bit-identical at any tick N regardless of pacing — and the turn gate
	// still stalls the pump the moment the tail runs dry.
	if (bResyncCatchUpInProgress)
	{
		TimeAccumulator = FMath::Max(
			TimeAccumulator,
			FixedDeltaTimeSeconds * static_cast<float>(MaxTicks));
	}

	int32 TicksProcessed = 0;
	int32 LastCompletedTickThisFrame = INDEX_NONE;
	while (bIsRunning
		&& TimeAccumulator >= FixedDeltaTimeSeconds
		&& TicksProcessed < MaxTicks)
	{
		if (!ValidateFrozenConfigFingerprint()
			|| !ValidateFrozenCanonicalStateWorldBindings())
		{
			break;
		}
		const int32 NextTick = CurrentTick + 1;

		// Lockstep gate (Phase 2b). At each turn boundary, ask the network
		// layer whether the assembled turn for the upcoming turn is ready.
		// If not, stall — leave the accumulator alone so we retry next frame.
		// Resolver unbound (Standalone, networking disabled, or NetSubsystem
		// hasn't latched yet) = no gating, sim runs free.
		if (TurnReadyResolver.IsBound() && (NextTick % TicksPerTurn == 0))
		{
			const int32 NextTurn = NextTick / TicksPerTurn;
			if (!TurnReadyResolver.Execute(NextTurn))
			{
				// Stall — break out of the catch-up loop without consuming
				// the accumulator. Next frame's pump will retry. The "falling
				// behind" warning at the bottom is suppressed by the early
				// break since TicksProcessed < MaxTicks may still be true.
				break;
			}
			if (TurnConsumeNotifier.IsBound())
			{
				TurnConsumeNotifier.Execute(NextTurn);
			}
		}

		// Replay turns are primed between ticks. Commit them only when their
		// exact upcoming tick is actually about to run. Keeping this lane
		// separate until now lets Stop() retract a primed future turn without
		// erasing deterministic-system follow-ups already in PendingCommands.
		if (bReplayOwnsExternalCommandIngress
			&& PendingReplayCommands.Num() > 0)
		{
			for (const FSeinCommand& ReplayCommand :
				PendingReplayCommands.GetCommands())
			{
				PendingCommands.AddCommand(ReplayCommand);
			}
			PendingReplayCommands.Clear();
		}

		CurrentTick = NextTick;
		TimeAccumulator -= FixedDeltaTimeSeconds;
		TicksProcessed++;

		// Convert to deterministic fixed-point (from compile-time constant, not runtime float)
		FFixedPoint SimDeltaTime = FFixedPoint::One / FFixedPoint::FromInt(Settings->SimulationTickRate);

		TickSystems(SimDeltaTime);
		if (!bExecutionTopologyValid)
		{
			break;
		}

#if !UE_BUILD_SHIPPING
		// Local mutation diagnostics: when the legacy Log cvar is on, dump the
		// incomplete in-process fingerprint each tick. This can localize a
		// divergence only when preload/name-pool conditions are controlled; it
		// is not peer or fresh-process determinism evidence. Use the canonical
		// world root at stable boundaries for that. Gated off in shipping.
		//
		// Two log levels:
		//   = 1: local fingerprint only. Compact, useful for one-process traces.
		//   = 2: hash + per-entity dump on tick 1, then hash-only on
		//        subsequent ticks. Tick 1 is the initial state — diffing
		//        two log files at tick 1 reveals what's structurally
		//        different about the starting world (entity IDs / owners
		//        / positions). Best for "spawns are wrong sometimes" hunts.
		//   = 3: hash + per-entity dump EVERY tick. Verbose; use only for
		//        narrow ranges or very early divergences.
		{
			static IConsoleVariable* CVarLog = IConsoleManager::Get().FindConsoleVariable(TEXT("Sein.Sim.StateHash.Log"));
			const int32 StateLogLevel = CVarLog ? CVarLog->GetInt() : 0;
			if (StateLogLevel != 0)
			{
				UE_LOG(LogSeinSim, Log,
					TEXT("LegacyLocalStateFingerprint[tick %d] = 0x%08x"),
					CurrentTick, static_cast<uint32>(ComputeStateHash()));

				const bool bDumpEntities = (StateLogLevel >= 3) || (StateLogLevel == 2 && CurrentTick == 1);
				if (bDumpEntities)
				{
					UE_LOG(LogSeinSim, Log,
						TEXT("LegacyLocalStateFingerprint[tick %d] entity dump  (active=%d):"),
						CurrentTick, EntityPool.GetActiveCount());

					// Walk every alive entity and print ID, owner, and the
					// raw fixed-point position. Raw int64 values diff
					// cleanly across log files (no float-to-string drift).
					EntityPool.ForEachEntity([this](FSeinEntityHandle Handle, const FSeinEntity& Entity)
					{
						const FSeinPlayerID Owner = EntityPool.GetOwner(Handle);
						const FFixedVector& L = Entity.Transform.Location;
						UE_LOG(LogSeinSim, Log,
							TEXT("  H(%d:%d)  slot=%u  pos=(%lld, %lld, %lld) [raw 32.32]"),
							Handle.Index, Handle.Generation, Owner.Value,
							L.X.Value, L.Y.Value, L.Z.Value);
					});
				}
			}
		}
#endif

		{
			TRACE_CPUPROFILER_EVENT_SCOPE(Sein_World_ReplayCommandBoundary);
			ReplayCommandBoundaryNotifier.Broadcast(CurrentTick);
		}
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(Sein_World_TickCompletedObservers);
			TGuardValue<bool> ReadOnlyGuard(bReadOnlyCallbackInProgress, true);
			TGuardValue<bool> ObserverGuard(bObserverCallbackInProgress, true);
			OnSimTickCompleted.Broadcast(CurrentTick);
		}
		LastCompletedTickThisFrame = CurrentTick;
	}

	if (LastCompletedTickThisFrame != INDEX_NONE)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(Sein_World_FrameCompletedObservers);
		TGuardValue<bool> ReadOnlyGuard(bReadOnlyCallbackInProgress, true);
		TGuardValue<bool> ObserverGuard(bObserverCallbackInProgress, true);
		OnSimFrameCompleted.Broadcast(
			LastCompletedTickThisFrame, TicksProcessed);
	}

	if (TicksProcessed >= MaxTicks && TimeAccumulator > FixedDeltaTimeSeconds)
	{
		// Persistence-escalated log: most clamps are single-frame hitches
		// (PIE multi-window, GC, level streaming) — recovers on the next
		// frame. Only escalate to Warning when the sim has been clamping
		// CONTINUOUSLY for ≥1 second (i.e. genuinely can't keep up). Below
		// the threshold, stay at Verbose so the log isn't drowned.
		const double NowSec = FPlatformTime::Seconds();
		if (LastClampTime <= 0.0 || (NowSec - LastClampTime) > 0.25)
		{
			// Not been clamping recently — start a fresh clamp window.
			ClampWindowStartTime = NowSec;
			bClampWarningEmitted = false;
		}
		LastClampTime = NowSec;
		const double ClampDuration = NowSec - ClampWindowStartTime;
		if (ClampDuration < 1.0)
		{
			UE_LOG(LogSeinSim, Verbose, TEXT("Simulation falling behind (transient clamp). Accumulator clamped."));
		}
		else if (!bClampWarningEmitted || (NowSec - LastClampWarnTime) >= 2.0)
		{
			UE_LOG(LogSeinSim, Warning,
				TEXT("Simulation falling behind for %.1fs (continuous). Sim cannot keep up — drop SimulationTickRate or raise MaxTicksPerFrame in plugin settings."),
				ClampDuration);
			LastClampWarnTime = NowSec;
			bClampWarningEmitted = true;
		}
		TimeAccumulator = FixedDeltaTimeSeconds;
	}

	return bExecutionTopologyValid;
}

bool USeinWorldSubsystem::ValidateFrozenConfigFingerprint()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(Sein_World_ValidateConfigFingerprint);
	const USeinARTSCoreSettings* CurrentSettings =
		GetDefault<USeinARTSCoreSettings>();
	const int32 CurrentFingerprint = CurrentSettings
		? CurrentSettings->ComputeConfigFingerprint()
		: 0;
	if (CurrentFingerprint == ConfigFingerprint)
	{
		return true;
	}

	InvalidateDeterministicExecutionContract(FString::Printf(
		TEXT("Lockstep configuration changed after world initialization (frozen=0x%08x, current=0x%08x). Restart the match/PIE session after changing simulation settings."),
		static_cast<uint32>(ConfigFingerprint),
		static_cast<uint32>(CurrentFingerprint)));
	return false;
}

bool USeinWorldSubsystem::ValidateFrozenCanonicalStateWorldBindings()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(Sein_World_ValidateCanonicalBindings);
	TArray<FString> CurrentFrames;
	FString Error;
	bool bCaptured = false;
	{
		// World-binding providers may inspect immutable subsystem state, but a
		// tick-boundary validator must never lend them simulation mutation or
		// command authority.
		TGuardValue<bool> ReadOnlyGuard(
			bReadOnlyCallbackInProgress, true);
		TGuardValue<bool> ObserverGuard(
			bObserverCallbackInProgress, true);
		bCaptured =
			FSeinCanonicalStateRegistry::CaptureWorldBindingFrames(
				NativeCanonicalStateSchema,
				{*this,
					ESeinCanonicalStateWorldBindingDisposition::
						Provisional},
				CurrentFrames,
				Error);
		if (bCaptured)
		{
			FString AuthoritativeDestinationProviderFrame;
			bCaptured = BuildAuthoritativeDestinationProviderBindingFrame(
				AuthoritativeDestinationProviderFrame,
				Error);
			if (bCaptured)
			{
				CurrentFrames.Add(
					MoveTemp(AuthoritativeDestinationProviderFrame));
			}
		}
	}

	if (!bCaptured)
	{
		if (bExecutionTopologyValid)
		{
			InvalidateDeterministicExecutionContract(
				Error.IsEmpty()
					? TEXT("Canonical StateContract world-binding validation failed at the fixed-tick boundary.")
					: FString::Printf(
						TEXT("Canonical StateContract world-binding validation failed at the fixed-tick boundary: %s"),
						*Error));
		}
		return false;
	}
	if (CurrentFrames != FrozenCanonicalStateWorldBindingFrames)
	{
		InvalidateDeterministicExecutionContract(
			TEXT("Canonical StateContract world bindings changed after bootstrap or checkpoint adoption."));
		return false;
	}
	return bExecutionTopologyValid;
}

void USeinWorldSubsystem::TickSystems(FFixedPoint DeltaTime)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(Sein_World_TickSystems);
	if (!bExecutionTopologyFrozen || !bExecutionTopologyValid)
	{
		InvalidateFrozenExecutionTopology(
			TEXT("Simulation tick reached an unfrozen or invalid execution topology."));
		return;
	}

	{
		SEIN_SIM_SCOPE(*this)
		// Phase 1: PreTick — effects, cooldowns, resources
		for (const FRegisteredSystem& Registered : Systems)
		{
			if (Registered.System
				&& Registered.Descriptor.Phase == ESeinTickPhase::PreTick)
			{
				TRACE_CPUPROFILER_EVENT_SCOPE_TEXT(*Registered.CanonicalStableID);
				Registered.System->Tick(DeltaTime, *this);
				if (!bExecutionTopologyValid) return;
			}
		}

		// Advance deterministic match state and expire idle votes.
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(Sein_World_TickMatchState);
			TickMatchState();
		}
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(Sein_World_TickVotes);
			TickVotes();
		}
	}

	// Host-only AI reasoning is intentionally outside mutation authority. Its
	// sole write seam is EmitCommand, which routes through lockstep ingress.
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(Sein_World_TickAIControllers);
		TickAIControllers(DeltaTime);
	}
	{
		SEIN_SIM_SCOPE(*this)
		// Phase 2: process AI-emitted/external commands, then deterministic systems.
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(Sein_World_ProcessCommands);
			ProcessCommands();
		}
		for (const FRegisteredSystem& Registered : Systems)
		{
			if (Registered.System
				&& Registered.Descriptor.Phase
				== ESeinTickPhase::CommandProcessing)
			{
				TRACE_CPUPROFILER_EVENT_SCOPE_TEXT(*Registered.CanonicalStableID);
				Registered.System->Tick(DeltaTime, *this);
				if (!bExecutionTopologyValid) return;
			}
		}

		// Phase 3: AbilityExecution — tick active abilities and latent actions
		if (LatentActionManager)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(Sein_World_TickLatentActions);
			LatentActionManager->TickAll(DeltaTime, *this);
		}
		for (const FRegisteredSystem& Registered : Systems)
		{
			if (Registered.System
				&& Registered.Descriptor.Phase
				== ESeinTickPhase::AbilityExecution)
			{
				TRACE_CPUPROFILER_EVENT_SCOPE_TEXT(*Registered.CanonicalStableID);
				Registered.System->Tick(DeltaTime, *this);
				if (!bExecutionTopologyValid) return;
			}
		}

		// Phase 4: PostTick — cleanup and settled tick state
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(Sein_World_ProcessDeferredDestroys);
			ProcessDeferredDestroys();
		}
		for (const FRegisteredSystem& Registered : Systems)
		{
			if (Registered.System
				&& Registered.Descriptor.Phase == ESeinTickPhase::PostTick)
			{
				TRACE_CPUPROFILER_EVENT_SCOPE_TEXT(*Registered.CanonicalStableID);
				Registered.System->Tick(DeltaTime, *this);
				if (!bExecutionTopologyValid) return;
			}
		}

		// Phase 5: terminal, stateless observation of settled authoritative state.
		for (const FRegisteredSystem& Registered : Systems)
		{
			if (Registered.System
				&& Registered.Descriptor.Phase
					== ESeinTickPhase::FinalObservation)
			{
				TRACE_CPUPROFILER_EVENT_SCOPE_TEXT(*Registered.CanonicalStableID);
				Registered.System->Tick(DeltaTime, *this);
				if (!bExecutionTopologyValid) return;
			}
		}
	}
}

// ==================== Command Processing ====================

void USeinWorldSubsystem::ProcessCommands()
{
	// Diagnostic trace of command-buffer drain — each tick that has pending
	// commands, dump the type list. Verbose so it stays out of default logs;
	// enable with `Log LogSeinSim Verbose` when debugging command-flow issues
	// (auto-activate-Build chain, observer command leak, broker dispatch, etc).
	if (PendingCommands.Num() > 0)
	{
		UE_LOG(LogSeinSim, Verbose, TEXT("ProcessCommands[tick %d]: %d commands pending at entry"),
			CurrentTick, PendingCommands.Num());
		for (const FSeinCommand& C : PendingCommands.GetCommands())
		{
			UE_LOG(LogSeinSim, Verbose,
				TEXT("  - type=%s entity=%s ability=%s target=%s player=%s"),
				*C.CommandType.ToString(), *C.EntityHandle.ToString(),
				*C.AbilityTag.ToString(), *C.TargetEntity.ToString(),
				*C.PlayerID.ToString());
		}
	}

	// Broadcast for debug tooling before processing (commands are still in the buffer)
	if (PendingCommands.Num() > 0)
	{
		TGuardValue<bool> ReadOnlyGuard(bReadOnlyCallbackInProgress, true);
		TGuardValue<bool> ObserverGuard(bObserverCallbackInProgress, true);
		OnCommandsProcessing.Broadcast(
			CurrentTick, PendingCommands.GetCommands());
	}

	// SNAPSHOT-AND-DRAIN: move the pending commands into a local working set,
	// leaving the buffer empty BEFORE we iterate. Critical because ability
	// OnActivate BPs (and effects, and broker dispatches) can enqueue NEW
	// commands during processing — e.g. SA_PlaceBarracks's OnActivate calls
	// SeinIssueAbilityCommand to chain into Build. Without the snapshot, those
	// new enqueues either iterator-invalidate the live PendingCommands array
	// or get wiped by the final Clear(), depending on whether TArray realloc
	// fires. With the snapshot, mid-processing enqueues land in the now-empty
	// PendingCommands and get processed cleanly on the next sim tick.
	const TArray<FSeinCommand> CommandsThisTick = PendingCommands.DrainCommands();

	// Reset the within-tick sequence consumed by the built-in BrokerOrder handler.
	// CurrentTick + this counter is the deterministic cohesion-group identity.
	CommandCohesionOrderSequence = 0;

	for (const FSeinCommand& Cmd : CommandsThisTick)
	{
		// Diagnostic trace — per-command handling. Verbose so it stays out of
		// default logs; pair with the entry-summary above when debugging.
		UE_LOG(LogSeinSim, Verbose,
			TEXT("ProcessCommands: handling type=%s entity=%s ability=%s"),
			*Cmd.CommandType.ToString(), *Cmd.EntityHandle.ToString(), *Cmd.AbilityTag.ToString());

		FSeinCommandSchemaDescriptor Schema;
		const ESeinCommandStructureResult StructureResult =
			CommandSchemaSnapshot.ValidateStructure(Cmd, &Schema);
		if (StructureResult != ESeinCommandStructureResult::Valid)
		{
			RejectCommand(Cmd, StructureResultToRejectionTag(StructureResult));
			continue;
		}
		DispatchValidatedCommand(Cmd, Schema);
	}

	// PendingCommands was cleared up-front (see snapshot-and-drain comment at
	// top of function). Any commands enqueued by abilities/effects DURING the
	// iteration above are sitting in PendingCommands now, queued for next tick.
}

void USeinWorldSubsystem::DispatchValidatedCommand(
	const FSeinCommand& Command,
	const FSeinCommandSchemaDescriptor& Schema)
{
	TOptional<FSeinCommand> CanonicalStorage;
	auto GetCanonicalCommand = [&]() -> const FSeinCommand&
	{
		return CanonicalStorage.IsSet() ? CanonicalStorage.GetValue() : Command;
	};
	FGameplayTag RejectionReason;
	if (!Schema.AllowedPayloadNames.IsEmpty())
	{
		CanonicalStorage.Emplace(Command);
		if (!SeinCanonicalizeCommandPayloadNames(
			CanonicalStorage.GetValue(), Schema))
		{
			RejectCommand(Command, SeinARTSTags::Command_Reject_Malformed);
			return;
		}
	}
	if (!IsCommandContextAllowed(GetCanonicalCommand(), Schema, RejectionReason))
	{
		RejectCommand(Command, RejectionReason);
		return;
	}

	// EntitySet commands are normalized once at the common dispatcher seam.
	// A custom handler must never receive foreign or duplicate recipients just
	// because its authority policy accepted one valid member of a mixed list.
	if (Schema.AuthorityScope == ESeinCommandAuthorityScope::EntitySet)
	{
		if (!CanonicalStorage.IsSet())
		{
			CanonicalStorage.Emplace(Command);
		}
		FSeinCommand& CanonicalCommand = CanonicalStorage.GetValue();
		TArray<FSeinEntityHandle> AuthorizedEntities;
		AuthorizedEntities.Reserve(CanonicalCommand.EntityList.Num());
		TSet<FSeinEntityHandle> SeenEntities;
		for (const FSeinEntityHandle Entity : CanonicalCommand.EntityList)
		{
			if (!SeenEntities.Contains(Entity)
				&& CommandAuthorityPolicy
				&& CommandAuthorityPolicy->CanControlEntity(
					CommandAuthorityView, CanonicalCommand, Entity))
			{
				SeenEntities.Add(Entity);
				AuthorizedEntities.Add(Entity);
			}
		}
		CanonicalCommand.EntityList = MoveTemp(AuthorizedEntities);
		if (CanonicalCommand.EntityList.IsEmpty())
		{
			RejectCommand(Command, SeinARTSTags::Command_Reject_Unauthorized);
			return;
		}
	}

	if (!CommandAuthorityPolicy
		|| !CommandAuthorityPolicy->AuthorizeCommand(
			CommandAuthorityView, GetCanonicalCommand(),
			Schema.AuthorityScope, RejectionReason))
	{
		RejectCommand(Command, RejectionReason.IsValid()
			? RejectionReason
			: SeinARTSTags::Command_Reject_Unauthorized);
		return;
	}

	const USeinCommandHandler* Handler = Schema.HandlerClass
		? Cast<USeinCommandHandler>(Schema.HandlerClass->GetDefaultObject())
		: nullptr;
	if (!Handler)
	{
		RejectCommand(Command, SeinARTSTags::Command_Reject_UnsupportedSchema);
		return;
	}

	RejectionReason = FGameplayTag();
	if (!Handler->ExecuteCommand(this, GetCanonicalCommand(), RejectionReason))
	{
		RejectCommand(Command, RejectionReason.IsValid()
			? RejectionReason
			: SeinARTSTags::Command_Reject_InvalidTarget);
	}
}

FSeinPauseControlCursor USeinWorldSubsystem::GetExpectedPauseControlCursor() const
{
	FSeinPauseControlCursor Cursor;
	Cursor.PauseEpoch = PauseEpoch;
	Cursor.Sequence = LastAppliedPauseControlSequence == MAX_int64
		? MAX_int64
		: LastAppliedPauseControlSequence + 1;
	Cursor.FrozenTick = PauseFrozenTick;
	return Cursor;
}

bool USeinWorldSubsystem::ResolvePauseControlFrame(FSeinPauseControlFrame& OutFrame)
{
	OutFrame = FSeinPauseControlFrame();
	if (PauseControlFrameResolver.IsBound())
	{
		return PauseControlFrameResolver.Execute(OutFrame);
	}
	if (PendingStandalonePauseControlCommands.IsEmpty())
	{
		return false;
	}

	OutFrame.Cursor = GetExpectedPauseControlCursor();
	OutFrame.Commands.Add(MoveTemp(PendingStandalonePauseControlCommands[0]));
	PendingStandalonePauseControlCommands.RemoveAt(
		0, 1, EAllowShrinking::No);
	return true;
}

bool USeinWorldSubsystem::PreflightPauseControlFrame(
	const FSeinPauseControlFrame& Frame,
	TArray<FSeinCommandSchemaDescriptor>& OutSchemas) const
{
	OutSchemas.Reset();
	if (!bSimPaused
		|| LastAppliedPauseControlSequence == MAX_int64
		|| Frame.Cursor != GetExpectedPauseControlCursor()
		|| Frame.Commands.IsEmpty()
		|| Frame.Commands.Num() > MaxPauseControlCommandsPerFrame)
	{
		return false;
	}

	OutSchemas.Reserve(Frame.Commands.Num());
	for (const FSeinCommand& Command : Frame.Commands)
	{
		FSeinCommandSchemaDescriptor Schema;
		if (Command.Tick != PauseFrozenTick
			|| CommandSchemaSnapshot.ValidateStructure(Command, &Schema)
				!= ESeinCommandStructureResult::Valid
			|| (Schema.AllowedExecutionContexts
				& static_cast<int32>(ESeinCommandExecutionAllowance::FrozenPauseControl)) == 0)
		{
			OutSchemas.Reset();
			return false;
		}
		OutSchemas.Add(MoveTemp(Schema));
	}
	return true;
}

void USeinWorldSubsystem::PumpPauseControlFrame()
{
	FSeinPauseControlFrame Frame;
	if (!ResolvePauseControlFrame(Frame))
	{
		return;
	}
	if (!ValidateFrozenConfigFingerprint()
		|| !ValidateFrozenCanonicalStateWorldBindings())
	{
		return;
	}

	TArray<FSeinCommandSchemaDescriptor> Schemas;
	if (!PreflightPauseControlFrame(Frame, Schemas))
	{
		UE_LOG(LogSeinSim, Error,
			TEXT("Rejected pause-control frame epoch=%lld sequence=%lld frozenTick=%d: protocol preflight failed."),
			Frame.Cursor.PauseEpoch, Frame.Cursor.Sequence, Frame.Cursor.FrozenTick);
		if (PauseControlAppliedNotifier.IsBound())
		{
			const FGuid InvalidCanonicalStateDigest;
			PauseControlAppliedNotifier.Execute(
				Frame.Cursor, bSimPaused, InvalidCanonicalStateDigest,
				/*bProtocolFailure=*/true);
		}
		return;
	}

	bool bProtocolFailure = false;
	{
		SEIN_SIM_SCOPE(*this)
		CommandCohesionOrderSequence = 0;
		bPauseControlDispatchProtocolViolation = false;
		TGuardValue<bool> FrameDispatchGuard(
			bDispatchingPauseControlFrame, true);
		TGuardValue<int32> CommandCountGuard(
			ActivePauseControlCommandCount, Frame.Commands.Num());
		for (int32 Index = 0; Index < Frame.Commands.Num(); ++Index)
		{
			TGuardValue<int32> CommandIndexGuard(
				ActivePauseControlCommandIndex, Index);
			DispatchValidatedCommand(Frame.Commands[Index], Schemas[Index]);
			if (bPauseControlDispatchProtocolViolation)
			{
				bProtocolFailure = true;
				break;
			}
		}
		if (!bProtocolFailure)
		{
			LastAppliedPauseControlSequence = Frame.Cursor.Sequence;
		}
	}

	if (PauseControlAppliedNotifier.IsBound())
	{
		FGuid CanonicalStateDigest;
		if (!bProtocolFailure)
		{
			FString RootError;
			if (!ComputeCanonicalStateRoot(
					CanonicalStateDigest, RootError))
			{
				UE_LOG(LogSeinSim, Error,
					TEXT("Pause-control frame epoch=%lld sequence=%lld applied, but canonical state-root capture failed: %s"),
					Frame.Cursor.PauseEpoch,
					Frame.Cursor.Sequence,
					*RootError);
			}
		}
		PauseControlAppliedNotifier.Execute(
			Frame.Cursor, bSimPaused, CanonicalStateDigest,
			bProtocolFailure);
	}
}

FGameplayTag USeinWorldSubsystem::StructureResultToRejectionTag(
	ESeinCommandStructureResult Result)
{
	switch (Result)
	{
	case ESeinCommandStructureResult::PayloadTooLarge:
	case ESeinCommandStructureResult::EntityListTooLarge:
	case ESeinCommandStructureResult::TargeterPointsTooLarge:
		return SeinARTSTags::Command_Reject_PayloadTooLarge;

	case ESeinCommandStructureResult::PayloadNameOutsideCatalog:
		return SeinARTSTags::Command_Reject_Malformed;

	case ESeinCommandStructureResult::InvalidSchemaVersion:
	case ESeinCommandStructureResult::UnknownCommandType:
	case ESeinCommandStructureResult::UnsupportedSchemaVersion:
		return SeinARTSTags::Command_Reject_UnsupportedSchema;

	case ESeinCommandStructureResult::Valid:
		return FGameplayTag();

	default:
		return SeinARTSTags::Command_Reject_Malformed;
	}
}

bool USeinWorldSubsystem::IsCommandContextAllowed(
	const FSeinCommand& Command,
	const FSeinCommandSchemaDescriptor& Schema,
	FGameplayTag& OutRejectionReason) const
{
	const auto HasAllowance = [&Schema](ESeinCommandExecutionAllowance Allowance)
	{
		return (Schema.AllowedExecutionContexts & static_cast<int32>(Allowance)) != 0;
	};

	if (const FSeinPlayerState* PlayerState = GetPlayerState(Command.PlayerID))
	{
		if (PlayerState->bIsSpectator
			&& !HasAllowance(ESeinCommandExecutionAllowance::Spectator))
		{
			OutRejectionReason = SeinARTSTags::Command_Reject_SpectatorForbidden;
			return false;
		}
	}
	if (bSimPausedHard && !HasAllowance(ESeinCommandExecutionAllowance::HardPause))
	{
		OutRejectionReason = SeinARTSTags::Command_Reject_SimPaused;
		return false;
	}
	if (MatchState == ESeinMatchState::Starting
		&& !HasAllowance(ESeinCommandExecutionAllowance::Starting))
	{
		OutRejectionReason = SeinARTSTags::Command_Reject_MatchStateInvalid;
		return false;
	}
	if ((Schema.AuthorityScope == ESeinCommandAuthorityScope::Entity
			|| Schema.AuthorityScope == ESeinCommandAuthorityScope::EntitySet)
		&& MatchState != ESeinMatchState::Playing
		&& MatchState != ESeinMatchState::Paused)
	{
		OutRejectionReason = SeinARTSTags::Command_Reject_MatchStateInvalid;
		return false;
	}
	const bool bActiveParticipantMatchCommand =
		Command.CommandType == SeinARTSTags::Command_Type_PauseMatchRequest
		|| Command.CommandType == SeinARTSTags::Command_Type_ResumeMatchRequest
		|| Command.CommandType == SeinARTSTags::Command_Type_ConcedeMatch
		|| Command.CommandType == SeinARTSTags::Command_Type_StartVote
		|| Command.CommandType == SeinARTSTags::Command_Type_CastVote;
	if (bActiveParticipantMatchCommand
		&& MatchState != ESeinMatchState::Starting
		&& MatchState != ESeinMatchState::Playing
		&& MatchState != ESeinMatchState::Paused)
	{
		OutRejectionReason = SeinARTSTags::Command_Reject_MatchStateInvalid;
		return false;
	}
	return true;
}

bool USeinWorldSubsystem::ExecuteBuiltInCommand(
	const FSeinCommand& Command,
	FGameplayTag& OutRejectionReason)
{
	if (!ValidateBuiltInCommandSemantics(Command, OutRejectionReason))
	{
		return false;
	}

	// Observer commands are deterministic replay records but deliberately have
	// no simulation-side effect.
	if (Command.IsObserverCommand())
	{
		return true;
	}
	if (TryHandleMatchFlowOrVoteCommand(Command) == ECommandHandleResult::Handled
		|| TryHandlePingCommand(Command) == ECommandHandleResult::Handled
		|| TryHandleSetPairCapabilityCommand(Command) == ECommandHandleResult::Handled
		|| TryHandleBrokerOrderCommand(
			Command, CommandCohesionOrderSequence) == ECommandHandleResult::Handled)
	{
		return true;
	}

	if (!EntityPool.IsValid(Command.EntityHandle))
	{
		OutRejectionReason = SeinARTSTags::Command_Reject_MissingComponent;
		return false;
	}
	if (TryHandleActivateAbilityCommand(Command) == ECommandHandleResult::Handled
		|| TryHandleCancelAbilityCommand(Command) == ECommandHandleResult::Handled
		|| TryHandleCancelProductionCommand(Command) == ECommandHandleResult::Handled)
	{
		return true;
	}

	OutRejectionReason = SeinARTSTags::Command_Reject_UnsupportedSchema;
	return false;
}

bool USeinWorldSubsystem::ValidateMatchSettings(
	const FSeinMatchSettings& Settings,
	FGameplayTag& OutRejectionReason) const
{
	OutRejectionReason = FGameplayTag();
	const USeinARTSCoreSettings* CoreSettings =
		GetDefault<USeinARTSCoreSettings>();
	const int32 MaxSlots = CoreSettings
		? FMath::Clamp(CoreSettings->MaxPlayers, 1, 16)
		: 16;
	if (Settings.Slots.Num() > MaxSlots)
	{
		OutRejectionReason = SeinARTSTags::Command_Reject_Malformed;
		return false;
	}

	TSet<int32> SlotIndices;
	for (const FSeinMatchSlot& Slot : Settings.Slots)
	{
		const bool bValidState = StaticEnum<ESeinSlotState>()->IsValidEnumValue(
			static_cast<int64>(Slot.State));
		// Factions are an OPT-IN catalog: a project with no authored faction
		// assets legitimately fields occupied slots with the invalid/zero
		// FactionID (the sim registers such players as Faction(0), and the
		// value participates in the canonical settings digest either way).
		// Requiring a valid faction here would refuse bootstrap for every
		// factionless project; faction-required is a game-mode policy, not a
		// core invariant.
		if (!bValidState || Slot.SlotIndex < 1 || Slot.SlotIndex > MaxSlots
			|| SlotIndices.Contains(Slot.SlotIndex))
		{
			OutRejectionReason = SeinARTSTags::Command_Reject_Malformed;
			return false;
		}
		SlotIndices.Add(Slot.SlotIndex);
	}

	const TConstArrayView<const UScriptStruct*> AllowedExtensions =
		CommandSchemaSnapshot.GetAdditionalDynamicPayloadStructs();
	TSet<FString> ExtensionTypes;
	for (const FInstancedStruct& Extension : Settings.Extensions)
	{
		const UScriptStruct* Type = Extension.GetScriptStruct();
		const FString TypePath = Type ? Type->GetPathName() : FString();
		if (!Extension.IsValid() || !Type || !Extension.GetMemory()
			|| !AllowedExtensions.Contains(Type)
			|| ExtensionTypes.Contains(TypePath))
		{
			OutRejectionReason = SeinARTSTags::Command_Reject_Malformed;
			return false;
		}
		ExtensionTypes.Add(TypePath);
	}

	if (const FSeinMatchBootstrapRules* Rules =
		FindMatchExtension<FSeinMatchBootstrapRules>(Settings))
	{
		TSet<FGameplayTag> CatalogTags;
		if (CoreSettings)
		{
			for (const FSeinResourceDefinition& Definition :
				CoreSettings->ResourceCatalog)
			{
				if (Definition.ResourceTag.IsValid())
				{
					CatalogTags.Add(Definition.ResourceTag);
				}
			}
		}
		TSet<FGameplayTag> SeenOverrides;
		for (const FSeinStartingResourceOverride& Override :
			Rules->StartingResources)
		{
			if (!Override.ResourceTag.IsValid()
				|| !CatalogTags.Contains(Override.ResourceTag)
				|| SeenOverrides.Contains(Override.ResourceTag))
			{
				OutRejectionReason = SeinARTSTags::Command_Reject_Malformed;
				return false;
			}
			SeenOverrides.Add(Override.ResourceTag);
		}
	}

	FSeinMatchSettings Canonical = Settings;
	FGuid Digest;
	if (!SeinCanonicalizeAndDigestMatchSettings(Canonical, Digest))
	{
		OutRejectionReason = SeinARTSTags::Command_Reject_Malformed;
		return false;
	}
	FSeinDeterministicValueDigestOptions BoundedOptions =
		MakeRuntimeDigestOptions();
	BoundedOptions.MaxEncodedBytes = 256ULL * 1024ULL;
	BoundedOptions.MaxAggregateElements = 4096;
	FSeinDeterministicValueDigestError DigestError;
	if (FSeinDeterministicValueDigest::Compute(
		FSeinMatchSettings::StaticStruct(), &Canonical, Digest,
		&DigestError, BoundedOptions)
		!= ESeinDeterministicValueDigestResult::Success)
	{
		OutRejectionReason = SeinARTSTags::Command_Reject_Malformed;
		return false;
	}
	return true;
}

bool USeinWorldSubsystem::ValidateBuiltInCommandSemantics(
	const FSeinCommand& Command,
	FGameplayTag& OutRejectionReason) const
{
	const auto FailMalformed = [&OutRejectionReason]()
	{
		OutRejectionReason = SeinARTSTags::Command_Reject_Malformed;
		return false;
	};
	const auto IsZeroVector = [](const FFixedVector& Value)
	{
		return Value == FFixedVector::ZeroVector;
	};
	const auto HasDefaultAuxiliaryFields = [&]()
	{
		return !Command.bQueueCommand
			&& IsZeroVector(Command.AuxLocation)
			&& Command.TargeterPoints.IsEmpty()
			&& Command.AuxA == FFixedPoint::Zero
			&& Command.AuxB == FFixedPoint::Zero
			&& Command.EntityList.IsEmpty()
			&& Command.ActiveFocusIndex == INDEX_NONE;
	};
	const auto HasNoActionEnvelope = [&]()
	{
		return !Command.EntityHandle.IsValid()
			&& !Command.AbilityTag.IsValid()
			&& !Command.TargetEntity.IsValid()
			&& IsZeroVector(Command.TargetLocation)
			&& Command.QueueIndex == INDEX_NONE
			&& HasDefaultAuxiliaryFields();
	};
	const auto HasUniqueEntities = [](const TArray<FSeinEntityHandle>& Entities)
	{
		TSet<FSeinEntityHandle> Seen;
		for (const FSeinEntityHandle Entity : Entities)
		{
			if (Seen.Contains(Entity))
			{
				return false;
			}
			Seen.Add(Entity);
		}
		return true;
	};

	OutRejectionReason = FGameplayTag();
	if (Command.DerivedResourcePayer.IsValid()
		&& (Command.IssuerKind != ESeinCommandIssuerKind::DeterministicSystem
			|| Command.CommandType != SeinARTSTags::Command_Type_ActivateAbility))
	{
		return FailMalformed();
	}

	if (Command.CommandType == SeinARTSTags::Command_Type_ActivateAbility)
	{
		return Command.EntityHandle.IsValid()
			&& Command.AbilityTag.IsValid()
			&& Command.QueueIndex == INDEX_NONE
			&& !Command.bQueueCommand
			&& IsZeroVector(Command.AuxLocation)
			&& Command.AuxA == FFixedPoint::Zero
			&& Command.AuxB == FFixedPoint::Zero
			&& Command.EntityList.IsEmpty()
			&& Command.ActiveFocusIndex == INDEX_NONE
			? true : FailMalformed();
	}
	if (Command.CommandType == SeinARTSTags::Command_Type_CancelAbility)
	{
		return Command.EntityHandle.IsValid()
			&& !Command.AbilityTag.IsValid()
			&& !Command.TargetEntity.IsValid()
			&& IsZeroVector(Command.TargetLocation)
			&& Command.QueueIndex == INDEX_NONE
			&& HasDefaultAuxiliaryFields()
			? true : FailMalformed();
	}
	if (Command.CommandType == SeinARTSTags::Command_Type_CancelProduction)
	{
		return Command.EntityHandle.IsValid()
			&& Command.QueueIndex >= 0
			&& !Command.AbilityTag.IsValid()
			&& !Command.TargetEntity.IsValid()
			&& IsZeroVector(Command.TargetLocation)
			&& HasDefaultAuxiliaryFields()
			? true : FailMalformed();
	}
	if (Command.CommandType == SeinARTSTags::Command_Type_Ping)
	{
		return !Command.EntityHandle.IsValid()
			&& !Command.AbilityTag.IsValid()
			&& Command.QueueIndex == INDEX_NONE
			&& HasDefaultAuxiliaryFields()
			? true : FailMalformed();
	}
	if (Command.CommandType == SeinARTSTags::Command_Type_BrokerOrder)
	{
		const FSeinBrokerOrderPayload* Payload =
			Command.Payload.GetPtr<FSeinBrokerOrderPayload>();
		const bool bHasIntent = Payload
			&& (!Payload->CommandContext.IsEmpty()
				|| Payload->PredeterminedAbilityTag.IsValid());
		return !Command.EntityHandle.IsValid()
			&& !Command.AbilityTag.IsValid()
			&& Command.QueueIndex == INDEX_NONE
			&& IsZeroVector(Command.AuxLocation)
			&& Command.TargeterPoints.IsEmpty()
			&& Command.AuxA == FFixedPoint::Zero
			&& Command.AuxB == FFixedPoint::Zero
			&& Command.ActiveFocusIndex == INDEX_NONE
			&& !Command.EntityList.IsEmpty()
			&& bHasIntent
			&& Payload->GuidePoints.Num() <= 4096
			&& Payload->TargeterPoints.Num() <= 256
			&& (Payload->TargeterPoints.IsEmpty()
				|| Payload->PredeterminedAbilityTag.IsValid())
			? true : FailMalformed();
	}
	if (Command.CommandType
		== SeinARTSTags::Command_Type_SetPairCapability)
	{
		const FSeinSetPairCapabilityCommandPayload* Payload =
			Command.Payload.GetPtr<FSeinSetPairCapabilityCommandPayload>();
		return Payload
			&& Payload->SourcePlayer.IsValid()
			&& Payload->TargetPlayer.IsValid()
			&& Payload->SourcePlayer != Payload->TargetPlayer
			&& IsPairCapabilityTag(Payload->CapabilityTag)
			&& IsPairCapabilitySourceKindTag(Payload->SourceKindTag)
			&& Payload->SourceInstanceID > 0
			&& HasNoActionEnvelope()
			? true : FailMalformed();
	}

	if (Command.CommandType == SeinARTSTags::Command_Type_EndMatch
		|| Command.CommandType == SeinARTSTags::Command_Type_PauseMatchRequest
		|| Command.CommandType == SeinARTSTags::Command_Type_ResumeMatchRequest
		|| Command.CommandType == SeinARTSTags::Command_Type_ConcedeMatch)
	{
		return HasNoActionEnvelope() ? true : FailMalformed();
	}
	if (Command.CommandType == SeinARTSTags::Command_Type_StartVote)
	{
		const FSeinStartVoteCommandPayload* Payload =
			Command.Payload.GetPtr<FSeinStartVoteCommandPayload>();
		return Payload
			&& Command.AbilityTag.IsValid()
			&& !Command.EntityHandle.IsValid()
			&& !Command.TargetEntity.IsValid()
			&& IsZeroVector(Command.TargetLocation)
			&& Command.QueueIndex == INDEX_NONE
			&& HasDefaultAuxiliaryFields()
			&& StaticEnum<ESeinVoteResolution>()->IsValidEnumValue(
				static_cast<int64>(Payload->Resolution))
			&& Payload->RequiredThreshold > 0
			&& (Payload->ExpiresInTicks <= 0
				|| Payload->ExpiresInTicks <= MAX_int32 - CurrentTick)
			? true : FailMalformed();
	}
	if (Command.CommandType == SeinARTSTags::Command_Type_CastVote)
	{
		return Command.AbilityTag.IsValid()
			&& !Command.EntityHandle.IsValid()
			&& !Command.TargetEntity.IsValid()
			&& IsZeroVector(Command.TargetLocation)
			&& (Command.QueueIndex == 0 || Command.QueueIndex == 1)
			&& HasDefaultAuxiliaryFields()
			? true : FailMalformed();
	}

	if (Command.CommandType == SeinARTSTags::Command_Type_Observer_CameraUpdate)
	{
		return !Command.EntityHandle.IsValid()
			&& !Command.AbilityTag.IsValid()
			&& !Command.TargetEntity.IsValid()
			&& Command.QueueIndex == INDEX_NONE
			&& Command.TargeterPoints.IsEmpty()
			&& Command.EntityList.IsEmpty()
			&& Command.ActiveFocusIndex == INDEX_NONE
			&& Command.AuxLocation.Y == FFixedPoint::Zero
			&& Command.AuxLocation.Z == FFixedPoint::Zero
			? true : FailMalformed();
	}
	const bool bSelectionReplace =
		Command.CommandType == SeinARTSTags::Command_Type_Observer_SelectionChanged
		|| Command.CommandType == SeinARTSTags::Command_Type_Observer_Selection_Replaced;
	const bool bSelectionDelta =
		Command.CommandType == SeinARTSTags::Command_Type_Observer_Selection_Added
		|| Command.CommandType == SeinARTSTags::Command_Type_Observer_Selection_Removed;
	if (bSelectionReplace || bSelectionDelta)
	{
		const bool bValidFocus = bSelectionReplace
			? (Command.ActiveFocusIndex == INDEX_NONE
				|| Command.EntityList.IsValidIndex(Command.ActiveFocusIndex))
			: Command.ActiveFocusIndex == INDEX_NONE;
		return !Command.EntityHandle.IsValid()
			&& !Command.AbilityTag.IsValid()
			&& !Command.TargetEntity.IsValid()
			&& IsZeroVector(Command.TargetLocation)
			&& Command.QueueIndex == INDEX_NONE
			&& !Command.bQueueCommand
			&& IsZeroVector(Command.AuxLocation)
			&& Command.TargeterPoints.IsEmpty()
			&& Command.AuxA == FFixedPoint::Zero
			&& Command.AuxB == FFixedPoint::Zero
			&& bValidFocus
			&& HasUniqueEntities(Command.EntityList)
			? true : FailMalformed();
	}
	const bool bControlGroupWrite =
		Command.CommandType == SeinARTSTags::Command_Type_Observer_ControlGroup_Assigned
		|| Command.CommandType == SeinARTSTags::Command_Type_Observer_ControlGroup_AddedTo;
	if (bControlGroupWrite
		|| Command.CommandType == SeinARTSTags::Command_Type_Observer_ControlGroup_Selected)
	{
		return !Command.EntityHandle.IsValid()
			&& !Command.AbilityTag.IsValid()
			&& !Command.TargetEntity.IsValid()
			&& IsZeroVector(Command.TargetLocation)
			&& Command.QueueIndex >= 0
			&& Command.QueueIndex <= 9
			&& !Command.bQueueCommand
			&& IsZeroVector(Command.AuxLocation)
			&& Command.TargeterPoints.IsEmpty()
			&& Command.AuxA == FFixedPoint::Zero
			&& Command.AuxB == FFixedPoint::Zero
			&& Command.ActiveFocusIndex == INDEX_NONE
			&& (bControlGroupWrite || Command.EntityList.IsEmpty())
			&& HasUniqueEntities(Command.EntityList)
			? true : FailMalformed();
	}

	OutRejectionReason = SeinARTSTags::Command_Reject_UnsupportedSchema;
	return false;
}

void USeinWorldSubsystem::RejectCommand(const FSeinCommand& Cmd, FGameplayTag Reason)
{
	EnqueueVisualEvent(FSeinVisualEvent::MakeCommandRejectedEvent(
		Cmd.PlayerID, Cmd.EntityHandle, Cmd.CommandType, Reason));
}

USeinWorldSubsystem::ECommandHandleResult USeinWorldSubsystem::TryHandleMatchFlowOrVoteCommand(
	const FSeinCommand& Cmd)
{
	// Match-flow commands have no entity target. Common structural, context, and
	// authority gates have already run; Resume may also arrive through the frozen
	// pause-control lane when its schema explicitly carries that allowance.
	if (Cmd.CommandType == SeinARTSTags::Command_Type_EndMatch)
	{
		const FSeinEndMatchCommandPayload* Payload =
			Cmd.Payload.GetPtr<FSeinEndMatchCommandPayload>();
		if (!Payload)
		{
			RejectCommand(Cmd, SeinARTSTags::Command_Reject_Malformed);
			return ECommandHandleResult::Handled;
		}
		EndMatch(Payload->Winner, Payload->Reason);
		return ECommandHandleResult::Handled;
	}
	if (Cmd.CommandType == SeinARTSTags::Command_Type_PauseMatchRequest)
	{
		// Tactical-style pause by default (commands queue + drain). Designer
		// wanting Hard-pause behavior can either: (1) call SetSimPaused
		// directly with bRejectCommandsWhilePaused=true from BP, or (2)
		// reject input at PC layer during pause so commands never reach
		// the wire.
		SetSimPaused(true, /*bRejectCommandsWhilePaused=*/false);
		return ECommandHandleResult::Handled;
	}
	if (Cmd.CommandType == SeinARTSTags::Command_Type_ResumeMatchRequest)
	{
		SetSimPaused(false);
		return ECommandHandleResult::Handled;
	}
	if (Cmd.CommandType == SeinARTSTags::Command_Type_ConcedeMatch)
	{
		// V1: concede immediately ends the match with no-winner. Designers
		// who want per-team victory / last-player-standing wire their own
		// scenario + call EndMatch with the right winner.
		EndMatch(FSeinPlayerID::Neutral(), SeinARTSTags::Command_Type_ConcedeMatch);
		return ECommandHandleResult::Handled;
	}
	// Vote commands (DESIGN §18 voting primitive).
	if (Cmd.CommandType == SeinARTSTags::Command_Type_StartVote)
	{
		FSeinStartVoteCommandPayload Pay;
		if (Cmd.Payload.IsValid() && Cmd.Payload.GetScriptStruct() == FSeinStartVoteCommandPayload::StaticStruct())
		{
			Pay = Cmd.Payload.Get<FSeinStartVoteCommandPayload>();
		}
		StartVote(Cmd.AbilityTag, Pay.Resolution, Pay.RequiredThreshold, Pay.ExpiresInTicks, Cmd.PlayerID);
		return ECommandHandleResult::Handled;
	}
	if (Cmd.CommandType == SeinARTSTags::Command_Type_CastVote)
	{
		CastVote(Cmd.AbilityTag, Cmd.PlayerID, Cmd.QueueIndex);
		return ECommandHandleResult::Handled;
	}

	return ECommandHandleResult::Unhandled;
}

USeinWorldSubsystem::ECommandHandleResult USeinWorldSubsystem::TryHandlePingCommand(
	const FSeinCommand& Cmd)
{
	// Ping commands don't require a valid entity — emit a visual event and continue.
	if (Cmd.CommandType == SeinARTSTags::Command_Type_Ping)
	{
		EnqueueVisualEvent(FSeinVisualEvent::MakePingEvent(
			Cmd.PlayerID, Cmd.TargetLocation, Cmd.TargetEntity));
		return ECommandHandleResult::Handled;
	}

	return ECommandHandleResult::Unhandled;
}

USeinWorldSubsystem::ECommandHandleResult
USeinWorldSubsystem::TryHandleSetPairCapabilityCommand(
	const FSeinCommand& Cmd)
{
	if (Cmd.CommandType != SeinARTSTags::Command_Type_SetPairCapability)
	{
		return ECommandHandleResult::Unhandled;
	}
	if (!Cmd.Payload.IsValid()
		|| Cmd.Payload.GetScriptStruct()
			!= FSeinSetPairCapabilityCommandPayload::StaticStruct())
	{
		RejectCommand(Cmd, SeinARTSTags::Command_Reject_Malformed);
		return ECommandHandleResult::Handled;
	}

	const FSeinSetPairCapabilityCommandPayload& Payload =
		Cmd.Payload.Get<FSeinSetPairCapabilityCommandPayload>();
	const bool bApplied = Payload.bGrant
		? GrantPairCapability(
			Payload.SourcePlayer,
			Payload.TargetPlayer,
			Payload.CapabilityTag,
			Payload.SourceKindTag,
			Payload.SourceInstanceID)
		: RevokePairCapability(
			Payload.SourcePlayer,
			Payload.TargetPlayer,
			Payload.CapabilityTag,
			Payload.SourceKindTag,
			Payload.SourceInstanceID);
	if (!bApplied)
	{
		RejectCommand(Cmd, SeinARTSTags::Command_Reject_Malformed);
	}
	return ECommandHandleResult::Handled;
}

USeinWorldSubsystem::ECommandHandleResult USeinWorldSubsystem::TryHandleBrokerOrderCommand(
	const FSeinCommand& Cmd, int32& CohesionOrderSeq)
{
	if (Cmd.CommandType != SeinARTSTags::Command_Type_BrokerOrder)
	{
		return ECommandHandleResult::Unhandled;
	}

	if (Cmd.EntityList.Num() == 0)
	{
		RejectCommand(Cmd, SeinARTSTags::Command_Reject_InvalidTarget);
		return ECommandHandleResult::Handled;
	}

	// Stable-filter the mixed recipient set through the selected policy. The
	// common EntitySet gate proves that at least one member is controllable;
	// this per-member pass prevents a valid grant from smuggling foreign members
	// through the same envelope. Preserve input order and collapse duplicates.
	TArray<FSeinEntityHandle> Filtered;
	Filtered.Reserve(Cmd.EntityList.Num());
	TSet<FSeinEntityHandle> Seen;
	for (const FSeinEntityHandle& M : Cmd.EntityList)
	{
		if (!EntityPool.IsValid(M) || Seen.Contains(M)) continue;
		Seen.Add(M);
		if (CommandAuthorityPolicy
			&& CommandAuthorityPolicy->CanControlEntity(
				CommandAuthorityView, Cmd, M))
		{
			Filtered.Add(M);
		}
	}
	if (Filtered.Num() == 0)
	{
		RejectCommand(Cmd, SeinARTSTags::Command_Reject_InvalidTarget);
		return ECommandHandleResult::Handled;
	}

	// Extract the typed BrokerOrder payload — smart-resolution context +
	// drag-order endpoint. Missing payload is a malformed command.
	const FSeinBrokerOrderPayload* Payload = Cmd.Payload.GetPtr<FSeinBrokerOrderPayload>();
	if (!Payload)
	{
		RejectCommand(Cmd, SeinARTSTags::Command_Reject_InvalidTarget);
		return ECommandHandleResult::Handled;
	}

	// Resolve the predetermined ability ONCE — both cost + footprint
	// gates need the same lookup (first capable member's instance of
	// the named ability). Smart-resolved orders (no PredeterminedAbilityTag)
	// leave this null and skip both gates; their per-member cost across
	// heterogeneous selections is not well-defined for "one click" semantics.
	const USeinAbility* PredeterminedAbility = nullptr;
	if (Payload->PredeterminedAbilityTag.IsValid())
	{
		for (const FSeinEntityHandle& M : Filtered)
		{
			const FSeinAbilityComponent* MemberAC = GetComponent<FSeinAbilityComponent>(M);
			if (!MemberAC) continue;
			if (USeinAbility* Found = MemberAC->FindAbilityByTag(*this, Payload->PredeterminedAbilityTag))
			{
				PredeterminedAbility = Found;
				break;
			}
		}
	}

	// Cost gate REMOVED (refactored 2026-05-07 alongside broker dispatch
	// going through ProcessCommands' full gate). Previously the broker-
	// order branch deducted cost up front for targeter-originated orders
	// and the per-member dispatch in SeinCommandBrokerSystem skipped
	// cost. Now per-member dispatch enqueues ActivateAbility commands
	// that go through the full activation gate (including cost deduct)
	// — keeping the cost gate here would double-charge.
	//
	// Affordability pre-check at click time (so the player can't
	// over-issue) belongs at the UI layer (HUD button greying), not
	// here. The activation gate rejects with Unaffordable if the
	// player runs out by the time the per-member command processes.

	// Footprint placement gate — only meaningful for targeter-originated
	// orders (have PredeterminedAbilityTag + TargeterPoints). Reuses
	// the PredeterminedAbility resolved above. Skip silently if any
	// precondition fails: no predetermined ability, no points, no
	// capable member, no spec, no extents on the building. This keeps
	// the gate opt-in and additive — abilities that don't set
	// bRequiresFreeFootprint are unaffected.
	if (PredeterminedAbility && Payload->TargeterPoints.Num() > 0
		&& FootprintPlacementResolver.IsBound()
		&& PredeterminedAbility->bRequiresFreeFootprint)
	{
		// Pull the spec to get BuildingClass, then read extents from CDO.
		// Only USeinPointFacingTargeterSpec carries a BuildingClass; other
		// specs silently bypass.
		const USeinPointFacingTargeterSpec* PFSpec =
			Cast<USeinPointFacingTargeterSpec>(PredeterminedAbility->TargeterSpec);
		const FSeinExtentsShape* Shape = nullptr;
		if (PFSpec && !PFSpec->BuildingClass.IsNull())
		{
			UClass* BuildingClass = PFSpec->BuildingClass.LoadSynchronous();
			Shape = SeinExtentsHelpers::GetPrimaryExtentsShape(BuildingClass);
		}

		if (Shape)
		{
			const FSeinTargeterPoint& First = Payload->TargeterPoints[0];

			// YawDegrees is the authoritative captured pose for both snapped and
			// free rotation. RotationStep is only gesture/UI metadata.
			const FFixedPoint YawDeg = First.YawDegrees;

			// AgentLayerMask: blocking-perspective bit. We don't have an
			// agent here (placing a building, not pathing through one).
			// Use 0xFF "block on any layer" so any blocker rejects placement.
			const uint8 AgentLayerMask = 0xFF;

			if (!FootprintPlacementResolver.Execute(First.Location, YawDeg, *Shape, AgentLayerMask))
			{
				UE_LOG(LogSeinSim, Warning,
					TEXT("BrokerOrder[%s]: footprint blocked at (%.1f, %.1f, %.1f) yaw=%.1f"),
					*Payload->PredeterminedAbilityTag.ToString(),
					First.Location.X.ToFloat(), First.Location.Y.ToFloat(), First.Location.Z.ToFloat(),
					YawDeg.ToFloat());
				RejectCommand(Cmd, SeinARTSTags::Command_Reject_FootprintBlocked);
				return ECommandHandleResult::Handled;
			}
		}
		// Else: ability requires footprint check but we couldn't resolve
		// a shape — log Verbose and let the order through. Designer
		// either forgot to set BuildingClass or the BP has no extents
		// component; failing closed here would block legitimate-looking
		// orders during authoring iteration.
		else
		{
			UE_LOG(LogSeinSim, Verbose,
				TEXT("BrokerOrder[%s]: bRequiresFreeFootprint set but no shape resolved (spec or BuildingClass missing); skipping gate."),
				*Payload->PredeterminedAbilityTag.ToString());
		}
	}

	FSeinBrokerQueuedOrder Order;
	Order.Context = Payload->CommandContext;
	Order.TargetEntity = Cmd.TargetEntity;
	Order.TargetLocation = Cmd.TargetLocation;
	Order.FormationEnd = Payload->FormationEnd;
	Order.GuidePoints = Payload->GuidePoints;
	Order.FormationTag = Payload->FormationTag;
	Order.TargeterPoints = Payload->TargeterPoints;
	Order.PredeterminedAbilityTag = Payload->PredeterminedAbilityTag;

	// Snap pure location-targets (no entity click) to the nearest
	// nav-passable cell. Without this, a click that lands on a non-
	// walkable surface (wall side, building roof, water mesh — anything
	// the cursor trace stops on whose footprint is bake-blocked) routes
	// downstream as TargetLocation-on-a-blocked-cell. Per-member FindPath
	// then either rejects the command (bRequiresPathableTarget) or
	// returns a degenerate path, producing the "click on wall, nobody
	// moves" failure mode. Snap to nearest pathable matches the standard
	// RTS contract: a move order always commits to *somewhere* reachable.
	//
	// Entity-targeted orders skip the snap — the resolver dispatches
	// against the entity (Attack X, Repair Y), TargetLocation is only
	// a fallback for resolvers that need a click anchor, and snapping
	// it away from the clicked entity would be actively wrong.
	//
	// Sim-side rather than PC-side so AI-issued orders get the same
	// safety net, and so the snap is deterministic (same nav data on
	// every client). Bypass on no-nav (tests / nav-less games).
	if (!Cmd.TargetEntity.IsValid() && NavProjectResolver.IsBound())
	{
		FFixedVector Projected;
		if (NavProjectResolver.Execute(Order.TargetLocation, Projected))
		{
			Order.TargetLocation = Projected;
		}
		// Formation-drag endpoint gets the same treatment so the
		// formation line's far end doesn't land on a wall.
		if (!Order.FormationEnd.IsNearlyZero())
		{
			FFixedVector ProjectedEnd;
			if (NavProjectResolver.Execute(Order.FormationEnd, ProjectedEnd))
			{
				Order.FormationEnd = ProjectedEnd;
			}
		}
		// Gesture guide path: project each point so the formation lays out on
		// reachable cells (same contract as TargetLocation / FormationEnd).
		for (FFixedVector& GuidePoint : Order.GuidePoints)
		{
			FFixedVector ProjectedGuide;
			if (NavProjectResolver.Execute(GuidePoint, ProjectedGuide))
			{
				GuidePoint = ProjectedGuide;
			}
		}
	}

	// Persistent-broker partitioning: any selected entity that ITSELF carries
	// FSeinCommandBrokerData is a persistent broker (squad / scenario-owned).
	// Append the order directly to its OrderQueue rather than wrapping it
	// in an ephemeral broker — persistent brokers are sub-brokers from the
	// player POV (one entity), and wrapping would create a meaningless
	// top-level broker whose only "member" is itself a broker. Entities
	// without FSeinCommandBrokerData flow through the existing ephemeral path.
	TArray<FSeinEntityHandle> PersistentBrokerEntities;
	TArray<FSeinEntityHandle> EphemeralEntities;
	PersistentBrokerEntities.Reserve(Filtered.Num());
	EphemeralEntities.Reserve(Filtered.Num());
	for (const FSeinEntityHandle& M : Filtered)
	{
		if (HasComponent<FSeinCommandBrokerData>(M)) { PersistentBrokerEntities.Add(M); }
		else                                          { EphemeralEntities.Add(M); }
	}

	// Persistent broker path — route the order to each broker's queue.
	// Each persistent broker gets its OWN copy of the Order (so per-broker
	// mutations to TargetMembers don't bleed across brokers). Order applies
	// to all of that broker's members (TargetMembers left empty = all).
	//
	// Replace vs. append by `Cmd.bQueueCommand`:
	//  - bQueueCommand == false (default right-click): clear pending orders
	//    AND explicitly cancel each member's active ability so in-flight
	//    work stops unconditionally (doesn't rely on tag-pair self-cancel
	//    pattern at the ability level).
	//  - bQueueCommand == true (shift-click): append, executing the new
	//    order after the current one finishes.
	//
	// A2: ONE unified formation over the WHOLE selection (squads sized by FormationRadius +
	// loose units sized by their footprint, co-equal elements) so a mixed selection forms a
	// SINGLE shape instead of a squad-formation and a loose-formation overlapping. Each squad
	// takes its element position as its anchor; each loose unit's element position becomes a
	// pre-placed goal for the ephemeral broker below. SAME helper the preview calls so
	// preview == commit.
	TArray<FFixedQuaternion> ElementFacings;
	const TArray<FFixedVector> ElementPositions =
		USeinCommandBrokerBPFL::ComputeMultiBrokerAnchors(
			*this, Filtered, Order.TargetLocation, Order.GuidePoints, Order.FormationTag, ElementFacings);
	TMap<FSeinEntityHandle, int32> ElementIndex;
	ElementIndex.Reserve(Filtered.Num());
	for (int32 i = 0; i < Filtered.Num(); ++i) { ElementIndex.Add(Filtered[i], i); }

	if (PersistentBrokerEntities.Num() > 0)
	{
		for (const FSeinEntityHandle& BrokerHandle : PersistentBrokerEntities)
		{
			FSeinCommandBrokerData* PersistentBroker =
				GetComponentMutable<FSeinCommandBrokerData>(
					BrokerHandle);
			if (!PersistentBroker) { continue; }
			const TArray<FSeinEntityHandle> BrokerMembers = PersistentBroker->Members;

			const int32* EidxPtr = ElementIndex.Find(BrokerHandle);
			const int32 Eidx = EidxPtr ? *EidxPtr : INDEX_NONE;
			const FFixedVector BrokerAnchor = ElementPositions.IsValidIndex(Eidx)
				? ElementPositions[Eidx] : Order.TargetLocation;

			if (!Cmd.bQueueCommand)
			{
				for (const FSeinEntityHandle& Member : BrokerMembers)
				{
					if (!IsEntityAlive(Member)) continue;
					FSeinAbilityComponent* MemberAC =
						GetComponentMutable<FSeinAbilityComponent>(
							Member);
					if (!MemberAC) continue;
					const int32 ActiveID = MemberAC->ActiveAbilityID;
					USeinAbility* Active = GetAbilityInstance(ActiveID);
					if (MemberAC->AbilityInstanceIDs.Contains(ActiveID)
						&& Active
						&& Active->OwnerEntity == Member)
					{
						if (Active->bIsActive)
						{
							Active->CancelAbility();
						}
					}
				}
			}

			// Cancellation callbacks may replace or destroy broker storage.
			PersistentBroker = IsEntityAlive(BrokerHandle)
				? GetComponentMutable<FSeinCommandBrokerData>(BrokerHandle)
				: nullptr;
			if (!PersistentBroker) { continue; }

			// Element facing (radial in a ring, drag-perp in a box) hands the squad its
			// orientation; same value the preview computed so preview == commit. (Limitation:
			// AnchorFacing is one field, so formation orders shift-queued in one tick share it.)
			if (ElementFacings.IsValidIndex(Eidx)) { PersistentBroker->AnchorFacing = ElementFacings[Eidx]; }

			if (!Cmd.bQueueCommand)
			{
				PersistentBroker->OrderQueue.Reset();
				PersistentBroker->CurrentOrderContext = FGameplayTagContainer();
			}

			FSeinBrokerQueuedOrder MyOrder = Order;
			MyOrder.TargetLocation = BrokerAnchor;
			PersistentBroker->OrderQueue.Add(MyOrder);
		}
	}
	// Ephemeral-units path — original ephemeral-broker logic, applied only
	// to entities without persistent brokers. If the selection was all
	// persistent brokers, this is empty and the block no-ops.
	if (EphemeralEntities.Num() > 0)
	{
		// A2: feed the loose units' element positions (from the unified formation above) as
		// pre-placed goals so the default resolver dispatches each to its slot in the SINGLE
		// shape rather than solving a second, overlapping formation. Set on Order here (after
		// the squad loop) so the squad copies stayed pre-placed-free.
		Order.PreplacedMembers.Reset();
		Order.PreplacedPositions.Reset();
		Order.PreplacedMembers.Reserve(EphemeralEntities.Num());
		Order.PreplacedPositions.Reserve(EphemeralEntities.Num());
		for (const FSeinEntityHandle& E : EphemeralEntities)
		{
			const int32* EidxPtr = ElementIndex.Find(E);
			const int32 Eidx = EidxPtr ? *EidxPtr : INDEX_NONE;
			Order.PreplacedMembers.Add(E);
			Order.PreplacedPositions.Add(ElementPositions.IsValidIndex(Eidx) ? ElementPositions[Eidx] : Order.TargetLocation);
		}
		FSeinEntityHandle ExistingBroker;
		if (Cmd.bQueueCommand)
		{
			ExistingBroker = FindSharedBroker(EphemeralEntities);
		}
		if (ExistingBroker.IsValid())
		{
			if (FSeinCommandBrokerData* Broker =
				GetComponentMutable<FSeinCommandBrokerData>(
					ExistingBroker))
			{
				// Strict subset? Order is TargetMembers-scoped. Full match? All-members.
				if (EphemeralEntities.Num() < Broker->Members.Num())
				{
					Order.TargetMembers = EphemeralEntities;
				}
				Broker->OrderQueue.Add(Order);
			}
		}
		else
		{
			CreateBrokerForMembers(EphemeralEntities, Cmd.PlayerID, Order);
		}
	}

	// Cohesion group stamp. Give every UNIT participating in this order the
	// SAME id — loose units directly, plus each persistent (squad) broker's
	// members — so co-ordered units ACROSS separate squads, and squad-vs-loose,
	// are one group to local-avoidance cohesion (they pack instead of steering
	// around each other; the hard floor still prevents overlap). Broker
	// membership alone is single-level — squad members carry their squad's
	// handle, loose units the ephemeral broker's — so without this a mixed /
	// multi-squad selection wouldn't cohere below the top level. A solo order
	// stamps 0, clearing any stale group so a unit pulled out of a group stops
	// cohering with its former peers. Deterministic id: (CurrentTick, within-
	// tick order index). See FSeinBrokerMembershipData::CohesionGroupId.
	{
		const int64 CohesionId = (Filtered.Num() > 1)
			? ((static_cast<int64>(CurrentTick) << 20) | static_cast<int64>(CohesionOrderSeq++ & 0xFFFFF))
			: 0;
		auto StampCohesion = [this, CohesionId](const FSeinEntityHandle& U)
		{
			if (FSeinBrokerMembershipData* MB =
				GetComponentMutable<FSeinBrokerMembershipData>(U))
			{
				MB->CohesionGroupId = CohesionId;
			}
			else
			{
				FSeinBrokerMembershipData NB;
				NB.CohesionGroupId = CohesionId;
				AddComponent(U, NB);
			}
		};
		for (const FSeinEntityHandle& E : EphemeralEntities) { StampCohesion(E); }
		for (const FSeinEntityHandle& BH : PersistentBrokerEntities)
		{
			// Snapshot Members before stamping: the defensive AddComponent branch
			// can reallocate component storage, so we must not iterate a live
			// storage-backed list across it.
			if (const FSeinCommandBrokerData* PB = GetComponent<FSeinCommandBrokerData>(BH))
			{
				const TArray<FSeinEntityHandle> Members = PB->Members;
				for (const FSeinEntityHandle& Mem : Members) { StampCohesion(Mem); }
			}
		}
	}

	return ECommandHandleResult::Handled;
}

USeinWorldSubsystem::ECommandHandleResult USeinWorldSubsystem::TryHandleActivateAbilityCommand(
	const FSeinCommand& Cmd)
{
	if (Cmd.CommandType != SeinARTSTags::Command_Type_ActivateAbility)
	{
		return ECommandHandleResult::Unhandled;
	}

	FSeinAbilityComponent* AbilityComp =
		GetComponentMutable<FSeinAbilityComponent>(
			Cmd.EntityHandle);
	if (!AbilityComp)
	{
		UE_LOG(LogSeinSim, Verbose, TEXT("ActivateAbility[%s]: entity %s has no FSeinAbilityComponent"),
			*Cmd.AbilityTag.ToString(), *Cmd.EntityHandle.ToString());
		RejectCommand(Cmd, SeinARTSTags::Command_Reject_MissingComponent);
		return ECommandHandleResult::Handled;
	}

	USeinAbility* Ability = AbilityComp->FindAbilityByTag(*this, Cmd.AbilityTag);
	if (!Ability)
	{
		UE_LOG(LogSeinSim, Verbose, TEXT("ActivateAbility[%s]: entity %s has no ability with that tag (%d instances from %d granted classes)"),
			*Cmd.AbilityTag.ToString(), *Cmd.EntityHandle.ToString(),
			AbilityComp->AbilityInstanceIDs.Num(), AbilityComp->GrantedAbilities.Num());
		RejectCommand(Cmd, SeinARTSTags::Command_Reject_InvalidTarget);
		return ECommandHandleResult::Handled;
	}

	int32 AbilityID = INDEX_NONE;
	for (const int32 CandidateID : AbilityComp->AbilityInstanceIDs)
	{
		if (GetAbilityInstance(CandidateID) == Ability)
		{
			AbilityID = CandidateID;
			break;
		}
	}
	if (AbilityID == INDEX_NONE)
	{
		RejectCommand(Cmd, SeinARTSTags::Command_Reject_InvalidTarget);
		return ECommandHandleResult::Handled;
	}

	// Ability callbacks may replace component storage, revoke the instance, or
	// destroy its owner. Pool IDs are recycled, so identity includes the pointer
	// that occupied the snapshotted ID when command handling began.
	const TStrongObjectPtr<USeinAbility> OriginalAbility(Ability);
	auto RefreshAbility = [this, &AbilityComp, &Ability, AbilityID, &OriginalAbility, &Cmd]()
	{
		AbilityComp = nullptr;
		Ability = nullptr;
		const FSeinEntity* CurrentEntity = EntityPool.Get(Cmd.EntityHandle);
		if (!CurrentEntity || !CurrentEntity->IsAlive())
		{
			return false;
		}
		FSeinAbilityComponent* CurrentComp =
			GetComponentMutable<FSeinAbilityComponent>(Cmd.EntityHandle);
		if (!CurrentComp || !CurrentComp->AbilityInstanceIDs.Contains(AbilityID))
		{
			return false;
		}
		USeinAbility* CurrentAbility = GetAbilityInstance(AbilityID);
		if (CurrentAbility != OriginalAbility.Get()
			|| CurrentAbility->OwnerEntity != Cmd.EntityHandle)
		{
			return false;
		}
		AbilityComp = CurrentComp;
		Ability = CurrentAbility;
		return true;
	};
	auto RejectRevokedAbility = [this, &Cmd]()
	{
		RejectCommand(Cmd, SeinARTSTags::Command_Reject_InvalidTarget);
		return ECommandHandleResult::Handled;
	};

	const bool bHasDerivedPayer =
		Cmd.IssuerKind == ESeinCommandIssuerKind::DeterministicSystem
		&& Cmd.DerivedResourcePayer.IsValid();
	const FSeinPlayerID ResourcePayer = bHasDerivedPayer
		? Cmd.DerivedResourcePayer
		: (CommandAuthorityPolicy
			? CommandAuthorityPolicy->ResolveResourcePayer(
				CommandAuthorityView, Cmd, Cmd.EntityHandle)
			: FSeinPlayerID::Neutral());
	if (!RefreshAbility())
	{
		return RejectRevokedAbility();
	}
	if (!GetPlayerState(ResourcePayer))
	{
		RejectCommand(Cmd, SeinARTSTags::Command_Reject_Unauthorized);
		return ECommandHandleResult::Handled;
	}
	// Full activation ordering per DESIGN §7:
	//   1. Cooldown check
	//   2a. BlockedTags vs entity tags
	//   2b. RequiredEntityTags vs entity tags
	//   2c. RequiredPlayerTags vs player tags
	//   3. Declarative target validation (range / tags / LOS)
	//   4. CanActivate BP escape hatch
	//   5. Affordability check
	//   6. Deduct cost
	//   7. Cancel-others via CancelAbilitiesWithTag
	//   8. Record deducted cost snapshot + USeinAbility::ActivateAbility
	//      (which handles cooldown start + GrantedTags grant + OnActivate)

	// 1. Cooldown
	if (Ability->IsOnCooldown())
	{
		UE_LOG(LogSeinSim, Verbose, TEXT("ActivateAbility[%s]: on cooldown"), *Cmd.AbilityTag.ToString());
		RejectCommand(Cmd, SeinARTSTags::Command_Reject_OnCooldown);
		return ECommandHandleResult::Handled;
	}

	// 2a. BlockedTags — entity must NOT have any of these.
	if (!Ability->BlockedTags.IsEmpty())
	{
		if (HasAnyTag(Cmd.EntityHandle, Ability->BlockedTags))
		{
			UE_LOG(LogSeinSim, Verbose, TEXT("ActivateAbility[%s]: blocked by entity tags"),
				*Cmd.AbilityTag.ToString());
			RejectCommand(Cmd, SeinARTSTags::Command_Reject_BlockedByTag);
			return ECommandHandleResult::Handled;
		}
	}

	// 2b. RequiredEntityTags — entity must have ALL of these.
	// Use for entity-state preconditions: a Heal ability that requires
	// the caster to be `SeinARTS.State.Damaged`, etc. Reject silently
	// if the entity has no matching tags.
	if (!Ability->RequiredEntityTags.IsEmpty())
	{
		if (!HasAllTags(Cmd.EntityHandle, Ability->RequiredEntityTags))
		{
			UE_LOG(LogSeinSim, Verbose,
				TEXT("ActivateAbility[%s]: missing required entity tags"),
				*Cmd.AbilityTag.ToString());
			RejectCommand(Cmd, SeinARTSTags::Command_Reject_BlockedByTag);
			return ECommandHandleResult::Handled;
		}
	}

	// 2c. RequiredPlayerTags — owning player must have ALL listed tags.
	// This is where tech prereqs land for production abilities (e.g.
	// `SeinARTS.Tech.VehicleAccess` on `SA_TrainTank`). Player tags are
	// refcounted via USeinWorldSubsystem::GrantPlayerTag (DESIGN §10).
	if (!Ability->RequiredPlayerTags.IsEmpty())
	{
		const FSeinPlayerState* PS = GetPlayerState(ResourcePayer);
		if (!PS || !PS->HasAllPlayerTags(Ability->RequiredPlayerTags))
		{
			UE_LOG(LogSeinSim, Verbose,
				TEXT("ActivateAbility[%s]: blocked by missing player tags"),
				*Cmd.AbilityTag.ToString());
			RejectCommand(Cmd, SeinARTSTags::Command_Reject_BlockedByTag);
			return ECommandHandleResult::Handled;
		}
	}

	// 3. Declarative target validation (range / tags / LOS)
	const ESeinAbilityTargetValidationResult ValidationResult = FSeinAbilityValidation::ValidateTarget(
		*Ability, Cmd.EntityHandle, Cmd.TargetEntity, Cmd.TargetLocation, *this);
	if (!RefreshAbility())
	{
		return RejectRevokedAbility();
	}
	if (ValidationResult != ESeinAbilityTargetValidationResult::Valid)
	{
		// OutOfRange + AutoMoveThen: prepend an internal Move order on a
		// single-member broker, then queue the original ability behind it.
		// The Move targets the target's current position (or Cmd.TargetLocation
		// if no target entity). The eventual broker follow-up goes through the
		// ordinary activation gate and pays exactly once when it can execute.
		// Reserving at click time would require a first-class escrow record with
		// cancellation/refund ownership; an untracked early deduction can be
		// double-charged or stranded if movement never completes.
		if (ValidationResult == ESeinAbilityTargetValidationResult::OutOfRange &&
			Ability->OutOfRangeBehavior == ESeinOutOfRangeBehavior::AutoMoveThen)
		{
			// Member must have a Move ability to fulfill the prefix. If not,
			// there's nothing to auto-move with — reject as OutOfRange.
			// Move-ability lookup is via the bIsMoveAbility flag designer-set
			// on the move ability (no hardcoded tag).
			const USeinAbility* MoveAbility = AbilityComp->FindMoveAbility(*this);
			if (!MoveAbility || !MoveAbility->AbilityTag.IsValid())
			{
				UE_LOG(LogSeinSim, Verbose,
					TEXT("ActivateAbility[%s]: AutoMoveThen requested but entity has no ability flagged as Move (bIsMoveAbility) with a valid tag; rejecting"),
					*Cmd.AbilityTag.ToString());
				RejectCommand(Cmd, SeinARTSTags::Command_Reject_OutOfRange);
				return ECommandHandleResult::Handled;
			}
			const FGameplayTag MoveAbilityTag = MoveAbility->AbilityTag;

			// Fail fast when the order is authored, but do not deduct here. The
			// follow-up rechecks and charges through the same activation gate as
			// every direct/broker activation. If resources are spent while moving,
			// the follow-up may correctly fail instead of consuming untracked
			// escrow that has no cancellation owner.
			FSeinResourceCost AutoMoveActivationCost;
			FSeinResourceCost AutoMoveAtCompletionUnused;
			Ability->ResolveActivationCosts(
				this, AutoMoveActivationCost, AutoMoveAtCompletionUnused);
			if (!USeinResourceBPFL::SeinCanAfford(
				this, ResourcePayer, AutoMoveActivationCost))
			{
				UE_LOG(LogSeinSim, Verbose,
					TEXT("ActivateAbility[%s]: AutoMoveThen rejected — unaffordable"),
					*Cmd.AbilityTag.ToString());
				RejectCommand(Cmd, SeinARTSTags::Command_Reject_Unaffordable);
				return ECommandHandleResult::Handled;
			}
			// Resolve the Move destination. If the command targets an entity,
			// stand at the EDGE of its footprint (perimeter-standoff — units
			// build / repair / attack on the footprint perimeter, not the
			// center). Falls back to the entity center when the target has
			// no extents data, and to the raw TargetLocation when there's
			// no target entity at all.
			FFixedVector MoveDest = Cmd.TargetLocation;
			if (Cmd.TargetEntity.IsValid())
			{
				if (const FSeinEntity* Tgt = GetEntity(Cmd.TargetEntity))
				{
					const FFixedVector TargetCenter = Tgt->Transform.GetLocation();
					const FSeinEntity* MoverEntity = GetEntity(Cmd.EntityHandle);
					const FFixedVector ApproachFrom = MoverEntity ? MoverEntity->Transform.GetLocation() : TargetCenter;

					// Use the target's runtime extents if present. First-shape
					// only — compound bodies (turret + chassis) take the chassis
					// shape's bounding radius, which is usually the larger one
					// anyway. Falls back to TargetCenter if no extents.
					const FSeinExtentsComponent* TargetExtents = GetComponent<FSeinExtentsComponent>(Cmd.TargetEntity);
					const FSeinExtentsShape* TargetShape = (TargetExtents && TargetExtents->Shapes.Num() > 0)
						? &TargetExtents->Shapes[0]
						: nullptr;

					MoveDest = SeinExtentsHelpers::ComputeStandoffPoint(
						TargetShape, Tgt->Transform, ApproachFrom);
				}
			}

			UE_LOG(LogSeinSim, Verbose,
				TEXT("ActivateAbility[%s]: AutoMoveThen — out of range, queueing Move + Build on member %s targeting (%.1f, %.1f, %.1f)"),
				*Cmd.AbilityTag.ToString(), *Cmd.EntityHandle.ToString(),
				MoveDest.X.ToFloat(), MoveDest.Y.ToFloat(), MoveDest.Z.ToFloat());

			const TArray<FSeinEntityHandle> SingleMember = { Cmd.EntityHandle };

			// Move-prefix + follow-up targeted at just this member. Both
			// orders carry the one-broker-per-member invariant and the
			// subset-targeting machinery so non-target members (if this
			// folds into an existing multi-member broker) stay untouched.
			//
			// CRITICAL: set `PredeterminedAbilityTag` (NOT just `Context`)
			// so the default resolver's first-capable-member fast-path
			// dispatches the ability directly. Context-only entries
			// require the member's DefaultCommands table to map the
			// context tag to an ability — designers don't author such
			// mappings for framework-internal AutoMoveThen, so without
			// the predetermined tag the resolver silently no-ops and the
			// member stays idle.
			FSeinBrokerQueuedOrder MovePrefix;
			MovePrefix.Context.AddTag(MoveAbilityTag);
			MovePrefix.PredeterminedAbilityTag = MoveAbilityTag;
			MovePrefix.TargetLocation = MoveDest;
			MovePrefix.TargetMembers = SingleMember;
			MovePrefix.bIsInternalPrefix = true;

			FSeinBrokerQueuedOrder Followup;
			Followup.Context.AddTag(Cmd.AbilityTag);
			Followup.PredeterminedAbilityTag = Cmd.AbilityTag;
			Followup.TargetEntity = Cmd.TargetEntity;
			Followup.TargetLocation = Cmd.TargetLocation;
			Followup.TargetMembers = SingleMember;
			Followup.bIsInternalPrefix = true;
			Followup.DerivedResourcePayer = ResourcePayer;

			// Prefer the member's existing broker if it has one — inject the
			// [Move, Follow-up] pair right after the currently-executing
			// order. One-broker-per-member preserved, shift-queue on the
			// existing broker preserved, non-target members unaffected.
			FSeinEntityHandle ExistingBroker;
			if (const FSeinBrokerMembershipData* Memb = GetComponent<FSeinBrokerMembershipData>(Cmd.EntityHandle))
			{
				ExistingBroker = Memb->CurrentBrokerHandle;
			}
			if (ExistingBroker.IsValid() && EntityPool.IsValid(ExistingBroker))
			{
				if (FSeinCommandBrokerData* Broker =
					GetComponentMutable<FSeinCommandBrokerData>(
						ExistingBroker))
				{
					// Insert position: right after the LAST currently-executing
					// order. Under per-order parallelism multiple orders may be
					// executing concurrently; we want the AutoMoveThen pair to
					// be the next-priority candidate AFTER everything that's
					// currently in flight, but BEFORE any other queued
					// non-executing orders. Counts forward through the queue
					// so the result is `last_executing_index + 1`. With no
					// executing orders, InsertAt stays at 0 (front).
					int32 InsertAt = 0;
					for (int32 q = 0; q < Broker->OrderQueue.Num(); ++q)
					{
						if (Broker->OrderQueue[q].bIsExecuting)
						{
							InsertAt = q + 1;
						}
					}
					// Insert Followup first, then MovePrefix at the same index
					// — MovePrefix pushes Followup back one slot. Net result:
					// [..., executing..., MovePrefix, Followup, queued...].
					// Move runs first, Followup runs when Move completes (the
					// member lock keeps them serialized for this same member).
					Broker->OrderQueue.Insert(Followup, FMath::Min(InsertAt, Broker->OrderQueue.Num()));
					Broker->OrderQueue.Insert(MovePrefix, FMath::Min(InsertAt, Broker->OrderQueue.Num()));
					UE_LOG(LogSeinSim, Verbose,
						TEXT("AutoMoveThen[%s]: injected Move+Followup into existing broker %s at index %d (queue depth now=%d)"),
						*Cmd.AbilityTag.ToString(), *ExistingBroker.ToString(),
						InsertAt, Broker->OrderQueue.Num());
					return ECommandHandleResult::Handled;
				}
			}

			// No existing broker — spawn one for this single member with the
			// Move+Follow-up queued. CreateBrokerForMembers takes a first
			// order and pre-queues it; append follow-up after.
			FSeinEntityHandle BrokerHandle = CreateBrokerForMembers(
				SingleMember, GetEntityOwner(Cmd.EntityHandle), MovePrefix);
			if (BrokerHandle.IsValid())
			{
				if (FSeinCommandBrokerData* Broker =
					GetComponentMutable<FSeinCommandBrokerData>(
						BrokerHandle))
				{
					Broker->OrderQueue.Add(Followup);
					UE_LOG(LogSeinSim, Verbose,
						TEXT("AutoMoveThen[%s]: created new broker %s with Move+Followup (queue depth=%d)"),
						*Cmd.AbilityTag.ToString(), *BrokerHandle.ToString(), Broker->OrderQueue.Num());
				}
			}
			else
			{
				UE_LOG(LogSeinSim, Verbose,
					TEXT("AutoMoveThen[%s]: CreateBrokerForMembers returned invalid handle — silent failure"),
					*Cmd.AbilityTag.ToString());
			}
			return ECommandHandleResult::Handled;
		}
		UE_LOG(LogSeinSim, Verbose, TEXT("ActivateAbility[%s]: target validation failed (%d)"),
			*Cmd.AbilityTag.ToString(), static_cast<int32>(ValidationResult));
		FGameplayTag ReasonTag;
		switch (ValidationResult)
		{
		case ESeinAbilityTargetValidationResult::OutOfRange:     ReasonTag = SeinARTSTags::Command_Reject_OutOfRange; break;
		case ESeinAbilityTargetValidationResult::NoLineOfSight:  ReasonTag = SeinARTSTags::Command_Reject_NoLineOfSight; break;
		default:                                                  ReasonTag = SeinARTSTags::Command_Reject_InvalidTarget; break;
		}
		RejectCommand(Cmd, ReasonTag);
		return ECommandHandleResult::Handled;
	}

	// 3b. Pathable-target gate — if enabled on this ability, consult the
	// nav-registered resolver for reachability. From/To stay FFixedVector
	// end-to-end; no lossy float round-trip on the sim path.
	if (Ability->bRequiresPathableTarget && PathableTargetResolver.IsBound())
	{
		const FSeinEntity* ActingEntity = GetEntity(Cmd.EntityHandle);
		if (ActingEntity)
		{
			const FFixedVector FromWorld = ActingEntity->Transform.GetLocation();
			const FFixedVector ToWorld = Cmd.TargetLocation;
			const FSeinNavAgentProfile Agent =
				BuildNavAgentProfile(Cmd.EntityHandle);

			const bool bPathable =
				PathableTargetResolver.Execute(
					FromWorld, ToWorld, Agent);
			if (!RefreshAbility())
			{
				return RejectRevokedAbility();
			}

			if (!bPathable)
			{
				UE_LOG(LogSeinSim, Verbose, TEXT("ActivateAbility[%s]: target not reachable"),
					*Cmd.AbilityTag.ToString());
				RejectCommand(Cmd, SeinARTSTags::Command_Reject_PathUnreachable);
				return ECommandHandleResult::Handled;
			}
		}
	}

	// 4. CanActivate escape hatch (after declarative validation)
	const bool bCanActivate = Ability->CanActivate();
	if (!RefreshAbility())
	{
		return RejectRevokedAbility();
	}
	if (!bCanActivate)
	{
		UE_LOG(LogSeinSim, Verbose, TEXT("ActivateAbility[%s]: CanActivate returned false"),
			*Cmd.AbilityTag.ToString());
		RejectCommand(Cmd, SeinARTSTags::Command_Reject_CanActivateFailed);
		return ECommandHandleResult::Handled;
	}
	if (!Ability->CanCommitGrantedTags())
	{
		UE_LOG(LogSeinSim, Error,
			TEXT("ActivateAbility[%s]: owned-tag refcount is saturated"),
			*Cmd.AbilityTag.ToString());
		RejectCommand(Cmd, SeinARTSTags::Command_Reject_CanActivateFailed);
		return ECommandHandleResult::Handled;
	}

	// 5. Resolve the authored per-ability timing policy. Immediate charges the
	// full cost regardless of catalog metadata. Production Queue splits catalog
	// AtEnqueue/AtCompletion amounts and transfers the latter to its queue entry.
	FSeinResourceCost ActivationCost;
	FSeinResourceCost PendingCompletionCost;
	Ability->ResolveActivationCosts(
		this, ActivationCost, PendingCompletionCost);

	// 6. Only the activation cost is affordable now. Deferred production cost
	// is checked atomically by the production system at completion.
	if (!USeinResourceBPFL::SeinCanAfford(this, ResourcePayer, ActivationCost))
	{
		UE_LOG(LogSeinSim, Verbose, TEXT("ActivateAbility[%s]: unaffordable"),
			*Cmd.AbilityTag.ToString());
		RejectCommand(Cmd, SeinARTSTags::Command_Reject_Unaffordable);
		return ECommandHandleResult::Handled;
	}

	// 7. Commit the policy-resolved activation principal.
	if (!RefreshAbility())
	{
		return RejectRevokedAbility();
	}
	if (!USeinResourceBPFL::SeinDeduct(this, ResourcePayer, ActivationCost))
	{
		UE_LOG(LogSeinSim, Error,
			TEXT("ActivateAbility[%s]: resource deduction failed after affordability preflight"),
			*Cmd.AbilityTag.ToString());
		RejectCommand(Cmd, SeinARTSTags::Command_Reject_Unaffordable);
		return ECommandHandleResult::Handled;
	}
	auto RefundAndRejectRevoked = [this, &Cmd, ResourcePayer, &ActivationCost]()
	{
		USeinResourceBPFL::SeinRefund(this, ResourcePayer, ActivationCost);
		RejectCommand(Cmd, SeinARTSTags::Command_Reject_InvalidTarget);
		return ECommandHandleResult::Handled;
	};

	// 7a. Cancel OTHER abilities whose GrantedTags intersect this ability's
	// CancelAbilitiesWithTag. Explicitly skip self — matching self here
	// would cancel-then-reactivate on every duplicate command (e.g. a
	// broker dispatching the same move twice in one command frame), and
	// nothing actually moves. Self-cancelling-reissue is handled below.
	const FGameplayTagContainer CancelTags = Ability->CancelAbilitiesWithTag;
	if (!CancelTags.IsEmpty())
	{
		TArray<int32> AbilityIDsToCancel;
		TArray<TStrongObjectPtr<USeinAbility>> AbilitiesToCancel;
		const TArray<int32> AbilityIDs = AbilityComp->AbilityInstanceIDs;
		for (const int32 OtherID : AbilityIDs)
		{
			USeinAbility* Other = GetAbilityInstance(OtherID);
			if (Other && Other != Ability && Other->bIsActive &&
				Other->GrantedTags.HasAny(CancelTags))
			{
				AbilityIDsToCancel.Add(OtherID);
				AbilitiesToCancel.Emplace(Other);
			}
		}
		for (int32 Index = 0; Index < AbilitiesToCancel.Num(); ++Index)
		{
			const int32 OtherID = AbilityIDsToCancel[Index];
			USeinAbility* const ExpectedAbility = AbilitiesToCancel[Index].Get();
			const FSeinAbilityComponent* CurrentComp =
				GetComponent<FSeinAbilityComponent>(Cmd.EntityHandle);
			USeinAbility* Other = GetAbilityInstance(OtherID);
			if (!CurrentComp
				|| !CurrentComp->AbilityInstanceIDs.Contains(OtherID)
				|| Other != ExpectedAbility
				|| Other->OwnerEntity != Cmd.EntityHandle
				|| !Other->bIsActive)
			{
				continue;
			}
			Other->CancelAbility();
			if (!RefreshAbility())
			{
				return RefundAndRejectRevoked();
			}
		}
	}

	// 7b. Self-cancelling reissue: if this ability is already running and
	// lists any of its own GrantedTags in CancelAbilitiesWithTag, the
	// designer is asking "re-issuing me should kill the previous run
	// before the new one starts" — so cancel the prior activation
	// before ActivateAbility spins up a fresh one.
	if (Ability->bIsActive &&
		Ability->GrantedTags.HasAny(CancelTags))
	{
		Ability->CancelAbility();
		if (!RefreshAbility())
		{
			return RefundAndRejectRevoked();
		}
	}
	if (Ability->bIsActive)
	{
		USeinResourceBPFL::SeinRefund(this, ResourcePayer, ActivationCost);
		RejectCommand(Cmd, SeinARTSTags::Command_Reject_CanActivateFailed);
		return ECommandHandleResult::Handled;
	}

	UE_LOG(LogSeinSim, Verbose,
		TEXT("ActivateAbility[%s]: gates passed, calling Ability->ActivateAbility on entity %s targeting %s"),
		*Cmd.AbilityTag.ToString(), *Cmd.EntityHandle.ToString(), *Cmd.TargetEntity.ToString());

	// 8. Stamp the resolved funding snapshots and commit activation.
	//
	// Targeter-originated commands carry captured points; route through
	// the points-aware overload so the ability's runtime TargeterPoints
	// array gets populated. Empty array degrades to the basic activation
	// path. Broker per-member dispatches carry these forward via
	// SeinCommandBrokerDispatch::ActivateMemberAbility.
	if (!RefreshAbility())
	{
		return RefundAndRejectRevoked();
	}
	Ability->RecordDeductedCost(ActivationCost);
	Ability->RecordPendingCompletionCost(PendingCompletionCost);
	Ability->RecordResourcePayer(ResourcePayer);
	bool bActivated = false;
	if (Cmd.TargeterPoints.Num() > 0)
	{
		bActivated = Ability->ActivateAbilityWithTargeterPoints(
			Cmd.TargetEntity, Cmd.TargetLocation, Cmd.TargeterPoints);
	}
	else
	{
		bActivated = Ability->ActivateAbility(Cmd.TargetEntity, Cmd.TargetLocation);
	}
	const bool bAbilityStillOwned = RefreshAbility();
	if (!bActivated)
	{
		// The command preflight should make this unreachable in the
		// single-threaded sim, but preserve economic balance if a future
		// activation seam introduces another transactional failure.
		USeinResourceBPFL::SeinRefund(this, ResourcePayer, ActivationCost);
		if (bAbilityStillOwned)
		{
			Ability->RecordDeductedCost(FSeinResourceCost());
			Ability->RecordPendingCompletionCost(FSeinResourceCost());
			Ability->RecordResourcePayer(FSeinPlayerID::Neutral());
		}
		RejectCommand(Cmd, SeinARTSTags::Command_Reject_CanActivateFailed);
		return ECommandHandleResult::Handled;
	}
	if (!bAbilityStillOwned)
	{
		// OnActivate ran and may have committed arbitrary deterministic side
		// effects. The activation succeeded, so there is no economic rollback;
		// only avoid writing through storage that the callback invalidated.
		return ECommandHandleResult::Handled;
	}
	return ECommandHandleResult::Handled;
}

USeinWorldSubsystem::ECommandHandleResult USeinWorldSubsystem::TryHandleCancelAbilityCommand(
	const FSeinCommand& Cmd)
{
	if (Cmd.CommandType != SeinARTSTags::Command_Type_CancelAbility)
	{
		return ECommandHandleResult::Unhandled;
	}

	FSeinAbilityComponent* AbilityComp =
		GetComponentMutable<FSeinAbilityComponent>(
			Cmd.EntityHandle);
	const int32 ActiveAbilityID = AbilityComp
		? AbilityComp->ActiveAbilityID
		: INDEX_NONE;
	USeinAbility* Active = GetAbilityInstance(ActiveAbilityID);
	if (AbilityComp
		&& AbilityComp->AbilityInstanceIDs.Contains(ActiveAbilityID)
		&& Active
		&& Active->OwnerEntity == Cmd.EntityHandle
		&& Active->bIsActive)
	{
		Active->CancelAbility();
	}
	else
	{
		RejectCommand(Cmd, SeinARTSTags::Command_Reject_InvalidTarget);
	}

	return ECommandHandleResult::Handled;
}

USeinWorldSubsystem::ECommandHandleResult USeinWorldSubsystem::TryHandleCancelProductionCommand(
	const FSeinCommand& Cmd)
{
	if (Cmd.CommandType != SeinARTSTags::Command_Type_CancelProduction)
	{
		return ECommandHandleResult::Unhandled;
	}

	FSeinProductionComponent* ProdComp =
		GetComponentMutable<FSeinProductionComponent>(
			Cmd.EntityHandle);
	if (!ProdComp) { RejectCommand(Cmd, SeinARTSTags::Command_Reject_MissingComponent); return ECommandHandleResult::Handled; }

	const int32 CancelIdx = Cmd.QueueIndex;
	if (CancelIdx < 0 || CancelIdx >= ProdComp->Queue.Num()) { RejectCommand(Cmd, SeinARTSTags::Command_Reject_InvalidTarget); return ECommandHandleResult::Handled; }

	// Refund AtEnqueueCost only (AtCompletion was never deducted). Policy
	// chooses between progress-proportional (default) and flat-custom.
	const FSeinPlayerID ResourcePayer = ProdComp->Queue[CancelIdx].ResourcePayer;
	if (FSeinPlayerState* PS = GetPlayerStateMutable(ResourcePayer))
	{
		const FSeinProductionQueueEntry& CancelledEntry = ProdComp->Queue[CancelIdx];

		FFixedPoint RefundFraction;
		if (CancelledEntry.RefundPolicy.bUseCustomRefund)
		{
			RefundFraction = CancelledEntry.RefundPolicy.CustomRefundPercentage;
		}
		else
		{
			// Progress-proportional: refund = (1 - progress) * cost.
			// Only the front entry has non-zero progress.
			FFixedPoint ProgressFraction = FFixedPoint::Zero;
			if (CancelIdx == 0 && CancelledEntry.TotalBuildTime > FFixedPoint::Zero)
			{
				ProgressFraction = ProdComp->CurrentBuildProgress / CancelledEntry.TotalBuildTime;
				if (ProgressFraction > FFixedPoint::One) ProgressFraction = FFixedPoint::One;
			}
			RefundFraction = FFixedPoint::One - ProgressFraction;
		}

		if (RefundFraction > FFixedPoint::Zero)
		{
			FSeinResourceCost Refund;
			Refund.Amounts.Reserve(CancelledEntry.AtEnqueueCost.Amounts.Num());
			for (const auto& Pair : CancelledEntry.AtEnqueueCost.Amounts)
			{
				Refund.Amounts.Add(Pair.Key, Pair.Value * RefundFraction);
			}
			USeinResourceBPFL::SeinRefund(this, ResourcePayer, Refund);
		}
	}

	ProdComp->Queue.RemoveAt(CancelIdx);
	if (CancelIdx == 0)
	{
		ProdComp->CurrentBuildProgress = FFixedPoint::Zero;
		ProdComp->bStalledAtCompletion = false;
	}

	return ECommandHandleResult::Handled;
}

void USeinWorldSubsystem::SetAIEmitInterceptor(
	FSeinAIEmitInterceptor&& Interceptor)
{
	AIEmitInterceptor = MoveTemp(Interceptor);
}

void USeinWorldSubsystem::ClearAIEmitInterceptor()
{
	AIEmitInterceptor.Unbind();
}

bool USeinWorldSubsystem::HasAIEmitInterceptor() const
{
	return AIEmitInterceptor.IsBound();
}

void USeinWorldSubsystem::SetLocalCommandSubmitter(
	FSeinLocalCommandSubmitter&& Submitter)
{
	LocalCommandSubmitter = MoveTemp(Submitter);
}

void USeinWorldSubsystem::ClearLocalCommandSubmitter()
{
	LocalCommandSubmitter.Unbind();
}

bool USeinWorldSubsystem::HasLocalCommandSubmitter() const
{
	return LocalCommandSubmitter.IsBound();
}

void USeinWorldSubsystem::EnqueueCommand(const FSeinCommand& Command)
{
	SEIN_CHECK_NOT_PARALLEL();
	if (bObserverCallbackInProgress)
	{
		UE_LOG(LogSeinSim, Error,
			TEXT("Rejected replay command '%s' from a read-only observer."),
			*Command.CommandType.ToString());
		return;
	}
	if (MatchBootstrapState != ESeinMatchBootstrapState::Consumed
		|| !bIsRunning)
	{
		UE_LOG(LogSeinSim, Error,
			TEXT("Rejected replay command '%s': canonical ingress requires a launched, running match (bootstrap=%s running=%d)."),
			*Command.CommandType.ToString(),
			MatchBootstrapStateName(MatchBootstrapState),
			bIsRunning ? 1 : 0);
		return;
	}
	if (!bReplayOwnsExternalCommandIngress)
	{
		UE_LOG(LogSeinSim, Error,
			TEXT("Rejected replay command '%s' without exclusive replay ingress."),
			*Command.CommandType.ToString());
		return;
	}
	if ((Command.IssuerKind != ESeinCommandIssuerKind::Player
			&& Command.IssuerKind != ESeinCommandIssuerKind::MatchAdministrator)
		|| Command.DerivedResourcePayer.IsValid())
	{
		UE_LOG(LogSeinSim, Error,
			TEXT("Rejected non-external replay command '%s'; canonical replay ingress accepts only neutral external principals."),
			*Command.CommandType.ToString());
		return;
	}
	if (bSimPausedHard)
	{
		RejectCommand(Command, SeinARTSTags::Command_Reject_SimPaused);
		return;
	}
	PendingReplayCommands.AddCommand(Command);
}

void USeinWorldSubsystem::EnqueueAuthenticatedCommand(
	const FSeinCommand& Command,
	FSeinPlayerID AuthenticatedPlayer,
	ESeinCommandIssuerKind AuthenticatedIssuerKind)
{
	SEIN_CHECK_NOT_PARALLEL();
	if (bObserverCallbackInProgress)
	{
		UE_LOG(LogSeinSim, Error,
			TEXT("Rejected transport-authenticated command '%s' from a read-only observer."),
			*Command.CommandType.ToString());
		return;
	}
	if (MatchBootstrapState != ESeinMatchBootstrapState::Consumed
		|| !bIsRunning)
	{
		UE_LOG(LogSeinSim, Error,
			TEXT("Rejected transport-authenticated command '%s': ingress requires a launched, running match (bootstrap=%s running=%d)."),
			*Command.CommandType.ToString(),
			MatchBootstrapStateName(MatchBootstrapState),
			bIsRunning ? 1 : 0);
		return;
	}
	if (bReplayOwnsExternalCommandIngress)
	{
		UE_LOG(LogSeinSim, Warning,
			TEXT("Rejected transport-authenticated command '%s' while replay owns external ingress."),
			*Command.CommandType.ToString());
		return;
	}
	if (AuthenticatedIssuerKind != ESeinCommandIssuerKind::Player
		&& AuthenticatedIssuerKind != ESeinCommandIssuerKind::MatchAdministrator)
	{
		UE_LOG(LogSeinSim, Error,
			TEXT("Rejected trusted command ingress with invalid external issuer kind %d."),
			static_cast<int32>(AuthenticatedIssuerKind));
		return;
	}

	FSeinCommand Canonical = Command;
	Canonical.IssuerKind = AuthenticatedIssuerKind;
	Canonical.PlayerID = AuthenticatedIssuerKind
			== ESeinCommandIssuerKind::MatchAdministrator
		? FSeinPlayerID::Neutral()
		: AuthenticatedPlayer;
	Canonical.DerivedResourcePayer = FSeinPlayerID::Neutral();
	if (bSimPausedHard)
	{
		RejectCommand(Canonical, SeinARTSTags::Command_Reject_SimPaused);
		return;
	}
	PendingCommands.AddCommand(Canonical);
}

void USeinWorldSubsystem::EnqueueDerivedCommand(const FSeinCommand& Command)
{
	SEIN_CHECK_NOT_PARALLEL();
	if (bObserverCallbackInProgress)
	{
		UE_LOG(LogSeinSim, Error,
			TEXT("Rejected deterministic-system command '%s' from a read-only observer."),
			*Command.CommandType.ToString());
		return;
	}
	if (!SeinIsInSimContext(this))
	{
		UE_LOG(LogSeinSim, Error,
			TEXT("Rejected deterministic-system command '%s' outside simulation context."),
			*Command.CommandType.ToString());
		return;
	}
	if (Command.DerivedResourcePayer.IsValid()
		&& Command.CommandType != SeinARTSTags::Command_Type_ActivateAbility)
	{
		UE_LOG(LogSeinSim, Error,
			TEXT("Rejected deterministic-system command '%s' carrying an inapplicable resource payer."),
			*Command.CommandType.ToString());
		return;
	}
	FSeinCommand Canonical = Command;
	Canonical.IssuerKind = ESeinCommandIssuerKind::DeterministicSystem;
	if (bSimPausedHard)
	{
		RejectCommand(Canonical, SeinARTSTags::Command_Reject_SimPaused);
		return;
	}
	PendingCommands.AddCommand(Canonical);
}

void USeinWorldSubsystem::SubmitLocalCommandDraft(
	const FSeinCommand& Draft,
	bool bRequestMatchAdministration)
{
	SEIN_CHECK_NOT_PARALLEL();
	if (bObserverCallbackInProgress)
	{
		UE_LOG(LogSeinSim, Error,
			TEXT("Rejected local command draft '%s' from a read-only observer."),
			*Draft.CommandType.ToString());
		return;
	}
	if (MatchBootstrapState != ESeinMatchBootstrapState::Consumed
		|| !bIsRunning)
	{
		UE_LOG(LogSeinSim, Error,
			TEXT("Rejected local command draft '%s': ordinary ingress requires a launched, running match (bootstrap=%s running=%d)."),
			*Draft.CommandType.ToString(),
			MatchBootstrapStateName(MatchBootstrapState),
			bIsRunning ? 1 : 0);
		return;
	}
	if (bReplayOwnsExternalCommandIngress)
	{
		UE_LOG(LogSeinSim, Warning,
			TEXT("Rejected local command draft '%s' while replay owns external ingress."),
			*Draft.CommandType.ToString());
		return;
	}
	if (bSimPausedHard)
	{
		FSeinCommandSchemaDescriptor Schema;
		const bool bFrozenPauseControl =
			CommandSchemaSnapshot.ValidateStructure(Draft, &Schema)
				== ESeinCommandStructureResult::Valid
			&& (Schema.AllowedExecutionContexts
				& static_cast<int32>(ESeinCommandExecutionAllowance::FrozenPauseControl)) != 0;
		if (!bFrozenPauseControl)
		{
			RejectCommand(Draft, SeinARTSTags::Command_Reject_SimPaused);
			return;
		}
	}
	if (LocalCommandSubmitter.IsBound())
	{
		FSeinCommand ScrubbedDraft = Draft;
		ScrubbedDraft.DerivedResourcePayer = FSeinPlayerID::Neutral();
		LocalCommandSubmitter.Execute(ScrubbedDraft, bRequestMatchAdministration);
		return;
	}

	if (!bRequestMatchAdministration && !Draft.PlayerID.IsValid())
	{
		UE_LOG(LogSeinSim, Error,
			TEXT("Rejected standalone command draft '%s' without a valid player."),
			*Draft.CommandType.ToString());
		return;
	}

	const FSeinPlayerID AuthenticatedPlayer = bRequestMatchAdministration
		? FSeinPlayerID::Neutral()
		: Draft.PlayerID;
	const ESeinCommandIssuerKind AuthenticatedIssuer = bRequestMatchAdministration
		? ESeinCommandIssuerKind::MatchAdministrator
		: ESeinCommandIssuerKind::Player;
	if (bSimPaused)
	{
		FSeinCommand Canonical = Draft;
		Canonical.PlayerID = AuthenticatedPlayer;
		Canonical.IssuerKind = AuthenticatedIssuer;
		Canonical.DerivedResourcePayer = FSeinPlayerID::Neutral();

		FSeinCommandSchemaDescriptor Schema;
		const bool bFrozenPauseControl =
			CommandSchemaSnapshot.ValidateStructure(Canonical, &Schema)
				== ESeinCommandStructureResult::Valid
			&& (Schema.AllowedExecutionContexts
				& static_cast<int32>(ESeinCommandExecutionAllowance::FrozenPauseControl)) != 0;
		if (bFrozenPauseControl)
		{
			Canonical.Tick = PauseFrozenTick;
			if (PendingStandalonePauseControlCommands.Num()
				>= MaxPauseControlCommandsPerFrame)
			{
				RejectCommand(Canonical, SeinARTSTags::Command_Reject_PayloadTooLarge);
				return;
			}
			PendingStandalonePauseControlCommands.Add(MoveTemp(Canonical));
			return;
		}
		if (bSimPausedHard)
		{
			RejectCommand(Canonical, SeinARTSTags::Command_Reject_SimPaused);
			return;
		}
	}

	EnqueueAuthenticatedCommand(
		Draft,
		AuthenticatedPlayer,
		AuthenticatedIssuer);
}

bool USeinWorldSubsystem::BeginReplayExclusiveCommandIngress(FString& OutError)
{
	// NOTE: v9 checkpoint playback does NOT call this — a continuation
	// checkpoint legitimately restores with PendingCommands, so
	// USeinReplayReader::PlayV9 (a friend) re-implements every guard here
	// EXCEPT the pending-command refusal and sets the flag directly. If a
	// new invariant is added to this function, mirror it in the reader's
	// inline copy.
	SEIN_CHECK_NOT_PARALLEL();
	OutError.Reset();
	if (bReplayOwnsExternalCommandIngress)
	{
		OutError = TEXT("another replay reader already owns external command ingress");
		return false;
	}
	if (PendingCommands.Num() > 0
		|| PendingReplayCommands.Num() > 0
		|| !PendingStandalonePauseControlCommands.IsEmpty())
	{
		OutError = TEXT("the world has pending external commands at replay start");
		return false;
	}
	bReplayOwnsExternalCommandIngress = true;
	return true;
}

void USeinWorldSubsystem::EndReplayExclusiveCommandIngress()
{
	SEIN_CHECK_NOT_PARALLEL();
	PendingReplayCommands.Clear();
	bReplayOwnsExternalCommandIngress = false;
}

bool USeinWorldSubsystem::FindCommandSchema(
	FGameplayTag CommandType,
	int32 SchemaVersion,
	FSeinCommandSchemaDescriptor& OutSchema) const
{
	return CommandSchemaSnapshot.FindSchema(CommandType, SchemaVersion, OutSchema);
}

ESeinCommandStructureResult USeinWorldSubsystem::ValidateCommandStructure(
	const FSeinCommand& Command,
	FSeinCommandSchemaDescriptor* OutSchema) const
{
	return CommandSchemaSnapshot.ValidateStructure(Command, OutSchema);
}

// ==================== Entity Management ====================

FSeinEntityHandle USeinWorldSubsystem::SpawnEntity(
	TSubclassOf<ASeinActor> ActorClass,
	const FFixedTransform& SpawnTransform,
	FSeinPlayerID OwnerPlayerID)
{
	if (!RequireStateMutationAuthorization(TEXT("SpawnEntity")))
	{
		return FSeinEntityHandle::Invalid();
	}
	if (!ActorClass)
	{
		UE_LOG(LogSeinSim, Error, TEXT("Cannot spawn entity: ActorClass is null"));
		return FSeinEntityHandle::Invalid();
	}

	// CDO required for the SCS-aware walk below. Legacy ArchetypeDefinition has
	// been excised — identity/producible/extents/etc. live as
	// FSein*Component entries in USeinEntityComponent::ComponentData.
	const ASeinActor* CDO = GetDefault<ASeinActor>(ActorClass);
	if (!CDO)
	{
		UE_LOG(LogSeinSim, Error, TEXT("Cannot spawn entity: Blueprint %s has no CDO"), *ActorClass->GetName());
		return FSeinEntityHandle::Invalid();
	}

	// Degenerate-scale guard. A zero scale component is never a legitimate
	// spawn input, but it fails SILENTLY: the entity is fully functional in
	// the sim (movement/collision/extents never read scale) while the bridge
	// drives the actor's render transform from it — an invisible "ghost"
	// unit. Corrupted authored FFixedTransform data (the fix-1 serializer
	// window) shipped exactly this via squad slot offsets. Normalize to
	// Identity and say so loudly. Deterministic: pure function of the input.
	FFixedTransform SafeTransform = SpawnTransform;
	if (SafeTransform.Scale.X == FFixedPoint::Zero
		|| SafeTransform.Scale.Y == FFixedPoint::Zero
		|| SafeTransform.Scale.Z == FFixedPoint::Zero)
	{
		UE_LOG(LogSeinSim, Warning,
			TEXT("SpawnEntity(%s): spawn transform has a zero scale component (%s) — normalized to Identity. "
				 "Check the authored transform data feeding this spawn."),
			*ActorClass->GetName(), *SafeTransform.Scale.ToString());
		SafeTransform.Scale = FFixedVector::Identity;
	}

	FSeinEntityHandle Handle = EntityPool.Acquire(
		SafeTransform,
		OwnerPlayerID
	);

	if (!Handle.IsValid())
	{
		UE_LOG(LogSeinSim, Error, TEXT("Failed to acquire entity from pool"));
		return FSeinEntityHandle::Invalid();
	}

	// Store actor class for bridge spawning
	EntityActorClassMap.Add(Handle, ActorClass);

	// Walk the Blueprint CDO's USeinEntityComponent subobjects and inject
	// every authored ComponentData entry into deterministic sim storage. This
	// is the sole sim-authoring path post-Phase-5 — typed-wrapper ACs are
	// gone; designers compose entities by adding entries to the entity
	// component's ComponentData array.
	//
	// NB: AActor::GetComponents() on a CDO only sees native CreateDefaultSubobject
	// components — BP-editor-added components live on the SCS. The helper below
	// walks native components + SCS nodes up the BP hierarchy in a stable order.
	// Walk the BP CDO's USeinEntityComponent ONCE here; the resolved bridge
	// (BridgeCDO) is reused for BaseTags seeding below instead of walking twice.
	const USeinEntityComponent* BridgeCDO = nullptr;
	if (CDO)
	{
		TArray<const USeinEntityComponent*> EntityComps;
		AActor::GetActorClassDefaultComponents<USeinEntityComponent>(ActorClass, EntityComps);
		if (EntityComps.Num() > 1)
		{
			UE_LOG(LogSeinSim, Warning,
				TEXT("SpawnEntity: %s carries %d USeinEntityComponents — only the first will be used."),
				*ActorClass->GetName(), EntityComps.Num());
		}
		if (EntityComps.Num() > 0 && EntityComps[0])
		{
			BridgeCDO = EntityComps[0];
			BridgeCDO->InjectAuthoredComponents(*this, Handle);
		}
	}

	// Instantiate ability UObjects if the entity was granted any
	InitializeEntityAbilities(Handle);

	// Initialize the entity's tag state. Seed BaseTags from the entity bridge's
	// authored BaseTags UPROPERTY, then merge in the identity tag (from
	// FSeinIdentityComponent) and the
	// UnderConstruction tag if the entity carries a construction component.
	// Finally seed refcounts + the global EntityTagIndex from the full set.
	//
	// The matching ungrant for UnderConstruction lives in SeinFinishConstruction
	// (drops the refcount we add here via the BaseTags seed). Designers can
	// also list UnderConstruction explicitly in BaseTags — harmless, just gives
	// a +1 refcount that the system holds onto.
	const bool bHasConstructionComponent = GetComponent<FSeinConstructionComponent>(Handle) != nullptr;
	{
		FSeinEntityTagState& TagState = EntityTagStates.FindOrAdd(Handle);

		// Seed from the entity bridge's authored BaseTags, reusing the single CDO
		// walk above (BridgeCDO). Falls back to a fresh walk only if injection
		// didn't run (CDO was null); preserves the original unconditional seed.
		const USeinEntityComponent* TagBridge = BridgeCDO;
		if (!TagBridge)
		{
			TArray<const USeinEntityComponent*> EntityComps;
			AActor::GetActorClassDefaultComponents<USeinEntityComponent>(ActorClass, EntityComps);
			if (EntityComps.Num() > 0) TagBridge = EntityComps[0];
		}
		if (TagBridge)
		{
			TagState.BaseTags.AppendTags(TagBridge->BaseTags);
		}

		// Resolve the identity tag from the entity's FSeinIdentityComponent
		// (injected from the bridge's ComponentData array above).
		if (const FSeinIdentityComponent* Identity = GetComponent<FSeinIdentityComponent>(Handle))
		{
			if (Identity->IdentityTag.IsValid())
			{
				TagState.BaseTags.AddTag(Identity->IdentityTag);
			}
		}
		if (bHasConstructionComponent)
		{
			TagState.BaseTags.AddTag(SeinARTSTags::State_UnderConstruction);
		}
		SeedEntityTagsFromBase(Handle);

		// AFTER tag seeding — replay any active player-scope effects that
		// grant abilities to entities matching this entity's tag state.
		// Covers the "unit spawned after tech research completed" case so
		// the new unit picks up unlocked abilities at spawn instead of
		// being permanently stuck without them.
		ReplayEffectAbilityGrants(Handle);
	}

	// Fire spawn visual event. The actor bridge processes EntitySpawned first
	// (creates the bridged actor), THEN downstream events for the same entity
	// land on its now-live ACs. Order matters — we enqueue spawn before the
	// optional construction-state event so the construction AC exists by the
	// time the construction event reaches it.
	EnqueueVisualEvent(FSeinVisualEvent::MakeSpawnEvent(Handle, SafeTransform.GetLocation()));

	// Construction-state notification — drives the placement-visual swap on the
	// bridged actor's USeinConstructionRenderComponent. Only fired when the entity
	// actually carries a construction component (which is also what drove the
	// auto-grant above). Symmetric with the un-grant + event in SeinFinishConstruction.
	if (bHasConstructionComponent)
	{
		EnqueueVisualEvent(FSeinVisualEvent::MakeConstructionStateChangedEvent(Handle, /*bUnderConstruction=*/true));
	}

	UE_LOG(LogSeinSim, Verbose, TEXT("Spawned entity %s from %s (owner: %s)"),
		*Handle.ToString(), *ActorClass->GetName(), *OwnerPlayerID.ToString());

	// Fire OnEntitySpawned AFTER components are injected + visual event
	// enqueued. Optional system subsystems (USeinCoverSubsystem etc.)
	// subscribe to discover entities with their relevant components and
	// self-register them in their per-system registries.
		{
			TGuardValue<bool> ReadOnlyGuard(bReadOnlyCallbackInProgress, true);
			TGuardValue<bool> ObserverGuard(bObserverCallbackInProgress, true);
			OnEntitySpawned.Broadcast(Handle);
	}

	return Handle;
}

FSeinEntityHandle USeinWorldSubsystem::SpawnEntityFromPlacedActor(
	ASeinActor* PlacedActor,
	FSeinPlayerID OwnerPlayerID)
{
	if (!PlacedActor)
	{
		UE_LOG(LogSeinSim, Error, TEXT("SpawnEntityFromPlacedActor: null actor"));
		return FSeinEntityHandle::Invalid();
	}
	// Legacy ArchetypeDefinition has been excised — identity / producible / extents
	// data lives on the entity bridge's ComponentData array.

	// Sim transform = editor-baked snapshot. Both LOCATION and ROTATION
	// are baked in the editor (`ASeinActor::PostEditMove`); the int64 bits
	// were serialized to the .umap. We just read them here — no FromFloat /
	// FromQuat at runtime, so cross-arch clients (PC + ARM Mac + mobile +
	// console) land on identical sim transforms.
	//
	// Missing editor bakes are invalid authored state. Converting an actor's
	// runtime float transform here would make tick zero platform-dependent and
	// bypass the bootstrap preflight's determinism guarantee.
	if (!PlacedActor->bSimLocationBaked || !PlacedActor->bSimRotationBaked)
	{
		UE_LOG(LogSeinSim, Error,
			TEXT("SpawnEntityFromPlacedActor: '%s' is missing a baked deterministic transform "
				 "(location=%d rotation=%d). Re-save or move the actor in the editor, "
				 "then run Bake Level Data before starting the match."),
			*PlacedActor->GetPathName(),
			PlacedActor->bSimLocationBaked ? 1 : 0,
			PlacedActor->bSimRotationBaked ? 1 : 0);
		return FSeinEntityHandle::Invalid();
	}
	if (!RequireStateMutationAuthorization(TEXT("SpawnEntityFromPlacedActor")))
	{
		return FSeinEntityHandle::Invalid();
	}

	const FFixedTransform SimTransform(
		PlacedActor->PlacedSimLocation,
		PlacedActor->PlacedSimRotation);

	FSeinEntityHandle Handle = EntityPool.Acquire(SimTransform, OwnerPlayerID);
	if (!Handle.IsValid())
	{
		UE_LOG(LogSeinSim, Error,
			TEXT("SpawnEntityFromPlacedActor: pool.Acquire failed for %s"),
			*PlacedActor->GetName());
		return FSeinEntityHandle::Invalid();
	}

	EntityActorClassMap.Add(Handle, PlacedActor->GetClass());

	// Inject the LIVE actor's entity component ComponentData — captures
	// per-instance edits beyond CDO defaults. Designers can drop a placed
	// actor and tune fields on the level instance; this path picks them up
	// correctly.
	if (USeinEntityComponent* EntityComp = PlacedActor->FindComponentByClass<USeinEntityComponent>())
	{
		EntityComp->InjectAuthoredComponents(*this, Handle);
	}

	InitializeEntityAbilities(Handle);

	const bool bHasConstructionComponent = GetComponent<FSeinConstructionComponent>(Handle) != nullptr;
	{
		// Initialize tag state — mirror of SpawnEntity's path. Seeds BaseTags
		// from the LIVE placed actor's entity bridge (per-instance edits to
		// BaseTags on the level instance are honored, just like other per-
		// instance authoring on placed actors).
		FSeinEntityTagState& TagState = EntityTagStates.FindOrAdd(Handle);

		if (const USeinEntityComponent* LiveBridge = PlacedActor->FindComponentByClass<USeinEntityComponent>())
		{
			TagState.BaseTags.AppendTags(LiveBridge->BaseTags);
		}

		// Identity-data first — matches SpawnEntity. Legacy archetype-def fallback is gone.
		if (const FSeinIdentityComponent* Identity = GetComponent<FSeinIdentityComponent>(Handle))
		{
			if (Identity->IdentityTag.IsValid())
			{
				TagState.BaseTags.AddTag(Identity->IdentityTag);
			}
		}
		if (bHasConstructionComponent)
		{
			TagState.BaseTags.AddTag(SeinARTSTags::State_UnderConstruction);
		}
		SeedEntityTagsFromBase(Handle);

		// AFTER tag seeding — replay any active player-scope effects that
		// grant abilities to entities matching this entity's tag state.
		// Covers the "unit spawned after tech research completed" case so
		// the new unit picks up unlocked abilities at spawn instead of
		// being permanently stuck without them.
		ReplayEffectAbilityGrants(Handle);
	}

	// Deliberately NO EntitySpawned visual event — placed actors already exist
	// in the world; firing EntitySpawned would make the actor bridge spawn a
	// second render actor in addition to the one the designer placed.
	//
	// ConstructionStateChanged IS safe to emit — it's a state notification
	// dispatched to the existing actor's construction AC (mesh swap), no
	// extra actor spawn. Designers placing under-construction stubs in the
	// editor get correct preview visuals at PIE start.
	if (bHasConstructionComponent)
	{
		EnqueueVisualEvent(FSeinVisualEvent::MakeConstructionStateChangedEvent(Handle, /*bUnderConstruction=*/true));
	}

	// Verbose: large maps register dozens of placed actors at travel time;
	// per-actor lines drown the log. Re-enable with `Log LogSeinSim Verbose`
	// when diagnosing slot-binding / handle-allocation bugs.
	UE_LOG(LogSeinSim, Verbose,
		TEXT("Auto-registered placed actor %s as entity %s (owner: %s)"),
		*PlacedActor->GetName(), *Handle.ToString(), *OwnerPlayerID.ToString());

	// Fire OnEntitySpawned — same notification placed actors get as freshly-
	// spawned ones. Cover providers placed in the level via BP get their
	// USeinCoverSubsystem registration through this path.
		{
			TGuardValue<bool> ReadOnlyGuard(bReadOnlyCallbackInProgress, true);
			TGuardValue<bool> ObserverGuard(bObserverCallbackInProgress, true);
			OnEntitySpawned.Broadcast(Handle);
	}

	return Handle;
}

FSeinEntityHandle USeinWorldSubsystem::SpawnAbstractEntity(
	const FFixedTransform& SpawnTransform,
	FSeinPlayerID OwnerPlayerID)
{
	if (!RequireStateMutationAuthorization(TEXT("SpawnAbstractEntity")))
	{
		return FSeinEntityHandle::Invalid();
	}
	// Acquire a handle from the pool; no ActorClass = no render spawn, no
	// CDO walk, no ability initialization. The caller is on the hook to
	// add whatever components the abstract entity needs via AddComponent<T>.
	FSeinEntityHandle Handle = EntityPool.Acquire(SpawnTransform, OwnerPlayerID);
	if (!Handle.IsValid())
	{
		UE_LOG(LogSeinSim, Error, TEXT("SpawnAbstractEntity: pool.Acquire failed"));
		return FSeinEntityHandle::Invalid();
	}
	// Intentionally no EntityActorClassMap entry — actor bridge no-ops on missing map entry.
	UE_LOG(LogSeinSim, Verbose, TEXT("Spawned abstract entity %s (owner: %s)"),
		*Handle.ToString(), *OwnerPlayerID.ToString());
	return Handle;
}

void USeinWorldSubsystem::DestroyEntity(FSeinEntityHandle Handle)
{
	SEIN_CHECK_NOT_PARALLEL();
	if (!RequireStateMutationAuthorization(TEXT("DestroyEntity")))
	{
		return;
	}
	if (!Handle.IsValid() || !EntityPool.IsValid(Handle))
	{
		return;
	}

	// Mark for deferred destruction
	FSeinEntity* Entity = EntityPool.Get(Handle);
	if (Entity)
	{
		Entity->SetAlive(false);
	}
	PendingDestroy.AddUnique(Handle);
}

void USeinWorldSubsystem::ProcessDeferredDestroys()
{
	// Detach this tick's work. Callbacks and broker self-cull may queue more
	// destroys; those remain in PendingDestroy for the next PostTick instead of
	// invalidating iteration or being dropped by a trailing Empty().
	TArray<FSeinEntityHandle> Draining;
	Swap(Draining, PendingDestroy);
	for (const FSeinEntityHandle& Handle : Draining)
	{
		if (!EntityPool.IsDeferredDestroyTombstone(Handle)) continue;
		TGuardValue<FSeinEntityHandle> TeardownReadGuard(
			DeferredTeardownHandle, Handle);

		// Cancel any active abilities/latent actions
		if (LatentActionManager)
		{
			LatentActionManager->CancelActionsForEntity(Handle);
		}

		// Strip any effects this entity was the source of, where the effect class
		// declares bRemoveOnSourceDeath (DESIGN §8 Q4c). Runs BEFORE component
		// storages + pool release so downstream consumers can still read the
		// effect's state while the removal hooks fire.
		RemoveEffectsFromDeadSource(Handle);

		// Containment death propagation runs before storages clear so
		// PropagateContainerDeath can still read the container's Occupants list +
		// OnEject/OnContainerDeath effect classes off FSeinContainmentData.
		if (GetDeferredTeardownComponent<FSeinContainmentData>(Handle))
		{
			PropagateContainerDeath(Handle);
		}

		// Member-side: if the dying entity is contained, evict it from its
		// container's Occupants + CurrentLoad / VisualSlotAssignments / attachment
		// slot. Mirrors the CommandBroker eviction below.
		if (const FSeinContainmentMemberData* MemComp =
			GetDeferredTeardownComponent<FSeinContainmentMemberData>(Handle))
		{
			if (EntityPool.IsValid(MemComp->CurrentContainer))
			{
				if (FSeinContainmentData* Container =
					GetComponentMutable<FSeinContainmentData>(
						MemComp->CurrentContainer))
				{
					Container->Occupants.Remove(Handle);
					const int64 RemainingLoad =
						static_cast<int64>(Container->CurrentLoad)
						- static_cast<int64>(MemComp->Size);
					Container->CurrentLoad = RemainingLoad <= 0
						? 0
						: RemainingLoad >= MAX_int32
							? MAX_int32
							: static_cast<int32>(RemainingLoad);
					if (Container->bTracksVisualSlots)
					{
						const int32 Idx = MemComp->VisualSlotIndex;
						if (Container->VisualSlotAssignments.IsValidIndex(Idx))
						{
							Container->VisualSlotAssignments[Idx] = FSeinEntityHandle();
						}
					}
					// Attachment slot (if any) — clear assignment + fire visual event.
					if (MemComp->CurrentSlot.IsValid())
					{
						if (FSeinAttachmentSpec* Spec =
							GetComponentMutable<FSeinAttachmentSpec>(
								MemComp->CurrentContainer))
						{
							Spec->Assignments.Remove(MemComp->CurrentSlot);
						}
						EnqueueVisualEvent(FSeinVisualEvent::MakeAttachmentSlotEmptiedEvent(
							MemComp->CurrentContainer, Handle, MemComp->CurrentSlot));
					}
					// Death of a contained entity doesn't spawn an exit-location event
					// — container dying with eject=false funnels through
					// PropagateContainerDeath above; death of just one occupant inside
					// a still-living container is a quieter cleanup (no world teleport).
				}
			}
		}

		// Evict from the dying entity's current broker (DESIGN §5). If this leaves
		// the broker with no members and no queued orders, cull it via DestroyEntity
		// — it'll be processed on the next tick's PostTick.
		if (const FSeinBrokerMembershipData* Memb =
			GetDeferredTeardownComponent<FSeinBrokerMembershipData>(Handle))
		{
			if (EntityPool.IsValid(Memb->CurrentBrokerHandle))
			{
				if (FSeinCommandBrokerData* Broker =
					GetComponentMutable<FSeinCommandBrokerData>(
						Memb->CurrentBrokerHandle))
				{
					Broker->Members.Remove(Handle);
					Broker->bCapabilityMapDirty = true;
					// Per-order parallelism: queue-empty implies nothing executing.
					if (Broker->bSelfCullOnEmpty && Broker->Members.Num() == 0 && Broker->OrderQueue.Num() == 0)
					{
						DestroyEntity(Memb->CurrentBrokerHandle);
					}
				}
			}
		}

		// Clear the entity from the global tag index and the named registry
		// before component storages are freed (UnindexEntityTags reads EntityTagStates).
		UnindexEntityTags(Handle);
		UnregisterHandleFromNames(Handle);

		// Player/Class effects can outlive every unit they ever granted to. Drop
		// this recipient from all live ledgers before its ability component is
		// discarded; no revoke is needed because every ability row is torn down
		// immediately below. Stable removal keeps surviving grant order canonical.
		for (FSeinPlayerID PlayerID : GetRegisteredPlayerIDs())
		{
			FSeinPlayerState* State = GetPlayerStateMutable(PlayerID);
			if (!State) continue;
			auto Prune = [Handle](TArray<FSeinActiveEffect>& Effects)
			{
				for (FSeinActiveEffect& Effect : Effects)
				{
					Effect.CommittedAbilityGrants.RemoveAll(
						[Handle](const FSeinEffectAbilityGrant& Grant)
						{
							return Grant.Recipient == Handle;
						});
				}
			};
			Prune(State->ClassEffects);
			Prune(State->PlayerEffects);
		}

		// Phase 4 architecture: release this entity's ability + resolver pool
		// slots BEFORE component storage clears. The pool slots own the
		// UObject lifetime via UPROPERTY; freeing the slot lets the GC reap
		// the ability/resolver instance the next pass.
		if (const FSeinAbilityComponent* AbilityComp =
			GetDeferredTeardownComponent<FSeinAbilityComponent>(Handle))
		{
			for (int32 ID : AbilityComp->AbilityInstanceIDs)
			{
				UnregisterAbilityInstance(ID);
			}
		}
		if (const FSeinCommandBrokerData* BrokerComp =
			GetDeferredTeardownComponent<FSeinCommandBrokerData>(Handle))
		{
			UnregisterCommandBrokerResolver(BrokerComp->ResolverID);
		}

		// Fire OnEntityDestroyed BEFORE wiping components — subscribers
		// (USeinCoverSubsystem, etc.) need to read storage to decide on
		// per-system unregistration.
		{
			TGuardValue<bool> ReadOnlyGuard(bReadOnlyCallbackInProgress, true);
			TGuardValue<bool> ObserverGuard(bObserverCallbackInProgress, true);
			TGuardValue<bool> NotificationGuard(
				bDestroyNotificationInProgress, true);
			OnEntityDestroyed.Broadcast(Handle);
		}

		// Remove all components
		for (auto& Pair : ComponentStorages)
		{
			Pair.Value->RemoveAllForEntity(Handle);
		}

		EnqueueVisualEvent(FSeinVisualEvent::MakeDestroyEvent(Handle));
		EntityActorClassMap.Remove(Handle);
		OwnerTransitionRevisions.Remove(Handle);
		EntityPool.ReleaseDeferredDestroy(Handle);

		UE_LOG(LogSeinSim, Verbose, TEXT("Destroyed entity %s"), *Handle.ToString());
	}

}

FSeinEntity* USeinWorldSubsystem::GetEntityMutable(
	FSeinEntityHandle Handle)
{
	if (!RequireMutableStateAccess(TEXT("GetEntityMutable")))
	{
		return nullptr;
	}
	return EntityPool.Get(Handle);
}

const FSeinEntity* USeinWorldSubsystem::GetEntity(FSeinEntityHandle Handle) const
{
	return EntityPool.Get(Handle);
}

bool USeinWorldSubsystem::BeginResyncCatchUpWindow(FString& OutError)
{
	OutError.Reset();
	if (!IsInGameThread())
	{
		OutError =
			TEXT("The resync catch-up window may only be opened on the game thread.");
		return false;
	}
	if (bResyncCatchUpInProgress)
	{
		OutError =
			TEXT("A resync catch-up window is already open for this world.");
		return false;
	}
	if (bSnapshotCaptureInProgress || bSnapshotRestoreInProgress
		|| bSimulationTickDispatchInProgress || SeinIsInSimContext())
	{
		OutError =
			TEXT("The resync catch-up window may only be opened between fixed ticks, outside capture/restore.");
		return false;
	}
	bResyncCatchUpInProgress = true;
	return true;
}

void USeinWorldSubsystem::EndResyncCatchUpWindow()
{
	if (!IsInGameThread())
	{
		UE_LOG(LogSeinSim, Error,
			TEXT("EndResyncCatchUpWindow rejected off the game thread."));
		return;
	}
	bResyncCatchUpInProgress = false;
	// Drop ALL residual burst budget so live pacing resumes immediately —
	// even one leftover fixed delta would double-tick the next pump. The
	// cost is at most one frame of deferred real time, which the lockstep
	// gate absorbs.
	TimeAccumulator = 0.0f;
}

FSeinEntityPool* USeinWorldSubsystem::GetEntityPoolMutable()
{
	if (!RequireMutableStateAccess(TEXT("GetEntityPoolMutable")))
	{
		return nullptr;
	}
	return &EntityPool;
}

FSeinCollisionSpatialHash*
USeinWorldSubsystem::GetCollisionSpatialHashMutable()
{
	if (!RequireMutableStateAccess(
		TEXT("GetCollisionSpatialHashMutable")))
	{
		return nullptr;
	}
	return &CollisionSpatialHash;
}

bool USeinWorldSubsystem::IsEntityAlive(FSeinEntityHandle Handle) const
{
	const FSeinEntity* Entity = EntityPool.Get(Handle);
	return Entity && Entity->IsAlive();
}

FSeinPlayerID USeinWorldSubsystem::GetEntityOwner(FSeinEntityHandle Handle) const
{
	return EntityPool.GetOwner(Handle);
}

const FSeinEntity* USeinWorldSubsystem::GetDestroyingEntity(
	FSeinEntityHandle Handle) const
{
	return bDestroyNotificationInProgress && Handle == DeferredTeardownHandle
		? EntityPool.GetDeferredDestroyTombstone(Handle)
		: nullptr;
}

FSeinPlayerID USeinWorldSubsystem::GetDestroyingEntityOwner(
	FSeinEntityHandle Handle) const
{
	return GetDestroyingEntity(Handle)
		? EntityPool.GetDeferredDestroyOwner(Handle)
		: FSeinPlayerID::Neutral();
}

void USeinWorldSubsystem::SetEntityOwner(FSeinEntityHandle Handle, FSeinPlayerID NewOwner)
{
	if (!RequireStateMutationAuthorization(TEXT("SetEntityOwner")))
	{
		return;
	}
	FSeinEntity* Entity = EntityPool.Get(Handle);
	if (!Entity || !Entity->IsAlive()) return;
	const FSeinPlayerID OldOwner = GetEntityOwner(Handle);
	if (OldOwner == NewOwner) return;

	check(OwnerTransitionDepth < MAX_int32);
	TGuardValue<int32> OwnerTransitionGuard(
		OwnerTransitionDepth, OwnerTransitionDepth + 1);

	struct FDetachedGrant
	{
		int64 EffectInstanceID = 0;
		TSubclassOf<USeinAbility> AbilityClass;
	};
	TArray<FDetachedGrant> DetachedGrants;
	if (FSeinPlayerState* OldState = GetPlayerStateMutable(OldOwner))
	{
		TArray<FSeinActiveEffect*> Effects;
		Effects.Reserve(OldState->ClassEffects.Num() + OldState->PlayerEffects.Num());
		for (FSeinActiveEffect& Effect : OldState->ClassEffects) Effects.Add(&Effect);
		for (FSeinActiveEffect& Effect : OldState->PlayerEffects) Effects.Add(&Effect);
		Effects.Sort([](const FSeinActiveEffect& A, const FSeinActiveEffect& B)
		{
			return A.EffectInstanceID < B.EffectInstanceID;
		});

		// Detach every old-owner claim before the first callback-capable revoke.
		for (FSeinActiveEffect* Effect : Effects)
		{
			for (const FSeinEffectAbilityGrant& Grant : Effect->CommittedAbilityGrants)
			{
				if (Grant.Recipient == Handle && Grant.AbilityClass)
				{
					DetachedGrants.Add({ Effect->EffectInstanceID, Grant.AbilityClass });
				}
			}
			Effect->CommittedAbilityGrants.RemoveAll(
				[Handle](const FSeinEffectAbilityGrant& Grant)
				{
					return Grant.Recipient == Handle;
				});
		}
	}

	// Publish the new owner before revoke callbacks. A recursive transfer then
	// sees the current transition, and the outer call will skip stale replay.
	EntityPool.SetOwner(Handle, NewOwner);
	uint64& Revision = OwnerTransitionRevisions.FindOrAdd(Handle);
	const uint64 ThisTransitionRevision = ++Revision;
	bool bSuperseded = false;
	for (const FDetachedGrant& Grant : DetachedGrants)
	{
		USeinAbilityBPFL::SeinRevokeAbilityFromEffect(
			this, Handle, Grant.AbilityClass, Grant.EffectInstanceID);
		const uint64* CurrentRevision = OwnerTransitionRevisions.Find(Handle);
		bSuperseded |= !CurrentRevision
			|| *CurrentRevision != ThisTransitionRevision;
	}

	Entity = EntityPool.Get(Handle);
	if (!bSuperseded && Entity && Entity->IsAlive()
		&& GetEntityOwner(Handle) == NewOwner)
	{
		ReplayEffectAbilityGrants(Handle);
	}
}

TSubclassOf<ASeinActor> USeinWorldSubsystem::GetEntityActorClass(FSeinEntityHandle Handle) const
{
	const TSubclassOf<ASeinActor>* Found = EntityActorClassMap.Find(Handle);
	return Found ? *Found : nullptr;
}

FSeinPlayerState* USeinWorldSubsystem::GetPlayerStateMutable(FSeinPlayerID PlayerID)
{
	if (!RequireMutableStateAccess(TEXT("GetPlayerStateMutable")))
	{
		return nullptr;
	}
	FSeinPlayerState* State = PlayerStates.Find(PlayerID);
	if (State)
	{
		MarkCanonicalAuxiliaryStateDirty();
	}
	return State;
}

void USeinWorldSubsystem::MarkCanonicalAuxiliaryStateDirty()
{
	CanonicalAuxiliaryMutationRevision =
		CanonicalAuxiliaryMutationRevision == MAX_uint64
			? 1
			: CanonicalAuxiliaryMutationRevision + 1;
}

// ==================== Player & Faction ====================

void USeinWorldSubsystem::RegisterPlayer(FSeinPlayerID PlayerID, FSeinFactionID FactionID, uint8 TeamID)
{
	const bool bNeutralInitialization =
		MatchBootstrapState == ESeinMatchBootstrapState::Awaiting
		&& PlayerID.IsNeutral() && PlayerStates.IsEmpty();
	if (!bNeutralInitialization
		&& !RequireStateMutationAuthorization(TEXT("RegisterPlayer")))
	{
		return;
	}
	if (PlayerStates.Contains(PlayerID))
	{
		UE_LOG(LogSeinSim, Warning, TEXT("Player %s already registered"), *PlayerID.ToString());
		return;
	}

	FSeinPlayerState NewState(PlayerID, FactionID, TeamID);

	const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>();
	const TArray<FSeinResourceDefinition>& Catalog = Settings->ResourceCatalog;

	// Layer 1: catalog defaults populate every resource the project knows
	// about. This is the "actual default" — if nothing overrides, the player
	// starts with the catalog's DefaultStartingValue / DefaultCap. Designers
	// who set DefaultStartingValue=500 on Money expect every player to start
	// with 500 Money, full stop. Faction kits and canonical match rules layer
	// on top in that order.
	for (const FSeinResourceDefinition& Def : Catalog)
	{
		if (!Def.ResourceTag.IsValid()) { continue; }
		NewState.Resources.Add(Def.ResourceTag, Def.DefaultStartingValue);
		if (Def.DefaultCap > FFixedPoint::Zero)
		{
			NewState.ResourceCaps.Add(Def.ResourceTag, Def.DefaultCap);
		}
	}

	// Layer 2: faction's ResourceKit overrides the catalog defaults for the
	// resources it cares about. Used for asymmetric factions (e.g. one starts
	// with bonus fuel, another with bonus manpower) without rewriting the
	// catalog per faction.
	if (TObjectPtr<USeinFaction>* FactionPtr = Factions.Find(FactionID))
	{
		for (const FSeinFactionResourceEntry& KitEntry : (*FactionPtr)->ResourceKit)
		{
			if (!KitEntry.ResourceTag.IsValid()) { continue; }

			const FSeinResourceDefinition* CatalogEntry = Catalog.FindByPredicate(
				[&](const FSeinResourceDefinition& D) { return D.ResourceTag == KitEntry.ResourceTag; });

			const FFixedPoint StartingValue = KitEntry.bOverrideStartingValue
				? KitEntry.StartingValueOverride
				: (CatalogEntry ? CatalogEntry->DefaultStartingValue : FFixedPoint::Zero);

			const FFixedPoint Cap = KitEntry.bOverrideCap
				? KitEntry.CapOverride
				: (CatalogEntry ? CatalogEntry->DefaultCap : FFixedPoint::Zero);

			NewState.Resources.Add(KitEntry.ResourceTag, StartingValue);
			if (Cap > FFixedPoint::Zero)
			{
				NewState.ResourceCaps.Add(KitEntry.ResourceTag, Cap);
			}
		}
	}

	// Layer 3: canonical match rules override catalog/faction defaults. This is
	// Core-owned materialization state, not a GameMode side channel, so every
	// gameplay shell and network topology gets the same result.
	if (!PlayerID.IsNeutral())
	{
		if (const FSeinMatchBootstrapRules* Rules =
			FindMatchExtension<FSeinMatchBootstrapRules>(CurrentMatchSettings))
		{
			for (const FSeinStartingResourceOverride& Override :
				Rules->StartingResources)
			{
				NewState.Resources.Add(Override.ResourceTag, Override.Amount);
			}
		}
	}

	PlayerStates.Add(PlayerID, MoveTemp(NewState));
	SeedTeamPairCapabilitiesForPlayer(PlayerID);
	MarkCanonicalAuxiliaryStateDirty();

	UE_LOG(LogSeinSim, Log, TEXT("Registered player %s (faction: %s, team: %d)"),
		*PlayerID.ToString(), *FactionID.ToString(), TeamID);
}

const FSeinPlayerState* USeinWorldSubsystem::GetPlayerState(FSeinPlayerID PlayerID) const
{
	return PlayerStates.Find(PlayerID);
}

bool USeinWorldSubsystem::GetPlayerStateCopy(FSeinPlayerID PlayerID, FSeinPlayerState& OutState) const
{
	const FSeinPlayerState* Found = PlayerStates.Find(PlayerID);
	if (Found)
	{
		OutState = *Found;
		return true;
	}
	return false;
}

TArray<FSeinPlayerID> USeinWorldSubsystem::GetRegisteredPlayerIDs() const
{
	TArray<FSeinPlayerID> PlayerIDs;
	PlayerStates.GetKeys(PlayerIDs);
	PlayerIDs.Sort();
	return PlayerIDs;
}

bool USeinWorldSubsystem::HasPairCapability(
	FSeinPlayerID SourcePlayer,
	FSeinPlayerID TargetPlayer,
	FGameplayTag CapabilityTag) const
{
	if (!IsPairCapabilityTag(CapabilityTag))
	{
		return false;
	}
	if (SourcePlayer == TargetPlayer)
	{
		return SourcePlayer.IsValid() && PlayerStates.Contains(SourcePlayer);
	}
	FSeinPairCapabilityKey Key;
	Key.SourcePlayer = SourcePlayer;
	Key.TargetPlayer = TargetPlayer;
	Key.CapabilityTag = CapabilityTag;
	const int32* Count = PairCapabilityEffectiveRefCounts.Find(Key);
	return Count && *Count > 0;
}

bool USeinWorldSubsystem::ShouldPresentPlayerAsFriendly(
	FSeinPlayerID SourcePlayer,
	FSeinPlayerID ViewingPlayer) const
{
	return HasPairCapability(
		SourcePlayer,
		ViewingPlayer,
		SeinARTSTags::Relationship_Capability_PresentAsFriendly);
}

bool USeinWorldSubsystem::GrantPairCapability(
	FSeinPlayerID SourcePlayer,
	FSeinPlayerID TargetPlayer,
	FGameplayTag CapabilityTag,
	FGameplayTag SourceKindTag,
	int64 SourceInstanceID)
{
	if (!RequireStateMutationAuthorization(TEXT("GrantPairCapability")))
	{
		return false;
	}
	if (!SourcePlayer.IsValid() || !TargetPlayer.IsValid()
		|| SourcePlayer == TargetPlayer
		|| !IsPairCapabilityTag(CapabilityTag)
		|| !IsPairCapabilitySourceKindTag(SourceKindTag)
		|| SourceInstanceID <= 0
		|| !PlayerStates.Contains(SourcePlayer)
		|| !PlayerStates.Contains(TargetPlayer))
	{
		return false;
	}
	if (!ValidatePairCapabilityState())
	{
		UE_LOG(LogSeinSim, Error,
			TEXT("GrantPairCapability: repairing inconsistent derived pair-capability cache."));
		RebuildPairCapabilityEffectiveCache();
		if (!ValidatePairCapabilityState())
		{
			UE_LOG(LogSeinSim, Error,
				TEXT("GrantPairCapability: authoritative pair-capability state is invalid; mutation rejected."));
			return false;
		}
	}

	FSeinPairCapabilitySourceKey SourceKey;
	SourceKey.SourcePlayer = SourcePlayer;
	SourceKey.TargetPlayer = TargetPlayer;
	SourceKey.CapabilityTag = CapabilityTag;
	SourceKey.SourceKindTag = SourceKindTag;
	SourceKey.SourceInstanceID = SourceInstanceID;
	int32& SourceCount =
		PairCapabilitySourceRefCounts.FindOrAdd(SourceKey);
	if (SourceCount == MAX_int32)
	{
		UE_LOG(LogSeinSim, Error,
			TEXT("GrantPairCapability: source refcount saturated for %s -> %s capability %s source %s:%lld."),
			*SourcePlayer.ToString(), *TargetPlayer.ToString(),
			*CapabilityTag.ToString(), *SourceKindTag.ToString(),
			SourceInstanceID);
		return false;
	}
	++SourceCount;

	FSeinPairCapabilityKey EffectiveKey;
	EffectiveKey.SourcePlayer = SourcePlayer;
	EffectiveKey.TargetPlayer = TargetPlayer;
	EffectiveKey.CapabilityTag = CapabilityTag;
	int32& EffectiveCount =
		PairCapabilityEffectiveRefCounts.FindOrAdd(EffectiveKey);
	if (EffectiveCount == MAX_int32)
	{
		--SourceCount;
		if (SourceCount == 0)
		{
			PairCapabilitySourceRefCounts.Remove(SourceKey);
		}
		UE_LOG(LogSeinSim, Error,
			TEXT("GrantPairCapability: effective refcount saturated for %s -> %s capability %s."),
			*SourcePlayer.ToString(), *TargetPlayer.ToString(),
			*CapabilityTag.ToString());
		return false;
	}
	++EffectiveCount;
	MarkCanonicalAuxiliaryStateDirty();
	return true;
}

bool USeinWorldSubsystem::RevokePairCapability(
	FSeinPlayerID SourcePlayer,
	FSeinPlayerID TargetPlayer,
	FGameplayTag CapabilityTag,
	FGameplayTag SourceKindTag,
	int64 SourceInstanceID)
{
	if (!RequireStateMutationAuthorization(TEXT("RevokePairCapability")))
	{
		return false;
	}
	if (!SourcePlayer.IsValid() || !TargetPlayer.IsValid()
		|| SourcePlayer == TargetPlayer
		|| !IsPairCapabilityTag(CapabilityTag)
		|| !IsPairCapabilitySourceKindTag(SourceKindTag)
		|| SourceInstanceID <= 0)
	{
		return false;
	}
	if (!ValidatePairCapabilityState())
	{
		UE_LOG(LogSeinSim, Error,
			TEXT("RevokePairCapability: repairing inconsistent derived pair-capability cache."));
		RebuildPairCapabilityEffectiveCache();
		if (!ValidatePairCapabilityState())
		{
			UE_LOG(LogSeinSim, Error,
				TEXT("RevokePairCapability: authoritative pair-capability state is invalid; mutation rejected."));
			return false;
		}
	}

	FSeinPairCapabilitySourceKey SourceKey;
	SourceKey.SourcePlayer = SourcePlayer;
	SourceKey.TargetPlayer = TargetPlayer;
	SourceKey.CapabilityTag = CapabilityTag;
	SourceKey.SourceKindTag = SourceKindTag;
	SourceKey.SourceInstanceID = SourceInstanceID;
	int32* SourceCount =
		PairCapabilitySourceRefCounts.Find(SourceKey);
	if (!SourceCount || *SourceCount <= 0)
	{
		return false;
	}
	--(*SourceCount);
	if (*SourceCount == 0)
	{
		PairCapabilitySourceRefCounts.Remove(SourceKey);
	}

	FSeinPairCapabilityKey EffectiveKey;
	EffectiveKey.SourcePlayer = SourcePlayer;
	EffectiveKey.TargetPlayer = TargetPlayer;
	EffectiveKey.CapabilityTag = CapabilityTag;
	if (int32* EffectiveCount =
		PairCapabilityEffectiveRefCounts.Find(EffectiveKey))
	{
		--(*EffectiveCount);
		if (*EffectiveCount <= 0)
		{
			PairCapabilityEffectiveRefCounts.Remove(EffectiveKey);
		}
	}
	else
	{
		UE_LOG(LogSeinSim, Error,
			TEXT("RevokePairCapability: missing effective cache for %s -> %s capability %s."),
			*SourcePlayer.ToString(), *TargetPlayer.ToString(),
			*CapabilityTag.ToString());
		RebuildPairCapabilityEffectiveCache();
	}
	MarkCanonicalAuxiliaryStateDirty();
	return true;
}

TArray<FSeinPairCapabilityGrantRecord>
USeinWorldSubsystem::GetPairCapabilityGrantRecords() const
{
	TArray<FSeinPairCapabilitySourceKey> Keys;
	PairCapabilitySourceRefCounts.GetKeys(Keys);
	Keys.Sort([](
		const FSeinPairCapabilitySourceKey& A,
		const FSeinPairCapabilitySourceKey& B)
	{
		if (A.SourcePlayer != B.SourcePlayer)
		{
			return A.SourcePlayer < B.SourcePlayer;
		}
		if (A.TargetPlayer != B.TargetPlayer)
		{
			return A.TargetPlayer < B.TargetPlayer;
		}
		const int32 CapabilityOrder = A.CapabilityTag.GetTagName().Compare(
			B.CapabilityTag.GetTagName());
		if (CapabilityOrder != 0)
		{
			return CapabilityOrder < 0;
		}
		const int32 SourceKindOrder = A.SourceKindTag.GetTagName().Compare(
			B.SourceKindTag.GetTagName());
		return SourceKindOrder != 0
			? SourceKindOrder < 0
			: A.SourceInstanceID < B.SourceInstanceID;
	});

	TArray<FSeinPairCapabilityGrantRecord> Records;
	Records.Reserve(Keys.Num());
	for (const FSeinPairCapabilitySourceKey& Key : Keys)
	{
		const int32 Count =
			PairCapabilitySourceRefCounts.FindChecked(Key);
		if (Count <= 0)
		{
			continue;
		}
		FSeinPairCapabilityGrantRecord& Record =
			Records.AddDefaulted_GetRef();
		Record.SourcePlayer = Key.SourcePlayer;
		Record.TargetPlayer = Key.TargetPlayer;
		Record.CapabilityTag = Key.CapabilityTag;
		Record.SourceKindTag = Key.SourceKindTag;
		Record.SourceInstanceID = Key.SourceInstanceID;
		Record.RefCount = Count;
	}
	return Records;
}

void USeinWorldSubsystem::SeedTeamPairCapabilitiesForPlayer(
	FSeinPlayerID PlayerID)
{
	const FSeinPlayerState* NewState = PlayerStates.Find(PlayerID);
	if (!NewState || NewState->TeamID == 0 || PlayerID.IsNeutral())
	{
		return;
	}
	TArray<FSeinPlayerID> ExistingPlayers;
	PlayerStates.GetKeys(ExistingPlayers);
	ExistingPlayers.Sort();
	for (const FSeinPlayerID OtherID : ExistingPlayers)
	{
		if (OtherID == PlayerID || OtherID.IsNeutral())
		{
			continue;
		}
		const FSeinPlayerState* OtherState = PlayerStates.Find(OtherID);
		if (!OtherState || OtherState->TeamID != NewState->TeamID)
		{
			continue;
		}
		GrantPairCapability(
			PlayerID,
			OtherID,
			SeinARTSTags::Relationship_Capability_PresentAsFriendly,
			SeinARTSTags::Relationship_Source_TeamBootstrap,
			static_cast<int64>(NewState->TeamID));
		GrantPairCapability(
			OtherID,
			PlayerID,
			SeinARTSTags::Relationship_Capability_PresentAsFriendly,
			SeinARTSTags::Relationship_Source_TeamBootstrap,
			static_cast<int64>(NewState->TeamID));
	}
}

void USeinWorldSubsystem::RebuildPairCapabilityEffectiveCache()
{
	PairCapabilityEffectiveRefCounts.Reset();
	for (const TPair<FSeinPairCapabilitySourceKey, int32>& Pair :
		PairCapabilitySourceRefCounts)
	{
		if (Pair.Value <= 0)
		{
			continue;
		}
		FSeinPairCapabilityKey EffectiveKey;
		EffectiveKey.SourcePlayer = Pair.Key.SourcePlayer;
		EffectiveKey.TargetPlayer = Pair.Key.TargetPlayer;
		EffectiveKey.CapabilityTag = Pair.Key.CapabilityTag;
		int32& Count =
			PairCapabilityEffectiveRefCounts.FindOrAdd(EffectiveKey);
		const int64 NewCount =
			static_cast<int64>(Count) + static_cast<int64>(Pair.Value);
		if (NewCount > MAX_int32)
		{
			UE_LOG(LogSeinSim, Error,
				TEXT("RebuildPairCapabilityEffectiveCache: aggregate refcount overflow."));
			PairCapabilityEffectiveRefCounts.Reset();
			return;
		}
		Count = static_cast<int32>(NewCount);
	}
}

bool USeinWorldSubsystem::ValidatePairCapabilityState() const
{
	TMap<FSeinPairCapabilityKey, int32> ExpectedEffective;
	for (const TPair<FSeinPairCapabilitySourceKey, int32>& Pair :
		PairCapabilitySourceRefCounts)
	{
		const FSeinPairCapabilitySourceKey& Key = Pair.Key;
		if (Pair.Value <= 0 || !Key.SourcePlayer.IsValid()
			|| !Key.TargetPlayer.IsValid()
			|| Key.SourcePlayer == Key.TargetPlayer
			|| !IsPairCapabilityTag(Key.CapabilityTag)
			|| !IsPairCapabilitySourceKindTag(Key.SourceKindTag)
			|| Key.SourceInstanceID <= 0
			|| !PlayerStates.Contains(Key.SourcePlayer)
			|| !PlayerStates.Contains(Key.TargetPlayer))
		{
			return false;
		}
		FSeinPairCapabilityKey EffectiveKey;
		EffectiveKey.SourcePlayer = Key.SourcePlayer;
		EffectiveKey.TargetPlayer = Key.TargetPlayer;
		EffectiveKey.CapabilityTag = Key.CapabilityTag;
		int32& ExpectedCount = ExpectedEffective.FindOrAdd(EffectiveKey);
		if (ExpectedCount > MAX_int32 - Pair.Value)
		{
			return false;
		}
		ExpectedCount += Pair.Value;
	}
	if (ExpectedEffective.Num()
		!= PairCapabilityEffectiveRefCounts.Num())
	{
		return false;
	}
	for (const TPair<FSeinPairCapabilityKey, int32>& Pair :
		ExpectedEffective)
	{
		const int32* Actual =
			PairCapabilityEffectiveRefCounts.Find(Pair.Key);
		if (!Actual || *Actual != Pair.Value || *Actual <= 0)
		{
			return false;
		}
	}
	return true;
}

void USeinWorldSubsystem::RegisterFaction(USeinFaction* Faction)
{
	if (MatchBootstrapState != ESeinMatchBootstrapState::Applying
		|| bIsRunning || CurrentTick != 0)
	{
		UE_LOG(LogSeinSim, Error,
			TEXT("RegisterFaction rejected outside stopped tick-zero bootstrap Applying."));
		return;
	}
	if (!RequireStateMutationAuthorization(TEXT("RegisterFaction")))
	{
		return;
	}
	if (!Faction) return;
	Factions.Add(Faction->FactionID, Faction);
	MarkCanonicalAuxiliaryStateDirty();
	UE_LOG(LogSeinSim, Log, TEXT("Registered faction: %s (FactionID=%u)"),
		*Faction->FactionName.ToString(), Faction->FactionID.Value);
}

bool USeinWorldSubsystem::SeedSimRandom(
	const FSeinMatchBootstrapAuthorityHandle& Authority,
	int64 Seed,
	FString& OutError)
{
	OutError.Reset();
	if (!IsExactMatchBootstrapAuthority(Authority))
	{
		OutError =
			TEXT("Deterministic session seeding requires the exact claimed bootstrap authority.");
		return false;
	}
	if (MatchBootstrapState != ESeinMatchBootstrapState::Awaiting
		|| bIsRunning || CurrentTick != 0
		|| MatchState != ESeinMatchState::Lobby)
	{
		OutError = TEXT(
			"Deterministic session seeding is available only in a stopped tick-zero Awaiting world.");
		return false;
	}
	if (bSimSessionSeedInstalled)
	{
		if (SimSessionSeed != Seed)
		{
			OutError = FString::Printf(
				TEXT("Deterministic session seeding rejected conflicting retry (%lld != %lld)."),
				Seed,
				SimSessionSeed);
			return false;
		}
		return true;
	}
	SimSessionSeed = Seed;
	SimRandom.SetSeed(static_cast<uint64>(Seed));
	bSimSessionSeedInstalled = true;
	UE_LOG(LogSeinSim, Log, TEXT("SeedSimRandom: PRNG seeded with %lld."), Seed);
	return true;
}

// ============================================================================
// Ability + Resolver pools (Phase 4 architecture cleanup)
// ============================================================================
//
// Generic pool primitive shared between abilities and resolvers. Free-list-
// recycled, deterministic by allocation order, GC-rooted via UPROPERTY-tagged
// pool arrays on the subsystem.

namespace
{
	template <typename T>
	int32 PoolRegister(TArray<TObjectPtr<T>>& Pool, TArray<int32>& FreeList, T* Obj)
	{
		if (!Obj) return INDEX_NONE;
		if (FreeList.Num() > 0)
		{
			const int32 ID = FreeList.Pop(EAllowShrinking::No);
			Pool[ID] = Obj;
			return ID;
		}
		return Pool.Add(Obj);
	}

	template <typename T>
	void PoolUnregister(TArray<TObjectPtr<T>>& Pool, TArray<int32>& FreeList, int32 ID)
	{
		if (!Pool.IsValidIndex(ID)) return;
		if (Pool[ID] == nullptr) return; // already released
		Pool[ID] = nullptr;
		FreeList.Add(ID);
	}

	template <typename T>
	T* PoolGet(const TArray<TObjectPtr<T>>& Pool, int32 ID)
	{
		return Pool.IsValidIndex(ID) ? Pool[ID].Get() : nullptr;
	}
}

int32 USeinWorldSubsystem::RegisterAbilityInstance(USeinAbility* Ability)
{
	if (!RequireStateMutationAuthorization(TEXT("RegisterAbilityInstance")))
	{
		return INDEX_NONE;
	}
	if (!Ability || Ability->RuntimePoolID != INDEX_NONE)
	{
		return INDEX_NONE;
	}
	const int32 ID =
		PoolRegister(AbilityPool, AbilityPoolFreeList, Ability);
	if (ID != INDEX_NONE)
	{
		Ability->RuntimePoolID = ID;
		// Preserve existing slot revisions when the pool grows. SetNumZeroed
		// clears the whole allocation and would make unchanged live objects look
		// untracked every time a new ability occupied an appended slot.
		AbilityPoolStateRevisions.SetNum(AbilityPool.Num());
		++AbilityPoolMutationRevision;
		if (AbilityPoolMutationRevision == 0)
		{
			++AbilityPoolMutationRevision;
		}
		AbilityPoolStateRevisions[ID] = AbilityPoolMutationRevision;
		++AbilityPoolTopologyRevision;
		if (AbilityPoolTopologyRevision == 0)
		{
			++AbilityPoolTopologyRevision;
		}
	}
	return ID;
}

void USeinWorldSubsystem::UnregisterAbilityInstance(int32 AbilityID)
{
	if (!RequireStateMutationAuthorization(TEXT("UnregisterAbilityInstance")))
	{
		return;
	}
	if (USeinAbility* Ability = GetAbilityInstance(AbilityID))
	{
		Ability->RuntimePoolID = INDEX_NONE;
	}
	PoolUnregister(AbilityPool, AbilityPoolFreeList, AbilityID);
	if (AbilityPoolStateRevisions.IsValidIndex(AbilityID))
	{
		AbilityPoolStateRevisions[AbilityID] = 0;
	}
	++AbilityPoolTopologyRevision;
	if (AbilityPoolTopologyRevision == 0)
	{
		++AbilityPoolTopologyRevision;
	}
}

USeinAbility* USeinWorldSubsystem::GetAbilityInstance(int32 AbilityID) const
{
	return PoolGet(AbilityPool, AbilityID);
}

int32 USeinWorldSubsystem::FindAbilityInstanceID(
	const USeinAbility* Ability) const
{
	if (!Ability)
	{
		return INDEX_NONE;
	}
	const int32 ID = Ability->RuntimePoolID;
	return AbilityPool.IsValidIndex(ID)
		&& AbilityPool[ID].Get() == Ability
			? ID
			: INDEX_NONE;
}

void USeinWorldSubsystem::MarkAbilityRuntimeStateDirty(
	const USeinAbility* Ability)
{
	if (!Ability)
	{
		return;
	}
	const int32 ID = FindAbilityInstanceID(Ability);
	if (!AbilityPoolStateRevisions.IsValidIndex(ID))
	{
		return;
	}
	++AbilityPoolMutationRevision;
	if (AbilityPoolMutationRevision == 0)
	{
		++AbilityPoolMutationRevision;
	}
	AbilityPoolStateRevisions[ID] = AbilityPoolMutationRevision;
}

bool USeinWorldSubsystem::TryAllocateAbilityActivationID(
	int64& OutID)
{
	OutID = 0;
	if (NextAbilityActivationID <= 0
		|| NextAbilityActivationID == MAX_int64)
	{
		return false;
	}
	OutID = NextAbilityActivationID++;
	return true;
}

bool USeinWorldSubsystem::RegisterAbilityActivity(USeinAbility* Ability)
{
	if (!Ability || Ability->WorldSubsystem != this || !Ability->bIsActive)
	{
		return false;
	}
	const int32 AbilityID = FindAbilityInstanceID(Ability);
	FSeinAbilityComponent* Component =
		GetComponentMutable<FSeinAbilityComponent>(Ability->OwnerEntity);
	if (AbilityID == INDEX_NONE || !Component
		|| !Component->AbilityInstanceIDs.Contains(AbilityID))
	{
		UE_LOG(LogSeinSim, Error,
			TEXT("RegisterAbilityActivity[%s]: pooled ability is not owned by entity %s."),
			*Ability->GetName(), *Ability->OwnerEntity.ToString());
		return false;
	}

	if (Ability->bIsPassive)
	{
		if (Component->ActiveAbilityID == AbilityID)
		{
			UE_LOG(LogSeinSim, Error,
				TEXT("RegisterAbilityActivity[%s]: passive ability occupies the primary slot."),
				*Ability->GetName());
			return false;
		}
		Component->ActivePassiveIDs.AddUnique(AbilityID);
		return true;
	}

	if (Component->ActivePassiveIDs.Contains(AbilityID))
	{
		UE_LOG(LogSeinSim, Error,
			TEXT("RegisterAbilityActivity[%s]: primary ability occupies the passive list."),
			*Ability->GetName());
		return false;
	}
	if (Component->ActiveAbilityID != INDEX_NONE
		&& Component->ActiveAbilityID != AbilityID)
	{
		const int32 OccupantID = Component->ActiveAbilityID;
		const USeinAbility* Occupant = GetAbilityInstance(OccupantID);
		const bool bLiveOwnedPrimary = Occupant
			&& !Occupant->bIsPassive
			&& Occupant->bIsActive
			&& Occupant->OwnerEntity == Ability->OwnerEntity
			&& Component->AbilityInstanceIDs.Contains(OccupantID);
		if (bLiveOwnedPrimary)
		{
			UE_LOG(LogSeinSim, Warning,
				TEXT("RegisterAbilityActivity[%s]: entity %s already has active primary %s; use broker or cancellation-tag arbitration before activation."),
				*Ability->GetName(), *Ability->OwnerEntity.ToString(),
				*Occupant->GetName());
			return false;
		}
		// Repair only an impossible stale locator. Live conflicting ownership
		// always fails closed above; it is never silently displaced.
		Component->ActiveAbilityID = INDEX_NONE;
	}
	Component->ActiveAbilityID = AbilityID;
	return true;
}

void USeinWorldSubsystem::UnregisterAbilityActivity(
	const USeinAbility* Ability)
{
	if (!Ability || Ability->WorldSubsystem != this)
	{
		return;
	}
	const int32 AbilityID = FindAbilityInstanceID(Ability);
	FSeinAbilityComponent* Component =
		GetComponentMutable<FSeinAbilityComponent>(Ability->OwnerEntity);
	if (AbilityID == INDEX_NONE || !Component
		|| !Component->AbilityInstanceIDs.Contains(AbilityID))
	{
		return;
	}
	if (Component->ActiveAbilityID == AbilityID)
	{
		Component->ActiveAbilityID = INDEX_NONE;
	}
	Component->ActivePassiveIDs.Remove(AbilityID);
}

int32 USeinWorldSubsystem::RegisterCommandBrokerResolver(USeinCommandBrokerResolver* Resolver)
{
	if (!RequireStateMutationAuthorization(TEXT("RegisterCommandBrokerResolver")))
	{
		return INDEX_NONE;
	}
	const int32 ID = PoolRegister(
		CommandBrokerResolverPool,
		CommandBrokerResolverPoolFreeList,
		Resolver);
	if (ID != INDEX_NONE)
	{
		CommandBrokerResolverPoolStateRevisions.SetNum(
			CommandBrokerResolverPool.Num());
		++CommandBrokerResolverPoolMutationRevision;
		if (CommandBrokerResolverPoolMutationRevision == 0)
		{
			++CommandBrokerResolverPoolMutationRevision;
		}
		CommandBrokerResolverPoolStateRevisions[ID] =
			CommandBrokerResolverPoolMutationRevision;
		++CommandBrokerResolverPoolTopologyRevision;
		if (CommandBrokerResolverPoolTopologyRevision == 0)
		{
			++CommandBrokerResolverPoolTopologyRevision;
		}
	}
	return ID;
}

void USeinWorldSubsystem::UnregisterCommandBrokerResolver(int32 ResolverID)
{
	if (!RequireStateMutationAuthorization(TEXT("UnregisterCommandBrokerResolver")))
	{
		return;
	}
	PoolUnregister(CommandBrokerResolverPool, CommandBrokerResolverPoolFreeList, ResolverID);
	if (CommandBrokerResolverPoolStateRevisions.IsValidIndex(ResolverID))
	{
		CommandBrokerResolverPoolStateRevisions[ResolverID] = 0;
	}
	++CommandBrokerResolverPoolTopologyRevision;
	if (CommandBrokerResolverPoolTopologyRevision == 0)
	{
		++CommandBrokerResolverPoolTopologyRevision;
	}
}

USeinCommandBrokerResolver* USeinWorldSubsystem::GetCommandBrokerResolver(int32 ResolverID) const
{
	return PoolGet(CommandBrokerResolverPool, ResolverID);
}

void USeinWorldSubsystem::MarkCommandBrokerResolverRuntimeStateDirty(
	const USeinCommandBrokerResolver* Resolver)
{
	if (!Resolver)
	{
		return;
	}
	const int32 ID = CommandBrokerResolverPool.IndexOfByPredicate(
		[Resolver](const TObjectPtr<USeinCommandBrokerResolver>& Candidate)
		{
			return Candidate.Get() == Resolver;
		});
	if (!CommandBrokerResolverPoolStateRevisions.IsValidIndex(ID))
	{
		return;
	}
	++CommandBrokerResolverPoolMutationRevision;
	if (CommandBrokerResolverPoolMutationRevision == 0)
	{
		++CommandBrokerResolverPoolMutationRevision;
	}
	CommandBrokerResolverPoolStateRevisions[ID] =
		CommandBrokerResolverPoolMutationRevision;
}

// ============================================================================
// World Snapshot — Capture + Restore
// ============================================================================

void USeinWorldSubsystem::CaptureSnapshot(FSeinWorldSnapshot& OutSnapshot)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(Sein_World_CaptureSnapshot);

	// The API predates fallible checkpoint capture. Clear a reused destination
	// and leave version zero on refusal so callers cannot serialize stale or
	// default-initialized data as a valid checkpoint.
	OutSnapshot = FSeinWorldSnapshot();
	OutSnapshot.SnapshotVersion = 0;
	if (bSnapshotCaptureInProgress || bSnapshotRestoreInProgress)
	{
		UE_LOG(LogSeinSim, Error,
			TEXT("CaptureSnapshot: recursive or restore-overlapping capture is not permitted."));
		return;
	}
	TGuardValue<bool> CaptureInProgressGuard(
		bSnapshotCaptureInProgress, true);
	FSeinWorldSnapshotReferenceGuard SnapshotGCGuard(OutSnapshot);
	if (bSimulationTickDispatchInProgress || SeinIsInSimContext()
		|| OwnerTransitionDepth != 0
		|| !PendingDestroy.IsEmpty() || !PendingEffectApplies.IsEmpty())
	{
		UE_LOG(LogSeinSim, Error,
			TEXT("CaptureSnapshot: checkpoint capture requires a quiescent fixed-tick boundary with empty deferred queues."));
		return;
	}
	if (bReplayOwnsExternalCommandIngress || PendingReplayCommands.Num() != 0)
	{
		UE_LOG(LogSeinSim, Error,
			TEXT("CaptureSnapshot: active replay ingress must stop before checkpoint capture."));
		return;
	}
	if (bResyncCatchUpInProgress)
	{
		// A peer that adopted a checkpoint and is still consuming its command
		// tail holds pre-frontier state. `!bIsRunning` gates its input but NOT
		// capture, so without this refusal a catching-up peer could emit a
		// checkpoint healthy peers might treat as authoritative.
		UE_LOG(LogSeinSim, Error,
			TEXT("CaptureSnapshot: a world catching up from an adopted checkpoint cannot produce checkpoints until resync activation completes."));
		return;
	}
	if (MatchBootstrapState != ESeinMatchBootstrapState::Consumed
		|| !MatchBootstrapReceipt.IsValid()
		|| !MatchBootstrapAuthorizationContextDigest.IsValid()
		|| MatchBootstrapReceipt.ContractDigest != MatchSettingsDigest
		|| !bExecutionTopologyFrozen || !bExecutionTopologyValid
		|| !ExecutionTopologyDigest.IsValid()
		|| !bSimSessionSeedInstalled
		|| !MatchBootstrapNativeContributors.IsEmpty())
	{
		UE_LOG(LogSeinSim, Error,
			TEXT("CaptureSnapshot: only a consumed, frozen bootstrap with a valid execution topology can produce a checkpoint."));
		return;
	}
	if (!IsSimulationContentReady()
		|| MatchBootstrapReceipt.SimulationContentDigest
			!= SimulationContentDigest)
	{
		UE_LOG(LogSeinSim, Error,
			TEXT("CaptureSnapshot: simulation-content compatibility is unavailable or no longer matches the consumed bootstrap."));
		return;
	}
	if (EntityPool.GetCapacity()
		> FSeinWorldSnapshot::MaxSupportedEntitySlotIndex)
	{
		UE_LOG(LogSeinSim, Error,
			TEXT("CaptureSnapshot: entity-pool capacity exceeds the checkpoint reconstruction bound."));
		return;
	}
	if (!NativeCanonicalStateSchema.IsValid()
		|| !LatentActionCodecManifest.IsValid()
		|| !LatentActionCodecManifest.GetDigest().IsValid()
		|| !PoolObjectCodecManifest.IsValid()
		|| !PoolObjectCodecManifest.GetDigest().IsValid()
		|| !LatentActionManager
		|| !CanonicalStateValues.IsSealed()
		|| CanonicalStateValues.GetContractDigest()
			!= MatchBootstrapReceipt.StateContractDigest)
	{
		UE_LOG(LogSeinSim, Error,
			TEXT("CaptureSnapshot: canonical state contract is unavailable or no longer matches the consumed bootstrap."));
		return;
	}
	FString PoolLeaseError;
	if (!PoolObjectCodecManifest.VerifyProviderLeases(
		PoolLeaseError))
	{
		UE_LOG(LogSeinSim, Error,
			TEXT("CaptureSnapshot: pool-object provider lease expired (%s)."),
			*PoolLeaseError);
		return;
	}
	FString ContainmentError;
	if (!ValidateContainmentState(ContainmentError))
	{
		UE_LOG(LogSeinSim, Error,
			TEXT("CaptureSnapshot: invalid containment state (%s)."),
			*ContainmentError);
		return;
	}

	OutSnapshot.SnapshotVersion = FSeinWorldSnapshot::CurrentVersion;
	OutSnapshot.FrameworkVersion = SeinReplayCompatibility::GetFrameworkVersion();
	OutSnapshot.GameVersion = SeinReplayCompatibility::GetGameVersion();
	OutSnapshot.MapIdentifier =
		SeinReplayCompatibility::GetMapIdentifier(GetWorld());
	OutSnapshot.CapturedAt = FDateTime::UtcNow();
	OutSnapshot.CommandProtocolDigest = CommandProtocolDigest;
	OutSnapshot.SimulationContentDigest = SimulationContentDigest;
	OutSnapshot.MatchSettingsDigest = MatchSettingsDigest;
	OutSnapshot.ConfigFingerprint = ConfigFingerprint;
	OutSnapshot.BootstrapCheckpoint.FormatVersion =
		FSeinSnapshotBootstrapCheckpoint::CurrentFormatVersion;
	OutSnapshot.BootstrapCheckpoint.BootstrapState = MatchBootstrapState;
	OutSnapshot.BootstrapCheckpoint.Receipt = MatchBootstrapReceipt;
	OutSnapshot.BootstrapCheckpoint.AuthorizationContextDigest =
		MatchBootstrapAuthorizationContextDigest;
	OutSnapshot.BootstrapCheckpoint.InitialStateContributions.Reset(
		MatchBootstrapValueContributions.Num());
	for (const FSeinCanonicalInitialStateValueContribution& Value :
		MatchBootstrapValueContributions)
	{
		FSeinSnapshotInitialStateContribution& SnapshotValue =
			OutSnapshot.BootstrapCheckpoint.InitialStateContributions
				.AddDefaulted_GetRef();
		SnapshotValue.StableContributorID = Value.StableContributorID;
		SnapshotValue.SchemaVersion = Value.SchemaVersion;
		SnapshotValue.ValueDigest = Value.ValueDigest;
	}
	OutSnapshot.BootstrapCheckpoint.InitialStateContributions.Sort(
		[](const FSeinSnapshotInitialStateContribution& A,
			const FSeinSnapshotInitialStateContribution& B)
		{
			return FSeinCanonicalInitialStateDigest::CanonicalContributorID(
				A.StableContributorID)
				< FSeinCanonicalInitialStateDigest::CanonicalContributorID(
					B.StableContributorID);
		});
	OutSnapshot.BootstrapCheckpoint.FactionRegistrations.Reset(Factions.Num());
	for (const auto& Pair : Factions)
	{
		const USeinFaction* Faction = Pair.Value.Get();
		if (!Pair.Key.IsValid() || !Faction || Faction->FactionID != Pair.Key
			|| Faction->GetPathName().IsEmpty())
		{
			OutSnapshot = FSeinWorldSnapshot();
			OutSnapshot.SnapshotVersion = 0;
			UE_LOG(LogSeinSim, Error,
				TEXT("CaptureSnapshot: bootstrap faction registry is invalid."));
			return;
		}

		FSeinSnapshotFactionRegistration& Registration =
			OutSnapshot.BootstrapCheckpoint.FactionRegistrations
				.AddDefaulted_GetRef();
		Registration.FactionID = Pair.Key;
		Registration.FactionAssetPath = Faction->GetPathName();
	}
	OutSnapshot.BootstrapCheckpoint.FactionRegistrations.Sort(
		[](const FSeinSnapshotFactionRegistration& A,
			const FSeinSnapshotFactionRegistration& B)
		{
			return A.FactionID < B.FactionID;
		});

	OutSnapshot.CurrentTick = CurrentTick;
	OutSnapshot.SessionSeed = SimSessionSeed;
	OutSnapshot.PRNGState0 = static_cast<int64>(SimRandom.State0);
	OutSnapshot.PRNGState1 = static_cast<int64>(SimRandom.State1);
	OutSnapshot.NextEffectInstanceID = NextEffectInstanceID;
	OutSnapshot.NextLatentActionID =
		LatentActionManager->GetNextActionID();
	OutSnapshot.NextAbilityActivationID =
		NextAbilityActivationID;

	OutSnapshot.MatchSettings = CurrentMatchSettings;
	OutSnapshot.MatchState = static_cast<uint8>(MatchState);
	OutSnapshot.MatchStartTick = MatchStartTick;
	OutSnapshot.StartingStateDeadlineTick = StartingStateDeadlineTick;
	OutSnapshot.bSimPaused = bSimPaused;
	OutSnapshot.bSimPausedHard = bSimPausedHard;
	OutSnapshot.PauseEpoch = PauseEpoch;
	OutSnapshot.PauseFrozenTick = PauseFrozenTick;
	OutSnapshot.LastAppliedPauseControlSequence = LastAppliedPauseControlSequence;
	OutSnapshot.PendingCommands = PendingCommands.GetCommands();
	OutSnapshot.PendingStandalonePauseControlCommands =
		PendingStandalonePauseControlCommands;

	OutSnapshot.PlayerStates = PlayerStates;
	if (!ValidatePairCapabilityState())
	{
		OutSnapshot = FSeinWorldSnapshot();
		OutSnapshot.SnapshotVersion = 0;
		UE_LOG(LogSeinSim, Error,
			TEXT("CaptureSnapshot: invalid pair-capability source records or cache."));
		return;
	}
	OutSnapshot.PairCapabilityGrants = GetPairCapabilityGrantRecords();
	const auto TagLess = [](const FGameplayTag& A, const FGameplayTag& B)
	{
		return A.GetTagName().Compare(B.GetTagName()) < 0;
	};
	const auto RefuseRegistryCapture = [&OutSnapshot](const TCHAR* Reason)
	{
		OutSnapshot = FSeinWorldSnapshot();
		OutSnapshot.SnapshotVersion = 0;
		UE_LOG(LogSeinSim, Error,
			TEXT("CaptureSnapshot: invalid centralized sim registry (%s)."),
			Reason);
	};

	TMap<FGameplayTag, TSet<FSeinEntityHandle>> ExpectedTagIndex;
	TArray<FSeinEntityHandle> TagStateHandles;
	EntityTagStates.GetKeys(TagStateHandles);
	TagStateHandles.Sort();
	OutSnapshot.EntityTagStates.Reset(TagStateHandles.Num());
	for (const FSeinEntityHandle Handle : TagStateHandles)
	{
		const FSeinEntityTagState& State = EntityTagStates.FindChecked(Handle);
		if (!EntityPool.IsValid(Handle))
		{
			RefuseRegistryCapture(TEXT("stale entity-tag handle"));
			return;
		}

		FSeinSnapshotEntityTagState& Record =
			OutSnapshot.EntityTagStates.AddDefaulted_GetRef();
		Record.Entity = Handle;
		State.BaseTags.GetGameplayTagArray(Record.BaseTags);
		Record.BaseTags.Sort(TagLess);

		TArray<FGameplayTag> RefTags;
		State.TagRefCounts.GetKeys(RefTags);
		RefTags.Sort(TagLess);
		Record.RefCounts.Reserve(RefTags.Num());
		for (const FGameplayTag Tag : RefTags)
		{
			const int32 RefCount = State.TagRefCounts.FindChecked(Tag);
			if (!Tag.IsValid() || RefCount <= 0
				|| !State.CombinedTags.HasTagExact(Tag))
			{
				RefuseRegistryCapture(TEXT("invalid entity-tag refcount/cache"));
				return;
			}
			FSeinSnapshotTagRefCount& Ref =
				Record.RefCounts.AddDefaulted_GetRef();
			Ref.Tag = Tag;
			Ref.RefCount = RefCount;
			ExpectedTagIndex.FindOrAdd(Tag).Add(Handle);
		}
		for (const FGameplayTag BaseTag : Record.BaseTags)
		{
			if (!BaseTag.IsValid() || !State.TagRefCounts.Contains(BaseTag))
			{
				RefuseRegistryCapture(TEXT("base tag lacks a live refcount"));
				return;
			}
		}
		TArray<FGameplayTag> CombinedTags;
		State.CombinedTags.GetGameplayTagArray(CombinedTags);
		if (CombinedTags.Num() != RefTags.Num())
		{
			RefuseRegistryCapture(TEXT("combined-tag cache is not exact"));
			return;
		}
		CombinedTags.Sort(TagLess);
		for (int32 Index = 0; Index < RefTags.Num(); ++Index)
		{
			if (CombinedTags[Index] != RefTags[Index])
			{
				RefuseRegistryCapture(TEXT("combined-tag cache is not exact"));
				return;
			}
		}
	}

	TArray<FGameplayTag> IndexedTags;
	EntityTagIndex.GetKeys(IndexedTags);
	IndexedTags.Sort(TagLess);
	if (IndexedTags.Num() != ExpectedTagIndex.Num())
	{
		RefuseRegistryCapture(TEXT("tag-index key set differs from refcounts"));
		return;
	}
	OutSnapshot.EntityTagIndex.Reset(IndexedTags.Num());
	for (const FGameplayTag Tag : IndexedTags)
	{
		const TArray<FSeinEntityHandle>& Bucket =
			EntityTagIndex.FindChecked(Tag);
		const TSet<FSeinEntityHandle>* Expected = ExpectedTagIndex.Find(Tag);
		TSet<FSeinEntityHandle> Seen;
		if (!Tag.IsValid() || !Expected || Bucket.Num() != Expected->Num())
		{
			RefuseRegistryCapture(TEXT("invalid tag-index bucket"));
			return;
		}
		for (const FSeinEntityHandle Handle : Bucket)
		{
			if (!EntityPool.IsValid(Handle) || Seen.Contains(Handle)
				|| !Expected->Contains(Handle))
			{
				RefuseRegistryCapture(TEXT("invalid tag-index membership"));
				return;
			}
			Seen.Add(Handle);
		}
		FSeinSnapshotTagIndexBucket& Record =
			OutSnapshot.EntityTagIndex.AddDefaulted_GetRef();
		Record.Tag = Tag;
		Record.Entities = Bucket;
	}

	TArray<FName> NamedKeys;
	NamedEntityRegistry.GetKeys(NamedKeys);
	NamedKeys.Sort([](const FName& A, const FName& B)
	{
		return A.Compare(B) < 0;
	});
	OutSnapshot.NamedEntities.Reset(NamedKeys.Num());
	for (const FName Name : NamedKeys)
	{
		const FSeinEntityHandle Handle = NamedEntityRegistry.FindChecked(Name);
		if (Name.IsNone() || !EntityPool.IsValid(Handle))
		{
			RefuseRegistryCapture(TEXT("invalid named-entity binding"));
			return;
		}
		FSeinSnapshotNamedEntity& Record =
			OutSnapshot.NamedEntities.AddDefaulted_GetRef();
		Record.Name = Name;
		Record.Entity = Handle;
	}

	TArray<FGameplayTag> VoteTypes;
	ActiveVotes.GetKeys(VoteTypes);
	VoteTypes.Sort(TagLess);
	OutSnapshot.ActiveVotes.Reset(VoteTypes.Num());
	for (const FGameplayTag VoteType : VoteTypes)
	{
		const FSeinVoteState& Vote = ActiveVotes.FindChecked(VoteType);
		if (!VoteType.IsValid() || Vote.VoteType != VoteType
			|| Vote.RequiredThreshold < 1 || Vote.InitiatedAtTick < 0
			|| Vote.ExpiresAtTick < Vote.InitiatedAtTick
			|| !StaticEnum<ESeinVoteResolution>()->IsValidEnumValue(
				static_cast<int64>(Vote.Resolution)))
		{
			RefuseRegistryCapture(TEXT("invalid active vote"));
			return;
		}
		FSeinSnapshotActiveVote& Record =
			OutSnapshot.ActiveVotes.AddDefaulted_GetRef();
		Record.VoteType = Vote.VoteType;
		Record.RequiredThreshold = Vote.RequiredThreshold;
		Record.Resolution = Vote.Resolution;
		Record.InitiatedAtTick = Vote.InitiatedAtTick;
		Record.ExpiresAtTick = Vote.ExpiresAtTick;
		Record.Initiator = Vote.Initiator;
		TArray<FSeinPlayerID> Voters;
		Vote.Votes.GetKeys(Voters);
		Voters.Sort();
		Record.Votes.Reserve(Voters.Num());
		for (const FSeinPlayerID Voter : Voters)
		{
			FSeinSnapshotVoteCast& Cast =
				Record.Votes.AddDefaulted_GetRef();
			Cast.Voter = Voter;
			Cast.Value = Vote.Votes.FindChecked(Voter);
		}
	}

	FString CanonicalStateCaptureError;
	if (!FSeinCanonicalStateRegistry::CaptureContributorRecords(
		NativeCanonicalStateSchema,
		{ *this, CurrentTick },
		OutSnapshot.NativeCanonicalStateRecords,
		CanonicalStateCaptureError))
	{
		OutSnapshot = FSeinWorldSnapshot();
		OutSnapshot.SnapshotVersion = 0;
		UE_LOG(LogSeinSim, Error,
			TEXT("CaptureSnapshot: native canonical state failed (%s)."),
			*CanonicalStateCaptureError);
		return;
	}
	if (!CanonicalStateValues.CaptureRecords(
		OutSnapshot.CanonicalStateValueRecords,
		CanonicalStateCaptureError))
	{
		OutSnapshot = FSeinWorldSnapshot();
		OutSnapshot.SnapshotVersion = 0;
		UE_LOG(LogSeinSim, Error,
			TEXT("CaptureSnapshot: canonical state values failed (%s)."),
			*CanonicalStateCaptureError);
		return;
	}

	FString EntityPoolCaptureError;
	if (!EntityPool.CaptureExactState(
		OutSnapshot.EntityPoolState,
		EntityPoolCaptureError))
	{
		OutSnapshot = FSeinWorldSnapshot();
		OutSnapshot.SnapshotVersion = 0;
		UE_LOG(LogSeinSim, Error,
			TEXT("CaptureSnapshot: exact entity-pool capture failed (%s)."),
			*EntityPoolCaptureError);
		return;
	}

	OutSnapshot.Entities.Reset();
	EntityPool.ForEachEntity([&OutSnapshot, this](FSeinEntityHandle Handle, const FSeinEntity& Entity)
	{
		FSeinSnapshotEntityRecord Rec;
		Rec.SlotIndex = Handle.Index;
		Rec.Generation = Handle.Generation;
		Rec.Transform = Entity.Transform;
		Rec.Owner = EntityPool.GetOwner(Handle);
		Rec.bAlive = Entity.IsAlive();
		if (const TSubclassOf<ASeinActor>* SpawnedClass = EntityActorClassMap.Find(Handle))
		{
			if (UClass* CRef = SpawnedClass->Get())
			{
				Rec.ActorClassPath = CRef->GetPathName();
			}
		}
		OutSnapshot.Entities.Add(Rec);
	});

	OutSnapshot.ComponentStorageBlobs.Reset();
	for (auto& Pair : ComponentStorages)
	{
		UScriptStruct* StructType = Pair.Key;
		ISeinComponentStorage* Storage = Pair.Value;
		if (!StructType || !Storage) continue;

		FSeinSnapshotComponentStorageBlob Blob;
		const uint64 TopologyRevision = Storage->GetTopologyRevision();
		const uint64 LatestMutationRevision =
			Storage->GetLatestMutationRevision();
		const bool bCanReuse = Storage->CanReuseSnapshotSerialization();
		uint64 CacheByteBudget = MaxComponentStorageSnapshotCacheBytes;
#if WITH_DEV_AUTOMATION_TESTS
		CacheByteBudget = ComponentStorageSnapshotCacheBudgetForTests;
#endif
		const auto DropCachedStorage = [this, StructType]()
		{
			if (const FComponentStorageSnapshotCacheEntry* Existing =
				ComponentStorageSnapshotCache.Find(StructType))
			{
				check(static_cast<uint64>(Existing->Bytes.Num())
					<= ComponentStorageSnapshotCacheBytes);
				ComponentStorageSnapshotCacheBytes -=
					static_cast<uint64>(Existing->Bytes.Num());
				ComponentStorageSnapshotCache.Remove(StructType);
			}
		};
		if (!bCanReuse)
		{
			DropCachedStorage();
		}
		const FComponentStorageSnapshotCacheEntry* Cached =
			bCanReuse
				? ComponentStorageSnapshotCache.Find(StructType)
				: nullptr;
		if (Cached
			&& Cached->TopologyRevision == TopologyRevision
			&& Cached->LatestMutationRevision == LatestMutationRevision)
		{
#if WITH_DEV_AUTOMATION_TESTS
			++ComponentStorageSnapshotCacheHitCount;
#endif
			TRACE_CPUPROFILER_EVENT_SCOPE(
				Sein_World_CaptureSnapshot_ComponentStorageCacheHit);
			Blob.EntryCount = Cached->EntryCount;
			Blob.Bytes = Cached->Bytes;
		}
		else
		{
#if WITH_DEV_AUTOMATION_TESTS
			++ComponentStorageSnapshotCacheMissCount;
#endif
			TRACE_CPUPROFILER_EVENT_SCOPE(
				Sein_World_CaptureSnapshot_ComponentStorageSerialize);
			// The base FMemoryArchive asserts on UObject* serialization. The proxy
			// stringifies reflected object references as stable paths.
			FMemoryWriter MemWriter(Blob.Bytes, /*bIsPersistent*/ true);
			FObjectAndNameAsStringProxyArchive Writer(
				MemWriter, /*bInLoadIfFindFails*/ false);
			Blob.EntryCount = Storage->SerializeFromArchive(Writer);
			if (Writer.IsError())
			{
				OutSnapshot = FSeinWorldSnapshot();
				OutSnapshot.SnapshotVersion = 0;
				UE_LOG(LogSeinSim, Error,
					TEXT("CaptureSnapshot: component storage %s failed serialization."),
					*StructType->GetPathName());
				return;
			}

			DropCachedStorage();
			const uint64 BlobByteCount =
				static_cast<uint64>(Blob.Bytes.Num());
			if (bCanReuse
				&& ComponentStorageSnapshotCacheBytes <= CacheByteBudget
				&& BlobByteCount <= CacheByteBudget
					- ComponentStorageSnapshotCacheBytes)
			{
				FComponentStorageSnapshotCacheEntry& NewCache =
					ComponentStorageSnapshotCache.FindOrAdd(StructType);
				NewCache.TopologyRevision = TopologyRevision;
				NewCache.LatestMutationRevision = LatestMutationRevision;
				NewCache.EntryCount = Blob.EntryCount;
				NewCache.Bytes = Blob.Bytes;
				ComponentStorageSnapshotCacheBytes += BlobByteCount;
			}
		}
		OutSnapshot.ComponentStorageBlobs.Add(StructType->GetPathName(), MoveTemp(Blob));
	}

	// ---- Ability pool ----
	// Imported identity never chooses code. The frozen local manifest selects
	// the exact provider generation and stamps its complete compatibility claim.
	auto RefusePoolCapture = [&OutSnapshot](
		const TCHAR* PoolName, int32 PoolID, const FString& Error)
	{
		OutSnapshot = FSeinWorldSnapshot();
		OutSnapshot.SnapshotVersion = 0;
		UE_LOG(LogSeinSim, Error,
			TEXT("CaptureSnapshot: %s slot %d failed provider capture (%s)."),
			PoolName, PoolID, *Error);
	};
	OutSnapshot.AbilityPoolRecords.Reset(AbilityPool.Num());
	OutSnapshot.AbilityPoolFreeList = AbilityPoolFreeList;
	for (int32 ID = 0; ID < AbilityPool.Num(); ++ID)
	{
		FSeinSnapshotPoolInstanceRecord Rec;
		Rec.PoolID = ID;
		USeinAbility* A = AbilityPool[ID].Get();
		if (A)
		{
			TGuardValue<bool> ReadOnlyGuard(
				bReadOnlyCallbackInProgress, true);
			TGuardValue<bool> ObserverGuard(
				bObserverCallbackInProgress, true);
			FString PoolError;
			if (!FSeinPoolObjectCodecRegistry::CaptureObject(
				PoolObjectCodecManifest,
				*A,
				ESeinPoolObjectKind::Ability,
				ID,
				Rec,
				PoolError))
			{
				RefusePoolCapture(
					TEXT("ability"), ID, PoolError);
				return;
			}
		}
		OutSnapshot.AbilityPoolRecords.Add(MoveTemp(Rec));
	}

	FString LatentCaptureError;
	if (!FSeinLatentActionCodecRegistry::CaptureRecords(
		LatentActionCodecManifest,
		*this,
		LatentActionManager,
		CurrentTick,
		OutSnapshot.NextLatentActionID,
		OutSnapshot.NextAbilityActivationID,
		OutSnapshot.LatentActionRecords,
		OutSnapshot.LatentActionSequenceDigest,
		LatentCaptureError))
	{
		OutSnapshot = FSeinWorldSnapshot();
		OutSnapshot.SnapshotVersion = 0;
		UE_LOG(LogSeinSim, Error,
			TEXT("CaptureSnapshot: latent continuation capture failed (%s)."),
			*LatentCaptureError);
		return;
	}

	// ---- Resolver pool ----
	OutSnapshot.ResolverPoolRecords.Reset(CommandBrokerResolverPool.Num());
	OutSnapshot.ResolverPoolFreeList =
		CommandBrokerResolverPoolFreeList;
	for (int32 ID = 0; ID < CommandBrokerResolverPool.Num(); ++ID)
	{
		FSeinSnapshotPoolInstanceRecord Rec;
		Rec.PoolID = ID;
		USeinCommandBrokerResolver* R = CommandBrokerResolverPool[ID].Get();
		if (R)
		{
			TGuardValue<bool> ReadOnlyGuard(
				bReadOnlyCallbackInProgress, true);
			TGuardValue<bool> ObserverGuard(
				bObserverCallbackInProgress, true);
			FString PoolError;
			if (!FSeinPoolObjectCodecRegistry::CaptureObject(
				PoolObjectCodecManifest,
				*R,
				ESeinPoolObjectKind::CommandBrokerResolver,
				ID,
				Rec,
				PoolError))
			{
				RefusePoolCapture(
					TEXT("resolver"), ID, PoolError);
				return;
			}
		}
		OutSnapshot.ResolverPoolRecords.Add(MoveTemp(Rec));
	}

	UE_LOG(LogSeinSim, Log,
		TEXT("CaptureSnapshot: tick=%d  entities=%d  componentStorages=%d  playerStates=%d  abilityPool=%d  latentActions=%d  resolverPool=%d"),
		OutSnapshot.CurrentTick, OutSnapshot.Entities.Num(),
		OutSnapshot.ComponentStorageBlobs.Num(), OutSnapshot.PlayerStates.Num(),
		OutSnapshot.AbilityPoolRecords.Num(),
		OutSnapshot.LatentActionRecords.Num(),
		OutSnapshot.ResolverPoolRecords.Num());

	// Let Framework's presentation bridge stamp only the local camera slot.
	// Authoritative checkpoint fields are never exposed through this callback.
	{
		TGuardValue<bool> ReadOnlyGuard(bReadOnlyCallbackInProgress, true);
		TGuardValue<bool> ObserverGuard(bObserverCallbackInProgress, true);
		OnCaptureSnapshotPostSim.Broadcast(OutSnapshot.CameraState);
	}

	// Retain an internal continuation-envelope check as defense in depth even
	// though the callback above can now reach only non-authoritative camera data.
	bool bEnvelopeUnchanged =
		OutSnapshot.SnapshotVersion == FSeinWorldSnapshot::CurrentVersion
		&& OutSnapshot.FrameworkVersion
			== SeinReplayCompatibility::GetFrameworkVersion()
		&& OutSnapshot.GameVersion == SeinReplayCompatibility::GetGameVersion()
		&& OutSnapshot.MapIdentifier
			== SeinReplayCompatibility::GetMapIdentifier(GetWorld())
		&& OutSnapshot.CommandProtocolDigest == CommandProtocolDigest
		&& OutSnapshot.SimulationContentDigest
			== SimulationContentDigest
		&& OutSnapshot.MatchSettingsDigest == MatchSettingsDigest
		&& OutSnapshot.ConfigFingerprint == ConfigFingerprint
		&& OutSnapshot.NextEffectInstanceID == NextEffectInstanceID
		&& OutSnapshot.NextLatentActionID
			== LatentActionManager->GetNextActionID()
		&& OutSnapshot.NextAbilityActivationID
			== NextAbilityActivationID
		&& OutSnapshot.LatentActionSequenceDigest.IsValid()
		&& OutSnapshot.BootstrapCheckpoint.IsValidConsumedCheckpoint()
		&& OutSnapshot.BootstrapCheckpoint.Receipt == MatchBootstrapReceipt
		&& OutSnapshot.BootstrapCheckpoint.AuthorizationContextDigest
			== MatchBootstrapAuthorizationContextDigest
		&& OutSnapshot.BootstrapCheckpoint.InitialStateContributions.Num()
			== MatchBootstrapValueContributions.Num()
		&& OutSnapshot.BootstrapCheckpoint.FactionRegistrations.Num()
			== Factions.Num();
	for (int32 Index = 0;
		bEnvelopeUnchanged
			&& Index < OutSnapshot.BootstrapCheckpoint
				.InitialStateContributions.Num();
		++Index)
	{
		const FSeinSnapshotInitialStateContribution& Saved =
			OutSnapshot.BootstrapCheckpoint.InitialStateContributions[Index];
		const FSeinCanonicalInitialStateValueContribution* Existing =
			MatchBootstrapValueContributions.FindByPredicate(
				[&Saved](const FSeinCanonicalInitialStateValueContribution& Value)
				{
					return FSeinCanonicalInitialStateDigest::CanonicalContributorID(
						Value.StableContributorID)
						== FSeinCanonicalInitialStateDigest::CanonicalContributorID(
							Saved.StableContributorID);
				});
		bEnvelopeUnchanged = Existing
			&& Existing->SchemaVersion == Saved.SchemaVersion
			&& Existing->ValueDigest == Saved.ValueDigest;
	}
	for (const FSeinSnapshotFactionRegistration& Saved :
		OutSnapshot.BootstrapCheckpoint.FactionRegistrations)
	{
		if (!bEnvelopeUnchanged)
		{
			break;
		}
		const TObjectPtr<USeinFaction>* Existing = Factions.Find(Saved.FactionID);
		bEnvelopeUnchanged = Existing && Existing->Get()
			&& Existing->Get()->GetPathName() == Saved.FactionAssetPath;
	}
	if (!bEnvelopeUnchanged)
	{
		OutSnapshot = FSeinWorldSnapshot();
		OutSnapshot.SnapshotVersion = 0;
		UE_LOG(LogSeinSim, Error,
			TEXT("CaptureSnapshot: an extension modified the Core checkpoint envelope."));
	}
}

namespace
{
	bool ValidateSnapshotComponentBlob(const FSeinSnapshotComponentStorageBlob& Blob,
		UScriptStruct& StructType,
		const TMap<int32, FSeinEntityHandle>& AliveHandleBySlot)
	{
		const TStrongObjectPtr<UScriptStruct> StructTypeRoot(&StructType);
		UScriptStruct* const RootedStructType = StructTypeRoot.Get();
		if (Blob.EntryCount < 0 || Blob.EntryCount > AliveHandleBySlot.Num()) return false;
		FMemoryReader MemoryReader(Blob.Bytes, /*bIsPersistent=*/true);
		FObjectAndNameAsStringProxyArchive Reader(
			MemoryReader, /*bInLoadIfFindFails=*/true);
		int32 InnerCount = 0;
		Reader << InnerCount;
		if (Reader.IsError() || InnerCount != Blob.EntryCount || InnerCount < 0)
		{
			return false;
		}
		TSet<int32> SeenSlots;
		for (int32 EntryIndex = 0; EntryIndex < InnerCount; ++EntryIndex)
		{
			int32 Slot = 0;
			int32 Generation = 0;
			Reader << Slot;
			Reader << Generation;
			const FSeinEntityHandle* AliveHandle = AliveHandleBySlot.Find(Slot);
			if (Reader.IsError() || !AliveHandle
				|| AliveHandle->Generation != Generation
				|| SeenSlots.Contains(Slot))
			{
				return false;
			}
			SeenSlots.Add(Slot);
			FStructOnScope Component(RootedStructType);
			FSeinStructOnScopeGCGuard ComponentGCGuard(Component);
			RootedStructType->SerializeBin(
				Reader, Component.GetStructMemory());
			if (Reader.IsError()) return false;
		}
		return MemoryReader.Tell() == Blob.Bytes.Num();
	}

	template<typename ComponentType, typename VisitorType>
	bool DecodeSnapshotComponentBlob(const FSeinWorldSnapshot& Snapshot,
		const TMap<int32, FSeinEntityHandle>& AliveHandleBySlot,
		VisitorType&& Visitor)
	{
		const FSeinSnapshotComponentStorageBlob* Blob = Snapshot.ComponentStorageBlobs.Find(
			ComponentType::StaticStruct()->GetPathName());
		if (!Blob) return true;
		if (Blob->EntryCount < 0 || Blob->EntryCount > AliveHandleBySlot.Num()) return false;

		FMemoryReader MemoryReader(Blob->Bytes, /*bIsPersistent=*/true);
		FObjectAndNameAsStringProxyArchive Reader(
			MemoryReader, /*bInLoadIfFindFails=*/true);
		int32 InnerCount = 0;
		Reader << InnerCount;
		if (Reader.IsError() || InnerCount != Blob->EntryCount || InnerCount < 0)
		{
			return false;
		}

		TSet<int32> SeenSlots;
		for (int32 EntryIndex = 0; EntryIndex < InnerCount; ++EntryIndex)
		{
			int32 Slot = 0;
			int32 Generation = 0;
			Reader << Slot;
			Reader << Generation;
			const FSeinEntityHandle* AliveHandle = AliveHandleBySlot.Find(Slot);
			if (Reader.IsError() || !AliveHandle
				|| AliveHandle->Generation != Generation
				|| SeenSlots.Contains(Slot))
			{
				return false;
			}
			SeenSlots.Add(Slot);
			ComponentType Component;
			TSeinStackStructGCGuard<ComponentType> ComponentGCGuard(
				Component);
			ComponentType::StaticStruct()->SerializeBin(Reader, &Component);
			if (Reader.IsError() || !Visitor(Slot, Component)) return false;
		}
		return !Reader.IsError() && MemoryReader.Tell() == Blob->Bytes.Num();
	}

	bool TryValidateSnapshotSimState(const FSeinWorldSnapshot& Snapshot,
		const TArray<TStrongObjectPtr<USeinAbility>>& StagedAbilityPool,
		const TArray<TStrongObjectPtr<USeinCommandBrokerResolver>>&
			StagedResolverPool,
		int64& OutMaxEffectID,
		TMap<FSeinEntityHandle, FSeinEntityTagState>& OutEntityTagStates,
		TMap<FGameplayTag, TArray<FSeinEntityHandle>>& OutEntityTagIndex,
		TMap<FName, FSeinEntityHandle>& OutNamedEntityRegistry,
		TMap<FGameplayTag, FSeinVoteState>& OutActiveVotes)
	{
		struct FValidatedAbilityPoolState
		{
			UClass* Class = nullptr;
			FSeinEntityHandle OwnerEntity;
			bool bIsPassive = false;
			bool bIsActive = false;
			int64 AbilityActivationID = 0;
		};

		OutMaxEffectID = 0;
		OutEntityTagStates.Reset();
		OutEntityTagIndex.Reset();
		OutNamedEntityRegistry.Reset();
		OutActiveVotes.Reset();
		if (Snapshot.EntityPoolState.Capacity < 0
			|| Snapshot.EntityPoolState.Capacity
				> FSeinWorldSnapshot::MaxSupportedEntitySlotIndex
			|| Snapshot.Entities.Num()
				> FSeinWorldSnapshot::MaxSupportedEntitySlotIndex
			|| Snapshot.AbilityPoolRecords.Num()
				> FSeinWorldSnapshot::MaxSupportedObjectPoolSlots
			|| Snapshot.ResolverPoolRecords.Num()
				> FSeinWorldSnapshot::MaxSupportedObjectPoolSlots
			|| Snapshot.AbilityPoolFreeList.Num()
				> Snapshot.AbilityPoolRecords.Num()
			|| Snapshot.ResolverPoolFreeList.Num()
				> Snapshot.ResolverPoolRecords.Num()
			|| Snapshot.LatentActionRecords.Num()
				> FSeinLatentActionCodecRegistry::MaxActiveActions
			|| Snapshot.ComponentStorageBlobs.Num()
				> FSeinWorldSnapshot::MaxSupportedComponentStorageTypes)
		{
			return false;
		}
		int64 AggregateOpaqueStateBytes = 0;
		for (const FSeinSnapshotPoolInstanceRecord& Record :
			Snapshot.AbilityPoolRecords)
		{
			if (Record.ClassPath.Len() > 1024
				|| Record.NativeAnchorClassPath.Len() > 1024
				|| Record.StableProviderID.Len() > 256
				|| Record.StateBytes.Num()
					> FSeinWorldSnapshot::MaxSupportedPoolObjectStateBytes)
			{
				return false;
			}
			AggregateOpaqueStateBytes += Record.StateBytes.Num();
		}
		for (const FSeinSnapshotPoolInstanceRecord& Record :
			Snapshot.ResolverPoolRecords)
		{
			if (Record.ClassPath.Len() > 1024
				|| Record.NativeAnchorClassPath.Len() > 1024
				|| Record.StableProviderID.Len() > 256
				|| Record.StateBytes.Num()
					> FSeinWorldSnapshot::MaxSupportedPoolObjectStateBytes)
			{
				return false;
			}
			AggregateOpaqueStateBytes += Record.StateBytes.Num();
		}
		for (const FSeinSnapshotLatentActionRecord& Record :
			Snapshot.LatentActionRecords)
		{
			if (Record.ActionClassPath.Len() > 1024
				|| Record.StableCodecID.Len() > 256
				|| Record.PayloadBytes.Num()
					> FSeinLatentActionCodecRegistry::MaxPayloadBytes)
			{
				return false;
			}
			AggregateOpaqueStateBytes += Record.PayloadBytes.Num();
		}
		for (const auto& Pair : Snapshot.ComponentStorageBlobs)
		{
			if (Pair.Key.IsEmpty() || Pair.Key.Len() > 1024
				|| Pair.Value.Bytes.Num()
					> FSeinWorldSnapshot::MaxSupportedComponentBlobBytes)
			{
				return false;
			}
			AggregateOpaqueStateBytes += Pair.Value.Bytes.Num();
		}
		if (AggregateOpaqueStateBytes
			> FSeinWorldSnapshot::MaxSupportedAggregateOpaqueStateBytes)
		{
			return false;
		}
		FSeinEntityPool ValidatedEntityPool;
		FString EntityPoolError;
		if (!ValidatedEntityPool.TryStageExactState(
			Snapshot.EntityPoolState,
			FSeinWorldSnapshot::MaxSupportedEntitySlotIndex,
			EntityPoolError))
		{
			return false;
		}
		if (Snapshot.Entities.Num()
			!= ValidatedEntityPool.GetActiveCount())
		{
			return false;
		}
		TSet<int32> AllEntitySlots;
		TSet<FSeinEntityHandle> AliveEntityHandles;
		TMap<int32, FSeinEntityHandle> AliveHandleBySlot;
		for (const FSeinSnapshotEntityRecord& Entity : Snapshot.Entities)
		{
			if (Entity.SlotIndex <= 0
				|| Entity.SlotIndex
					> FSeinWorldSnapshot::MaxSupportedEntitySlotIndex
				|| Entity.Generation <= 0
				|| !Entity.bAlive
				|| AllEntitySlots.Contains(Entity.SlotIndex))
			{
				return false;
			}
			const FSeinEntityHandle Handle(
				Entity.SlotIndex, Entity.Generation);
			const FSeinEntity* ExactEntity =
				ValidatedEntityPool.Get(Handle);
			if (!ExactEntity
				|| ExactEntity->Transform != Entity.Transform
				|| ValidatedEntityPool.GetOwner(Handle)
					!= Entity.Owner)
			{
				return false;
			}
			AllEntitySlots.Add(Entity.SlotIndex);
			AliveEntityHandles.Add(Handle);
			AliveHandleBySlot.Add(Entity.SlotIndex, Handle);
		}

		const auto TagLess = [](const FGameplayTag& A, const FGameplayTag& B)
		{
			return A.GetTagName().Compare(B.GetTagName()) < 0;
		};
		constexpr int32 MaxCentralRegistryRecords = 1 << 20;
		if (Snapshot.EntityTagStates.Num() > AliveEntityHandles.Num()
			|| Snapshot.EntityTagIndex.Num() > MaxCentralRegistryRecords
			|| Snapshot.NamedEntities.Num() > MaxCentralRegistryRecords
			|| Snapshot.ActiveVotes.Num() > MaxCentralRegistryRecords)
		{
			return false;
		}

		TMap<FGameplayTag, TSet<FSeinEntityHandle>> ExpectedTagIndex;
		FSeinEntityHandle PreviousTagEntity;
		bool bHasPreviousTagEntity = false;
		for (const FSeinSnapshotEntityTagState& Record :
			Snapshot.EntityTagStates)
		{
			if (!Record.Entity.IsValid()
				|| !AliveEntityHandles.Contains(Record.Entity)
				|| (bHasPreviousTagEntity
					&& !(PreviousTagEntity < Record.Entity)))
			{
				return false;
			}
			bHasPreviousTagEntity = true;
			PreviousTagEntity = Record.Entity;

			FSeinEntityTagState State;
			FGameplayTag PreviousBaseTag;
			bool bHasPreviousBaseTag = false;
			for (const FGameplayTag Tag : Record.BaseTags)
			{
				if (!Tag.IsValid()
					|| (bHasPreviousBaseTag
						&& !TagLess(PreviousBaseTag, Tag)))
				{
					return false;
				}
				bHasPreviousBaseTag = true;
				PreviousBaseTag = Tag;
				State.BaseTags.AddTag(Tag);
			}

			FGameplayTag PreviousRefTag;
			bool bHasPreviousRefTag = false;
			for (const FSeinSnapshotTagRefCount& Ref : Record.RefCounts)
			{
				if (!Ref.Tag.IsValid() || Ref.RefCount <= 0
					|| (bHasPreviousRefTag
						&& !TagLess(PreviousRefTag, Ref.Tag)))
				{
					return false;
				}
				bHasPreviousRefTag = true;
				PreviousRefTag = Ref.Tag;
				State.TagRefCounts.Add(Ref.Tag, Ref.RefCount);
				ExpectedTagIndex.FindOrAdd(Ref.Tag).Add(Record.Entity);
			}
			for (const FGameplayTag BaseTag : Record.BaseTags)
			{
				if (!State.TagRefCounts.Contains(BaseTag))
				{
					return false;
				}
			}
			State.RebuildCombinedTags();
			OutEntityTagStates.Add(Record.Entity, MoveTemp(State));
		}

		FGameplayTag PreviousIndexTag;
		bool bHasPreviousIndexTag = false;
		for (const FSeinSnapshotTagIndexBucket& Record :
			Snapshot.EntityTagIndex)
		{
			if (!Record.Tag.IsValid() || Record.Entities.IsEmpty()
				|| (bHasPreviousIndexTag
					&& !TagLess(PreviousIndexTag, Record.Tag)))
			{
				return false;
			}
			bHasPreviousIndexTag = true;
			PreviousIndexTag = Record.Tag;
			const TSet<FSeinEntityHandle>* Expected =
				ExpectedTagIndex.Find(Record.Tag);
			if (!Expected || Expected->Num() != Record.Entities.Num())
			{
				return false;
			}
			TSet<FSeinEntityHandle> Seen;
			for (const FSeinEntityHandle Handle : Record.Entities)
			{
				if (!AliveEntityHandles.Contains(Handle)
					|| Seen.Contains(Handle) || !Expected->Contains(Handle))
				{
					return false;
				}
				Seen.Add(Handle);
			}
			OutEntityTagIndex.Add(Record.Tag, Record.Entities);
		}
		if (OutEntityTagIndex.Num() != ExpectedTagIndex.Num())
		{
			return false;
		}

		FName PreviousName;
		bool bHasPreviousName = false;
		for (const FSeinSnapshotNamedEntity& Record :
			Snapshot.NamedEntities)
		{
			if (Record.Name.IsNone()
				|| !AliveEntityHandles.Contains(Record.Entity)
				|| (bHasPreviousName
					&& PreviousName.Compare(Record.Name) >= 0))
			{
				return false;
			}
			bHasPreviousName = true;
			PreviousName = Record.Name;
			OutNamedEntityRegistry.Add(Record.Name, Record.Entity);
		}

		FGameplayTag PreviousVoteType;
		bool bHasPreviousVoteType = false;
		for (const FSeinSnapshotActiveVote& Record : Snapshot.ActiveVotes)
		{
			if (!Record.VoteType.IsValid() || Record.RequiredThreshold < 1
				|| Record.InitiatedAtTick < 0
				|| Record.InitiatedAtTick > Snapshot.CurrentTick
				|| Record.ExpiresAtTick < Record.InitiatedAtTick
				|| !StaticEnum<ESeinVoteResolution>()->IsValidEnumValue(
					static_cast<int64>(Record.Resolution))
				|| (bHasPreviousVoteType
					&& !TagLess(PreviousVoteType, Record.VoteType)))
			{
				return false;
			}
			bHasPreviousVoteType = true;
			PreviousVoteType = Record.VoteType;

			FSeinVoteState Vote;
			Vote.VoteType = Record.VoteType;
			Vote.RequiredThreshold = Record.RequiredThreshold;
			Vote.Resolution = Record.Resolution;
			Vote.InitiatedAtTick = Record.InitiatedAtTick;
			Vote.ExpiresAtTick = Record.ExpiresAtTick;
			Vote.Initiator = Record.Initiator;
			FSeinPlayerID PreviousVoter;
			bool bHasPreviousVoter = false;
			for (const FSeinSnapshotVoteCast& Cast : Record.Votes)
			{
				if (bHasPreviousVoter
					&& !(PreviousVoter < Cast.Voter))
				{
					return false;
				}
				bHasPreviousVoter = true;
				PreviousVoter = Cast.Voter;
				Vote.Votes.Add(Cast.Voter, Cast.Value);
			}
			OutActiveVotes.Add(Record.VoteType, MoveTemp(Vote));
		}

		for (const auto& Pair : Snapshot.ComponentStorageBlobs)
		{
			UScriptStruct* StructType = FindObject<UScriptStruct>(nullptr, *Pair.Key);
			if (!StructType || StructType->GetPathName() != Pair.Key
				|| !ValidateSnapshotComponentBlob(
					Pair.Value, *StructType, AliveHandleBySlot))
			{
				return false;
			}
		}

		TMap<FSeinEntityHandle, FSeinSquadComponent> Squads;
		TMap<FSeinEntityHandle, FSeinSquadMemberComponent> SquadMembers;
		TMap<FSeinEntityHandle, FSeinCommandBrokerData> Brokers;
		TMap<FSeinEntityHandle, FSeinBrokerMembershipData> BrokerMemberships;
		auto DecodeComponents = [&]<typename ComponentType>(
			TMap<FSeinEntityHandle, ComponentType>& OutComponents)
		{
			return DecodeSnapshotComponentBlob<ComponentType>(
				Snapshot,
				AliveHandleBySlot,
				[&](int32 Slot, const ComponentType& Component)
				{
					const FSeinEntityHandle* Handle =
						AliveHandleBySlot.Find(Slot);
					if (!Handle) return false;
					OutComponents.Add(*Handle, Component);
					return true;
				});
		};
		if (!DecodeComponents(Squads)
			|| !DecodeComponents(SquadMembers)
			|| !DecodeComponents(Brokers)
			|| !DecodeComponents(BrokerMemberships))
		{
			return false;
		}

		TMap<FSeinEntityHandle, FSeinEntityHandle> OccupantSquad;
		for (const auto& SquadPair : Squads)
		{
			const FSeinEntityHandle SquadHandle = SquadPair.Key;
			const FSeinSquadComponent& Squad = SquadPair.Value;
			if (Squad.NextReinforceRequestID <= 0)
			{
				return false;
			}

			TSet<FSeinEntityHandle> Occupants;
			for (int32 SlotIndex = 0;
				SlotIndex < Squad.Slots.Num(); ++SlotIndex)
			{
				const FSeinSquadSlot& Slot = Squad.Slots[SlotIndex];
				if (Slot.CurrentCooldown < FFixedPoint::Zero)
				{
					return false;
				}
				if (!Slot.CurrentOccupant.IsValid()) continue;
				if (!AliveEntityHandles.Contains(Slot.CurrentOccupant)
					|| Occupants.Contains(Slot.CurrentOccupant)
					|| OccupantSquad.Contains(Slot.CurrentOccupant))
				{
					return false;
				}
				Occupants.Add(Slot.CurrentOccupant);
				OccupantSquad.Add(Slot.CurrentOccupant, SquadHandle);

				const FSeinSquadMemberComponent* Member =
					SquadMembers.Find(Slot.CurrentOccupant);
				const FSeinBrokerMembershipData* Membership =
					BrokerMemberships.Find(Slot.CurrentOccupant);
				if (!Member || Member->SquadEntity != SquadHandle
					|| Member->SlotIndex != SlotIndex
					|| Member->SlotTag != Slot.GetCanonicalSlotTag()
					|| !Membership
					|| Membership->CurrentBrokerHandle != SquadHandle)
				{
					return false;
				}
			}
			if (Squad.Leader.IsValid()
				&& !Occupants.Contains(Squad.Leader))
			{
				return false;
			}

			TSet<int64> RequestIDs;
			TSet<int32> RequestedSlots;
			for (const FSeinSquadReinforceEntry& Entry :
				Squad.ReinforceQueue)
			{
				if (Entry.RequestID <= 0
					|| Entry.RequestID >= Squad.NextReinforceRequestID
					|| RequestIDs.Contains(Entry.RequestID)
					|| !Squad.Slots.IsValidIndex(Entry.RequestedSlotIndex)
					|| RequestedSlots.Contains(Entry.RequestedSlotIndex)
					|| Squad.Slots[Entry.RequestedSlotIndex].
						CurrentOccupant.IsValid()
					|| Entry.BuildProgress < FFixedPoint::Zero
					|| Entry.TotalBuildTime < FFixedPoint::Zero
					|| Entry.BuildProgress > Entry.TotalBuildTime
					|| Entry.SlotTag !=
						Squad.Slots[Entry.RequestedSlotIndex].
							GetCanonicalSlotTag()
					|| !Snapshot.PlayerStates.Contains(Entry.ResourcePayer))
				{
					return false;
				}
				for (const auto& CostPair : Entry.DeductedCost.Amounts)
				{
					if (!CostPair.Key.IsValid()
						|| CostPair.Value < FFixedPoint::Zero)
					{
						return false;
					}
				}
				RequestIDs.Add(Entry.RequestID);
				RequestedSlots.Add(Entry.RequestedSlotIndex);
			}

			const FSeinCommandBrokerData* Broker =
				Brokers.Find(SquadHandle);
			if (!Broker)
			{
				if (!Occupants.IsEmpty()) return false;
				continue;
			}
			TSet<FSeinEntityHandle> BrokerMembers;
			for (const FSeinEntityHandle Member : Broker->Members)
			{
				if (!Occupants.Contains(Member)
					|| BrokerMembers.Contains(Member))
				{
					return false;
				}
				BrokerMembers.Add(Member);
			}
			if (Broker->bSelfCullOnEmpty
				|| BrokerMembers.Num() != Occupants.Num()
				|| Broker->SettledSlotPositions.Num()
					!= Broker->SettledSlotFacings.Num()
				|| (Broker->bSettledSlotsMemberAligned
					&& Broker->SettledSlotPositions.Num()
						!= Broker->Members.Num()))
			{
				return false;
			}
		}
		for (const auto& MemberPair : SquadMembers)
		{
			const FSeinSquadMemberComponent& Member = MemberPair.Value;
			if (!Member.SquadEntity.IsValid())
			{
				if (Member.SlotIndex != INDEX_NONE
					|| Member.SlotTag.IsValid())
				{
					return false;
				}
				continue;
			}
			const FSeinEntityHandle* ExpectedSquad =
				OccupantSquad.Find(MemberPair.Key);
			if (!ExpectedSquad || *ExpectedSquad != Member.SquadEntity)
			{
				return false;
			}
		}
		for (const auto& MembershipPair : BrokerMemberships)
		{
			const FSeinBrokerMembershipData& Membership =
				MembershipPair.Value;
			if (!Membership.CurrentBrokerHandle.IsValid()
				|| !Squads.Contains(Membership.CurrentBrokerHandle))
			{
				continue;
			}
			const FSeinEntityHandle* ExpectedSquad =
				OccupantSquad.Find(MembershipPair.Key);
			if (!ExpectedSquad
				|| *ExpectedSquad != Membership.CurrentBrokerHandle)
			{
				return false;
			}
		}

		auto ValidatePoolTopology = [](
			const TArray<FSeinSnapshotPoolInstanceRecord>& Records,
			const TArray<int32>& FreeList,
			ESeinPoolObjectKind ExpectedKind)
		{
			TSet<int32> FreeSlots;
			for (const int32 FreeSlot : FreeList)
			{
				if (!Records.IsValidIndex(FreeSlot)
					|| FreeSlots.Contains(FreeSlot))
				{
					return false;
				}
				FreeSlots.Add(FreeSlot);
			}
			for (int32 Index = 0; Index < Records.Num(); ++Index)
			{
				const FSeinSnapshotPoolInstanceRecord& Record = Records[Index];
				if (Record.PoolID != Index) return false;
				if (!Record.bAlive)
				{
					if (!FreeSlots.Contains(Index)
						|| Record.ObjectKind != 0
						|| !Record.ClassPath.IsEmpty()
						|| !Record.NativeAnchorClassPath.IsEmpty()
						|| !Record.StableProviderID.IsEmpty()
						|| Record.StateSchemaVersion != 0
						|| Record.BehaviorRevision != 0
						|| Record.CodecRevision != 0
						|| Record.ProviderDescriptorDigest.IsValid()
						|| Record.ExactClassSchemaDigest.IsValid()
						|| !Record.StateBytes.IsEmpty())
					{
						return false;
					}
					continue;
				}
				if (FreeSlots.Contains(Index)
					|| Record.ObjectKind
						!= static_cast<uint8>(ExpectedKind)
					|| Record.ClassPath.IsEmpty()
					|| Record.NativeAnchorClassPath.IsEmpty()
					|| Record.StableProviderID.IsEmpty()
					|| Record.StateSchemaVersion == 0
					|| Record.BehaviorRevision == 0
					|| Record.CodecRevision == 0
					|| !Record.ProviderDescriptorDigest.IsValid()
					|| !Record.ExactClassSchemaDigest.IsValid())
				{
					return false;
				}
			}
			return true;
		};
		if (!ValidatePoolTopology(
				Snapshot.AbilityPoolRecords,
				Snapshot.AbilityPoolFreeList,
				ESeinPoolObjectKind::Ability)
			|| !ValidatePoolTopology(
				Snapshot.ResolverPoolRecords,
				Snapshot.ResolverPoolFreeList,
				ESeinPoolObjectKind::CommandBrokerResolver)
			|| StagedAbilityPool.Num()
				!= Snapshot.AbilityPoolRecords.Num()
			|| StagedResolverPool.Num()
				!= Snapshot.ResolverPoolRecords.Num())
		{
			return false;
		}
		TArray<FValidatedAbilityPoolState> AbilityPoolStates;
		AbilityPoolStates.SetNum(StagedAbilityPool.Num());
		for (int32 Index = 0; Index < StagedAbilityPool.Num(); ++Index)
		{
			USeinAbility* Ability = StagedAbilityPool[Index].Get();
			const FSeinSnapshotPoolInstanceRecord& Record =
				Snapshot.AbilityPoolRecords[Index];
			if (!Record.bAlive)
			{
				if (Ability) return false;
				continue;
			}
			if (!Ability
				|| Ability->GetClass()->GetPathName()
					!= Record.ClassPath
				|| Ability->GetOuter() == nullptr)
			{
				return false;
			}
			FValidatedAbilityPoolState& State =
				AbilityPoolStates[Index];
			State.Class = Ability->GetClass();
			State.OwnerEntity = Ability->OwnerEntity;
			State.bIsPassive = Ability->bIsPassive;
			State.bIsActive = Ability->bIsActive;
			State.AbilityActivationID = Ability->GetActivationID();
		}
		for (int32 Index = 0; Index < StagedResolverPool.Num(); ++Index)
		{
			USeinCommandBrokerResolver* Resolver =
				StagedResolverPool[Index].Get();
			const FSeinSnapshotPoolInstanceRecord& Record =
				Snapshot.ResolverPoolRecords[Index];
			if ((!Record.bAlive && Resolver)
				|| (Record.bAlive
					&& (!Resolver
						|| Resolver->GetClass()->GetPathName()
							!= Record.ClassPath
						|| Resolver->GetOuter() == nullptr)))
			{
				return false;
			}
		}

		TSet<int64> SeenAbilityActivationIDs;
		for (const FValidatedAbilityPoolState& State : AbilityPoolStates)
		{
			if (!State.Class)
			{
				continue;
			}
			if (State.AbilityActivationID < 0
				|| State.AbilityActivationID
					>= Snapshot.NextAbilityActivationID
				|| (State.bIsActive
					&& State.AbilityActivationID == 0)
				|| (State.AbilityActivationID != 0
					&& SeenAbilityActivationIDs.Contains(
						State.AbilityActivationID)))
			{
				return false;
			}
			if (State.AbilityActivationID != 0)
			{
				SeenAbilityActivationIDs.Add(
					State.AbilityActivationID);
			}
		}

		for (const auto& Pair : Snapshot.PlayerStates)
		{
			const FSeinPlayerState& State = Pair.Value;
			if (State.PlayerID != Pair.Key) return false;
			for (const auto& RefCount : State.PlayerTagRefCounts)
			{
				if (!RefCount.Key.IsValid() || RefCount.Value <= 0
					|| !State.PlayerTags.HasTagExact(RefCount.Key))
				{
					return false;
				}
			}
			TArray<FGameplayTag> PresentTags;
			State.PlayerTags.GetGameplayTagArray(PresentTags);
			for (const FGameplayTag& Tag : PresentTags)
			{
				if (!State.PlayerTagRefCounts.Contains(Tag)) return false;
			}
		}
		TMap<FSeinPairCapabilitySourceKey, int32> PairSourceCounts;
		TMap<FSeinPairCapabilityKey, int32> PairEffectiveCounts;
		for (const FSeinPairCapabilityGrantRecord& Grant :
			Snapshot.PairCapabilityGrants)
		{
			if (!Grant.SourcePlayer.IsValid()
				|| !Grant.TargetPlayer.IsValid()
				|| Grant.SourcePlayer == Grant.TargetPlayer
				|| !IsPairCapabilityTag(Grant.CapabilityTag)
				|| !IsPairCapabilitySourceKindTag(Grant.SourceKindTag)
				|| Grant.SourceInstanceID <= 0
				|| Grant.RefCount <= 0
				|| !Snapshot.PlayerStates.Contains(Grant.SourcePlayer)
				|| !Snapshot.PlayerStates.Contains(Grant.TargetPlayer))
			{
				return false;
			}
			FSeinPairCapabilitySourceKey Key;
			Key.SourcePlayer = Grant.SourcePlayer;
			Key.TargetPlayer = Grant.TargetPlayer;
			Key.CapabilityTag = Grant.CapabilityTag;
			Key.SourceKindTag = Grant.SourceKindTag;
			Key.SourceInstanceID = Grant.SourceInstanceID;
			int32& Count = PairSourceCounts.FindOrAdd(Key);
			if (Count != 0 || Count > MAX_int32 - Grant.RefCount)
			{
				return false;
			}
			Count += Grant.RefCount;

			FSeinPairCapabilityKey EffectiveKey;
			EffectiveKey.SourcePlayer = Grant.SourcePlayer;
			EffectiveKey.TargetPlayer = Grant.TargetPlayer;
			EffectiveKey.CapabilityTag = Grant.CapabilityTag;
			int32& EffectiveCount =
				PairEffectiveCounts.FindOrAdd(EffectiveKey);
			if (EffectiveCount > MAX_int32 - Grant.RefCount)
			{
				return false;
			}
			EffectiveCount += Grant.RefCount;
		}

		TSet<int64> SeenIDs;
		TMap<FString, int32> LedgerGrantMultiset;
		TMap<FString, int32> OwnershipGrantMultiset;
		auto GrantKey = [](int64 EffectID, FSeinEntityHandle Recipient,
			const UClass* AbilityClass)
		{
			return FString::Printf(TEXT("%lld|%d|%d|%s"), EffectID,
				Recipient.Index, Recipient.Generation,
				AbilityClass ? *AbilityClass->GetPathName() : TEXT("<null>"));
		};
		auto IncrementMultiset = [](TMap<FString, int32>& Multiset, FString Key)
		{
			int32& Count = Multiset.FindOrAdd(MoveTemp(Key));
			if (Count == MAX_int32) return false;
			++Count;
			return true;
		};
		auto Accumulate = [&](const TArray<FSeinActiveEffect>& Effects,
			ESeinModifierScope ExpectedScope,
			const FSeinEntityHandle* ExpectedInstanceTarget = nullptr)
		{
			TMap<const UClass*, int32> ClassCounts;
			for (const FSeinActiveEffect& Effect : Effects)
			{
				if (Effect.EffectInstanceID <= 0 || SeenIDs.Contains(Effect.EffectInstanceID))
				{
					return false;
				}
				const USeinEffect* Def = Effect.EffectClass
					? GetDefault<USeinEffect>(Effect.EffectClass)
					: nullptr;
				if (!Def) return false;
				if (Def->Scope != ExpectedScope
					|| (ExpectedInstanceTarget
						&& Effect.Target != *ExpectedInstanceTarget))
				{
					return false;
				}
				const int32 EffectiveMaxStacks = FMath::Max(1, Def->MaxStacks);
				if (Effect.CurrentStacks < 1 || Effect.CurrentStacks > EffectiveMaxStacks
					|| (Def->StackingRule != ESeinEffectStackingRule::Stack
						&& Effect.CurrentStacks != 1))
				{
					return false;
				}
				int32& ClassCount = ClassCounts.FindOrAdd(Effect.EffectClass.Get());
				++ClassCount;
				if ((Def->StackingRule == ESeinEffectStackingRule::Independent
						&& ClassCount > EffectiveMaxStacks)
					|| (Def->StackingRule != ESeinEffectStackingRule::Independent
						&& ClassCount > 1))
				{
					return false;
				}
				for (const FSeinEffectAbilityGrant& Grant : Effect.CommittedAbilityGrants)
				{
					if (!Grant.AbilityClass || !AliveEntityHandles.Contains(Grant.Recipient))
					{
						return false;
					}
					if (!IncrementMultiset(LedgerGrantMultiset, GrantKey(
						Effect.EffectInstanceID, Grant.Recipient,
						Grant.AbilityClass.Get())))
					{
						return false;
					}
				}
				SeenIDs.Add(Effect.EffectInstanceID);
				if (Effect.EffectInstanceID > OutMaxEffectID)
				{
					OutMaxEffectID = Effect.EffectInstanceID;
				}
			}
			return true;
		};

		for (const auto& Pair : Snapshot.PlayerStates)
		{
			if (!Accumulate(Pair.Value.ClassEffects, ESeinModifierScope::Class)
				|| !Accumulate(Pair.Value.PlayerEffects, ESeinModifierScope::Player))
			{
				return false;
			}
		}

		if (!DecodeSnapshotComponentBlob<FSeinActiveEffectsComponent>(
			Snapshot, AliveHandleBySlot,
			[&](int32 Slot, const FSeinActiveEffectsComponent& Component)
		{
			const FSeinEntityHandle* Target = AliveHandleBySlot.Find(Slot);
			return Target && Accumulate(Component.ActiveEffects,
				ESeinModifierScope::Instance, Target);
		})) return false;

		TSet<int32> ReferencedAbilityPoolIDs;
		if (!DecodeSnapshotComponentBlob<FSeinAbilityComponent>(
			Snapshot, AliveHandleBySlot,
			[&](int32 Slot, const FSeinAbilityComponent& Component)
		{
			const FSeinEntityHandle* Recipient = AliveHandleBySlot.Find(Slot);
			if (!Recipient) return false;
			const int32 NumInstances = Component.AbilityInstanceIDs.Num();
			if (Component.AbilityGrantCounts.Num() != NumInstances
				|| Component.AbilityGrantOwnership.Num() != NumInstances)
			{
				return false;
			}
			if (Component.ActiveAbilityID != INDEX_NONE
				&& !Component.AbilityInstanceIDs.Contains(Component.ActiveAbilityID))
			{
				return false;
			}
			TSet<int32> ActivePassiveIDs;
			for (int32 PassiveID : Component.ActivePassiveIDs)
			{
				if (!Component.AbilityInstanceIDs.Contains(PassiveID)
					|| ActivePassiveIDs.Contains(PassiveID))
				{
					return false;
				}
				ActivePassiveIDs.Add(PassiveID);
			}
			for (int32 Index = 0; Index < NumInstances; ++Index)
			{
				const int32 PoolID = Component.AbilityInstanceIDs[Index];
				if (!Snapshot.AbilityPoolRecords.IsValidIndex(PoolID)
					|| !Snapshot.AbilityPoolRecords[PoolID].bAlive
					|| ReferencedAbilityPoolIDs.Contains(PoolID))
				{
					return false;
				}
				ReferencedAbilityPoolIDs.Add(PoolID);
				const FValidatedAbilityPoolState& AbilityState =
					AbilityPoolStates[PoolID];
				const UClass* AbilityClass = AbilityState.Class;
				if (!AbilityClass || AbilityState.OwnerEntity != *Recipient)
				{
					return false;
				}
				const bool bIsPrimary = Component.ActiveAbilityID == PoolID;
				const bool bIsListedPassive = ActivePassiveIDs.Contains(PoolID);
				const bool bMustBePrimary =
					AbilityState.bIsActive && !AbilityState.bIsPassive;
				const bool bMustBeListedPassive =
					AbilityState.bIsActive && AbilityState.bIsPassive;
				// Activity identity is an exact bidirectional lifecycle contract:
				// every active ability is indexed in its role, and no inactive
				// ability may remain tickable/cancellable through a stale locator.
				if (bIsPrimary != bMustBePrimary
					|| bIsListedPassive != bMustBeListedPassive)
				{
					return false;
				}
				const FSeinAbilityGrantOwnership& Ownership =
					Component.AbilityGrantOwnership[Index];
				if (Ownership.AnonymousGrantCount < 0) return false;
				for (int64 EffectID : Ownership.EffectInstanceIDs)
				{
					if (EffectID <= 0 || !SeenIDs.Contains(EffectID)) return false;
					if (!IncrementMultiset(OwnershipGrantMultiset,
						GrantKey(EffectID, *Recipient, AbilityClass)))
					{
						return false;
					}
				}
				const int64 Total = static_cast<int64>(Ownership.AnonymousGrantCount)
					+ static_cast<int64>(Ownership.EffectInstanceIDs.Num());
				if (Total < 1 || Total > MAX_int32
					|| Component.AbilityGrantCounts[Index] != Total)
				{
					return false;
				}
			}
			return true;
		})) return false;
		for (int32 PoolID = 0; PoolID < Snapshot.AbilityPoolRecords.Num(); ++PoolID)
		{
			if (Snapshot.AbilityPoolRecords[PoolID].bAlive
				&& !ReferencedAbilityPoolIDs.Contains(PoolID))
			{
				return false;
			}
		}
		return LedgerGrantMultiset.OrderIndependentCompareEqual(
			OwnershipGrantMultiset);
	}
}

bool USeinWorldSubsystem::ClaimSnapshotRestoreAuthority(
	FName StableAuthorityID,
	const UObject* AuthorityOwner,
	FSeinSnapshotRestoreAuthorityHandle& OutHandle,
	FString& OutError)
{
	OutError.Reset();

	// Never overwrite a live capability supplied as the output slot. It may
	// belong to another world, whose outstanding claim would otherwise be
	// stranded with no handle available to release it.
	if (OutHandle.IsValid())
	{
		OutError =
			TEXT("Snapshot restore authority output handle must be invalid.");
		return false;
	}
	if (!IsInGameThread())
	{
		OutError =
			TEXT("Snapshot restore authority may be claimed only on the game thread.");
		return false;
	}
	if (bExecutionTopologyTeardown
		|| bSimulationTickDispatchInProgress || SeinIsInSimContext()
		|| OwnerTransitionDepth != 0
		|| bSnapshotCaptureInProgress || bSnapshotRestoreInProgress
		|| bReadOnlyCallbackInProgress || bObserverCallbackInProgress
		|| bDestroyNotificationInProgress)
	{
		OutError =
			TEXT("Snapshot restore authority is unavailable during simulation dispatch, callbacks, ownership transitions, capture/restore, or teardown.");
		return false;
	}

	// A weak owner must never strand the world behind an orphaned token.
	if (SnapshotRestoreAuthorityToken.IsValid()
		&& !SnapshotRestoreAuthorityOwner.IsValid())
	{
		ClearSnapshotRestoreAuthority();
	}
	if (SnapshotRestoreAuthorityToken.IsValid())
	{
		if (StableAuthorityID == SnapshotRestoreAuthorityID
			&& AuthorityOwner
			&& SnapshotRestoreAuthorityOwner.Get() == AuthorityOwner)
		{
			OutHandle.StableAuthorityID = SnapshotRestoreAuthorityID;
			OutHandle.Token = SnapshotRestoreAuthorityToken;
			return true;
		}

		OutError = FString::Printf(
			TEXT("Snapshot restore authority is already claimed by '%s'."),
			*SnapshotRestoreAuthorityID.ToString());
		return false;
	}
	if (StableAuthorityID.IsNone() || !AuthorityOwner)
	{
		OutError =
			TEXT("Snapshot restore authority requires a stable ID and concrete UObject owner.");
		return false;
	}

	const UWorld* ThisWorld = GetWorld();
	const UGameInstance* ThisGameInstance = ThisWorld
		? ThisWorld->GetGameInstance()
		: nullptr;
	const bool bOwnerBelongsToWorld = AuthorityOwner == this
		|| AuthorityOwner == ThisWorld
		|| AuthorityOwner == ThisGameInstance
		|| AuthorityOwner->GetWorld() == ThisWorld
		|| (ThisGameInstance
			&& AuthorityOwner->GetTypedOuter<UGameInstance>()
				== ThisGameInstance);
	if (!ThisWorld || !bOwnerBelongsToWorld)
	{
		OutError =
			TEXT("Snapshot restore authority owner does not belong to this world or its game instance.");
		return false;
	}

	const FGuid Token = FGuid::NewGuid();
	if (!Token.IsValid())
	{
		OutError = TEXT("Snapshot restore authority token allocation failed.");
		return false;
	}
	SnapshotRestoreAuthorityID = StableAuthorityID;
	SnapshotRestoreAuthorityToken = Token;
	SnapshotRestoreAuthorityOwner = AuthorityOwner;
	OutHandle.StableAuthorityID = StableAuthorityID;
	OutHandle.Token = Token;
	return true;
}

bool USeinWorldSubsystem::ReleaseSnapshotRestoreAuthority(
	FSeinSnapshotRestoreAuthorityHandle&& Authority,
	FString& OutError)
{
	OutError.Reset();
	if (!IsInGameThread())
	{
		OutError =
			TEXT("Snapshot restore authority may be released only on the game thread.");
		return false;
	}
	if (!IsExactSnapshotRestoreAuthority(Authority))
	{
		OutError = TEXT("Snapshot restore authority does not match this world.");
		return false;
	}
	Authority.StableAuthorityID = NAME_None;
	Authority.Token.Invalidate();
	ClearSnapshotRestoreAuthority();
	return true;
}

bool USeinWorldSubsystem::IsExactSnapshotRestoreAuthority(
	const FSeinSnapshotRestoreAuthorityHandle& Authority) const
{
	return SnapshotRestoreAuthorityToken.IsValid()
		&& SnapshotRestoreAuthorityOwner.IsValid()
		&& Authority.Token == SnapshotRestoreAuthorityToken
		&& Authority.StableAuthorityID == SnapshotRestoreAuthorityID;
}

void USeinWorldSubsystem::ClearSnapshotRestoreAuthority()
{
	SnapshotRestoreAuthorityOwner.Reset();
	SnapshotRestoreAuthorityID = NAME_None;
	SnapshotRestoreAuthorityToken.Invalidate();
}

bool USeinWorldSubsystem::RestoreSnapshot(
	FSeinSnapshotRestoreAuthorityHandle&& Authority,
	const FSeinWorldSnapshot& InSnapshot,
	const FSeinSnapshotRestoreOptions& Options)
{
	if (!IsInGameThread())
	{
		UE_LOG(LogSeinSim, Error,
			TEXT("RestoreSnapshot: restore is available only on the game thread."));
		return false;
	}
	if (!IsExactSnapshotRestoreAuthority(Authority))
	{
		UE_LOG(LogSeinSim, Error,
			TEXT("RestoreSnapshot: rejected without this world's exact trusted-envelope authority."));
		return false;
	}

	// An authenticated envelope authorizes one attempt only. Consume before
	// parsing or staging so malformed and lifecycle-invalid inputs cannot be
	// replayed under a stale authorization.
	const FName ConsumedRestoreAuthorityID = SnapshotRestoreAuthorityID;
	Authority.StableAuthorityID = NAME_None;
	Authority.Token.Invalidate();
	ClearSnapshotRestoreAuthority();
	if (Options.LocalStatePolicy !=
			ESeinSnapshotLocalStateRestorePolicy::RestoreCaptured
		&& Options.LocalStatePolicy !=
			ESeinSnapshotLocalStateRestorePolicy::PreserveCurrent)
	{
		UE_LOG(LogSeinSim, Error,
			TEXT("RestoreSnapshot: invalid local-state restore policy."));
		return false;
	}
	if (Options.ResumePolicy != ESeinSnapshotResumePolicy::ResumeImmediately
		&& Options.ResumePolicy != ESeinSnapshotResumePolicy::RemainStopped)
	{
		UE_LOG(LogSeinSim, Error,
			TEXT("RestoreSnapshot: invalid simulation-resume policy."));
		return false;
	}
	if (bExecutionTopologyTeardown
		|| bSimulationTickDispatchInProgress || SeinIsInSimContext()
		|| OwnerTransitionDepth != 0
		|| bReadOnlyCallbackInProgress || bObserverCallbackInProgress
		|| bDestroyNotificationInProgress)
	{
		UE_LOG(LogSeinSim, Error,
			TEXT("RestoreSnapshot: restore is unavailable during simulation dispatch, callbacks, ownership transitions, or teardown."));
		return false;
	}
	if (bReplayOwnsExternalCommandIngress)
	{
		UE_LOG(LogSeinSim, Error,
			TEXT("RestoreSnapshot: active replay ingress must stop before checkpoint adoption."));
		return false;
	}
	if (bSnapshotCaptureInProgress || bSnapshotRestoreInProgress)
	{
		UE_LOG(LogSeinSim, Error,
			TEXT("RestoreSnapshot: recursive or capture-overlapping restore is not permitted."));
		return false;
	}
	TGuardValue<bool> RestoreInProgressGuard(
		bSnapshotRestoreInProgress, true);
	FSeinWorldSnapshotReferenceGuard SnapshotGCGuard(InSnapshot);

	if (InSnapshot.SnapshotVersion != FSeinWorldSnapshot::CurrentVersion)
	{
		UE_LOG(LogSeinSim, Error, TEXT("RestoreSnapshot: unsupported version %d (expected %d)."),
			InSnapshot.SnapshotVersion, FSeinWorldSnapshot::CurrentVersion);
		return false;
	}
	const FString LocalFrameworkVersion =
		SeinReplayCompatibility::GetFrameworkVersion();
	const FString LocalGameVersion = SeinReplayCompatibility::GetGameVersion();
	const FName LocalMapIdentifier =
		SeinReplayCompatibility::GetMapIdentifier(GetWorld());
	if (InSnapshot.FrameworkVersion != LocalFrameworkVersion
		|| InSnapshot.GameVersion != LocalGameVersion
		|| LocalMapIdentifier.IsNone()
		|| InSnapshot.MapIdentifier != LocalMapIdentifier)
	{
		UE_LOG(LogSeinSim, Error,
			TEXT("RestoreSnapshot: runtime compatibility mismatch (framework=%s/%s game=%s/%s map=%s/%s)."),
			*InSnapshot.FrameworkVersion, *LocalFrameworkVersion,
			*InSnapshot.GameVersion, *LocalGameVersion,
			*InSnapshot.MapIdentifier.ToString(),
			*LocalMapIdentifier.ToString());
		return false;
	}
	if (!IsSimulationContentReady()
		|| InSnapshot.SimulationContentDigest
			!= SimulationContentDigest)
	{
		UE_LOG(LogSeinSim, Error,
			TEXT("RestoreSnapshot: simulation-content digest mismatch."));
		return false;
	}

	const FSeinSnapshotBootstrapCheckpoint& Checkpoint =
		InSnapshot.BootstrapCheckpoint;
	if (!Checkpoint.IsValidConsumedCheckpoint()
		|| Checkpoint.Receipt.ContractDigest != InSnapshot.MatchSettingsDigest
		|| Checkpoint.Receipt.SimulationContentDigest
			!= InSnapshot.SimulationContentDigest)
	{
		UE_LOG(LogSeinSim, Error,
			TEXT("RestoreSnapshot: invalid consumed-bootstrap checkpoint envelope."));
		return false;
	}
	TArray<FSeinCanonicalInitialStateValueContribution>
		CheckpointContributions;
	CheckpointContributions.Reserve(
		Checkpoint.InitialStateContributions.Num());
	FString PreviousContributorID;
	for (const FSeinSnapshotInitialStateContribution& SnapshotValue :
		Checkpoint.InitialStateContributions)
	{
		const FString CanonicalID =
			FSeinCanonicalInitialStateDigest::CanonicalContributorID(
				SnapshotValue.StableContributorID);
		if (CanonicalID.IsEmpty() || SnapshotValue.SchemaVersion == 0
			|| !SnapshotValue.ValueDigest.IsValid()
			|| (!PreviousContributorID.IsEmpty()
				&& CanonicalID <= PreviousContributorID))
		{
			UE_LOG(LogSeinSim, Error,
				TEXT("RestoreSnapshot: invalid canonical bootstrap contribution envelope."));
			return false;
		}
		PreviousContributorID = CanonicalID;

		FSeinCanonicalInitialStateValueContribution& Value =
			CheckpointContributions.AddDefaulted_GetRef();
		Value.StableContributorID = SnapshotValue.StableContributorID;
		Value.SchemaVersion = SnapshotValue.SchemaVersion;
		Value.ValueDigest = SnapshotValue.ValueDigest;
	}

	const bool bFreshBootstrapAdoption =
		MatchBootstrapState == ESeinMatchBootstrapState::Awaiting;
	if (!bFreshBootstrapAdoption
		&& (MatchBootstrapState != ESeinMatchBootstrapState::Consumed
			|| MatchBootstrapReceipt != Checkpoint.Receipt
			|| MatchBootstrapAuthorizationContextDigest
				!= Checkpoint.AuthorizationContextDigest
			|| SimSessionSeed != InSnapshot.SessionSeed))
	{
		UE_LOG(LogSeinSim, Error,
			TEXT("RestoreSnapshot: checkpoint does not belong to this consumed bootstrap."));
		return false;
	}
	if (!bFreshBootstrapAdoption)
	{
		TArray<FSeinCanonicalInitialStateValueContribution>
			ExistingContributions = MatchBootstrapValueContributions;
		ExistingContributions.Sort(
			[](const FSeinCanonicalInitialStateValueContribution& A,
				const FSeinCanonicalInitialStateValueContribution& B)
			{
				return FSeinCanonicalInitialStateDigest::CanonicalContributorID(
					A.StableContributorID)
					< FSeinCanonicalInitialStateDigest::CanonicalContributorID(
						B.StableContributorID);
			});
		bool bExactContributions = MatchBootstrapNativeContributors.IsEmpty()
			&& ExistingContributions.Num() == CheckpointContributions.Num();
		for (int32 Index = 0;
			bExactContributions && Index < ExistingContributions.Num(); ++Index)
		{
			const FSeinCanonicalInitialStateValueContribution& Existing =
				ExistingContributions[Index];
			const FSeinCanonicalInitialStateValueContribution& Saved =
				CheckpointContributions[Index];
			bExactContributions =
				FSeinCanonicalInitialStateDigest::CanonicalContributorID(
					Existing.StableContributorID)
				== FSeinCanonicalInitialStateDigest::CanonicalContributorID(
					Saved.StableContributorID)
				&& Existing.SchemaVersion == Saved.SchemaVersion
				&& Existing.ValueDigest == Saved.ValueDigest;
		}
		if (!bExactContributions)
		{
			UE_LOG(LogSeinSim, Error,
				TEXT("RestoreSnapshot: checkpoint bootstrap contributions do not match this world."));
			return false;
		}
	}
	if (bFreshBootstrapAdoption)
	{
		const TArray<FSeinPlayerID> ExistingPlayers = GetRegisteredPlayerIDs();
		const bool bPristineWorld = !bIsRunning
			&& !bSimulationSchedulerReserved && !TickerHandle.IsValid()
			&& CurrentTick == 0 && MatchState == ESeinMatchState::Lobby
			&& !MatchBootstrapReceipt.IsValid()
			&& !MatchBootstrapAuthorizationContextDigest.IsValid()
			&& MatchBootstrapFailureReason.IsEmpty()
			&& MatchBootstrapNativeContributors.IsEmpty()
			&& MatchBootstrapValueContributions.IsEmpty()
			&& !bMatchBootstrapClosedBroadcast
			&& !bMatchBootstrapMaterializerInvocationActive
			&& !bSnapshotRestoreMutationAuthorized
			&& EntityPool.GetActiveCount() == 0
			&& ExistingPlayers.Num() == 1
			&& ExistingPlayers[0].IsNeutral()
			&& PendingCommands.Num() == 0
			&& PendingReplayCommands.Num() == 0
			&& PendingStandalonePauseControlCommands.IsEmpty()
			&& PendingDestroy.IsEmpty() && PendingEffectApplies.IsEmpty()
			&& OwnerTransitionRevisions.IsEmpty() && ActiveVotes.IsEmpty()
			&& !bDispatchingPauseControlFrame
			&& !bPauseControlDispatchProtocolViolation
			&& EntityTagStates.IsEmpty() && EntityTagIndex.IsEmpty()
			&& NamedEntityRegistry.IsEmpty()
			&& AbilityPool.IsEmpty() && CommandBrokerResolverPool.IsEmpty()
			&& LatentActionManager
			&& LatentActionManager->GetActiveActionCount() == 0
			&& LatentActionManager->GetNextActionID() == 1
			&& NextAbilityActivationID == 1
			&& ComponentStorages.IsEmpty() && Factions.IsEmpty()
			&& (!bSimSessionSeedInstalled
				|| SimSessionSeed == InSnapshot.SessionSeed);
		if (!bPristineWorld)
		{
			UE_LOG(LogSeinSim, Error,
				TEXT("RestoreSnapshot: fresh checkpoint adoption requires a pristine Awaiting world."));
			return false;
		}
	}
	if (!bCommandProtocolReady
		|| InSnapshot.CommandProtocolDigest != CommandProtocolDigest)
	{
		UE_LOG(LogSeinSim, Error,
			TEXT("RestoreSnapshot: command protocol digest mismatch."));
		return false;
	}
	if (InSnapshot.ConfigFingerprint != ConfigFingerprint)
	{
		UE_LOG(LogSeinSim, Error,
			TEXT("RestoreSnapshot: configuration fingerprint mismatch (%d != %d)."),
			InSnapshot.ConfigFingerprint, ConfigFingerprint);
		return false;
	}
	if (InSnapshot.CurrentTick < 0 || InSnapshot.MatchStartTick < 0
		|| InSnapshot.StartingStateDeadlineTick < 0
		|| (InSnapshot.PRNGState0 == 0 && InSnapshot.PRNGState1 == 0)
		|| InSnapshot.MatchState > static_cast<uint8>(ESeinMatchState::Ended)
		|| (InSnapshot.bSimPausedHard && !InSnapshot.bSimPaused)
		|| (InSnapshot.MatchState == static_cast<uint8>(ESeinMatchState::Paused)
			&& !InSnapshot.bSimPaused)
		|| (InSnapshot.MatchState == static_cast<uint8>(ESeinMatchState::Playing)
			&& InSnapshot.bSimPaused)
		|| InSnapshot.PauseEpoch < 0
		|| InSnapshot.PauseFrozenTick < INDEX_NONE
		|| InSnapshot.LastAppliedPauseControlSequence < -1
		|| (InSnapshot.PauseEpoch == 0
			&& (InSnapshot.PauseFrozenTick != INDEX_NONE
				|| InSnapshot.LastAppliedPauseControlSequence != -1))
		|| (InSnapshot.PauseEpoch > 0
			&& (InSnapshot.PauseFrozenTick < 0
				|| InSnapshot.PauseFrozenTick > InSnapshot.CurrentTick))
		|| (InSnapshot.bSimPaused
			&& (InSnapshot.PauseEpoch == 0
				|| InSnapshot.PauseFrozenTick != InSnapshot.CurrentTick)))
	{
		UE_LOG(LogSeinSim, Error,
			TEXT("RestoreSnapshot: invalid match/pause state."));
		return false;
	}
	const auto IsAuthenticatedSnapshotCommand = [](const FSeinCommand& Command)
	{
		return Command.IssuerKind != ESeinCommandIssuerKind::Unauthenticated
			&& StaticEnum<ESeinCommandIssuerKind>()->IsValidEnumValue(
				static_cast<int64>(Command.IssuerKind))
			&& (!Command.DerivedResourcePayer.IsValid()
				|| (Command.IssuerKind == ESeinCommandIssuerKind::DeterministicSystem
					&& Command.CommandType
						== SeinARTSTags::Command_Type_ActivateAbility));
	};
	for (const FSeinCommand& Command : InSnapshot.PendingCommands)
	{
		if (!IsAuthenticatedSnapshotCommand(Command)
			|| CommandSchemaSnapshot.ValidateStructure(Command)
				!= ESeinCommandStructureResult::Valid)
		{
			UE_LOG(LogSeinSim, Error,
				TEXT("RestoreSnapshot: invalid ordinary command continuation."));
			return false;
		}
	}
	if (InSnapshot.PendingStandalonePauseControlCommands.Num()
		> MaxPauseControlCommandsPerFrame
		|| (!InSnapshot.PendingStandalonePauseControlCommands.IsEmpty()
			&& !InSnapshot.bSimPaused))
	{
		UE_LOG(LogSeinSim, Error,
			TEXT("RestoreSnapshot: invalid standalone pause-control continuation."));
		return false;
	}
	for (const FSeinCommand& Command :
		InSnapshot.PendingStandalonePauseControlCommands)
	{
		FSeinCommandSchemaDescriptor Schema;
		if (!IsAuthenticatedSnapshotCommand(Command)
			|| Command.Tick != InSnapshot.PauseFrozenTick
			|| CommandSchemaSnapshot.ValidateStructure(Command, &Schema)
				!= ESeinCommandStructureResult::Valid
			|| (Schema.AllowedExecutionContexts
				& static_cast<int32>(ESeinCommandExecutionAllowance::FrozenPauseControl)) == 0)
		{
			UE_LOG(LogSeinSim, Error,
				TEXT("RestoreSnapshot: invalid standalone pause-control continuation."));
			return false;
		}
	}
	FGameplayTag MatchValidationRejection;
	if (!ValidateMatchSettings(
		InSnapshot.MatchSettings, MatchValidationRejection))
	{
		UE_LOG(LogSeinSim, Error,
			TEXT("RestoreSnapshot: match settings violate the frozen command contract (%s)."),
			*MatchValidationRejection.ToString());
		return false;
	}
	FSeinMatchSettings CanonicalSnapshotSettings = InSnapshot.MatchSettings;
	FGuid RestoredMatchSettingsDigest;
	FSeinDeterministicValueDigestError MatchDigestError;
	if (!SeinCanonicalizeAndDigestMatchSettings(
			CanonicalSnapshotSettings, RestoredMatchSettingsDigest, &MatchDigestError)
		|| RestoredMatchSettingsDigest != InSnapshot.MatchSettingsDigest)
	{
		UE_LOG(LogSeinSim, Error,
			TEXT("RestoreSnapshot: match settings digest is invalid (%s: %s)."),
			*MatchDigestError.FieldPath, *MatchDigestError.Message);
		return false;
	}

	// A fresh target has not frozen its dispatch contract yet. Build the exact
	// canonical order/manifest/digest as inert local data; a rejected checkpoint
	// must not turn an Awaiting world into a frozen one. Existing matches retain
	// and validate the topology they already own.
	FExecutionTopologyCandidate StagedExecutionTopology;
	const bool bNeedsExecutionTopologyAdoption =
		!bExecutionTopologyFrozen;
	FString TopologyError;
	if (bExecutionTopologyFrozen)
	{
		if (!bExecutionTopologyValid
			|| !ExecutionTopologyDigest.IsValid()
			|| ExecutionTopologyManifest.IsEmpty())
		{
			TopologyError = ExecutionTopologyFailureReason.IsEmpty()
				? TEXT("The frozen execution topology is invalid.")
				: ExecutionTopologyFailureReason;
		}
	}
	else if (!TryBuildExecutionTopologyCandidate(
		StagedExecutionTopology, TopologyError))
	{
		// Candidate construction is deliberately non-poisoning here. Restore
		// validates imported state; refusal cannot close the bootstrap barrier.
		if (TopologyError.IsEmpty())
		{
			TopologyError =
				TEXT("Execution topology candidate construction failed.");
		}
	}
	if (!TopologyError.IsEmpty())
	{
		UE_LOG(LogSeinSim, Error,
			TEXT("RestoreSnapshot: execution topology is unavailable (%s)."),
			*TopologyError);
		return false;
	}
	const FString& RestoreTopologyManifest =
		bNeedsExecutionTopologyAdoption
			? StagedExecutionTopology.Manifest
			: ExecutionTopologyManifest;

	// Declare the state schema from local native/Blueprint recipes before
	// checkpoint records are allowed to supply any values. This is the
	// anti-self-description boundary: imported type paths and bounds are
	// compared as data and are never used as load instructions.
	FSeinCanonicalStateValueStore ExpectedCanonicalStateValues;
	FSeinCanonicalStateValueStore StagedCanonicalStateValues;
	class FStagedCanonicalStateValueGCGuard final : public FGCObject
	{
	public:
		explicit FStagedCanonicalStateValueGCGuard(
			FSeinCanonicalStateValueStore& InStore,
			const TCHAR* InName)
			: Store(InStore)
			, Name(InName)
		{
		}

		virtual void AddReferencedObjects(
			FReferenceCollector& Collector) override
		{
			Store.AddReferencedObjects(Collector);
		}

		virtual FString GetReferencerName() const override
		{
			return Name;
		}

	private:
		FSeinCanonicalStateValueStore& Store;
		FString Name;
	};
	FStagedCanonicalStateValueGCGuard ExpectedCanonicalValueGCGuard(
		ExpectedCanonicalStateValues,
		TEXT("SeinSnapshotExpectedCanonicalStateValues"));
	FStagedCanonicalStateValueGCGuard StagedCanonicalValueGCGuard(
		StagedCanonicalStateValues,
		TEXT("SeinSnapshotStagedCanonicalStateValues"));
	TArray<FString> StagedWorldBindingFrames;
	FString StateContractError;
	if (!BuildLocallyDeclaredCanonicalState(
			CanonicalSnapshotSettings,
			false,
			RestoreTopologyManifest,
			ExpectedCanonicalStateValues,
			StagedWorldBindingFrames,
			StateContractError)
		|| !StagedCanonicalStateValues.TryRestoreRecords(
			ExpectedCanonicalStateValues,
			InSnapshot.CanonicalStateValueRecords,
			Checkpoint.Receipt.StateContractDigest,
			StateContractError))
	{
		UE_LOG(LogSeinSim, Error,
			TEXT("RestoreSnapshot: local canonical-state declaration or checkpoint values are invalid (%s)."),
			*StateContractError);
		return false;
	}
	if (InSnapshot.NextEffectInstanceID <= 0
		|| InSnapshot.NextLatentActionID <= 0
		|| InSnapshot.NextAbilityActivationID <= 0
		|| !InSnapshot.LatentActionSequenceDigest.IsValid()
		|| !LatentActionManager)
	{
		UE_LOG(LogSeinSim, Error,
			TEXT("RestoreSnapshot: invalid allocator cursor, latent sequence, or manager state."));
		return false;
	}

	// Materialize each live pool slot once, directly under its final Outer.
	// These exact candidates feed semantic validation and, if every later
	// check succeeds, authoritative adoption. Imported paths are only compared
	// with the locally frozen manifest by the registry.
	if (!PoolObjectCodecManifest.IsValid()
		|| !PoolObjectCodecManifest.GetDigest().IsValid()
		|| InSnapshot.AbilityPoolRecords.Num()
			> FSeinWorldSnapshot::MaxSupportedObjectPoolSlots
		|| InSnapshot.ResolverPoolRecords.Num()
			> FSeinWorldSnapshot::MaxSupportedObjectPoolSlots
		|| InSnapshot.AbilityPoolFreeList.Num()
			> InSnapshot.AbilityPoolRecords.Num()
		|| InSnapshot.ResolverPoolFreeList.Num()
			> InSnapshot.ResolverPoolRecords.Num())
	{
		UE_LOG(LogSeinSim, Error,
			TEXT("RestoreSnapshot: invalid or unavailable pool-object provider contract."));
		return false;
	}
	TArray<TStrongObjectPtr<USeinAbility>> StagedAbilityPool;
	TArray<int32> StagedAbilityFreeList =
		InSnapshot.AbilityPoolFreeList;
	TArray<TStrongObjectPtr<USeinCommandBrokerResolver>>
		StagedResolverPool;
	TArray<int32> StagedResolverFreeList =
		InSnapshot.ResolverPoolFreeList;
	StagedAbilityPool.Reserve(
		InSnapshot.AbilityPoolRecords.Num());
	StagedResolverPool.Reserve(
		InSnapshot.ResolverPoolRecords.Num());
	{
		TGuardValue<bool> ReadOnlyGuard(
			bReadOnlyCallbackInProgress, true);
		TGuardValue<bool> ObserverGuard(
			bObserverCallbackInProgress, true);
		for (const FSeinSnapshotPoolInstanceRecord& Record :
			InSnapshot.AbilityPoolRecords)
		{
			if (!Record.bAlive)
			{
				StagedAbilityPool.Add(
					TStrongObjectPtr<USeinAbility>());
				continue;
			}
			FString PoolError;
			UObject* Candidate =
				FSeinPoolObjectCodecRegistry::MaterializeObject(
					PoolObjectCodecManifest,
					Record,
					ESeinPoolObjectKind::Ability,
					*this,
					PoolError);
			USeinAbility* Ability = Cast<USeinAbility>(Candidate);
			if (!Ability)
			{
				UE_LOG(LogSeinSim, Error,
					TEXT("RestoreSnapshot: ability slot %d failed provider materialization (%s)."),
					Record.PoolID, *PoolError);
				return false;
			}
			TStrongObjectPtr<USeinAbility> AbilityRoot(Ability);
			AbilityRoot->WorldSubsystem = nullptr;
			AbilityRoot->RuntimePoolID = Record.PoolID;
			StagedAbilityPool.Add(MoveTemp(AbilityRoot));
		}
		for (const FSeinSnapshotPoolInstanceRecord& Record :
			InSnapshot.ResolverPoolRecords)
		{
			if (!Record.bAlive)
			{
				StagedResolverPool.Add(
					TStrongObjectPtr<USeinCommandBrokerResolver>());
				continue;
			}
			FString PoolError;
			UObject* Candidate =
				FSeinPoolObjectCodecRegistry::MaterializeObject(
					PoolObjectCodecManifest,
					Record,
					ESeinPoolObjectKind::
						CommandBrokerResolver,
					*this,
					PoolError);
			USeinCommandBrokerResolver* Resolver =
				Cast<USeinCommandBrokerResolver>(Candidate);
			if (!Resolver)
			{
				UE_LOG(LogSeinSim, Error,
					TEXT("RestoreSnapshot: resolver slot %d failed provider materialization (%s)."),
					Record.PoolID, *PoolError);
				return false;
			}
			StagedResolverPool.Emplace(Resolver);
		}
	}

	int64 MaxActiveEffectID = 0;
	TMap<FSeinEntityHandle, FSeinEntityTagState> StagedEntityTagStates;
	TMap<FGameplayTag, TArray<FSeinEntityHandle>> StagedEntityTagIndex;
	TMap<FName, FSeinEntityHandle> StagedNamedEntityRegistry;
	TMap<FGameplayTag, FSeinVoteState> StagedActiveVotes;
	if (!TryValidateSnapshotSimState(
		InSnapshot,
		StagedAbilityPool,
		StagedResolverPool,
		MaxActiveEffectID,
		StagedEntityTagStates, StagedEntityTagIndex,
		StagedNamedEntityRegistry, StagedActiveVotes))
	{
		UE_LOG(LogSeinSim, Error,
			TEXT("RestoreSnapshot: authoritative sim state failed structural preflight."));
		return false;
	}
	if (InSnapshot.NextEffectInstanceID <= MaxActiveEffectID)
	{
		UE_LOG(LogSeinSim, Error,
			TEXT("RestoreSnapshot: next effect ID %lld must exceed max active effect ID %lld."),
			InSnapshot.NextEffectInstanceID, MaxActiveEffectID);
		return false;
	}

	// Stage every resource whose resolution or deserialization can fail before
	// replacing live state. The commit section below then consists only of
	// moves, resets, and already-validated deterministic reconstruction.
	const int32 MaxSnapshotEntitySlot =
		InSnapshot.EntityPoolState.Capacity;
	TMap<FSeinEntityHandle, TSubclassOf<ASeinActor>>
		StagedEntityActorClasses;
	TArray<TStrongObjectPtr<UClass>> StagedEntityActorClassRoots;
	StagedEntityActorClassRoots.Reserve(InSnapshot.Entities.Num());
	for (const FSeinSnapshotEntityRecord& Record : InSnapshot.Entities)
	{
		if (!Record.bAlive || Record.ActorClassPath.IsEmpty())
		{
			continue;
		}
		UClass* ActorClass = LoadClass<ASeinActor>(
			nullptr, *Record.ActorClassPath);
		if (!ActorClass || ActorClass->HasAnyClassFlags(
			CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists)
			|| ActorClass->GetPathName() != Record.ActorClassPath)
		{
			UE_LOG(LogSeinSim, Error,
				TEXT("RestoreSnapshot: invalid entity actor class %s."),
				*Record.ActorClassPath);
			return false;
		}
		StagedEntityActorClassRoots.Emplace(ActorClass);
		StagedEntityActorClasses.Add(
			FSeinEntityHandle(Record.SlotIndex, Record.Generation),
			TSubclassOf<ASeinActor>(ActorClass));
	}

	FSeinEntityPool StagedEntityPool;
	FString EntityPoolStageError;
	if (!StagedEntityPool.TryStageExactState(
		InSnapshot.EntityPoolState,
		FSeinWorldSnapshot::MaxSupportedEntitySlotIndex,
		EntityPoolStageError))
	{
		UE_LOG(LogSeinSim, Error,
			TEXT("RestoreSnapshot: entity-pool topology failed staging (%s)."),
			*EntityPoolStageError);
		return false;
	}

	struct FStagedComponentStorage
	{
		UScriptStruct* Type = nullptr;
		TStrongObjectPtr<UScriptStruct> TypeRoot;
		TUniquePtr<FSeinGenericComponentStorage> Storage;
	};
	TArray<FStagedComponentStorage> StagedComponentStorages;
	class FStagedComponentStorageGCGuard final : public FGCObject
	{
	public:
		FStagedComponentStorageGCGuard(
			TArray<FStagedComponentStorage>& InStorages,
			UObject& InOwner)
			: Storages(InStorages), Owner(InOwner)
		{
		}

		virtual void AddReferencedObjects(
			FReferenceCollector& Collector) override
		{
			for (FStagedComponentStorage& Staged : Storages)
			{
				if (Staged.Storage)
				{
					Staged.Storage->CollectReferences(Collector, &Owner);
				}
			}
		}

		virtual FString GetReferencerName() const override
		{
			return TEXT("SeinSnapshotStagedComponentStorage");
		}

	private:
		TArray<FStagedComponentStorage>& Storages;
		UObject& Owner;
	};
	FStagedComponentStorageGCGuard StagedStorageGCGuard(
		StagedComponentStorages, *this);
	StagedComponentStorages.Reserve(
		InSnapshot.ComponentStorageBlobs.Num());
	for (const auto& Pair : InSnapshot.ComponentStorageBlobs)
	{
		UScriptStruct* StructType = FindObject<UScriptStruct>(
			nullptr, *Pair.Key);
		if (!StructType || StructType->GetPathName() != Pair.Key)
		{
			UE_LOG(LogSeinSim, Error,
				TEXT("RestoreSnapshot: unresolved component type %s during staging."),
				*Pair.Key);
			return false;
		}

		FStagedComponentStorage& Staged =
			StagedComponentStorages.AddDefaulted_GetRef();
		Staged.Type = StructType;
		Staged.TypeRoot.Reset(StructType);
		Staged.Storage = MakeUnique<FSeinGenericComponentStorage>(
			StructType, MaxSnapshotEntitySlot);
		TArray<uint8> MutableBytes = Pair.Value.Bytes;
		FMemoryReader MemoryReader(MutableBytes, /*bIsPersistent=*/true);
		FObjectAndNameAsStringProxyArchive Reader(
			MemoryReader, /*bInLoadIfFindFails=*/true);
		const int32 ReadCount =
			Staged.Storage->SerializeFromArchive(Reader);
		if (Reader.IsError()
			|| MemoryReader.Tell() != Pair.Value.Bytes.Num()
			|| ReadCount != Pair.Value.EntryCount)
		{
			UE_LOG(LogSeinSim, Error,
				TEXT("RestoreSnapshot: component storage %s failed staging."),
				*Pair.Key);
			return false;
		}
	}

	TMap<FSeinFactionID, TObjectPtr<USeinFaction>> StagedFactions;
	TArray<TStrongObjectPtr<USeinFaction>> StagedFactionRoots;
	StagedFactions.Reserve(Checkpoint.FactionRegistrations.Num());
	StagedFactionRoots.Reserve(Checkpoint.FactionRegistrations.Num());
	FSeinFactionID PreviousFactionID;
	for (const FSeinSnapshotFactionRegistration& Registration :
		Checkpoint.FactionRegistrations)
	{
		if (!Registration.FactionID.IsValid()
			|| (PreviousFactionID.IsValid()
				&& !(PreviousFactionID < Registration.FactionID))
			|| Registration.FactionAssetPath.IsEmpty())
		{
			UE_LOG(LogSeinSim, Error,
				TEXT("RestoreSnapshot: invalid canonical faction registry envelope."));
			return false;
		}
		PreviousFactionID = Registration.FactionID;

		USeinFaction* Faction = LoadObject<USeinFaction>(
			nullptr, *Registration.FactionAssetPath);
		if (!Faction || Faction->GetPathName() != Registration.FactionAssetPath
			|| Faction->FactionID != Registration.FactionID)
		{
			UE_LOG(LogSeinSim, Error,
				TEXT("RestoreSnapshot: faction %s failed exact identity staging."),
				*Registration.FactionAssetPath);
			return false;
		}
		StagedFactionRoots.Emplace(Faction);
		StagedFactions.Add(Registration.FactionID, Faction);
	}
	if (!bFreshBootstrapAdoption)
	{
		bool bExactFactionRegistry = Factions.Num() == StagedFactions.Num();
		for (const auto& Pair : StagedFactions)
		{
			const TObjectPtr<USeinFaction>* Existing = Factions.Find(Pair.Key);
			bExactFactionRegistry = bExactFactionRegistry && Existing
				&& Existing->Get()
				&& Existing->Get()->GetPathName() == Pair.Value->GetPathName();
		}
		if (!bExactFactionRegistry)
		{
			UE_LOG(LogSeinSim, Error,
				TEXT("RestoreSnapshot: checkpoint faction registry does not match this world."));
			return false;
		}
	}

	class FStagedSnapshotCandidateView final
		: public ISeinCanonicalStateCandidateView
	{
	public:
		FStagedSnapshotCandidateView(
			const FSeinEntityPool& InEntityPool,
			const TArray<FStagedComponentStorage>& InComponentStorages,
			const TMap<FSeinEntityHandle, TSubclassOf<ASeinActor>>&
				InEntityActorClasses,
			const TArray<TStrongObjectPtr<USeinAbility>>& InAbilityPool,
			const TArray<TStrongObjectPtr<USeinCommandBrokerResolver>>&
				InResolverPool,
			const FSeinCanonicalStateValueStore& InCanonicalStateValues)
			: EntityPool(InEntityPool)
			, ComponentStorages(InComponentStorages)
			, EntityActorClasses(InEntityActorClasses)
			, AbilityPool(InAbilityPool)
			, ResolverPool(InResolverPool)
			, CanonicalStateValues(InCanonicalStateValues)
		{
		}

		virtual bool IsEntityValid(
			FSeinEntityHandle Handle) const override
		{
			return EntityPool.IsValid(Handle);
		}

		virtual const void* FindComponentRaw(
			FSeinEntityHandle Handle,
			const UScriptStruct* ComponentType) const override
		{
			if (!ComponentType || !EntityPool.IsValid(Handle))
			{
				return nullptr;
			}
			const FStagedComponentStorage* Found =
				ComponentStorages.FindByPredicate(
					[ComponentType](
						const FStagedComponentStorage& Candidate)
					{
						return Candidate.Type == ComponentType;
					});
			const ISeinComponentStorage* Storage = Found && Found->Storage
				? Found->Storage.Get()
				: nullptr;
			return Storage
				? Storage->GetComponentRaw(Handle)
				: nullptr;
		}

		virtual const UClass* FindEntityActorClass(
			FSeinEntityHandle Handle) const override
		{
			if (!EntityPool.IsValid(Handle))
			{
				return nullptr;
			}
			const TSubclassOf<ASeinActor>* Found =
				EntityActorClasses.Find(Handle);
			return Found ? Found->Get() : nullptr;
		}

		virtual const UClass* FindAbilityClass(
			int32 PoolID) const override
		{
			const USeinAbility* Ability = FindAbility(PoolID);
			return Ability ? Ability->GetClass() : nullptr;
		}

		virtual const USeinAbility* FindAbility(
			int32 PoolID) const override
		{
			return AbilityPool.IsValidIndex(PoolID)
				? AbilityPool[PoolID].Get()
				: nullptr;
		}

		virtual const UClass* FindCommandBrokerResolverClass(
			int32 PoolID) const override
		{
			const USeinCommandBrokerResolver* Resolver =
				ResolverPool.IsValidIndex(PoolID)
					? ResolverPool[PoolID].Get()
					: nullptr;
			return Resolver ? Resolver->GetClass() : nullptr;
		}

		virtual bool GetCanonicalStateValue(
			const FSeinCanonicalStateKey& Key,
			FInstancedStruct& OutValue) const override
		{
			return CanonicalStateValues.GetValue(Key, OutValue);
		}

	private:
		const FSeinEntityPool& EntityPool;
		const TArray<FStagedComponentStorage>& ComponentStorages;
		const TMap<FSeinEntityHandle, TSubclassOf<ASeinActor>>&
			EntityActorClasses;
		const TArray<TStrongObjectPtr<USeinAbility>>& AbilityPool;
		const TArray<TStrongObjectPtr<USeinCommandBrokerResolver>>&
			ResolverPool;
		const FSeinCanonicalStateValueStore& CanonicalStateValues;
	};
	const FStagedSnapshotCandidateView StagedCandidateView(
		StagedEntityPool,
		StagedComponentStorages,
		StagedEntityActorClasses,
		StagedAbilityPool,
		StagedResolverPool,
		StagedCanonicalStateValues);
	TArray<FSeinEntityHandle> StagedEntityHandles;
	StagedEntityHandles.Reserve(InSnapshot.Entities.Num());
	for (const FSeinSnapshotEntityRecord& Entity : InSnapshot.Entities)
	{
		StagedEntityHandles.Emplace(Entity.SlotIndex, Entity.Generation);
	}
	FString ContainmentError;
	if (!UE::SeinARTSCoreEntity::ValidateContainmentState(
			StagedEntityHandles,
			[&StagedCandidateView](FSeinEntityHandle Handle)
			{
				return StagedCandidateView.IsEntityValid(Handle);
			},
			[&StagedCandidateView](FSeinEntityHandle Handle)
			{
				return StagedCandidateView
					.FindComponent<FSeinContainmentData>(Handle);
			},
			[&StagedCandidateView](FSeinEntityHandle Handle)
			{
				return StagedCandidateView
					.FindComponent<FSeinContainmentMemberData>(Handle);
			},
			[&StagedCandidateView](FSeinEntityHandle Handle)
			{
				return StagedCandidateView
					.FindComponent<FSeinAttachmentSpec>(Handle);
			},
			ContainmentError))
	{
		UE_LOG(LogSeinSim, Error,
			TEXT("RestoreSnapshot: containment state failed structural preflight (%s)."),
			*ContainmentError);
		return false;
	}

	FSeinCanonicalStateRestorePlan StagedNativeState;
	FSeinCanonicalStateStageContext NativeStageContext;
	NativeStageContext.Tick = InSnapshot.CurrentTick;
	NativeStageContext.Candidate = &StagedCandidateView;
	NativeStageContext.Services = this;
	if (!FSeinCanonicalStateRegistry::TryStageContributorRestore(
		NativeCanonicalStateSchema,
		NativeStageContext,
		InSnapshot.NativeCanonicalStateRecords,
		StagedNativeState,
		StateContractError))
	{
		UE_LOG(LogSeinSim, Error,
			TEXT("RestoreSnapshot: native canonical state failed staging (%s)."),
			*StateContractError);
		return false;
	}

	FSeinLatentActionRestorePlan StagedLatentState;
	if (!FSeinLatentActionCodecRegistry::StageRecords(
		LatentActionCodecManifest,
		StagedCandidateView,
		StagedNativeState,
		*this,
		InSnapshot.CurrentTick,
		InSnapshot.NextLatentActionID,
		InSnapshot.NextAbilityActivationID,
		InSnapshot.LatentActionRecords,
		InSnapshot.LatentActionSequenceDigest,
		StagedLatentState,
		StateContractError))
	{
		UE_LOG(LogSeinSim, Error,
			TEXT("RestoreSnapshot: latent continuation state failed staging (%s)."),
			*StateContractError);
		return false;
	}

	if (bNeedsExecutionTopologyAdoption)
	{
		// Recipe/contributor staging may execute project-owned code. Rebuild the
		// inert topology after those callbacks and require exact descriptor plus
		// implementation-pointer identity before the candidate can be published.
		FExecutionTopologyCandidate CurrentExecutionTopology;
		FString CurrentTopologyError;
		if (!TryBuildExecutionTopologyCandidate(
				CurrentExecutionTopology, CurrentTopologyError)
			|| !StagedExecutionTopology.IsEquivalentTo(
				CurrentExecutionTopology))
		{
			if (CurrentTopologyError.IsEmpty())
			{
				CurrentTopologyError =
					TEXT("The registered system set changed during restore staging.");
			}
			UE_LOG(LogSeinSim, Error,
				TEXT("RestoreSnapshot: execution topology changed during staging (%s)."),
				*CurrentTopologyError);
			return false;
		}
		StagedExecutionTopology = MoveTemp(CurrentExecutionTopology);
	}

	{
		// Reserve the scheduler before touching the old authoritative state. A
		// running world already owns a valid handle; a stopped/fresh world gets
		// one now. FTSTicker queues AddTicker without invoking it on this stack.
		const bool bSchedulerWasAlreadyReserved =
			bSimulationSchedulerReserved && TickerHandle.IsValid();
		FString SchedulerError;
		if (!ReserveSimulationScheduler(SchedulerError))
		{
			UE_LOG(LogSeinSim, Error,
				TEXT("RestoreSnapshot: could not reserve the fixed-tick scheduler (%s)."),
				*SchedulerError);
			return false;
		}

		// This is the final fallible check and sits immediately before the first
		// live-world mutation. A frozen token may have disappeared while
		// contributor staging or scheduler reservation ran.
		if (!StagedNativeState.VerifyProviderLeases(StateContractError)
			|| !StagedLatentState.VerifyProviderLeases(
				StateContractError)
			|| !PoolObjectCodecManifest.VerifyProviderLeases(
				StateContractError))
		{
			if (!bSchedulerWasAlreadyReserved)
			{
				ReleaseSimulationScheduler();
			}
			UE_LOG(LogSeinSim, Error,
				TEXT("RestoreSnapshot: canonical, latent, or pool provider lease expired (%s)."),
				*StateContractError);
			return false;
		}
	}
	if (bNeedsExecutionTopologyAdoption)
	{
		// All fallible staging, scheduler reservation, and provider-lease
		// verification is complete. From this point onward restore is a
		// no-fail adoption transaction, so the fresh world may finally publish
		// the exact topology used to validate its state contract.
		AdoptExecutionTopologyCandidate(
			MoveTemp(StagedExecutionTopology));
	}
	bIsRunning = false;
	if (LatentActionManager)
	{
		// The old timeline is discarded, not cancelled. Cancellation callbacks
		// would invent gameplay events absent from the adopted checkpoint.
		LatentActionManager->AbandonAllForSnapshotRestore();
	}
	for (USeinAbility* OldAbility : AbilityPool)
	{
		if (!OldAbility)
		{
			continue;
		}
		if (OldAbility->WorldSubsystem == this)
		{
			OldAbility->WorldSubsystem = nullptr;
		}
		OldAbility->RuntimePoolID = INDEX_NONE;
	}
	{
		// This capability exists only after every fallible structural check above
		// has passed. Keeping it lexically scoped prevents restore delegates or
		// the restarted scheduler from inheriting mutation authority.
		TGuardValue<bool> RestoreMutationCapability(
			bSnapshotRestoreMutationAuthorized, true);

		MatchBootstrapState = ESeinMatchBootstrapState::Consumed;
		MatchBootstrapReceipt = Checkpoint.Receipt;
		MatchBootstrapAuthorizationContextDigest =
			Checkpoint.AuthorizationContextDigest;
		MatchBootstrapFailureReason.Reset();
		bMatchBootstrapClosedBroadcast = true;
		bSimSessionSeedInstalled = true;
		if (bFreshBootstrapAdoption)
		{
			MatchBootstrapNativeContributors.Reset();
			MatchBootstrapValueContributions =
				MoveTemp(CheckpointContributions);
		}

		PendingCommands.Clear();
		for (const FSeinCommand& Command : InSnapshot.PendingCommands)
		{
			PendingCommands.AddCommand(Command);
		}
		// Replay ownership is external session state and capture is refused while
		// it is active. Never retain a primed queue from the abandoned timeline.
		PendingReplayCommands.Clear();
		PendingStandalonePauseControlCommands =
			InSnapshot.PendingStandalonePauseControlCommands;

		CurrentTick = InSnapshot.CurrentTick;
		SimSessionSeed = InSnapshot.SessionSeed;
		NextEffectInstanceID = InSnapshot.NextEffectInstanceID;
		NextAbilityActivationID =
			InSnapshot.NextAbilityActivationID;
		SimRandom.SetState(
			static_cast<uint64>(InSnapshot.PRNGState0),
			static_cast<uint64>(InSnapshot.PRNGState1));

		CurrentMatchSettings = MoveTemp(CanonicalSnapshotSettings);
		MatchSettingsDigest = RestoredMatchSettingsDigest;
		MatchState = static_cast<ESeinMatchState>(InSnapshot.MatchState);
		MatchStartTick = InSnapshot.MatchStartTick;
		StartingStateDeadlineTick = InSnapshot.StartingStateDeadlineTick;
		bSimPaused = InSnapshot.bSimPaused;
		bSimPausedHard = InSnapshot.bSimPausedHard;
		PauseEpoch = InSnapshot.PauseEpoch;
		PauseFrozenTick = InSnapshot.PauseFrozenTick;
		LastAppliedPauseControlSequence =
			InSnapshot.LastAppliedPauseControlSequence;

		PlayerStates = InSnapshot.PlayerStates;
		PairCapabilitySourceRefCounts.Reset();
		for (const FSeinPairCapabilityGrantRecord& Grant :
			InSnapshot.PairCapabilityGrants)
		{
			FSeinPairCapabilitySourceKey Key;
			Key.SourcePlayer = Grant.SourcePlayer;
			Key.TargetPlayer = Grant.TargetPlayer;
			Key.CapabilityTag = Grant.CapabilityTag;
			Key.SourceKindTag = Grant.SourceKindTag;
			Key.SourceInstanceID = Grant.SourceInstanceID;
			PairCapabilitySourceRefCounts.Add(Key, Grant.RefCount);
		}
		RebuildPairCapabilityEffectiveCache();
		// Passive designer values are Core authoritative state. Adopt them
		// before native contributor commit so continuation adapters can read the
		// restored values without a second executable Blueprint restore graph.
		CanonicalStateValues = MoveTemp(StagedCanonicalStateValues);
		FrozenCanonicalStateWorldBindingFrames =
			MoveTemp(StagedWorldBindingFrames);
		OwnerTransitionRevisions.Reset();

		// Timeline-local collision and visual-event state is not authoritative
		// snapshot data. Drop the abandoned timeline's pending render events and
		// deferred mutations, then force both broadphase tiers/overlap diffs to
		// re-derive from restored state. Capturing in-flight applies for exact
		// continuation remains part of STATE-01; they must never leak in from the
		// timeline being replaced.
		VisualEventQueue.Events.Reset();
		PendingDestroy.Reset();
		PendingEffectApplies.Reset();
		CollisionSpatialHash.ClearStatic();
		CollisionSpatialHash.ClearDynamic();
		CollisionSpatialHash.MarkStaticDirty();

		// Staged class resolution makes bridge reconciliation non-fallible after
		// the authoritative commit begins.
		EntityActorClassMap = MoveTemp(StagedEntityActorClasses);
		EntityPool = MoveTemp(StagedEntityPool);
		EntityTagStates = MoveTemp(StagedEntityTagStates);
		EntityTagIndex = MoveTemp(StagedEntityTagIndex);
		NamedEntityRegistry = MoveTemp(StagedNamedEntityRegistry);
		ActiveVotes = MoveTemp(StagedActiveVotes);

		for (auto& ExistingStorage : ComponentStorages)
		{
			delete ExistingStorage.Value;
		}
		ComponentStorages.Reset();
		ComponentStorageSnapshotCache.Reset();
		ComponentStorageSnapshotCacheBytes = 0;
		for (FStagedComponentStorage& Staged : StagedComponentStorages)
		{
			ComponentStorages.Add(Staged.Type, Staged.Storage.Release());
		}

		AbilityPool.Reset();
		AbilityPool.SetNum(StagedAbilityPool.Num());
		for (int32 Index = 0; Index < StagedAbilityPool.Num(); ++Index)
		{
			AbilityPool[Index] = StagedAbilityPool[Index].Get();
			if (AbilityPool[Index])
			{
				AbilityPool[Index]->WorldSubsystem = this;
				AbilityPool[Index]->RuntimePoolID = Index;
			}
		}
		AbilityPoolFreeList = MoveTemp(StagedAbilityFreeList);
		AbilityPoolStateRevisions.SetNumZeroed(AbilityPool.Num());
		for (int32 Index = 0; Index < AbilityPool.Num(); ++Index)
		{
			if (AbilityPool[Index])
			{
				++AbilityPoolMutationRevision;
				if (AbilityPoolMutationRevision == 0)
				{
					++AbilityPoolMutationRevision;
				}
				AbilityPoolStateRevisions[Index] =
					AbilityPoolMutationRevision;
			}
		}
		++AbilityPoolTopologyRevision;

		CommandBrokerResolverPool.Reset();
		CommandBrokerResolverPool.SetNum(StagedResolverPool.Num());
		for (int32 Index = 0; Index < StagedResolverPool.Num(); ++Index)
		{
			CommandBrokerResolverPool[Index] = StagedResolverPool[Index].Get();
		}
		CommandBrokerResolverPoolFreeList =
			MoveTemp(StagedResolverFreeList);
		CommandBrokerResolverPoolStateRevisions.SetNumZeroed(
			CommandBrokerResolverPool.Num());
		for (int32 Index = 0;
			Index < CommandBrokerResolverPool.Num();
			++Index)
		{
			if (CommandBrokerResolverPool[Index])
			{
				++CommandBrokerResolverPoolMutationRevision;
				if (CommandBrokerResolverPoolMutationRevision == 0)
				{
					++CommandBrokerResolverPoolMutationRevision;
				}
				CommandBrokerResolverPoolStateRevisions[Index] =
					CommandBrokerResolverPoolMutationRevision;
			}
		}
		++CommandBrokerResolverPoolTopologyRevision;
		CanonicalStateRootCache.Reset();
		if (bFreshBootstrapAdoption)
		{
			Factions = MoveTemp(StagedFactions);
		}
	}
	{
		// Core's private restore capability ends before any extension-owned
		// executable code runs. Contributor and latent commits are infallible
		// swaps of already-validated staged state; they may mutate their own
		// subsystem/action objects, but cannot borrow authority to invent Core
		// entities, components, players, or commands absent from the checkpoint.
		TGuardValue<bool> ReadOnlyGuard(
			bReadOnlyCallbackInProgress, true);
		TGuardValue<bool> ObserverGuard(
			bObserverCallbackInProgress, true);
		FSeinCanonicalStateCommitContext NativeStateCommitContext{
			*this, CurrentTick
		};
		StagedNativeState.Commit(NativeStateCommitContext);
		StagedLatentState.Commit(
			*this, *LatentActionManager, CurrentTick);
	}
	if (CollisionResolver)
	{
		// Custom resolver code must not inherit the private restore capability.
		CollisionResolver->OnSnapshotRestored();
	}

	// Rebuild extension-owned, non-canonical sim indexes before actor-bridge
	// cull/spawn callbacks can query them. The callback is read-only with
	// respect to authoritative state; custom extensions never receive Core's
	// private restore capability and must not depend on restored actors.
	{
		TGuardValue<bool> ReadOnlyGuard(bReadOnlyCallbackInProgress, true);
		TGuardValue<bool> ObserverGuard(bObserverCallbackInProgress, true);
		OnAuthoritativeStateRestored.Broadcast();
	}

	UE_LOG(LogSeinSim, Log,
		TEXT("RestoreSnapshot: authority=%s  localState=%s  resume=%s  tick=%d  entities=%d  componentStorages=%d  playerStates=%d  abilityPool=%d  latentActions=%d  resolverPool=%d"),
		*ConsumedRestoreAuthorityID.ToString(),
		Options.LocalStatePolicy
				== ESeinSnapshotLocalStateRestorePolicy::RestoreCaptured
			? TEXT("RestoreCaptured")
			: TEXT("PreserveCurrent"),
		Options.ResumePolicy == ESeinSnapshotResumePolicy::ResumeImmediately
			? TEXT("ResumeImmediately")
			: TEXT("RemainStopped"),
		CurrentTick, InSnapshot.Entities.Num(),
		InSnapshot.ComponentStorageBlobs.Num(), PlayerStates.Num(),
		AbilityPool.Num(),
		LatentActionManager->GetActiveActionCount(),
		CommandBrokerResolverPool.Num());

	// Reconcile the actor bridge against the new sim state — cull orphaned
	// actors (entities that no longer exist) + spawn missing actors (entities
	// the snapshot has but the world doesn't). Bridge knows which class to
	// spawn from the EntityActorClassMap entries we rehydrated above.
	if (UWorld* W = GetWorld())
	{
		if (USeinActorBridgeSubsystem* Bridge = W->GetSubsystem<USeinActorBridgeSubsystem>())
		{
			Bridge->ReconcileBridgeAfterRestore();
		}
	}

	// Scheduler registration was reserved before commit, so restart is now an
	// infallible local state transition when requested. Catch-up workflows may
	// instead retain the dormant reservation while installing their command
	// tail; StartSimulation resumes it after outer readiness/root agreement.
	TimeAccumulator = 0.0f;
	bIsRunning =
		Options.ResumePolicy == ESeinSnapshotResumePolicy::ResumeImmediately;

	// Let upstream modules consume their own slots (camera, UI). Fired
	// after the sim is fully live + bridge reconciled, so the restore
	// handlers can read a coherent world.
	if (Options.LocalStatePolicy
		== ESeinSnapshotLocalStateRestorePolicy::RestoreCaptured)
	{
		TGuardValue<bool> ReadOnlyGuard(bReadOnlyCallbackInProgress, true);
		TGuardValue<bool> ObserverGuard(bObserverCallbackInProgress, true);
		OnRestoreSnapshotPostSim.Broadcast(InSnapshot.CameraState);
	}

	return true;
}

void USeinWorldSubsystem::RegisterFactionsFromSettings()
{
	if (MatchBootstrapState != ESeinMatchBootstrapState::Applying
		|| bIsRunning || CurrentTick != 0)
	{
		UE_LOG(LogSeinSim, Error,
			TEXT("RegisterFactionsFromSettings rejected outside stopped tick-zero bootstrap Applying."));
		return;
	}
	if (!RequireStateMutationAuthorization(
			TEXT("RegisterFactionsFromSettings")))
	{
		return;
	}
	const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>();
	if (!Settings) return;

	int32 NumLoaded = 0;
	int32 NumSkipped = 0;
	for (const TSoftObjectPtr<USeinFaction>& SoftRef : Settings->RegisteredFactions)
	{
		if (SoftRef.IsNull())
		{
			++NumSkipped;
			continue;
		}
		// LoadSynchronous because we need the asset before any RegisterPlayer
		// looks it up. Settings-driven enumeration runs once per world init,
		// not per-tick, so the sync load is fine.
		USeinFaction* Faction = SoftRef.LoadSynchronous();
		if (!Faction)
		{
			UE_LOG(LogSeinSim, Warning,
				TEXT("RegisterFactionsFromSettings: failed to load %s — skipping. Asset moved or deleted?"),
				*SoftRef.ToString());
			++NumSkipped;
			continue;
		}
		if (!Faction->FactionID.IsValid())
		{
			UE_LOG(LogSeinSim, Warning,
				TEXT("RegisterFactionsFromSettings: %s has invalid FactionID (=0). Set a non-zero FactionID on the data asset."),
				*Faction->FactionName.ToString());
			++NumSkipped;
			continue;
		}
		RegisterFaction(Faction);
		++NumLoaded;
	}

	UE_LOG(LogSeinSim, Log,
		TEXT("RegisterFactionsFromSettings: registered %d faction(s) from settings (%d skipped)."),
		NumLoaded, NumSkipped);
}

// ==================== Tags ====================
//
// Per-entity tag state lives in EntityTagStates (keyed by handle) — see
// FSeinEntityTagState. Actor-backed entities seed an entry from the bridge's
// BaseTags; abstract entities acquire one lazily on their first tag mutation.
// Tag mutations refcount via the state's GrantTagInternal/UngrantTagInternal;
// this method keeps the global EntityTagIndex in sync on 0↔1 edges.

// --- FSeinEntityTagState helpers (formerly on FSeinTagData) ---

void FSeinEntityTagState::RebuildCombinedTags()
{
	CombinedTags.Reset();
	for (const TPair<FGameplayTag, int32>& Pair : TagRefCounts)
	{
		if (Pair.Value > 0)
		{
			CombinedTags.AddTag(Pair.Key);
		}
	}
}

bool FSeinEntityTagState::GrantTagInternal(const FGameplayTag& Tag)
{
	if (!Tag.IsValid()) return false;
	int32& RefCount = TagRefCounts.FindOrAdd(Tag, 0);
	if (RefCount == MAX_int32)
	{
		return false;
	}
	++RefCount;
	if (RefCount == 1)
	{
		CombinedTags.AddTag(Tag);
		return true;
	}
	return false;
}

bool FSeinEntityTagState::UngrantTagInternal(const FGameplayTag& Tag)
{
	if (!Tag.IsValid()) return false;
	int32* RefCount = TagRefCounts.Find(Tag);
	if (!RefCount || *RefCount <= 0) return false;

	--(*RefCount);
	if (*RefCount == 0)
	{
		TagRefCounts.Remove(Tag);
		CombinedTags.RemoveTag(Tag);
		return true;
	}
	return false;
}

bool USeinWorldSubsystem::HasTag(FSeinEntityHandle Handle, FGameplayTag Tag) const
{
	const FSeinEntityTagState* TagState = EntityTagStates.Find(Handle);
	return TagState ? TagState->HasTag(Tag) : false;
}

bool USeinWorldSubsystem::HasAnyTag(FSeinEntityHandle Handle, const FGameplayTagContainer& Tags) const
{
	if (Tags.IsEmpty()) return false;
	const FSeinEntityTagState* TagState = EntityTagStates.Find(Handle);
	return TagState ? TagState->HasAnyTag(Tags) : false;
}

bool USeinWorldSubsystem::HasAllTags(FSeinEntityHandle Handle, const FGameplayTagContainer& Tags) const
{
	if (Tags.IsEmpty()) return true; // vacuously true — no tags required
	const FSeinEntityTagState* TagState = EntityTagStates.Find(Handle);
	return TagState ? TagState->HasAllTags(Tags) : false;
}

const FGameplayTagContainer& USeinWorldSubsystem::GetEntityTags(FSeinEntityHandle Handle) const
{
	static const FGameplayTagContainer Empty;
	const FSeinEntityTagState* TagState = EntityTagStates.Find(Handle);
	return TagState ? TagState->CombinedTags : Empty;
}

FSeinNavAgentProfile USeinWorldSubsystem::BuildNavAgentProfile(
	FSeinEntityHandle Handle) const
{
	const FSeinExtentsComponent* Extents =
		GetComponent<FSeinExtentsComponent>(Handle);
	const FSeinNavigationComponent* Navigation =
		GetComponent<FSeinNavigationComponent>(Handle);
	FFixedPoint Radius = Extents
		? SeinExtentsHelpers::GetColliderBoundingRadius(*Extents)
		: FFixedPoint::Zero;
	if (Radius <= FFixedPoint::Zero && Navigation)
	{
		Radius = Navigation->FallbackFootprintRadius;
	}
	return BuildNavAgentProfile(Handle, Radius);
}

FSeinNavAgentProfile USeinWorldSubsystem::BuildNavAgentProfile(
	FSeinEntityHandle Handle,
	FFixedPoint ResolvedFootprintRadius) const
{
	FSeinNavAgentProfile Profile;
	Profile.Requester = Handle;
	Profile.AgentTags = GetEntityTags(Handle);
	Profile.AgentFootprintRadius = ResolvedFootprintRadius;
	if (const FSeinNavigationComponent* Navigation =
		GetComponent<FSeinNavigationComponent>(Handle))
	{
		Profile.BlockedTerrainTags =
			Navigation->BlockedTerrainTags;
		Profile.AgentNavLayerMask =
			Navigation->NavLayerMask;
		Profile.AgentWallPaddingCells =
			Navigation->WallPadding;
	}
	return Profile;
}

const FGameplayTagContainer& USeinWorldSubsystem::GetEntityBaseTags(FSeinEntityHandle Handle) const
{
	static const FGameplayTagContainer Empty;
	const FSeinEntityTagState* TagState = EntityTagStates.Find(Handle);
	return TagState ? TagState->BaseTags : Empty;
}

bool USeinWorldSubsystem::CanGrantTag(FSeinEntityHandle Handle, FGameplayTag Tag) const
{
	if (!Tag.IsValid()) return false;
	const FSeinEntityTagState* TagState = EntityTagStates.Find(Handle);
	return !TagState || TagState->TagRefCounts.FindRef(Tag) < MAX_int32;
}

bool USeinWorldSubsystem::GrantTag(FSeinEntityHandle Handle, FGameplayTag Tag)
{
	if (!RequireStateMutationAuthorization(TEXT("GrantTag"))) return false;
	if (!Tag.IsValid()) return false;
	// FindOrAdd — auto-create the entity's tag state if it doesn't exist yet
	// (e.g., transient grants from abilities/effects on entities that didn't
	// author any BaseTags). Refcount handles the rest.
	FSeinEntityTagState& TagState = EntityTagStates.FindOrAdd(Handle);
	if (TagState.TagRefCounts.FindRef(Tag) == MAX_int32)
	{
		UE_LOG(LogSeinSim, Error,
			TEXT("GrantTag: refcount saturated for %s on entity %s"),
			*Tag.ToString(), *Handle.ToString());
		return false;
	}
	MarkCanonicalAuxiliaryStateDirty();

	if (TagState.GrantTagInternal(Tag))
	{
		EntityTagIndex.FindOrAdd(Tag).Add(Handle);
	}
	return true;
}

void USeinWorldSubsystem::UngrantTag(FSeinEntityHandle Handle, FGameplayTag Tag)
{
	if (!RequireStateMutationAuthorization(TEXT("UngrantTag"))) return;
	if (!Tag.IsValid()) return;
	FSeinEntityTagState* TagState = EntityTagStates.Find(Handle);
	if (!TagState) return;
	MarkCanonicalAuxiliaryStateDirty();

	if (TagState->UngrantTagInternal(Tag))
	{
		if (TArray<FSeinEntityHandle>* Bucket = EntityTagIndex.Find(Tag))
		{
			Bucket->RemoveSingle(Handle);
			if (Bucket->Num() == 0)
			{
				EntityTagIndex.Remove(Tag);
			}
		}
	}
}

// --- Player tags (refcounted, mirrors entity tag plumbing above) ---

void USeinWorldSubsystem::GrantPlayerTag(FSeinPlayerID PlayerID, FGameplayTag Tag)
{
	if (!RequireStateMutationAuthorization(TEXT("GrantPlayerTag"))) return;
	if (!Tag.IsValid()) return;
	FSeinPlayerState* State = GetPlayerStateMutable(PlayerID);
	if (!State) return;

	int32& Count = State->PlayerTagRefCounts.FindOrAdd(Tag);
	if (Count == MAX_int32)
	{
		UE_LOG(LogSeinSim, Error,
			TEXT("GrantPlayerTag: refcount saturated for %s on player %s"),
			*Tag.ToString(), *PlayerID.ToString());
		return;
	}
	const int32 Old = Count;
	++Count;
	if (Old == 0)
	{
		State->PlayerTags.AddTag(Tag);
	}
}

void USeinWorldSubsystem::UngrantPlayerTag(FSeinPlayerID PlayerID, FGameplayTag Tag)
{
	if (!RequireStateMutationAuthorization(TEXT("UngrantPlayerTag"))) return;
	if (!Tag.IsValid()) return;
	FSeinPlayerState* State = GetPlayerStateMutable(PlayerID);
	if (!State) return;

	int32* Count = State->PlayerTagRefCounts.Find(Tag);
	if (!Count || *Count <= 0) return;

	--(*Count);
	if (*Count == 0)
	{
		State->PlayerTagRefCounts.Remove(Tag);
		State->PlayerTags.RemoveTag(Tag);
	}
}

bool USeinWorldSubsystem::AddBaseTag(FSeinEntityHandle Handle, FGameplayTag Tag)
{
	if (!RequireStateMutationAuthorization(TEXT("AddBaseTag"))) return false;
	if (!Tag.IsValid()) return false;
	FSeinEntityTagState& TagState = EntityTagStates.FindOrAdd(Handle);
	if (TagState.BaseTags.HasTagExact(Tag)) return false;

	if (!GrantTag(Handle, Tag)) return false;
	TagState.BaseTags.AddTag(Tag);
	return true;
}

bool USeinWorldSubsystem::RemoveBaseTag(FSeinEntityHandle Handle, FGameplayTag Tag)
{
	if (!RequireStateMutationAuthorization(TEXT("RemoveBaseTag"))) return false;
	if (!Tag.IsValid()) return false;
	FSeinEntityTagState* TagState = EntityTagStates.Find(Handle);
	if (!TagState) return false;
	if (!TagState->BaseTags.HasTagExact(Tag)) return false;

	TagState->BaseTags.RemoveTag(Tag);
	UngrantTag(Handle, Tag);
	return true;
}

void USeinWorldSubsystem::ReplaceBaseTags(FSeinEntityHandle Handle, const FGameplayTagContainer& NewBaseTags)
{
	if (!RequireStateMutationAuthorization(TEXT("ReplaceBaseTags"))) return;
	FSeinEntityTagState& TagState = EntityTagStates.FindOrAdd(Handle);

	// Diff old vs new. Touch refcounts only for tags that actually changed
	// membership in BaseTags — tags that persist keep their existing refcount.
	FGameplayTagContainer ToUngrant;
	for (const FGameplayTag& Existing : TagState.BaseTags)
	{
		if (!NewBaseTags.HasTagExact(Existing))
		{
			ToUngrant.AddTag(Existing);
		}
	}
	FGameplayTagContainer ToGrant;
	for (const FGameplayTag& Incoming : NewBaseTags)
	{
		if (!TagState.BaseTags.HasTagExact(Incoming))
		{
			ToGrant.AddTag(Incoming);
		}
	}
	for (const FGameplayTag& Tag : ToGrant)
	{
		if (!CanGrantTag(Handle, Tag))
		{
			UE_LOG(LogSeinSim, Error,
				TEXT("ReplaceBaseTags: refcount saturated for %s on entity %s; replacement rejected"),
				*Tag.ToString(), *Handle.ToString());
			return;
		}
	}

	TagState.BaseTags = NewBaseTags;
	for (const FGameplayTag& Tag : ToUngrant) UngrantTag(Handle, Tag);
	for (const FGameplayTag& Tag : ToGrant)   GrantTag(Handle, Tag);
}

TArray<FSeinEntityHandle> USeinWorldSubsystem::GetEntitiesWithTag(FGameplayTag Tag) const
{
	if (const TArray<FSeinEntityHandle>* Bucket = EntityTagIndex.Find(Tag))
	{
		return *Bucket;
	}
	return {};
}

const TArray<FSeinEntityHandle>* USeinWorldSubsystem::FindEntitiesWithTag(FGameplayTag Tag) const
{
	return EntityTagIndex.Find(Tag);
}

// ==================== Named Entity Registry ====================

void USeinWorldSubsystem::RegisterNamedEntity(FName Name, FSeinEntityHandle Handle)
{
	if (!RequireStateMutationAuthorization(TEXT("RegisterNamedEntity"))) return;
	if (Name.IsNone()) return;
	if (!EntityPool.IsValid(Handle)) return;
	NamedEntityRegistry.Add(Name, Handle);
	MarkCanonicalAuxiliaryStateDirty();
}

FSeinEntityHandle USeinWorldSubsystem::LookupNamedEntity(FName Name) const
{
	if (Name.IsNone()) return FSeinEntityHandle::Invalid();
	if (const FSeinEntityHandle* Found = NamedEntityRegistry.Find(Name))
	{
		return *Found;
	}
	return FSeinEntityHandle::Invalid();
}

void USeinWorldSubsystem::UnregisterNamedEntity(FName Name)
{
	if (!RequireStateMutationAuthorization(TEXT("UnregisterNamedEntity"))) return;
	if (NamedEntityRegistry.Remove(Name) > 0)
	{
		MarkCanonicalAuxiliaryStateDirty();
	}
}

// ==================== Attribute Resolution ====================

FFixedPoint USeinWorldSubsystem::ResolveAttribute(FSeinEntityHandle Handle, UScriptStruct* ComponentType, FName FieldName)
{
	const ISeinComponentStorage* Storage =
		GetComponentStorageRaw(ComponentType);
	if (!Storage) return FFixedPoint::Zero;

	const void* CompData = Storage->GetComponentRaw(Handle);
	if (!CompData) return FFixedPoint::Zero;

	const FFixedPoint BaseValue = FSeinAttributeResolver::ReadFixedPointField(CompData, ComponentType, FieldName);

	TArray<FSeinModifier> AllModifiers;

	// Instance-scope: walk the entity's active effects; CDO supplies the modifier list.
	if (const FSeinActiveEffectsComponent* EffectsComp = GetComponent<FSeinActiveEffectsComponent>(Handle))
	{
		for (const FSeinActiveEffect& Effect : EffectsComp->ActiveEffects)
		{
			const USeinEffect* Def = Effect.EffectClass ? GetDefault<USeinEffect>(Effect.EffectClass) : nullptr;
			if (!Def) continue;
			for (const FSeinModifier& Mod : Def->Modifiers)
			{
				if (Mod.TargetComponentType != ComponentType) continue;
				if (Mod.TargetFieldName != FieldName) continue;
				// Instance-scope modifiers (the effect's scope drives semantics; modifiers
				// on an Instance-scope effect are Instance by construction — the per-modifier
				// Scope field remains for legacy compatibility).
				for (int32 Stack = 0; Stack < Effect.CurrentStacks; ++Stack)
				{
					FSeinModifier& Added = AllModifiers.Add_GetRef(Mod);
					Added.SourceEntity = Effect.Source;
					Added.SourceEffectID = Effect.EffectInstanceID;
				}
			}
		}
	}

	const FSeinPlayerID OwnerID = GetEntityOwner(Handle);
	if (const FSeinPlayerState* PlayerState = GetPlayerState(OwnerID))
	{
		const FGameplayTagContainer& EntityTags = GetEntityTags(Handle);

		// Class-scope: iterate the player's class effects; CDO modifiers are filtered
		// by TargetClassTag against the entity's tags.
		for (const FSeinActiveEffect& Effect : PlayerState->ClassEffects)
		{
			const USeinEffect* Def = Effect.EffectClass ? GetDefault<USeinEffect>(Effect.EffectClass) : nullptr;
			if (!Def) continue;
			for (const FSeinModifier& Mod : Def->Modifiers)
			{
				if (Mod.TargetComponentType != ComponentType) continue;
				if (Mod.TargetFieldName != FieldName) continue;
				const FGameplayTag ArchTag = Mod.TargetClassTag.IsValid()
					? Mod.TargetClassTag
					: Def->DefaultTargetClassTag;
				if (ArchTag.IsValid() && !EntityTags.HasTag(ArchTag))
				{
					continue;
				}
				for (int32 Stack = 0; Stack < Effect.CurrentStacks; ++Stack)
				{
					FSeinModifier& Added = AllModifiers.Add_GetRef(Mod);
					Added.SourceEntity = Effect.Source;
					Added.SourceEffectID = Effect.EffectInstanceID;
				}
			}
		}

		// Legacy `ArchetypeModifiers` flat list retired in Session 2.4 — tech-granted
		// class-scope modifiers now flow through `ClassEffects` above via the
		// unified effect pipeline.
	}

	if (AllModifiers.Num() == 0)
	{
		return BaseValue;
	}

	return FSeinAttributeResolver::ResolveModifiers(BaseValue, AllModifiers);
}

FFixedPoint USeinWorldSubsystem::ResolvePlayerAttribute(FSeinPlayerID PlayerID, UScriptStruct* StructType, FName FieldName) const
{
	const FSeinPlayerState* State = GetPlayerState(PlayerID);
	if (!State || !StructType)
	{
		return FFixedPoint::Zero;
	}

	// Base value — PlayerState itself is the only player-scope struct we reflect today;
	// future sub-structs (income rates, caps) can be targeted the same way.
	const void* BaseStruct = StructType == FSeinPlayerState::StaticStruct() ? static_cast<const void*>(State) : nullptr;
	FFixedPoint BaseValue = FFixedPoint::Zero;
	if (BaseStruct)
	{
		BaseValue = FSeinAttributeResolver::ReadFixedPointField(const_cast<void*>(BaseStruct), StructType, FieldName);
	}

	TArray<FSeinModifier> AllModifiers;
	for (const FSeinActiveEffect& Effect : State->PlayerEffects)
	{
		const USeinEffect* Def = Effect.EffectClass ? GetDefault<USeinEffect>(Effect.EffectClass) : nullptr;
		if (!Def) continue;
		for (const FSeinModifier& Mod : Def->Modifiers)
		{
			if (Mod.TargetComponentType != StructType) continue;
			if (Mod.TargetFieldName != FieldName) continue;
			for (int32 Stack = 0; Stack < Effect.CurrentStacks; ++Stack)
			{
				FSeinModifier& Added = AllModifiers.Add_GetRef(Mod);
				Added.SourceEntity = Effect.Source;
				Added.SourceEffectID = Effect.EffectInstanceID;
			}
		}
	}

	if (AllModifiers.Num() == 0)
	{
		return BaseValue;
	}
	return FSeinAttributeResolver::ResolveModifiers(BaseValue, AllModifiers);
}

// ==================== Effects ====================

namespace
{
	int32 CountEffectStacksWithTag(const TArray<FSeinActiveEffect>& Effects, FGameplayTag Tag)
	{
		if (!Tag.IsValid())
		{
			return 0;
		}

		int64 Total = 0;
		for (const FSeinActiveEffect& Effect : Effects)
		{
			const USeinEffect* Def = Effect.EffectClass ? GetDefault<USeinEffect>(Effect.EffectClass) : nullptr;
			if (Def && Def->EffectTag.MatchesTag(Tag))
			{
				Total += FMath::Max(0, Effect.CurrentStacks);
				if (Total >= MAX_int32) return MAX_int32;
			}
		}
		return static_cast<int32>(Total);
	}
}

int64 USeinWorldSubsystem::ApplyEffect(FSeinEntityHandle Target, TSubclassOf<USeinEffect> EffectClass, FSeinEntityHandle Source)
{
	SEIN_CHECK_NOT_PARALLEL();
	if (!RequireStateMutationAuthorization(TEXT("ApplyEffect")))
	{
		return 0;
	}

	const FSeinEntity* TargetEntity = EntityPool.Get(Target);
	if (!EffectClass || !TargetEntity || !TargetEntity->IsAlive())
	{
		return 0;
	}

	// Runtime callbacks defer to the next PreTick drain. Authorized bootstrap
	// materialization and validated snapshot restore commit synchronously.
	if (SEIN_IS_SIM_CONTEXT_FOR(this))
	{
		PendingEffectApplies.Add({ Target, EffectClass, Source });
		return 0;
	}
	return ApplyEffectInternal(Target, EffectClass, Source);
}

void USeinWorldSubsystem::ProcessPendingEffectApplies()
{
	if (!RequireStateMutationAuthorization(TEXT("ProcessPendingEffectApplies")))
	{
		return;
	}
	if (PendingEffectApplies.Num() == 0)
	{
		return;
	}
	// Swap-out the current queue so any applies-from-hooks land in a fresh queue
	// for the NEXT PreTick (per DESIGN §8 Q9c apply-batching).
	TArray<FSeinPendingEffectApply> Draining;
	Draining.Reserve(PendingEffectApplies.Num());
	Swap(Draining, PendingEffectApplies);

	for (const FSeinPendingEffectApply& P : Draining)
	{
		ApplyEffectInternal(P.Target, P.EffectClass, P.Source);
	}
}

FSeinActiveEffect* USeinWorldSubsystem::FindActiveEffectByID(int64 EffectInstanceID)
{
	if (EffectInstanceID <= 0)
	{
		return nullptr;
	}

	FSeinActiveEffect* Found = nullptr;
	EntityPool.ForEachEntity([&](FSeinEntityHandle Handle, const FSeinEntity& /*Entity*/)
	{
		if (Found) return;
		if (FSeinActiveEffectsComponent* Effects =
			GetComponentMutable<FSeinActiveEffectsComponent>(
				Handle))
		{
			Found = Effects->ActiveEffects.FindByPredicate([EffectInstanceID](const FSeinActiveEffect& Effect)
			{
				return Effect.EffectInstanceID == EffectInstanceID;
			});
		}
	});
	if (Found)
	{
		return Found;
	}

	for (FSeinPlayerID PlayerID : GetRegisteredPlayerIDs())
	{
		FSeinPlayerState* State = GetPlayerStateMutable(PlayerID);
		if (!State) continue;
		Found = State->ClassEffects.FindByPredicate([EffectInstanceID](const FSeinActiveEffect& Effect)
		{
			return Effect.EffectInstanceID == EffectInstanceID;
		});
		if (!Found)
		{
			Found = State->PlayerEffects.FindByPredicate([EffectInstanceID](const FSeinActiveEffect& Effect)
			{
				return Effect.EffectInstanceID == EffectInstanceID;
			});
		}
		if (Found)
		{
			return Found;
		}
	}
	return nullptr;
}

void USeinWorldSubsystem::PruneEffectAbilityGrantClaim(int64 EffectInstanceID,
	FSeinEntityHandle Recipient, TSubclassOf<USeinAbility> AbilityClass)
{
	if (!RequireStateMutationAuthorization(TEXT("PruneEffectAbilityGrantClaim")))
	{
		return;
	}
	FSeinActiveEffect* Effect = FindActiveEffectByID(EffectInstanceID);
	if (!Effect || !AbilityClass) return;
	const int32 Index = Effect->CommittedAbilityGrants.IndexOfByPredicate(
		[&](const FSeinEffectAbilityGrant& Grant)
		{
			return Grant.Recipient == Recipient && Grant.AbilityClass == AbilityClass;
		});
	if (Index != INDEX_NONE)
	{
		Effect->CommittedAbilityGrants.RemoveAt(Index, 1, EAllowShrinking::No);
	}
}

void USeinWorldSubsystem::PruneAllEffectAbilityGrantClaims(
	FSeinEntityHandle Recipient, TSubclassOf<USeinAbility> AbilityClass)
{
	if (!RequireStateMutationAuthorization(TEXT("PruneAllEffectAbilityGrantClaims")))
	{
		return;
	}
	if (!AbilityClass) return;
	auto Prune = [&](TArray<FSeinActiveEffect>& Effects)
	{
		for (FSeinActiveEffect& Effect : Effects)
		{
			Effect.CommittedAbilityGrants.RemoveAll(
				[&](const FSeinEffectAbilityGrant& Grant)
				{
					return Grant.Recipient == Recipient
						&& Grant.AbilityClass == AbilityClass;
				});
		}
	};
	EntityPool.ForEachEntity([&](FSeinEntityHandle Handle, const FSeinEntity& /*Entity*/)
	{
		if (FSeinActiveEffectsComponent* Effects =
			GetComponentMutable<FSeinActiveEffectsComponent>(
				Handle))
		{
			Prune(Effects->ActiveEffects);
		}
	});
	for (FSeinPlayerID PlayerID : GetRegisteredPlayerIDs())
	{
		if (FSeinPlayerState* State = GetPlayerStateMutable(PlayerID))
		{
			Prune(State->ClassEffects);
			Prune(State->PlayerEffects);
		}
	}
}

FSeinActiveEffect* USeinWorldSubsystem::ResolveEffect(const FEffectLocator& Locator)
{
	TArray<FSeinActiveEffect>* Storage = nullptr;
	if (Locator.Scope == ESeinModifierScope::Instance)
	{
		FSeinActiveEffectsComponent* Effects =
			GetComponentMutable<FSeinActiveEffectsComponent>(
				Locator.InstanceTarget);
		const bool bKnownPendingTombstone =
			EntityPool.IsDeferredDestroyTombstone(Locator.InstanceTarget)
			&& (Locator.InstanceTarget == DeferredTeardownHandle
				|| PendingDestroy.Contains(Locator.InstanceTarget));
		if (!Effects && bKnownPendingTombstone)
		{
			if (ISeinComponentStorage* RawStorage =
				GetComponentStorageMutable(
					FSeinActiveEffectsComponent::StaticStruct()))
			{
				Effects = static_cast<FSeinActiveEffectsComponent*>(
					RawStorage->GetComponentRaw(Locator.InstanceTarget));
			}
		}
		if (Effects)
		{
			Storage = &Effects->ActiveEffects;
		}
	}
	else if (FSeinPlayerState* State = GetPlayerStateMutable(Locator.PlayerID))
	{
		Storage = Locator.Scope == ESeinModifierScope::Class
			? &State->ClassEffects
			: &State->PlayerEffects;
	}
	return Storage ? Storage->FindByPredicate([&](const FSeinActiveEffect& Effect)
	{
		return Effect.EffectInstanceID == Locator.EffectInstanceID;
	}) : nullptr;
}

bool USeinWorldSubsystem::IsEffectGrantRecipientEligible(
	const FEffectLocator& Locator, FSeinEntityHandle Recipient)
{
	const FSeinEntity* Entity = EntityPool.Get(Recipient);
	FSeinActiveEffect* Active = ResolveEffect(Locator);
	if (!Active || !Entity || !Entity->IsAlive()
		|| !GetComponent<FSeinAbilityComponent>(Recipient))
	{
		return false;
	}
	if (Locator.Scope == ESeinModifierScope::Instance)
	{
		return Recipient == Locator.InstanceTarget && Active->Target == Locator.InstanceTarget;
	}

	const USeinEffect* CDO = Active->EffectClass
		? GetDefault<USeinEffect>(Active->EffectClass)
		: nullptr;
	return CDO && GetEntityOwner(Recipient) == Locator.PlayerID
		&& CDO->AbilityTargetClassTag.IsValid()
		&& HasTag(Recipient, CDO->AbilityTargetClassTag);
}

bool USeinWorldSubsystem::GrantAbilityTrackedByEffect(const FEffectLocator& Locator,
	FSeinEntityHandle Recipient, TSubclassOf<USeinAbility> AbilityClass)
{
	FSeinActiveEffect* Effect = ResolveEffect(Locator);
	if (!Effect || !AbilityClass)
	{
		return Effect != nullptr;
	}
	if (!IsEffectGrantRecipientEligible(Locator, Recipient))
	{
		return true; // Effect survives; this sampled recipient no longer qualifies.
	}

	// Reserve ownership before the grant: a new passive can execute arbitrary
	// Blueprint and remove this effect before SeinGrantAbility returns. Teardown
	// then sees the reservation and balances the just-committed ability ref.
	FSeinEffectAbilityGrant Grant;
	Grant.Recipient = Recipient;
	Grant.AbilityClass = AbilityClass;
	Effect->CommittedAbilityGrants.Add(Grant);

	if (USeinAbilityBPFL::SeinGrantAbilityFromEffect(this, Recipient,
		AbilityClass, Locator.EffectInstanceID) == INDEX_NONE)
	{
		// Reconcile by multiplicity, not by blindly removing the last matching
		// ledger entry. A passive may transfer B→C→B and re-grant the same
		// effect/class before this older activation returns; in that case the live
		// matching entry belongs to the replacement source row, not this detached
		// reservation.
		if (FSeinActiveEffect* StillActive = ResolveEffect(Locator))
		{
			int32 LedgerClaims = 0;
			for (const FSeinEffectAbilityGrant& ExistingGrant
				: StillActive->CommittedAbilityGrants)
			{
				if (ExistingGrant.Recipient == Recipient
					&& ExistingGrant.AbilityClass == AbilityClass)
				{
					++LedgerClaims;
				}
			}

			int32 SourceClaims = 0;
			if (const FSeinAbilityComponent* AbilityComp =
				GetComponent<FSeinAbilityComponent>(Recipient))
			{
				for (int32 Index = 0; Index < AbilityComp->AbilityInstanceIDs.Num(); ++Index)
				{
					const USeinAbility* Ability =
						GetAbilityInstance(AbilityComp->AbilityInstanceIDs[Index]);
					if (!Ability || Ability->GetClass() != AbilityClass.Get()
						|| !AbilityComp->AbilityGrantOwnership.IsValidIndex(Index))
					{
						continue;
					}
					for (int64 SourceID
						: AbilityComp->AbilityGrantOwnership[Index].EffectInstanceIDs)
					{
						SourceClaims += SourceID == Locator.EffectInstanceID ? 1 : 0;
					}
				}
			}

			for (int32 Index = StillActive->CommittedAbilityGrants.Num() - 1;
				LedgerClaims > SourceClaims && Index >= 0; --Index)
			{
				const FSeinEffectAbilityGrant& Reserved =
					StillActive->CommittedAbilityGrants[Index];
				if (Reserved.Recipient == Recipient && Reserved.AbilityClass == AbilityClass)
				{
					StillActive->CommittedAbilityGrants.RemoveAt(
						Index, 1, EAllowShrinking::No);
					break;
				}
			}
		}
	}
	return ResolveEffect(Locator) != nullptr;
}

USeinWorldSubsystem::FEffectApplyResult USeinWorldSubsystem::ApplyEffectTransactional(
	FSeinEntityHandle Target, TSubclassOf<USeinEffect> EffectClass,
	FSeinEntityHandle Source)
{
	SEIN_CHECK_NOT_PARALLEL();

	const auto Reject = []
	{
		return FEffectApplyResult{EEffectApplyStatus::RejectedNoMutation, 0};
	};
	if (!RequireStateMutationAuthorization(TEXT("ApplyEffectTransactional")))
	{
		return Reject();
	}
	const auto Invalidated = [Target, EffectClass]
	{
		UE_LOG(LogSeinSim, Error,
			TEXT("ApplyEffect: replacement callbacks invalidated target %s before %s could commit; prior removals are irreversible."),
			*Target.ToString(), *EffectClass->GetPathName());
		return FEffectApplyResult{
			EEffectApplyStatus::InvalidatedAfterReplacementRemoval, 0};
	};

	const FSeinEntity* TargetEntity = EntityPool.Get(Target);
	if (!EffectClass || !TargetEntity || !TargetEntity->IsAlive())
	{
		return Reject();
	}
	if (EffectClass->HasAnyClassFlags(
		CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
	{
		UE_LOG(LogSeinSim, Error,
			TEXT("ApplyEffect: effect class %s is abstract, deprecated, or superseded."),
			*EffectClass->GetPathName());
		return Reject();
	}
	const USeinEffect* CDO = GetDefault<USeinEffect>(EffectClass);
	if (!CDO)
	{
		return Reject();
	}
	struct FApplyDefinition
	{
		explicit FApplyDefinition(const USeinEffect& Effect)
			: Scope(Effect.Scope)
			, DurationMode(Effect.DurationMode)
			, Duration(Effect.Duration)
			, StackingRule(Effect.StackingRule)
			, MaxStacks(Effect.MaxStacks)
			, EffectTag(Effect.EffectTag)
			, GrantedTags(Effect.GrantedTags)
			, RemoveEffectsWithTag(Effect.RemoveEffectsWithTag)
			, GrantedAbilities(Effect.GrantedAbilities)
			, AbilityTargetClassTag(Effect.AbilityTargetClassTag)
		{
		}

		ESeinModifierScope Scope;
		ESeinEffectDurationMode DurationMode;
		FFixedPoint Duration;
		ESeinEffectStackingRule StackingRule;
		int32 MaxStacks = 1;
		FGameplayTag EffectTag;
		FGameplayTagContainer GrantedTags;
		FGameplayTagContainer RemoveEffectsWithTag;
		TArray<TSubclassOf<USeinAbility>> GrantedAbilities;
		FGameplayTag AbilityTargetClassTag;
	};
	const FApplyDefinition Definition(*CDO);

	// Effect objects are stateless class-default strategies. Snapshot every
	// persistent reflected field, including fields introduced by native or
	// Blueprint subclasses, so a victim callback cannot retarget the replacement's
	// later OnTick/modifier behavior between removal and commit. Transient VM/cache
	// fields are deliberately outside the authored definition contract.
	const EPropertyFlags IgnoredDefinitionFlags =
		CPF_Transient | CPF_DuplicateTransient | CPF_NonPIEDuplicateTransient
		| CPF_Deprecated | CPF_SkipSerialization | CPF_EditorOnly;
	FStructOnScope FrozenDefinition(EffectClass.Get());
	if (!FrozenDefinition.IsValid())
	{
		UE_LOG(LogSeinSim, Error,
			TEXT("ApplyEffect: could not snapshot effect definition %s."),
			*EffectClass->GetPathName());
		return Reject();
	}
	for (TFieldIterator<FProperty> It(
		EffectClass.Get(), EFieldIterationFlags::IncludeSuper); It; ++It)
	{
		if (!It->HasAnyPropertyFlags(IgnoredDefinitionFlags))
		{
			It->CopyCompleteValue_InContainer(
				FrozenDefinition.GetStructMemory(), CDO);
		}
	}

	const FSeinPlayerID OwnerID = GetEntityOwner(Target);
	FSeinActiveEffectsComponent* InstanceComp =
		GetComponentMutable<FSeinActiveEffectsComponent>(Target);
	FSeinPlayerState* OwnerState = GetPlayerStateMutable(OwnerID);
	auto ResolveApplyStorage = [&]() -> TArray<FSeinActiveEffect>*
	{
		switch (Definition.Scope)
		{
			case ESeinModifierScope::Instance:
				return InstanceComp ? &InstanceComp->ActiveEffects : nullptr;
			case ESeinModifierScope::Class:
				return OwnerState ? &OwnerState->ClassEffects : nullptr;
			case ESeinModifierScope::Player:
				return OwnerState ? &OwnerState->PlayerEffects : nullptr;
			default:
				return nullptr;
		}
	};
	TArray<FSeinActiveEffect>* Storage = ResolveApplyStorage();
	if (!Storage)
	{
		UE_LOG(LogSeinSim, Error,
			TEXT("ApplyEffect: target %s has no storage for scope %d."),
			*Target.ToString(), static_cast<int32>(Definition.Scope));
		return Reject();
	}

	// Freeze the exact effects that existed when replacement began. Callbacks
	// may add matching effects, but those did not exist at the apply boundary and
	// are not retroactively folded into this transaction.
	struct FPlannedRemoval
	{
		ESeinModifierScope Scope = ESeinModifierScope::Instance;
		FSeinEntityHandle InstanceTarget;
		FSeinPlayerID PlayerID;
		int64 EffectInstanceID = 0;
		TSubclassOf<USeinEffect> EffectClass;
		FGameplayTag EffectTag;
		FGameplayTagContainer GrantedTags;
	};
	TArray<FPlannedRemoval> PlannedRemovals;
	TSet<int64> PlannedRemovalIDs;
	auto AppendMatching = [&](const TArray<FSeinActiveEffect>& Effects,
		ESeinModifierScope Scope, FGameplayTag RemovalTag)
	{
		for (const FSeinActiveEffect& Effect : Effects)
		{
			const USeinEffect* Def = Effect.EffectClass
				? GetDefault<USeinEffect>(Effect.EffectClass)
				: nullptr;
			if (!Def || !Def->EffectTag.MatchesTag(RemovalTag)
				|| PlannedRemovalIDs.Contains(Effect.EffectInstanceID))
			{
				continue;
			}
			PlannedRemovalIDs.Add(Effect.EffectInstanceID);
			PlannedRemovals.Add({Scope, Target, OwnerID,
				Effect.EffectInstanceID, Effect.EffectClass,
				Def->EffectTag, Def->GrantedTags});
		}
	};
	for (const FGameplayTag& RemovalTag : Definition.RemoveEffectsWithTag)
	{
		if (InstanceComp)
		{
			AppendMatching(InstanceComp->ActiveEffects,
				ESeinModifierScope::Instance, RemovalTag);
		}
		if (OwnerState)
		{
			AppendMatching(OwnerState->ClassEffects,
				ESeinModifierScope::Class, RemovalTag);
			AppendMatching(OwnerState->PlayerEffects,
				ESeinModifierScope::Player, RemovalTag);
		}
	}

	const int32 EffectiveMaxStacks = FMath::Max(1, Definition.MaxStacks);
	const TArray<TSubclassOf<USeinAbility>>& AbilityClasses =
		Definition.GrantedAbilities;
	auto ValidateProjection = [&](const TArray<FSeinActiveEffect>& CandidateStorage,
		const FSeinPlayerState* CurrentOwnerState, bool bProjectRemovals,
		bool& bOutNeedsNewInstance) -> bool
	{
		const auto IsProjectedOut = [&](int64 EffectID)
		{
			return bProjectRemovals && PlannedRemovalIDs.Contains(EffectID);
		};

		const FSeinActiveEffect* Existing = nullptr;
		if (Definition.StackingRule != ESeinEffectStackingRule::Independent)
		{
			Existing = CandidateStorage.FindByPredicate(
				[&](const FSeinActiveEffect& Effect)
				{
					return !IsProjectedOut(Effect.EffectInstanceID)
						&& Effect.EffectClass == EffectClass;
				});
		}
		bOutNeedsNewInstance = Existing == nullptr;
		if (!bOutNeedsNewInstance)
		{
			return true;
		}

		if (Definition.StackingRule == ESeinEffectStackingRule::Independent)
		{
			int32 Count = 0;
			for (const FSeinActiveEffect& Effect : CandidateStorage)
			{
				if (!IsProjectedOut(Effect.EffectInstanceID)
					&& Effect.EffectClass == EffectClass)
				{
					++Count;
				}
			}
			if (Count >= EffectiveMaxStacks)
			{
				return false;
			}
		}

		if (NextEffectInstanceID <= 0 || NextEffectInstanceID == MAX_int64)
		{
			UE_LOG(LogSeinSim, Error,
				TEXT("Effect ID space exhausted; refusing effect apply."));
			return false;
		}

		const bool bAbilityGrantsActive = !AbilityClasses.IsEmpty()
			&& (Definition.Scope == ESeinModifierScope::Instance
				|| Definition.AbilityTargetClassTag.IsValid());
		if (bAbilityGrantsActive)
		{
			for (const TSubclassOf<USeinAbility>& AbilityClass : AbilityClasses)
			{
				UClass* Class = AbilityClass.Get();
				if (!Class || Class->HasAnyClassFlags(
					CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
				{
					UE_LOG(LogSeinSim, Error,
						TEXT("ApplyEffect: %s contains an unusable GrantedAbilities entry."),
						*EffectClass->GetPathName());
					return false;
				}
			}
		}

		struct FTagIncrement
		{
			FGameplayTag Tag;
			int32 Count = 0;
		};
		TArray<FTagIncrement, TInlineAllocator<8>> TagIncrements;
		auto AddTagIncrement = [&](FGameplayTag Tag)
		{
			if (!Tag.IsValid()) return;
			if (FTagIncrement* ExistingIncrement = TagIncrements.FindByPredicate(
				[Tag](const FTagIncrement& Increment)
				{
					return Increment.Tag == Tag;
				}))
			{
				++ExistingIncrement->Count;
			}
			else
			{
				TagIncrements.Add({Tag, 1});
			}
		};
		if (Definition.Scope != ESeinModifierScope::Instance)
		{
			AddTagIncrement(Definition.EffectTag);
		}
		for (const FGameplayTag& Tag : Definition.GrantedTags)
		{
			AddTagIncrement(Tag);
		}

		for (const FTagIncrement& Increment : TagIncrements)
		{
			int64 ExistingRefCount = 0;
			if (Definition.Scope == ESeinModifierScope::Instance)
			{
				if (const FSeinEntityTagState* TagState = EntityTagStates.Find(Target))
				{
					ExistingRefCount = TagState->TagRefCounts.FindRef(Increment.Tag);
				}
			}
			else if (CurrentOwnerState)
			{
				ExistingRefCount =
					CurrentOwnerState->PlayerTagRefCounts.FindRef(Increment.Tag);
			}

			if (bProjectRemovals)
			{
				for (const FPlannedRemoval& Removal : PlannedRemovals)
				{
					const bool bSameTagStorage =
						(Definition.Scope == ESeinModifierScope::Instance
							&& Removal.Scope == ESeinModifierScope::Instance)
						|| (Definition.Scope != ESeinModifierScope::Instance
							&& Removal.Scope != ESeinModifierScope::Instance);
					if (!bSameTagStorage) continue;
					if (Removal.Scope != ESeinModifierScope::Instance
						&& Removal.EffectTag == Increment.Tag)
					{
						--ExistingRefCount;
					}
					if (Removal.GrantedTags.HasTagExact(Increment.Tag))
					{
						--ExistingRefCount;
					}
				}
			}

			if (ExistingRefCount < 0
				|| ExistingRefCount + Increment.Count > MAX_int32)
			{
				UE_LOG(LogSeinSim, Error,
					TEXT("ApplyEffect: %s tag refcount cannot accept %s on %s."),
					Definition.Scope == ESeinModifierScope::Instance
						? TEXT("entity") : TEXT("player"),
					*Increment.Tag.ToString(), *Target.ToString());
				return false;
			}
		}

		if (!bAbilityGrantsActive)
		{
			return true;
		}

		TArray<FSeinEntityHandle> Recipients;
		if (Definition.Scope == ESeinModifierScope::Instance)
		{
			Recipients.Add(Target);
		}
		else
		{
			EntityPool.ForEachEntity(
				[&](FSeinEntityHandle Other, const FSeinEntity&)
				{
					if (GetEntityOwner(Other) == OwnerID
						&& HasTag(Other, Definition.AbilityTargetClassTag))
					{
						Recipients.Add(Other);
					}
				});
		}
		for (const FSeinEntityHandle Recipient : Recipients)
		{
			const FSeinAbilityComponent* AbilityComp =
				GetComponent<FSeinAbilityComponent>(Recipient);
			if (!AbilityComp) continue;

			TSet<const UClass*> CheckedClasses;
			for (const TSubclassOf<USeinAbility>& AbilityClass : AbilityClasses)
			{
				const UClass* Class = AbilityClass.Get();
				if (CheckedClasses.Contains(Class)) continue;
				CheckedClasses.Add(Class);
				int32 RequestedCount = 0;
				for (const TSubclassOf<USeinAbility>& Candidate : AbilityClasses)
				{
					RequestedCount += Candidate.Get() == Class ? 1 : 0;
				}

				int32 ExistingIndex = INDEX_NONE;
				for (int32 Index = 0;
					Index < AbilityComp->AbilityInstanceIDs.Num(); ++Index)
				{
					const USeinAbility* ExistingAbility =
						GetAbilityInstance(AbilityComp->AbilityInstanceIDs[Index]);
					if (ExistingAbility && ExistingAbility->GetClass() == Class)
					{
						ExistingIndex = Index;
						break;
					}
				}
				if (ExistingIndex == INDEX_NONE) continue;

				int64 ExistingClaims = 0;
				if (AbilityComp->AbilityGrantOwnership.IsValidIndex(ExistingIndex))
				{
					const FSeinAbilityGrantOwnership& Ownership =
						AbilityComp->AbilityGrantOwnership[ExistingIndex];
					ExistingClaims = Ownership.AnonymousGrantCount;
					for (int64 EffectID : Ownership.EffectInstanceIDs)
					{
						if (!bProjectRemovals
							|| !PlannedRemovalIDs.Contains(EffectID))
						{
							++ExistingClaims;
						}
					}
				}
				else
				{
					ExistingClaims = AbilityComp->AbilityGrantCounts.IsValidIndex(ExistingIndex)
						? FMath::Max(1, AbilityComp->AbilityGrantCounts[ExistingIndex])
						: 1;
				}
				if (ExistingClaims < 0
					|| ExistingClaims + RequestedCount > MAX_int32)
				{
					UE_LOG(LogSeinSim, Error,
						TEXT("ApplyEffect: ability grant refcount would overflow for %s on %s."),
						*Class->GetPathName(), *Recipient.ToString());
					return false;
				}
			}
		}
		return true;
	};

	bool bNeedsNewInstance = false;
	if (!ValidateProjection(*Storage, OwnerState,
		/*bProjectRemovals=*/true, bNeedsNewInstance))
	{
		return Reject();
	}

	bool bCommittedReplacementRemoval = false;
	auto ReplacementDefinitionMatches = [&]()
	{
		const USeinEffect* Current = GetDefault<USeinEffect>(EffectClass);
		if (!Current || Current->GetClass() != EffectClass.Get()) return false;
		for (TFieldIterator<FProperty> It(
			EffectClass.Get(), EFieldIterationFlags::IncludeSuper); It; ++It)
		{
			if (It->HasAnyPropertyFlags(IgnoredDefinitionFlags)) continue;
			for (int32 ArrayIndex = 0; ArrayIndex < It->ArrayDim; ++ArrayIndex)
			{
				if (!It->Identical_InContainer(
					Current, FrozenDefinition.GetStructMemory(), ArrayIndex))
				{
					return false;
				}
			}
		}
		return true;
	};
	auto RemovalDefinitionMatches = [](const FPlannedRemoval& Removal)
	{
		const USeinEffect* Current = Removal.EffectClass
			? GetDefault<USeinEffect>(Removal.EffectClass)
			: nullptr;
		return Current
			&& Current->Scope == Removal.Scope
			&& Current->EffectTag == Removal.EffectTag
			&& Current->GrantedTags == Removal.GrantedTags;
	};
	for (const FPlannedRemoval& Removal : PlannedRemovals)
	{
		if (!ReplacementDefinitionMatches()
			|| !RemovalDefinitionMatches(Removal))
		{
			return bCommittedReplacementRemoval ? Invalidated() : Reject();
		}
		const bool bRemoved = Removal.Scope == ESeinModifierScope::Instance
			? RemoveEffect(Removal.InstanceTarget, Removal.EffectInstanceID,
				/*bByExpiration=*/false)
			: RemovePlayerEffect(Removal.PlayerID, Removal.EffectInstanceID,
				/*bByExpiration=*/false);
		bCommittedReplacementRemoval |= bRemoved;
		if (!ReplacementDefinitionMatches())
		{
			return bCommittedReplacementRemoval ? Invalidated() : Reject();
		}
	}

	TargetEntity = EntityPool.Get(Target);
	if (!TargetEntity || !TargetEntity->IsAlive()
		|| GetEntityOwner(Target) != OwnerID)
	{
		return bCommittedReplacementRemoval ? Invalidated() : Reject();
	}
	InstanceComp = GetComponentMutable<FSeinActiveEffectsComponent>(Target);
	OwnerState = GetPlayerStateMutable(OwnerID);
	Storage = ResolveApplyStorage();
	if (!Storage || !ValidateProjection(*Storage, OwnerState,
		/*bProjectRemovals=*/false, bNeedsNewInstance))
	{
		return bCommittedReplacementRemoval ? Invalidated() : Reject();
	}

	FSeinActiveEffect* Existing = nullptr;
	if (!bNeedsNewInstance)
	{
		Existing = Storage->FindByPredicate(
			[&](const FSeinActiveEffect& Effect)
			{
				return Effect.EffectClass == EffectClass;
			});
		if (!Existing)
		{
			return bCommittedReplacementRemoval ? Invalidated() : Reject();
		}
	}

	int64 AssignedID = 0;
	bool bIsNewInstance = false;
	FEffectLocator EffectLocator;
	EffectLocator.Scope = Definition.Scope;
	EffectLocator.InstanceTarget = Target;
	EffectLocator.PlayerID = OwnerID;
	auto IsAssignedEffectActive = [&]() -> bool
	{
		EffectLocator.EffectInstanceID = AssignedID;
		return ResolveEffect(EffectLocator) != nullptr;
	};

	if (Existing && Definition.StackingRule == ESeinEffectStackingRule::Stack)
	{
		const int32 ClampedStacks = FMath::Clamp(
			Existing->CurrentStacks, 1, EffectiveMaxStacks);
		Existing->CurrentStacks = ClampedStacks < EffectiveMaxStacks
			? ClampedStacks + 1
			: EffectiveMaxStacks;
		if (Definition.DurationMode == ESeinEffectDurationMode::Timed)
		{
			Existing->RemainingDuration = Definition.Duration;
		}
		AssignedID = Existing->EffectInstanceID;
	}
	else if (Existing && Definition.StackingRule == ESeinEffectStackingRule::Refresh)
	{
		Existing->CurrentStacks = 1;
		if (Definition.DurationMode == ESeinEffectDurationMode::Timed)
		{
			Existing->RemainingDuration = Definition.Duration;
		}
		AssignedID = Existing->EffectInstanceID;
	}
	else
	{
#if !UE_BUILD_SHIPPING
		const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>();
		const int32 Threshold = Settings ? Settings->EffectCountWarningThreshold : 256;
		const int32 BeforeCount = Storage->Num();
		if (Threshold > 0 && BeforeCount < Threshold && BeforeCount + 1 >= Threshold)
		{
			UE_LOG(LogSeinSim, Warning,
				TEXT("Effect apply count crossing threshold (%d) for target %s — possible runaway effect loop"),
				Threshold, *Target.ToString());
		}
#endif

		FSeinActiveEffect NewEffect;
		NewEffect.EffectClass = EffectClass;
		NewEffect.Source = Source;
		NewEffect.Target = Target;
		NewEffect.CurrentStacks = 1;
		NewEffect.TimeSinceLastPeriodic = FFixedPoint::Zero;
		NewEffect.RemainingDuration =
			Definition.DurationMode == ESeinEffectDurationMode::Timed
			? Definition.Duration : FFixedPoint::Zero;
		NewEffect.EffectInstanceID = NextEffectInstanceID++;
		Storage->Add(NewEffect);
		AssignedID = NewEffect.EffectInstanceID;
		EffectLocator.EffectInstanceID = AssignedID;
		bIsNewInstance = true;

		EnqueueVisualEvent(FSeinVisualEvent::MakeEffectEvent(
			Target, Definition.EffectTag, /*bApplied=*/true));
		if (Definition.Scope == ESeinModifierScope::Instance)
		{
			for (const FGameplayTag& Tag : Definition.GrantedTags)
			{
				GrantTag(Target, Tag);
			}
		}
		else
		{
			if (Definition.EffectTag.IsValid())
			{
				GrantPlayerTag(OwnerID, Definition.EffectTag);
			}
			for (const FGameplayTag& Tag : Definition.GrantedTags)
			{
				GrantPlayerTag(OwnerID, Tag);
			}
		}

		if (!AbilityClasses.IsEmpty())
		{
			if (Definition.Scope == ESeinModifierScope::Instance)
			{
				for (const TSubclassOf<USeinAbility>& AbilityClass : AbilityClasses)
				{
					if (!GrantAbilityTrackedByEffect(
						EffectLocator, Target, AbilityClass))
					{
						return {EEffectApplyStatus::Applied, AssignedID};
					}
				}
			}
			else if (Definition.AbilityTargetClassTag.IsValid())
			{
				TArray<FSeinEntityHandle> Recipients;
				EntityPool.ForEachEntity(
					[&](FSeinEntityHandle Other, const FSeinEntity&)
					{
						if (GetEntityOwner(Other) == OwnerID
							&& HasTag(Other, Definition.AbilityTargetClassTag))
						{
							Recipients.Add(Other);
						}
					});
				for (FSeinEntityHandle Other : Recipients)
				{
					for (const TSubclassOf<USeinAbility>& AbilityClass : AbilityClasses)
					{
						if (!GrantAbilityTrackedByEffect(
							EffectLocator, Other, AbilityClass))
						{
							return {EEffectApplyStatus::Applied, AssignedID};
						}
					}
				}
			}
		}
	}

	if (bIsNewInstance)
	{
		const FSeinEntity* CurrentTarget = EntityPool.Get(Target);
		const bool bTargetStillEligible = CurrentTarget && CurrentTarget->IsAlive()
			&& (Definition.Scope == ESeinModifierScope::Instance
				|| GetEntityOwner(Target) == OwnerID);
		if (!bTargetStillEligible || !IsAssignedEffectActive())
		{
			return {EEffectApplyStatus::Applied, AssignedID};
		}
		const USeinEffect* EffectDefinition = GetDefault<USeinEffect>(EffectClass);
		if (EffectDefinition)
		{
			EffectDefinition->OnApply(Target, Source);
		}
		if (!IsAssignedEffectActive())
		{
			return {EEffectApplyStatus::Applied, AssignedID};
		}
	}

	if (bIsNewInstance && Definition.DurationMode == ESeinEffectDurationMode::Instant)
	{
		if (Definition.Scope == ESeinModifierScope::Instance)
		{
			RemoveEffect(Target, AssignedID, /*bByExpiration=*/true);
		}
		else
		{
			RemovePlayerEffect(OwnerID, AssignedID, /*bByExpiration=*/true);
		}
	}

	return {EEffectApplyStatus::Applied, AssignedID};
}

int64 USeinWorldSubsystem::ApplyEffectInternal(FSeinEntityHandle Target,
	TSubclassOf<USeinEffect> EffectClass, FSeinEntityHandle Source)
{
	return ApplyEffectTransactional(Target, EffectClass, Source).EffectInstanceID;
}

bool USeinWorldSubsystem::RemoveEffectFromStorage(TArray<FSeinActiveEffect>& Storage,
	int64 EffectInstanceID, FSeinPlayerID PlayerForTags, bool bByExpiration)
{
	const int32 EffectIndex = Storage.IndexOfByPredicate([EffectInstanceID](const FSeinActiveEffect& Effect)
	{
		return Effect.EffectInstanceID == EffectInstanceID;
	});
	if (EffectIndex == INDEX_NONE)
	{
		return false;
	}

	// Detach before any ability or Blueprint callback. Re-entrant removal then
	// sees a coherent storage, and stable removal preserves callback/modifier order.
	const FSeinActiveEffect Effect = Storage[EffectIndex];
	Storage.RemoveAt(EffectIndex, 1, EAllowShrinking::No);

	const USeinEffect* Def = Effect.EffectClass
		? GetDefault<USeinEffect>(Effect.EffectClass)
		: nullptr;
	const ESeinModifierScope RemovedScope = Def
		? Def->Scope : ESeinModifierScope::Instance;
	const ESeinEffectDurationMode RemovedDurationMode = Def
		? Def->DurationMode : ESeinEffectDurationMode::Instant;
	const FGameplayTag RemovedEffectTag = Def
		? Def->EffectTag : FGameplayTag();
	const FGameplayTagContainer RemovedGrantedTags = Def
		? Def->GrantedTags : FGameplayTagContainer();
	if (Def && RemovedScope == ESeinModifierScope::Instance)
	{
		for (const FGameplayTag& Tag : RemovedGrantedTags)
		{
			UngrantTag(Effect.Target, Tag);
		}
	}
	else if (Def)
	{
		if (RemovedEffectTag.IsValid())
		{
			UngrantPlayerTag(PlayerForTags, RemovedEffectTag);
		}
		for (const FGameplayTag& Tag : RemovedGrantedTags)
		{
			UngrantPlayerTag(PlayerForTags, Tag);
		}
	}

	// Revoke only references this exact effect instance committed. The ledger is
	// detached with the effect before callbacks, so partial passive self-removal
	// cannot revoke an authored class that this application never reached.
	for (const FSeinEffectAbilityGrant& Grant : Effect.CommittedAbilityGrants)
	{
		if (Grant.AbilityClass)
		{
			USeinAbilityBPFL::SeinRevokeAbilityFromEffect(this, Grant.Recipient,
				Grant.AbilityClass, Effect.EffectInstanceID);
		}
	}

	if (Def)
	{
		if (bByExpiration && RemovedDurationMode == ESeinEffectDurationMode::Timed)
		{
			Def->OnExpire(Effect.Target);
		}
		Def->OnRemoved(Effect.Target, bByExpiration);
	}
	EnqueueVisualEvent(FSeinVisualEvent::MakeEffectEvent(
		Effect.Target, RemovedEffectTag, /*bApplied=*/false));
	return true;
}

bool USeinWorldSubsystem::RemoveEffect(FSeinEntityHandle Target, int64 EffectInstanceID, bool bByExpiration)
{
	if (!RequireStateMutationAuthorization(TEXT("RemoveEffect")))
	{
		return false;
	}
	const bool bLiveTarget = EntityPool.IsValid(Target);
	const bool bKnownPendingTombstone =
		EntityPool.IsDeferredDestroyTombstone(Target)
		&& (Target == DeferredTeardownHandle || PendingDestroy.Contains(Target));
	if (EffectInstanceID <= 0 || (!bLiveTarget && !bKnownPendingTombstone))
	{
		return false;
	}

	const FSeinPlayerID OwnerID = bLiveTarget
		? GetEntityOwner(Target)
		: EntityPool.GetDeferredDestroyOwner(Target);
	FSeinActiveEffectsComponent* InstanceComp =
		GetComponentMutable<FSeinActiveEffectsComponent>(Target);
	if (!InstanceComp && bKnownPendingTombstone)
	{
			if (ISeinComponentStorage* RawStorage =
				GetComponentStorageMutable(
					FSeinActiveEffectsComponent::StaticStruct()))
		{
			InstanceComp = static_cast<FSeinActiveEffectsComponent*>(
				RawStorage->GetComponentRaw(Target));
		}
	}
	if (InstanceComp)
	{
		if (RemoveEffectFromStorage(InstanceComp->ActiveEffects, EffectInstanceID, OwnerID, bByExpiration))
		{
			return true;
		}
	}
	return RemovePlayerEffect(OwnerID, EffectInstanceID, bByExpiration);
}

bool USeinWorldSubsystem::RemoveEffectByID(int64 EffectInstanceID, bool bByExpiration)
{
	if (!RequireStateMutationAuthorization(TEXT("RemoveEffectByID")))
	{
		return false;
	}
	if (EffectInstanceID <= 0)
	{
		return false;
	}

	// Locate first, dispatch teardown second: removal callbacks may grow or
	// destroy the entity pool and must never run inside pool enumeration.
	FSeinEntityHandle InstanceTarget;
	EntityPool.ForEachEntity([&](FSeinEntityHandle Handle, const FSeinEntity& /*Entity*/)
	{
		if (InstanceTarget.IsValid()) return;
		const FSeinActiveEffectsComponent* Effects = GetComponent<FSeinActiveEffectsComponent>(Handle);
		if (Effects && Effects->ActiveEffects.ContainsByPredicate([EffectInstanceID](const FSeinActiveEffect& Effect)
		{
			return Effect.EffectInstanceID == EffectInstanceID;
		}))
		{
			InstanceTarget = Handle;
		}
	});
	if (InstanceTarget.IsValid())
	{
		return RemoveEffect(InstanceTarget, EffectInstanceID, bByExpiration);
	}

	for (FSeinPlayerID PlayerID : GetRegisteredPlayerIDs())
	{
		if (RemovePlayerEffect(PlayerID, EffectInstanceID, bByExpiration))
		{
			return true;
		}
	}
	return false;
}

bool USeinWorldSubsystem::RemovePlayerEffect(FSeinPlayerID PlayerID, int64 EffectInstanceID, bool bByExpiration)
{
	if (!RequireStateMutationAuthorization(TEXT("RemovePlayerEffect")))
	{
		return false;
	}
	if (EffectInstanceID <= 0)
	{
		return false;
	}
	FSeinPlayerState* PlayerState = GetPlayerStateMutable(PlayerID);
	if (!PlayerState)
	{
		return false;
	}
	if (RemoveEffectFromStorage(PlayerState->ClassEffects, EffectInstanceID, PlayerID, bByExpiration))
	{
		return true;
	}
	return RemoveEffectFromStorage(PlayerState->PlayerEffects, EffectInstanceID, PlayerID, bByExpiration);
}

bool USeinWorldSubsystem::HasInstanceEffectWithTag(FSeinEntityHandle Target, FGameplayTag Tag) const
{
	const FSeinActiveEffectsComponent* Effects = GetComponent<FSeinActiveEffectsComponent>(Target);
	return Effects && Effects->HasEffectWithTag(Tag);
}

int32 USeinWorldSubsystem::GetInstanceEffectStacks(FSeinEntityHandle Target, FGameplayTag Tag) const
{
	const FSeinActiveEffectsComponent* Effects = GetComponent<FSeinActiveEffectsComponent>(Target);
	return Effects ? Effects->GetStackCountForTag(Tag) : 0;
}

int32 USeinWorldSubsystem::GetEffectStacksForPlayer(
	FSeinPlayerID PlayerID, ESeinModifierScope Scope, FGameplayTag Tag) const
{
	const FSeinPlayerState* State = GetPlayerState(PlayerID);
	if (!State)
	{
		return 0;
	}

	if (Scope == ESeinModifierScope::Class)
	{
		return CountEffectStacksWithTag(State->ClassEffects, Tag);
	}
	if (Scope == ESeinModifierScope::Player)
	{
		return CountEffectStacksWithTag(State->PlayerEffects, Tag);
	}
	return 0;
}

bool USeinWorldSubsystem::HasEffectWithTagForPlayer(
	FSeinPlayerID PlayerID, ESeinModifierScope Scope, FGameplayTag Tag) const
{
	return GetEffectStacksForPlayer(PlayerID, Scope, Tag) > 0;
}

void USeinWorldSubsystem::RemoveInstanceEffectsWithTag(FSeinEntityHandle Target, FGameplayTag Tag)
{
	if (!RequireStateMutationAuthorization(TEXT("RemoveInstanceEffectsWithTag")))
	{
		return;
	}
	if (!Tag.IsValid()) return;

	auto CollectMatching = [&](const TArray<FSeinActiveEffect>& Storage, TArray<int64>& OutEffectIDs)
	{
		for (const FSeinActiveEffect& Effect : Storage)
		{
			const USeinEffect* Def = Effect.EffectClass ? GetDefault<USeinEffect>(Effect.EffectClass) : nullptr;
			if (Def && Def->EffectTag.MatchesTag(Tag))
			{
				OutEffectIDs.Add(Effect.EffectInstanceID);
			}
		}
	};

	const FSeinPlayerID OwnerID = GetEntityOwner(Target);
	TArray<int64> InstanceEffectIDs;
	TArray<int64> PlayerEffectIDs;
	if (const FSeinActiveEffectsComponent* InstanceComp = GetComponent<FSeinActiveEffectsComponent>(Target))
	{
		CollectMatching(InstanceComp->ActiveEffects, InstanceEffectIDs);
	}
	if (const FSeinPlayerState* OwnerState = GetPlayerState(OwnerID))
	{
		CollectMatching(OwnerState->ClassEffects, PlayerEffectIDs);
		CollectMatching(OwnerState->PlayerEffects, PlayerEffectIDs);
	}

	for (int64 EffectID : InstanceEffectIDs)
	{
		RemoveEffect(Target, EffectID, /*bByExpiration=*/false);
	}
	for (int64 EffectID : PlayerEffectIDs)
	{
		RemovePlayerEffect(OwnerID, EffectID, /*bByExpiration=*/false);
	}
}

void USeinWorldSubsystem::RemoveEffectsFromDeadSource(FSeinEntityHandle DeadHandle)
{
	if (!RequireStateMutationAuthorization(TEXT("RemoveEffectsFromDeadSource")))
	{
		return;
	}
	if (!DeadHandle.IsValid()) return;

	auto WantsSourceDeathRemoval = [](const TSubclassOf<USeinEffect>& Class) -> bool
	{
		const USeinEffect* Def = Class ? GetDefault<USeinEffect>(Class) : nullptr;
		return Def && Def->bRemoveOnSourceDeath;
	};

	struct FPendingEffectRemoval
	{
		FSeinEntityHandle Target;
		FSeinPlayerID PlayerID;
		int64 EffectID = 0;
		bool bPlayerScoped = false;
	};
	TArray<FPendingEffectRemoval> ToRemove;

	// Instance scope: every entity's FSeinActiveEffectsComponent.
	EntityPool.ForEachEntity([&](FSeinEntityHandle Handle, FSeinEntity& /*Entity*/)
	{
		const FSeinActiveEffectsComponent* EffectsComp = GetComponent<FSeinActiveEffectsComponent>(Handle);
		if (!EffectsComp) return;
		for (const FSeinActiveEffect& E : EffectsComp->ActiveEffects)
		{
			if (E.Source == DeadHandle && WantsSourceDeathRemoval(E.EffectClass))
			{
				ToRemove.Add({ Handle, FSeinPlayerID::Neutral(), E.EffectInstanceID, false });
			}
		}
	});

	// Class / Player scope: canonical player order, with player identity retained
	// so removal still works if the effect's original target is stale.
	for (FSeinPlayerID PlayerID : GetRegisteredPlayerIDs())
	{
		const FSeinPlayerState* State = GetPlayerState(PlayerID);
		if (!State) continue;
		for (const FSeinActiveEffect& E : State->ClassEffects)
		{
			if (E.Source == DeadHandle && WantsSourceDeathRemoval(E.EffectClass))
			{
				ToRemove.Add({ E.Target, PlayerID, E.EffectInstanceID, true });
			}
		}
		for (const FSeinActiveEffect& E : State->PlayerEffects)
		{
			if (E.Source == DeadHandle && WantsSourceDeathRemoval(E.EffectClass))
			{
				ToRemove.Add({ E.Target, PlayerID, E.EffectInstanceID, true });
			}
		}
	}

	// bByExpiration=false — this is cancellation by source death, not natural expiry.
	for (const FPendingEffectRemoval& Removal : ToRemove)
	{
		if (Removal.bPlayerScoped)
		{
			RemovePlayerEffect(Removal.PlayerID, Removal.EffectID, /*bByExpiration=*/false);
		}
		else
		{
			RemoveEffect(Removal.Target, Removal.EffectID, /*bByExpiration=*/false);
		}
	}
}

// ==================== Component Storage Helpers ====================

ISeinComponentStorage*
USeinWorldSubsystem::GetComponentStorageMutable(
	UScriptStruct* StructType)
{
	if (!RequireMutableStateAccess(
		TEXT("GetComponentStorageMutable")))
	{
		return nullptr;
	}
	ISeinComponentStorage** Found = ComponentStorages.Find(StructType);
	return Found ? *Found : nullptr;
}

const ISeinComponentStorage* USeinWorldSubsystem::GetComponentStorageRaw(UScriptStruct* StructType) const
{
	ISeinComponentStorage* const* Found = ComponentStorages.Find(StructType);
	return Found ? *Found : nullptr;
}

TArray<UScriptStruct*> USeinWorldSubsystem::GetComponentStorageTypes() const
{
	TArray<UScriptStruct*> Types;
	ComponentStorages.GetKeys(Types);
	return Types;
}

ISeinComponentStorage* USeinWorldSubsystem::GetOrCreateStorageForType(UScriptStruct* StructType)
{
	SEIN_CHECK_NOT_PARALLEL();
	if (!RequireStateMutationAuthorization(TEXT("GetOrCreateStorageForType")))
	{
		return nullptr;
	}
	if (ISeinComponentStorage** Found = ComponentStorages.Find(StructType))
	{
		return *Found;
	}

	FSeinGenericComponentStorage* Storage = new FSeinGenericComponentStorage(StructType, EntityPool.GetCapacity());
	ComponentStorages.Add(StructType, Storage);

	UE_LOG(LogSeinSim, Verbose, TEXT("Created component storage for %s"), *StructType->GetName());

#if !UE_BUILD_SHIPPING
	// Legacy-diagnostic guard (dev only, once per type): warn if this component
	// carries state the incomplete local fingerprint silently drops. Canonical
	// world-root capture uses a separate exact, fail-closed reflected encoder.
	{
		TArray<FString> Unhashed;
		FSeinGenericComponentStorage::CollectUnhashedStateFields(StructType, Unhashed);
		if (Unhashed.Num() > 0)
		{
			UE_LOG(LogSeinSim, Warning,
				TEXT("Component '%s' has field(s) excluded from the legacy local state fingerprint: %s. ")
				TEXT("Use the canonical world root for peer desync detection; extend ComputeHash only ")
				TEXT("when this compatibility diagnostic needs the field."),
				*StructType->GetName(), *FString::Join(Unhashed, TEXT(", ")));
		}
	}
#endif

	return Storage;
}

// ==================== System Registration ====================

void USeinWorldSubsystem::RecordExecutionTopologyFailure(
	const FString& Reason)
{
	if (!bExecutionTopologyValid)
	{
		return;
	}
	bExecutionTopologyValid = false;
	ExecutionTopologyFailureReason = Reason.IsEmpty()
		? TEXT("The deterministic execution topology became invalid.")
		: Reason;
	UE_LOG(LogSeinSim, Error, TEXT("Execution topology invalid: %s"),
		*ExecutionTopologyFailureReason);
}

void USeinWorldSubsystem::InvalidateFrozenExecutionTopology(
	const FString& Reason)
{
	if (bExecutionTopologyTeardown || !bExecutionTopologyValid)
	{
		return;
	}
	RecordExecutionTopologyFailure(Reason);

	if (MatchBootstrapState != ESeinMatchBootstrapState::Consumed)
	{
		FailMatchBootstrapInternal(ExecutionTopologyFailureReason);
		return;
	}

	StopSimulation();
	TGuardValue<bool> ReadOnlyGuard(bReadOnlyCallbackInProgress, true);
	TGuardValue<bool> ObserverGuard(bObserverCallbackInProgress, true);
	OnExecutionTopologyInvalidated.Broadcast(
		ExecutionTopologyFailureReason);
}

void USeinWorldSubsystem::InvalidateDeterministicExecutionContract(
	const FString& Reason)
{
	checkf(IsInGameThread(),
		TEXT("A deterministic execution contract was invalidated away from the game thread."));
	InvalidateFrozenExecutionTopology(Reason);
}

void USeinWorldSubsystem::TerminateAndReleaseForModuleUnload(
	FName OwnerModuleId,
	const FString& Detail)
{
	checkf(IsInGameThread(),
		TEXT("A deterministic module attempted to unload away from the game thread."));
	checkf(!OwnerModuleId.IsNone(),
		TEXT("Deterministic module unload requires a stable module id."));
	checkf(!bSimulationTickDispatchInProgress
			&& !bSnapshotCaptureInProgress
			&& !bSnapshotRestoreInProgress
			&& !bMatchBootstrapMaterializerInvocationActive
			&& OwnerTransitionDepth == 0
			&& !bDispatchingPauseControlFrame
			&& !bReadOnlyCallbackInProgress
			&& !bObserverCallbackInProgress
			&& !bDestroyNotificationInProgress
			&& ActiveAICommandEmitter == nullptr,
		TEXT("Deterministic module unload entered while the world was inside a callback or state transaction."));

	// Notification is exactly once, but cleanup is deliberately repeatable. A
	// replacement generation can bind public extension seams on this already
	// terminal world before it too unloads.
	if (bExecutionTopologyTeardown || bModuleUnloadStateReleased)
	{
		ReleaseAllModuleOwnedState();
		return;
	}

	FString CanonicalOwner;
	FString OwnerError;
	if (!FSeinSimulationContentManifestCodec::CanonicalizeStableId(
			OwnerModuleId.ToString(),
			CanonicalOwner,
			OwnerError))
	{
		CanonicalOwner = TEXT("<invalid-module-id>");
	}
	const FString Reason = FString::Printf(
		TEXT("Deterministic module '%s' withdrew live state%s%s."),
		*CanonicalOwner,
		Detail.IsEmpty() ? TEXT("") : TEXT(": "),
		*Detail);
	if (bExecutionTopologyValid)
	{
		InvalidateFrozenExecutionTopology(Reason);
	}
	else
	{
		if (ExecutionTopologyFailureReason.IsEmpty())
		{
			ExecutionTopologyFailureReason = Reason;
		}
		if (MatchBootstrapState != ESeinMatchBootstrapState::Consumed)
		{
			// A pre-freeze registration failure may already have poisoned the
			// topology without closing an Awaiting/Applying bootstrap.
			FailMatchBootstrapInternal(ExecutionTopologyFailureReason);
		}
	}

	bModuleUnloadStateReleased = true;
	ReleaseAllModuleOwnedState();
}

bool USeinWorldSubsystem::RegisterAuthoritativeDestinationProvider(
	const FString& StableProviderId,
	uint32 BehaviorRevision,
	FSeinAuthoritativeDestinationProviderResolver Resolver,
	uint64& OutRegistrationToken,
	FString* OutError)
{
	OutRegistrationToken = 0;
	if (OutError)
	{
		OutError->Reset();
	}
	const auto Reject = [&](const FString& Error, bool bPoison)
	{
		if (OutError)
		{
			*OutError = Error;
		}
		if (bPoison)
		{
			if (bExecutionTopologyFrozen)
			{
				InvalidateFrozenExecutionTopology(Error);
			}
			else
			{
				RecordExecutionTopologyFailure(Error);
			}
		}
		return false;
	};

	if (!IsInGameThread())
	{
		return Reject(
			TEXT("Authoritative-destination providers must register on the game thread."),
			false);
	}
	if (bExecutionTopologyTeardown || bModuleUnloadStateReleased)
	{
		return Reject(
			TEXT("Authoritative-destination providers cannot register after world teardown or terminal module unload."),
			false);
	}
	if (bReadOnlyCallbackInProgress
		|| bObserverCallbackInProgress
		|| bSimulationTickDispatchInProgress
		|| bSnapshotCaptureInProgress
		|| bSnapshotRestoreInProgress
		|| bMatchBootstrapMaterializerInvocationActive)
	{
		return Reject(
			TEXT("Authoritative-destination provider registration cannot mutate topology during a callback or state transaction."),
			true);
	}
	if (bExecutionTopologyFrozen)
	{
		return Reject(
			TEXT("Authoritative-destination providers must register before deterministic topology freeze."),
			true);
	}
	if (!bExecutionTopologyValid)
	{
		return Reject(
			ExecutionTopologyFailureReason.IsEmpty()
				? TEXT("The deterministic execution topology is already invalid.")
				: ExecutionTopologyFailureReason,
			false);
	}

	FString CanonicalStableID;
	FString StableIDError;
	if (!FSeinSimulationContentManifestCodec::CanonicalizeStableId(
			StableProviderId,
			CanonicalStableID,
			StableIDError))
	{
		return Reject(
			FString::Printf(
				TEXT("Invalid authoritative-destination provider ID '%s': %s"),
				*StableProviderId,
				*StableIDError),
			true);
	}
	if (BehaviorRevision == 0 || !Resolver.IsBound())
	{
		return Reject(
			TEXT("Authoritative-destination providers require a positive behavior revision and a bound resolver."),
			true);
	}
	if (AuthoritativeDestinationProviders.Num()
		>= MaxAuthoritativeDestinationProviders)
	{
		return Reject(
			TEXT("The authoritative-destination provider registry reached its deterministic capacity."),
			true);
	}
	if (AuthoritativeDestinationProviders.ContainsByPredicate(
			[&CanonicalStableID](
				const FRegisteredAuthoritativeDestinationProvider& Existing)
			{
				return Existing.CanonicalStableID == CanonicalStableID;
			}))
	{
		return Reject(
			FString::Printf(
				TEXT("Duplicate authoritative-destination provider ID '%s'."),
				*CanonicalStableID),
			true);
	}
	if (NextAuthoritativeDestinationProviderToken == 0
		|| NextAuthoritativeDestinationProviderToken == MAX_uint64)
	{
		return Reject(
			TEXT("The authoritative-destination provider token space is exhausted."),
			true);
	}

	FRegisteredAuthoritativeDestinationProvider Registered;
	Registered.CanonicalStableID = MoveTemp(CanonicalStableID);
	Registered.BehaviorRevision = BehaviorRevision;
	Registered.RegistrationToken =
		NextAuthoritativeDestinationProviderToken++;
	Registered.Resolver = MoveTemp(Resolver);
	OutRegistrationToken = Registered.RegistrationToken;
	AuthoritativeDestinationProviders.Add(MoveTemp(Registered));
	AuthoritativeDestinationProviders.Sort(
		[](const FRegisteredAuthoritativeDestinationProvider& Left,
			const FRegisteredAuthoritativeDestinationProvider& Right)
		{
			return Left.CanonicalStableID < Right.CanonicalStableID;
		});
	return true;
}

bool USeinWorldSubsystem::UnregisterAuthoritativeDestinationProvider(
	uint64 RegistrationToken,
	FString* OutError)
{
	if (OutError)
	{
		OutError->Reset();
	}
	if (!IsInGameThread())
	{
		if (OutError)
		{
			*OutError =
				TEXT("Authoritative-destination providers must unregister on the game thread.");
		}
		return false;
	}
	if (RegistrationToken == 0)
	{
		if (OutError)
		{
			*OutError =
				TEXT("Authoritative-destination provider unregistration requires a valid token.");
		}
		return false;
	}
	if (bReadOnlyCallbackInProgress
		|| bObserverCallbackInProgress
		|| bSimulationTickDispatchInProgress
		|| bSnapshotCaptureInProgress
		|| bSnapshotRestoreInProgress
		|| bMatchBootstrapMaterializerInvocationActive)
	{
		const FString Error =
			TEXT("Authoritative-destination provider unregistration cannot mutate topology during a callback or state transaction.");
		if (OutError)
		{
			*OutError = Error;
		}
		if (bExecutionTopologyFrozen)
		{
			InvalidateFrozenExecutionTopology(Error);
		}
		else
		{
			RecordExecutionTopologyFailure(Error);
		}
		return false;
	}

	const int32 ProviderIndex =
		AuthoritativeDestinationProviders.IndexOfByPredicate(
			[RegistrationToken](
				const FRegisteredAuthoritativeDestinationProvider& Existing)
			{
				return Existing.RegistrationToken == RegistrationToken;
			});
	if (ProviderIndex == INDEX_NONE)
	{
		if (OutError)
		{
			*OutError =
				TEXT("The authoritative-destination provider token is no longer registered.");
		}
		return false;
	}

	const FString RemovedStableID =
		AuthoritativeDestinationProviders[ProviderIndex].CanonicalStableID;
	AuthoritativeDestinationProviders.RemoveAt(ProviderIndex);
	if (bExecutionTopologyFrozen
		&& !bExecutionTopologyTeardown
		&& !bModuleUnloadStateReleased)
	{
		InvalidateFrozenExecutionTopology(FString::Printf(
			TEXT("Authoritative-destination provider '%s' unregistered after deterministic topology freeze."),
			*RemovedStableID));
	}
	return true;
}

bool USeinWorldSubsystem::HasAuthoritativeDestinationProviders() const
{
	return !AuthoritativeDestinationProviders.IsEmpty()
		|| AuthoritativeDestinationResolver.IsBound();
}

bool USeinWorldSubsystem::IsAuthoritativeDestination(
	const FSeinAuthoritativeDestinationQuery& Query)
{
	checkf(IsInGameThread(),
		TEXT("Authoritative-destination providers may only execute on the game thread."));
	if (bAuthoritativeDestinationQueryInProgress)
	{
		const FString Error =
			TEXT("Authoritative-destination provider queries may not re-enter the registry.");
		if (bExecutionTopologyFrozen)
		{
			InvalidateFrozenExecutionTopology(Error);
		}
		else
		{
			RecordExecutionTopologyFailure(Error);
		}
		return false;
	}

	TGuardValue<bool> QueryGuard(
		bAuthoritativeDestinationQueryInProgress, true);
	TGuardValue<bool> ReadOnlyGuard(bReadOnlyCallbackInProgress, true);
	TGuardValue<bool> ObserverGuard(bObserverCallbackInProgress, true);
	for (const FRegisteredAuthoritativeDestinationProvider& Provider :
		AuthoritativeDestinationProviders)
	{
		if (!Provider.Resolver.IsBound())
		{
			return false;
		}
		if (Provider.Resolver.Execute(Query))
		{
			return true;
		}
	}
	return AuthoritativeDestinationResolver.IsBound()
		&& AuthoritativeDestinationResolver.Execute(Query.WorldPosition);
}

bool USeinWorldSubsystem::RegisterSystem(
	ISeinSystem* System,
	FString* OutError)
{
	if (OutError)
	{
		OutError->Reset();
	}
	const auto Reject = [&](const FString& Error, bool bPoison)
	{
		if (OutError)
		{
			*OutError = Error;
		}
		if (bPoison)
		{
			if (bExecutionTopologyFrozen)
			{
				InvalidateFrozenExecutionTopology(Error);
			}
			else
			{
				RecordExecutionTopologyFailure(Error);
			}
		}
		return false;
	};

	if (!IsInGameThread())
	{
		const FString Error =
			TEXT("Simulation systems may register only on the game thread.");
		if (OutError)
		{
			*OutError = Error;
		}
		UE_LOG(LogSeinSim, Error, TEXT("%s"), *Error);
		return false;
	}
	if (!System)
	{
		return Reject(
			TEXT("Cannot register a null simulation system."),
			true);
	}
	if (bExecutionTopologyTeardown)
	{
		return Reject(
			TEXT("Cannot register a simulation system during world teardown."),
			false);
	}
	for (const FRegisteredSystem& Registered : Systems)
	{
		if (Registered.System == System)
		{
			// Registration captures the descriptor exactly once. Repeating the
			// same pointer is an idempotent ownership/lifecycle retry and never
			// re-enters potentially mutable implementation code.
			return true;
		}
	}
	if (bExecutionTopologyFrozen)
	{
		return Reject(
			TEXT("A new simulation system registered after execution topology freeze."),
			true);
	}
	if (!bExecutionTopologyValid)
	{
		return Reject(
			ExecutionTopologyFailureReason.IsEmpty()
				? TEXT("The pending execution topology is already invalid.")
				: ExecutionTopologyFailureReason,
			false);
	}

	FSeinSystemDescriptor Descriptor = System->DescribeSystem();
	FString CanonicalStableID;
	FString StableIDError;
	if (!FSeinSimulationContentManifestCodec::CanonicalizeStableId(
			Descriptor.StableSystemID.ToString(),
			CanonicalStableID,
			StableIDError))
	{
		return Reject(
			FString::Printf(
				TEXT("Simulation system stable ID '%s' is invalid: %s"),
				*Descriptor.StableSystemID.ToString(),
				*StableIDError),
			true);
	}
	if (Descriptor.ImplementationRevision == 0)
	{
		return Reject(
			FString::Printf(
				TEXT("Simulation system '%s' has a zero implementation revision."),
				*CanonicalStableID),
			true);
	}
	if (static_cast<uint8>(Descriptor.Phase)
		> static_cast<uint8>(ESeinTickPhase::FinalObservation))
	{
		return Reject(
			FString::Printf(
				TEXT("Simulation system '%s' has an invalid tick phase."),
				*CanonicalStableID),
			true);
	}
	if (Descriptor.Phase == ESeinTickPhase::FinalObservation
		&& Descriptor.StateCoverage != ESeinSystemStateCoverage::Stateless)
	{
		return Reject(
			FString::Printf(
				TEXT("Final-observation system '%s' must declare stateless coverage."),
				*CanonicalStableID),
			true);
	}

	switch (Descriptor.StateCoverage)
	{
	case ESeinSystemStateCoverage::Stateless:
		if (!Descriptor.RequiredCanonicalStateContributorKeys.IsEmpty())
		{
			return Reject(
				FString::Printf(
					TEXT("Stateless simulation system '%s' names canonical-state contributors."),
					*CanonicalStableID),
				true);
		}
		break;

	case ESeinSystemStateCoverage::CanonicalStateContributors:
		{
			if (Descriptor.RequiredCanonicalStateContributorKeys.IsEmpty()
				|| Descriptor.RequiredCanonicalStateContributorKeys.Num()
					> FSeinSimulationContentManifestCodec::MaxContributors)
			{
				return Reject(
					FString::Printf(
						TEXT("Simulation system '%s' requires an invalid canonical-state contributor count."),
						*CanonicalStableID),
					true);
			}

			TArray<FString> CanonicalContributorKeys;
			CanonicalContributorKeys.Reserve(
				Descriptor.RequiredCanonicalStateContributorKeys.Num());
			for (FName RawKey :
				Descriptor.RequiredCanonicalStateContributorKeys)
			{
				FString CanonicalKey;
				FString KeyError;
				if (!CanonicalizeSystemStateContributorKey(
						RawKey,
						CanonicalKey,
						KeyError))
				{
					return Reject(
						FString::Printf(
							TEXT("Simulation system '%s' has invalid canonical-state contributor key '%s': %s"),
							*CanonicalStableID,
							*RawKey.ToString(),
							*KeyError),
						true);
				}
				CanonicalContributorKeys.Add(MoveTemp(CanonicalKey));
			}
			CanonicalContributorKeys.Sort();
			for (int32 Index = 1;
				Index < CanonicalContributorKeys.Num();
				++Index)
			{
				if (CanonicalContributorKeys[Index - 1]
					== CanonicalContributorKeys[Index])
				{
					return Reject(
						FString::Printf(
							TEXT("Simulation system '%s' names duplicate canonical-state contributor '%s'."),
							*CanonicalStableID,
							*CanonicalContributorKeys[Index]),
						true);
				}
			}

			Descriptor.RequiredCanonicalStateContributorKeys.Reset(
				CanonicalContributorKeys.Num());
			for (const FString& CanonicalKey : CanonicalContributorKeys)
			{
				Descriptor.RequiredCanonicalStateContributorKeys.Add(
					FName(*CanonicalKey));
			}
		}
		break;

	case ESeinSystemStateCoverage::Unspecified:
	default:
		return Reject(
			FString::Printf(
				TEXT("Simulation system '%s' did not declare retained-state recapture coverage."),
				*CanonicalStableID),
			true);
	}

	Descriptor.StableSystemID = FName(*CanonicalStableID);
	for (const FRegisteredSystem& Registered : Systems)
	{
		if (Registered.CanonicalStableID == CanonicalStableID)
		{
			return Reject(
				FString::Printf(
					TEXT("Duplicate simulation system stable ID '%s'."),
					*CanonicalStableID),
				true);
		}
	}

	FRegisteredSystem& Registered = Systems.AddDefaulted_GetRef();
	Registered.System = System;
	Registered.Descriptor = Descriptor;
	Registered.CanonicalStableID = MoveTemp(CanonicalStableID);
	UE_LOG(LogSeinSim, Log,
		TEXT("Registered system: %s (revision: %u, phase: %d, priority: %d)"),
		*Registered.CanonicalStableID,
		Registered.Descriptor.ImplementationRevision,
		static_cast<int32>(Registered.Descriptor.Phase),
		Registered.Descriptor.Priority);
	return true;
}

bool USeinWorldSubsystem::UnregisterSystem(
	ISeinSystem* System,
	FString* OutError)
{
	if (OutError)
	{
		OutError->Reset();
	}
	if (!IsInGameThread())
	{
		const FString Error =
			TEXT("Simulation systems may unregister only on the game thread.");
		if (OutError)
		{
			*OutError = Error;
		}
		UE_LOG(LogSeinSim, Error, TEXT("%s"), *Error);
		return false;
	}
	if (!System)
	{
		if (OutError)
		{
			*OutError = TEXT("Cannot unregister a null simulation system.");
		}
		return false;
	}

	const int32 Index = Systems.IndexOfByPredicate(
		[System](const FRegisteredSystem& Registered)
		{
			return Registered.System == System;
		});
	if (Index == INDEX_NONE)
	{
		return true;
	}

	const FString StableID = Systems[Index].CanonicalStableID;
	if (!bExecutionTopologyFrozen)
	{
		Systems.RemoveAt(Index);
		return true;
	}

	// Never resize the dispatch array under an active range iteration. Nulling
	// the pointer severs the executable module reference immediately; the now
	// invalid frozen topology will never dispatch another frame.
	if (bSimulationTickDispatchInProgress)
	{
		Systems[Index].System = nullptr;
	}
	else
	{
		Systems.RemoveAt(Index);
	}
	if (bExecutionTopologyTeardown)
	{
		return true;
	}

	const FString Error = FString::Printf(
		TEXT("Simulation system '%s' unregistered after execution topology freeze."),
		*StableID);
	if (OutError)
	{
		*OutError = Error;
	}
	InvalidateFrozenExecutionTopology(Error);
	return false;
}

bool USeinWorldSubsystem::TryBuildExecutionTopologyCandidate(
	FExecutionTopologyCandidate& OutCandidate,
	FString& OutError) const
{
	OutCandidate = {};
	OutError.Reset();
	if (bExecutionTopologyFrozen)
	{
		OutError =
			TEXT("Execution topology candidate construction requires an unfrozen world.");
		return false;
	}
	if (!IsInGameThread())
	{
		OutError =
			TEXT("Execution topology may freeze only on the game thread.");
		return false;
	}
	const UWorld* World = GetWorld();
	if (!World || !World->IsInitialized())
	{
		OutError =
			TEXT("Execution topology cannot freeze before world-subsystem initialization completes.");
		return false;
	}
	if (bExecutionTopologyTeardown)
	{
		OutError =
			TEXT("Execution topology cannot freeze during world teardown.");
		return false;
	}
	if (!bExecutionTopologyValid)
	{
		OutError = ExecutionTopologyFailureReason.IsEmpty()
			? TEXT("The pending execution topology is invalid.")
			: ExecutionTopologyFailureReason;
		return false;
	}
	if (MatchBootstrapState != ESeinMatchBootstrapState::Awaiting
		|| bIsRunning || TickerHandle.IsValid() || CurrentTick != 0
		|| MatchState != ESeinMatchState::Lobby
		|| bSimulationTickDispatchInProgress
		|| bSnapshotCaptureInProgress)
	{
		OutError = FString::Printf(
			TEXT("Execution topology requires a stopped tick-zero Awaiting/Lobby world (bootstrap=%s running=%d tick=%d)."),
			MatchBootstrapStateName(MatchBootstrapState),
			bIsRunning ? 1 : 0,
			CurrentTick);
		return false;
	}
	if (Systems.IsEmpty()
		|| Systems.Num()
			> FSeinSimulationContentManifestCodec::MaxContributors)
	{
		OutError =
			TEXT("Execution topology has an invalid system count.");
		return false;
	}
	if (!NativeCanonicalStateSchema.IsValid())
	{
		OutError =
			TEXT("Execution topology cannot verify system-state coverage without a frozen canonical-state schema.");
		return false;
	}

	TSet<FString> AvailableCanonicalStateContributors;
	for (const FSeinFrozenCanonicalStateContributor& Contributor :
		NativeCanonicalStateSchema.GetContributors())
	{
		const FString CanonicalKey =
			FSeinCanonicalStateRegistry::CanonicalKey(
				Contributor.Descriptor.Key);
		if (CanonicalKey.IsEmpty())
		{
			OutError =
				TEXT("The frozen canonical-state schema contains an invalid contributor key.");
			return false;
		}
		AvailableCanonicalStateContributors.Add(CanonicalKey);
	}
	TSet<FString> SystemClaimedContributors;
	for (const FRegisteredSystem& Registered : Systems)
	{
		if (Registered.Descriptor.StateCoverage
			!= ESeinSystemStateCoverage::CanonicalStateContributors)
		{
			continue;
		}
		for (FName RequiredKey :
			Registered.Descriptor.RequiredCanonicalStateContributorKeys)
		{
			// FName round-trips resurrect the first-created casing in the
			// process, so re-lowercase before comparing against canonical
			// (always-lowercase) contributor keys — otherwise an unrelated
			// mixed-case FName created earlier makes this check load-order
			// dependent.
			const FString ClaimedKey = RequiredKey.ToString().ToLower();
			if (!AvailableCanonicalStateContributors.Contains(ClaimedKey))
			{
				OutError = FString::Printf(
					TEXT("Simulation system '%s' requires missing canonical-state contributor '%s'."),
					*Registered.CanonicalStableID,
					*ClaimedKey);
				return false;
			}
			SystemClaimedContributors.Add(ClaimedKey);
		}
	}
	// Reverse direction: captured state with no live owner is a bootstrap
	// error. Conditional providers may use their external exemption only when
	// their owning subsystem proves that the corresponding world feature is
	// explicitly disabled; an enabled feature still requires its system edge.
	const TArray<FString> ClaimedContributorKeys =
		SystemClaimedContributors.Array();
	if (!FSeinCanonicalStateRegistry::ValidateWorldOwnershipClaims(
		NativeCanonicalStateSchema,
		{*this,
			ESeinCanonicalStateWorldBindingDisposition::Provisional},
		ClaimedContributorKeys,
		OutError))
	{
		return false;
	}

	TArray<FRegisteredSystem> CanonicalSystems = Systems;
	CanonicalSystems.Sort(
		[](const FRegisteredSystem& A, const FRegisteredSystem& B)
		{
			if (A.Descriptor.Phase != B.Descriptor.Phase)
			{
				return static_cast<uint8>(A.Descriptor.Phase)
					< static_cast<uint8>(B.Descriptor.Phase);
			}
			if (A.Descriptor.Priority != B.Descriptor.Priority)
			{
				return A.Descriptor.Priority < B.Descriptor.Priority;
			}
			return A.CanonicalStableID.Compare(
				B.CanonicalStableID,
				ESearchCase::CaseSensitive) < 0;
		});

	FSeinCanonicalDigestWriter Writer(
		TEXT("SeinARTS.Simulation.ExecutionTopology"), 2);
	FString CandidateManifest =
		TEXT("SeinARTS.Simulation.ExecutionTopology\n2\n");
	CandidateManifest += FString::Printf(
		TEXT("%d\n"), CanonicalSystems.Num());
	bool bWriteOK = Writer.WriteInt32(CanonicalSystems.Num());
	for (const FRegisteredSystem& Registered : CanonicalSystems)
	{
		bWriteOK = bWriteOK
			&& Writer.WriteString(Registered.CanonicalStableID)
			&& Writer.WriteUInt32(
				Registered.Descriptor.ImplementationRevision)
			&& Writer.WriteUInt8(
				static_cast<uint8>(Registered.Descriptor.Phase))
			&& Writer.WriteInt32(Registered.Descriptor.Priority)
			&& Writer.WriteUInt8(static_cast<uint8>(
				Registered.Descriptor.StateCoverage))
			&& Writer.WriteInt32(
				Registered.Descriptor.
					RequiredCanonicalStateContributorKeys.Num());
		for (FName RequiredKey :
			Registered.Descriptor.RequiredCanonicalStateContributorKeys)
		{
			bWriteOK = bWriteOK
				&& Writer.WriteString(RequiredKey.ToString());
		}
		CandidateManifest += FString::Printf(
			TEXT("%d:%s|%u|%u|%d|%u|%d"),
			Registered.CanonicalStableID.Len(),
			*Registered.CanonicalStableID,
			Registered.Descriptor.ImplementationRevision,
			static_cast<uint8>(Registered.Descriptor.Phase),
			Registered.Descriptor.Priority,
			static_cast<uint8>(Registered.Descriptor.StateCoverage),
			Registered.Descriptor.
				RequiredCanonicalStateContributorKeys.Num());
		for (FName RequiredKey :
			Registered.Descriptor.RequiredCanonicalStateContributorKeys)
		{
			const FString Key = RequiredKey.ToString();
			CandidateManifest += FString::Printf(
				TEXT("|%d:%s"),
				Key.Len(),
				*Key);
		}
		CandidateManifest += TEXT("\n");
	}

	FGuid CandidateDigest;
	if (!bWriteOK || !Writer.Finalize(CandidateDigest, OutError)
		|| !CandidateDigest.IsValid())
	{
		if (OutError.IsEmpty())
		{
			OutError =
				TEXT("Execution topology digest computation failed.");
		}
		return false;
	}

	OutCandidate.Systems = MoveTemp(CanonicalSystems);
	OutCandidate.Manifest = MoveTemp(CandidateManifest);
	OutCandidate.Digest = CandidateDigest;
	return true;
}

void USeinWorldSubsystem::AdoptExecutionTopologyCandidate(
	FExecutionTopologyCandidate&& Candidate)
{
	check(IsInGameThread());
	check(!bExecutionTopologyFrozen);
	check(bExecutionTopologyValid);
	check(Candidate.IsValid());

	Systems = MoveTemp(Candidate.Systems);
	ExecutionTopologyManifest = MoveTemp(Candidate.Manifest);
	ExecutionTopologyDigest = Candidate.Digest;
	bExecutionTopologyFrozen = true;
	UE_LOG(LogSeinSim, Log,
		TEXT("Execution topology frozen (%d systems, digest=%s)."),
		Systems.Num(),
		*ExecutionTopologyDigest.ToString(EGuidFormats::Digits));
}

bool USeinWorldSubsystem::FreezeExecutionTopology(FString& OutError)
{
	OutError.Reset();
	if (bExecutionTopologyFrozen)
	{
		if (bExecutionTopologyValid
			&& ExecutionTopologyDigest.IsValid()
			&& !ExecutionTopologyManifest.IsEmpty())
		{
			return true;
		}
		OutError = ExecutionTopologyFailureReason.IsEmpty()
			? TEXT("The frozen execution topology is invalid.")
			: ExecutionTopologyFailureReason;
		return false;
	}
	if (bSnapshotRestoreInProgress)
	{
		OutError =
			TEXT("Execution topology cannot freeze during snapshot restore.");
		return false;
	}

	FExecutionTopologyCandidate Candidate;
	if (!TryBuildExecutionTopologyCandidate(Candidate, OutError))
	{
		return false;
	}
	AdoptExecutionTopologyCandidate(MoveTemp(Candidate));
	return true;
}

// ==================== Visual Events ====================

void USeinWorldSubsystem::EnqueueVisualEvent(const FSeinVisualEvent& Event)
{
	SEIN_CHECK_NOT_PARALLEL();
	VisualEventQueue.Enqueue(Event);
}

TArray<FSeinVisualEvent> USeinWorldSubsystem::FlushVisualEvents()
{
	return VisualEventQueue.Flush();
}

// ==================== State Hashing ====================

namespace
{
	uint32 HashCanonicalNameString(FString Value)
	{
		// FName equality is case-insensitive; normalize before hashing so display
		// casing selected by process-local construction order cannot affect peers.
		Value.ToLowerInline();
		const FTCHARToUTF8 Utf8(*Value, Value.Len());
		uint32 Hash = 2166136261u; // FNV-1a 32
		for (int32 Index = 0; Index < Utf8.Length(); ++Index)
		{
			Hash ^= static_cast<uint8>(Utf8.Get()[Index]);
			Hash *= 16777619u;
		}
		return Hash;
	}

	FORCEINLINE uint32 HashCanonicalName(const FName Name)
	{
		return HashCanonicalNameString(Name.ToString());
	}

	FORCEINLINE uint32 HashCanonicalTag(const FGameplayTag Tag)
	{
		return HashCanonicalName(Tag.GetTagName());
	}

	// Tag comparator — iteration order that's stable across processes.
	FORCEINLINE bool TagNameLess(const FGameplayTag& A, const FGameplayTag& B)
	{
		return A.GetTagName().Compare(B.GetTagName()) < 0;
	}

	// Walk a TMap<FGameplayTag, T> in sorted-by-name order + hash each
	// (tag, value) pair. Handles the cross-process-stable iteration the
	// audit flagged for pointer-keyed maps + any tag-keyed state that
	// contributes to the sim hash.
	template<typename ValueType, typename HashValueFn>
	FORCEINLINE void HashTagMap(uint32& Hash, const TMap<FGameplayTag, ValueType>& Map, HashValueFn HashVal)
	{
		TArray<FGameplayTag> Keys;
		Map.GetKeys(Keys);
		Keys.Sort(TagNameLess);
		for (const FGameplayTag& Key : Keys)
		{
			Hash = HashCombine(Hash, HashCanonicalTag(Key));
			Hash = HashCombine(Hash, HashVal(Map[Key]));
		}
	}

	// Hash a FSeinPlayerState field-by-field. The free-function
	// GetTypeHash(FSeinPlayerState) only covers PlayerID (it's a TMap-
	// key hash) — we need the full state for desync detection.
	uint32 HashPlayerStateFields(const FSeinPlayerState& State)
	{
		uint32 Hash = GetTypeHash(State.PlayerID);
		Hash = HashCombine(Hash, GetTypeHash(State.FactionID));
		Hash = HashCombine(Hash, GetTypeHash(State.TeamID));
		Hash = HashCombine(Hash, GetTypeHash(State.bEliminated));
		Hash = HashCombine(Hash, GetTypeHash(State.bReady));
		Hash = HashCombine(Hash, GetTypeHash(State.bIsSpectator));
		Hash = HashCombine(Hash, GetTypeHash(State.bIsAI));
		HashTagMap(Hash, State.Resources,          [](const FFixedPoint& V) { return GetTypeHash(V); });
		HashTagMap(Hash, State.ResourceCaps,       [](const FFixedPoint& V) { return GetTypeHash(V); });
		HashTagMap(Hash, State.PlayerTagRefCounts, [](int32 V)              { return GetTypeHash(V); });

		// PlayerTags is the cached presence set mirroring PlayerTagRefCounts.
		// Hashing both catches drift between the refcount map and the cache
		// (a known silent-desync category in tag-based systems). Sort the
		// container's tags by name first — FGameplayTagContainer's natural
		// iteration order isn't guaranteed across processes.
		{
			TArray<FGameplayTag> Tags;
			State.PlayerTags.GetGameplayTagArray(Tags);
			Tags.Sort(TagNameLess);
			Hash = HashCombine(Hash, GetTypeHash(Tags.Num()));
			for (const FGameplayTag& T : Tags)
			{
				Hash = HashCombine(Hash, HashCanonicalTag(T));
			}
		}

		// ClassEffects + PlayerEffects are TArrays — order is already
		// deterministic by insertion (apply order is sim-tick driven).
		Hash = HashCombine(Hash, GetTypeHash(State.ClassEffects.Num()));
		for (const FSeinActiveEffect& E : State.ClassEffects)
		{
			Hash = HashCombine(Hash, GetTypeHash(E));
		}
		Hash = HashCombine(Hash, GetTypeHash(State.PlayerEffects.Num()));
		for (const FSeinActiveEffect& E : State.PlayerEffects)
		{
			Hash = HashCombine(Hash, GetTypeHash(E));
		}
		return Hash;
	}

	uint32 HashPairCapabilitySourceKey(
		const FSeinPairCapabilitySourceKey& Key,
		int32 RefCount)
	{
		uint32 Hash = GetTypeHash(Key.SourcePlayer);
		Hash = HashCombine(Hash, GetTypeHash(Key.TargetPlayer));
		Hash = HashCombine(Hash, HashCanonicalTag(Key.CapabilityTag));
		Hash = HashCombine(Hash, HashCanonicalTag(Key.SourceKindTag));
		Hash = HashCombine(Hash, GetTypeHash(Key.SourceInstanceID));
		Hash = HashCombine(Hash, GetTypeHash(RefCount));
		return Hash;
	}

	uint32 HashPairCapabilityEffectiveKey(
		const FSeinPairCapabilityKey& Key,
		int32 RefCount)
	{
		uint32 Hash = GetTypeHash(Key.SourcePlayer);
		Hash = HashCombine(Hash, GetTypeHash(Key.TargetPlayer));
		Hash = HashCombine(Hash, HashCanonicalTag(Key.CapabilityTag));
		Hash = HashCombine(Hash, GetTypeHash(RefCount));
		return Hash;
	}
}

int32 USeinWorldSubsystem::ComputeStateHash() const
{
	uint32 Hash = GetTypeHash(CurrentTick);
	Hash = HashCombine(Hash, GetTypeHash(NextEffectInstanceID));
	Hash = HashCombine(Hash, GetTypeHash(NextAbilityActivationID));
	Hash = HashCombine(
		Hash,
		GetTypeHash(LatentActionManager
			? LatentActionManager->GetNextActionID()
			: int64{1}));

	// Entities — pool iterates in slot-index order, already deterministic.
	EntityPool.ForEachEntity([&Hash](FSeinEntityHandle Handle, const FSeinEntity& Entity)
	{
		Hash = HashCombine(Hash, GetTypeHash(Entity));
	});

	// Component storages — TMap is keyed by UScriptStruct* (pointer), so sort
	// by path for a repeatable local fold. The reflected property/value hashes
	// below still contain process-local and incomplete identity; this legacy
	// fingerprint is intentionally not a cross-process contract.
	//
	// The per-storage ComputeHash() walk is the expensive part (every live slot
	// × every reflected field). Each storage's hash is INDEPENDENT and a pure
	// read, so fan those out across worker threads — then fold the results back
	// SERIALLY in the sorted order. HashCombine is not associative, so the fold
	// order is load-bearing and must stay identical to the serial path; only the
	// independent per-storage hashing runs concurrently. SeinParallelFor is
	// gated by Sein.Sim.Parallel and is bit-identical to the serial loop (it
	// runs serially when the cvar is off or the batch is below the min). The
	// SeinParallelFor body obeys the contract: reads immutable storage, writes
	// only its own disjoint Results slot, no structural mutation.
	{
		TArray<UScriptStruct*> Structs;
		Structs.Reserve(ComponentStorages.Num());
		for (const auto& Pair : ComponentStorages) { Structs.Add(Pair.Key); }
		Structs.Sort([](const UScriptStruct& A, const UScriptStruct& B)
		{
			return A.GetPathName() < B.GetPathName();
		});

		const int32 NumStorages = Structs.Num();
		TArray<uint32> Results;
		Results.SetNumUninitialized(NumStorages);
		SeinParallelFor(NumStorages, [this, &Structs, &Results](int32 Index)
		{
			Results[Index] = ComponentStorages[Structs[Index]]->ComputeHash();
		});

		for (int32 Index = 0; Index < NumStorages; ++Index)
		{
			Hash = HashCombine(Hash,
				HashCanonicalNameString(Structs[Index]->GetPathName()));
			Hash = HashCombine(Hash, Results[Index]);
		}
	}

	// Player states — sort by PlayerID (uint8, ordering trivially stable).
	{
		TArray<FSeinPlayerID> Keys;
		PlayerStates.GetKeys(Keys);
		Keys.Sort();
		for (const FSeinPlayerID& PID : Keys)
		{
			Hash = HashCombine(Hash, HashPlayerStateFields(PlayerStates[PID]));
		}
	}
	{
		TArray<FSeinPairCapabilitySourceKey> Keys;
		PairCapabilitySourceRefCounts.GetKeys(Keys);
		Keys.Sort([](
			const FSeinPairCapabilitySourceKey& A,
			const FSeinPairCapabilitySourceKey& B)
		{
			if (A.SourcePlayer != B.SourcePlayer)
			{
				return A.SourcePlayer < B.SourcePlayer;
			}
			if (A.TargetPlayer != B.TargetPlayer)
			{
				return A.TargetPlayer < B.TargetPlayer;
			}
			const int32 CapabilityOrder =
				A.CapabilityTag.GetTagName().Compare(
					B.CapabilityTag.GetTagName());
			if (CapabilityOrder != 0)
			{
				return CapabilityOrder < 0;
			}
			const int32 SourceKindOrder =
				A.SourceKindTag.GetTagName().Compare(
					B.SourceKindTag.GetTagName());
			return SourceKindOrder != 0
				? SourceKindOrder < 0
				: A.SourceInstanceID < B.SourceInstanceID;
		});
		Hash = HashCombine(Hash, GetTypeHash(Keys.Num()));
		for (const FSeinPairCapabilitySourceKey& Key : Keys)
		{
			Hash = HashCombine(
				Hash,
				HashPairCapabilitySourceKey(
					Key,
					PairCapabilitySourceRefCounts[Key]));
		}
		TArray<FSeinPairCapabilityKey> EffectiveKeys;
		PairCapabilityEffectiveRefCounts.GetKeys(EffectiveKeys);
		EffectiveKeys.Sort([](
			const FSeinPairCapabilityKey& A,
			const FSeinPairCapabilityKey& B)
		{
			if (A.SourcePlayer != B.SourcePlayer)
			{
				return A.SourcePlayer < B.SourcePlayer;
			}
			if (A.TargetPlayer != B.TargetPlayer)
			{
				return A.TargetPlayer < B.TargetPlayer;
			}
			return A.CapabilityTag.GetTagName().Compare(
				B.CapabilityTag.GetTagName()) < 0;
		});
		Hash = HashCombine(Hash, GetTypeHash(EffectiveKeys.Num()));
		for (const FSeinPairCapabilityKey& Key : EffectiveKeys)
		{
			Hash = HashCombine(
				Hash,
				HashPairCapabilityEffectiveKey(
					Key,
					PairCapabilityEffectiveRefCounts[Key]));
		}
	}

	// Core/extension canonical state is part of the peer hash contract. Fold
	// each already-canonical leaf digest; never hash payload allocation or map
	// order. A missing/unloaded frozen native provider gets a distinct failure
	// marker so it cannot masquerade as valid empty state.
	const auto HashGuid = [&Hash](const FGuid& Digest)
	{
		Hash = HashCombine(Hash, GetTypeHash(Digest.A));
		Hash = HashCombine(Hash, GetTypeHash(Digest.B));
		Hash = HashCombine(Hash, GetTypeHash(Digest.C));
		Hash = HashCombine(Hash, GetTypeHash(Digest.D));
	};
	{
		TArray<FSeinSnapshotLatentActionRecord> LatentRecords;
		FGuid LatentSequenceDigest;
		FString CaptureError;
		const int64 NextLatentActionID = LatentActionManager
			? LatentActionManager->GetNextActionID()
			: 1;
		const bool bLatentCaptured =
			FSeinLatentActionCodecRegistry::CaptureRecords(
				LatentActionCodecManifest,
				*this,
				LatentActionManager,
				CurrentTick,
				NextLatentActionID,
				NextAbilityActivationID,
				LatentRecords,
				LatentSequenceDigest,
				CaptureError);
		Hash = HashCombine(Hash, 0x4C41544Eu); // "LATN"
		Hash = HashCombine(Hash, GetTypeHash(bLatentCaptured));
		Hash = HashCombine(Hash, GetTypeHash(LatentRecords.Num()));
		if (bLatentCaptured)
		{
			HashGuid(LatentSequenceDigest);
			for (const FSeinSnapshotLatentActionRecord& Record :
				LatentRecords)
			{
				HashGuid(Record.RecordDigest);
			}
		}
		else
		{
			Hash = HashCombine(Hash, 0xDEAD1A7Eu);
		}
	}
	{
		TArray<FSeinCanonicalStateContributorRecord> NativeRecords;
		FString CaptureError;
		// Before bootstrap commits, provider preparation is allowed to warm
		// immutable world-local inputs for a restore candidate. That provisional
		// readiness is not authoritative sim state and must not make this legacy
		// fingerprint depend on whether a rejected restore happened to reach the
		// provider-staging phase. Canonical provider state joins the fold only
		// after this world's own StateContract/value store is sealed.
		const bool bCommittedStateContract =
			CanonicalStateValues.IsSealed()
			&& MatchBootstrapReceipt.StateContractDigest.IsValid();
		const bool bNativeCaptured = bCommittedStateContract
			&& NativeCanonicalStateSchema.IsValid()
			&& FSeinCanonicalStateRegistry::CaptureContributorRecords(
				NativeCanonicalStateSchema,
				{ *this, CurrentTick },
				NativeRecords,
				CaptureError);
		Hash = HashCombine(Hash, GetTypeHash(bNativeCaptured));
		Hash = HashCombine(Hash, GetTypeHash(NativeRecords.Num()));
		if (bNativeCaptured)
		{
			for (const FSeinCanonicalStateContributorRecord& Record :
				NativeRecords)
			{
				HashGuid(Record.LeafDigest);
			}
		}

		TArray<FSeinCanonicalStateValueRecord> ValueRecords;
		const bool bValueStateCaptured =
			CanonicalStateValues.CaptureRecords(
				ValueRecords, CaptureError);
		if (!bValueStateCaptured)
		{
			Hash = HashCombine(Hash, 0xA5F17E3Du);
		}
		else
		{
			Hash = HashCombine(
				Hash, GetTypeHash(CanonicalStateValues.IsSealed()));
			Hash = HashCombine(Hash, GetTypeHash(ValueRecords.Num()));
			for (const FSeinCanonicalStateValueRecord& Record :
				ValueRecords)
			{
				HashGuid(Record.LeafDigest);
			}
		}
	}

	// Authoritative per-entity tag refcounts. Presence alone is insufficient:
	// refcount 1 and 2 behave differently after the next UngrantTag.
	{
		TArray<FSeinEntityHandle> Handles;
		EntityTagStates.GetKeys(Handles);
		Handles.Sort();
		Hash = HashCombine(Hash, GetTypeHash(Handles.Num()));
		for (const FSeinEntityHandle Handle : Handles)
		{
			const FSeinEntityTagState& State =
				EntityTagStates.FindChecked(Handle);
			Hash = HashCombine(Hash, GetTypeHash(Handle));

			TArray<FGameplayTag> BaseTags;
			State.BaseTags.GetGameplayTagArray(BaseTags);
			BaseTags.Sort(TagNameLess);
			Hash = HashCombine(Hash, GetTypeHash(BaseTags.Num()));
			for (const FGameplayTag Tag : BaseTags)
			{
				Hash = HashCombine(Hash, HashCanonicalTag(Tag));
			}

			Hash = HashCombine(
				Hash, GetTypeHash(State.TagRefCounts.Num()));
			HashTagMap(Hash, State.TagRefCounts,
				[](int32 RefCount) { return GetTypeHash(RefCount); });

			// CombinedTags is a derived cache, but hash it independently so a
			// cache/refcount drift is visible at the tick where it occurs.
			TArray<FGameplayTag> CombinedTags;
			State.CombinedTags.GetGameplayTagArray(CombinedTags);
			CombinedTags.Sort(TagNameLess);
			Hash = HashCombine(Hash, GetTypeHash(CombinedTags.Num()));
			for (const FGameplayTag Tag : CombinedTags)
			{
				Hash = HashCombine(Hash, HashCanonicalTag(Tag));
			}
		}
	}

	// Entity tag index — sorted by tag name. Bucket order remains exact because
	// LookupFirstEntityByTag exposes it to gameplay. Hashing this derived index
	// independently also catches index/refcount drift.
	{
		TArray<FGameplayTag> Keys;
		EntityTagIndex.GetKeys(Keys);
		Keys.Sort(TagNameLess);
		for (const FGameplayTag& Tag : Keys)
		{
			Hash = HashCombine(Hash, HashCanonicalTag(Tag));
			const TArray<FSeinEntityHandle>& Bucket = EntityTagIndex[Tag];
			Hash = HashCombine(Hash, GetTypeHash(Bucket.Num()));
			for (const FSeinEntityHandle& H : Bucket)
			{
				Hash = HashCombine(Hash, GetTypeHash(H));
			}
		}
	}

	// Named entity registry — sort by name.
	{
		TArray<FName> Keys;
		NamedEntityRegistry.GetKeys(Keys);
		Keys.Sort([](const FName& A, const FName& B) { return A.Compare(B) < 0; });
		for (const FName& Name : Keys)
		{
			Hash = HashCombine(Hash, HashCanonicalName(Name));
			Hash = HashCombine(Hash, GetTypeHash(NamedEntityRegistry[Name]));
		}
	}

	// Active votes — sort by vote type tag. Inner Votes map sorted by player ID.
	{
		TArray<FGameplayTag> Keys;
		ActiveVotes.GetKeys(Keys);
		Keys.Sort(TagNameLess);
		for (const FGameplayTag& VTag : Keys)
		{
			const FSeinVoteState& V = ActiveVotes[VTag];
			Hash = HashCombine(Hash, HashCanonicalTag(VTag));
			Hash = HashCombine(Hash, GetTypeHash(V.RequiredThreshold));
			Hash = HashCombine(Hash, GetTypeHash(static_cast<uint8>(V.Resolution)));
			Hash = HashCombine(Hash, GetTypeHash(V.InitiatedAtTick));
			Hash = HashCombine(Hash, GetTypeHash(V.ExpiresAtTick));
			Hash = HashCombine(Hash, GetTypeHash(V.Initiator));
			TArray<FSeinPlayerID> Voters;
			V.Votes.GetKeys(Voters);
			Voters.Sort();
			for (const FSeinPlayerID& Voter : Voters)
			{
				Hash = HashCombine(Hash, GetTypeHash(Voter));
				Hash = HashCombine(Hash, GetTypeHash(V.Votes[Voter]));
			}
		}
	}

	// Accepted command tails are future simulation state. Hash their canonical
	// reflected values in queue order; pointer-backed FInstancedStruct storage must
	// never be hashed directly. Standalone pause entries are already authenticated,
	// structurally accepted commands—the later frame cursor assignment is mechanical.
	const auto HashCommandQueue = [&Hash](uint32 LaneMarker,
		const TArray<FSeinCommand>& Commands)
	{
		Hash = HashCombine(Hash, LaneMarker);
		Hash = HashCombine(Hash, GetTypeHash(Commands.Num()));
		for (const FSeinCommand& Command : Commands)
		{
			FGuid Digest;
			FSeinDeterministicValueDigestError Error;
			const ESeinDeterministicValueDigestResult Result =
				FSeinDeterministicValueDigest::Compute(
					FSeinCommand::StaticStruct(), &Command, Digest, &Error,
					MakeRuntimeDigestOptions());
			Hash = HashCombine(Hash, GetTypeHash(static_cast<uint8>(Result)));
			if (Result == ESeinDeterministicValueDigestResult::Success)
			{
				Hash = HashCombine(Hash, GetTypeHash(Digest.A));
				Hash = HashCombine(Hash, GetTypeHash(Digest.B));
				Hash = HashCombine(Hash, GetTypeHash(Digest.C));
				Hash = HashCombine(Hash, GetTypeHash(Digest.D));
			}
			else
			{
				// Fail closed but remain diagnosable if malformed trusted ingress ever
				// reaches the queue before its ordinary dispatcher rejection.
				Hash = HashCombine(
					Hash, HashCanonicalTag(Command.CommandType));
				Hash = HashCombine(Hash, GetTypeHash(Command.SchemaVersion));
				Hash = HashCombine(Hash, GetTypeHash(Command.Tick));
			}
		}
	};
	HashCommandQueue(0x50454e44u, PendingCommands.GetCommands()); // "PEND"
	HashCommandQueue(
		0x52504c59u, PendingReplayCommands.GetCommands()); // "RPLY"
	HashCommandQueue(0x50415553u, PendingStandalonePauseControlCommands); // "PAUS"

	// Pending destroys — order matters if destroys trigger same-tick effects.
	Hash = HashCombine(Hash, GetTypeHash(PendingDestroy.Num()));
	for (const FSeinEntityHandle& H : PendingDestroy)
	{
		Hash = HashCombine(Hash, GetTypeHash(H));
	}

	// Sim PRNG cursor — determinism of any roll-ordered systems depends on
	// it advancing identically on all clients. Hash both state halves.
	Hash = HashCombine(Hash, GetTypeHash(SimRandom.State0));
	Hash = HashCombine(Hash, GetTypeHash(SimRandom.State1));

	// Match + pause flags.
	Hash = HashCombine(Hash, GetTypeHash(SimSessionSeed));
	Hash = HashCombine(Hash, GetTypeHash(CommandProtocolDigest.A));
	Hash = HashCombine(Hash, GetTypeHash(CommandProtocolDigest.B));
	Hash = HashCombine(Hash, GetTypeHash(CommandProtocolDigest.C));
	Hash = HashCombine(Hash, GetTypeHash(CommandProtocolDigest.D));
	Hash = HashCombine(Hash, GetTypeHash(MatchSettingsDigest.A));
	Hash = HashCombine(Hash, GetTypeHash(MatchSettingsDigest.B));
	Hash = HashCombine(Hash, GetTypeHash(MatchSettingsDigest.C));
	Hash = HashCombine(Hash, GetTypeHash(MatchSettingsDigest.D));
	Hash = HashCombine(Hash, GetTypeHash(ConfigFingerprint));
	Hash = HashCombine(Hash, GetTypeHash(static_cast<uint8>(MatchState)));
	Hash = HashCombine(Hash, GetTypeHash(MatchStartTick));
	Hash = HashCombine(Hash, GetTypeHash(StartingStateDeadlineTick));
	Hash = HashCombine(Hash, GetTypeHash(bSimPaused));
	Hash = HashCombine(Hash, GetTypeHash(bSimPausedHard));
	Hash = HashCombine(Hash, GetTypeHash(PauseEpoch));
	Hash = HashCombine(Hash, GetTypeHash(PauseFrozenTick));
	Hash = HashCombine(Hash, GetTypeHash(LastAppliedPauseControlSequence));

	return static_cast<int32>(Hash);
}

// ==================== Ability Initialization ====================

void USeinWorldSubsystem::InitializeEntityAbilities(FSeinEntityHandle Handle)
{
	FSeinAbilityComponent* AbilityComp =
		GetComponentMutable<FSeinAbilityComponent>(Handle);
	if (!AbilityComp)
	{
		// Not every entity has abilities (projectiles, static props, resource piles);
		// this is expected, not an error.
		return;
	}

	// Snapshot the authored list — `SeinGrantAbility` mutates
	// `GrantedAbilities` (via `AddUnique`), and iterating the live list
	// while it's being modified would risk reading the just-added element
	// twice. Snapshot is cheap (typically <20 classes).
	const TArray<TSubclassOf<USeinAbility>> AuthoredClasses = AbilityComp->GrantedAbilities;

	// Route through the BPFL so refcount semantics seed correctly: each
	// natively-authored class lands at refcount=1, which subsequent
	// effect-driven grants bump to 2/3/etc., and effect-revokes can
	// decrement back toward 1 without ever destroying the native grant.
	// Also dedupes against authoring mistakes (same class listed twice in
	// the editor) — second grant is idempotent + becomes refcount=2 which
	// is benign at spawn.
	for (const TSubclassOf<USeinAbility>& AbilityClass : AuthoredClasses)
	{
		if (!AbilityClass)
		{
			UE_LOG(LogSeinSim, Warning, TEXT("Entity %s: null ability class in GrantedAbilities"),
				*Handle.ToString());
			continue;
		}
		USeinAbilityBPFL::SeinGrantAbility(this, Handle, AbilityClass);
	}
}

void USeinWorldSubsystem::ReplayEffectAbilityGrants(FSeinEntityHandle Handle)
{
	// A unit spawned AFTER the player completed a tech effect that grants
	// abilities should pick up those grants on spawn — otherwise the
	// effect-driven grant pattern only works for entities that existed at
	// research-complete time. Walk the owner's active Class/Player effect
	// storage; for each whose GrantedAbilities are non-empty and whose
	// AbilityTargetClassTag matches a tag on this brand-new entity, run
	// SeinGrantAbility.
	//
	// MUST be called AFTER `SeedEntityTagsFromBase` (or equivalent) — the
	// AbilityTargetClassTag check reads the entity's tag state, which is
	// only meaningful after BaseTags / identity tag have been seeded.
	// InitializeEntityAbilities runs before tag seeding (so passives can
	// fire OnActivate without depending on BaseTags being present), so we
	// keep this as a SEPARATE post-seed step.
	if (!Handle.IsValid()) return;

	// AbilityComponent is the gate — entities without one can't hold
	// abilities, so there's nothing to grant.
	if (!GetComponent<FSeinAbilityComponent>(Handle)) return;

	const FSeinPlayerID Owner = GetEntityOwner(Handle);
	const FSeinPlayerState* OwnerState = GetPlayerState(Owner);
	if (!OwnerState) return;
	const uint64 InitialOwnerRevision = OwnerTransitionRevisions.FindRef(Handle);

	// Snapshot stable storage locators before any passive callback. IDs are sorted
	// by commit order, then each locator is re-resolved immediately before use;
	// removing the current or a later effect cannot invalidate iteration.
	TArray<FEffectLocator> EffectLocators;
	EffectLocators.Reserve(OwnerState->ClassEffects.Num() + OwnerState->PlayerEffects.Num());
	for (const FSeinActiveEffect& Active : OwnerState->ClassEffects)
	{
		FEffectLocator& Locator = EffectLocators.AddDefaulted_GetRef();
		Locator.Scope = ESeinModifierScope::Class;
		Locator.PlayerID = Owner;
		Locator.EffectInstanceID = Active.EffectInstanceID;
	}
	for (const FSeinActiveEffect& Active : OwnerState->PlayerEffects)
	{
		FEffectLocator& Locator = EffectLocators.AddDefaulted_GetRef();
		Locator.Scope = ESeinModifierScope::Player;
		Locator.PlayerID = Owner;
		Locator.EffectInstanceID = Active.EffectInstanceID;
	}
	EffectLocators.Sort([](const FEffectLocator& A, const FEffectLocator& B)
	{
		return A.EffectInstanceID < B.EffectInstanceID;
	});

	for (const FEffectLocator& Locator : EffectLocators)
	{
		const FSeinEntity* RecipientEntity = EntityPool.Get(Handle);
		if (!RecipientEntity || !RecipientEntity->IsAlive()
			|| GetEntityOwner(Handle) != Owner
			|| OwnerTransitionRevisions.FindRef(Handle) != InitialOwnerRevision
			|| !GetComponent<FSeinAbilityComponent>(Handle))
		{
			break;
		}

		FSeinActiveEffect* Active = ResolveEffect(Locator);
		if (!Active) continue;
		const USeinEffect* CDO = Active->EffectClass
			? GetDefault<USeinEffect>(Active->EffectClass)
			: nullptr;
		if (!CDO || (CDO->Scope != ESeinModifierScope::Class
				&& CDO->Scope != ESeinModifierScope::Player)
			|| !CDO->AbilityTargetClassTag.IsValid()
			|| !HasTag(Handle, CDO->AbilityTargetClassTag))
		{
			continue;
		}

		const TArray<TSubclassOf<USeinAbility>> AbilityClasses = CDO->GrantedAbilities;
		for (const TSubclassOf<USeinAbility>& AbilityClass : AbilityClasses)
		{
			if (AbilityClass
				&& !GrantAbilityTrackedByEffect(Locator, Handle, AbilityClass))
			{
				break;
			}
			RecipientEntity = EntityPool.Get(Handle);
			if (!RecipientEntity || !RecipientEntity->IsAlive()
				|| GetEntityOwner(Handle) != Owner
				|| OwnerTransitionRevisions.FindRef(Handle) != InitialOwnerRevision)
			{
				return;
			}
		}
	}
}

// ==================== Tag seeding / unindexing ====================

void USeinWorldSubsystem::SeedEntityTagsFromBase(FSeinEntityHandle Handle)
{
	// Ensure the tag-state entry exists first (creates an empty record if
	// the entity is brand-new and hasn't had any tags touched yet).
	FSeinEntityTagState& TagState = EntityTagStates.FindOrAdd(Handle);
	MarkCanonicalAuxiliaryStateDirty();

	// Snapshot first — GrantTag doesn't touch BaseTags, but a stable
	// iteration source is cheap and makes the intent obvious.
	TArray<FGameplayTag> SeedTags;
	TagState.BaseTags.GetGameplayTagArray(SeedTags);
	for (const FGameplayTag& Tag : SeedTags)
	{
		GrantTag(Handle, Tag);
	}
}

void USeinWorldSubsystem::UnindexEntityTags(FSeinEntityHandle Handle)
{
	const FSeinEntityTagState* TagState = EntityTagStates.Find(Handle);
	if (!TagState) return;

	for (const TPair<FGameplayTag, int32>& Pair : TagState->TagRefCounts)
	{
		if (Pair.Value <= 0) continue;
		if (TArray<FSeinEntityHandle>* Bucket = EntityTagIndex.Find(Pair.Key))
		{
			Bucket->RemoveSingle(Handle);
			if (Bucket->Num() == 0)
			{
				EntityTagIndex.Remove(Pair.Key);
			}
		}
	}

	// Free the tag-state entry — entity is being destroyed.
	EntityTagStates.Remove(Handle);
	MarkCanonicalAuxiliaryStateDirty();
}

void USeinWorldSubsystem::UnregisterHandleFromNames(FSeinEntityHandle Handle)
{
	bool bRemovedAny = false;
	for (auto It = NamedEntityRegistry.CreateIterator(); It; ++It)
	{
		if (It.Value() == Handle)
		{
			It.RemoveCurrent();
			bRemovedAny = true;
		}
	}
	if (bRemovedAny)
	{
		MarkCanonicalAuxiliaryStateDirty();
	}
}

// ==================== CommandBroker helpers (DESIGN §5) ====================

FSeinEntityHandle USeinWorldSubsystem::FindSharedBroker(const TArray<FSeinEntityHandle>& Members) const
{
	if (Members.Num() == 0) return FSeinEntityHandle::Invalid();

	FSeinEntityHandle Shared;
	for (const FSeinEntityHandle& M : Members)
	{
		const FSeinBrokerMembershipData* Memb = GetComponent<FSeinBrokerMembershipData>(M);
		if (!Memb || !Memb->CurrentBrokerHandle.IsValid())
		{
			return FSeinEntityHandle::Invalid();
		}
		if (!Shared.IsValid())
		{
			Shared = Memb->CurrentBrokerHandle;
		}
		else if (Shared != Memb->CurrentBrokerHandle)
		{
			return FSeinEntityHandle::Invalid();
		}
	}
	return Shared;
}

FSeinEntityHandle USeinWorldSubsystem::CreateBrokerForMembers(
	const TArray<FSeinEntityHandle>& FilteredMembers,
	FSeinPlayerID OwnerPlayerID,
	const FSeinBrokerQueuedOrder& FirstOrder)
{
	if (!RequireStateMutationAuthorization(TEXT("CreateBrokerForMembers")))
	{
		return FSeinEntityHandle::Invalid();
	}
	if (FilteredMembers.Num() == 0) return FSeinEntityHandle::Invalid();

	// 1. Evict each member from its prior broker (one-broker-per-member invariant).
	//    When a player issues a new (non-shift) order, the old broker's in-flight
	//    work for this member needs to terminate cleanly — without this the
	//    member's old active ability (e.g. Move toward the previous destination)
	//    keeps running alongside whatever the new broker dispatches, producing
	//    the "two competing orders" symptom the user reported.
	for (const FSeinEntityHandle& M : FilteredMembers)
	{
		if (!IsEntityAlive(M)) continue;
		FSeinBrokerMembershipData* Memb =
			GetComponentMutable<FSeinBrokerMembershipData>(M);
		if (!Memb || !Memb->CurrentBrokerHandle.IsValid()) continue;
		const FSeinEntityHandle OldBrokerHandle = Memb->CurrentBrokerHandle;
		if (!EntityPool.IsValid(OldBrokerHandle)) continue;
		FSeinCommandBrokerData* OldBroker =
			GetComponentMutable<FSeinCommandBrokerData>(
				OldBrokerHandle);
		if (!OldBroker) continue;

		// Cancel the member's active primary ability before evicting. The active
		// ability tracked on FSeinAbilityComponent was dispatched by this broker —
		// once we evict, the broker no longer owns it. Cancellation runs the
		// ability's OnEnd cleanup (latent-action teardown, refunds, tag ungrant).
		// Safe no-op if the member has no active ability.
		if (FSeinAbilityComponent* AC =
			GetComponentMutable<FSeinAbilityComponent>(M))
		{
			const int32 ActiveID = AC->ActiveAbilityID;
			USeinAbility* Active = GetAbilityInstance(ActiveID);
			if (AC->AbilityInstanceIDs.Contains(ActiveID)
				&& Active
				&& Active->OwnerEntity == M)
			{
				if (Active->bIsActive)
				{
					Active->CancelAbility();
				}
			}
		}

		// Ability callbacks may replace component storage or destroy the old
		// broker. Re-resolve the snapshotted handle before touching its members.
		OldBroker = IsEntityAlive(OldBrokerHandle)
			? GetComponentMutable<FSeinCommandBrokerData>(OldBrokerHandle)
			: nullptr;
		if (!OldBroker) continue;
		OldBroker->Members.Remove(M);
		OldBroker->bCapabilityMapDirty = true;

		// Cull the old broker if it now has no members. A member-less broker
		// can't dispatch its remaining queue anyway — keeping it alive just
		// leaks abstract entities. Relaxed from the previous condition (which
		// also required queue=0 + !bIsExecuting) since neither matters once
		// Members.Num() is zero.
		if (OldBroker->bSelfCullOnEmpty && OldBroker->Members.Num() == 0)
		{
			DestroyEntity(OldBrokerHandle);
		}
	}

	TArray<FSeinEntityHandle> LiveMembers;
	LiveMembers.Reserve(FilteredMembers.Num());
	for (const FSeinEntityHandle& M : FilteredMembers)
	{
		if (IsEntityAlive(M))
		{
			LiveMembers.Add(M);
		}
	}
	if (LiveMembers.Num() == 0)
	{
		return FSeinEntityHandle::Invalid();
	}

	// 2. Compute initial centroid.
	FFixedVector InitialCentroid;
	int32 CentroidCount = 0;
	for (const FSeinEntityHandle& M : LiveMembers)
	{
		if (const FSeinEntity* E = GetEntity(M))
		{
			InitialCentroid += E->Transform.GetLocation();
			++CentroidCount;
		}
	}
	if (CentroidCount > 0)
	{
		InitialCentroid = InitialCentroid / FFixedPoint::FromInt(CentroidCount);
	}

	// Resolver class resolution (WYSIWYG). None/empty => loose-unit + smart-command dispatch is OFF:
	// create no broker at all, so the order simply doesn't dispatch. A set-but-unloadable/abstract
	// class is a mistake, not an off-switch: fall back to the shipped default. Resolved BEFORE the
	// spawn (step 3) so an off broker never leaks an orphan abstract entity.
	TSubclassOf<USeinCommandBrokerResolver> ResolverClass;
	if (const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>())
	{
		if (!Settings->DefaultBrokerResolverClass.IsNull())
		{
			ResolverClass = Settings->DefaultBrokerResolverClass.LoadSynchronous();
			if (!ResolverClass || ResolverClass->HasAnyClassFlags(CLASS_Abstract))
			{
				UE_LOG(LogSeinSim, Error,
					TEXT("DefaultBrokerResolverClass '%s' could not be loaded or is abstract — falling back to the shipped default."),
					*Settings->DefaultBrokerResolverClass.ToString());
				ResolverClass = USeinDefaultCommandBrokerResolver::StaticClass();
			}
		}
	}
	if (!ResolverClass)
	{
		// Broker resolver is None → dispatch off. The prior broker was already evicted above, so the
		// new order still cancels the member's previous one; we simply create nothing new.
		USeinARTSCoreSettings::ReportDisabledSystem(TEXT("Broker Resolver"),
			TEXT("Loose-unit group orders and single-unit smart commands (move-then-attack) won't dispatch. Plain single moves and squads are unaffected."), /*bHighSeverity*/ true);
		return FSeinEntityHandle::Invalid();
	}

	// 3. Spawn the abstract broker entity.
	FSeinEntityHandle BrokerHandle = SpawnAbstractEntity(FFixedTransform(InitialCentroid), OwnerPlayerID);
	if (!BrokerHandle.IsValid()) return FSeinEntityHandle::Invalid();

	// 4. Build + inject FSeinCommandBrokerData with the first order pre-queued.
	FSeinCommandBrokerData BrokerData;
	BrokerData.Members = LiveMembers;
	BrokerData.Centroid = InitialCentroid;
	BrokerData.Anchor = FirstOrder.TargetLocation;
	BrokerData.OrderQueue.Add(FirstOrder);
	BrokerData.bCapabilityMapDirty = true;

	// Phase 4 architecture: resolver is registered in the world's resolver
	// pool; component stores the int32 ID, not a TObjectPtr.
	USeinCommandBrokerResolver* ResolverInstance = NewObject<USeinCommandBrokerResolver>(this, ResolverClass);
	BrokerData.ResolverID = RegisterCommandBrokerResolver(ResolverInstance);

	AddComponent(BrokerHandle, BrokerData);

	// 5. Update each member's back-reference. Create the component if missing.
	for (const FSeinEntityHandle& M : LiveMembers)
	{
		FSeinBrokerMembershipData* Memb =
			GetComponentMutable<FSeinBrokerMembershipData>(M);
		if (Memb)
		{
			Memb->CurrentBrokerHandle = BrokerHandle;
		}
		else
		{
			FSeinBrokerMembershipData NewMemb;
			NewMemb.CurrentBrokerHandle = BrokerHandle;
			AddComponent(M, NewMemb);
		}
	}

	// 6. Inline-dispatch the first order (skips the 1-tick delay of waiting for
	// SeinCommandBrokerSystem's PostTick pass). Subsequent queue advancement runs
	// through the system. Under per-order parallelism the "first order" is at
	// index 0 — DispatchOrderAtIndex sets that order's bIsExecuting + stamps
	// LastDispatchTick so the system's completion check picks it up next tick.
	SeinCommandBrokerDispatch::DispatchOrderAtIndex(*this, BrokerHandle, 0);

	return BrokerHandle;
}

// ==================== Match Flow (DESIGN §18) ====================

void USeinWorldSubsystem::SetSimPaused(bool bPaused, bool bRejectCommandsWhilePaused)
{
	if (!RequireStateMutationAuthorization(TEXT("SetSimPaused")))
	{
		return;
	}
	const bool bWasPaused = bSimPaused;
	if (bDispatchingPauseControlFrame && bWasPaused != bPaused)
	{
		const bool bUnpauseBeforeFinalCommand = !bPaused
			&& ActivePauseControlCommandIndex != ActivePauseControlCommandCount - 1;
		const bool bReenterPause = bPaused;
		if (bUnpauseBeforeFinalCommand || bReenterPause)
		{
			bPauseControlDispatchProtocolViolation = true;
			UE_LOG(LogSeinSim, Error,
				TEXT("Pause-control protocol violation at command %d/%d: unpause is legal only from the final command and re-entering pause is forbidden."),
				ActivePauseControlCommandIndex + 1,
				ActivePauseControlCommandCount);
			return;
		}
	}
	if (bPaused && !bWasPaused && PauseEpoch == MAX_int64)
	{
		UE_LOG(LogSeinSim, Error,
			TEXT("SetSimPaused refused: pause epoch space is exhausted."));
		return;
	}
	bSimPaused = bPaused;
	bSimPausedHard = bPaused && bRejectCommandsWhilePaused;

	// Fire match-flow visual events for UI / scenario subscribers. Suppress
	// double-fires when the state didn't actually change.
	if (bWasPaused != bPaused)
	{
		PendingStandalonePauseControlCommands.Reset();
		if (bPaused)
		{
			++PauseEpoch;
			PauseFrozenTick = CurrentTick;
			LastAppliedPauseControlSequence = -1;
			if (MatchState == ESeinMatchState::Playing)
			{
				MatchState = ESeinMatchState::Paused;
			}
			EnqueueVisualEvent(FSeinVisualEvent::MakeMatchFlowEvent(ESeinVisualEventType::MatchPaused));
		}
		else
		{
			if (MatchState == ESeinMatchState::Paused)
			{
				MatchState = ESeinMatchState::Playing;
			}
			EnqueueVisualEvent(FSeinVisualEvent::MakeMatchFlowEvent(ESeinVisualEventType::MatchResumed));
		}
	}
}

void USeinWorldSubsystem::StartMatch(const FSeinMatchSettings& Settings)
{
	if (MatchBootstrapState != ESeinMatchBootstrapState::Applying
		|| bIsRunning || CurrentTick != 0)
	{
		UE_LOG(LogSeinSim, Error,
			TEXT("StartMatch rejected outside stopped tick-zero bootstrap Applying."));
		return;
	}
	if (!RequireStateMutationAuthorization(TEXT("StartMatch")))
	{
		return;
	}
	if (MatchState != ESeinMatchState::Lobby)
	{
		UE_LOG(LogSeinSim, Warning, TEXT("StartMatch: ignored — match state is not Lobby (%d)"),
			static_cast<int32>(MatchState));
		return;
	}

	// Validate direct callers through the same registered wire contract used by
	// command dispatch. Match bootstrap is allowed to install settings before the
	// sim runs, but it is not allowed to bypass deterministic payload rules.
	FGameplayTag RejectionReason;
	if (!ValidateMatchSettings(Settings, RejectionReason))
	{
		UE_LOG(LogSeinSim, Error,
			TEXT("StartMatch refused malformed/non-deterministic settings (%s)."),
			*RejectionReason.ToString());
		return;
	}

	// Canonical order is part of the match contract. Slot iteration allocates
	// player/entity state, while extension order feeds manifests and hashes; a
	// lobby/UI insertion order must not influence either.
	FSeinMatchSettings CanonicalSettings = Settings;
	FGuid CanonicalSettingsDigest;
	FSeinDeterministicValueDigestError DigestError;
	if (!SeinCanonicalizeAndDigestMatchSettings(
			CanonicalSettings, CanonicalSettingsDigest, &DigestError))
	{
		UE_LOG(LogSeinSim, Error,
			TEXT("StartMatch refused settings that cannot be canonically digested (%s: %s)."),
			*DigestError.FieldPath, *DigestError.Message);
		return;
	}
	CurrentMatchSettings = MoveTemp(CanonicalSettings);
	MatchSettingsDigest = CanonicalSettingsDigest;
	bSimPaused = false;
	bSimPausedHard = false;
	PendingStandalonePauseControlCommands.Reset();
	MatchStartTick = 0;
	MatchState = ESeinMatchState::Starting;

	// Framework no longer ships a pre-match countdown — `Starting` transitions
	// to `Playing` on the next tick (effectively instant). Designer who wants
	// a UI countdown delays calling StartMatch from their lobby BP for the
	// desired duration. Setting deadline = CurrentTick guarantees the
	// transition fires on the very next tick boundary.
	StartingStateDeadlineTick = CurrentTick;

	EnqueueVisualEvent(FSeinVisualEvent::MakeMatchFlowEvent(ESeinVisualEventType::MatchStarting));
	UE_LOG(LogSeinSim, Log, TEXT("StartMatch: Starting state begins, deadline at tick %d"), StartingStateDeadlineTick);
}

void USeinWorldSubsystem::EndMatch(FSeinPlayerID Winner, FGameplayTag Reason)
{
	if (!RequireStateMutationAuthorization(TEXT("EndMatch")))
	{
		return;
	}
	if (MatchState == ESeinMatchState::Ended || MatchState == ESeinMatchState::Lobby)
	{
		return; // nothing in-flight to end
	}
	MatchState = ESeinMatchState::Ending;
	EnqueueVisualEvent(FSeinVisualEvent::MakeMatchFlowEvent(ESeinVisualEventType::MatchEnding, Winner, Reason));

	// Immediate Ending → Ended transition in V1; designers that want a staged
	// cleanup phase (fade-out cinematic, score screen pause) can schedule work
	// during Ending via scenario abilities, then route to a follow-up command
	// to finalize. Minimal polish can land when the first real game asks.
	MatchState = ESeinMatchState::Ended;
	EnqueueVisualEvent(FSeinVisualEvent::MakeMatchFlowEvent(ESeinVisualEventType::MatchEnded, Winner, Reason));
	UE_LOG(LogSeinSim, Log, TEXT("EndMatch: Winner=%s Reason=%s"),
		*Winner.ToString(), *Reason.ToString());
}

void USeinWorldSubsystem::TickMatchState()
{
	if (MatchState == ESeinMatchState::Starting)
	{
		if (CurrentTick >= StartingStateDeadlineTick)
		{
			MatchState = ESeinMatchState::Playing;
			MatchStartTick = CurrentTick;
			EnqueueVisualEvent(FSeinVisualEvent::MakeMatchFlowEvent(ESeinVisualEventType::MatchStarted));
			UE_LOG(LogSeinSim, Log, TEXT("Match transitioned to Playing at tick %d"), CurrentTick);
		}
	}
}

// ==================== Voting (DESIGN §18) ====================

void USeinWorldSubsystem::StartVote(FGameplayTag VoteType, ESeinVoteResolution Resolution, int32 RequiredThreshold, int32 ExpiresInTicks, FSeinPlayerID Initiator)
{
	if (!RequireStateMutationAuthorization(TEXT("StartVote")))
	{
		return;
	}
	if (!VoteType.IsValid()) return;
	if (ExpiresInTicks > 0 && ExpiresInTicks > MAX_int32 - CurrentTick)
	{
		UE_LOG(LogSeinSim, Warning,
			TEXT("StartVote: expiration overflows the simulation tick domain."));
		return;
	}
	if (ActiveVotes.Contains(VoteType))
	{
		UE_LOG(LogSeinSim, Warning, TEXT("StartVote: vote %s already active"), *VoteType.ToString());
		return;
	}
	FSeinVoteState Vote;
	Vote.VoteType = VoteType;
	Vote.Resolution = Resolution;
	Vote.RequiredThreshold = FMath::Max(1, RequiredThreshold);
	Vote.InitiatedAtTick = CurrentTick;
	Vote.ExpiresAtTick = (ExpiresInTicks > 0) ? CurrentTick + ExpiresInTicks : INT32_MAX;
	Vote.Initiator = Initiator;
	ActiveVotes.Add(VoteType, MoveTemp(Vote));
	MarkCanonicalAuxiliaryStateDirty();

	EnqueueVisualEvent(FSeinVisualEvent::MakeVoteStartedEvent(VoteType, Initiator, RequiredThreshold));
}

void USeinWorldSubsystem::CastVote(FGameplayTag VoteType, FSeinPlayerID Voter, int32 VoteValue)
{
	if (!RequireStateMutationAuthorization(TEXT("CastVote")))
	{
		return;
	}
	if (!VoteType.IsValid()) return;
	FSeinVoteState* Vote = ActiveVotes.Find(VoteType);
	if (!Vote) return;
	MarkCanonicalAuxiliaryStateDirty();
	Vote->Votes.Add(Voter, VoteValue);

	int32 Yes = 0, No = 0;
	for (const auto& Pair : Vote->Votes) { (Pair.Value > 0) ? ++Yes : ++No; }
	EnqueueVisualEvent(FSeinVisualEvent::MakeVoteProgressEvent(VoteType, Yes, No));

	EvaluateAndResolveVote(VoteType);
}

ESeinVoteStatus USeinWorldSubsystem::GetVoteStatus(FGameplayTag VoteType) const
{
	if (!VoteType.IsValid()) return ESeinVoteStatus::NotStarted;
	return ActiveVotes.Contains(VoteType) ? ESeinVoteStatus::Active : ESeinVoteStatus::NotStarted;
}

TArray<FSeinVoteState> USeinWorldSubsystem::GetActiveVotes() const
{
	TArray<FSeinVoteState> Out;
	Out.Reserve(ActiveVotes.Num());
	for (const auto& Pair : ActiveVotes) { Out.Add(Pair.Value); }
	return Out;
}

bool USeinWorldSubsystem::EvaluateAndResolveVote(FGameplayTag VoteType)
{
	FSeinVoteState* Vote = ActiveVotes.Find(VoteType);
	if (!Vote) return false;

	int32 Yes = 0, No = 0;
	for (const auto& Pair : Vote->Votes) { (Pair.Value > 0) ? ++Yes : ++No; }

	// Default electorate is every registered, non-neutral active participant.
	// Spectators observe votes but do not change their threshold.
	int32 Eligible = 0;
	for (const auto& Pair : PlayerStates)
	{
		if (Pair.Key.IsValid() && !Pair.Value.bIsSpectator)
		{
			++Eligible;
		}
	}
	Eligible = FMath::Max(1, Eligible);

	bool bPassed = false;
	bool bResolveNow = false;
	switch (Vote->Resolution)
	{
	case ESeinVoteResolution::Majority:
		if (Yes * 2 > Eligible) { bPassed = true; bResolveNow = true; }
		else if (Yes + No >= Eligible) { bResolveNow = true; bPassed = false; }
		break;
	case ESeinVoteResolution::Unanimous:
		if (Yes >= Eligible) { bPassed = true; bResolveNow = true; }
		else if (No > 0) { bResolveNow = true; bPassed = false; }
		break;
	case ESeinVoteResolution::HostDecides:
		// V1: any "yes" passes, any "no" fails. Host designation lands with
		// §18 match-flow network plumbing; until then treat first vote as decisive.
		if (Yes > 0) { bPassed = true; bResolveNow = true; }
		else if (No > 0) { bResolveNow = true; bPassed = false; }
		break;
	case ESeinVoteResolution::Plurality:
		if (Yes + No >= Eligible)
		{
			bResolveNow = true;
			bPassed = (Yes > No);
		}
		break;
	}

	// Also check the explicit threshold (overrides resolution if passed first).
	if (!bResolveNow && Yes >= Vote->RequiredThreshold)
	{
		bResolveNow = true;
		bPassed = true;
	}

	if (bResolveNow)
	{
		const FGameplayTag Resolved = Vote->VoteType;
		EnqueueVisualEvent(FSeinVisualEvent::MakeVoteResolvedEvent(Resolved, bPassed));
		ActiveVotes.Remove(VoteType);
		MarkCanonicalAuxiliaryStateDirty();
		return true;
	}
	return false;
}

void USeinWorldSubsystem::TickVotes()
{
	if (ActiveVotes.Num() == 0) return;
	TArray<FGameplayTag> Expired;
	for (const auto& Pair : ActiveVotes)
	{
		if (CurrentTick >= Pair.Value.ExpiresAtTick) { Expired.Add(Pair.Key); }
	}
	for (const FGameplayTag& Tag : Expired)
	{
		// Expired votes that haven't passed on their own fail deterministically.
		EnqueueVisualEvent(FSeinVisualEvent::MakeVoteResolvedEvent(Tag, /*bPassed=*/false));
		ActiveVotes.Remove(Tag);
		MarkCanonicalAuxiliaryStateDirty();
	}
}

// ==================== AI (DESIGN §16) ====================

bool USeinWorldSubsystem::RouteAICommandFromController(
	USeinAIController* Controller,
	const FSeinCommand& Command)
{
	SEIN_CHECK_NOT_PARALLEL();
	if (!Controller
		|| ActiveAICommandEmitter != Controller
		|| Controller->WorldSubsystem != this
		|| !AIControllers.Contains(Controller))
	{
		UE_LOG(LogSeinAI, Error,
			TEXT("EmitCommand rejected: %s is not the exact active registered AI tick callback."),
			*GetNameSafe(Controller));
		return false;
	}
	if (MatchBootstrapState != ESeinMatchBootstrapState::Consumed
		|| !bIsRunning)
	{
		UE_LOG(LogSeinAI, Error,
			TEXT("EmitCommand rejected for AI slot %s before the match is launched and running."),
			*Controller->OwnedPlayerID.ToString());
		return false;
	}
	if (bObserverCallbackInProgress)
	{
		UE_LOG(LogSeinAI, Error,
			TEXT("EmitCommand rejected for AI slot %s from a read-only observer."),
			*Controller->OwnedPlayerID.ToString());
		return false;
	}

	// The topology adapter owns networked AI authentication. Once its hook is
	// present, declining a command must fail closed: applying it only to the
	// host's sim would silently desync every peer.
	if (AIEmitInterceptor.IsBound())
	{
		if (!AIEmitInterceptor.Execute(Controller->OwnedPlayerID, Command))
		{
			UE_LOG(LogSeinAI, Error,
				TEXT("EmitCommand: active topology adapter declined AI slot %s command; dropping instead of applying host-only."),
				*Controller->OwnedPlayerID.ToString());
			return false;
		}
		return true;
	}

	// A local command submitter without the AI authority hook is an incomplete
	// network binding, not Standalone. Never misattribute the AI command to the
	// local human participant.
	if (LocalCommandSubmitter.IsBound())
	{
		UE_LOG(LogSeinAI, Error,
			TEXT("EmitCommand: topology adapter is active without an AI authority hook; dropping slot %s command."),
			*Controller->OwnedPlayerID.ToString());
		return false;
	}

	FSeinCommand Draft = Command;
	Draft.PlayerID = Controller->OwnedPlayerID;
	SubmitLocalCommandDraft(Draft);
	return true;
}

void USeinWorldSubsystem::RegisterAIController(USeinAIController* Controller, FSeinPlayerID OwnedPlayer)
{
	if (!Controller) return;
	if (bExecutionTopologyTeardown || bModuleUnloadStateReleased)
	{
		UE_LOG(LogSeinAI, Warning,
			TEXT("RegisterAIController rejected on a terminal world."));
		return;
	}
	const bool bAlreadyRegistered = AIControllers.Contains(Controller);
	if (bAlreadyRegistered
		&& Controller->OwnedPlayerID == OwnedPlayer
		&& Controller->WorldSubsystem == this)
	{
		return;
	}

	// Treat an ownership change as a balanced lifecycle transition. Reuse the
	// ordinary teardown path so designer callbacks see the old player/world
	// context and the tick list is already safe from the retiring registration.
	if (bAlreadyRegistered)
	{
		UnregisterAIController(Controller);
	}
	AIControllers.Add(Controller);
	Controller->OwnedPlayerID = OwnedPlayer;
	Controller->WorldSubsystem = this;
	{
		TGuardValue<bool> ReadOnlyGuard(bReadOnlyCallbackInProgress, true);
		TGuardValue<bool> ObserverGuard(bObserverCallbackInProgress, true);
		Controller->OnRegistered();
	}
	UE_LOG(LogSeinSim, Log, TEXT("Registered AI controller %s for player %s"),
		*Controller->GetName(), *OwnedPlayer.ToString());
}

void USeinWorldSubsystem::UnregisterAIController(USeinAIController* Controller)
{
	if (!Controller) return;
	const int32 Removed = AIControllers.Remove(Controller);
	if (Removed > 0)
	{
		TGuardValue<bool> ReadOnlyGuard(bReadOnlyCallbackInProgress, true);
		TGuardValue<bool> ObserverGuard(bObserverCallbackInProgress, true);
		Controller->OnUnregistered();
		Controller->WorldSubsystem = nullptr;
	}
}

USeinAIController* USeinWorldSubsystem::GetAIControllerForPlayer(FSeinPlayerID PlayerID) const
{
	for (const TObjectPtr<USeinAIController>& Ctrl : AIControllers)
	{
		if (Ctrl && Ctrl->OwnedPlayerID == PlayerID)
		{
			return Ctrl;
		}
	}
	return nullptr;
}

void USeinWorldSubsystem::TickAIControllers(FFixedPoint DeltaTime)
{
	// A replay journal is the only external command authority during playback.
	// Skipping the controller tick also prevents designer AI from advancing
	// private decision state while its duplicate emissions are suppressed.
	if (bReplayOwnsExternalCommandIngress) return;

	// Snapshot the list so Tick callbacks that register/unregister don't crash
	// the iteration; pending removals take effect next tick.
	TArray<TObjectPtr<USeinAIController>> Snapshot = AIControllers;

	// DETERMINISM: sort by OwnedPlayerID before ticking. Registration order depends
	// on actor spawn order + BeginPlay sequencing, which can differ across clients.
	// PlayerIDs are globally agreed (registered via RegisterPlayer); sorting by
	// PlayerID.Value pins tick order network-wide. If two controllers somehow share
	// a PlayerID, we fall back to pointer-index stability — indeterminate but rare
	// (would be a misconfiguration; log as warning).
	// UE 5.7 quirk: TArray<TObjectPtr<>>::StableSort dereferences via
	// TDereferenceWrapper before invoking the lambda, so the lambda's params
	// must be raw `T*` (not `const TObjectPtr<T>&`).
	Snapshot.StableSort([](const USeinAIController& A, const USeinAIController& B)
	{
		return A.OwnedPlayerID < B.OwnedPlayerID;
	});

	for (const TObjectPtr<USeinAIController>& Ctrl : Snapshot)
	{
		if (!Ctrl) continue;
		FSeinAITickContext Ctx;
		Ctx.CurrentTick = CurrentTick;
		Ctx.DeltaTime = DeltaTime;
		Ctx.OwnedPlayerID = Ctrl->OwnedPlayerID;
		TGuardValue<bool> ReadOnlyGuard(bReadOnlyCallbackInProgress, true);
		TGuardValue<USeinAIController*> EmitterGuard(
			ActiveAICommandEmitter, Ctrl.Get());
		Ctrl->Tick(Ctx);
	}
}

// ==================== Pair capabilities and containment ====================

namespace
{
	// Assign the first invalid/free visual-slot index in a container's
	// TotalCapacity-sized VisualSlotAssignments array. Growing the array lazily
	// keeps cost proportional to actual occupant count; TotalCapacity just caps
	// the search. Returns INDEX_NONE if every slot is filled.
	int32 AssignFirstFreeVisualSlot(FSeinContainmentData& Container, FSeinEntityHandle Occupant)
	{
		if (!Container.bTracksVisualSlots) return INDEX_NONE;
		if (Container.VisualSlotAssignments.Num() < Container.TotalCapacity)
		{
			Container.VisualSlotAssignments.SetNum(Container.TotalCapacity);
		}
		for (int32 i = 0; i < Container.VisualSlotAssignments.Num(); ++i)
		{
			if (!Container.VisualSlotAssignments[i].IsValid())
			{
				Container.VisualSlotAssignments[i] = Occupant;
				return i;
			}
		}
		return INDEX_NONE;
	}
}

bool USeinWorldSubsystem::ValidateContainmentState(FString& OutError) const
{
	TArray<FSeinEntityHandle> Entities;
	Entities.Reserve(EntityPool.GetActiveCount());
	EntityPool.ForEachEntity(
		[&Entities](FSeinEntityHandle Handle, const FSeinEntity&)
		{
			Entities.Add(Handle);
		});
	return UE::SeinARTSCoreEntity::ValidateContainmentState(
		Entities,
		[this](FSeinEntityHandle Handle)
		{
			return EntityPool.IsValid(Handle);
		},
		[this](FSeinEntityHandle Handle)
		{
			return GetComponent<FSeinContainmentData>(Handle);
		},
		[this](FSeinEntityHandle Handle)
		{
			return GetComponent<FSeinContainmentMemberData>(Handle);
		},
		[this](FSeinEntityHandle Handle)
		{
			return GetComponent<FSeinAttachmentSpec>(Handle);
		},
		OutError);
}

bool USeinWorldSubsystem::EnterContainer(FSeinEntityHandle Entity, FSeinEntityHandle Container)
{
	if (!RequireStateMutationAuthorization(TEXT("EnterContainer")))
	{
		return false;
	}
	if (!EntityPool.IsValid(Entity) || !EntityPool.IsValid(Container))
	{
		return false;
	}
	if (Entity == Container)
	{
		UE_LOG(LogSeinSim, Warning, TEXT("EnterContainer: entity %s cannot enter itself"), *Entity.ToString());
		return false;
	}

	FSeinContainmentMemberData* MemComp =
		GetComponentMutable<FSeinContainmentMemberData>(Entity);
	if (!MemComp)
	{
		UE_LOG(LogSeinSim, Warning, TEXT("EnterContainer: entity %s has no FSeinContainmentMemberData"), *Entity.ToString());
		return false;
	}
	if (MemComp->CurrentContainer.IsValid())
	{
		UE_LOG(LogSeinSim, Warning, TEXT("EnterContainer: entity %s already contained by %s"),
			*Entity.ToString(), *MemComp->CurrentContainer.ToString());
		return false;
	}

	FSeinContainmentData* ContComp =
		GetComponentMutable<FSeinContainmentData>(Container);
	if (!ContComp)
	{
		UE_LOG(LogSeinSim, Warning, TEXT("EnterContainer: container %s has no FSeinContainmentData"), *Container.ToString());
		return false;
	}
	if (MemComp->Size <= 0 || MemComp->CurrentSlot.IsValid()
		|| MemComp->VisualSlotIndex != INDEX_NONE)
	{
		UE_LOG(LogSeinSim, Warning,
			TEXT("EnterContainer: entity %s has invalid uncontained member state"),
			*Entity.ToString());
		return false;
	}
	FString ContainerError;
	if (!UE::SeinARTSCoreEntity::ValidateContainmentContainer(
			Container,
			*ContComp,
			[this](FSeinEntityHandle Handle)
			{
				return EntityPool.IsValid(Handle);
			},
			[this](FSeinEntityHandle Handle)
			{
				return GetComponent<FSeinContainmentMemberData>(Handle);
			},
			GetComponent<FSeinAttachmentSpec>(Container),
			ContainerError))
	{
		UE_LOG(LogSeinSim, Warning,
			TEXT("EnterContainer: container %s has invalid state (%s)"),
			*Container.ToString(), *ContainerError);
		return false;
	}

	TSet<FSeinEntityHandle> VisitedAncestors;
	FSeinEntityHandle Ancestor = Container;
	while (EntityPool.IsValid(Ancestor))
	{
		if (Ancestor == Entity || VisitedAncestors.Contains(Ancestor))
		{
			UE_LOG(LogSeinSim, Warning,
				TEXT("EnterContainer: placing %s in %s would create or extend a containment cycle"),
				*Entity.ToString(), *Container.ToString());
			return false;
		}
		VisitedAncestors.Add(Ancestor);
		const FSeinContainmentMemberData* AncestorMember =
			GetComponent<FSeinContainmentMemberData>(Ancestor);
		if (!AncestorMember || !AncestorMember->CurrentContainer.IsValid())
		{
			break;
		}
		const FSeinEntityHandle NextAncestor =
			AncestorMember->CurrentContainer;
		const FSeinContainmentData* NextContainer =
			GetComponent<FSeinContainmentData>(NextAncestor);
		if (!EntityPool.IsValid(NextAncestor) || !NextContainer
			|| !NextContainer->Occupants.Contains(Ancestor))
		{
			UE_LOG(LogSeinSim, Warning,
				TEXT("EnterContainer: ancestor %s references invalid container %s"),
				*Ancestor.ToString(), *NextAncestor.ToString());
			return false;
		}
		Ancestor = NextAncestor;
	}

	// Capacity
	const int64 NewLoad = static_cast<int64>(ContComp->CurrentLoad)
		+ static_cast<int64>(MemComp->Size);
	if (NewLoad > static_cast<int64>(ContComp->TotalCapacity))
	{
		return false;
	}

	// Tag query (empty query = permissive)
	if (!ContComp->AcceptedEntityQuery.IsEmpty())
	{
		if (!ContComp->AcceptedEntityQuery.Matches(GetEntityTags(Entity)))
		{
			return false;
		}
	}

	// Commit state
	ContComp->Occupants.Add(Entity);
	ContComp->CurrentLoad = static_cast<int32>(NewLoad);
	MemComp->CurrentContainer = Container;
	MemComp->VisualSlotIndex = AssignFirstFreeVisualSlot(*ContComp, Entity);
	// CurrentSlot stays empty — set by AttachToSlot when attachment is used.

	// Visibility-mode spatial effect: Hidden + Partial remove from grid; only
	// PositionedRelative stays registered (rendered via container + offset).
	if (ContComp->Visibility != ESeinContainmentVisibility::PositionedRelative)
	{
		if (SpatialGridUnregisterCallback.IsBound())
		{
			SpatialGridUnregisterCallback.Execute(Entity);
		}
	}

	EnqueueVisualEvent(FSeinVisualEvent::MakeEntityEnteredContainerEvent(
		Container, Entity, MemComp->VisualSlotIndex));
	return true;
}

bool USeinWorldSubsystem::ExitContainer(FSeinEntityHandle Entity, FFixedVector ExitLocation)
{
	if (!RequireStateMutationAuthorization(TEXT("ExitContainer")))
	{
		return false;
	}
	return ExitContainerInternal(
		Entity, ExitLocation, /*bAllowDeferredTeardownContainer=*/false);
}

bool USeinWorldSubsystem::ExitContainerInternal(
	FSeinEntityHandle Entity, FFixedVector ExitLocation,
	bool bAllowDeferredTeardownContainer)
{
	if (!EntityPool.IsValid(Entity)) return false;

	FSeinContainmentMemberData* MemComp =
		GetComponentMutable<FSeinContainmentMemberData>(Entity);
	if (!MemComp || !MemComp->CurrentContainer.IsValid())
	{
		return false;
	}
	const FSeinEntityHandle Container = MemComp->CurrentContainer;
	const bool bLiveContainer = EntityPool.IsValid(Container);
	const bool bDeferredContainer = bAllowDeferredTeardownContainer
		&& Container == DeferredTeardownHandle
		&& EntityPool.IsDeferredDestroyTombstone(Container);
	if (!bLiveContainer && !bDeferredContainer)
	{
		// Stale pointer — scrub and bail.
		MemComp->CurrentContainer = FSeinEntityHandle();
		MemComp->CurrentSlot = FGameplayTag();
		MemComp->VisualSlotIndex = INDEX_NONE;
		return false;
	}

	FSeinContainmentData* ContComp = bDeferredContainer
		? GetDeferredTeardownComponent<FSeinContainmentData>(Container)
		: GetComponentMutable<FSeinContainmentData>(Container);
	if (!ContComp) return false;

	// Resolve exit world position.
	FFixedVector FinalExit = ExitLocation;
	if (FinalExit == FFixedVector())
	{
		FFixedVector ContainerLoc;
		const FSeinEntity* ContEntity = bDeferredContainer
			? EntityPool.GetDeferredDestroyTombstone(Container)
			: GetEntity(Container);
		if (ContEntity)
		{
			ContainerLoc = ContEntity->Transform.GetLocation();
		}
		FinalExit = ContainerLoc;
		const FSeinTransportSpec* TransportSpec = bDeferredContainer
			? GetDeferredTeardownComponent<FSeinTransportSpec>(Container)
			: GetComponent<FSeinTransportSpec>(Container);
		if (TransportSpec)
		{
			const FFixedVector WorldDeployOffset = ContEntity
				? ContEntity->Transform.TransformVector(
					TransportSpec->DeployOffset)
				: TransportSpec->DeployOffset;
			FinalExit = ContainerLoc + WorldDeployOffset;
		}
	}

	// Write the exiter's transform.
	if (FSeinEntity* ExiterEntity = EntityPool.Get(Entity))
	{
		FFixedTransform NewXfm = ExiterEntity->Transform;
		NewXfm.SetLocation(FinalExit);
		ExiterEntity->Transform = NewXfm;
	}

	// Attachment-slot cleanup, if any.
	if (MemComp->CurrentSlot.IsValid())
	{
		FSeinAttachmentSpec* Spec = bDeferredContainer
			? GetDeferredTeardownComponent<FSeinAttachmentSpec>(Container)
			: GetComponentMutable<FSeinAttachmentSpec>(Container);
		if (Spec)
		{
			Spec->Assignments.Remove(MemComp->CurrentSlot);
		}
		EnqueueVisualEvent(FSeinVisualEvent::MakeAttachmentSlotEmptiedEvent(
			Container, Entity, MemComp->CurrentSlot));
	}

	// Visual-slot cleanup.
	if (ContComp->bTracksVisualSlots && ContComp->VisualSlotAssignments.IsValidIndex(MemComp->VisualSlotIndex))
	{
		ContComp->VisualSlotAssignments[MemComp->VisualSlotIndex] = FSeinEntityHandle();
	}

	// Occupant-list cleanup.
	ContComp->Occupants.Remove(Entity);
	const int64 RemainingLoad = static_cast<int64>(ContComp->CurrentLoad)
		- static_cast<int64>(MemComp->Size);
	ContComp->CurrentLoad = RemainingLoad <= 0
		? 0
		: RemainingLoad >= MAX_int32
			? MAX_int32
			: static_cast<int32>(RemainingLoad);
	MemComp->CurrentContainer = FSeinEntityHandle();
	MemComp->CurrentSlot = FGameplayTag();
	MemComp->VisualSlotIndex = INDEX_NONE;

	// Re-register in spatial grid if the container was Hidden/Partial.
	if (ContComp->Visibility != ESeinContainmentVisibility::PositionedRelative)
	{
		if (SpatialGridRegisterCallback.IsBound())
		{
			SpatialGridRegisterCallback.Execute(Entity);
		}
	}

	EnqueueVisualEvent(FSeinVisualEvent::MakeEntityExitedContainerEvent(Container, Entity, FinalExit));
	return true;
}

bool USeinWorldSubsystem::AttachToSlot(FSeinEntityHandle Entity, FSeinEntityHandle Container, FGameplayTag SlotTag)
{
	if (!RequireStateMutationAuthorization(TEXT("AttachToSlot")))
	{
		return false;
	}
	if (!EntityPool.IsValid(Entity) || !EntityPool.IsValid(Container)) return false;
	if (!SlotTag.IsValid()) return false;

	FSeinAttachmentSpec* Spec =
		GetComponentMutable<FSeinAttachmentSpec>(Container);
	if (!Spec) return false;

	// Locate slot by tag.
	const FSeinAttachmentSlotDef* SlotDef = Spec->Slots.FindByPredicate(
		[&](const FSeinAttachmentSlotDef& S) { return S.SlotTag == SlotTag; });
	if (!SlotDef) return false;

	// Already filled?
	if (FSeinEntityHandle* Existing = Spec->Assignments.Find(SlotTag))
	{
		if (Existing->IsValid()) return false;
	}

	// Slot-level tag query (independent of container-level AcceptedEntityQuery).
	if (!SlotDef->AcceptedEntityQuery.IsEmpty())
	{
		if (!SlotDef->AcceptedEntityQuery.Matches(GetEntityTags(Entity)))
		{
			return false;
		}
	}

	// Attachment implies containment — run the standard enter path first.
	if (!EnterContainer(Entity, Container))
	{
		return false;
	}

	// Stamp slot assignment + member back-ref.
	Spec->Assignments.Add(SlotTag, Entity);
	if (FSeinContainmentMemberData* Mem =
		GetComponentMutable<FSeinContainmentMemberData>(Entity))
	{
		Mem->CurrentSlot = SlotTag;
	}

	EnqueueVisualEvent(FSeinVisualEvent::MakeAttachmentSlotFilledEvent(Container, Entity, SlotTag));
	return true;
}

bool USeinWorldSubsystem::DetachFromSlot(FSeinEntityHandle Entity)
{
	if (!RequireStateMutationAuthorization(TEXT("DetachFromSlot")))
	{
		return false;
	}
	if (!EntityPool.IsValid(Entity)) return false;
	FSeinContainmentMemberData* Mem =
		GetComponentMutable<FSeinContainmentMemberData>(Entity);
	if (!Mem || !Mem->CurrentSlot.IsValid()) return false;
	// ExitContainer handles slot-assignment removal + visual event; simply delegate.
	return ExitContainer(Entity);
}

void USeinWorldSubsystem::PropagateContainerDeath(FSeinEntityHandle DyingContainer)
{
	FSeinContainmentData* Container =
		GetDeferredTeardownComponent<FSeinContainmentData>(DyingContainer);
	if (!Container) return;

	FFixedVector ContainerLoc;
	if (const FSeinEntity* ContEntity =
		EntityPool.GetDeferredDestroyTombstone(DyingContainer))
	{
		ContainerLoc = ContEntity->Transform.GetLocation();
	}

	const bool bEject = Container->bEjectOnContainerDeath;
	const TSubclassOf<USeinEffect> OnEjectEffect = Container->OnEjectEffect;
	const TSubclassOf<USeinEffect> OnDeathEffect = Container->OnContainerDeathEffect;

	// Snapshot — occupants list is mutated while iterating when ExitContainer
	// runs, so copy first.
	TArray<FSeinEntityHandle> Occupants = Container->Occupants;
	for (const FSeinEntityHandle& Occ : Occupants)
	{
		if (!EntityPool.IsValid(Occ)) continue;

		if (bEject)
		{
			// Exit at container's last location; apply eject effect if authored.
			ExitContainerInternal(Occ, ContainerLoc,
				/*bAllowDeferredTeardownContainer=*/true);
			if (OnEjectEffect)
			{
				ApplyEffect(Occ, OnEjectEffect, DyingContainer);
			}
		}
		else
		{
			// Occupant dies with container; optional effect first, then destroy.
			if (OnDeathEffect)
			{
				ApplyEffect(Occ, OnDeathEffect, DyingContainer);
			}
			DestroyEntity(Occ);
		}
	}

	// Container's Occupants now empty; its FSeinContainmentData is about to be
	// stripped by the surrounding ProcessDeferredDestroys sweep.
}

FSeinEntityHandle USeinWorldSubsystem::GetImmediateContainer(FSeinEntityHandle Entity) const
{
	const FSeinContainmentMemberData* Mem = GetComponent<FSeinContainmentMemberData>(Entity);
	return (Mem && EntityPool.IsValid(Mem->CurrentContainer)) ? Mem->CurrentContainer : FSeinEntityHandle();
}

FSeinEntityHandle USeinWorldSubsystem::GetRootContainer(FSeinEntityHandle Entity) const
{
	FSeinEntityHandle Cursor = GetImmediateContainer(Entity);
	if (!Cursor.IsValid()) return FSeinEntityHandle();
	TSet<FSeinEntityHandle> Seen;
	Seen.Add(Entity);
	while (Cursor.IsValid())
	{
		if (Seen.Contains(Cursor))
		{
			UE_LOG(LogSeinSim, Warning,
				TEXT("GetRootContainer: containment cycle reached from %s"),
				*Entity.ToString());
			return FSeinEntityHandle();
		}
		Seen.Add(Cursor);
		const FSeinEntityHandle Next = GetImmediateContainer(Cursor);
		if (!Next.IsValid()) return Cursor;
		Cursor = Next;
	}
	return FSeinEntityHandle();
}

bool USeinWorldSubsystem::IsContained(FSeinEntityHandle Entity) const
{
	const FSeinContainmentMemberData* Mem = GetComponent<FSeinContainmentMemberData>(Entity);
	return Mem && EntityPool.IsValid(Mem->CurrentContainer);
}

TArray<FSeinEntityHandle> USeinWorldSubsystem::GetAllNestedOccupants(FSeinEntityHandle Container) const
{
	TArray<FSeinEntityHandle> Out;
	const FSeinContainmentData* Cont = GetComponent<FSeinContainmentData>(Container);
	if (!Cont) return Out;

	TArray<FSeinEntityHandle> Frontier = Cont->Occupants;
	TSet<FSeinEntityHandle> Seen;
	Seen.Add(Container);
	while (Frontier.Num() > 0)
	{
		FSeinEntityHandle Current = Frontier.Pop();
		if (!EntityPool.IsValid(Current) || Seen.Contains(Current)) continue;
		Seen.Add(Current);
		Out.Add(Current);
		if (const FSeinContainmentData* Nested = GetComponent<FSeinContainmentData>(Current))
		{
			Frontier.Append(Nested->Occupants);
		}
	}
	return Out;
}

FSeinContainmentTree USeinWorldSubsystem::BuildContainmentTree(FSeinEntityHandle Container) const
{
	FSeinContainmentTree Tree;

	// Iterative DFS emitting entries in pre-order so the flattened array encodes
	// the hierarchy: each child appears after its parent and is flagged with
	// Depth + ParentIndex. BP consumers walk sequentially to rebuild the tree.
	struct FFrame { FSeinEntityHandle Entity; int32 Depth; int32 ParentIndex; };
	TArray<FFrame> Stack;
	Stack.Reserve(8);
	Stack.Push({Container, 0, INDEX_NONE});
	TSet<FSeinEntityHandle> Seen;

	while (Stack.Num() > 0)
	{
		const FFrame Frame = Stack.Pop();
		if (!EntityPool.IsValid(Frame.Entity) || Seen.Contains(Frame.Entity))
		{
			continue;
		}
		Seen.Add(Frame.Entity);

		FSeinContainmentTreeEntry Entry;
		Entry.Entity = Frame.Entity;
		Entry.Depth = Frame.Depth;
		Entry.ParentIndex = Frame.ParentIndex;
		const int32 ThisIndex = Tree.Entries.Add(Entry);

		if (const FSeinContainmentData* Cont = GetComponent<FSeinContainmentData>(Frame.Entity))
		{
			// Push children in reverse order so stack-popped order matches original
			// occupant list order (deterministic).
			for (int32 i = Cont->Occupants.Num() - 1; i >= 0; --i)
			{
				Stack.Push({Cont->Occupants[i], Frame.Depth + 1, ThisIndex});
			}
		}
	}

	return Tree;
}
