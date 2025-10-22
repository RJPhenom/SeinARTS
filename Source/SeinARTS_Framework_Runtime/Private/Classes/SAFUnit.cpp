#include "Classes/SAFUnit.h"
#include "Classes/SAFPawn.h"
#include "Components/SAFCoverCollider.h"
#include "Components/CapsuleComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SAFProductionComponent.h"
#include "Components/SAFTechnologyComponent.h"
#include "Assets/SAFUnitAsset.h"
#include "Assets/SAFPawnAsset.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "Abilities/GameplayAbility.h"
#include "Abilities/GameplayAbilityTypes.h"
#include "Abilities/GameplayAbilityTargetTypes.h"
#include "Engine/ActorChannel.h"
#include "Engine/World.h"
#include "Enums/SAFCoverTypes.h"
#include "GameFramework/Character.h"
#include "Gameplay/Abilities/SAFAbility.h"
#include "Gameplay/Attributes/SAFAttributeSet.h"
#include "Gameplay/Attributes/SAFUnitAttributes.h"
#include "Gameplay/Attributes/SAFProductionAttributes.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/GameState.h"
#include "GameplayTagContainer.h"
#include "Interfaces/SAFPawnInterface.h"
#include "Kismet/KismetMathLibrary.h"
#include "NavigationSystem.h"
#include "Net/UnrealNetwork.h"
#include "Resolvers/SAFAssetResolver.h"
#include "Resolvers/SAFSpacingResolver.h"
#include "Utils/SAFCoverUtilities.h"
#include "Utils/SAFLibrary.h"
#include "Utils/SAFMathLibrary.h"
#include "Debug/SAFDebugTool.h"
#include "DrawDebugHelpers.h"

#if WITH_EDITOR
#include "Components/SkeletalMeshComponent.h"
#include "Components/CapsuleComponent.h"
#endif

ASAFUnit::ASAFUnit() {
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	SetReplicateMovement(false);
	AbilitySystem = CreateDefaultSubobject<UAbilitySystemComponent>(TEXT("AbilitySystem"));
	AbilitySystem->SetIsReplicated(true);
	AbilitySystem->SetReplicationMode(EGameplayEffectReplicationMode::Mixed);
	
	// Create technology component for receiving technology modifications
	TechnologyComponent = CreateDefaultSubobject<USAFTechnologyComponent>(TEXT("TechnologyComponent"));
}

void ASAFUnit::PreInitializeComponents() {
	Super::PreInitializeComponents();  // This calls SAFActor::PreInitializeComponents which clears editor previews
}

// Actor Interface Overrides
// ===============================================================================================================================
void ASAFUnit::SetAsset_Implementation(USAFAsset* InAsset) {
	USAFUnitAsset* UnitAsset = Cast<USAFUnitAsset>(InAsset);
	if (!UnitAsset) { SAFDEBUG_WARNING("SetAsset: unit implementation aborted, asset type was not SAFUnitAsset."); return; }
	
	Asset = InAsset;
	ASAFPlayerState* InOwner = ISAFActorInterface::Execute_GetOwningPlayer(this);
	ISAFActorInterface::Execute_InitFromAsset(this, InAsset, InOwner, true);
}

void ASAFUnit::InitFromAsset_Implementation(USAFAsset* InAsset, ASAFPlayerState* InOwner, bool bReinitialize) {
	if (!HasAuthority()) return;

	// Type validation + Super
	USAFAsset* InitAsset = InAsset ? InAsset : SAFAssetResolver::ResolveAsset(Asset);
	USAFUnitAsset* UnitAsset = Cast<USAFUnitAsset>(InitAsset);
	if (!UnitAsset) { SAFDEBUG_WARNING(FORMATSTR("InitFromAsset: invalid Data Asset Type on actor '%s'. Culling.", *GetName())); Destroy(); return; }
	Super::InitFromAsset_Implementation(InAsset, InOwner, bReinitialize);

	// GAS
	InitAbilitySystem();
	GiveTagsFromAsset();
	GiveAttributesFromAsset();
	GiveAbilitiesFromAsset();
	ApplyStartupEffects();

	// Production
	if (GetUnitAsset()->bCanEverProduce && !ProductionComponent.Get()) {
		ProductionComponent = NewObject<USAFProductionComponent>(this, TEXT("ProductionComponent"));
		ProductionComponent->SetIsReplicated(true);
		AddInstanceComponent(ProductionComponent);
		ProductionComponent->RegisterComponent();
		ProductionComponent->InitProductionCatalogue(GetUnitAsset()->ProductionRecipes);
	}

	// Technology - Apply technology bundle after GAS is initialized
	if (TechnologyComponent) {
		TechnologyComponent->ApplyTechnologyBundleAtSpawn(UnitAsset);
	}

	// Initialize based on unit mode
	if (UnitAsset->bSquadUnit) {
		SetSquadMode_Implementation(true);
		InitAsPawns_Implementation();
	} else {
		SetSquadMode_Implementation(false);
		InitAsPawn_Implementation();
	}

	ForceNetUpdate();
}

