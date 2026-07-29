#include "CQTest.h"
#include "Components/ActorTestSpawner.h"

#include "Components/SeinAbilityComponent.h"
#include "Brokers/SeinDefaultCommandBrokerResolver.h"
#include "Containers/Ticker.h"
#include "Simulation/SeinTestSimContext.h"
#include "Input/SeinCommand.h"
#include "Lib/SeinAbilityBPFL.h"
#include "Settings/PluginSettings.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Simulation/SeinTestMatchBootstrap.h"
#include "Tags/SeinARTSGameplayTags.h"
#include "TestTypes/SeinPayerContinuityTestTypes.h"

bool USeinPayerContinuityAuthorityPolicy::AuthorizeCommand_Implementation(
	const USeinCommandAuthorityView* View,
	const FSeinCommand& Command,
	ESeinCommandAuthorityScope Scope,
	FGameplayTag& OutRejectionReason) const
{
	(void)Scope;
	OutRejectionReason = FGameplayTag();
	return View && Command.IssuerKind != ESeinCommandIssuerKind::Unauthenticated;
}

bool USeinPayerContinuityAuthorityPolicy::CanControlEntity_Implementation(
	const USeinCommandAuthorityView* View,
	const FSeinCommand& Command,
	FSeinEntityHandle Entity) const
{
	(void)Command;
	return View && View->IsEntityValid(Entity);
}

FSeinPlayerID USeinPayerContinuityAuthorityPolicy::ResolveResourcePayer_Implementation(
	const USeinCommandAuthorityView* View,
	const FSeinCommand& Command,
	FSeinEntityHandle Entity) const
{
	(void)View;
	(void)Entity;
	return Command.PlayerID;
}

namespace
{
	constexpr int32 StartingBalance = 100;
	constexpr int32 AbilityCost = 25;

	struct FScopedPayerTestSettings
	{
		FScopedPayerTestSettings()
		{
			Settings = GetMutableDefault<USeinARTSCoreSettings>();
			check(Settings);
			PreviousAuthorityPolicy = Settings->CommandAuthorityPolicyClass;
			PreviousResourceCatalog = Settings->ResourceCatalog;
			PreviousBrokerResolver = Settings->DefaultBrokerResolverClass;

			Settings->CommandAuthorityPolicyClass = FSoftClassPath(
				USeinPayerContinuityAuthorityPolicy::StaticClass()->GetPathName());
			FSeinResourceDefinition Resource;
			Resource.ResourceTag = SeinARTSTags::Resource;
			Resource.DefaultStartingValue = FFixedPoint::FromInt(StartingBalance);
			Resource.CostDirection = ESeinCostDirection::DeductFromBalance;
			Resource.SpendBehavior = ESeinResourceSpendBehavior::RejectOnInsufficient;
			Resource.ProductionDeductionTiming =
				ESeinProductionDeductionTiming::AtEnqueue;
			Settings->ResourceCatalog = {Resource};
			Settings->DefaultBrokerResolverClass =
				USeinDefaultCommandBrokerResolver::StaticClass();
		}

		~FScopedPayerTestSettings()
		{
			Settings->CommandAuthorityPolicyClass = PreviousAuthorityPolicy;
			Settings->ResourceCatalog = MoveTemp(PreviousResourceCatalog);
			Settings->DefaultBrokerResolverClass = PreviousBrokerResolver;
		}

		USeinARTSCoreSettings* Settings = nullptr;
		FSoftClassPath PreviousAuthorityPolicy;
		TArray<FSeinResourceDefinition> PreviousResourceCatalog;
		TSoftClassPtr<USeinCommandBrokerResolver> PreviousBrokerResolver;
	};

