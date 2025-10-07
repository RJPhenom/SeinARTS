#include "Classes/SAFFormationManager.h"
#include "Classes/SAFActor.h"
#include "Interfaces/SAFActorInterface.h"
#include "Interfaces/Units/SAFUnitInterface.h"
#include "Utils/SAFLibrary.h"
#include "Engine/World.h"
#include "GameplayTagContainer.h"
#include "Debug/SAFDebugTool.h"

ASAFFormationManager::ASAFFormationManager() {
	PrimaryActorTick.bCanEverTick = false;
}

void ASAFFormationManager::BeginPlay() {
	Super::BeginPlay();
}

// Formation Interface Overrides
// ==============================================================================================================================================================
// Gets the actors in the formation
TArray<AActor*> ASAFFormationManager::GetActors_Implementation() const {
	TArray<AActor*> OutActors; 
	OutActors.Reserve(Actors.Num());
	for (const TObjectPtr<AActor>& Actor : Actors) 
		OutActors.Add(Actor.Get());

	return OutActors;
}

// Adds a single actors to the formation
bool ASAFFormationManager::AddActor_Implementation(AActor* Actor) {
	if (!SAFLibrary::IsActorPtrValidSeinARTSUnit(Actor)) { SAFDEBUG_WARNING(FORMATSTR("Aborted adding actor '%s': Actor was invalid", Actor ? *Actor->GetName() : TEXT("nullptr"))); return false; }
	if (Actors.Contains(Actor)) { SAFDEBUG_INFO(FORMATSTR("Aborted adding actor '%s': actor was already in the formation", Actor ? *Actor->GetName() : TEXT("nullptr"))); return false; }
	SAFDEBUG_SUCCESS(FORMATSTR("Formation '%s' added actors '%s'.", *this->GetName(), *Actor->GetName()));
	Actors.Add(Actor);

	// Ensure removal script runs on stale formation, if any
	if (ASAFFormationManager* OldFormation = ISAFUnitInterface::Execute_GetFormation(Actor))
		if (OldFormation->GetClass()->ImplementsInterface(USAFFormationInterface::StaticClass())) 
			ISAFFormationInterface::Execute_RemoveActor(OldFormation, Actor);

	// Run setter, return success
	ISAFUnitInterface::Execute_SetFormation(Actor, this);
	return true;
}

// Adds a set of Actors to the formation
bool ASAFFormationManager::AddActors_Implementation(const TArray<AActor*>& InActors) {
	int32 Added = 0;
	for (AActor* Actor : InActors) {
		if (SAFLibrary::IsActorPtrValidSeinARTSUnit(Actor)) {
			Added += ISAFFormationInterface::Execute_AddActor(this, Actor) ? 1 : 0;
		} else SAFDEBUG_ERROR(FORMATSTR("Skipped actor '%s' while adding actors: Actor was invalid", Actor ? *Actor->GetName() : TEXT("nullptr")));
	}

	if (Added > 0) SAFDEBUG_SUCCESS(FORMATSTR("Succeeded in adding %d actors out of %d.", Added, InActors.Num()));
	else SAFDEBUG_WARNING(FORMATSTR("Formation added 0 Actors out of %d. There may have been an issue with InActors param.", InActors.Num()));
	return Added > 0;
}

// Removes a single actor from the formation
bool ASAFFormationManager::RemoveActor_Implementation(AActor* Actor) {
	if (!SAFLibrary::IsActorPtrValidSeinARTSUnit(Actor)) { SAFDEBUG_ERROR("RemoveActor aborted: invalid actor"); return false; }

	bool bRemoved = Actors.Remove(Actor) > 0;
	if (bRemoved) {
		SAFDEBUG_SUCCESS(FORMATSTR("Succeeded in removing actor '%s'.", *Actor->GetName()));
		ISAFUnitInterface::Execute_SetFormation(Actor, nullptr);
		if (Actors.Num() <= 0) ISAFFormationInterface::Execute_CullFormation(this);
	}
	
	return bRemoved;
}

// Handles self-culling on emptying out the formaiton
void ASAFFormationManager::CullFormation_Implementation() {
	if (Actors.Num() == 0 && HasAuthority() && !IsActorBeingDestroyed()) {
		SAFDEBUG_INFO(FORMATSTR("Formation '%s' has been emptied: culling formation.", *GetName()));
		Destroy();
	}
}