// Attached Pawn API
// ==================================================================================================
/** Initializes the SAFUnit using pawn representation. */
void ASAFUnit::InitAsPawn_Implementation() {
	if (!HasAuthority()) return;
	
	USAFUnitAsset* UnitAsset = GetUnitAsset();
	if (!UnitAsset) { SAFDEBUG_ERROR("InitAsPawn aborted: null UnitAsset."); return; }
	if (UnitAsset->bSquadUnit) { SAFDEBUG_WARNING("InitAsPawn called on squad unit. Use InitAsPawns instead."); return; }
	
	UWorld* World = GetWorld();
	if (!World) { SAFDEBUG_ERROR("InitAsPawn aborted: World is nullptr."); return; }
	
	// Get the pawn asset for single-pawn mode
	USAFPawnAsset* PawnAsset = UnitAsset->Pawn.LoadSynchronous();
	if (!PawnAsset) { SAFDEBUG_ERROR("InitAsPawn aborted: null PawnAsset in UnitAsset."); return; }
	
	// Spawn the pawn
	FActorSpawnParameters Params;
	Params.Owner = this;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	
	APawn* SpawnedPawn = World->SpawnActor<APawn>(ASAFPawn::StaticClass(), GetActorLocation(), GetActorRotation(), Params);
	if (!SpawnedPawn) { SAFDEBUG_WARNING("InitAsPawn failed to spawn pawn. Culling."); Destroy(); return; }
	
	// Initialize the pawn
	if (ISAFPawnInterface* PawnInterface = Cast<ISAFPawnInterface>(SpawnedPawn)) {
		ISAFPawnInterface::Execute_InitPawn(SpawnedPawn, PawnAsset, this);
		ISAFUnitInterface::Execute_AttachToPawn(this, SpawnedPawn);
		SAFDEBUG_SUCCESS(FORMATSTR("Pawn '%s' initialized for Unit '%s'.", *SpawnedPawn->GetName(), *GetName()));
	} else {
		SAFDEBUG_ERROR("InitAsPawn: spawned pawn does not implement ISAFPawnInterface. Culling.");
		SpawnedPawn->Destroy();
		Destroy();
	}
}

/** Initializes this Unit Actor using multiple pawns in SquadMode */
void ASAFUnit::InitAsPawns_Implementation() {
	if (!HasAuthority()) return;
	
	USAFUnitAsset* UnitAsset = GetUnitAsset();
	if (!UnitAsset) { SAFDEBUG_ERROR("InitAsPawns aborted: null UnitAsset."); return; }
	if (!UnitAsset->bSquadUnit) { SAFDEBUG_WARNING("InitAsPawns called on single-pawn unit. Use InitAsPawn instead."); return; }
	
	if (!(UnitAsset->Pawns.Num() > 0)) {
		SAFDEBUG_WARNING("UnitAsset has no pawns set. Squad will be empty.");
		return;
	}
	
	if (UnitAsset->Positions.Num() > 0) {
		Positions = UnitAsset->Positions;
	} else {
		SAFDEBUG_WARNING("UnitAsset has no positions set. All pawns will be spawned at relative ZeroVector.");
	}
	
	const FTransform UnitTransform = GetActorTransform();
	const FVector UnitLoc = UnitTransform.GetLocation();
	const FRotator UnitRot = UnitTransform.Rotator();
	const int32 Count = UnitAsset->Pawns.Num();
	UWorld* World = GetWorld();
	if (!World) { SAFDEBUG_ERROR("InitAsPawns aborted: World is nullptr."); return; }
	
	FActorSpawnParameters Params;
	Params.Owner = this;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	
	// Iterate to spawn pawns at positions (if valid)
	for (int32 i = 0; i < Count; ++i) {
		FVector Offset = FVector::ZeroVector;
		if (UnitAsset->Positions.IsValidIndex(i)) Offset = UnitAsset->Positions[i];
		const FVector WorldOffset = UnitRot.RotateVector(Offset);
		const FVector SpawnLoc = UnitLoc + WorldOffset;
		
		// Load the pawn asset
		USAFPawnAsset* PawnAsset = UnitAsset->Pawns[i].LoadSynchronous();
		if (!PawnAsset) { SAFDEBUG_WARNING(FORMATSTR("InitAsPawns failed to load PawnAsset for pawn %d", i)); continue; }
		
		// Spawn the pawn
		APawn* Pawn = World->SpawnActor<APawn>(ASAFPawn::StaticClass(), SpawnLoc, UnitRot, Params);
		if (!Pawn) { SAFDEBUG_WARNING(FORMATSTR("InitAsPawns failed to spawn pawn %d.", i)); continue; }
		
		// Validate and initialize the pawn
		if (ISAFPawnInterface* PawnInterface = Cast<ISAFPawnInterface>(Pawn)) {
			ISAFUnitInterface::Execute_AddPawnToSquad(this, Pawn, i == 0);
			ISAFPawnInterface::Execute_InitPawn(Pawn, PawnAsset, this);
			SAFDEBUG_SUCCESS(FORMATSTR("Pawn '%s' initialized. Squad '%s' now has '%d' members.", *Pawn->GetName(), *GetName(), SquadPawns.Num()));
		} else {
			SAFDEBUG_WARNING(FORMATSTR("InitAsPawns spawned an invalid pawn %d, culling.", i));
			Pawn->Destroy();
		}
	}
}

