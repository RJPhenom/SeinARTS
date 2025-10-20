#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemComponent.h"
#include "Gameplay/Attributes/SAFAttributeSet.h"
#include "Net/UnrealNetwork.h"
#include "Structs/SAFResourceBundle.h"
#include "Utils/SAFAttributeMacros.h"
#include "SAFProductionAttributes.generated.h"

struct FSAFAttributesRow;
struct FGameplayEffectModCallbackData;

/**
 * USAFProductionAttributes
 * 
 * Production/economy attributes (build time/speed and up to 12 cost channels).
 * These exist on SAFDataAssets that are buildable from a SAFProductionComponent.
 */
UCLASS()
class SEINARTS_FRAMEWORK_RUNTIME_API USAFProductionAttributes : public USAFAttributeSet {

	GENERATED_BODY()

public:

	USAFProductionAttributes();

	UPROPERTY(BlueprintReadOnly, Category="SeinARTS|Production", ReplicatedUsing=OnRep_BuildTime)
	FGameplayAttributeData BuildTime;                SAF_ATTR_ACCESSORS(USAFProductionAttributes, BuildTime)
	UPROPERTY(BlueprintReadOnly, Category="SeinARTS|Production", ReplicatedUsing=OnRep_BuildSpeed)
	FGameplayAttributeData BuildSpeed;               SAF_ATTR_ACCESSORS(USAFProductionAttributes, BuildSpeed)
	UPROPERTY(BlueprintReadOnly, Category="SeinARTS|Production", ReplicatedUsing=OnRep_Cost1)
	FGameplayAttributeData Cost1;                    SAF_ATTR_ACCESSORS(USAFProductionAttributes, Cost1)
	UPROPERTY(BlueprintReadOnly, Category="SeinARTS|Production", ReplicatedUsing=OnRep_Cost2)
	FGameplayAttributeData Cost2;                    SAF_ATTR_ACCESSORS(USAFProductionAttributes, Cost2)
	UPROPERTY(BlueprintReadOnly, Category="SeinARTS|Production", ReplicatedUsing=OnRep_Cost3)
	FGameplayAttributeData Cost3;                    SAF_ATTR_ACCESSORS(USAFProductionAttributes, Cost3)
	UPROPERTY(BlueprintReadOnly, Category="SeinARTS|Production", ReplicatedUsing=OnRep_Cost4)
	FGameplayAttributeData Cost4;                    SAF_ATTR_ACCESSORS(USAFProductionAttributes, Cost4)
	UPROPERTY(BlueprintReadOnly, Category="SeinARTS|Production", ReplicatedUsing=OnRep_Cost5)
	FGameplayAttributeData Cost5;                    SAF_ATTR_ACCESSORS(USAFProductionAttributes, Cost5)
	UPROPERTY(BlueprintReadOnly, Category="SeinARTS|Production", ReplicatedUsing=OnRep_Cost6)
	FGameplayAttributeData Cost6;                    SAF_ATTR_ACCESSORS(USAFProductionAttributes, Cost6)
	UPROPERTY(BlueprintReadOnly, Category="SeinARTS|Production", ReplicatedUsing=OnRep_Cost7)
	FGameplayAttributeData Cost7;                    SAF_ATTR_ACCESSORS(USAFProductionAttributes, Cost7)
	UPROPERTY(BlueprintReadOnly, Category="SeinARTS|Production", ReplicatedUsing=OnRep_Cost8)
	FGameplayAttributeData Cost8;                    SAF_ATTR_ACCESSORS(USAFProductionAttributes, Cost8)
	UPROPERTY(BlueprintReadOnly, Category="SeinARTS|Production", ReplicatedUsing=OnRep_Cost9)
	FGameplayAttributeData Cost9;                    SAF_ATTR_ACCESSORS(USAFProductionAttributes, Cost9)
	UPROPERTY(BlueprintReadOnly, Category="SeinARTS|Production", ReplicatedUsing=OnRep_Cost10)
	FGameplayAttributeData Cost10;                   SAF_ATTR_ACCESSORS(USAFProductionAttributes, Cost10)
	UPROPERTY(BlueprintReadOnly, Category="SeinARTS|Production", ReplicatedUsing=OnRep_Cost11)
	FGameplayAttributeData Cost11;                   SAF_ATTR_ACCESSORS(USAFProductionAttributes, Cost11)
	UPROPERTY(BlueprintReadOnly, Category="SeinARTS|Production", ReplicatedUsing=OnRep_Cost12)
	FGameplayAttributeData Cost12;                   SAF_ATTR_ACCESSORS(USAFProductionAttributes, Cost12)

