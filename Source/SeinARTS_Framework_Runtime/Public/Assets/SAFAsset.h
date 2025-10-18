// SAFAsset.h
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "SAFAsset.generated.h"

class UTexture2D;

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
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Asset Identity")
	FText DisplayName;

	/** Tooltip text. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Asset Identity", meta=(MultiLine="true"))
	FText Tooltip;

	/** Icon (small). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Asset Identity")
	TSoftObjectPtr<UTexture2D> Icon;

	/** Portrait (large). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Asset Identity")
	TSoftObjectPtr<UTexture2D> Portait;

	/** Gameplay tags associated with the identification of this asset. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Asset Identity")
	FGameplayTagContainer Tags;

	/** Set the class that this asset should use when spawned as an instance in the world.
	 * 
	 * By default the SeinARTS Framework assets seed SAFActor runtime instances, but this setting 
	 * allows for custom overrides (you will still be expected to implement the necessary actor 
	 * interface and logic in your custom class). 
	 * 
	 * For SeinARTS Unit Assets, this defaults to the SeinARTS Unit class.*/
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Asset Identity", 
	meta=(MustImplement="SAFActorInterface", AllowAbstract="false"))
	TSoftClassPtr<AActor> InstanceClass;

protected:

	virtual void PostLoad() override;

};