void ASAFUnit::AttachToPawn_Implementation(APawn* Pawn) {
	if (!HasAuthority()) return;
	if (!IsValid(Pawn)) { SAFDEBUG_WARNING("AttachToPawn aborted: invalid actor."); return; }
	if (AttachedPawn == Pawn && GetAttachParentActor() == Pawn) return;
	if (IsValid(AttachedPawn)) ISAFUnitInterface::Execute_DetachFromPawn(this);;
	

	// Attach and track tickless, set net policy & bind OnDestroy
	FAttachmentTransformRules Rules(EAttachmentRule::SnapToTarget, false);
	AttachToActor(Pawn, Rules);
	SetActorRelativeTransform(FTransform::Identity);
	if (AActor* PawnOwner = Pawn->GetOwner()) SetOwner(PawnOwner);
	AttachedPawn = Pawn;
	AttachedPawn->OnDestroyed.AddDynamic(this, &ASAFUnit::OnAttachedPawnDestroyedProxy);
	ForceNetUpdate();
}

// Detaches this Unit Actor from its pawn, if it is attached to one.
void ASAFUnit::DetachFromPawn_Implementation() {
	if (!HasAuthority()) return;

	DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	if (AttachedPawn) {
		AttachedPawn->OnDestroyed.RemoveDynamic(this, &ASAFUnit::OnAttachedPawnDestroyedProxy);
		AttachedPawn = nullptr;
	}

	ForceNetUpdate();
}

void ASAFUnit::AddPawnToSquad_Implementation(APawn* Pawn, bool bIsNewLeader) {
	if (!bSquadMode) { SAFDEBUG_WARNING("AddPawnToSquad called on non-squad unit."); return; }
	if (!IsValid(Pawn)) { SAFDEBUG_WARNING("AddPawnToSquad aborted: invalid pawn."); return; }

	// Check if already present (and not new leader)
	bool bPresent = SquadPawns.Contains(Pawn);
	if (bPresent && !bIsNewLeader) {
		SAFDEBUG_INFO("AddPawnToSquad pawn is already present in squad. Function call discarded.");
		return;
	} else if (bPresent) {
		SAFDEBUG_SUCCESS(FORMATSTR("AddPawnToSquad: '%s' promoted to leader.", *Pawn->GetName()));
		ISAFUnitInterface::Execute_SetSquadLeader(this, Pawn);
		return;
	}
	
	// Clear old squad then add unique
	if (ISAFPawnInterface* PawnInterface = Cast<ISAFPawnInterface>(Pawn)) {
		ASAFUnit* OldUnit = ISAFPawnInterface::Execute_GetOwningUnit(Pawn);
		if (IsValid(OldUnit) && OldUnit != this) {
			ISAFUnitInterface::Execute_RemovePawnFromSquad(this, Pawn);
		}
	}
	
	// Add and set owning unit
	SquadPawns.AddUnique(Pawn);
	if (ISAFPawnInterface* PawnInterface = Cast<ISAFPawnInterface>(Pawn)) ISAFPawnInterface::Execute_SetOwningUnit(Pawn, this);
	if (bIsNewLeader) ISAFUnitInterface::Execute_SetSquadLeader(this, Pawn);
	SAFDEBUG_SUCCESS(FORMATSTR("AddPawnToSquad: successfully added pawn '%s'.", *Pawn->GetName()));
	ForceNetUpdate();
}

bool ASAFUnit::RemovePawnFromSquad_Implementation(APawn* Pawn) {
	if (!bSquadMode) { SAFDEBUG_WARNING("RemovePawnFromSquad called on non-squad unit."); return false; }
	if (!IsValid(Pawn)) { SAFDEBUG_ERROR("RemovePawnFromSquad aborted: invalid pawn."); return false; }
	if (!SquadPawns.Contains(Pawn)) {
		SAFDEBUG_INFO("RemovePawnFromSquad called, but pawn is not present in squad. Function call discarded.");
		return false;
	}
	
	SquadPawns.Remove(Pawn);
	if (ISAFPawnInterface* PawnInterface = Cast<ISAFPawnInterface>(Pawn)) {
		ISAFPawnInterface::Execute_SetOwningUnit(Pawn, nullptr);
	}
	
	// If we removed the leader, find a new one
	if (AttachedPawn == Pawn) {
		APawn* NewLeader = FindNextSquadLeader();
		if (NewLeader) ISAFUnitInterface::Execute_SetSquadLeader(this, NewLeader);
		else ISAFUnitInterface::Execute_DetachFromPawn(this);
	}
	
	if (SquadPawns.Num() == 0) ISAFUnitInterface::Execute_CullSquadUnit(this);
	ForceNetUpdate();
	return true;
}

void ASAFUnit::SetSquadLeader_Implementation(APawn* InSquadLeader) {
	if (!bSquadMode) { SAFDEBUG_WARNING("SetSquadLeader called on non-squad unit."); return; }
	if (!IsValid(InSquadLeader)) { SAFDEBUG_WARNING("SetSquadLeader aborted: invalid pawn."); return; }
	if (!SquadPawns.Contains(InSquadLeader)) { 
		SAFDEBUG_WARNING("SetSquadLeader aborted: pawn is not a member of this squad."); 
		return; 
	}
	
	ISAFUnitInterface::Execute_AttachToPawn(this, InSquadLeader);
	ForceNetUpdate();
}

