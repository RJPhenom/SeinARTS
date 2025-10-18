#include "Classes/SAFFormationManager.h"
#include "Classes/SAFActor.h"
#include "Interfaces/SAFActorInterface.h"
#include "Interfaces/SAFUnitInterface.h"
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
// Gets the units in the formation
TArray<AActor*> ASAFFormationManager::GetUnits_Implementation() const {
	TArray<AActor*> OutUnits; 
	OutUnits.Reserve(Units.Num());
	for (const TObjectPtr<AActor>& Unit : Units) 
		OutUnits.Add(Unit.Get());

	return OutUnits;
}

// Adds a single unit to the formation
bool ASAFFormationManager::AddUnit_Implementation(AActor* Unit) {
	if (!SAFLibrary::IsActorPtrValidSeinARTSUnit(Unit)) { SAFDEBUG_WARNING(FORMATSTR("Aborted adding unit '%s': Unit was invalid", Unit ? *Unit->GetName() : TEXT("nullptr"))); return false; }
	if (Units.Contains(Unit)) { SAFDEBUG_INFO(FORMATSTR("Aborted adding unit '%s': unit was already in the formation", Unit ? *Unit->GetName() : TEXT("nullptr"))); return false; }
	SAFDEBUG_SUCCESS(FORMATSTR("Formation '%s' added units '%s'.", *this->GetName(), *Unit->GetName()));
	Units.Add(Unit);

	// Ensure removal script runs on stale formation, if any
	if (ASAFFormationManager* OldFormation = ISAFUnitInterface::Execute_GetFormation(Unit))
		if (OldFormation->GetClass()->ImplementsInterface(USAFFormationInterface::StaticClass())) 
			ISAFFormationInterface::Execute_RemoveUnit(OldFormation, Unit);

	// Run setter, return success
	ISAFUnitInterface::Execute_SetFormation(Unit, this);
	return true;
}

// Adds a set of units to the formation
bool ASAFFormationManager::AddUnits_Implementation(const TArray<AActor*>& InUnits) {
	int32 Added = 0;
	for (AActor* Unit : InUnits) {
		if (SAFLibrary::IsActorPtrValidSeinARTSUnit(Unit)) {
			Added += ISAFFormationInterface::Execute_AddUnit(this, Unit) ? 1 : 0;
		} else SAFDEBUG_ERROR(FORMATSTR("Skipped actor '%s' while adding units: Unit was invalid", Unit ? *Unit->GetName() : TEXT("nullptr")));
	}

	if (Added > 0) SAFDEBUG_SUCCESS(FORMATSTR("Succeeded in adding %d units out of %d.", Added, InUnits.Num()));
	else SAFDEBUG_WARNING(FORMATSTR("Formation added 0 Units out of %d. There may have been an issue with InUnits param.", InUnits.Num()));
	return Added > 0;
}

// Removes a single unit from the formation
bool ASAFFormationManager::RemoveUnit_Implementation(AActor* Unit) {
	if (!SAFLibrary::IsActorPtrValidSeinARTSUnit(Unit)) { SAFDEBUG_ERROR("RemoveUnit aborted: invalid unit"); return false; }

	bool bRemoved = Units.Remove(Unit) > 0;
	if (bRemoved) {
		SAFDEBUG_SUCCESS(FORMATSTR("Succeeded in removing unit '%s'.", *Unit->GetName()));
		ISAFUnitInterface::Execute_SetFormation(Unit, nullptr);
		if (Units.Num() <= 0) ISAFFormationInterface::Execute_CullFormation(this);
	}
	
	return bRemoved;
}

