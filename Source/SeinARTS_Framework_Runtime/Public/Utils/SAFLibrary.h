#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbilityTypes.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Resolvers/SAFAssetResolver.h"
#include "Structs/SAFOrder.h"
#include "SAFLibrary.generated.h"

class AActor;
class APawn;
class APlayerController;
class APlayerState;
class ASAFActor;
class ASAFUnit;
class ASquadMember;
class AVehiclePawn;
class ASAFPlayerController;
class ASAFPlayerState;
class USAFAsset;

/**
 * SAFLibrary
 *
 * Core global utilities for SeinARTS Framework.
 */
namespace SAFLibrary {

// Validation
// ==================================================================================================
bool SoftEqual(const TSoftObjectPtr<USAFAsset>& A, const TSoftObjectPtr<USAFAsset>& B);
template <typename T> bool SoftCastCheck(const TSoftObjectPtr<USAFAsset>& Asset);
bool IsPlayerControllerPtrValidSeinARTSPlayer(APlayerController* InPtr);
bool IsActorPtrValidSeinARTSActor(AActor* InPtr);
bool IsActorPtrValidSeinARTSUnit(AActor* InPtr);
bool IsPawnPtrValidSeinARTSPawn(APawn* InPtr);
bool IsActorPtrValidSeinARTSCover(AActor* InPtr);

// Template Implementations
// ==================================================================================================
template<typename T>
inline bool SAFLibrary::SoftCastCheck(const TSoftObjectPtr<USAFAsset>& Asset) {
	if (USAFAsset* ResolvedAsset = SAFAssetResolver::ResolveAsset(Asset)) 
		return Cast<T>(ResolvedAsset) != nullptr;
	else return false;
}

// Controller Helpers
// ==================================================================================================
bool IsSelectionFriendly(const TArray<AActor*>& Selection, const APlayerState* RequestingPlayer);
TArray<AActor*> FilterSelectionByPlayer(const TArray<AActor*>& InActors, const APlayerState* RequestingPlayer, bool bPrintDebugMessages = true);
FGameplayAbilityTargetDataHandle MakeTargetDataFromOrder(const FSAFOrder& Order, AActor* Source);

}


/**
 * USAFLibrary (BPFL)
 *
 * Blueprint wrapper for core SAFLibrary helpers.
 */
UCLASS()
class USAFLibrary : public UBlueprintFunctionLibrary {
		GENERATED_BODY()

public:

	// Validation
	// ==================================================================================================
	/** Soft-compare two soft refs (prefers loaded pointers, otherwise compares object paths). */
	UFUNCTION(BlueprintPure, Category="SeinARTS|Utilities")
	static bool SoftEqual(const TSoftObjectPtr<USAFAsset>& A, const TSoftObjectPtr<USAFAsset>& B) 
	{ return SAFLibrary::SoftEqual(A, B); }

	/** Checks if a PlayerController points to a valid SeinARTS player type. */
	UFUNCTION(BlueprintCallable, Category="SeinARTS|Utilities")
	static bool IsPlayerControllerValidSeinARTSPlayerController(APlayerController* PlayerController) 
	{ return SAFLibrary::IsPlayerControllerPtrValidSeinARTSPlayer(PlayerController); }

	/** Checks if an actor is valid, implements the unit interface and not marked for destruction. */
	UFUNCTION(BlueprintCallable, Category="SeinARTS|Utilities")
	static bool IsActorValidSeinARTSActor(AActor* Actor) { return SAFLibrary::IsActorPtrValidSeinARTSActor(Actor); }

	/** Checks if an actor is valid, implements the unit interface and not marked for destruction. */
	UFUNCTION(BlueprintCallable, Category="SeinARTS|Utilities")
	static bool IsActorValidSeinARTSUnit(AActor* Actor) { return SAFLibrary::IsActorPtrValidSeinARTSUnit(Actor); }

	/** Checks if a pawn is valid, implements the pawn interface and not marked for destruction. */
	UFUNCTION(BlueprintCallable, Category="SeinARTS|Utilities")
	static bool IsPawnValidSeinARTSPawn(APawn* Pawn) { return SAFLibrary::IsPawnPtrValidSeinARTSPawn(Pawn); }

	/** Checks if an actor is valid, implements the cover interface and not marked for destruction. */
	UFUNCTION(BlueprintCallable, Category="SeinARTS|Utilities")
	static bool IsActorValidCover(AActor* Actor) { return SAFLibrary::IsActorPtrValidSeinARTSCover(Actor); }

	// Controller Helpers
	// ==================================================================================================
	/** Decide if a candidate selection is friendly to the requesting player. */
	UFUNCTION(BlueprintPure, Category="SeinARTS|Selection")
	static bool IsSelectionFriendly(const TArray<AActor*>& Selection, APlayerState* RequestingPlayer) 
	{ return SAFLibrary::IsSelectionFriendly(Selection, RequestingPlayer); }

	/** Filter input actors by player friendliness. */
	UFUNCTION(BlueprintCallable, Category="SeinARTS|Selection")
	static TArray<AActor*> FilterSelectionByPlayer(const TArray<AActor*>& InActors, APlayerState* RequestingPlayer, bool bPrintDebugMessages) 
	{ return SAFLibrary::FilterSelectionByPlayer(InActors, RequestingPlayer, bPrintDebugMessages); }

	/** Construct FGameplayAbilityTargetDataHandle from order data (for GAS). */
	UFUNCTION(BlueprintCallable, Category="SeinARTS|Orders")
	static FGameplayAbilityTargetDataHandle MakeTargetDataFromOrder(const FSAFOrder& Order, AActor* Source) 
	{ return SAFLibrary::MakeTargetDataFromOrder(Order, Source); }

};