APawn* ASAFUnit::FindNextSquadLeader_Implementation() {
	if (!bSquadMode) return nullptr;
	
	for (const TObjectPtr<APawn>& Member : SquadPawns) {
		if (!Member) continue;
		if (IsValid(Member) && Member != AttachedPawn && !Member->IsActorBeingDestroyed()) {
			return Member;
		}
	}
	return nullptr;
}

APawn* ASAFUnit::GetFrontmostPawnInSquad_Implementation() const {
	if (!bSquadMode) return AttachedPawn;
	if (SquadPawns.Num() <= 0) { 
		SAFDEBUG_WARNING("GetFrontmostPawnInSquad aborted: SquadPawns is empty.");
		return nullptr;
	}
	
	APawn* SquadFront = SquadPawns[0].Get();
	if (!IsValid(SquadFront)) {
		SAFDEBUG_ERROR("GetFrontmostPawnInSquad found an invalid pawn at position 0 "
		"of SquadPawns. This should never occur.");
	}

	return SquadFront;
}

void ASAFUnit::ReinitPositions_Implementation(const TArray<FVector>& InPositions) {
	if (!bSquadMode) { SAFDEBUG_WARNING("ReinitPositions called on non-squad unit."); return; }
	
	if (Positions.Num() != InPositions.Num()) {
		SAFDEBUG_WARNING("ReinitPositions called with different number of positions. "
		"Some positions may be ZeroVector or unfilled.");
	}
	
	int32 itrs = Positions.Num() > InPositions.Num() ? Positions.Num() : InPositions.Num();
	TArray<FVector> NewPositions;
	NewPositions.Reserve(itrs);
	for (int32 i = 0; i < itrs; i++) {
		FVector Position = InPositions.IsValidIndex(i) ? InPositions[i] : FVector::ZeroVector;
		NewPositions.Add(Position);
	}
	
	Positions = NewPositions;
	ForceNetUpdate();
}

void ASAFUnit::InvertPositions_Implementation() {
	if (!bSquadMode) { SAFDEBUG_WARNING("InvertPositions called on non-squad unit."); return; }
	
	const int32 Count = SquadPawns.Num();
	if (Count <= 1) return;
	
	const int32 SplitIndex = Count / 2;
	decltype(SquadPawns) NewOrder;
	NewOrder.Reserve(Count);
	
	for (int32 i = SplitIndex; i < Count; ++i) { NewOrder.Add(SquadPawns[i]); }
	for (int32 i = 0; i < SplitIndex; ++i) { NewOrder.Add(SquadPawns[i]); }
	
	SquadPawns = MoveTemp(NewOrder);
	ForceNetUpdate();
}

TArray<FVector> ASAFUnit::GetPositionsAtPoint_Implementation(const FVector& Point, bool bTriggersInversion) {
	TArray<FVector> OutPositions;
	const FVector Location = GetActorLocation();
	const FVector PointWithFlattenedZ = FVector(Point.X, Point.Y, 0.f);
	const FVector LocationFlattenedZ = FVector(Location.X, Location.Y, 0.f);
	const FRotator Rotation = UKismetMathLibrary::FindLookAtRotation(LocationFlattenedZ, PointWithFlattenedZ);
	const float Dot = FVector::DotProduct(GetActorForwardVector(), Rotation.Vector());
	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	if (!NavSys) { SAFDEBUG_ERROR("GetPositionsAtPoint failed: no NavSys."); return Positions; }

	// Progressive query extents for better nav mesh projection
	const TArray<float> QueryExtents = {250.f, 500.f, 1000.f, 2500.f, 5000.f};
	
	// Single units just project to nav
	if (!bSquadMode) {
		FNavLocation NavProjection;
		bool bFoundNavLocation = false;
		
		for (float Extent : QueryExtents) {
			const FVector QueryExtent(Extent, Extent, Extent);
			if (NavSys->ProjectPointToNavigation(Point, NavProjection, QueryExtent)) {
				bFoundNavLocation = true;
				break;
			}
		}
		
		if (!bFoundNavLocation) {
			SAFDEBUG_WARNING(FORMATSTR(
			"GetPositionsAtPoint failed to project point (%s) to navigation "
			"mesh after trying all query extents.", *Point.ToString()));
			NavProjection.Location = Point; // Fallback to original point
		}
		
		OutPositions.Add(NavProjection.Location);
		return OutPositions;
	}
	
	// Squad units rotate positions and project to nav
	OutPositions.Reserve(Positions.Num());
	if (Dot < 0.f && bTriggersInversion) ISAFUnitInterface::Execute_InvertPositions(this);
	for (const FVector& Position : Positions) {
		FVector RotatedPosition = Rotation.RotateVector(Position) + Point;
		FNavLocation NavProjection;
		bool bFoundNavLocation = false;
		
		for (float Extent : QueryExtents) {
			const FVector QueryExtent(Extent, Extent, Extent);
			if (NavSys->ProjectPointToNavigation(RotatedPosition, NavProjection, QueryExtent)) {
				bFoundNavLocation = true;
				break;
			}
		}
		
		if (!bFoundNavLocation) {
			SAFDEBUG_WARNING(FORMATSTR(
			"GetPositionsAtPoint failed to project squad position (%s) to "
			"navigation mesh after trying all query extents.", *RotatedPosition.ToString()));
			NavProjection.Location = RotatedPosition; // Fallback to original position
		}
		
		OutPositions.Add(NavProjection.Location);
	}
	
	return OutPositions;
}