// Handles receipt of a single order
bool ASAFFormationManager::ReceiveOrder_Implementation(bool bQueueMode, FSAFOrder Order) {
	if (!Order.Tag.IsValid()) Order.Tag = ISAFFormationInterface::Execute_ResolveOrderTag(this, Order);
	if (!Order.Tag.IsValid()) { SAFDEBUG_ERROR(FORMATSTR("ReceiveOrder failed: formation '%s' failed to resolve OrderTag.", *GetName())); return false; }

	if (!bQueueMode) {
		OrderQueue.Reset();
		OrderQueue.Add(Order);
		bool bExecuted = ISAFFormationInterface::Execute_ExecuteOrder(this, Order);
		if (bExecuted) SAFDEBUG_SUCCESS(FORMATSTR("Order successfully received by formation '%s'!", *GetName()));
		else SAFDEBUG_WARNING(FORMATSTR("Order received by formation '%s', but actor is not orderable. Execution failed.", *GetName()));
		return true;
	} else {
		OrderQueue.Add(Order);
		SAFDEBUG_SUCCESS(FORMATSTR("Order successfully received by formation '%s', order added to queue.", *GetName()));
		return true;
	}
}

// Handles receipt of a set of orders
bool ASAFFormationManager::ReceiveOrders_Implementation(bool bQueueMode, const TArray<FSAFOrder>& Orders) {
	int32 Received = 0;
	for (const FSAFOrder& Order : Orders) {
		Received += ISAFFormationInterface::Execute_ReceiveOrder(this, bQueueMode, Order) ? 1 : 0;
	}

	if (Received > 0) SAFDEBUG_SUCCESS(FORMATSTR("Formation '%s' successfully received %d orders out of %d.", *GetName(), Received, Orders.Num()));
	else SAFDEBUG_WARNING(FORMATSTR("Formation received 0 orders out of %d. There may have been an issue with the Orders param.", Orders.Num()));
	return Received > 0;
}

// Executes an order passed in directly by dispatching individual orders to each actor.
bool ASAFFormationManager::ExecuteOrder_Implementation(FSAFOrder Order) {
	if (Actors.Num() == 0) {
		SAFDEBUG_INFO(FORMATSTR("ExecutedOrder aborted: formation '%s' has no actors, formation will be culled.", *GetName()));
		ISAFFormationInterface::Execute_CullFormation(this);
		return false;
	}
	
	// Manage the pending orders
	UnbindAllActorDelegates();
	PendingActors.Reset();

	// Execute/dispatch
	int32 Executed = 0;
	for (const TObjectPtr<AActor>& Actor : Actors) {
		if (SAFLibrary::IsActorPtrValidSeinARTSUnit(Actor)) {
			PendingActors.Add(Actor);
			BindActorDelegates(Actor);
			ISAFUnitInterface::Execute_Order(Actor, Order);
			++Executed;
		} else SAFDEBUG_WARNING(FORMATSTR("Formation '%s' skipped Order execution on an invalid actor.", *GetName()));
	}

	if (Executed > 0) SAFDEBUG_SUCCESS(FORMATSTR("Formation '%s' successfully executed %d order(s) on %d actor(s).", *GetName(), Executed, Actors.Num()));
	else SAFDEBUG_WARNING(FORMATSTR("Formation executed 0 orders on %d actor(s). It is possible the actors do not implement this order tag.", Actors.Num()));
	return Executed > 0;
}

// Executes the next order on the queue
bool ASAFFormationManager::ExecuteNextOrder_Implementation() {
	if (OrderQueue.Num() == 0) { SAFDEBUG_WARNING("ExecuteNextOrder aborted: OrderQueue empty."); return false; }
	return ISAFFormationInterface::Execute_ExecuteOrder(this, OrderQueue[0]);
}

// Call when an order is completed to remove it from the order stack and trigger
// the next order in the stack, if any.
bool ASAFFormationManager::CompleteOrder_Implementation() {
	if (OrderQueue.Num() <= 0) { SAFDEBUG_ERROR("CompleteOrder failed: OrderQueue is already empty."); return false; }
	OrderQueue.RemoveAt(0, 1);
	SAFDEBUG_SUCCESS(FORMATSTR("Formation '%s' has completed its order.", *GetName()));
	return true;
}

// Call to mark an actor in Actors complete for the current order at the top of the stack.
bool ASAFFormationManager::CompleteOrderOnActor_Implementation(AActor* Actor) {
	if (!SAFLibrary::IsActorPtrValidSeinARTSUnit(Actor)) { SAFDEBUG_ERROR("CompleteOrderOnActor failed: invalid actor."); return false; }
	if (OrderQueue.Num() == 0) { SAFDEBUG_ERROR("CompleteOrderOnActor failed: no active order."); return false; }
	return true;
}

