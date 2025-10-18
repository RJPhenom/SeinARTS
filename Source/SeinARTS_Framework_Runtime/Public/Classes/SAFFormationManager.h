#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "GameFramework/Actor.h"
#include "Interfaces/SAFFormationInterface.h"
#include "Structs/SAFOrder.h"
#include "SAFFormationManager.generated.h"

class AActor;
class ASAFPlayerController;
class ASAFUnit;

/**
 * ASAFFormationManager
 * 
 * A default class for SeinARTS Framework formation handling. Formations are the
 * intermediary manager class between the SAFPlayerController (who sends orders)
 * and groups of units (who can receive and be ordered).
 */
UCLASS(ClassGroup=(SeinARTS), Blueprintable, BlueprintType, meta=(DisplayName="SeinARTS Formation Manager"))
class SEINARTS_FRAMEWORK_RUNTIME_API ASAFFormationManager : public AActor, 
	public ISAFFormationInterface {
	GENERATED_BODY()

public:

	ASAFFormationManager();

	// Formation Interface / API
	// ==================================================================================================
	virtual TArray<AActor*>       GetUnits_Implementation() const;
	virtual bool                  AddUnit_Implementation(AActor* Unit);
	virtual bool                  AddUnits_Implementation(const TArray<AActor*>& InUnits);
	virtual bool                  RemoveUnit_Implementation(AActor* Unit);
	virtual void                  CullFormation_Implementation();

	virtual bool                  ReceiveOrder_Implementation(bool bQueueMode, FSAFOrder Order);
	virtual bool                  ReceiveOrders_Implementation(bool bQueueMode, const TArray<FSAFOrder>& Orders);
	virtual bool                  ExecuteOrder_Implementation(FSAFOrder Order);
	virtual bool                  ExecuteNextOrder_Implementation();
	virtual bool                  CompleteOrder_Implementation();
	virtual bool                  CompleteOrderOnUnit_Implementation(AActor* Unit);
	virtual FGameplayTag          ResolveOrderTag_Implementation(const FSAFOrder& InOrder) const;

	virtual void                  HandleUnitOrderCompleted_Implementation(AActor* Unit, FSAFOrder Order);
	virtual void                  HandleUnitDestroyed_Implementation(AActor* Unit);

	/** Proxy Functions to bind delegates */
	UFUNCTION() void HandleUnitOrderCompletedProxy(AActor* Unit, FSAFOrder Order);
	UFUNCTION() void HandleUnitDestroyedProxy(AActor* Unit);

	/** Maintains the list of units this formation manager controls. */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category="SeinARTS", meta=(AllowPrivateAccess="true"))
	TArray<TObjectPtr<AActor>> Units;

	/** Queue of orders that have been issued to this formation. */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category="SeinARTS")
	TArray<FSAFOrder> OrderQueue;

	/** Contains units still pending completion on their current orders */
	UPROPERTY(VisibleInstanceOnly, Category="SeinARTS") 
	TSet<TWeakObjectPtr<AActor>> PendingUnits;

protected:

	virtual void BeginPlay() override;

	// Internal helpers for managing delegates
	// ==================================================================================================
	void BindUnitDelegates(AActor* Unit);
	void UnbindUnitDelegates(AActor* Unit);
	void UnbindAllUnitDelegates();

};
