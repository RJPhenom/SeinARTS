#pragma once

#include "Abilities/SeinAbility.h"
#include "Brokers/SeinCommandBrokerResolver.h"
#include "Components/SeinComponent.h"
#include "Data/SeinFaction.h"
#include "StructUtils/InstancedStruct.h"
#include "SeinInitialStateDigestTestTypes.generated.h"

USTRUCT(meta = (SeinDeterministic))
struct FSeinInitialStateDigestNestedValue
{
	GENERATED_BODY()

	FSeinInitialStateDigestNestedValue() = default;
	explicit FSeinInitialStateDigestNestedValue(const int32 InMarker)
		: Marker(InMarker)
	{
	}

	UPROPERTY()
	int32 Marker = 0;
};

USTRUCT(meta = (SeinDeterministic))
struct FSeinInitialStateDigestSignedValue
{
	GENERATED_BODY()

	UPROPERTY()
	int32 Value = 0;
};

USTRUCT(meta = (SeinDeterministic))
struct FSeinInitialStateDigestUnsignedValue
{
	GENERATED_BODY()

	UPROPERTY()
	uint32 Value = 0;
};

UCLASS(EditInlineNew, DefaultToInstanced)
class USeinInitialStateDigestAliasNode : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY()
	int32 Marker = 0;

	UPROPERTY(Instanced)
	TObjectPtr<USeinInitialStateDigestAliasNode> Next;
};

USTRUCT(meta = (SeinDeterministic))
struct FSeinInitialStateDigestProbeComponent : public FSeinComponent
{
	GENERATED_BODY()

	UPROPERTY()
	int32 Scalar = 0;

	UPROPERTY()
	TMap<FName, int32> Values;

	UPROPERTY()
	FInstancedStruct Nested;

	UPROPERTY(Transient)
	int32 TransientValue = 0;

	/** Metadata alone must not alter the cooked lockstep schema. */
	UPROPERTY(meta = (SeinStateIgnore))
	int32 MetadataOnlyIgnoreAttempt = 0;

	UPROPERTY()
	FText PresentationText;

	/** Null is canonical; a non-asset runtime object must fail closed. */
	UPROPERTY()
	TObjectPtr<UObject> RuntimeObject;

	UPROPERTY(Instanced)
	TObjectPtr<USeinInitialStateDigestAliasNode> FirstAlias;

	UPROPERTY(Instanced)
	TObjectPtr<USeinInitialStateDigestAliasNode> SecondAlias;
};

UCLASS()
class USeinInitialStateDigestTestAbility : public USeinAbility
{
	GENERATED_BODY()

public:
	UPROPERTY()
	int32 DeterministicMarker = 0;

	UPROPERTY(Transient)
	int32 TransientMarker = 0;

	UPROPERTY()
	FText PresentationText;
};

UCLASS()
class USeinInitialStateDigestTestResolver
	: public USeinCommandBrokerResolver
{
	GENERATED_BODY()

public:
	UPROPERTY()
	int32 DeterministicMarker = 0;

	UPROPERTY()
	TMap<FName, int32> AuthoredValues;
};

UCLASS()
class USeinInitialStateDigestTestFaction : public USeinFaction
{
	GENERATED_BODY()

public:
	UPROPERTY()
	int32 AuthoredMarker = 0;
};
