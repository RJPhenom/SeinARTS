#include "CQTest.h"
#include "Components/ActorTestSpawner.h"

#include "Brokers/SeinBrokerTypes.h"
#include "Brokers/SeinDefaultCommandBrokerResolver.h"
#include "Components/SeinAbilityComponent.h"
#include "Components/SeinBrokerMembershipData.h"
#include "Components/SeinCommandBrokerData.h"
#include "Containers/Ticker.h"
#include "Simulation/SeinTestSimContext.h"
#include "Input/SeinCommand.h"
#include "Input/SeinCommandAuthorityPolicy.h"
#include "Lib/SeinAbilityBPFL.h"
#include "Settings/PluginSettings.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Simulation/SeinTestMatchBootstrap.h"
#include "Tags/SeinARTSGameplayTags.h"
#include "TestTypes/SeinAbilityCallbackSafetyTestTypes.h"

void USeinCallbackImmediateEndAbility::OnActivate_Implementation()
{
	EndAbility();
}

void USeinCallbackReplaceComponentAbility::OnActivate_Implementation()
{
	if (WorldSubsystem)
	{
		WorldSubsystem->AddComponent(OwnerEntity, FSeinAbilityComponent());
	}
}

void USeinCallbackGrowComponentStorageAbility::OnActivate_Implementation()
{
	if (!WorldSubsystem)
	{
		return;
	}
	// World component storage starts at 1024 slots. Crossing it and adding the
	// same component type forces a relocation while activation is on the stack.
	FSeinEntityHandle GrowthEntity;
	for (int32 Index = 0; Index < 1100; ++Index)
	{
		GrowthEntity = WorldSubsystem->SpawnAbstractEntity(
			FFixedTransform(), FSeinPlayerID::Neutral());
	}
	WorldSubsystem->AddComponent(GrowthEntity, FSeinAbilityComponent());
}

void USeinCallbackRevokeOnCancelAbility::OnEnd_Implementation(bool bWasCancelled)
{
	if (!bWasCancelled || !WorldSubsystem)
	{
		return;
	}

	USeinAbilityBPFL::SeinForceRevokeAbilityByClass(
		WorldSubsystem, OwnerEntity, USeinCallbackRevokeOnCancelAbility::StaticClass());
	const int32 ReplacementID = USeinAbilityBPFL::SeinGrantAbility(
		WorldSubsystem, OwnerEntity, USeinCallbackCancelReplacementAbility::StaticClass());
	USeinAbility* Replacement = WorldSubsystem->GetAbilityInstance(ReplacementID);
	if (!Replacement)
	{
		return;
	}
	Replacement->AbilityTag = SeinARTSTags::Command_Context_Target_Ground;
	USeinAbilityBPFL::SeinActivateAbility(
		WorldSubsystem, OwnerEntity, Replacement->AbilityTag,
		FSeinEntityHandle::Invalid(), FFixedVector());
	if (FSeinAbilityComponent* AbilityComponent =
		WorldSubsystem->GetComponent<FSeinAbilityComponent>(OwnerEntity))
	{
		AbilityComponent->ActiveAbilityID = ReplacementID;
	}
}

namespace
{
	struct FScopedBrokerResolver
	{
		FScopedBrokerResolver()
		{
			Settings = GetMutableDefault<USeinARTSCoreSettings>();
			check(Settings);
			Previous = Settings->DefaultBrokerResolverClass;
			PreviousAuthority = Settings->CommandAuthorityPolicyClass;
			Settings->DefaultBrokerResolverClass =
				USeinDefaultCommandBrokerResolver::StaticClass();
			Settings->CommandAuthorityPolicyClass = FSoftClassPath(
				USeinDefaultCommandAuthorityPolicy::StaticClass()->GetPathName());
		}

		~FScopedBrokerResolver()
		{
			Settings->DefaultBrokerResolverClass = Previous;
			Settings->CommandAuthorityPolicyClass = PreviousAuthority;
		}

		USeinARTSCoreSettings* Settings = nullptr;
		TSoftClassPtr<USeinCommandBrokerResolver> Previous;
		FSoftClassPath PreviousAuthority;
	};

	template <typename TTestRunner>
	void ExpectAbilityHashDiagnostic(TTestRunner& TestRunner)
	{
		TestRunner.AddExpectedError(
			TEXT("Component 'SeinAbilityComponent' has field(s) excluded from the legacy local state fingerprint"),
			EAutomationExpectedErrorFlags::Contains, 1, false);
	}

	void TickOnce(USeinWorldSubsystem& World)
	{
		if (!SeinTestMatchBootstrap::Start(World)) return;
		FTSTicker::GetCoreTicker().Tick(World.GetFixedDeltaTimeSeconds());
	}

