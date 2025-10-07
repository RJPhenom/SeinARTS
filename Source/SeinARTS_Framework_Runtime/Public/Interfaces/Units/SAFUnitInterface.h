#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h" 
#include "UObject/Interface.h"
#include "Structs/SAFOrder.h"
#include "Data/SAFUnitData.h"
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

		// Attached Pawn API (optional)
		// ==================================================================================================
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
