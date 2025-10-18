#include "Classes/Units/SAFSquad.h"
#include "Classes/Units/SAFSquadMember.h"
#include "Components/SAFCoverCollider.h"
#include "Components/CapsuleComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Assets/Units/SAFSquadAsset.h"
#include "Engine/World.h"
#include "Enums/SAFCoverTypes.h"
#include "GameFramework/Character.h"
#include "Interfaces/Units/SAFSquadMemberInterface.h"
#include "Kismet/KismetMathLibrary.h"
#include "NavigationSystem.h"
#include "Net/UnrealNetwork.h"
#include "Resolvers/SAFAssetResolver.h"
#include "Utils/SAFCoverUtilities.h"
#include "Utils/SAFLibrary.h"
#include "Debug/SAFDebugTool.h"

ASAFSquad::ASAFSquad() {
	SquadMemberClass = ASAFSquadMember::StaticClass();
}

// Asset Interface Overrides
// ============================================================================================================================================
// Overide adds additional initalization steps
void ASAFSquad::InitFromAsset_Implementation(USAFAsset* InAsset, ASAFPlayerState* InOwner, bool bReinitialize) {
	if (!HasAuthority()) return;

	// Type validation + Super
	USAFAsset* InitFromAsset = InAsset ? InAsset : SAFAssetResolver::ResolveAsset(Asset);
	USAFSquadAsset* SquadAsset = Cast<USAFSquadAsset>(InitFromAsset);
	if (!SquadAsset) { SAFDEBUG_WARNING(FORMATSTR("InitUnit: invalid Data Asset Type on actor '%s'. Culling.", *GetName())); Destroy(); return; }
	Super::InitFromAsset_Implementation(SquadAsset, InOwner, bReinitialize);

	// InitSquad
	InitSquad(GetSquadAsset());
}

