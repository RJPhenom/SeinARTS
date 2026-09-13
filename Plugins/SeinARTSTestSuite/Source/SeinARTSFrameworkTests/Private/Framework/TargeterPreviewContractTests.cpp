#include "CQTest.h"
#include "Components/ActorTestSpawner.h"
#include "TestTypes/SeinPreviewContractTestTypes.h"
#include "Targeter/SeinTargeterVisualComponent.h"
#include "Targeter/SeinLineTargeterPreview.h"
#include "Components/StaticMeshComponent.h"
#include "Components/DecalComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/Material.h"

namespace UE::SeinARTSTests
{
 TEST(GenericContextLifecycleIsIndependentOfVisuals, "SeinARTS.Sim.TargeterPreview")
 {
  FActorTestSpawner Spawner;
  auto& Preview = Spawner.SpawnActor<ASeinPreviewContractTestActor>();
  FSeinTargeterPreviewContext Context;
  Context.AreaRadius = 300;
  Context.bHasAnchor = true;
  Context.AnchorWorld = FVector::ZeroVector;
  Context.CursorWorld = FVector(0, 500, 0);
  Context.ResolvedTarget = FTransform(FRotator(0, 90, 0), FVector::ZeroVector);
  Preview.BeginPreview(Context);
  ASSERT_THAT(AreEqual(1, Preview.Initialized));
  ASSERT_THAT(AreEqual(1, Preview.Changed));
  ASSERT_THAT(IsTrue(Preview.InitialContext.bHasAnchor));
  ASSERT_THAT(IsTrue(Preview.GetActorLocation().IsNearlyZero()));
  ASSERT_THAT(IsNear(90.0f, float(Preview.GetActorRotation().Yaw), .01f));
  ASSERT_THAT(IsNull(Preview.FindComponentByClass<UMeshComponent>()));
  ASSERT_THAT(IsNull(Preview.FindComponentByClass<UDecalComponent>()));
  Preview.Present(Context);
  ASSERT_THAT(AreEqual(1, Preview.Changed));
  Context.Validity = ESeinTargeterValidity::Blocked;
  Preview.Present(Context);
  ASSERT_THAT(AreEqual(2, Preview.Changed));
  Preview.EndPreview(ESeinPreviewEndReason::Submitted);
  const int32 Updates = Preview.Updated;
  Preview.Present(Context);
  Preview.EndPreview(ESeinPreviewEndReason::Cancelled);
  ASSERT_THAT(AreEqual(Updates, Preview.Updated));
  ASSERT_THAT(AreEqual(1, Preview.Ended));
  ASSERT_THAT(IsTrue(Preview.EndReason == ESeinPreviewEndReason::Submitted));
 }
 TEST(InitializationCancellationAndCustomTransformAreRespected, "SeinARTS.Sim.TargeterPreview")
 {
  FActorTestSpawner Spawner;
  auto& Cancelled = Spawner.SpawnActor<ASeinPreviewContractTestActor>();
  Cancelled.bEndDuringInitialize = true;
  Cancelled.BeginPreview(FSeinTargeterPreviewContext());
  ASSERT_THAT(AreEqual(1, Cancelled.Ended));
  ASSERT_THAT(AreEqual(0, Cancelled.Updated));
  auto& Custom = Spawner.SpawnActor<ASeinPreviewContractTestActor>();
  Custom.bFollowResolvedTarget = false;
  Custom.SetActorLocation(FVector(900, 0, 0));
  Custom.BeginPreview(FSeinTargeterPreviewContext());
  ASSERT_THAT(IsTrue(Custom.GetActorLocation().Equals(FVector(900, 0, 0))));
 }
 TEST(MeshHelperSwapsEverySlotAndLeavesAuthorMaterialsUntouched, "SeinARTS.Sim.TargeterPreview")
 {
  FActorTestSpawner Spawner;
  auto& Preview = Spawner.SpawnActor<ASeinPreviewContractTestActor>();
  auto* Helper = NewObject<USeinTargeterMeshComponent>(&Preview);
  Helper->RegisterComponent();
  auto* MeshAsset = NewObject<UStaticMesh>(&Preview);
  MeshAsset->GetStaticMaterials().Add(FStaticMaterial());
  MeshAsset->GetStaticMaterials().Add(FStaticMaterial());
  Helper->MeshOverride = MeshAsset;
  auto* Valid = NewObject<UMaterial>();
  auto* Blocked = NewObject<UMaterial>();
  Helper->Materials.Valid = Valid;
  Helper->Materials.Blocked = Blocked;
  FSeinTargeterPreviewContext Context;
  Preview.BeginPreview(Context);
  auto* Mesh = Helper->GetRootMesh();
  ASSERT_THAT(IsNotNull(Mesh));
  ASSERT_THAT(IsTrue(Mesh->GetMaterial(0) == Valid));
  ASSERT_THAT(AreEqual(2, Mesh->GetNumMaterials()));
  Context.Validity = ESeinTargeterValidity::Blocked;
  Preview.Present(Context);
  ASSERT_THAT(IsTrue(Mesh->GetMaterial(0) == Blocked));
  ASSERT_THAT(IsTrue(Mesh->GetMaterial(1) == Blocked));
  Context.Validity = ESeinTargeterValidity::Warning;
  Preview.Present(Context);
  ASSERT_THAT(IsTrue(Mesh->GetMaterial(0) == Valid));
  ASSERT_THAT(IsTrue(Mesh->GetMaterial(1) == Valid));
  ASSERT_THAT(IsTrue(Helper->Materials.Valid == Valid));
 }
 TEST(DecalHelperReceivesRadiusAndMaterialBeforeBlueprintInitialization, "SeinARTS.Sim.TargeterPreview")
 {
  FActorTestSpawner Spawner;
  auto& Preview = Spawner.SpawnActor<ASeinPreviewContractTestActor>();
  auto* Helper = NewObject<USeinTargeterDecalComponent>(&Preview);
  Helper->RegisterComponent();
  auto* Valid = NewObject<UMaterial>();
  auto* Blocked = NewObject<UMaterial>();
  Helper->Materials.Valid = Valid; Helper->Materials.Blocked = Blocked;
  FSeinTargeterPreviewContext Context; Context.AreaRadius = 320;
  Preview.BeginPreview(Context);
  ASSERT_THAT(IsNotNull(Helper->Decal));
  ASSERT_THAT(IsNear(320.0f, float(Helper->Decal->DecalSize.Y), .001f));
  ASSERT_THAT(IsTrue(Helper->Decal->GetDecalMaterial() == Valid));
  Context.Validity = ESeinTargeterValidity::Blocked;
  Preview.Present(Context);
  ASSERT_THAT(IsTrue(Helper->Decal->GetDecalMaterial() == Blocked));
  ASSERT_THAT(IsNear(-90.0f, float(Helper->Decal->GetComponentRotation().Pitch), .01f));
 }
 TEST(LinePresetUsesExplicitAnchorAtWorldOrigin, "SeinARTS.Sim.TargeterPreview")
 {
  FActorTestSpawner Spawner;
  auto& Preview = Spawner.SpawnActor<ASeinLineTargeterPreview>();
  Preview.InitializePreview(NewObject<USeinLineTargeterSpec>(), 0);
  FSeinTargeterPreviewContext Context;
  Context.bHasAnchor = true;
  Context.CursorWorld = FVector(400, 0, 0);
  Preview.BeginPreview(Context);
  auto* Decal = Preview.FindComponentByClass<UDecalComponent>();
  ASSERT_THAT(IsNotNull(Decal));
  ASSERT_THAT(IsTrue(Decal->IsVisible()));
  ASSERT_THAT(IsTrue(Decal->GetComponentLocation().Equals(FVector(200, 0, 0))));
  Context.bHasAnchor = false;
  Preview.Present(Context);
  ASSERT_THAT(IsFalse(Decal->IsVisible()));
 }

}