TArray<FVector> ASAFUnit::GetCoverPositionsAtPoint_Implementation(const FVector& Point, bool bTriggersInversion) {	
	TArray<FVector> OutPositions;
	
	// Nearest cover & quick validation
	AActor* NearestCoverActor = nullptr;
	UPrimitiveComponent* NearestCoverComponent;
	USAFCoverCollider* NearestCoverCollider;
	ESAFCoverType NearestCoverType = ESAFCoverType::None;
	SAFCoverUtilities::FindNearestCover(GetWorld(), Point, CoverSearchRadius, NearestCoverActor, NearestCoverComponent, NearestCoverType);
	NearestCoverCollider = IsValid(NearestCoverComponent) ? Cast<USAFCoverCollider>(NearestCoverComponent) : nullptr;
	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	if (NearestCoverType == ESAFCoverType::Negative 
		|| NearestCoverType == ESAFCoverType::None 
		|| !IsValid(NearestCoverActor) 
		|| !IsValid(NearestCoverCollider) 
		|| !NavSys
	) return ISAFUnitInterface::Execute_GetPositionsAtPoint(this, Point, bTriggersInversion);
	
	// Build an array for SAFLib that is const
	TArray<AActor*> MembersAsActors;
	if (bSquadMode) {
		MembersAsActors.Reserve(SquadPawns.Num());
		for (APawn* Pawn : SquadPawns) MembersAsActors.Add(Pawn);
	} else if (IsValid(AttachedPawn)) { 
		MembersAsActors.Add(AttachedPawn);
	} else { 
		SAFDEBUG_WARNING("GetCoverPositionsAtPoint: could not find any pawns for positions. Returning empty array."); 
		return OutPositions;
	}
	
	// Get cover settings from asset
	const USAFUnitAsset* UnitAsset = GetUnitAsset();
	const bool bWrapsCover = UnitAsset ? UnitAsset->bWrapsCover : true;
	const bool bScattersInCover = UnitAsset ? UnitAsset->bScattersInCover : true;
	const float CoverSpacingModifier = UnitAsset ? UnitAsset->CoverSpacingModifier : 2.f;
	const float CoverRowOffsetModifier = UnitAsset ? UnitAsset->CoverRowOffsetModifier : 1.f;
	const float LateralStaggerModifier = UnitAsset ? UnitAsset->LateralStaggerModifier : 1.f;
	
	// If we hit a nav-blocking cover object, build the cover stacked up against the nearest edge
	// else build a clumped formation near the Point
	FVector A, B, C, D;
	if (NearestCoverCollider->GetCoverNavBounds(A, B, C, D, true)) 
		OutPositions = SAFCoverUtilities::BuildCoverPositionsAroundCoverBox(
			A, B, C, D, Point, 
			MembersAsActors, NavSys, bWrapsCover, bScattersInCover, CoverSearchRadius, 
			CoverSpacingModifier, CoverRowOffsetModifier, LateralStaggerModifier
		); 

	else OutPositions = SAFCoverUtilities::BuildCoverPositionsAroundCoverPoint(
		Point, 
		MembersAsActors, NavSys, bScattersInCover, CoverSearchRadius, 
		CoverSpacingModifier);
	
	return OutPositions;
}

void ASAFUnit::CullSquadUnit_Implementation() {
	if (!HasAuthority()) return;
	if (!bSquadMode) { SAFDEBUG_INFO(FORMATSTR("CullSquadUnit called on non-squad unit. Discarding.")); return; }
	
	if (bSquadMode && SquadPawns.Num() > 0) ISAFUnitInterface::Execute_CullSquadUnitAndPawns(this);
	else { SAFDEBUG_INFO(FORMATSTR("Squad '%s' has no pawns: destroying unit.", *GetName())); Destroy(); }
}

void ASAFUnit::CullSquadUnitAndPawns_Implementation() {
	if (!HasAuthority()) return;
	if (!bSquadMode) { SAFDEBUG_INFO(FORMATSTR("CullSquadUnitAndPawns called on non-squad unit. Discarding.")); return; }

	if (SquadPawns.Num() <= 0) ISAFUnitInterface::Execute_CullSquadUnit(this);
	else {
		for (APawn* SquadPawn : SquadPawns) if (IsValid(SquadPawn)) SquadPawn->Destroy();
		SAFDEBUG_INFO(FORMATSTR("Squad '%s' has called Destroy() on its pawns: destroying unit.", *GetName()));
		Destroy();
	}
}