	template <typename TTestRunner>
	void ExpectAbilityHashDiagnostic(TTestRunner& TestRunner)
	{
		TestRunner.AddExpectedError(
			TEXT("Component 'SeinAbilityComponent' has field(s) excluded from the determinism state hash"),
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

	void SetPaidCost(USeinAbility& Ability)
	{
		Ability.ResourceCost.Amounts.Reset();
		Ability.ResourceCost.Amounts.Add(
			SeinARTSTags::Resource, FFixedPoint::FromInt(AbilityCost));
	}

	int64 ResourceValue(const USeinWorldSubsystem& World, FSeinPlayerID Player)
	{
		const FSeinPlayerState* State = World.GetPlayerState(Player);
		return State
			? State->GetResource(SeinARTSTags::Resource).Value
			: MIN_int64;
	}
}

namespace UE::SeinARTSTests
{
	TEST(AutoMoveThenPreservesThePreflightPayerThroughChargeAndRefund,
		"SeinARTS.Sim.Authority.DerivedPayer")
	{
		ExpectAbilityHashDiagnostic(*TestRunner);
		FScopedPayerTestSettings Settings;
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World =
			Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));

		const FSeinPlayerID PayerA(1);
		const FSeinPlayerID PayerB(2);
		FSeinEntityHandle Entity;
		USeinAbility* MoveAbility = nullptr;
		USeinAbility* PaidAbility = nullptr;
		const auto AuthorState = [&]()
		{
			World->RegisterPlayer(PayerA, FSeinFactionID(1));
			World->RegisterPlayer(PayerB, FSeinFactionID(1));
			Entity = World->SpawnAbstractEntity(FFixedTransform(), PayerA);
			World->AddComponent(Entity, FSeinAbilityComponent());

			MoveAbility = GrantAbility(*World, Entity,
				USeinPayerContinuityMoveAbility::StaticClass(),
				SeinARTSTags::Command_Context_Target_Ground);
			PaidAbility = GrantAbility(*World, Entity,
				USeinPayerContinuityAbility::StaticClass(),
				SeinARTSTags::Command_Context_AbilityTriggered);
			if (MoveAbility && PaidAbility)
			{
				MoveAbility->bIsMoveAbility = true;
				PaidAbility->MaxRange = FFixedPoint::FromInt(1);
				PaidAbility->OutOfRangeBehavior =
					ESeinOutOfRangeBehavior::AutoMoveThen;
				SetPaidCost(*PaidAbility);
			}

			if (FSeinPlayerState* StateB = World->GetPlayerState(PayerB))
			{
				StateB->SetResource(
					SeinARTSTags::Resource, FFixedPoint::Zero);
			}
		};
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Materialize(
			*World, AuthorState)));
		ASSERT_THAT(IsNotNull(MoveAbility));
		ASSERT_THAT(IsNotNull(PaidAbility));
		ASSERT_THAT(AreEqual(
			FFixedPoint::Zero.Value, ResourceValue(*World, PayerB)));

		const FFixedVector FarTarget(
			FFixedPoint::FromInt(100), FFixedPoint::Zero, FFixedPoint::Zero);
		FSeinCommand Activate = FSeinCommand::MakeAbilityCommand(
			PayerA, Entity, PaidAbility->AbilityTag,
			FSeinEntityHandle::Invalid(), FarTarget);
		SubmitAuthorizedDraft(*World, Activate);
		TickOnce(*World);
		ASSERT_THAT(AreEqual(
			FFixedPoint::FromInt(StartingBalance).Value,
			ResourceValue(*World, PayerA)));

		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*World);
			World->SetEntityOwner(Entity, PayerB);
			FSeinEntity* SimEntity = World->GetEntity(Entity);
			ASSERT_THAT(IsNotNull(SimEntity));
			SimEntity->Transform.SetLocation(FarTarget);
		}
		TickOnce(*World);
		ASSERT_THAT(IsTrue(MoveAbility->bIsActive));

		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*World);
			MoveAbility->EndAbility();
		}
		TickOnce(*World);
		TickOnce(*World);
		ASSERT_THAT(IsTrue(PaidAbility->bIsActive));
		ASSERT_THAT(IsTrue(PaidAbility->ResourcePayer == PayerA));
		ASSERT_THAT(AreEqual(
			FFixedPoint::FromInt(StartingBalance - AbilityCost).Value,
			ResourceValue(*World, PayerA)));
		ASSERT_THAT(AreEqual(
			FFixedPoint::Zero.Value, ResourceValue(*World, PayerB)));

		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*World);
			PaidAbility->CancelAbility();
			PaidAbility->CancelAbility();
		}
		ASSERT_THAT(AreEqual(
			FFixedPoint::FromInt(StartingBalance).Value,
			ResourceValue(*World, PayerA)));
		ASSERT_THAT(AreEqual(
			FFixedPoint::Zero.Value, ResourceValue(*World, PayerB)));
	}

	TEST(ExternalIngressCannotForgeADerivedResourcePayer,
		"SeinARTS.Sim.Authority.DerivedPayer")
	{
		ExpectAbilityHashDiagnostic(*TestRunner);
		TestRunner->AddExpectedError(
			TEXT("outside simulation context"),
			EAutomationExpectedErrorFlags::Contains, 1, false);
		FScopedPayerTestSettings Settings;
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World =
			Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));

		const FSeinPlayerID PayerA(1);
		const FSeinPlayerID ForgedPayer(2);
		FSeinEntityHandle Entity;
		USeinAbility* Ability = nullptr;
		const auto AuthorState = [&]()
		{
			World->RegisterPlayer(PayerA, FSeinFactionID(1));
			World->RegisterPlayer(ForgedPayer, FSeinFactionID(1));
			Entity = World->SpawnAbstractEntity(FFixedTransform(), PayerA);
			World->AddComponent(Entity, FSeinAbilityComponent());
			Ability = GrantAbility(*World, Entity,
				USeinPayerContinuityAbility::StaticClass(),
				SeinARTSTags::Command_Context_AbilityTriggered);
			if (Ability)
			{
				SetPaidCost(*Ability);
			}
		};
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Materialize(
			*World, AuthorState)));
		ASSERT_THAT(IsNotNull(Ability));

		FSeinCommand Forged = FSeinCommand::MakeAbilityCommand(
			PayerA, Entity, Ability->AbilityTag);
		Forged.IssuerKind = ESeinCommandIssuerKind::DeterministicSystem;
		Forged.DerivedResourcePayer = ForgedPayer;
		World->EnqueueDerivedCommand(Forged);
		TickOnce(*World);
		ASSERT_THAT(IsFalse(Ability->bIsActive));

		SubmitAuthorizedDraft(*World, Forged);
		TickOnce(*World);
		ASSERT_THAT(IsTrue(Ability->bIsActive));
		ASSERT_THAT(IsTrue(Ability->ResourcePayer == PayerA));
		ASSERT_THAT(AreEqual(
			FFixedPoint::FromInt(StartingBalance - AbilityCost).Value,
			ResourceValue(*World, PayerA)));
		ASSERT_THAT(AreEqual(
			FFixedPoint::FromInt(StartingBalance).Value,
			ResourceValue(*World, ForgedPayer)));
	}

	TEST(DerivedResourcePayerIsRejectedOnNonActivationCommands,
		"SeinARTS.Sim.Authority.DerivedPayer")
	{
		ExpectAbilityHashDiagnostic(*TestRunner);
		TestRunner->AddExpectedError(
			TEXT("carrying an inapplicable resource payer"),
			EAutomationExpectedErrorFlags::Contains, 1, false);
		FScopedPayerTestSettings Settings;
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World =
			Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));

		const FSeinPlayerID Player(1);
		FSeinEntityHandle Entity;
		USeinAbility* Ability = nullptr;
		const auto AuthorState = [&]()
		{
			World->RegisterPlayer(Player, FSeinFactionID(1));
			Entity = World->SpawnAbstractEntity(FFixedTransform(), Player);
			World->AddComponent(Entity, FSeinAbilityComponent());
			Ability = GrantAbility(*World, Entity,
				USeinPayerContinuityAbility::StaticClass(),
				SeinARTSTags::Command_Context_AbilityTriggered);
		};
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Materialize(
			*World, AuthorState)));
		ASSERT_THAT(IsNotNull(Ability));

		FSeinCommand Activate = FSeinCommand::MakeAbilityCommand(
			Player, Entity, Ability->AbilityTag);
		SubmitAuthorizedDraft(*World, Activate);
		TickOnce(*World);
		ASSERT_THAT(IsTrue(Ability->bIsActive));

		FSeinCommand InvalidDerived =
			FSeinCommand::MakeCancelCommand(Player, Entity);
		InvalidDerived.DerivedResourcePayer = Player;
		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*World);
			World->EnqueueDerivedCommand(InvalidDerived);
		}
		TickOnce(*World);
		ASSERT_THAT(IsTrue(Ability->bIsActive));
	}
}
