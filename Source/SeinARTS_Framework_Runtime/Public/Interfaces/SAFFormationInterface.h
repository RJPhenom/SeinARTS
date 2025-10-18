#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "GameplayTagContainer.h"
#include "Structs/SAFOrder.h"
#include "SAFFormationInterface.generated.h"

class ASAFActor;

UINTERFACE(Blueprintable)
class USAFFormationInterface : public UInterface { GENERATED_BODY() };

/**
 * SAFFormationInterface
 * 
 * The SAFFormationInterface is the primary interface for the formation layer, which takes orders
 * (or other input) from a controller and then manages the dispatch of those orders to the actual
 * actors they are meant for. 
 */
class SEINARTS_FRAMEWORK_RUNTIME_API ISAFFormationInterface {
	GENERATED_BODY()

public:

	// Formation API
	// ==================================================================================================
	/** Accessors (read-only property exposure). */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="SeinARTS|Formation")
	TArray<AActor*> GetUnits() const;

	/** Adds a unit to the formation. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="SeinARTS|Formation")
	bool AddUnit(AActor* Unit);

	/** Adds a set of units to the formation (calls AddUnit on each). */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="SeinARTS|Formation")
	bool AddUnits(const TArray<AActor*>& InUnits);

	/** Removes a unit from the formation, culls the FormationManager if the unit
	 * list is empty after removal. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="SeinARTS|Formation")
	bool RemoveUnit(AActor* Unit);

	/** Handles self-culling on emptying out the formation. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="SeinARTS|Formation")
	void CullFormation();

	/** Handler for when a unit is destroyed while within a formation. */
	UFUNCTION(BlueprintNativeEvent, Category="SeinARTS|Formation")
	void HandleUnitDestroyed(AActor* Unit);

	// Orders Intermediate Processing
	// ==================================================================================================
	/** Handles receipt of a single order. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="SeinARTS|Orders")
	bool ReceiveOrder(bool bQueueMode, FSAFOrder Order);

	/** Handles receipt of a set of orders. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="SeinARTS|Orders", meta=(AutoCreateRefTerm="Orders"))
	bool ReceiveOrders(bool bQueueMode, const TArray<FSAFOrder>& Orders);

	/** Executes a received order by dispatching individual orders to each unit. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="SeinARTS|Orders")
	bool ExecuteOrder(FSAFOrder Order);

	/** Executes the next order on the queue. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="SeinARTS|Orders")
	bool ExecuteNextOrder();

	/** Call when an order is completed (all units are marked complete) to remove it
	 * from the order queue and trigger the next order in the queue, if any. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="SeinARTS|Orders")
	bool CompleteOrder();

	/** Call to mark a unit complete for the current order at the top of the queue. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="SeinARTS|Orders")
	bool CompleteOrderOnUnit(AActor* Unit);

	/** Resolves the tag to be associated with an order, if it does not have one. */
	UFUNCTION(BlueprintNativeEvent, Category="SeinARTS|Orders")
	FGameplayTag ResolveOrderTag(const FSAFOrder& InOrder) const;

	/** Handler that is called when a formation is notified by a unit that it has completed
	 * its pending order (when all units finish notifying, the next order is executed). */
	UFUNCTION(BlueprintNativeEvent, Category="SeinARTS|Orders")
	void HandleUnitOrderCompleted(AActor* Unit, FSAFOrder Order);

};
