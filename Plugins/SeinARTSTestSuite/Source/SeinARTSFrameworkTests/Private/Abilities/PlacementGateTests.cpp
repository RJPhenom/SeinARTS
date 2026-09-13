#include "CQTest.h"
#include "Components/ActorTestSpawner.h"
#include "Abilities/SeinPlacementValidation.h"
#include "Components/SeinAbilityPayload.h"
#include "Components/SeinConstructionPayload.h"
#include "Components/SeinExtentsHelpers.h"
#include "Containers/Ticker.h"
#include "Data/SeinWorldSnapshot.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Lib/SeinAbilityBPFL.h"
#include "Lib/SeinConstructionBPFL.h"
#include "Settings/PluginSettings.h"
#include "Simulation/SeinTestMatchBootstrap.h"
#include "Simulation/SeinTestSimContext.h"
#include "Simulation/SeinTestSnapshotRestore.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Tags/SeinARTSGameplayTags.h"
#include "Targeter/SeinTargeterPreview.h"
#include "TestTypes/SeinPlacementGateTestTypes.h"
#include "TestTypes/SeinPlacementYawTestTypes.h"
#include "TestTypes/SeinPreviewContractTestTypes.h"

USeinPlacementGateTestAbility::USeinPlacementGateTestAbility()
{
	AbilityTag = SeinARTSTags::Command_Context_AbilityTriggered;
	bRequiresFreeFootprint = true;
	ResourceCost.Amounts.Add(SeinARTSTags::Resource, FFixedPoint::FromInt(10));
	auto* Spec = CreateDefaultSubobject<USeinPointFacingTargeterSpec>(TEXT("PlacementSpec"));
	Spec->BuildingClass = ASeinPlacementYawTestBuilding::StaticClass();
	Spec->RotationStepDegrees = 90;
	TargeterSpec = Spec;
}

void USeinPlacementGateTestAbility::OnActivate_Implementation()
{
	const auto* Extents = SeinExtentsHelpers::GetExtentsFromActorClass(SeinPlacementValidation::ResolveActorClass(*this).LoadSynchronous());
	const auto Site = WorldSubsystem->SpawnAbstractEntity(FFixedTransform(TargeterPoints[0].Location), ResourcePayer);
	WorldSubsystem->AddComponent(Site, *Extents);
	FSeinConstructionPayload Construction;
	Construction.bQueueConstructionOnSpawn = true;
	WorldSubsystem->AddComponent(Site, Construction);
	USeinConstructionBPFL::InitializeAtSpawn(*WorldSubsystem, Site);
	EndAbility();
}

USeinPlacementPointTestAbility::USeinPlacementPointTestAbility()
{
 Placement.ActorClass = ASeinPlacementYawTestBuilding::StaticClass();
 TargeterSpec = CreateDefaultSubobject<USeinPointTargeterSpec>(TEXT("PointSpec"));
}