	/** Returns the cost attributes as a FSAFResourceBundle bundle. */
	UFUNCTION(BlueprintPure, Category="SeinARTS|Production")
	FSAFResourceBundle BundleResources() const;

	// Replication
	// ==================================================================================================
	UFUNCTION() void OnRep_BuildTime        (const FGameplayAttributeData& Old) { GAMEPLAYATTRIBUTE_REPNOTIFY(USAFProductionAttributes, BuildTime,        Old); }
	UFUNCTION() void OnRep_BuildSpeed       (const FGameplayAttributeData& Old) { GAMEPLAYATTRIBUTE_REPNOTIFY(USAFProductionAttributes, BuildSpeed,       Old); }
	UFUNCTION() void OnRep_Cost1            (const FGameplayAttributeData& Old) { GAMEPLAYATTRIBUTE_REPNOTIFY(USAFProductionAttributes, Cost1,            Old); }
	UFUNCTION() void OnRep_Cost2            (const FGameplayAttributeData& Old) { GAMEPLAYATTRIBUTE_REPNOTIFY(USAFProductionAttributes, Cost2,            Old); }
	UFUNCTION() void OnRep_Cost3            (const FGameplayAttributeData& Old) { GAMEPLAYATTRIBUTE_REPNOTIFY(USAFProductionAttributes, Cost3,            Old); }
	UFUNCTION() void OnRep_Cost4            (const FGameplayAttributeData& Old) { GAMEPLAYATTRIBUTE_REPNOTIFY(USAFProductionAttributes, Cost4,            Old); }
	UFUNCTION() void OnRep_Cost5            (const FGameplayAttributeData& Old) { GAMEPLAYATTRIBUTE_REPNOTIFY(USAFProductionAttributes, Cost5,            Old); }
	UFUNCTION() void OnRep_Cost6            (const FGameplayAttributeData& Old) { GAMEPLAYATTRIBUTE_REPNOTIFY(USAFProductionAttributes, Cost6,            Old); }
	UFUNCTION() void OnRep_Cost7            (const FGameplayAttributeData& Old) { GAMEPLAYATTRIBUTE_REPNOTIFY(USAFProductionAttributes, Cost7,            Old); }
	UFUNCTION() void OnRep_Cost8            (const FGameplayAttributeData& Old) { GAMEPLAYATTRIBUTE_REPNOTIFY(USAFProductionAttributes, Cost8,            Old); }
	UFUNCTION() void OnRep_Cost9            (const FGameplayAttributeData& Old) { GAMEPLAYATTRIBUTE_REPNOTIFY(USAFProductionAttributes, Cost9,            Old); }
	UFUNCTION() void OnRep_Cost10           (const FGameplayAttributeData& Old) { GAMEPLAYATTRIBUTE_REPNOTIFY(USAFProductionAttributes, Cost10,           Old); }
	UFUNCTION() void OnRep_Cost11           (const FGameplayAttributeData& Old) { GAMEPLAYATTRIBUTE_REPNOTIFY(USAFProductionAttributes, Cost11,           Old); }
	UFUNCTION() void OnRep_Cost12           (const FGameplayAttributeData& Old) { GAMEPLAYATTRIBUTE_REPNOTIFY(USAFProductionAttributes, Cost12,           Old); }

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

};