float ASAFUnit::GetFormationSpacing_Implementation() const {
	if (!bSquadMode) { return FormationSpacing; }
	
	// For squad units, calculate spacing based on positions
	const TArray<FVector>* SourcePositions = &Positions;
	if (SourcePositions->Num() == 0) {
		const USAFUnitAsset* UnitAsset = GetUnitAsset();
		if (UnitAsset) SourcePositions = &UnitAsset->Positions;
	}
	
	if (SourcePositions->Num() == 0) return FormationSpacing;
	const FVector Center = (*SourcePositions)[0];
	float MaxDistSq = 0.f;
	
	for (int32 i = 1; i < SourcePositions->Num(); ++i) {
		const float DistSq = FVector::DistSquared((*SourcePositions)[i], Center);
		if (DistSq > MaxDistSq) MaxDistSq = DistSq;
	}
	
	const float Radius = FMath::Sqrt(MaxDistSq);
	return Radius + FormationSpacing;
}

// Override the basic on destroy implementation from ASAFUnit
void ASAFUnit::OnAttachedPawnDestroyed_Implementation(AActor* DestroyedPawn) {
	if (!HasAuthority()) return;
	ISAFUnitInterface::Execute_DetachFromPawn(this);
	
	if (bSquadMode) {
		// For squad units, find a new leader
		APawn* NewLeader = FindNextSquadLeader();
		if (!NewLeader) { 
			SAFDEBUG_WARNING("OnAttachedPawnDestroyed: Squad could not find next leader. Culling via CullSquadUnitAndPawns."); 
			CullSquadUnitAndPawns(); return;
		}

		ISAFUnitInterface::Execute_AttachToPawn(this, NewLeader);
	} else {
		// For single-pawn units, we lost our only pawn
		SAFDEBUG_WARNING("OnAttachedPawnDestroyed: Single-pawn unit lost its pawn. Culling unit.");
		CullSquadUnit();
	}
}

// Call to issue this an order.
bool ASAFUnit::Order_Implementation(FSAFOrder Order) {
	const bool bTagValid = Order.Tag.IsValid();
	const FString TagStr  = bTagValid ? Order.Tag.ToString() : TEXT("<Invalid>");
	if (!AbilitySystem) { SAFDEBUG_ERROR(FORMATSTR("Order: '%s' failed: ASC is nullptr.", *GetName()));               return false; }
	if (!bTagValid)     { SAFDEBUG_ERROR(FORMATSTR("Order: '%s' failed: invalid tag '%s'.", *GetName(), *TagStr));    return false; }
	if (!bOrderable)    { SAFDEBUG_INFO (FORMATSTR("Order: '%s' rejected orders, it is not orderable.", *GetName())); return false; }

	// Make order visible to abilities and notify listeners
	CurrentOrder = Order;
	OnOrderReceived.Broadcast(Order);

	// Build gameplay event payload
	FGameplayEventData Data;
	Data.EventTag = Order.Tag;
	Data.Instigator = this;
	Data.Target = Order.Target.Get();
	Data.EventMagnitude = 1.f;
	Data.TargetData = SAFLibrary::MakeTargetDataFromOrder(Order, this);
	UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(this, Order.Tag, Data);

	SAFDEBUG_SUCCESS(FORMATSTR( //174
		"Unit '%s' dispatched GameplayEvent tagged '%s' at Actor '%s' with Vectors: %s.", 
		*GetName(), *TagStr, 
		Order.Target.Get() ? *Order.Target->GetName() : TEXT("<None>"), 
		*Order.Vectors.ToString()
	));
	
	return true;
}

// Call to retrieve all order tags this unit has assigned to it.
// (i.e. what orders this unit can execute).
void ASAFUnit::GetOrderTags_Implementation(TArray<FGameplayTag>& OutTags) const {
	if (!AbilitySystem) { SAFDEBUG_ERROR("GetOrderTags aborted: AbilitySystem was nullptr."); return; }
	
	OutTags.Reset();
	for (const FGameplayAbilitySpec& Spec : AbilitySystem->GetActivatableAbilities()) {
		const UGameplayAbility* Ability = Spec.Ability;
		if (!Ability) { SAFDEBUG_WARNING("GetOrderTags skipped a null ability in one of the AbilitySpecs."); continue; }

		const FGameplayTagContainer& AssetTags = Ability->GetAssetTags();
		for (const FGameplayTag& Tag : AssetTags) OutTags.AddUnique(Tag);
	}
}

// Notifies listeners that the current order has been completed
bool ASAFUnit::NotifyOrderCompleted_Implementation() {
	OnOrderCompleted.Broadcast(this, CurrentOrder);
	SAFDEBUG_SUCCESS(FORMATSTR("'%s' completed order '%s'.", *GetName(), *this->CurrentOrder.Tag.ToString()));
	CurrentOrder = FSAFOrder{};
	return true;
}

// GAS Helpers
// ==================================================================================================================================================
void ASAFUnit::InitAbilitySystem() {
	if (!AbilitySystem) { SAFDEBUG_ERROR(FORMATSTR("Actor '%s' called InitASC but no AbilitySystemComponent was found.", *GetName()));      return; }
	AbilitySystem->InitAbilityActorInfo(this, this);
}

