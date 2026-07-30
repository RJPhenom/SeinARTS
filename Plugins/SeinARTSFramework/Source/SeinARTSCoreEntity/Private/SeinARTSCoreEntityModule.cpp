/**
 * SeinARTS Framework 
 * Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file:		SeinARTSCoreEntityModule.cpp
 * @date:		4/3/2026
 * @author:		RJ Macklem
 * @brief:		Module implementation for SeinARTSCoreEntity, including
 *				built-in command schemas, configured canonical-state recipes,
 *				simulation-content discovery, and reload-safe teardown.
 * @disclaimer: This code was generated in part by an AI language model.
 */

#include "SeinARTSCoreEntityModule.h"
#include "SeinARTSCoreEntityLog.h"

#include "Abilities/SeinAbility.h"
#include "Abilities/Actions/SeinWaitAction.h"
#include "Abilities/SeinTargeterSpec.h"
#include "Actor/SeinActor.h"
#include "AI/SeinAIController.h"
#include "Brokers/SeinBrokerTypes.h"
#include "Brokers/SeinCommandBrokerResolver.h"
#include "Brokers/SeinDefaultCommandBrokerResolver.h"
#include "Collision/SeinCollisionResolver.h"
#include "Data/SeinFaction.h"
#include "Data/SeinMatchSettings.h"
#include "Data/SeinVoteState.h"
#include "Effects/SeinEffect.h"
#include "Formations/SeinFormation.h"
#include "Input/SeinBuiltInCommandHandler.h"
#include "Input/SeinCommandAuthorityPolicy.h"
#include "Settings/PluginSettings.h"
#include "Simulation/SeinCanonicalStateRecipe.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Serialization/SeinCanonicalStateCodec.h"
#include "Serialization/SeinCollisionCanonicalStateProvider.h"
#include "Subsystems/SeinFactionService.h"
#include "Tags/SeinARTSGameplayTags.h"

// Module-shared log categories. Declared extern in SeinARTSCoreEntityLog.h and
// defined ONCE here — previously each was DEFINE_LOG_CATEGORY_STATIC in several
// .cpp files, which collided whenever the adaptive unity build packed two of
// those files into the same translation unit. Defined unconditionally (UE_LOG
// resolves them in every build config).
DEFINE_LOG_CATEGORY(LogSeinSim);
DEFINE_LOG_CATEGORY(LogSeinBridge);
DEFINE_LOG_CATEGORY(LogSeinBPFL);

#if !UE_BUILD_SHIPPING
#include "HAL/IConsoleManager.h"
#include "Engine/World.h"

DEFINE_LOG_CATEGORY_STATIC(LogSeinStateHashCmd, Log, All);

// Per-tick legacy local-fingerprint logging toggle. This remains useful for
// locating a mutation inside one process or an otherwise identical preload
// environment, but is neither complete nor cross-process canonical. The
// stable-boundary StateRoot command below is the authoritative determinism
// diagnostic. Stripped in shipping.
static TAutoConsoleVariable<int32> CVarSeinLogStateHash(
	TEXT("Sein.Sim.StateHash.Log"),
	0,
	TEXT("If nonzero, log the legacy local state fingerprint each tick. Not valid for cross-process determinism evidence; use Sein.Sim.StateRoot at a stable boundary."),
	ECVF_Default);

