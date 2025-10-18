#pragma once

#include "CoreMinimal.h"
#include "Assets/Units/SAFSquadAsset.h"
#include "Classes/SAFUnit.h"
#include "Enums/SAFCoverTypes.h"
#include "SAFSquad.generated.h"

class APawn;

/**
* SAFSquad 
*
* The abstract actor that represents the 'Unit' that is composed of
* SAFSquadMembers.
*
* Supporting squad-based infantry with cover tactics is a primary goal of the
* SeinARTS Framework. This class represents the 'Unit' from the player perspective
* with respect to squads of infantry. However, there can be 1-member squads, and 
* this might even be the optimal design used for some cases.
*/
UCLASS(ClassGroup=(SeinARTS), Blueprintable, BlueprintType, meta=(DisplayName="SeinARTS Squad Unit"))
class SEINARTS_FRAMEWORK_RUNTIME_API ASAFSquad : public ASAFUnit {

	GENERATED_BODY()

public:

	ASAFSquad();
	USAFSquadAsset* GetSquadAsset() { return Cast<USAFSquadAsset>(SAFAssetResolver::ResolveAsset(Asset)); }

	// Asset Interface Overrides
	// ==========================================================================================================================
	virtual void 		InitFromAsset_Implementation(USAFAsset* InAsset, ASAFPlayerState* InOwner, bool bReinitialize)	override;
	virtual float 		GetFormationSpacing_Implementation() const														override;

	// Squad Cover API
	// ==========================================================================================================================
	/** Tracks the highest state of current cover, for UI/UX purposes.
	(actual cover value for gameplay logic is tracked on individual squad members). */
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, ReplicatedUsing=OnRep_CurrentCoverDeprecated, Category="SeinARTS|Cover")
	ESAFCoverType CurrentCoverDeprecated = ESAFCoverType::None;

	/** The default query radius around a point that this squad's members will use to check for or find cover. 
	Also used when querying points for cover during navigation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category="SeinARTS|Cover")
	float CoverSearchRadiusDeprecated = 50.f;

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="SeinARTS|Cover")
	void UpdateCurrentCover(ESAFCoverType NewCover);
	virtual void UpdateCurrentCover_Implementation(ESAFCoverType NewCover) { CurrentCoverDeprecated = NewCover; }


	// Squad Properties / API
	// =====================================================================================================================================================
	/** Contains a reference to the SquadMember class you are using. Defaults to SAFSquadMember (C++). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="SeinARTS|Squad", meta=(MustImplement="/Script/SeinARTS_Framework_Runtime.SAFSquadMemberInterface"))
	TSoftClassPtr<APawn> SquadMemberClass;

	/** Contains references to the SquadMember instances of the actual SquadMembers. */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, ReplicatedUsing=OnRep_SquadMembers, Category="SeinARTS|Squad")
	TArray<TObjectPtr<APawn>> SquadMembers;

	/** Contains references to the SquadMember who is designated Squad Leader. By default, this will be the SquadMember
	at position 0 in the SquadData, but this could change at runtime if you have squad leadership mechanics. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing=OnRep_SquadLeader, Category="SeinARTS|Squad")
	TObjectPtr<APawn> SquadLeader;

	/** Ordered list of SquadMember positions in the squad instance.
	(Note: if you want your squad to be able to change formations, 
	you can call ReinitPositions() and pass in newly set positions). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category="SeinARTS|Squad")
	TArray<FVector> PositionsDeprecated;

	/** Wether or not this squad shoudl wrap around corners when finding cover positions.
	If off, SquadMembers will stack in flat rows along the edge of cover. Defaults to on. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category="SeinARTS|Squad")
	bool bWrapsCover = true;

	/** If active, adds a tiny amount of scatter to spacing while in cover for a more organic look and feel. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category="SeinARTS|Squad")
	bool bScattersInCover = true;

	/** Initializes the squad (generates members and positions) */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="SeinARTS|Squad")
	void InitSquad(USAFSquadAsset* SquadAsset);
	virtual void InitSquad_Implementation(USAFSquadAsset* SquadAsset);

	/** Adds a member to the squad. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="SeinARTS|Squad")
	void AddSquadMember(APawn* Pawn, bool bIsNewLeader = false);
	virtual void AddSquadMember_Implementation(APawn* Pawn, bool bIsNewLeader);

	/** Removes a member from the squad, if present. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="SeinARTS|Squad")
	bool RemoveSquadMember(APawn* Pawn);
	virtual bool RemoveSquadMember_Implementation(APawn* Pawn);

	/** Gets the squad leader. */
	UFUNCTION(BlueprintPure, Category="SeinARTS|Squad")
	APawn* GetSquadLeader() const { return SquadLeader.Get(); }

	/** Sets the squad leader. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="SeinARTS|Squad")
	void SetSquadLeader(APawn* InSquadLeader);
	virtual void SetSquadLeader_Implementation(APawn* InSquadLeader);

	/** Find the next leader for the squad when needed. (e.g. when SquadLeader dies) */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="SeinARTS|Squad")
	APawn* FindNextSquadLeader();
	virtual APawn* FindNextSquadLeader_Implementation();

	/** Returns whoever is at index 0 in the SquadMembers array. This can change dynamically
	as the squad moves around, as SquadMembers can reactively take up different positions
	in the array in response to navigation. Do not count on the same SquadMember always 
	being at position 0. */
	UFUNCTION(BlueprintPure, Category="SeinARTS|Squad")
	APawn* GetFrontSquadMember() const;

	/** Call this to reinitialize the squad positions. This will force the squad
	to re-receive the current order it has (if any) so that pathing rebuilds */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="SeinARTS|Squad")
	void ReinitPositions(const TArray<FVector>& InPositions);
	virtual void ReinitPositions_Implementation(const TArray<FVector>& InPositions);
	
	/** Makes the rear positions the front, and the front positions the rear. Useful when squad
	is issued a move order 'behind' itself to make movement flow more organically.
	
	Warning: this function modifies the live SquadMembers variable. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="SeinARTS|Squad")
	void InvertPositions();
	virtual void InvertPositions_Implementation();

	virtual TArray<FVector> GetPositionsAtPoint_Implementation(const FVector& Point, bool bTriggerInversion);
	TArray<FVector> GetCoverPositionsAtPoint_Implementation(const FVector& Point, bool bTriggerInversion);

	/** Safely deletes this squad actor. Does not delete the squad's SquadMembers. For that, use	CullSquadAndMembers(). */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="SeinARTS|Squad")
	void CullSquad();
	virtual void CullSquad_Implementation();

	/** Calls Destroy() on each SquadMember and then destroys itself. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="SeinARTS|Squad")
	void CullSquadAndMembers();
	virtual void CullSquadAndMembers_Implementation();

	// Attcahed Pawn Override
	// ==================================================================================================
	/** Override the basic on destroy implementation from ASAFUnit */
	virtual void OnAttachedPawnDestroyed_Implementation(AActor* DestroyedPawn) override;

protected:

	// Asset Interface Overrides
	// ==================================================================================================
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	UFUNCTION()	void OnRep_CurrentCoverDeprecated();
	UFUNCTION()	void OnRep_SquadMembers();
	UFUNCTION()	void OnRep_SquadLeader();

	/** Init flag to prevent duplicates on listen servers */
	bool bInitialized = false;

};