namespace UE::SeinARTSTests
{
namespace PlacementGateLocal
{
	FFixedVector Point(int32 X, int32 Y = 0)
	{
		return FFixedVector(FFixedPoint::FromInt(X), FFixedPoint::FromInt(Y), FFixedPoint::Zero);
	}
	FSeinExtentsShape Box(int32 X, int32 Y)
	{
		FSeinExtentsShape Shape;
		Shape.Shape = ESeinExtentsShape::Box;
		Shape.HalfExtentX = FFixedPoint::FromInt(X);
		Shape.HalfExtentY = FFixedPoint::FromInt(Y);
		return Shape;
	}
	struct FDefinition
	{
		USeinEntityBridgeComponent* Bridge;
		TArray<FInstancedStruct> Previous;
		TArray<FSeinResourceDefinition> Catalog;
		FDefinition()
		{
			TArray<const USeinEntityBridgeComponent*> Bridges;
			AActor::GetActorClassDefaultComponents<USeinEntityBridgeComponent>(ASeinPlacementYawTestBuilding::StaticClass(), Bridges);
			Bridge = const_cast<USeinEntityBridgeComponent*>(Bridges[0]);
			Previous = Bridge->ComponentData;
			FSeinExtentsPayload Extents;
			Extents.bBlocksNav = true;
			Extents.Shapes.Add(Box(100, 25));
			Bridge->ComponentData = {FInstancedStruct::Make(Extents)};
			auto* Settings = GetMutableDefault<USeinARTSCoreSettings>();
			Catalog = Settings->ResourceCatalog;
			FSeinResourceDefinition Resource;
			Resource.ResourceTag = SeinARTSTags::Resource;
			Resource.DefaultStartingValue = FFixedPoint::FromInt(100);
			Settings->ResourceCatalog = {Resource};
		}
		~FDefinition()
		{
			Bridge->ComponentData = MoveTemp(Previous);
			GetMutableDefault<USeinARTSCoreSettings>()->ResourceCatalog = MoveTemp(Catalog);
		}
	};
	struct FFixture
	{
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World = Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		FSeinEntityHandle A, B;
		bool Initialize(TSubclassOf<USeinAbility> AbilityClass = USeinPlacementGateTestAbility::StaticClass())
		{
			return SeinTestMatchBootstrap::Materialize(*World, [&]()
			{
				World->RegisterPlayer(FSeinPlayerID(1), FSeinFactionID(1));
				A = World->SpawnAbstractEntity(FFixedTransform(Point(-1000)), FSeinPlayerID(1));
				B = World->SpawnAbstractEntity(FFixedTransform(Point(-1100)), FSeinPlayerID(1));
				for (auto Entity : {A, B})
				{
					World->AddComponent(Entity, FSeinAbilityPayload());
					USeinAbilityBPFL::SeinGrantAbility(World, Entity, AbilityClass);
				}
			}) && SeinTestMatchBootstrap::Start(*World);
		}
		~FFixture() { World->StopSimulation(); }
		USeinAbility* Ability() const
		{
			return World->GetComponent<FSeinAbilityPayload>(A)->FindAbilityByTag(*World, SeinARTSTags::Command_Context_AbilityTriggered);
		}
		USeinPointFacingTargeterSpec* Spec() const { return Cast<USeinPointFacingTargeterSpec>(Ability()->TargeterSpec); }
		FSeinEntityHandle Blocker(FFixedVector Location)
		{
			auto Scope = FSeinSimContextTestAccess::Enter(*World);
			const auto Entity = World->SpawnAbstractEntity(FFixedTransform(Location), FSeinPlayerID(1));
			FSeinExtentsPayload Extents;
			Extents.bBlocksNav = true;
			Extents.Shapes.Add(Box(10, 10));
			World->AddComponent(Entity, Extents);
			return Entity;
		}
		void Queue(FSeinEntityHandle Entity, FFixedVector Location)
		{
			auto Cmd = FSeinCommand::MakeAbilityCommand(FSeinPlayerID(1), Entity, SeinARTSTags::Command_Context_AbilityTriggered);
			Cmd.TargetLocation = Location;
			Cmd.TargeterPoints.Add(FSeinTargeterPoint(Location));
			World->SubmitLocalCommandDraft(Cmd);
		}
		void Tick() { FTSTicker::GetCoreTicker().Tick(World->GetFixedDeltaTimeSeconds()); }
	};
}

	TEST(PlacementUsesLiveRotatedBlockersAndRejectsMissingFootprints, "SeinARTS.Sim.Placement")
	{
		using namespace PlacementGateLocal;
		FDefinition Definition; FFixture F;
		ASSERT_THAT(IsTrue(F.Initialize()));
		// No nav update occurs between spawn and query.
		const auto Blocker = F.Blocker(Point(80));
		ASSERT_THAT(IsFalse(SeinPlacementValidation::IsValid(*F.World, F.Spec(), Point(0), FFixedPoint::Zero)));
		ASSERT_THAT(IsTrue(SeinPlacementValidation::IsValid(*F.World, F.Spec(), Point(0), FFixedPoint::FromInt(90))));
		{
			auto Scope = FSeinSimContextTestAccess::Enter(*F.World);
			F.World->GetComponentMutable<FSeinExtentsPayload>(Blocker)->bBlocksNav = false;
		}
		ASSERT_THAT(IsTrue(SeinPlacementValidation::IsValid(*F.World, F.Spec(), Point(0), FFixedPoint::Zero)));
		F.World->FootprintPlacementResolver.BindLambda([](const FFixedVector&, const FFixedPoint&, const FSeinExtentsShape&, uint8) { return false; });
		ASSERT_THAT(IsFalse(SeinPlacementValidation::IsValid(*F.World, F.Spec(), Point(0), FFixedPoint::Zero)));
		F.World->FootprintPlacementResolver.Unbind();
		ASSERT_THAT(IsFalse(SeinPlacementValidation::IsValidForAbility(*F.World, *F.Ability(), {})));
		F.Spec()->BuildingClass.Reset();
		ASSERT_THAT(IsFalse(SeinPlacementValidation::IsValid(*F.World, F.Spec(), Point(0), FFixedPoint::Zero)));
	}

