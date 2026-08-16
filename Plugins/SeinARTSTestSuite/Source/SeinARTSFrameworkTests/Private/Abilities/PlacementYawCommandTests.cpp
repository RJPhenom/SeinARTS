#include "CQTest.h"
#include "Components/ActorTestSpawner.h"

#include "Abilities/SeinTargeterSpec.h"
#include "Actor/SeinEntityComponent.h"
#include "Brokers/SeinBrokerTypes.h"
#include "Components/SeinAbilityComponent.h"
#include "Components/SeinExtentsComponent.h"
#include "Components/SeinExtentsHelpers.h"
#include "Containers/Ticker.h"
#include "Lib/SeinAbilityBPFL.h"
#include "Simulation/SeinTestMatchBootstrap.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Tags/SeinARTSGameplayTags.h"
#include "TestTypes/SeinPlacementYawTestTypes.h"

namespace
{
	struct FScopedPlacementBuildingExtents
	{
		FScopedPlacementBuildingExtents()
		{
			TArray<const USeinEntityComponent*> Bridges;
			AActor::GetActorClassDefaultComponents<USeinEntityComponent>(
				ASeinPlacementYawTestBuilding::StaticClass(), Bridges);
			check(!Bridges.IsEmpty());
			Bridge = const_cast<USeinEntityComponent*>(Bridges[0]);
			PreviousComponentData = Bridge->ComponentData;

			FSeinExtentsComponent Extents;
			Extents.Shapes.AddDefaulted();
			Bridge->ComponentData.Add(FInstancedStruct::Make(Extents));
		}

		~FScopedPlacementBuildingExtents()
		{
			Bridge->ComponentData = MoveTemp(PreviousComponentData);
		}

		USeinEntityComponent* Bridge = nullptr;
		TArray<FInstancedStruct> PreviousComponentData;
	};
}

namespace UE::SeinARTSTests
{
	TEST(FreeRotationPlacementUsesCapturedYaw, "SeinARTS.Integration.Commands")
	{
		TestRunner->AddExpectedError(
			TEXT("Component 'SeinAbilityComponent' has field(s) excluded from the legacy local state fingerprint"),
			EAutomationExpectedErrorFlags::Contains, 1, false);
		TestRunner->AddExpectedError(
			TEXT("footprint blocked"),
			EAutomationExpectedErrorFlags::Contains, 1, false);

		FScopedPlacementBuildingExtents BuildingExtents;
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World = Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));
		const FSeinPlayerID Player(1);
		FSeinEntityHandle Entity;
		const auto AuthorState = [&]()
		{
			World->RegisterPlayer(Player, FSeinFactionID(1));
			Entity = World->SpawnAbstractEntity(FFixedTransform(), Player);
			World->AddComponent(Entity, FSeinAbilityComponent());
			const int32 AbilityID = USeinAbilityBPFL::SeinGrantAbility(
				World, Entity, USeinPlacementYawTestAbility::StaticClass());
			ASSERT_THAT(IsTrue(AbilityID != INDEX_NONE));
			USeinAbility* GrantedAbility = World->GetAbilityInstance(AbilityID);
			ASSERT_THAT(IsNotNull(GrantedAbility));
			GrantedAbility->AbilityTag =
				SeinARTSTags::Command_Context_AbilityTriggered;
			GrantedAbility->bRequiresFreeFootprint = true;
			USeinPointFacingTargeterSpec* Targeter =
				NewObject<USeinPointFacingTargeterSpec>(GrantedAbility);
			Targeter->BuildingClass =
				ASeinPlacementYawTestBuilding::StaticClass();
			Targeter->RotationStepDegrees = 0;
			GrantedAbility->TargeterSpec = Targeter;

			const FSeinAbilityComponent* AbilityComponent =
				World->GetComponent<FSeinAbilityComponent>(Entity);
			ASSERT_THAT(IsNotNull(AbilityComponent));
			const USeinAbility* Ability = AbilityComponent->FindAbilityByTag(
				*World, SeinARTSTags::Command_Context_AbilityTriggered);
			ASSERT_THAT(AreEqual(GrantedAbility, Ability));
			ASSERT_THAT(IsNotNull(
				Cast<USeinPointFacingTargeterSpec>(Ability->TargeterSpec)));
			ASSERT_THAT(IsNotNull(SeinExtentsHelpers::GetPrimaryExtentsShape(
				ASeinPlacementYawTestBuilding::StaticClass())));
		};
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Materialize(
			*World,
			AuthorState,
			FSeinMatchSettings(),
			0,
			TEXT("SeinARTS.PlacementYaw"))));

		const FFixedPoint ExpectedYaw = FFixedPoint::FromInt(137);
		bool bResolverCalled = false;
		FFixedPoint CapturedYaw = FFixedPoint::Zero;
		World->FootprintPlacementResolver.BindLambda(
			[&](const FFixedVector&, const FFixedPoint& YawDegrees,
				const FSeinExtentsShape&, uint8)
			{
				bResolverCalled = true;
				CapturedYaw = YawDegrees;
				return false;
			});

		FSeinBrokerOrderPayload Payload;
		Payload.PredeterminedAbilityTag = SeinARTSTags::Command_Context_AbilityTriggered;
		FSeinTargeterPoint Point(FFixedVector::ZeroVector);
		Point.RotationStep = 7; // Old step*0 path incorrectly produced zero yaw.
		Point.YawDegrees = ExpectedYaw;
		Payload.TargeterPoints.Add(Point);

		FSeinCommand Command;
		Command.PlayerID = Player;
		Command.CommandType = SeinARTSTags::Command_Type_BrokerOrder;
		Command.SchemaVersion = SeinBrokerOrderProtocol::SchemaVersion;
		Command.EntityList.Add(Entity);
		Command.Payload = FInstancedStruct::Make(Payload);
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Start(*World)));
		World->SubmitLocalCommandDraft(Command);

		FTSTicker::GetCoreTicker().Tick(World->GetFixedDeltaTimeSeconds());
		World->StopSimulation();

		ASSERT_THAT(IsTrue(bResolverCalled));
		ASSERT_THAT(AreEqual(ExpectedYaw.Value, CapturedYaw.Value));
	}
}