// Handles self-culling on emptying out the formation
void ASAFFormationManager::CullFormation_Implementation() {
	if (Units.Num() == 0 && HasAuthority() && !IsActorBeingDestroyed()) {
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
		else SAFDEBUG_WARNING(FORMATSTR("Order received by formation '%s', but unit is not orderable. Execution failed.", *GetName()));
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

// Executes an order passed in directly by dispatching individual orders to each unit.
bool ASAFFormationManager::ExecuteOrder_Implementation(FSAFOrder Order) {
	if (Units.Num() == 0) {
		SAFDEBUG_INFO(FORMATSTR("ExecutedOrder aborted: formation '%s' has no units, formation will be culled.", *GetName()));
		ISAFFormationInterface::Execute_CullFormation(this);
		return false;
	}
	
	// Manage the pending orders
	UnbindAllUnitDelegates();
	PendingUnits.Reset();

	// Execute/dispatch
	int32 Executed = 0;
	for (const TObjectPtr<AActor>& Unit : Units) {
		if (SAFLibrary::IsActorPtrValidSeinARTSUnit(Unit)) {
			PendingUnits.Add(Unit);
			BindUnitDelegates(Unit);
			ISAFUnitInterface::Execute_Order(Unit, Order);
			++Executed;
		} else SAFDEBUG_WARNING(FORMATSTR("Formation '%s' skipped Order execution on an invalid unit.", *GetName()));
	}

	if (Executed > 0) SAFDEBUG_SUCCESS(FORMATSTR("Formation '%s' successfully executed %d order(s) on %d unit(s).", *GetName(), Executed, Units.Num()));
	else SAFDEBUG_WARNING(FORMATSTR("Formation executed 0 orders on %d unit(s). It is possible the units do not implement this order tag.", Units.Num()));
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

// Call to mark a unit in Units complete for the current order at the top of the stack.
bool ASAFFormationManager::CompleteOrderOnUnit_Implementation(AActor* Unit) {
	if (!SAFLibrary::IsActorPtrValidSeinARTSUnit(Unit)) { SAFDEBUG_ERROR("CompleteOrderOnUnit failed: invalid unit."); return false; }
	if (OrderQueue.Num() == 0) { SAFDEBUG_ERROR("CompleteOrderOnUnit failed: no active order."); return false; }
	return true;
}

// Resolves the tag to be associated with an order, if does not have one.
FGameplayTag ASAFFormationManager::ResolveOrderTag_Implementation(const FSAFOrder& InOrder) const {
	return FGameplayTag::RequestGameplayTag(TEXT("SeinARTS.Order.Move"));
}

// Handler that is called when a formation is notified by a unit that it has completed its
// pending order (when all Units finish notifying, the next order is executed).
void ASAFFormationManager::HandleUnitOrderCompleted_Implementation(AActor* Unit, FSAFOrder Order) {
	if (OrderQueue.Num() == 0) { SAFDEBUG_WARNING("Unit completed order but no active order is present."); return; }
	if (Order.Id != OrderQueue[0].Id) { SAFDEBUG_WARNING("Unit completed an order, but that order is not the active order (order may be stale or expired."); return; }

	// Handle the unit completion
	const bool bRemoved = PendingUnits.Remove(Unit) > 0;
	if (bRemoved) SAFDEBUG_SUCCESS(FORMATSTR("Formation '%s': Unit '%s' completed the active order. Remaining=%d", *GetName(), Unit ? *Unit->GetName() : TEXT("nullptr"), PendingUnits.Num()));
	else SAFDEBUG_WARNING("HandleUnitOrderCompleted called, but no unit removed from PendingUnits.");
	UnbindUnitDelegates(Unit);

	// Handle formation completion
	if (PendingUnits.Num() == 0) { ISAFFormationInterface::Execute_CompleteOrder(this); }
}

// Handler for when a unit is destroyed while within a formation
void ASAFFormationManager::HandleUnitDestroyed_Implementation(AActor* Unit) {
	if (OrderQueue.Num() == 0) { SAFDEBUG_INFO("Unit was destroyed, no active order is present."); return; }
	if (PendingUnits.Remove(Unit) > 0) {
		SAFDEBUG_WARNING(FORMATSTR("Formation '%s': Unit '%s' destroyed during active order. Remaining=%d", *GetName(), Unit ? *Unit->GetName() : TEXT("nullptr"), PendingUnits.Num()));
		UnbindUnitDelegates(Unit);
		if (PendingUnits.Num() == 0) ISAFFormationInterface::Execute_CompleteOrder(this);
	} else SAFDEBUG_WARNING("HandleUnitDestroyed called, but no unit removed from PendingUnits.");
}

// Proxy Functions for binding/unbinding delegates
// =========================================================================================================================================================================
void ASAFFormationManager::HandleUnitOrderCompletedProxy(AActor* Unit, FSAFOrder Order) { ISAFFormationInterface::Execute_HandleUnitOrderCompleted(this, Unit, Order); }
void ASAFFormationManager::HandleUnitDestroyedProxy(AActor* Unit) { ISAFFormationInterface::Execute_HandleUnitDestroyed(this, Unit); }

// Binding/Unbinding Unit Delegates Helpers
// ==============================================================================================================================================================
void ASAFFormationManager::BindUnitDelegates(AActor* Unit) {
	if (!SAFLibrary::IsActorPtrValidSeinARTSUnit(Unit)) { SAFDEBUG_ERROR("BindUnitDelegates aborted: invalid unit"); return; }
	ASAFActor* SAFActor = Cast<ASAFActor>(Unit);
	SAFActor->OnOrderCompleted.AddDynamic(this, &ASAFFormationManager::HandleUnitOrderCompletedProxy);
	SAFActor->OnDestroyed.AddDynamic(this, &ASAFFormationManager::HandleUnitDestroyedProxy);
}

void ASAFFormationManager::UnbindUnitDelegates(AActor* Unit) {
	if (!SAFLibrary::IsActorPtrValidSeinARTSUnit(Unit)) { SAFDEBUG_ERROR("UnbindUnitDelegates aborted: invalid unit"); return; }
	ASAFActor* SAFActor = Cast<ASAFActor>(Unit);
	SAFActor->OnOrderCompleted.RemoveDynamic(this, &ASAFFormationManager::HandleUnitOrderCompletedProxy);
	SAFActor->OnDestroyed.RemoveDynamic(this, &ASAFFormationManager::HandleUnitDestroyedProxy);
}

void ASAFFormationManager::UnbindAllUnitDelegates() {
	for (const TObjectPtr<AActor>& Unit : Units) if (ASAFActor* SAFActor = Cast<ASAFActor>(Unit.Get())) UnbindUnitDelegates(SAFActor);
}
	