	TEST(SameTickPlacementsReserveLiveSpaceWithoutDoubleCharging, "SeinARTS.Sim.Placement")
	{
		using namespace PlacementGateLocal;
		FDefinition Definition; FFixture F;
		ASSERT_THAT(IsTrue(F.Initialize()));
		F.Queue(F.A, Point(0)); F.Queue(F.B, Point(0)); F.Tick();
		ASSERT_THAT(AreEqual(FFixedPoint::FromInt(90).Value, F.World->GetPlayerState(FSeinPlayerID(1))->GetResource(SeinARTSTags::Resource).Value));
		ASSERT_THAT(IsFalse(SeinPlacementValidation::IsValid(*F.World, F.Spec(), Point(0), FFixedPoint::Zero)));
	}

	TEST(PlacementPreviewAndPressReleaseFollowLiveValidity, "SeinARTS.Sim.Placement")
	{
		using namespace PlacementGateLocal;
		FDefinition Definition; FFixture F;
		ASSERT_THAT(IsTrue(F.Initialize()));
		auto* Player = NewObject<ULocalPlayer>(GEngine);
		auto* Targeter = NewObject<USeinPlacementGateTestTargeter>(Player);
		Targeter->TestWorld = &F.Spawner.GetWorld();
		// Multi-capture keeps the test away from submission to a real player controller.
		F.Spec()->TargetCount = 2;
		F.Spec()->PreviewClass = FSoftClassPath(ASeinPlacementGateTestPreview::StaticClass());
		Targeter->Activate(F.Spec(), F.Ability()->AbilityTag, F.A, 0);
		F.Blocker(Point(80));
		Targeter->UpdateCursor(FVector::ZeroVector);
		ASeinPlacementGateTestPreview* Preview = nullptr;
		for (TActorIterator<ASeinPlacementGateTestPreview> It(&F.Spawner.GetWorld()); It; ++It) Preview = *It;
		ASSERT_THAT(IsNotNull(Preview));
		ASSERT_THAT(IsTrue(Preview->GetValidity() == ESeinTargeterValidity::Blocked));
		Targeter->OnConfirmPressed();
		ASSERT_THAT(IsTrue(Targeter->GetState() == ESeinTargeterState::WaitingForCapture));
		Targeter->UpdateCursor(FVector(500, 0, 0));
		ASSERT_THAT(IsTrue(Preview->GetValidity() == ESeinTargeterValidity::Valid));
		Targeter->OnConfirmPressed();
		ASSERT_THAT(IsTrue(Targeter->GetState() == ESeinTargeterState::Dragging));
		F.Blocker(Point(500, 80));
		Targeter->UpdateCursor(FVector(500, 200, 0));
		ASSERT_THAT(IsTrue(Preview->GetValidity() == ESeinTargeterValidity::Blocked));
		Targeter->OnConfirmReleased();
		ASSERT_THAT(AreEqual(0, Targeter->GetCapturedCount()));
		ASSERT_THAT(IsTrue(Targeter->GetState() == ESeinTargeterState::WaitingForCapture));
		Targeter->Cancel();
	}