// Generates the formation spacing for this squad by calculating the radius around the central
// position (Positions[0]) that would fully enclose all other positions, plus a spacing buffer
// pulled from the FormationSpacing prop. Prefers instance positions, but will fallback on the
// data asset and then finally just the unit prop if all else fails.
float ASAFSquad::GetFormationSpacing_Implementation() const {
	const TArray<FVector>* SourcePositions = &Positions;
	if (SourcePositions->Num() == 0) {
		const USAFSquadAsset* SquadAsset = Cast<USAFSquadAsset>(SAFAssetResolver::ResolveAsset(Asset));
		if (SquadAsset) SourcePositions = &SquadAsset->Positions;
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

// Squad API
// ========================================================================================================================================================
// Initializes the squad (generates members and positions)
void ASAFSquad::InitSquad_Implementation(USAFSquadAsset* SquadAsset) {
	if (!HasAuthority()) return;
	if (bInitialized) { SAFDEBUG_WARNING(FORMATSTR("InitSquad called twice on squad '%s'. Discarding.", *GetName())); return; }
	
	UClass* MemberClass = SquadMemberClass.LoadSynchronous();
	if (!MemberClass 
		|| !MemberClass->ImplementsInterface(USAFActorInterface::StaticClass())
		|| !MemberClass->ImplementsInterface(USAFSquadMemberInterface::StaticClass())
	) { SAFDEBUG_ERROR("InitSquad aborted: invalid SquadMemberClass."); return;	}
	if (!SquadAsset) { SAFDEBUG_ERROR("InitSquad aborted: null SquadAsset."); return; }
	if (!(SquadAsset->Positions.Num() > 0)) SAFDEBUG_WARNING("SquadAsset has no positions set. All SquadMembers will be spawned at relative ZeroVector.");
	else Positions = SquadAsset->Positions;

	const FTransform SquadTransform = GetActorTransform();
	const FVector SquadLoc = SquadTransform.GetLocation();
	const FRotator SquadRot = SquadTransform.Rotator();
	const int32 Count = SquadAsset->Members.Num();
	UWorld* World = GetWorld();
	if (!World) { SAFDEBUG_ERROR("InitSquad aborted: World is nullptr."); return; }
	
	FActorSpawnParameters Params;
	Params.Owner = this;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

	// Iterate to spawn SquadMembers at positions (if valid)
	for (int32 i = 0; i < Count; ++i) {
		FVector Offset = FVector::ZeroVector;
		if (SquadAsset->Positions.IsValidIndex(i)) Offset = SquadAsset->Positions[i];
		const FVector WorldOffset = SquadRot.RotateVector(Offset);
		const FVector SpawnLoc   = SquadLoc + WorldOffset;

		// Add the new SquadMember
		APawn* Member = World->SpawnActor<APawn>(MemberClass, SpawnLoc, SquadRot, Params);
		if (!Member) { SAFDEBUG_WARNING(FORMATSTR("InitSquad failed to spawn member %d.", i)); continue; }
		if (!SAFLibrary::IsPawnPtrValidSeinARTSSquadMember(Member)) { SAFDEBUG_WARNING(FORMATSTR("InitSquad spawned an invalid squad member %d, culling.", i)); Member->Destroy(); }
		AddSquadMember(Member, i == 0);

		USAFSquadMemberAsset* MemberAsset = SquadAsset->Members[i].LoadSynchronous();
		if (!MemberAsset) { SAFDEBUG_WARNING(FORMATSTR("InitSquad failed to load SquadMemberData for member %d", i)); continue; }

		ISAFSquadMemberInterface::Execute_InitSquadMember(Member, MemberAsset, this);
		SAFDEBUG_SUCCESS(FORMATSTR("Squadmember '%s' initialized. Squad '%s' now has '%d' members.", *Member->GetName(), *GetName(), SquadMembers.Num()));
	}

	bInitialized = true;
}

// Adds a member to the squad. If leader, inserts the actor at position 0.
void ASAFSquad::AddSquadMember_Implementation(APawn* Pawn, bool bIsNewLeader) {
	if (!SAFLibrary::IsPawnPtrValidSeinARTSSquadMember(Pawn)) { SAFDEBUG_WARNING("AddSquadMember aborted: invalid actor."); return; }

	UClass* MemberClass = SquadMemberClass.LoadSynchronous();
	if (!MemberClass || !Pawn->IsA(MemberClass)) { SAFDEBUG_WARNING("AddSquadMember aborted: actor class is not a SquadMember type."); return; }

	// If present (and not new leader)
	bool bPresent = SquadMembers.Contains(Pawn);
	if (bPresent && !bIsNewLeader) { SAFDEBUG_INFO("AddSquadMember actor is already present in squad. Function call discarded."); return; }
	else if (bPresent) { SAFDEBUG_SUCCESS(FORMATSTR("AddSquadMember: '%s' promoted to leader.", *Pawn->GetName())); SetSquadLeader(Pawn); return;}

	// Clear old squad then add unique
	ASAFSquad* OldSquad = Cast<ASAFSquad>(ISAFSquadMemberInterface::Execute_GetSquad(Pawn));
	if (SAFLibrary::IsActorPtrValidSeinARTSActor(OldSquad)) OldSquad->RemoveSquadMember(Pawn);

	// Add and set squad to this
	SquadMembers.AddUnique(Pawn);
	ISAFSquadMemberInterface::Execute_SetSquad(Pawn, this);
	if (bIsNewLeader) SetSquadLeader(Pawn);
	SAFDEBUG_SUCCESS(FORMATSTR("AddSquadMember: successfully added SquadMember '%s'.", *Pawn->GetName()));
}

// Removes a member from the squad, if present.
bool ASAFSquad::RemoveSquadMember_Implementation(APawn* Pawn) {
	if (!SAFLibrary::IsPawnPtrValidSeinARTSSquadMember(Pawn)) { SAFDEBUG_ERROR("RemoveSquadMember aborted: invalid actor."); return false; }
	if (!SquadMembers.Contains(Pawn)) { SAFDEBUG_INFO("RemoveSquadMember called, but actor is not present in squad. Function call discarded."); return false; }
	SquadMembers.Remove(Pawn);
	if (SAFLibrary::IsPawnPtrValidSeinARTSSquadMember(Pawn)) ISAFSquadMemberInterface::Execute_SetSquad(Pawn, this);
	if (SquadMembers.Num() == 0) CullSquad();
	return true;
}

// Sets the squad leader.
void ASAFSquad::SetSquadLeader_Implementation(APawn* InSquadLeader) {
	UClass* MemberClass = SquadMemberClass.LoadSynchronous();
	if (!MemberClass || !InSquadLeader->IsA(MemberClass)) { SAFDEBUG_WARNING("AddSquadMember aborted: actor class is not a SquadMember type."); return; }
	SquadLeader = InSquadLeader;
	ISAFUnitInterface::Execute_AttachToPawn(this, SquadLeader);
}

// Find the next leader for the squad when needed. (e.g. when SquadLeader dies)
APawn* ASAFSquad::FindNextSquadLeader_Implementation() {
	UClass* MemberClass = SquadMemberClass.LoadSynchronous();
	for (const TObjectPtr<APawn>& Member : SquadMembers) {
		if (!Member) continue;
		if (!MemberClass || !Member->IsA(MemberClass)) continue;
		if (IsValid(Member) && Member != AttachedPawn && !Member->IsActorBeingDestroyed()) return Member;
	}

	return nullptr;
}

// Returns whoever is at index 0 in the SquadMembers array. This can change dynamically
// as the squad moves around, as SquadMembers can reactively take up different positions
// in the array in response to navigation. Do not count on the same SquadMember always 
// being at position 0.
APawn* ASAFSquad::GetFrontSquadMember() const {
	if (SquadMembers.Num() <= 0) { 
		SAFDEBUG_WARNING("GetFrontSquadMember aborted: SquadMembers is empty.");
		return nullptr;
	}

	APawn* SquadFront = SquadMembers[0].Get();
	if (!SAFLibrary::IsPawnPtrValidSeinARTSSquadMember(SquadFront)) 
		SAFDEBUG_ERROR("GetFrontSquadMember found an invalid actor at position 0 of SquadMembers. This should never occur, please check your implementations. Returning nullptr.");
	return SquadFront;
}

// Call this to reinitialize the squad positions. This will force the squad
// to re-receive the current order it has (if any) so that pathing rebuilds
void ASAFSquad::ReinitPositions_Implementation(const TArray<FVector>& InPositions) {
	if (Positions.Num() != InPositions.Num()) SAFDEBUG_WARNING("ReinitPositions called with different number of positions. Some positions may be ZeroVector or unfilled.");

	int32 itrs = Positions.Num() > InPositions.Num() ? Positions.Num() : InPositions.Num();
	TArray<FVector> NewPositions;
	NewPositions.Reserve(itrs);
	for (int32 i = 0; i < itrs; i++) {
		FVector Position = InPositions.IsValidIndex(i) ? InPositions[i] : FVector::ZeroVector;
		NewPositions.Add(Position);
	}

	Positions = NewPositions;
}

// Makes the rear positions the front, and the front positions the rear. Useful when squad
// is issued a move order 'behind' itself to make movement flow more organically.
//
// Warning: this function modifies the live SquadMembers variable.
void ASAFSquad::InvertPositions_Implementation() {
	const int32 Count = SquadMembers.Num();
	if (Count <= 1) return;

	const int32 SplitIndex = Count / 2; // floor
	decltype(SquadMembers) NewOrder;
	NewOrder.Reserve(Count);

	for (int32 i = SplitIndex; i < Count; ++i) { NewOrder.Add(SquadMembers[i]); }
	for (int32 i = 0; i < SplitIndex; ++i) { NewOrder.Add(SquadMembers[i]); }

	SquadMembers = MoveTemp(NewOrder);
}

// Returns the positions adjusted an input vector, pivoted around a rotation. If the rotation
// provided is zero, function will get the LookAtRotation from squad to point, and use that.
// Use bTriggerInversion to tell the squad if it should actually invert its positions (for a
// move order) or if thequery is information-only.
TArray<FVector> ASAFSquad::GetPositionsAtPoint_Implementation(const FVector& Point, bool bTriggersInversion) {
	TArray<FVector> OutPositions;
	const FVector Location = GetActorLocation();
	const FVector PointWithFlattenedZ = FVector(Point.X, Point.Y, 0.f);
	const FVector LocationFlattenedZ = FVector(Location.X, Location.Y, 0.f);
	const FRotator Rotation = UKismetMathLibrary::FindLookAtRotation(LocationFlattenedZ, PointWithFlattenedZ);
	const float Dot = FVector::DotProduct(GetActorForwardVector(), Rotation.Vector());
	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	if (!NavSys) { SAFDEBUG_ERROR("GetPositionsAtPoint failed: no NavSys."); return Positions; }
	OutPositions.Reserve(Positions.Num());

	if (Dot < 0.f && bTriggersInversion) InvertPositions();
	for (const FVector& Position : Positions) {
		FVector RotatedPosition = Rotation.RotateVector(Position) + Point;
		FNavLocation NavProjection;
		const FVector QueryExtent(250.f, 250.f, 250.f);
		NavSys->ProjectPointToNavigation(RotatedPosition, NavProjection, QueryExtent);
		OutPositions.Add(NavProjection.Location);
	}

	return OutPositions;
}

// Returns an array of positions that is reactive to if there is cover near the point or not.
// Use bTriggersInversion to tell the squad if it should actually invert its positions (for a
// move order) or if the query is information-only.
TArray<FVector> ASAFSquad::GetCoverPositionsAtPoint_Implementation(const FVector& Point, bool bTriggersInversion) {
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
	MembersAsActors.Reserve(SquadMembers.Num());
	for (APawn* Pawn : SquadMembers) {
			MembersAsActors.Add(Pawn);
	}

	// If we hit a nav-blocking cover object, build the cover stacked up against the nearest edge (e.g. line up against a sandbag wall, etc...)
	// else build a clumped formation near the Point (e.g. gathering in a foxhole, etc...)
	FVector A, B, C, D;
	const USAFSquadAsset* SquadAsset = GetSquadAsset();
	if (NearestCoverCollider->GetCoverNavBounds(A, B, C, D, true)) 
		OutPositions = SAFCoverUtilities::BuildCoverPositionsAroundCoverBox(
			A, B, C, D, Point, 
			MembersAsActors, NavSys, bScattersInCover, bWrapsCover, CoverSearchRadius, 
			SquadAsset->CoverSpacingModifier, SquadAsset->CoverRowOffsetModifier, SquadAsset->LateralStaggerModifier
		); 

	else OutPositions = SAFCoverUtilities::BuildCoverPositionsAroundCoverPoint(
		Point, 
		MembersAsActors, NavSys, bScattersInCover, CoverSearchRadius, 
		SquadAsset->CoverSpacingModifier);

	return OutPositions;
}

// Safely deletes this squad actor. Does not delete the squad's SquadMembers. For that, use
// CullSquadAndMembers(). Leave Positions input blank to query the current Positions array 
// on the squad.
void ASAFSquad::CullSquad_Implementation() {
	if (SquadMembers.Num() <= 0 && HasAuthority()) {
		SAFDEBUG_INFO(FORMATSTR("Squad '%s' has been emptied: destroying squad.", *GetName()));
		Destroy();
	}
}

// Calls Destroy() on each SquadMember and then destroys itself. Leave Positions input blank 
// to query the current Positions array on the squad.
void ASAFSquad::CullSquadAndMembers_Implementation() {
	if (!HasAuthority()) return;
	if (SquadMembers.Num() <= 0) CullSquad();
	for (APawn* SquadMember : SquadMembers) if (SAFLibrary::IsPawnPtrValidSeinARTSSquadMember(SquadMember)) SquadMember->Destroy();
	SAFDEBUG_INFO(FORMATSTR("Squad '%s' has called Destroy() on its SquadMembers: destroying squad.", *GetName()));
	Destroy();
}

// Pawn Attachment Handling
// ========================================================================================================================================================
// Override the basic on destroy implementation from ASAFUnit
void ASAFSquad::OnAttachedPawnDestroyed_Implementation(AActor* DestroyedPawn) {
	if (!HasAuthority()) return;
	ISAFUnitInterface::Execute_DetachFromPawn(this );

	APawn* NewLeader = FindNextSquadLeader();
	if (!NewLeader) { SAFDEBUG_WARNING("OnAttachedPawnDestroyed: SAFSquad could not find next leader. Culling via CullSquadAndMembers."); CullSquadAndMembers(); }

	ISAFUnitInterface::Execute_AttachToPawn(this, NewLeader);
}

// Replication
// =================================================================================================
void ASAFSquad::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const {
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ASAFSquad, CurrentCoverDeprecated);
	DOREPLIFETIME(ASAFSquad, CoverSearchRadius);
	DOREPLIFETIME(ASAFSquad, SquadMembers);
	DOREPLIFETIME(ASAFSquad, SquadLeader);
	DOREPLIFETIME(ASAFSquad, Positions);
}

void ASAFSquad::OnRep_CurrentCoverDeprecated() {
	SAFDEBUG_INFO(TEXT("OnRep_CurrentCover triggered."));
}

void ASAFSquad::OnRep_SquadMembers() {
	SAFDEBUG_INFO(TEXT("OnRep_SquadMembers triggered."));
}

void ASAFSquad::OnRep_SquadLeader() {
	SAFDEBUG_INFO(TEXT("OnRep_SquadLeader triggered."));
}
