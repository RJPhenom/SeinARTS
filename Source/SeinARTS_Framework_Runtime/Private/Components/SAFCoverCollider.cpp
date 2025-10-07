#include "Components/SAFCoverCollider.h"
#include "Interfaces/SAFCoverInterface.h"
#include "NavAreas/NavArea_Null.h"
#include "NavigationSystem.h"
#include "Utils/SAFLibrary.h"
#include "DrawDebugHelpers.h"

USAFCoverCollider::USAFCoverCollider() {
	PrimaryComponentTick.bCanEverTick = false;

	SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	SetCollisionObjectType(ECC_WorldDynamic);
	SetCollisionResponseToAllChannels(ECR_Ignore);
	SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	SetGenerateOverlapEvents(true);

	InitBoxExtent(FVector(150.f, 150.f, 120.f));
}

void USAFCoverCollider::OnRegister() {
	Super::OnRegister();
	OnComponentBeginOverlap.RemoveAll(this);
	OnComponentEndOverlap.RemoveAll(this);
	OnComponentBeginOverlap.AddDynamic(this, &USAFCoverCollider::HandleBeginOverlap);
	OnComponentEndOverlap.AddDynamic(this, &USAFCoverCollider::HandleEndOverlap);
}

// Returns the first navmesh-blocking mesh component on the owner (if any).
UMeshComponent* USAFCoverCollider::GetCoverMesh() const {
	const AActor* OwnerActor = GetOwner();
	if (!IsValid(OwnerActor)) return nullptr;

	TArray<UMeshComponent*> Meshes;
	OwnerActor->GetComponents<UMeshComponent>(Meshes);
	for (UMeshComponent* Mesh : Meshes) {
		if (!IsValid(Mesh)) continue;
		if (Mesh->CanEverAffectNavigation() 
		&& Mesh->IsNavigationRelevant() 
		&& Mesh->GetCollisionEnabled() != ECollisionEnabled::NoCollision) 
		return Mesh;
	}

	return nullptr;
}


// Returns 2D world positions (A,B,C,D) defining the nav bounds of this cover's parent object. 
// If the owner has a nav-blocking mesh, its bounds are used; otherwise, falls back to origin.
bool USAFCoverCollider::GetCoverNavBounds(FVector& OutA, FVector& OutB, FVector& OutC, FVector& OutD, bool bShowDebugBox) const {
	const AActor* OwnerActor = GetOwner();
	if (!IsValid(OwnerActor)) { OutA = OutB = OutC = OutD = FVector::ZeroVector; return false; }

	const UPrimitiveComponent* Mesh = Cast<UPrimitiveComponent>(GetCoverMesh());
	if (!IsValid(Mesh)) { const FVector Origin = GetComponentLocation(); OutA = OutB = OutC = OutD = Origin; return false; }

	// Get local-space bounds
	FBoxSphereBounds LocalBounds = Mesh->CalcBounds(FTransform::Identity);
	const FVector LocalCenter = LocalBounds.Origin;
	const FVector LocalExtent = LocalBounds.BoxExtent;

	// World transform (rotation & scale included)
	const FTransform& WT = Mesh->GetComponentTransform();
	const FVector WorldCenter3D = WT.TransformPosition(LocalCenter);

	// Flatten to ground
	const FVector Center = FVector(WorldCenter3D.X, WorldCenter3D.Y, 0.f);

	// Only yaw matters for 2D rectangle
	const float Yaw = WT.GetRotation().Rotator().Yaw;
	const FRotator YawRot(0.f, Yaw, 0.f);
	const FVector Forward = YawRot.Vector();
	const FVector Right   = FVector::CrossProduct(FVector::UpVector, Forward).GetSafeNormal();

	// Scaled extents
	const FVector WorldExtent = WT.GetScale3D().GetAbs() * LocalExtent;
	const FVector HalfF = Forward * WorldExtent.X;
	const FVector HalfR = Right   * WorldExtent.Y;

	// Output corners
	OutA = Center - HalfF - HalfR;
	OutB = Center + HalfF - HalfR;
	OutC = Center + HalfF + HalfR;
	OutD = Center - HalfF + HalfR;

	if (bShowDebugBox) DrawDebugBox(GetWorld(), Center, FVector(WorldExtent.X + 5.f, WorldExtent.Y + 5.f, 55.f), FQuat(YawRot), FColor::Turquoise, false, 0.f, 0, 2.f);
	return true;
}

// Handler function for when a new actor enters this cover collider (by default called the EntersCover on that object, if it implements the SAFCoverInterface).
void USAFCoverCollider::HandleBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult) {
	if (OtherActor == GetOwner()) return;
	if (!SAFLibrary::IsActorPtrValidSeinARTSActor(OtherActor)) return;
	if (!OtherActor->GetClass()->ImplementsInterface(USAFCoverInterface::StaticClass())) return;
	ISAFCoverInterface::Execute_EnterCover(OtherActor, GetOwner(), CoverType);
}

// Handler function for when a new actor exits this cover collider (by default called the ExitCover on that object, if it implements the SAFCoverInterface).
void USAFCoverCollider::HandleEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex) {
	if (OtherActor == GetOwner()) return;
	if (!SAFLibrary::IsActorPtrValidSeinARTSActor(OtherActor)) return;
	if (!OtherActor->GetClass()->ImplementsInterface(USAFCoverInterface::StaticClass())) return;
	ISAFCoverInterface::Execute_ExitCover(OtherActor, GetOwner(), CoverType);
}