	TEST(PlacementPreviewPoseMatchesOriginAnchorAndResetYaw, "SeinARTS.Sim.Placement")
	{
		using namespace PlacementGateLocal;
		FDefinition Definition; FFixture F;
		ASSERT_THAT(IsTrue(F.Initialize()));
		auto* Player = NewObject<ULocalPlayer>(GEngine);
		auto* Targeter = NewObject<USeinPlacementGateTestTargeter>(Player);
		Targeter->TestWorld = &F.Spawner.GetWorld();
		F.Spec()->TargetCount = 2;
		F.Spec()->PreviewClass = FSoftClassPath(ASeinPlacementGateTestPreview::StaticClass());
		Targeter->Activate(F.Spec(), F.Ability()->AbilityTag, F.A, 0);
		Targeter->UpdateCursor(FVector::ZeroVector);
		Targeter->OnConfirmPressed();
		F.Blocker(Point(0, 80));
		Targeter->UpdateCursor(FVector(0, 200, 0));
		ASeinPlacementGateTestPreview* Preview = nullptr;
		for (TActorIterator<ASeinPlacementGateTestPreview> It(&F.Spawner.GetWorld()); It; ++It) Preview = *It;
		ASSERT_THAT(IsNotNull(Preview));
		ASSERT_THAT(IsTrue(Preview->GetActorLocation().IsNearlyZero()));
		ASSERT_THAT(IsNear(90.0f, float(Preview->GetActorRotation().Yaw), 0.01f));
		Targeter->OnConfirmReleased();
		Targeter->UpdateCursor(FVector(500, 0, 0));
		ASSERT_THAT(IsNear(0.0f, float(Preview->GetActorRotation().Yaw), 0.01f));
		ASSERT_THAT(IsTrue(Preview->GetActorLocation().Equals(FVector(500, 0, 0))));
		Targeter->Cancel();
	}

	TEST(CompoundPlacementChecksSecondaryShapesAndOffsets, "SeinARTS.Sim.Placement")
	{
		using namespace PlacementGateLocal;
		FDefinition Definition;
		FSeinExtentsPayload Extents;
		Extents.bBlocksNav = true;
		Extents.Shapes = {Box(10, 10), Box(10, 10)};
		Extents.Shapes[1].LocalOffset = Point(200);
		Definition.Bridge->ComponentData = {FInstancedStruct::Make(Extents)};
		FFixture F; ASSERT_THAT(IsTrue(F.Initialize()));
		F.Blocker(Point(0, 200));
		ASSERT_THAT(IsTrue(SeinPlacementValidation::IsValid(*F.World, F.Spec(), Point(0), FFixedPoint::Zero)));
		ASSERT_THAT(IsFalse(SeinPlacementValidation::IsValid(*F.World, F.Spec(), Point(0), FFixedPoint::FromInt(90))));
	}


 TEST(PlacementDefinitionWorksWithPointCaptureAndOverridesLegacyVisualSource, "SeinARTS.Sim.Placement")
 {
  using namespace PlacementGateLocal;
  FDefinition Definition; FFixture F; ASSERT_THAT(IsTrue(F.Initialize()));
  F.Ability()->Placement.ActorClass = ASeinPlacementYawTestBuilding::StaticClass();
  F.Ability()->TargeterSpec = NewObject<USeinPointTargeterSpec>(F.Ability());
  TArray<FSeinTargeterPoint> Points = {FSeinTargeterPoint(Point(0))};
  ASSERT_THAT(IsTrue(SeinPlacementValidation::IsValidForAbility(*F.World, *F.Ability(), Points)));
  F.Blocker(Point(80));
  ASSERT_THAT(IsFalse(SeinPlacementValidation::IsValidForAbility(*F.World, *F.Ability(), Points)));
  Points[0].Location = Point(500);
  ASSERT_THAT(IsTrue(SeinPlacementValidation::IsValidForAbility(*F.World, *F.Ability(), Points)));
 }
 TEST(GenericPreviewReceivesCapturedPoseAndSessionEndReason, "SeinARTS.Sim.TargeterPreview")
 {
  using namespace PlacementGateLocal;
  FDefinition Definition; FFixture F; ASSERT_THAT(IsTrue(F.Initialize()));
  auto* Player = NewObject<ULocalPlayer>(GEngine);
  auto* Targeter = NewObject<USeinPlacementGateTestTargeter>(Player);
  Targeter->TestWorld = &F.Spawner.GetWorld();
  F.Spec()->TargetCount = 2;
  F.Spec()->PreviewClass = FSoftClassPath(ASeinPreviewContractTestActor::StaticClass());
  Targeter->Activate(F.Spec(), F.Ability()->AbilityTag, F.A, 250);
  ASeinPreviewContractTestActor* Preview = nullptr;
  for (TActorIterator<ASeinPreviewContractTestActor> It(&F.Spawner.GetWorld()); It; ++It) Preview = *It;
  ASSERT_THAT(IsNotNull(Preview));
  ASSERT_THAT(IsTrue(Preview->InitialContext.SourceEntity == F.A));
  ASSERT_THAT(IsNear(250.0f, Preview->InitialContext.AreaRadius, .001f));
  Targeter->UpdateCursor(FVector::ZeroVector);
  Targeter->OnConfirmPressed();
  Targeter->UpdateCursor(FVector(0, 200, 0));
  ASSERT_THAT(IsTrue(Preview->Context.bHasAnchor));
  const FTransform CapturedPose = Preview->Context.ResolvedTarget;
  Targeter->OnConfirmReleased();
  ASSERT_THAT(AreEqual(1, Preview->Context.CapturedPoints.Num()));
  ASSERT_THAT(IsTrue(Preview->Context.CapturedPoints[0].Location.ToVector().Equals(CapturedPose.GetLocation())));
  ASSERT_THAT(IsNear(90.0f, Preview->Context.CapturedPoints[0].YawDegrees.ToFloat(), .001f));
  Targeter->Activate(F.Spec(), F.Ability()->AbilityTag, F.A, 250);
  ASSERT_THAT(AreEqual(1, Preview->Ended));
  ASSERT_THAT(IsTrue(Preview->EndReason == ESeinPreviewEndReason::Replaced));
  Targeter->Cancel();
 }

