#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Enums/SAFCoverTypes.h"
#include "SAFCoverInterface.generated.h"

UINTERFACE(Blueprintable)
class SEINARTS_FRAMEWORK_RUNTIME_API USAFCoverInterface : public UInterface { GENERATED_BODY() };

/**
 * SAFCoverInterface
 * 
 * Implement on actors (e.g., SAFUnit, SAFSquadMember) that can react to cover.
 * EnterCover/ExitCover are called by USAFCoverCollider on overlap begin/end.
 */
class SEINARTS_FRAMEWORK_RUNTIME_API ISAFCoverInterface {

	GENERATED_BODY()

public:

	/** Called when the actor enters a cover collider. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="SeinARTS|Cover")
	void EnterCover(AActor* CoverObject, ESAFCoverType CoverType);

	/** Called when the actor exits a cover collider. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="SeinARTS|Cover")
	void ExitCover(AActor* CoverObject, ESAFCoverType CoverType);

};
