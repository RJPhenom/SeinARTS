// SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
#include "Misc/AutomationTest.h"
#include "Engine/World.h"
#include "Components/ActorTestSpawner.h"
#include "Player/SeinCursorTrace.h"
#include "Actor/SeinActor.h"
#include "Components/BoxComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SeinExtentsPayload.h"
#include "Simulation/SeinActorBridgeSubsystem.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Simulation/SeinTestMatchBootstrap.h"

namespace
{
	FSeinExtentsPayload Capsule()
	{
		FSeinExtentsPayload Extents;
		Extents.Shapes.AddDefaulted(); // radius 40, height 180; sim collision disabled
		return Extents;
	}
	bool Hit(const FSeinExtentsPayload& Extents, const FVector& Origin,
		const FVector& Direction, double& Distance, const FTransform& Transform = FTransform::Identity,
		double Limit = 10000.0)
	{
		return SeinCursorTrace::IntersectExtents(Extents, Transform, Origin, Direction, Limit, Distance);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSeinCursorCapsuleTest,
	"SeinARTS.Unit.Selection.Cursor.Capsule", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSeinCursorCapsuleTest::RunTest(const FString&)
{
	const FSeinExtentsPayload Extents = Capsule();
	double Distance;
	TestTrue(TEXT("top cap hit"), Hit(Extents, FVector(0, 0, 300), -FVector::UpVector, Distance));
	TestTrue(TEXT("top cap depth"), FMath::IsNearlyEqual(Distance, 120.0));
	TestTrue(TEXT("side hit"), Hit(Extents, FVector(100, 0, 90), -FVector::ForwardVector, Distance));
	TestTrue(TEXT("side depth"), FMath::IsNearlyEqual(Distance, 60.0));
	TestFalse(TEXT("capsule corner outside curved cap"), Hit(Extents, FVector(39, -100, 179), FVector::RightVector, Distance));
	TestTrue(TEXT("inside capsule starts at zero"), Hit(Extents, FVector(0, 0, 90), FVector::ForwardVector, Distance));
	TestEqual(TEXT("inside depth"), Distance, 0.0);
	TestFalse(TEXT("behind ray"), Hit(Extents, FVector(100, 0, 90), FVector::ForwardVector, Distance));
	TestFalse(TEXT("finite trace limit"), Hit(Extents, FVector(100, 0, 90), -FVector::ForwardVector, Distance, FTransform::Identity, 59.0));
	TestTrue(TEXT("tangent"), Hit(Extents, FVector(40, -100, 90), FVector::RightVector, Distance));
	TestTrue(TEXT("tangent depth"), FMath::IsNearlyEqual(Distance, 100.0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSeinCursorEnvironmentTest,
	"SeinARTS.Unit.Selection.Cursor.EnvironmentIgnoresEntityMeshes", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSeinCursorEnvironmentTest::RunTest(const FString&)
{
	FActorTestSpawner Spawner;
	UWorld& World = Spawner.GetWorld();
	AActor& Ground = Spawner.SpawnActor<AActor>();
	ASeinActor& UnregisteredEntity = Spawner.SpawnActor<ASeinActor>();
	const auto AddBox = [](AActor& Actor, FVector Location, FVector Half)
	{
		UBoxComponent* Box = NewObject<UBoxComponent>(&Actor);
		Actor.SetRootComponent(Box);
		Box->SetBoxExtent(Half);
		Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Box->SetCollisionResponseToAllChannels(ECR_Block);
		Box->RegisterComponent();
		Actor.SetActorLocation(Location);
		return Box;
	};
	AddBox(Ground, FVector(0, 0, -20), FVector(200, 200, 20));
	UBoxComponent* EntityMeshCollision = AddBox(UnregisteredEntity, FVector(0, 0, 500), FVector(100));
	const FVector Origin(0, 0, 1000);
	FHitResult Result;
	TestTrue(TEXT("fixture really has blocking entity collision"),
		World.LineTraceSingleByChannel(Result, Origin, FVector(0, 0, -100), ECC_Visibility));
	TestEqual(TEXT("raw UE trace hits entity"), Result.GetActor(), static_cast<AActor*>(&UnregisteredEntity));
	for (ECollisionChannel Channel : {ECC_Visibility, ECC_Camera})
	{
		TestTrue(TEXT("world fallback finds ground through entity"),
			SeinCursorTrace::TraceEnvironment(World, Origin, -FVector::UpVector, 2000, Channel, Result));
		TestEqual(TEXT("entity collision cannot become ground or a pick"), Result.GetActor(), &Ground);
		TestTrue(TEXT("actual ground surface"), FMath::IsNearlyZero(Result.ImpactPoint.Z));
	}
	EntityMeshCollision->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	TestTrue(TEXT("disabling mesh collision preserves fallback"),
		SeinCursorTrace::TraceEnvironment(World, Origin, -FVector::UpVector, 2000, ECC_Visibility, Result));
	TestEqual(TEXT("same ground with NoCollision"), Result.GetActor(), &Ground);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSeinCursorBoxTest,
	"SeinARTS.Unit.Selection.Cursor.RotatedCompoundBox", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSeinCursorBoxTest::RunTest(const FString&)
{
	FSeinExtentsPayload Extents;
	FSeinExtentsShape Shape;
	Shape.Shape = ESeinExtentsShape::Box;
	Shape.HalfExtentX = FFixedPoint::FromInt(100);
	Shape.HalfExtentY = FFixedPoint::FromInt(20);
	Shape.Height = FFixedPoint::FromInt(50);
	Shape.LocalOffset = FFixedVector(FFixedPoint::FromInt(200), FFixedPoint::Zero, FFixedPoint::FromInt(30));
	Shape.YawOffsetDegrees = FFixedPoint::FromInt(90);
	Extents.Shapes.Add(Shape);
	const FTransform Pose(FRotator(15, 40, 20), FVector(500, 900, 200), FVector(7, 3, 2));
	const FQuat Rotation = Pose.GetRotation() * FQuat(FVector::UpVector, PI * 0.5);
	const FVector Center = Pose.GetLocation() + Pose.GetRotation().RotateVector(FVector(200, 0, 30))
		+ Rotation.GetUpVector() * 25;
	const FVector Direction = -Rotation.GetForwardVector();
	double Distance;
	TestTrue(TEXT("offset + actor rotation + local yaw compose without scale"),
		Hit(Extents, Center - Direction * 300, Direction, Distance, Pose));
	TestTrue(TEXT("box entry is 200 units away"), FMath::IsNearlyEqual(Distance, 200.0, 0.001));
	TestFalse(TEXT("outside thin box side"), Hit(Extents, Center + Rotation.GetRightVector() * 21 - Direction * 300,
		Direction, Distance, Pose));
	Shape.LocalOffset.X = FFixedPoint::FromInt(-200);
	Extents.Shapes.Add(Shape);
	TestFalse(TEXT("compound gap is not a merged bounds hit"), Hit(Extents, FVector(0, 0, 200), -FVector::UpVector, Distance));
	TestTrue(TEXT("second shape participates"), Hit(Extents, FVector(-200, 0, 200), -FVector::UpVector, Distance));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSeinCursorDegenerateTest,
	"SeinARTS.Unit.Selection.Cursor.EmptyAndShortShapes", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSeinCursorDegenerateTest::RunTest(const FString&)
{
	double Distance;
	FSeinExtentsPayload Extents;
	TestFalse(TEXT("no extents means no mesh fallback"), Hit(Extents, FVector::ZeroVector, FVector::UpVector, Distance));
	Extents = Capsule();
	Extents.Shapes[0].Height = FFixedPoint::FromInt(20);
	TestTrue(TEXT("short capsule matches visualizer sphere clamp"), Hit(Extents, FVector(0, 0, 100), -FVector::UpVector, Distance));
	TestTrue(TEXT("sphere center uses authored half height"), FMath::IsNearlyEqual(Distance, 50.0));
	TestFalse(TEXT("zero direction rejected"), Hit(Extents, FVector::ZeroVector, FVector::ZeroVector, Distance));
	TestFalse(TEXT("negative distance rejected"), Hit(Extents, FVector::ZeroVector, FVector::UpVector, Distance, FTransform::Identity, -1.0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSeinCursorLiveActorsTest,
	"SeinARTS.Unit.Selection.Cursor.LiveActorsAfterMovement", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSeinCursorLiveActorsTest::RunTest(const FString&)
{
	FActorTestSpawner Spawner;
	UWorld& World = Spawner.GetWorld();
	USeinWorldSubsystem* Sim = World.GetSubsystem<USeinWorldSubsystem>();
	USeinActorBridgeSubsystem* Bridge = World.GetSubsystem<USeinActorBridgeSubsystem>();
	if (!TestNotNull(TEXT("simulation"), Sim) || !TestNotNull(TEXT("bridge"), Bridge)) return false;
	FSeinEntityHandle Handles[2];
	if (!TestTrue(TEXT("bootstrap"), SeinTestMatchBootstrap::Materialize(*Sim, [&]()
	{
		for (FSeinEntityHandle& Handle : Handles)
		{
			Handle = Sim->SpawnAbstractEntity(FFixedTransform(), FSeinPlayerID::Neutral());
			Sim->AddComponent(Handle, Capsule());
		}
	}))) return false;

	ASeinActor* Actors[2];
	for (int32 I = 0; I < 2; ++I)
	{
		Actors[I] = &Spawner.SpawnActor<ASeinActor>();
		USkeletalMeshComponent* Mesh = NewObject<USkeletalMeshComponent>(Actors[I]);
		Actors[I]->SetRootComponent(Mesh);
		Mesh->RegisterComponent();
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Actors[I]->InitializeWithEntity(Handles[I]);
		Bridge->RegisterActor(Handles[I], Actors[I]);
		Actors[I]->SetActorLocation(FVector(0, I * 300, 0));
	}
	FHitResult Result;
	const auto Pick = [&](FVector Origin)
	{
		return SeinCursorTrace::TraceEntities(World, Origin, -FVector::UpVector, 2000, Result);
	};
	TestTrue(TEXT("skeletal actor with NoCollision is picked"), Pick(FVector(0, 0, 1000)));
	TestEqual(TEXT("first actor"), Result.GetActor(), static_cast<AActor*>(Actors[0]));
	Actors[0]->SetActorLocation(FVector(500, 0, 0));
	TestFalse(TEXT("old location no longer picks after individual movement"), Pick(FVector(0, 0, 1000)));
	TestTrue(TEXT("new displayed location picks independently of sim/physics pose"), Pick(FVector(500, 0, 1000)));

	// Both displayed actors move together; no physics-body or sim transform update
	// is performed. The query must use their current presentation transforms.
	for (int32 I = 0; I < 2; ++I)
	{
		Actors[I]->SetActorLocation(FVector(1000, I * 300, 0));
		Actors[I]->SetActorRotation(FRotator(0, 90, 0));
		Actors[I]->SetActorEnableCollision(false);
		TestTrue(TEXT("each actor remains pickable after moving the group"), Pick(FVector(1000, I * 300, 1000)));
		TestEqual(TEXT("correct group member"), Result.GetActor(), static_cast<AActor*>(Actors[I]));
	}
	Actors[1]->SetActorLocation(FVector(1000, 0, 200));
	TestTrue(TEXT("overlapping extents pick nearest"), Pick(FVector(1000, 0, 1000)));
	TestEqual(TEXT("higher actor first"), Result.GetActor(), static_cast<AActor*>(Actors[1]));
	Actors[1]->SetActorLocation(FVector(1000, 0, 0));
	TestTrue(TEXT("equal depth"), Pick(FVector(1000, 0, 1000)));
	TestEqual(TEXT("tie uses full handle"), Result.GetActor(), static_cast<AActor*>(Actors[0]));
	Actors[0]->SetActorHiddenInGame(true);
	TestTrue(TEXT("hidden actor skipped"), Pick(FVector(1000, 0, 1000)));
	TestEqual(TEXT("visible actor wins"), Result.GetActor(), static_cast<AActor*>(Actors[1]));
	Bridge->UnregisterActor(Handles[1]);
	TestFalse(TEXT("unregistered and hidden actors not picked"), Pick(FVector(1000, 0, 1000)));
	Actors[0]->SetActorHiddenInGame(false);
	Actors[0]->InitializeWithEntity(FSeinEntityHandle(Handles[0].Index, Handles[0].Generation + 1));
	TestFalse(TEXT("stale bridge generation not picked"), Pick(FVector(1000, 0, 1000)));
	return true;
}
