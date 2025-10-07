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
 * and groups of actors (who receives them).
 */
UCLASS(ClassGroup=(SeinARTS), Blueprintable, BlueprintType, meta=(DisplayName="SeinARTS Formation Manager"))
class SEINARTS_FRAMEWORK_RUNTIME_API ASAFFormationManager : public AActor, 
  public ISAFFormationInterface {
  GENERATED_BODY()

public:

  ASAFFormationManager();

	// Formation Interface / API
	// ======================================================================================================
  virtual TArray<AActor*>       GetActors_Implementation() const;
  virtual bool                  AddActor_Implementation(AActor* Actor);
  virtual bool                  AddActors_Implementation(const TArray<AActor*>& InActors);
  virtual bool                  RemoveActor_Implementation(AActor* Actor);
  virtual void                  CullFormation_Implementation();

  virtual bool                  ReceiveOrder_Implementation(bool bQueueMode, FSAFOrder Order);
  virtual bool                  ReceiveOrders_Implementation(bool bQueueMode, const TArray<FSAFOrder>& Orders);
  virtual bool                  ExecuteOrder_Implementation(FSAFOrder Order);
  virtual bool                  ExecuteNextOrder_Implementation();
  virtual bool                  CompleteOrder_Implementation();
  virtual bool                  CompleteOrderOnActor_Implementation(AActor* Actor);
  virtual FGameplayTag          ResolveOrderTag_Implementation(const FSAFOrder& InOrder) const;
  
  virtual void                  HandleActorOrderCompleted_Implementation(AActor* Actor, FSAFOrder Order);
  virtual void                  HandleActorDestroyed_Implementation(AActor* Actor);

  // Proxy Functions to bind delegates
  UFUNCTION() void HandleActorOrderCompletedProxy(AActor* Actor, FSAFOrder Order);
  UFUNCTION() void HandleActorDestroyedProxy(AActor* Actor);

  // Maintains the list of actors this formation manager controls.
  UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category="SeinARTS|Formation", meta=(AllowPrivateAccess="true"))
  TArray<TObjectPtr<AActor>> Actors;

  // Queue of orders that have been issued to this formation.
  UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category="SeinARTS|Formation")
  TArray<FSAFOrder> OrderQueue;

  // Contains actors still pending completion on their current orders
  UPROPERTY(VisibleInstanceOnly, Category="SeinARTS|Formation") 
  TSet<TWeakObjectPtr<AActor>> PendingActors;

protected:

  virtual void BeginPlay() override;

  // Internal helpers for managing delegates
  // ==========================================
  void BindActorDelegates(AActor* Actor);
  void UnbindActorDelegates(AActor* Actor);
  void UnbindAllActorDelegates();

};