// One-shot compatibility command for the incomplete local fingerprint.
static FAutoConsoleCommandWithWorldAndArgs CmdSeinDumpStateHash(
	TEXT("Sein.Sim.StateHash"),
	TEXT("Log the legacy local state fingerprint once. Not valid for peer or fresh-process comparison."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(
		[](const TArray<FString>& /*Args*/, UWorld* World)
		{
			if (!World) return;
			if (USeinWorldSubsystem* Sub = World->GetSubsystem<USeinWorldSubsystem>())
			{
				UE_LOG(LogSeinStateHashCmd, Log,
					TEXT("LegacyLocalStateFingerprint[tick %d] = 0x%08x"),
					Sub->GetCurrentTick(),
					static_cast<uint32>(Sub->ComputeStateHash()));
			}
		}));

// Authoritative one-shot determinism diagnostic. Canonical capture is allowed
// only at a coherent stable boundary and explains any refusal.
static FAutoConsoleCommandWithWorldAndArgs CmdSeinDumpStateRoot(
	TEXT("Sein.Sim.StateRoot"),
	TEXT("Log the exact canonical world-state root at the current stable boundary."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(
		[](const TArray<FString>& /*Args*/, UWorld* World)
		{
			if (!World)
			{
				return;
			}
			if (USeinWorldSubsystem* Sub =
				World->GetSubsystem<USeinWorldSubsystem>())
			{
				FGuid Root;
				FString Error;
				if (Sub->ComputeCanonicalStateRoot(Root, Error))
				{
					UE_LOG(LogSeinStateHashCmd, Log,
						TEXT("StateRoot[tick %d] = %s"),
						Sub->GetCurrentTick(),
						*Root.ToString(EGuidFormats::Digits));
				}
				else
				{
					UE_LOG(LogSeinStateHashCmd, Warning,
						TEXT("StateRoot[tick %d] unavailable: %s"),
						Sub->GetCurrentTick(),
						*Error);
				}
			}
		}));
#endif // !UE_BUILD_SHIPPING

IMPLEMENT_MODULE(FSeinARTSCoreEntity, SeinARTSCoreEntity);

namespace
{
	const TCHAR* GCoreEntitySimulationContentContributorId =
		TEXT("seinarts.coreentity");
	constexpr uint32 GSimulationContentContributorRevision = 1;
	const FName GProjectSettingsCanonicalStateRecipeOwner(
		TEXT("seinarts.projectsettings"));
	const FName GWaitActionCodecOwner(TEXT("seinartscoreentity"));
	const FName GPoolObjectCodecOwner(TEXT("seinartscoreentity"));

	bool RegisterBuiltInPoolObjectCodecs(
		TArray<FSeinPoolObjectCodecRegistrationHandle>& OutHandles,
		FString& OutError)
	{
		OutHandles.Reset();
		struct FSpec
		{
			const UClass* Anchor;
			ESeinPoolObjectKind Kind;
			const TCHAR* StableProviderId;
		};
		const FSpec Specs[] = {
			{
				USeinAbility::StaticClass(),
				ESeinPoolObjectKind::Ability,
				TEXT("seinarts.core.pool.ability.reflection"),
			},
			{
				USeinCommandBrokerResolver::StaticClass(),
				ESeinPoolObjectKind::CommandBrokerResolver,
				TEXT("seinarts.core.pool.resolver.reflection"),
			},
			{
				USeinDefaultCommandBrokerResolver::StaticClass(),
				ESeinPoolObjectKind::CommandBrokerResolver,
				TEXT("seinarts.core.pool.default-resolver.reflection"),
			},
		};
		for (const FSpec& Spec : Specs)
		{
			FSeinPoolObjectCodecDescriptor Descriptor;
			Descriptor.NativeAnchor = Spec.Anchor;
			Descriptor.Kind = Spec.Kind;
			Descriptor.StableProviderId = Spec.StableProviderId;
			Descriptor.StateSchemaVersion = 1;
			Descriptor.BehaviorRevision = 1;
			Descriptor.CodecRevision = 2;
			Descriptor.MaxStateBytes =
				FSeinPoolObjectCodecRegistry::MaxStateBytes;
			Descriptor.bAllowBlueprintChildren = true;
			FSeinPoolObjectCodecRegistrationHandle Handle =
				FSeinPoolObjectCodecRegistry::Register(
					GPoolObjectCodecOwner,
					Descriptor,
					FSeinPoolObjectCodecRegistry::MakeReflectedOps(),
					&OutError);
			if (!Handle.IsValid())
			{
				OutHandles.Reset();
				return false;
			}
			OutHandles.Add(MoveTemp(Handle));
		}
		return true;
	}

	struct FWaitActionRestoreStage final
		: ISeinLatentActionRestoreStage
	{
		FSeinWaitActionCanonicalState State;
	};

	FSeinLatentActionCodecRegistrationHandle RegisterWaitActionCodec(
		FString& OutError)
	{
		FGuid SchemaDigest;
		if (!FSeinCanonicalStateCodec::ComputeSchemaDigest(
			FSeinWaitActionCanonicalState::StaticStruct(),
			SchemaDigest,
			OutError))
		{
			return {};
		}

		FSeinLatentActionCodecDescriptor Descriptor;
		Descriptor.SupportedClass = USeinWaitAction::StaticClass();
		Descriptor.StableCodecId =
			TEXT("seinarts.core.wait");
		Descriptor.StateSchemaVersion = 1;
		Descriptor.BehaviorRevision = 1;
		Descriptor.CodecRevision = 1;
		Descriptor.PayloadStruct =
			FSeinWaitActionCanonicalState::StaticStruct();
		Descriptor.PayloadSchemaDigest = SchemaDigest;
		Descriptor.Limits.MaxRecursionDepth = 8;
		Descriptor.Limits.MaxEncodedBytes = 256;
		Descriptor.Limits.MaxAggregateElements = 16;

		FSeinLatentActionCodecOps Ops;
		Ops.Capture = [](
			const FSeinLatentActionCaptureContext& Context,
			FInstancedStruct& OutPayload,
			FString& OutCaptureError)
		{
			OutPayload.Reset();
			const USeinWaitAction* Wait =
				Cast<USeinWaitAction>(&Context.Action);
			if (!Wait
				|| Wait->GetClass() != USeinWaitAction::StaticClass())
			{
				OutCaptureError =
					TEXT("Wait codec received a non-exact wait action.");
				return false;
			}
			FSeinWaitActionCanonicalState State;
			State.Duration = Wait->Duration;
			State.Elapsed = Wait->Elapsed;
			OutPayload.InitializeAs<FSeinWaitActionCanonicalState>(
				State);
			return true;
		};
		Ops.StageRestore = [](
			const FSeinLatentActionStageContext&,
			const FInstancedStruct& Payload,
			TUniquePtr<ISeinLatentActionRestoreStage>& OutStage,
			FString& OutStageError)
		{
			const FSeinWaitActionCanonicalState* State =
				Payload.GetPtr<FSeinWaitActionCanonicalState>();
			if (!State)
			{
				OutStageError =
					TEXT("Wait codec received the wrong staged payload.");
				return false;
			}
			TUniquePtr<FWaitActionRestoreStage> Stage =
				MakeUnique<FWaitActionRestoreStage>();
			Stage->State = *State;
			OutStage = MoveTemp(Stage);
			return true;
		};
		Ops.CommitRestore = [](
			FSeinLatentActionCommitContext& Context,
			TUniquePtr<ISeinLatentActionRestoreStage>&& BaseStage)
		{
			FWaitActionRestoreStage* Stage =
				static_cast<FWaitActionRestoreStage*>(BaseStage.Get());
			check(Stage);
			USeinWaitAction* Wait =
				NewObject<USeinWaitAction>(&Context.World);
			check(Wait);
			Wait->Duration = Stage->State.Duration;
			Wait->Elapsed = Stage->State.Elapsed;
			return Wait;
		};
		return FSeinLatentActionCodecRegistry::Register(
			GWaitActionCodecOwner,
			Descriptor,
			MoveTemp(Ops),
			&OutError);
	}

	FSeinSimulationContentDiscoveryRoot MakePackageDiscoveryRoot(
		const UClass* RootClass)
	{
		check(RootClass);

		FSeinSimulationContentDiscoveryRoot Root;
		Root.RootClassPath = RootClass->GetPathName();
		Root.StableRecordKindId =
			FSeinSimulationContentManifestCodec::GetCurrentRecordKindId();
		Root.RecordRevision =
			FSeinSimulationContentManifestCodec::CurrentRecordRevision;
		return Root;
	}

	const FName BuiltInCommandSchemaOwner(TEXT("SeinARTSCoreEntity.Commands"));
	// Bump whenever built-in command semantics change without a wire-shape change.
	constexpr int32 BuiltInCommandImplementationRevision = 1;

	constexpr int32 AllCommandExecutionAllowances =
		static_cast<int32>(ESeinCommandExecutionAllowance::Spectator)
		| static_cast<int32>(ESeinCommandExecutionAllowance::HardPause)
		| static_cast<int32>(ESeinCommandExecutionAllowance::Starting);
	constexpr int32 ActiveParticipantControlAllowances =
		static_cast<int32>(ESeinCommandExecutionAllowance::HardPause)
		| static_cast<int32>(ESeinCommandExecutionAllowance::Starting);
	constexpr int32 ResumeControlAllowances =
		ActiveParticipantControlAllowances
		| static_cast<int32>(ESeinCommandExecutionAllowance::FrozenPauseControl);

	struct FBuiltInCommandSchemaSpec
	{
		const TCHAR* StableSchemaId;
		FGameplayTag CommandType;
		const UScriptStruct* PayloadStruct;
		ESeinCommandAuthorityScope AuthorityScope;
		int32 MaxEntityListEntries;
		int32 MaxTargeterPoints;
		int32 MaxPayloadBytes;
		int32 MaxPayloadAggregateElements;
		int32 AllowedExecutionContexts;
	};

	FSeinCommandSchemaDescriptor MakeBuiltInDescriptor(const FBuiltInCommandSchemaSpec& Spec)
	{
		FSeinCommandSchemaDescriptor Descriptor;
		Descriptor.StableSchemaId = FName(Spec.StableSchemaId);
		Descriptor.CommandType = Spec.CommandType;
		Descriptor.SchemaVersion = 1;
		Descriptor.ImplementationRevision = BuiltInCommandImplementationRevision;
		Descriptor.PayloadStruct = Spec.PayloadStruct;
		Descriptor.AuthorityScope = Spec.AuthorityScope;
		Descriptor.MaxEntityListEntries = Spec.MaxEntityListEntries;
		Descriptor.MaxTargeterPoints = Spec.MaxTargeterPoints;
		Descriptor.MaxPayloadBytes = Spec.MaxPayloadBytes;
		Descriptor.MaxPayloadAggregateElements = Spec.MaxPayloadAggregateElements;
		Descriptor.AllowedExecutionContexts = Spec.AllowedExecutionContexts;
		Descriptor.HandlerClass = USeinBuiltInCommandHandler::StaticClass();
		return Descriptor;
	}

	void ReleaseBuiltInSchemas(TArray<FSeinCommandSchemaRegistrationHandle>& Handles)
	{
		for (int32 Index = Handles.Num() - 1; Index >= 0; --Index)
		{
			FSeinCommandSchemaRegistry::UnregisterSchema(Handles[Index]);
		}
		Handles.Reset();
	}

	void ReleaseConfiguredCanonicalStateRecipes(
		TArray<FSeinCanonicalStateRecipeRegistrationHandle>& Handles)
	{
		for (int32 Index = Handles.Num() - 1; Index >= 0; --Index)
		{
			Handles[Index].Reset();
		}
		Handles.Reset();
	}

	bool CollectConfiguredCanonicalStateRecipePaths(
		const USeinARTSCoreSettings& Settings,
		TArray<FString>& OutPaths,
		FString& OutError)
	{
		OutPaths.Reset();
		OutError.Reset();
		OutPaths.Reserve(Settings.CanonicalStateRecipes.Num());
		TSet<FString> ExactClassPaths;
		for (int32 Index = 0;
			Index < Settings.CanonicalStateRecipes.Num();
			++Index)
		{
			const TSoftClassPtr<USeinCanonicalStateRecipe>& Recipe =
				Settings.CanonicalStateRecipes[Index];
			if (Recipe.IsNull())
			{
				OutError = FString::Printf(
					TEXT("Canonical State Recipes[%d] is None."),
					Index);
				OutPaths.Reset();
				return false;
			}

			const FString ExactClassPath =
				Recipe.ToSoftObjectPath().ToString();
			if (ExactClassPath.IsEmpty())
			{
				OutError = FString::Printf(
					TEXT("Canonical State Recipes[%d] has an empty class path."),
					Index);
				OutPaths.Reset();
				return false;
			}
			if (ExactClassPaths.Contains(ExactClassPath))
			{
				OutError = FString::Printf(
					TEXT("Canonical State Recipes contains duplicate exact path '%s'."),
					*ExactClassPath);
				OutPaths.Reset();
				return false;
			}

			ExactClassPaths.Add(ExactClassPath);
			OutPaths.Add(ExactClassPath);
		}
		OutPaths.Sort();
		return true;
	}

	bool RegisterConfiguredCanonicalStateRecipes(
		const USeinARTSCoreSettings& Settings,
		TArray<FSeinCanonicalStateRecipeRegistrationHandle>& OutHandles,
		TArray<FString>& OutRegisteredPaths,
		FString& OutError)
	{
		ReleaseConfiguredCanonicalStateRecipes(OutHandles);
		OutRegisteredPaths.Reset();
		OutError.Reset();

		TArray<FString> RecipeClassPaths;
		if (!CollectConfiguredCanonicalStateRecipePaths(
			Settings, RecipeClassPaths, OutError))
		{
			return false;
		}

		OutHandles.Reserve(RecipeClassPaths.Num());
		for (const FString& RecipeClassPath : RecipeClassPaths)
		{
			FString RegistrationError;
			FSeinCanonicalStateRecipeRegistrationHandle Handle =
				FSeinCanonicalStateRecipeRegistry::Register(
					GProjectSettingsCanonicalStateRecipeOwner,
					FSoftClassPath(RecipeClassPath),
					&RegistrationError);
			if (!Handle.IsValid())
			{
				OutError = FString::Printf(
					TEXT("Canonical-state recipe '%s' failed to register: %s"),
					*RecipeClassPath,
					*RegistrationError);
				ReleaseConfiguredCanonicalStateRecipes(OutHandles);
				OutHandles.Add(MoveTemp(Handle));
				return false;
			}
			OutHandles.Add(MoveTemp(Handle));
		}
		OutRegisteredPaths = MoveTemp(RecipeClassPaths);
		return true;
	}
}

bool FSeinARTSCoreEntity::AreConfiguredCanonicalStateRecipesReady() const
{
	FString IgnoredError;
	return ValidateConfiguredCanonicalStateRecipes(IgnoredError);
}

bool FSeinARTSCoreEntity::ValidateConfiguredCanonicalStateRecipes(
	FString& OutError) const
{
	OutError.Reset();
	if (!bConfiguredCanonicalStateRecipesReady)
	{
		OutError =
			TEXT("Configured canonical-state recipes did not register completely during CoreEntity module startup.");
		return false;
	}

	const USeinARTSCoreSettings* Settings =
		GetDefault<USeinARTSCoreSettings>();
	if (!Settings)
	{
		OutError =
			TEXT("Core settings are unavailable while validating canonical-state recipes.");
		return false;
	}

	TArray<FString> CurrentPaths;
	if (!CollectConfiguredCanonicalStateRecipePaths(
		*Settings, CurrentPaths, OutError))
	{
		return false;
	}
	if (CurrentPaths != ConfiguredCanonicalStateRecipePaths)
	{
		OutError =
			TEXT("Canonical State Recipes changed after CoreEntity module startup. Restart the editor (or reload the module), then regenerate the Simulation Content manifest before launching a match.");
		return false;
	}
	return true;
}

void FSeinARTSCoreEntity::StartupModule()
{
	PoolObjectCodecHandles.Reset();
	FString PoolCodecError;
	if (!RegisterBuiltInPoolObjectCodecs(
		PoolObjectCodecHandles, PoolCodecError))
	{
		UE_LOG(LogSeinSim, Error,
			TEXT("Built-in pool-object codecs failed to register: %s"),
			*PoolCodecError);
	}

	WaitActionCodecHandle.Reset();
	FString WaitCodecError;
	WaitActionCodecHandle = RegisterWaitActionCodec(WaitCodecError);
	if (!WaitActionCodecHandle.IsValid())
	{
		UE_LOG(LogSeinSim, Error,
			TEXT("Built-in Wait latent codec failed to register: %s"),
			*WaitCodecError);
	}

	CollisionCanonicalStateHandle.Reset();
	FString CollisionStateError;
	CollisionCanonicalStateHandle =
		SeinRegisterCollisionCanonicalStateProvider(CollisionStateError);
	if (!CollisionCanonicalStateHandle.IsValid())
	{
		UE_LOG(LogSeinSim, Error,
			TEXT("Canonical-state contributor 'seinarts.collision/resolver-binding' failed to register: %s"),
			*CollisionStateError);
	}

	bConfiguredCanonicalStateRecipesReady = false;
	ReleaseConfiguredCanonicalStateRecipes(
		ConfiguredCanonicalStateRecipeHandles);
	ConfiguredCanonicalStateRecipePaths.Reset();
	SimulationContentRegistrationHandle.Reset();

	const FBuiltInCommandSchemaSpec Schemas[] = {
		{ TEXT("SeinARTS.Core.Command.ActivateAbility.V1"), SeinARTSTags::Command_Type_ActivateAbility,
			nullptr, ESeinCommandAuthorityScope::Entity, 0, 256, 0, 0, 0 },
		{ TEXT("SeinARTS.Core.Command.CancelAbility.V1"), SeinARTSTags::Command_Type_CancelAbility,
			nullptr, ESeinCommandAuthorityScope::Entity, 0, 0, 0, 0, 0 },
		{ TEXT("SeinARTS.Core.Command.CancelProduction.V1"), SeinARTSTags::Command_Type_CancelProduction,
			nullptr, ESeinCommandAuthorityScope::Entity, 0, 0, 0, 0, 0 },
		{ TEXT("SeinARTS.Core.Command.Ping.V1"), SeinARTSTags::Command_Type_Ping,
			nullptr, ESeinCommandAuthorityScope::Self, 0, 0, 0, 0, AllCommandExecutionAllowances },
		{ TEXT("SeinARTS.Core.Command.BrokerOrder.V1"), SeinARTSTags::Command_Type_BrokerOrder,
			FSeinBrokerOrderPayload::StaticStruct(), ESeinCommandAuthorityScope::EntitySet,
			4096, 0, 128 * 1024, 4096, 0 },

		{ TEXT("SeinARTS.Core.Command.PauseMatchRequest.V1"), SeinARTSTags::Command_Type_PauseMatchRequest,
			nullptr, ESeinCommandAuthorityScope::Self, 0, 0, 0, 0, ActiveParticipantControlAllowances },
		{ TEXT("SeinARTS.Core.Command.ResumeMatchRequest.V1"), SeinARTSTags::Command_Type_ResumeMatchRequest,
			nullptr, ESeinCommandAuthorityScope::Self, 0, 0, 0, 0, ResumeControlAllowances },
		{ TEXT("SeinARTS.Core.Command.EndMatch.V1"), SeinARTSTags::Command_Type_EndMatch,
			FSeinEndMatchCommandPayload::StaticStruct(), ESeinCommandAuthorityScope::MatchControl,
			0, 0, 256, 0, AllCommandExecutionAllowances },
		{ TEXT("SeinARTS.Core.Command.ConcedeMatch.V1"), SeinARTSTags::Command_Type_ConcedeMatch,
			nullptr, ESeinCommandAuthorityScope::Self, 0, 0, 0, 0, ActiveParticipantControlAllowances },
		{ TEXT("SeinARTS.Core.Command.StartVote.V1"), SeinARTSTags::Command_Type_StartVote,
			FSeinStartVoteCommandPayload::StaticStruct(), ESeinCommandAuthorityScope::Self,
			0, 0, 32, 0, ActiveParticipantControlAllowances },
		{ TEXT("SeinARTS.Core.Command.CastVote.V1"), SeinARTSTags::Command_Type_CastVote,
			nullptr, ESeinCommandAuthorityScope::Self, 0, 0, 0, 0, ActiveParticipantControlAllowances },

		{ TEXT("SeinARTS.Core.Command.Observer.CameraUpdate.V1"), SeinARTSTags::Command_Type_Observer_CameraUpdate,
			nullptr, ESeinCommandAuthorityScope::PublicObserver, 0, 0, 0, 0, AllCommandExecutionAllowances },
		{ TEXT("SeinARTS.Core.Command.Observer.SelectionChanged.V1"), SeinARTSTags::Command_Type_Observer_SelectionChanged,
			nullptr, ESeinCommandAuthorityScope::PublicObserver, 4096, 0, 0, 0, AllCommandExecutionAllowances },
		{ TEXT("SeinARTS.Core.Command.Observer.Selection.Replaced.V1"), SeinARTSTags::Command_Type_Observer_Selection_Replaced,
			nullptr, ESeinCommandAuthorityScope::PublicObserver, 4096, 0, 0, 0, AllCommandExecutionAllowances },
		{ TEXT("SeinARTS.Core.Command.Observer.Selection.Added.V1"), SeinARTSTags::Command_Type_Observer_Selection_Added,
			nullptr, ESeinCommandAuthorityScope::PublicObserver, 4096, 0, 0, 0, AllCommandExecutionAllowances },
		{ TEXT("SeinARTS.Core.Command.Observer.Selection.Removed.V1"), SeinARTSTags::Command_Type_Observer_Selection_Removed,
			nullptr, ESeinCommandAuthorityScope::PublicObserver, 4096, 0, 0, 0, AllCommandExecutionAllowances },
		{ TEXT("SeinARTS.Core.Command.Observer.ControlGroup.Assigned.V1"), SeinARTSTags::Command_Type_Observer_ControlGroup_Assigned,
			nullptr, ESeinCommandAuthorityScope::PublicObserver, 4096, 0, 0, 0, AllCommandExecutionAllowances },
		{ TEXT("SeinARTS.Core.Command.Observer.ControlGroup.AddedTo.V1"), SeinARTSTags::Command_Type_Observer_ControlGroup_AddedTo,
			nullptr, ESeinCommandAuthorityScope::PublicObserver, 4096, 0, 0, 0, AllCommandExecutionAllowances },
		{ TEXT("SeinARTS.Core.Command.Observer.ControlGroup.Selected.V1"), SeinARTSTags::Command_Type_Observer_ControlGroup_Selected,
			nullptr, ESeinCommandAuthorityScope::PublicObserver, 0, 0, 0, 0, AllCommandExecutionAllowances },
	};

	bBuiltInCommandSchemasReady = false;
	ReleaseBuiltInSchemas(BuiltInCommandSchemaHandles);
	BuiltInCommandSchemaHandles.Reset(UE_ARRAY_COUNT(Schemas));
	for (const FBuiltInCommandSchemaSpec& Spec : Schemas)
	{
		FSeinCommandSchemaRegistrationHandle Handle =
			FSeinCommandSchemaRegistry::RegisterSchema(
				BuiltInCommandSchemaOwner,
				MakeBuiltInDescriptor(Spec));
		if (!Handle.IsValid())
		{
			UE_LOG(LogSeinSim, Error,
				TEXT("Built-in command schema registration failed for '%s'; rolling back this module's registrations."),
				Spec.StableSchemaId);
			ReleaseBuiltInSchemas(BuiltInCommandSchemaHandles);
			return;
		}
		BuiltInCommandSchemaHandles.Add(MoveTemp(Handle));
	}

	bBuiltInCommandSchemasReady = true;
	UE_LOG(LogSeinSim, Log, TEXT("Registered %d built-in command schemas."), BuiltInCommandSchemaHandles.Num());

	FSeinSimulationContentContributorDescriptor ContentDescriptor;
	ContentDescriptor.StableContributorId =
		GCoreEntitySimulationContentContributorId;
	ContentDescriptor.ContributorRevision =
		GSimulationContentContributorRevision;
	ContentDescriptor.DiscoveryRoots = {
		MakePackageDiscoveryRoot(ASeinActor::StaticClass()),
		MakePackageDiscoveryRoot(USeinAbility::StaticClass()),
		MakePackageDiscoveryRoot(USeinEffect::StaticClass()),
		MakePackageDiscoveryRoot(USeinTargeterSpec::StaticClass()),
		MakePackageDiscoveryRoot(USeinCommandBrokerResolver::StaticClass()),
		MakePackageDiscoveryRoot(USeinCollisionResolver::StaticClass()),
		MakePackageDiscoveryRoot(USeinCommandAuthorityPolicy::StaticClass()),
		MakePackageDiscoveryRoot(USeinCommandHandler::StaticClass()),
		MakePackageDiscoveryRoot(USeinFactionService::StaticClass()),
		MakePackageDiscoveryRoot(USeinFaction::StaticClass()),
		MakePackageDiscoveryRoot(USeinAIController::StaticClass()),
		MakePackageDiscoveryRoot(USeinFormation::StaticClass()),
		MakePackageDiscoveryRoot(USeinCanonicalStateRecipe::StaticClass()),
	};

	FString ContentRegistrationError;
	SimulationContentRegistrationHandle =
		FSeinSimulationContentRegistry::RegisterContributor(
			ContentDescriptor,
			&ContentRegistrationError);
	if (!SimulationContentRegistrationHandle.IsValid())
	{
		UE_LOG(
			LogSeinSim,
			Error,
			TEXT("Simulation-content contributor '%s' failed to register: %s"),
			*ContentDescriptor.StableContributorId,
			*ContentRegistrationError);
	}

	const USeinARTSCoreSettings* Settings =
		GetDefault<USeinARTSCoreSettings>();
	FString RecipeRegistrationError;
	if (!Settings
		|| !RegisterConfiguredCanonicalStateRecipes(
			*Settings,
			ConfiguredCanonicalStateRecipeHandles,
			ConfiguredCanonicalStateRecipePaths,
			RecipeRegistrationError))
	{
		if (RecipeRegistrationError.IsEmpty())
		{
			RecipeRegistrationError =
				TEXT("Core settings are unavailable.");
		}
		UE_LOG(
			LogSeinSim,
			Error,
			TEXT("Configured canonical-state recipes failed to register: %s"),
			*RecipeRegistrationError);
		return;
	}

	bConfiguredCanonicalStateRecipesReady = true;
}

void FSeinARTSCoreEntity::ShutdownModule()
{
	PoolObjectCodecHandles.Reset();
	WaitActionCodecHandle.Reset();
	CollisionCanonicalStateHandle.Reset();
	bConfiguredCanonicalStateRecipesReady = false;
	ReleaseConfiguredCanonicalStateRecipes(
		ConfiguredCanonicalStateRecipeHandles);
	ConfiguredCanonicalStateRecipePaths.Reset();
	SimulationContentRegistrationHandle.Reset();
	ReleaseBuiltInSchemas(BuiltInCommandSchemaHandles);
	bBuiltInCommandSchemasReady = false;
}
