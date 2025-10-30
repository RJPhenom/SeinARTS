#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerStart.h"
#include "SAFPlayerStart.generated.h"

class ASAFPlayerState;

/**
 * ASAFPlayerStart
 *
 * Custom PlayerStart for the SeinARTS framework which carries a slot mapping
 * and an occupied flag. The SlotNumber can be used by the GameMode to
 * match players to spawn points.
 */
UCLASS(ClassGroup=(SeinARTS), Blueprintable, BlueprintType, meta=(DisplayName="SeinARTS Player Start"))
class SEINARTS_FRAMEWORK_RUNTIME_API ASAFPlayerStart : public APlayerStart {
	GENERATED_BODY()

public:

	ASAFPlayerStart(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
	virtual void BeginPlay() override;

	/** The slot ID this PlayerStart is assigned to. Use INDEX_NONE when unassigned. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SeinARTS|Spawn")
	int32 SlotNumber = INDEX_NONE;

	/** Returns true if the slot is currently occupied. */
	UFUNCTION(BlueprintCallable, Category="SeinARTS|Spawn")
	bool IsOccupied() const { return OccupyingPlayer != nullptr; }

	/** Returns the player occupying this slot, if any. */
	UFUNCTION(BlueprintCallable, Category="SeinARTS|Spawn")
	ASAFPlayerState* GetOccupyingPlayer() const { return OccupyingPlayer; }

    /** Fills the slot, marking it as occupied. */
    UFUNCTION(BlueprintCallable, Category="SeinARTS|Spawn")
    void FillSlot(ASAFPlayerState* NewOccupyingPlayer) { OccupyingPlayer = NewOccupyingPlayer; }

private:

	ASAFPlayerState* OccupyingPlayer = nullptr;

};

