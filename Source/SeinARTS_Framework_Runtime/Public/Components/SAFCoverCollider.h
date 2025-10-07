#pragma once

#include "CoreMinimal.h"
#include "Components/BoxComponent.h"
#include "Enums/SAFCoverTypes.h"
#include "NavModifierComponent.h"
#include "SAFCoverCollider.generated.h"
/**
 * SAFCoverCollider 
 * 
 * The collider which enables cover, if using the SeinARTS Framework cover system.
 * Place on any actor to mark an area that provides cover. Default shape is a box; 
 * resize in editor. Overlap events can be used to apply cover/remove cover.
 */
UCLASS(ClassGroup=(SeinARTS), Blueprintable, BlueprintType, meta=(BlueprintSpawnableComponent, DisplayName="Cover Collider"))
class SEINARTS_FRAMEWORK_RUNTIME_API USAFCoverCollider : public UBoxComponent {

	GENERATED_BODY()

public:

	USAFCoverCollider();

	// The class of cover this collider provides. Defaults to neutral.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SeinARTS|Cover")
	ESAFCoverType CoverType = ESAFCoverType::Neutral;
	
	// Returns the first navmesh-blocking mesh component on the owner (if any).
	UFUNCTION(BlueprintCallable, Category="SeinARTS|Cover")
	UMeshComponent* GetCoverMesh() const;

	// Returns 2D world positions (A,B,C,D) defining the nav bounds of this cover object. 
	// If the owner has a nav-blocking mesh, its bounds are used; otherwise, falls back to this collider’s box.
	// A->B is always aligned with the forward vector of the owning actor.
	UFUNCTION(BlueprintCallable, Category="SeinARTS|Cover")
	bool GetCoverNavBounds(FVector& OutA, FVector& OutB, FVector& OutC, FVector& OutD, bool bShowDebugBox) const;

protected:

	virtual void OnRegister() override;

	UFUNCTION() // Handler function for when a new actor enters this cover collider (by default called the EnterCover on that object, if it implements the SAFCoverInterface).
	void HandleBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UFUNCTION() // Handler function for when a new actor exits this cover collider (by default called the ExitCover on that object, if it implements the SAFCoverInterface).
	void HandleEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex);

};