// Gives abilities on begin play based on the assigned asset
void ASAFUnit::GiveAbilitiesFromAsset() {
	if (!Asset)         { SAFDEBUG_ERROR(FORMATSTR("Actor '%s' could not be given abilities: Asset was nullptr.", *GetName()));             return; }
	if (!AbilitySystem) { SAFDEBUG_ERROR(FORMATSTR("Actor '%s' could not be given abilities: AbilitySystem was nullptr.", *GetName()));     return; }

	for (const auto& Ability : GetUnitAsset()->Abilities) {
		if (!Ability.AbilityClass) { SAFDEBUG_WARNING("Ability skipped: An ability in the UnitData was null or invalid."); continue; }
		TSubclassOf<UGameplayAbility> AbilityClass = Ability.AbilityClass.Get();
		FGameplayAbilitySpec Spec(AbilityClass, Ability.Level, Ability.InputID);
		Spec.GetDynamicSpecSourceTags().AppendTags(Ability.AbilityTags);
		AbilitySystem->GiveAbility(Spec);
	}
}

// Gives attributes from the USAFUnitAttributes set on begin play, based on the assigned asset
void ASAFUnit::GiveAttributesFromAsset() {
	const USAFUnitAsset* UnitAsset = GetUnitAsset();
	if (!UnitAsset)     { SAFDEBUG_ERROR(FORMATSTR("Actor '%s' could not be given attributes: UnitAsset was nullptr.", *GetName()));     return; }
	if (!AbilitySystem) { SAFDEBUG_ERROR(FORMATSTR("Actor '%s' could not be given attributes: AbilitySystem was nullptr.", *GetName())); return; }

	// 1) Ensure all AttributeSets listed on the asset are present on the ASC (no duplicates).
	TArray<UAttributeSet*> SetInstances;
	SetInstances.Reserve(UnitAsset->AttributeSets.Num());

	for (const TSubclassOf<UAttributeSet>& SetClass : UnitAsset->AttributeSets) {
		if (!*SetClass) continue;
		const UAttributeSet* ExistingConst = AbilitySystem->GetAttributeSet(SetClass);
		UAttributeSet* Instance = const_cast<UAttributeSet*>(ExistingConst);
		if (!Instance) {
			Instance = NewObject<UAttributeSet>(this, SetClass);
			if (ensure(Instance)) AbilitySystem->AddAttributeSetSubobject(Instance);
			else { SAFDEBUG_WARNING(FORMATSTR("Actor '%s' failed to create AttributeSet '%s'.", *GetName(), *SetClass->GetName())); continue; }
		}

		SetInstances.Add(Instance);
	}

	// 2) Seed from attribute table rows (if provided). Later rows win. Only for sets deriving from USAFAttributeSet.
	if (UnitAsset->AttributeTableRows.Num() == 0) 
		{ SAFDEBUG_INFO(FORMATSTR("Actor '%s' has no AttributeTableRows; using AttributeSet class defaults.", *GetName())); return; }
		
	for (UAttributeSet* Set : SetInstances) {
		if (!Set) continue;
		if (USAFAttributeSet* AttrBase = Cast<USAFAttributeSet>(Set))
			for (const FDataTableRowHandle& Handle : UnitAsset->AttributeTableRows)
				if (Handle.DataTable && !Handle.RowName.IsNone())
					AttrBase->SeedFromRowHandle(Handle);
	}
}

// Gives tags on begin play bassed on the assigned asset
void ASAFUnit::GiveTagsFromAsset() {
	if (!Asset)         { SAFDEBUG_ERROR(FORMATSTR("Actor '%s' could not be given tags: Asset was nullptr.", *GetName()));                  return; }
	if (!AbilitySystem) { SAFDEBUG_ERROR(FORMATSTR("Actor '%s' could not be given tags: AbilitySystem was nullptr.", *GetName()));          return; }

	const FGameplayTagContainer& InTags = GetUnitAsset()->Tags;
	if (InTags.IsEmpty()) return;

	int32 Granted = 0;
	for (const FGameplayTag& Tag : InTags) {
		if (!Tag.IsValid()) { SAFDEBUG_WARNING("GiveTagsFromAsset skipped an invalid tag in DefaultTags."); continue; }
		if (!AbilitySystem->HasMatchingGameplayTag(Tag)) {
			AbilitySystem->AddLooseGameplayTag(Tag);
			++Granted;
		}
	}

	if (Granted > 0) SAFDEBUG_SUCCESS(FORMATSTR("Actor '%s' granted %d default tag(s) from asset.", *GetName(), Granted));
}

// Applies GAS startup effects
void ASAFUnit::ApplyStartupEffects() {
	if (!Asset)         { SAFDEBUG_ERROR(FORMATSTR("Actor '%s' could not apply startup effects: Asset was nullptr.", *GetName()));          return; }
	if (!AbilitySystem) { SAFDEBUG_ERROR(FORMATSTR("Actor '%s' could not apply startup effects: AbilitySystem was nullptr.", *GetName()));  return; }
	FGameplayEffectContextHandle Context = AbilitySystem->MakeEffectContext();
	Context.AddSourceObject(this);

	for (const auto& GameplayEffectClass : GetUnitAsset()->StartupEffects) {
		if (GameplayEffectClass) AbilitySystem->ApplyGameplayEffectToSelf(GameplayEffectClass->GetDefaultObject<UGameplayEffect>(), 1.f, Context);
		else SAFDEBUG_WARNING("GameplayEffectClass skipped during startup effects: GameplayEffectClass was nulltpr.");
	}
}

