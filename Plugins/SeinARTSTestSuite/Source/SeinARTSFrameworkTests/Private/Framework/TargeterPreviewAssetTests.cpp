#include "CQTest.h"
#include "Components/ActorTestSpawner.h"
#include "Targeter/SeinPointFacingTargeterPreview.h"
#include "Targeter/SeinPointTargeterPreview.h"
#include "Targeter/SeinTargeterVisualComponent.h"
#include "Components/MeshComponent.h"
#include "Components/DecalComponent.h"
#include "Materials/MaterialInterface.h"
#include "Engine/World.h"

namespace UE::SeinARTSTests
{
 TEST(SavedDemoPreviewsUseConfiguredMaterialsAfterSpawn, "SeinARTS.Integration.TargeterPreview")
 {
  FActorTestSpawner Spawner;
  auto* BuildingClass = LoadClass<ASeinPointFacingTargeterPreview>(nullptr,
   TEXT("/SeinARTSFramework/Demo/Blueprints/STP_BuildingHologram.STP_BuildingHologram_C"));
  auto* SmokeClass = LoadClass<ASeinPointTargeterPreview>(nullptr,
   TEXT("/SeinARTSFramework/Demo/Blueprints/STP_SmokeTargeter.STP_SmokeTargeter_C"));
  ASSERT_THAT(IsNotNull(BuildingClass)); ASSERT_THAT(IsNotNull(SmokeClass));
  auto* Building = Spawner.GetWorld().SpawnActor<ASeinPointFacingTargeterPreview>(BuildingClass);
  auto* Smoke = Spawner.GetWorld().SpawnActor<ASeinPointTargeterPreview>(SmokeClass);
  ASSERT_THAT(IsNotNull(Building)); ASSERT_THAT(IsNotNull(Smoke));
  auto* Spec = NewObject<USeinPointFacingTargeterSpec>();
  FSeinTargeterPreviewContext Context;
  Context.ActorClass = TSoftClassPtr<AActor>(FSoftObjectPath(TEXT("/SeinARTSFramework/Demo/Blueprints/SU_Barracks.SU_Barracks_C")));
  Context.AreaRadius = 300;
  Building->InitializePreview(Spec, 300);
  Building->BeginPreview(Context);
  Smoke->InitializePreview(NewObject<USeinPointTargeterSpec>(), 300);
  Smoke->BeginPreview(Context);
  ASSERT_THAT(IsTrue(Building->MeshPreview->GeneratedMeshes.Num() > 0));
  ASSERT_THAT(IsNotNull(Building->MeshPreview->Materials.Blocked));
  ASSERT_THAT(IsNotNull(Smoke->DecalPreview->Materials.Blocked));
  Context.Validity = ESeinTargeterValidity::Blocked;
  Building->Present(Context); Smoke->Present(Context);
  for (UMeshComponent* Mesh : Building->MeshPreview->GeneratedMeshes)
   for (int32 Slot = 0; Slot < Mesh->GetNumMaterials(); ++Slot)
    ASSERT_THAT(IsTrue(Mesh->GetMaterial(Slot) == Building->MeshPreview->Materials.Blocked));
  ASSERT_THAT(IsTrue(Smoke->DecalPreview->Decal->GetDecalMaterial() == Smoke->DecalPreview->Materials.Blocked));
  ASSERT_THAT(IsNear(300.0f, float(Smoke->DecalPreview->Decal->DecalSize.Y), .01f));
  Context.Validity = ESeinTargeterValidity::Valid;
  Building->Present(Context); Smoke->Present(Context);
  for (UMeshComponent* Mesh : Building->MeshPreview->GeneratedMeshes)
   ASSERT_THAT(IsTrue(Mesh->GetMaterial(0) == Building->MeshPreview->Materials.Valid));
  ASSERT_THAT(IsTrue(Smoke->DecalPreview->Decal->GetDecalMaterial() == Smoke->DecalPreview->Materials.Valid));
  Building->Destroy(); Smoke->Destroy();
 }
}
