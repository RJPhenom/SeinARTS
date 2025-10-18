#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h" 
#include "Enums/SAFCoverTypes.h"
#include "UObject/Interface.h"
#include "Structs/SAFOrder.h"
#include "SAFUnitInterface.generated.h"

class ASAFActor;
class ASAFFormationManager;

// This class does not need to be modified.
UINTERFACE(Blueprintable)
class USAFUnitInterface : public UInterface { GENERATED_BODY() };

/**
 * SAFUnitInterface
 * 
 * The SAFUnitInterface is the primary interface for communicating with "units" in the SeinARTS Framework.
 * It provides methods for selection, orders, placement, and accessing unit data and ownership.
 */
class SEINARTS_FRAMEWORK_RUNTIME_API ISAFUnitInterface {

	GENERATED_BODY()

public:	

	// Attached Pawn API
	// ==================================================================================================
	/** Initializes the SAFUnit using pawn representation. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="SeinARTS|Unit Interface")
	void InitAsPawn();

	/** Initializes this Unit Actor using multiple pawns in SquadMode */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="SeinARTS|Unit Interface")
	void InitAsPawns();

	/** Attaches this Unit Actor (which does not stream movement via replication) to a pawn 
	 * which represents the moving unit (and has an AI controller and so on). */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="SeinARTS|Unit Interface")
	void AttachToPawn(APawn* Pawn);

	/** Detaches this Unit Actor from its pawn, if it is attached to one. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="SeinARTS|Unit Interface")
	void DetachFromPawn();

	/** Handles detachment on destruction of the pawn this Unit Actor is attached to. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="SeinARTS|Unit Interface")
	void OnAttachedPawnDestroyed(AActor* DestroyedPawn);

	// SquadMode API
	// ==================================================================================================
	/** Returns true if this unit is currently in SquadMode. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="SeinARTS|Unit Interface")
	bool GetSquadMode() const;

	/** Sets whether this unit is in SquadMode. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="SeinARTS|Unit Interface")
	void SetSquadMode(bool bInSquadMode);

	/** Adds a pawn to the squad. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="SeinARTS|Unit Interface")
	void AddPawnToSquad(APawn* Pawn, bool bIsNewLeader = false);

	/** Removes a pawn from the squad, if present. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="SeinARTS|Unit Interface")
	bool RemovePawnFromSquad(APawn* Pawn);

	/** Gets the squad leader. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="SeinARTS|Unit Interface")
	APawn* GetSquadLeader() const;

	/** Sets the squad leader. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="SeinARTS|Unit Interface")
	void SetSquadLeader(APawn* InSquadLeader);

	/** Find the next leader for the squad when needed. (e.g. when SquadLeader dies) */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="SeinARTS|Unit Interface")
	APawn* FindNextSquadLeader();

	/** Returns the front-most squad member, i.e. the one closest to the front of the formation. 
	 * 
	 * NOTE: this is not necessarily the squad leader, as the squad can dynamically change positions 
	 * in response to navigation.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="SeinARTS|Unit Interface")
	APawn* GetFrontmostPawnInSquad() const;

	/** Call this to reinitialize the squad positions. This will force the squad
	to re-receive the current order it has (if any) so that pathing rebuilds */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="SeinARTS|Unit Interface")
	void ReinitPositions(const TArray<FVector>& InPositions);
	
	/** Makes the rear positions the front, and the front positions the rear. Useful when squad
	is issued a move order 'behind' itself to make movement flow more organically.
	
	Warning: this function modifies the live SquadMembers variable. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="SeinARTS|Unit Interface")
	void InvertPositions();
	
	/** Returns the positions adjusted an input vector, pivoted around a rotation. If the rotation
	provided is zero, function will get the LookAtRotation from squad to point, and use that.
	Use bTriggerInversion to tell the squad if it should actually invert its positions (for a
	move order) or if thequery is information-only. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="SeinARTS|Unit Interface", meta=(AutoCreateRefTerm="Point,Rotation", CPP_Default_Point="0,0,0", CPP_Default_Rotation="0,0,0"))
	TArray<FVector> GetPositionsAtPoint(const FVector& Point, bool bTriggerInversion = true);

	/** Returns an array of positions that is reactive to if there is cover near the point or not.
	Use bTriggerInversion to tell the squad if it should actually invert its positions (for a
	move order) or if thequery is information-only. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="SeinARTS|Unit Interface", meta=(AutoCreateRefTerm="Point", CPP_Default_Point="0,0,0"))
	TArray<FVector> GetCoverPositionsAtPoint(const FVector& Point, bool bTriggerInversion = true);

	/** Safely deletes this squad actor. Does not delete the squad's pawns. For that,
	 * use CullSquadUnitAndPawns(). */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="SeinARTS|Unit Interface")
	void CullSquadUnit();

	/** Calls Destroy() on each pawn in this squad and then destroys the squad itself. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="SeinARTS|Unit Interface")
	void CullSquadUnitAndPawns();

	// Cover
	// ==================================================================================================
	/** Gets whether this unit uses cover or not. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="SeinARTS|Unit Interface")
	bool UsesCover() const;

	/** Gets the current cover type of this unit. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="SeinARTS|Unit Interface")
	ESAFCoverType GetCurrentCover() const;

	// Formations
	// ==================================================================================================
	/** Gets the current formation. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="SeinARTS|Unit Interface")
	ASAFFormationManager* GetFormation() const;
	
	/** Gets the spacing at the formation level: if there is a formation with multiple assets, 
	 * this determines the radius around this asset in which other assets in the formation 
	 * should not overlap so as to be appropriately spaced out at the destination point of 
	 * a move order or movement. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="SeinARTS|Unit Interface")
	float GetFormationSpacing() const;

	/** Sets the current formation. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="SeinARTS|Unit Interface")
	void SetFormation(ASAFFormationManager* InFormation);

	/** Sets the asset formation spacing, if any. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="SeinARTS|Unit Interface")
	void SetFormationSpacing(float InSpacing);

	// Orders API
	// ==================================================================================================
	/** Returns true if this unit is currently orderable. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="SeinARTS|Unit Interface")
	bool GetOrderable() const;

	/** Set the orderability state of this unit. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="SeinARTS|Unit Interface")
	void SetOrderable(bool bNewOrderable);

	/** Call to issue this unit an order. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="SeinARTS|Unit Interface")
	bool Order(FSAFOrder Order);

	/** Call to mark the current order as completed. Used to notify the formation about its 
	 * current order state. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="SeinARTS|Unit Interface")
	bool NotifyOrderCompleted();

	/** Call to retrieve all order tags this unit has assigned to it. (i.e. what orders this 
	 * unit can execute). */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="SeinARTS|Unit Interface")
	void GetOrderTags(UPARAM(ref) TArray<FGameplayTag>& OutTags) const;

};