// Replication
// ==================================================================================================================================================
void ASAFUnit::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const {
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ASAFUnit, AttachedPawn);
	DOREPLIFETIME(ASAFUnit, SquadPawns);
	DOREPLIFETIME(ASAFUnit, bSquadMode);
	DOREPLIFETIME(ASAFUnit, Positions);
	DOREPLIFETIME(ASAFUnit, bOrderable);
	DOREPLIFETIME(ASAFUnit, FormationSpacing);
	DOREPLIFETIME(ASAFUnit, CurrentFormation);
	DOREPLIFETIME(ASAFUnit, ProductionComponent);
	DOREPLIFETIME(ASAFUnit, CurrentCover);
	DOREPLIFETIME(ASAFUnit, CoverSearchRadius);
}

void ASAFUnit::OnRep_AttachedPawn() {}
void ASAFUnit::OnRep_SquadPawns() {}
void ASAFUnit::OnRep_CurrentFormation() {}
void ASAFUnit::OnRep_CurrentCover() {}

#if WITH_EDITOR
void ASAFUnit::OnConstruction(const FTransform& Transform) {
	Super::OnConstruction(Transform);
	
	// Always clear preview before any runtime initialization
	if (GetWorld() && GetWorld()->IsGameWorld()) {
		ClearEditorPreview();
	}
}

void ASAFUnit::UpdateEditorPreview() {
	// Only run in editor, not in PIE or game
	if (!GetWorld() || GetWorld()->IsGameWorld()) {
		return;
	}

	ClearEditorPreview();

	USAFUnitAsset* UnitAsset = GetUnitAsset();
	if (!UnitAsset) {
		return;
	}

	// Create root for preview components
	EditorPreviewRoot = NewObject<USceneComponent>(this, USceneComponent::StaticClass(), TEXT("EditorPreviewRoot"));
	EditorPreviewRoot->SetupAttachment(RootComponent);
	EditorPreviewRoot->SetIsReplicated(false);
	EditorPreviewRoot->bIsEditorOnly = true;
	EditorPreviewRoot->CreationMethod = EComponentCreationMethod::UserConstructionScript;
	EditorPreviewRoot->RegisterComponent();

	// Handle squad units with multiple pawns
	if (UnitAsset->bSquadUnit && UnitAsset->Pawns.Num() > 0) {
		for (int32 i = 0; i < UnitAsset->Pawns.Num(); i++) {
			USAFPawnAsset* PawnAsset = SAFAssetResolver::ResolveAsset(UnitAsset->Pawns[i]);
			if (!PawnAsset || PawnAsset->SkeletalMesh.IsNull()) {
				continue;
			}

			USkeletalMesh* Mesh = PawnAsset->SkeletalMesh.LoadSynchronous();
			if (!Mesh) {
				continue;
			}

			// Create preview mesh for this squad member
			FString ComponentName = FString::Printf(TEXT("EditorPreviewMesh_%d"), i);
			USkeletalMeshComponent* PreviewMesh = NewObject<USkeletalMeshComponent>(this, USkeletalMeshComponent::StaticClass(), FName(*ComponentName));
			PreviewMesh->SetSkeletalMesh(Mesh);
			PreviewMesh->SetupAttachment(EditorPreviewRoot);
			PreviewMesh->SetIsReplicated(false);
			PreviewMesh->bIsEditorOnly = true;
			PreviewMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			PreviewMesh->CreationMethod = EComponentCreationMethod::UserConstructionScript;

			// Apply mesh offset
			PreviewMesh->SetRelativeTransform(PawnAsset->MeshOffset);

			// Apply squad position
			if (UnitAsset->Positions.IsValidIndex(i)) {
				PreviewMesh->AddRelativeLocation(UnitAsset->Positions[i]);
			}

			PreviewMesh->RegisterComponent();
		}
	}
	// Handle single pawn units
	else if (!UnitAsset->Pawn.IsNull()) {
		USAFPawnAsset* PawnAsset = SAFAssetResolver::ResolveAsset(UnitAsset->Pawn);
		if (PawnAsset && !PawnAsset->SkeletalMesh.IsNull()) {
			USkeletalMesh* Mesh = PawnAsset->SkeletalMesh.LoadSynchronous();
			if (Mesh) {
				USkeletalMeshComponent* PreviewMesh = NewObject<USkeletalMeshComponent>(this, USkeletalMeshComponent::StaticClass(), TEXT("EditorPreviewMesh"));
				PreviewMesh->SetSkeletalMesh(Mesh);
				PreviewMesh->SetupAttachment(EditorPreviewRoot);
				PreviewMesh->SetIsReplicated(false);
				PreviewMesh->bIsEditorOnly = true;
				PreviewMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				PreviewMesh->SetRelativeTransform(PawnAsset->MeshOffset);
				PreviewMesh->CreationMethod = EComponentCreationMethod::UserConstructionScript;
				PreviewMesh->RegisterComponent();
			}
		}
	}
}
#endif
