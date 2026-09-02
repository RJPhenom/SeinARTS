#include "CQTest.h"
#include "Components/ActorTestSpawner.h"

#include "Brokers/SeinBrokerTypes.h"
#include "Brokers/SeinDefaultCommandBrokerResolver.h"
#include "Components/SeinAbilityPayload.h"
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
		WorldSubsystem->AddComponent(OwnerEntity, FSeinAbilityPayload());
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
	WorldSubsystem->AddComponent(GrowthEntity, FSeinAbilityPayload());
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
}

USeinCallbackTickProbeAbility::FTickCallback
	USeinCallbackTickProbeAbility::TickCallback;

void USeinCallbackTickProbeAbility::OnTick_Implementation(
	FFixedPoint /*DeltaTime*/)
{
	++TickCount;
	if (TickCallback)
	{
		TickCallback(*this);
	}
}

USeinCallbackPassiveIdentityAbility::FActivationCallback
	USeinCallbackPassiveIdentityAbility::ActivationCallback;

USeinCallbackPassiveIdentityAbility::USeinCallbackPassiveIdentityAbility()
{
	bIsPassive = true;
}

void USeinCallbackPassiveIdentityAbility::OnActivate_Implementation()
{
	if (ActivationCallback)
	{
		ActivationCallback(*this);
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
	TEST(PassiveIdentityIsPublishedDuringActivationAndRemovedOnEnd,
		"SeinARTS.Unit.Abilities.CallbackSafety")
	{
		struct FResetPassiveCallback
		{
			~FResetPassiveCallback()
			{
				USeinCallbackPassiveIdentityAbility::ActivationCallback = nullptr;
			}
		} ResetPassiveCallback;
		ExpectAbilityHashDiagnostic(*TestRunner);
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World =
			Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));

		FSeinEntityHandle Entity;
		int32 PassiveID = INDEX_NONE;
		bool bObservedExactIdentity = false;
		USeinCallbackPassiveIdentityAbility::ActivationCallback =
			[&](USeinCallbackPassiveIdentityAbility& Ability)
			{
				const FSeinAbilityPayload* Component =
					World->GetComponent<FSeinAbilityPayload>(Entity);
				bObservedExactIdentity = Component
					&& Component->ActivePassiveIDs.Contains(
						Ability.GetRuntimePoolID())
					&& Component->GetActivePassives(*World).Contains(&Ability);
			};
		const auto AuthorState = [&]()
		{
			Entity = World->SpawnAbstractEntity(
				FFixedTransform(), FSeinPlayerID::Neutral());
			World->AddComponent(Entity, FSeinAbilityPayload());
			PassiveID = USeinAbilityBPFL::SeinGrantAbility(
				World, Entity,
				USeinCallbackPassiveIdentityAbility::StaticClass());
		};
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Materialize(
			*World, AuthorState)));
		ASSERT_THAT(IsTrue(bObservedExactIdentity));
		ASSERT_THAT(IsTrue(PassiveID != INDEX_NONE));
		USeinAbility* Passive = World->GetAbilityInstance(PassiveID);
		ASSERT_THAT(IsNotNull(Passive));
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Start(*World)));
		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*World);
			Passive->EndAbility();
		}
		const FSeinAbilityPayload* Component =
			World->GetComponent<FSeinAbilityPayload>(Entity);
		ASSERT_THAT(IsNotNull(Component));
		ASSERT_THAT(IsFalse(Passive->bIsActive));
		ASSERT_THAT(IsFalse(Component->ActivePassiveIDs.Contains(PassiveID)));
	}

	TEST(AbilityLifecycleOwnsPrimaryIdentityAndRefusesSilentDisplacement,
		"SeinARTS.Unit.Abilities.CallbackSafety")
	{
		ExpectAbilityHashDiagnostic(*TestRunner);
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World =
			Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));

		FSeinEntityHandle Entity;
		USeinAbility* First = nullptr;
		USeinAbility* Second = nullptr;
		const auto AuthorState = [&]()
		{
			Entity = World->SpawnAbstractEntity(
				FFixedTransform(), FSeinPlayerID::Neutral());
			World->AddComponent(Entity, FSeinAbilityPayload());
			First = GrantAbility(*World, Entity,
				USeinCallbackCancelReplacementAbility::StaticClass(),
				SeinARTSTags::Command_Context_AbilityTriggered);
			Second = GrantAbility(*World, Entity,
				USeinCallbackGrowComponentStorageAbility::StaticClass(),
				SeinARTSTags::Command_Context_Target_Ground);
		};
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Materialize(
			*World, AuthorState)));
		ASSERT_THAT(IsNotNull(First));
		ASSERT_THAT(IsNotNull(Second));
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Start(*World)));

		auto SimScope = FSeinSimContextTestAccess::Enter(*World);
		ASSERT_THAT(IsTrue(First->ActivateAbility(
			FSeinEntityHandle::Invalid(), FFixedVector::ZeroVector)));
		const int32 FirstID = First->GetRuntimePoolID();
		const int32 SecondID = Second->GetRuntimePoolID();
		const FSeinAbilityPayload* Component =
			World->GetComponent<FSeinAbilityPayload>(Entity);
		ASSERT_THAT(IsNotNull(Component));
		ASSERT_THAT(AreEqual(FirstID, Component->ActiveAbilityID));

		TestRunner->AddExpectedError(
			TEXT("already has active primary"),
			EAutomationExpectedErrorFlags::Contains, 1, false);
		ASSERT_THAT(IsFalse(Second->ActivateAbility(
			FSeinEntityHandle::Invalid(), FFixedVector::ZeroVector)));
		Component = World->GetComponent<FSeinAbilityPayload>(Entity);
		ASSERT_THAT(IsNotNull(Component));
		ASSERT_THAT(AreEqual(FirstID, Component->ActiveAbilityID));
		ASSERT_THAT(IsTrue(First->bIsActive));
		ASSERT_THAT(IsFalse(Second->bIsActive));

		First->EndAbility();
		Component = World->GetComponent<FSeinAbilityPayload>(Entity);
		ASSERT_THAT(IsNotNull(Component));
		ASSERT_THAT(AreEqual(INDEX_NONE, Component->ActiveAbilityID));

		Second->CooldownStartTiming = ESeinCooldownStartTiming::OnEnd;
		Second->Cooldown = FFixedPoint::FromInt(5);
		ASSERT_THAT(IsTrue(Second->ActivateAbility(
			FSeinEntityHandle::Invalid(), FFixedVector::ZeroVector)));
		ASSERT_THAT(AreEqual(SecondID,
			World->GetComponent<FSeinAbilityPayload>(Entity)->ActiveAbilityID));
		Second->EndAbility();
		ASSERT_THAT(AreEqual(FFixedPoint::FromInt(5),
			Second->CooldownRemaining));
		Second->TickCooldown(FFixedPoint::FromInt(5));
		ASSERT_THAT(IsTrue(Second->ActivateAbility(
			FSeinEntityHandle::Invalid(), FFixedVector::ZeroVector)));
		Second->EndAbility();
		ASSERT_THAT(AreEqual(FFixedPoint::FromInt(5),
			Second->CooldownRemaining));
		ASSERT_THAT(AreEqual(INDEX_NONE,
			World->GetComponent<FSeinAbilityPayload>(Entity)->ActiveAbilityID));
	}

	TEST(AbilityTickUsesFrozenPhaseAndPassiveTraversalMembership,
		"SeinARTS.Unit.Abilities.CallbackSafety")
	{
		struct FResetProbeCallback
		{
			~FResetProbeCallback()
			{
				USeinCallbackTickProbeAbility::TickCallback = nullptr;
			}
		} ResetProbeCallback;
		ExpectAbilityHashDiagnostic(*TestRunner);
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World =
			Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));

		const FSeinPlayerID Player(1);
		FSeinEntityHandle ParentEntity;
		USeinCallbackTickProbeAbility* Parent = nullptr;
		struct FProbeInstance
		{
			USeinCallbackTickProbeAbility* Ability = nullptr;
			int32 ID = INDEX_NONE;
		};
		const auto CreateProbe = [&](FSeinEntityHandle Entity)
		{
			FProbeInstance Result;
			Result.Ability = NewObject<USeinCallbackTickProbeAbility>(World);
			Result.Ability->InitializeAbility(Entity, World);
			Result.ID = World->RegisterAbilityInstance(Result.Ability);
			FSeinAbilityPayload* Component =
				World->GetComponentMutable<FSeinAbilityPayload>(Entity);
			Component->AbilityInstanceIDs.Add(Result.ID);
			Component->AbilityGrantOwnership.AddDefaulted();
			Result.Ability->bIsActive = true;
			return Result;
		};
		const auto AuthorState = [&]()
		{
			World->RegisterPlayer(Player, FSeinFactionID(1));
			ParentEntity = World->SpawnAbstractEntity(
				FFixedTransform(), Player);
			World->AddComponent(ParentEntity, FSeinAbilityPayload());
			const FProbeInstance ParentProbe = CreateProbe(ParentEntity);
			Parent = ParentProbe.Ability;
			World->GetComponentMutable<FSeinAbilityPayload>(
				ParentEntity)->ActiveAbilityID = ParentProbe.ID;
		};
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Materialize(
			*World, AuthorState)));
		ASSERT_THAT(IsNotNull(Parent));

		USeinCallbackTickProbeAbility* SpawnedEntityAbility = nullptr;
		USeinCallbackTickProbeAbility* AddedDuringPassive = nullptr;
		USeinCallbackTickProbeAbility* FirstPassive = nullptr;
		USeinCallbackTickProbeAbility::TickCallback =
			[&](USeinCallbackTickProbeAbility& Ability)
			{
				if (&Ability == Parent && !SpawnedEntityAbility)
				{
					const FSeinEntityHandle Spawned =
						World->SpawnAbstractEntity(FFixedTransform(), Player);
					World->AddComponent(Spawned, FSeinAbilityPayload());
					const FProbeInstance SpawnedProbe = CreateProbe(Spawned);
					SpawnedEntityAbility = SpawnedProbe.Ability;
					World->GetComponentMutable<FSeinAbilityPayload>(Spawned)
						->ActiveAbilityID = SpawnedProbe.ID;

					const FProbeInstance PassiveProbe = CreateProbe(ParentEntity);
					FirstPassive = PassiveProbe.Ability;
					World->GetComponentMutable<FSeinAbilityPayload>(
						ParentEntity)->ActivePassiveIDs.Add(PassiveProbe.ID);
				}
				else if (&Ability == FirstPassive && !AddedDuringPassive)
				{
					const FProbeInstance PassiveProbe = CreateProbe(ParentEntity);
					AddedDuringPassive = PassiveProbe.Ability;
					World->GetComponentMutable<FSeinAbilityPayload>(
						ParentEntity)->ActivePassiveIDs.Add(PassiveProbe.ID);
				}
			};

		TickOnce(*World);
		ASSERT_THAT(IsNotNull(SpawnedEntityAbility));
		ASSERT_THAT(IsNotNull(FirstPassive));
		ASSERT_THAT(IsNotNull(AddedDuringPassive));
		ASSERT_THAT(AreEqual(0, SpawnedEntityAbility->TickCount));
		ASSERT_THAT(AreEqual(1, FirstPassive->TickCount));
		ASSERT_THAT(AreEqual(0, AddedDuringPassive->TickCount));

		TickOnce(*World);
		ASSERT_THAT(AreEqual(1, SpawnedEntityAbility->TickCount));
		ASSERT_THAT(AreEqual(2, FirstPassive->TickCount));
		ASSERT_THAT(AreEqual(1, AddedDuringPassive->TickCount));
	}

	TEST(ActivationCallbacksCannotResurrectOrWriteThroughInvalidatedAbilityState,
		"SeinARTS.Unit.Abilities.CallbackSafety")
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
			World->AddComponent(EndingEntity, FSeinAbilityPayload());
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
		const FSeinAbilityPayload* AbilityComponent =
			World->GetComponent<FSeinAbilityPayload>(EndingEntity);
		ASSERT_THAT(IsNotNull(AbilityComponent));
		ASSERT_THAT(IsFalse(EndingAbility->bIsActive));
		ASSERT_THAT(AreEqual(INDEX_NONE, AbilityComponent->ActiveAbilityID));

		FSeinEntityHandle GrowthEntity;
		USeinAbility* GrowthAbility = nullptr;
		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*World);
			GrowthEntity = World->SpawnAbstractEntity(FFixedTransform(), Player);
			World->AddComponent(GrowthEntity, FSeinAbilityPayload());
			GrowthAbility = GrantAbility(*World, GrowthEntity,
				USeinCallbackGrowComponentStorageAbility::StaticClass(),
				SeinARTSTags::Command_Context_Target_Neutral);
		}
		ASSERT_THAT(IsNotNull(GrowthAbility));
		Command = FSeinCommand::MakeAbilityCommand(
			Player, GrowthEntity, GrowthAbility->AbilityTag);
		SubmitAuthorizedDraft(*World, Command);
		TickOnce(*World);
		AbilityComponent = World->GetComponent<FSeinAbilityPayload>(GrowthEntity);
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
			World->AddComponent(ReplacedEntity, FSeinAbilityPayload());
			ReplacingAbility = GrantAbility(*World, ReplacedEntity,
				USeinCallbackReplaceComponentAbility::StaticClass(),
				SeinARTSTags::Command_Context_Target_Ground);
		}
		ASSERT_THAT(IsNotNull(ReplacingAbility));
		Command = FSeinCommand::MakeAbilityCommand(
			Player, ReplacedEntity, ReplacingAbility->AbilityTag);
		SubmitAuthorizedDraft(*World, Command);
		TickOnce(*World);
		AbilityComponent = World->GetComponent<FSeinAbilityPayload>(ReplacedEntity);
		ASSERT_THAT(IsNotNull(AbilityComponent));
		ASSERT_THAT(AreEqual(0, AbilityComponent->AbilityInstanceIDs.Num()));
		ASSERT_THAT(AreEqual(INDEX_NONE, AbilityComponent->ActiveAbilityID));
	}

	TEST(CancelAndBrokerReplacementPreserveCallbackInstalledAbilityIdentity,
		"SeinARTS.Unit.Abilities.CallbackSafety")
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
			World->AddComponent(CancelEntity, FSeinAbilityPayload());
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
		const FSeinAbilityPayload* AbilityComponent =
			World->GetComponent<FSeinAbilityPayload>(CancelEntity);
		ASSERT_THAT(IsNotNull(AbilityComponent));
		const int32 RecycledCancelID = AbilityComponent->ActiveAbilityID;

		FSeinCommand Cancel = FSeinCommand::MakeCancelCommand(Player, CancelEntity);
		SubmitAuthorizedDraft(*World, Cancel);
		TickOnce(*World);
		AbilityComponent = World->GetComponent<FSeinAbilityPayload>(CancelEntity);
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
			World->AddComponent(BrokerMember, FSeinAbilityPayload());
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

		AbilityComponent = World->GetComponent<FSeinAbilityPayload>(BrokerMember);
		ASSERT_THAT(IsNotNull(AbilityComponent));
		Replacement = World->GetAbilityInstance(AbilityComponent->ActiveAbilityID);
		ASSERT_THAT(IsNotNull(Replacement));
		ASSERT_THAT(IsTrue(Replacement->IsA(
			USeinCallbackCancelReplacementAbility::StaticClass())));
		ASSERT_THAT(IsTrue(Replacement->bIsActive));
	}
}