	void SubmitAuthorizedDraft(
		USeinWorldSubsystem& World,
		const FSeinCommand& Command)
	{
		if (SeinTestMatchBootstrap::Start(World))
		{
			World.SubmitLocalCommandDraft(Command);
		}
	}

	USeinAbility* GrantAbility(USeinWorldSubsystem& World,
		FSeinEntityHandle Entity, TSubclassOf<USeinAbility> AbilityClass,
		FGameplayTag AbilityTag)
	{
		const int32 AbilityID = USeinAbilityBPFL::SeinGrantAbility(
			&World, Entity, AbilityClass);
		USeinAbility* Ability = World.GetAbilityInstance(AbilityID);
		if (Ability)
		{
			Ability->AbilityTag = AbilityTag;
		}
		return Ability;
	}
}

namespace UE::SeinARTSTests
{
	TEST(ActivationCallbacksCannotResurrectOrWriteThroughInvalidatedAbilityState,
		"SeinARTS.Sim.Ability.CallbackSafety")
	{
		ExpectAbilityHashDiagnostic(*TestRunner);
		FScopedBrokerResolver Settings;
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World =
			Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));

		const FSeinPlayerID Player(1);
		FSeinEntityHandle EndingEntity;
		USeinAbility* EndingAbility = nullptr;
		const auto AuthorState = [&]()
		{
			World->RegisterPlayer(Player, FSeinFactionID(1));
			EndingEntity =
				World->SpawnAbstractEntity(FFixedTransform(), Player);
			World->AddComponent(EndingEntity, FSeinAbilityComponent());
			EndingAbility = GrantAbility(*World, EndingEntity,
				USeinCallbackImmediateEndAbility::StaticClass(),
				SeinARTSTags::Command_Context_AbilityTriggered);
		};
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Materialize(
			*World, AuthorState)));
		ASSERT_THAT(IsNotNull(EndingAbility));
		FSeinCommand Command = FSeinCommand::MakeAbilityCommand(
			Player, EndingEntity, EndingAbility->AbilityTag);
		SubmitAuthorizedDraft(*World, Command);
		TickOnce(*World);
		const FSeinAbilityComponent* AbilityComponent =
			World->GetComponent<FSeinAbilityComponent>(EndingEntity);
		ASSERT_THAT(IsNotNull(AbilityComponent));
		ASSERT_THAT(IsFalse(EndingAbility->bIsActive));
		ASSERT_THAT(AreEqual(INDEX_NONE, AbilityComponent->ActiveAbilityID));

		FSeinEntityHandle GrowthEntity;
		USeinAbility* GrowthAbility = nullptr;
		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*World);
			GrowthEntity = World->SpawnAbstractEntity(FFixedTransform(), Player);
			World->AddComponent(GrowthEntity, FSeinAbilityComponent());
			GrowthAbility = GrantAbility(*World, GrowthEntity,
				USeinCallbackGrowComponentStorageAbility::StaticClass(),
				SeinARTSTags::Command_Context_Target_Neutral);
		}
		ASSERT_THAT(IsNotNull(GrowthAbility));
		Command = FSeinCommand::MakeAbilityCommand(
			Player, GrowthEntity, GrowthAbility->AbilityTag);
		SubmitAuthorizedDraft(*World, Command);
		TickOnce(*World);
		AbilityComponent = World->GetComponent<FSeinAbilityComponent>(GrowthEntity);
		ASSERT_THAT(IsNotNull(AbilityComponent));
		ASSERT_THAT(IsTrue(GrowthAbility->bIsActive));
		ASSERT_THAT(IsTrue(AbilityComponent->AbilityInstanceIDs.Contains(
			AbilityComponent->ActiveAbilityID)));
		ASSERT_THAT(IsTrue(World->GetAbilityInstance(
			AbilityComponent->ActiveAbilityID) == GrowthAbility));

		FSeinEntityHandle ReplacedEntity;
		USeinAbility* ReplacingAbility = nullptr;
		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*World);
			ReplacedEntity = World->SpawnAbstractEntity(FFixedTransform(), Player);
			World->AddComponent(ReplacedEntity, FSeinAbilityComponent());
			ReplacingAbility = GrantAbility(*World, ReplacedEntity,
				USeinCallbackReplaceComponentAbility::StaticClass(),
				SeinARTSTags::Command_Context_Target_Ground);
		}
		ASSERT_THAT(IsNotNull(ReplacingAbility));
		Command = FSeinCommand::MakeAbilityCommand(
			Player, ReplacedEntity, ReplacingAbility->AbilityTag);
		SubmitAuthorizedDraft(*World, Command);
		TickOnce(*World);
		AbilityComponent = World->GetComponent<FSeinAbilityComponent>(ReplacedEntity);
		ASSERT_THAT(IsNotNull(AbilityComponent));
		ASSERT_THAT(AreEqual(0, AbilityComponent->AbilityInstanceIDs.Num()));
		ASSERT_THAT(AreEqual(INDEX_NONE, AbilityComponent->ActiveAbilityID));
	}

	TEST(CancelAndBrokerReplacementPreserveCallbackInstalledAbilityIdentity,
		"SeinARTS.Sim.Ability.CallbackSafety")
	{
		ExpectAbilityHashDiagnostic(*TestRunner);
		FScopedBrokerResolver Settings;
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World =
			Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));

		const FSeinPlayerID Player(1);
		FSeinEntityHandle CancelEntity;
		USeinAbility* CancelledAbility = nullptr;
		const auto AuthorState = [&]()
		{
			World->RegisterPlayer(Player, FSeinFactionID(1));
			CancelEntity = World->SpawnAbstractEntity(
				FFixedTransform(), Player);
			World->AddComponent(CancelEntity, FSeinAbilityComponent());
			CancelledAbility = GrantAbility(*World, CancelEntity,
				USeinCallbackRevokeOnCancelAbility::StaticClass(),
				SeinARTSTags::Command_Context_AbilityTriggered);
		};
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Materialize(
			*World, AuthorState)));
		ASSERT_THAT(IsNotNull(CancelledAbility));
		FSeinCommand Activate = FSeinCommand::MakeAbilityCommand(
			Player, CancelEntity, CancelledAbility->AbilityTag);
		SubmitAuthorizedDraft(*World, Activate);
		TickOnce(*World);
		const FSeinAbilityComponent* AbilityComponent =
			World->GetComponent<FSeinAbilityComponent>(CancelEntity);
		ASSERT_THAT(IsNotNull(AbilityComponent));
		const int32 RecycledCancelID = AbilityComponent->ActiveAbilityID;

		FSeinCommand Cancel = FSeinCommand::MakeCancelCommand(Player, CancelEntity);
		SubmitAuthorizedDraft(*World, Cancel);
		TickOnce(*World);
		AbilityComponent = World->GetComponent<FSeinAbilityComponent>(CancelEntity);
		ASSERT_THAT(IsNotNull(AbilityComponent));
		ASSERT_THAT(AreEqual(RecycledCancelID, AbilityComponent->ActiveAbilityID));
		USeinAbility* Replacement =
			World->GetAbilityInstance(AbilityComponent->ActiveAbilityID);
		ASSERT_THAT(IsNotNull(Replacement));
		ASSERT_THAT(IsTrue(Replacement->IsA(
			USeinCallbackCancelReplacementAbility::StaticClass())));
		ASSERT_THAT(IsTrue(Replacement->bIsActive));

		FSeinEntityHandle BrokerMember;
		USeinAbility* BrokerAbility = nullptr;
		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*World);
			BrokerMember = World->SpawnAbstractEntity(FFixedTransform(), Player);
			World->AddComponent(BrokerMember, FSeinAbilityComponent());
			BrokerAbility = GrantAbility(*World, BrokerMember,
				USeinCallbackRevokeOnCancelAbility::StaticClass(),
				SeinARTSTags::Command_Context_AbilityTriggered);
		}
		ASSERT_THAT(IsNotNull(BrokerAbility));
		Activate = FSeinCommand::MakeAbilityCommand(
			Player, BrokerMember, BrokerAbility->AbilityTag);
		SubmitAuthorizedDraft(*World, Activate);
		TickOnce(*World);

		FSeinEntityHandle OldBroker;
		FSeinCommandBrokerData OldBrokerData;
		OldBrokerData.Members.Add(BrokerMember);
		OldBrokerData.bSelfCullOnEmpty = false;
		FSeinBrokerMembershipData Membership;
		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*World);
			OldBroker = World->SpawnAbstractEntity(FFixedTransform(), Player);
			World->AddComponent(OldBroker, OldBrokerData);
			Membership.CurrentBrokerHandle = OldBroker;
			World->AddComponent(BrokerMember, Membership);
		}

		FSeinBrokerQueuedOrder ReplacementOrder;
		ReplacementOrder.Context.AddTag(
			SeinARTSTags::Command_Context_AbilityTriggered);
		ReplacementOrder.PredeterminedAbilityTag =
			SeinARTSTags::Command_Context_AbilityTriggered;
		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*World);
			World->CreateBrokerForMembers(
				{BrokerMember}, Player, ReplacementOrder);
		}

		AbilityComponent = World->GetComponent<FSeinAbilityComponent>(BrokerMember);
		ASSERT_THAT(IsNotNull(AbilityComponent));
		Replacement = World->GetAbilityInstance(AbilityComponent->ActiveAbilityID);
		ASSERT_THAT(IsNotNull(Replacement));
		ASSERT_THAT(IsTrue(Replacement->IsA(
			USeinCallbackCancelReplacementAbility::StaticClass())));
		ASSERT_THAT(IsTrue(Replacement->bIsActive));
	}
}
