// SAFAsset.h
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "SAFAsset.generated.h"

class UTexture2D;
class ASAFActor;

/**
 * SAFAsset
 *
 * Base primary data asset for SeinARTS content. Holds identity info common to all assets.
 */
UCLASS(ClassGroup=(SeinARTS), Blueprintable, BlueprintType, meta=(DisplayName="SeinARTS Asset"))
class SEINARTS_FRAMEWORK_RUNTIME_API USAFAsset : public UPrimaryDataAsset {

	GENERATED_BODY()

public:

	USAFAsset(const FObjectInitializer& ObjectInitializer);

	static const FPrimaryAssetType PrimaryAssetType;
	virtual FPrimaryAssetId GetPrimaryAssetId() const override {
		return FPrimaryAssetId(PrimaryAssetType, GetFName());
	}

	// Asset Identity
	// ==================================================================================================
	/** The display name for UI, e.g. "Rifleman". */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Identity")
	FText DisplayName;

	/** Tooltip text. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Identity", meta=(MultiLine="true"))
	FText Tooltip;

	/** Icon (small). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Identity")
	TSoftObjectPtr<UTexture2D> Icon;

	/** Portrait (large). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Identity")
	TSoftObjectPtr<UTexture2D> Portait;

	/** Gameplay tags associated with the identification of this asset. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Identity")
	FGameplayTagContainer Tags;

	/** What class should this asset spawn as? SeinARTS Framework assets seed runtime
	instances (actors), this setting tells the framework init function which class
	this asset seeds. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Identity")
	TSoftClassPtr<AActor> InstanceClass;

	// Fallback Spacing
	// ==================================================================================================
	/** Sets the formation spacing fallback, if this is needed and this is not a SAFUnitAsset. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Fallback Spacing")
	float FallbackSpacing = 50.f;

protected:

	virtual void PostLoad() override;

};
