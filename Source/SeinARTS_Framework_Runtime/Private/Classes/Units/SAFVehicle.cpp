#include "Classes/Units/SAFVehicle.h"
#include "Assets/Units/SAFVehicleAsset.h"
#include "Interfaces/Units/SAFVehiclePawnInterface.h"
#include "Net/UnrealNetwork.h"
#include "Resolvers/SAFAssetResolver.h"
#include "Utils/SAFLibrary.h"
#include "Debug/SAFDebugTool.h"

ASAFVehicle::ASAFVehicle() {
	// Intentionally empty – inherits behavior from ASAFUnit
}

// Unit Interface / API
// =================================================================================================================================
// Overide adds additional initalization steps
void ASAFVehicle::InitFromAsset_Implementation(USAFAsset* InAsset, ASAFPlayerState* InOwner, bool bReinitialize) {
	USAFAsset* InitFromAsset = InAsset ? InAsset : SAFAssetResolver::ResolveAsset(Asset);
	USAFVehicleAsset* VehicleAsset = Cast<USAFVehicleAsset>(InitFromAsset);
	if (!VehicleAsset) { SAFDEBUG_WARNING(FORMATSTR("InitUnit: invalid Data Asset Type on actor '%s'. Culling.", *GetName())); Destroy(); return; }

	Super::InitFromAsset_Implementation(VehicleAsset, InOwner, bReinitialize);
	InitVehicle(VehicleAsset);
}

// Vehicle API
// =================================================================================================================================
// Initializes the vehicle (generates members and positions)
void ASAFVehicle::InitVehicle_Implementation(USAFVehicleAsset* VehicleAsset) {
	if (!HasAuthority()) return;
	if (bInitialized) { SAFDEBUG_WARNING(FORMATSTR("InitVehicle called twice on Vehicle '%s'. Discarding.", *GetName())); return; }
	if (!VehiclePawnClass 
		|| !VehiclePawnClass->ImplementsInterface(USAFActorInterface::StaticClass())
		|| !VehiclePawnClass->ImplementsInterface(USAFVehiclePawnInterface::StaticClass())
	) { SAFDEBUG_ERROR("InitVehicle aborted: invalid VehiclePawnClass."); return;	}
	if (!VehicleAsset) { SAFDEBUG_ERROR("InitVehicle aborted: null VehicleAsset."); return; }
	UWorld* World = GetWorld();
	if (!World) { SAFDEBUG_ERROR("InitVehicle aborted: World is nullptr."); return; }

	FActorSpawnParameters Params;
	Params.Owner = this;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	VehiclePawn = World->SpawnActor<APawn>(VehiclePawnClass, GetActorLocation(), GetActorRotation(), Params);
	if (!VehiclePawn) { SAFDEBUG_WARNING("InitVehicle failed to spawn vehicle pawn. Culling."); Destroy(); return; }
	if (!SAFLibrary::IsPawnPtrValidSeinARTSVehiclePawn(VehiclePawn)) { 
		SAFDEBUG_WARNING(FORMATSTR("InitVehicle spawned an invalid vehicle pawn, culling.")); 
		VehiclePawn->Destroy(); 
		Destroy(); 
		return; 
	}

	ISAFVehiclePawnInterface::Execute_InitVehiclePawn(VehiclePawn.Get(), VehicleAsset, this);
	ISAFUnitInterface::Execute_AttachToPawn(this, VehiclePawn.Get());
	SAFDEBUG_SUCCESS(FORMATSTR("VehiclePawn '%s' initialized for Vehicle '%s'.", *VehiclePawn->GetName(), *GetName()));
	bInitialized = true;
}

// Replication
// =================================================================================================================================
void ASAFVehicle::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const {
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ASAFVehicle, VehiclePawn);
}


void ASAFVehicle::OnRep_VehiclePawn() {
	SAFDEBUG_INFO(TEXT("OnRep_VehiclePawn triggered."));
}