// Resolves the tag to be associated with an order, if does not have one.
FGameplayTag ASAFFormationManager::ResolveOrderTag_Implementation(const FSAFOrder& InOrder) const {
	return FGameplayTag::RequestGameplayTag(TEXT("SeinARTS.Order.Move"));
}

// Handler that is called when a formation is notified by a actor that it has completed its
// pending order (when all Actors finish notifying, the next order is executed).
void ASAFFormationManager::HandleActorOrderCompleted_Implementation(AActor* Actor, FSAFOrder Order) {
	if (OrderQueue.Num() == 0) { SAFDEBUG_WARNING("Actor completed order but no active order is present."); return; }
	if (Order.Id != OrderQueue[0].Id) { SAFDEBUG_WARNING("Actor completed an order, but that order is not the active order (order may be stale or expired."); return; }

	// Handle the actor completion
	const bool bRemoved = PendingActors.Remove(Actor) > 0;
	if (bRemoved) SAFDEBUG_SUCCESS(FORMATSTR("Formation '%s': Actor '%s' completed the active order. Remaining=%d", *GetName(), Actor ? *Actor->GetName() : TEXT("nullptr"), PendingActors.Num()));
	else SAFDEBUG_WARNING("HandleActorOrderCompleted called, but no actor removed from PendingActors.");
	UnbindActorDelegates(Actor);

	// Handle formation completion
	if (PendingActors.Num() == 0) { ISAFFormationInterface::Execute_CompleteOrder(this); }
}

// Handler for when a actor is destroyed while within a formation
void ASAFFormationManager::HandleActorDestroyed_Implementation(AActor* Actor) {
	if (OrderQueue.Num() == 0) { SAFDEBUG_INFO("Actor was destroyed, no active order is present."); return; }
	if (PendingActors.Remove(Actor) > 0) {
		SAFDEBUG_WARNING(FORMATSTR("Formation '%s': Actor '%s' destroyed during active order. Remaining=%d", *GetName(), Actor ? *Actor->GetName() : TEXT("nullptr"), PendingActors.Num()));
		UnbindActorDelegates(Actor);
		if (PendingActors.Num() == 0) ISAFFormationInterface::Execute_CompleteOrder(this);
	} else SAFDEBUG_WARNING("HandleActorDestroyed called, but no actor removed from PendingActors.");
}

// Proxy Functions for binding/unbinding delegates
// =========================================================================================================================================================================
void ASAFFormationManager::HandleActorOrderCompletedProxy(AActor* Actor, FSAFOrder Order) { ISAFFormationInterface::Execute_HandleActorOrderCompleted(this, Actor, Order); }
void ASAFFormationManager::HandleActorDestroyedProxy(AActor* Actor) { ISAFFormationInterface::Execute_HandleActorDestroyed(this, Actor); }

// Binding/Unbinding Actor Delegates Helpers
// ==============================================================================================================================================================
void ASAFFormationManager::BindActorDelegates(AActor* Actor) {
	if (!SAFLibrary::IsActorPtrValidSeinARTSUnit(Actor)) { SAFDEBUG_ERROR("BindActorDelegates aborted: invalid actor"); return; }
	ASAFActor* SAFActor = Cast<ASAFActor>(Actor);
	SAFActor->OnOrderCompleted.AddDynamic(this, &ASAFFormationManager::HandleActorOrderCompletedProxy);
	SAFActor->OnDestroyed.AddDynamic(this, &ASAFFormationManager::HandleActorDestroyedProxy);
}

void ASAFFormationManager::UnbindActorDelegates(AActor* Actor) {
	if (!SAFLibrary::IsActorPtrValidSeinARTSUnit(Actor)) { SAFDEBUG_ERROR("UnbindActorDelegates aborted: invalid actor"); return; }
	ASAFActor* SAFActor = Cast<ASAFActor>(Actor);
	SAFActor->OnOrderCompleted.RemoveDynamic(this, &ASAFFormationManager::HandleActorOrderCompletedProxy);
	SAFActor->OnDestroyed.RemoveDynamic(this, &ASAFFormationManager::HandleActorDestroyedProxy);
}

void ASAFFormationManager::UnbindAllActorDelegates() {
	for (const TObjectPtr<AActor>& Actor : Actors) if (ASAFActor* SAFActor = Cast<ASAFActor>(Actor.Get())) UnbindActorDelegates(SAFActor);
}
