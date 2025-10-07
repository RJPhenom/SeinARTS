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
	TArray<AActor*> GetActors() const;

	/** Adds a actor to the formation. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="SeinARTS|Formation")
	bool AddActor(AActor* Actor);

	/** Adds a set of actors to the formation (calls AddActor on each). */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="SeinARTS|Formation")
	bool AddActors(const TArray<AActor*>& InActors);

	/** Removes a actor from the formation, culls the FormationManager if the actor 
	 * list is empty after removal. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="SeinARTS|Formation")
	bool RemoveActor(AActor* Actor);

	/** Handles self-culling on emptying out the formation. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="SeinARTS|Formation")
	void CullFormation();

	/** Handler for when a actor is destroyed while within a formation. */
	UFUNCTION(BlueprintNativeEvent, Category="SeinARTS|Formation")
	void HandleActorDestroyed(AActor* Actor);

	// Orders Intermediate Processing
	// ==================================================================================================
	/** Handles receipt of a single order. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="SeinARTS|Orders")
	bool ReceiveOrder(bool bQueueMode, FSAFOrder Order);

	/** Handles receipt of a set of orders. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="SeinARTS|Orders", meta=(AutoCreateRefTerm="Orders"))
	bool ReceiveOrders(bool bQueueMode, const TArray<FSAFOrder>& Orders);

	/** Executes a received order by dispatching individual orders to each actor. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="SeinARTS|Orders")
	bool ExecuteOrder(FSAFOrder Order);

	/** Executes the next order on the queue. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="SeinARTS|Orders")
	bool ExecuteNextOrder();

	/** Call when an order is completed (all actors are marked complete) to remove it 
	 * from the order queue and trigger the next order in the queue, if any. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="SeinARTS|Orders")
	bool CompleteOrder();

	/** Call to mark an actor complete for the current order at the top of the queue. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="SeinARTS|Orders")
	bool CompleteOrderOnActor(AActor* Actor);

	/** Resolves the tag to be associated with an order, if does not have one. */
	UFUNCTION(BlueprintNativeEvent, Category="SeinARTS|Orders")
	FGameplayTag ResolveOrderTag(const FSAFOrder& InOrder) const;

	/** Handler that is called when a formation is notified by a actor that it has completed 
	 * its pending order (when all actors finish notifying, the next order is executed). */
	UFUNCTION(BlueprintNativeEvent, Category="SeinARTS|Orders")
	void HandleActorOrderCompleted(AActor* Actor, FSAFOrder Order);

};