	TEST(PlacementCommandRootsMatchSerialAndParallel, "SeinARTS.Determinism.Placement")
	{
		using namespace PlacementGateLocal;
		FDefinition Definition;
		auto* Parallel = IConsoleManager::Get().FindConsoleVariable(TEXT("Sein.Sim.Parallel"));
		ASSERT_THAT(IsNotNull(Parallel));
		struct FRestore { IConsoleVariable* Var; int32 Value; ~FRestore() { Var->Set(Value, ECVF_SetByCode); } } Restore{Parallel, Parallel->GetInt()};
		TArray<FGuid> Reference;
		for (int32 Mode = 0; Mode < 2; ++Mode)
		{
			Parallel->Set(Mode, ECVF_SetByCode);
			FFixture F; ASSERT_THAT(IsTrue(F.Initialize()));
			F.Queue(F.A, Point(0)); F.Queue(F.B, Point(0));
			for (int32 Tick = 0; Tick < 3; ++Tick)
			{
				F.Tick(); FGuid Root; FString Error;
				ASSERT_THAT(IsTrue(F.World->ComputeCanonicalStateRoot(Root, Error)));
				if (Mode == 0) Reference.Add(Root);
				else ASSERT_THAT(IsTrue(Reference[Tick] == Root));
			}
		}
	}

	TEST(RestoredPlacementRejectsOccupiedSitesAndContinuesEqually, "SeinARTS.Determinism.Placement")
	{
		using namespace PlacementGateLocal;
		FDefinition Definition; FFixture F;
		ASSERT_THAT(IsTrue(F.Initialize(USeinPlacementPointTestAbility::StaticClass())));
		F.Queue(F.A, Point(0)); F.Tick();
		FSeinWorldSnapshot Snapshot; F.World->CaptureSnapshot(Snapshot);
		ASSERT_THAT(AreEqual(FSeinWorldSnapshot::CurrentVersion, Snapshot.SnapshotVersion));
		FFixture Restored;
		ASSERT_THAT(IsTrue(SeinTestSnapshotRestore::RestoreTrusted(*Restored.World, Snapshot)));
		Restored.A = F.A; Restored.B = F.B;
		for (auto* Fixture : {&F, &Restored})
		{
			ASSERT_THAT(IsTrue(SeinPlacementValidation::IsValidForAbility(*Fixture->World, *Fixture->Ability(), {FSeinTargeterPoint(Point(500))})));
			ASSERT_THAT(IsFalse(SeinPlacementValidation::IsValidForAbility(*Fixture->World, *Fixture->Ability(), {FSeinTargeterPoint(Point(0))})));
			Fixture->Queue(Fixture->B, Point(0));
			Fixture->Queue(Fixture->B, Point(500));
		}
		for (int32 Tick = 0; Tick < 3; ++Tick)
		{
			F.Tick(); FGuid A, B; FString Error;
			ASSERT_THAT(IsTrue(F.World->ComputeCanonicalStateRoot(A, Error)));
			ASSERT_THAT(IsTrue(Restored.World->ComputeCanonicalStateRoot(B, Error)));
			ASSERT_THAT(IsTrue(A == B));
		}
		ASSERT_THAT(AreEqual(FFixedPoint::FromInt(80).Value, Restored.World->GetPlayerState(FSeinPlayerID(1))->GetResource(SeinARTSTags::Resource).Value));
	}
}
