#pragma once

#include "CoreMinimal.h"
#include "Structs/SAFResourceBundle.h"
#include "GameFramework/PlayerState.h"
#include "SAFPlayerState.generated.h"

/**
 * ASAFPlayerState
 *
 * Maintains player identity and state in a SeinARTS game. Used for ownership
 * and control of units, as well as team affiliation and other player-specific
 * data.
 */
UCLASS(ClassGroup=(SeinARTS), Blueprintable, BlueprintType, meta=(DisplayName="SeinARTS Player State"))
class SEINARTS_FRAMEWORK_RUNTIME_API ASAFPlayerState : public APlayerState {

	GENERATED_BODY()

public:

	ASAFPlayerState();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// Match Readiness
	// =======================================================================================
	/** Use to track if this player is ready on the server */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadWrite, Replicated, Category="SeinARTS")
	bool bIsReady = false;

	UFUNCTION(BlueprintCallable, Category="SeinARTS|Networking")
	void SetReady() { if (HasAuthority()) bIsReady = true; }

	// Team
	// =======================================================================================
	/** Use to track the team of this player */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadWrite, Replicated, Category="SeinARTS")
	int32 TeamID = 0;
	
	// Production
	// =======================================================================================
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Replicated, Category="SeinARTS")
	FSAFResourceBundle Resources;

	/** Gets this player's resources. */
	UFUNCTION(BlueprintCallable, Category="SeinARTS|Production")
	FSAFResourceBundle GetResources() const { return Resources; }

	/** Adds an individual resource, via index of the resource in the standard bundle. */
	UFUNCTION(BlueprintCallable, Category="SeinARTS|Production")
	void AddResource(int32 ResourceNumber, int32 Amount);

	/** Adds a resources bundle to this player's resources. */
	UFUNCTION(BlueprintCallable, Category="SeinARTS|Production")
	void AddResources(const FSAFResourceBundle& Delta);

	/** Returns true if current resources cover 'Cost' (no mutation). */
	UFUNCTION(BlueprintCallable, Category="SeinARTS|Production")
	bool CheckResourcesAvailable(const FSAFResourceBundle& Cost) const;

	/** Atomically deducts if affordable; returns true on success. */
	UFUNCTION(BlueprintCallable, Category="SeinARTS|Production")
	bool RequestResources(const FSAFResourceBundle& Cost);

	// Technology System
	// =======================================================================================
	/** Gets the player's technology component for research and technology state management. */
	UFUNCTION(BlueprintPure, Category="SeinARTS|Technology")
	class USAFPlayerTechnologyComponent* GetTechnologyComponent() const { return TechnologyComponent; }

protected:

	/** Technology component that manages this player's research and technology state. */
	UPROPERTY(BlueprintReadOnly, Replicated, Category="SeinARTS")
	TObjectPtr<class USAFPlayerTechnologyComponent> TechnologyComponent = nullptr;
	
};
