#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "SeinPoolObjectCodecTestTypes.generated.h"

UENUM()
enum class ESeinPoolObjectSignedEnumTest : int8
{
	Negative = -1,
	Positive = 2,
};

UENUM()
enum ESeinPoolObjectByteEnumTest : uint8
{
	SeinPoolObjectByteEnum_First = 1,
	SeinPoolObjectByteEnum_Second = 4,
};

UENUM(meta = (Bitflags, UseEnumValuesAsMaskValuesInEditor = "true"))
enum class ESeinPoolObjectMaskBitTest : uint8
{
	None = 0,
	First = 1 << 0,
	Second = 1 << 1,
};
ENUM_CLASS_FLAGS(ESeinPoolObjectMaskBitTest);

UENUM(Flags)
enum class ESeinPoolObjectTypedFlagsTest : uint8
{
	None = 0,
	First = 1 << 0,
	Second = 1 << 1,
};
ENUM_CLASS_FLAGS(ESeinPoolObjectTypedFlagsTest);

UCLASS()
class USeinPoolObjectSignedEnumTestObject final : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY()
	ESeinPoolObjectSignedEnumTest Value =
		ESeinPoolObjectSignedEnumTest::Negative;
};

UCLASS()
class USeinPoolObjectByteEnumTestObject final : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY()
	TEnumAsByte<ESeinPoolObjectByteEnumTest> Value =
		SeinPoolObjectByteEnum_First;
};

UCLASS()
class USeinPoolObjectIntegerMaskTestObject final : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY(meta = (
		Bitmask,
		BitmaskEnum = "/Script/SeinARTSFrameworkTests.ESeinPoolObjectMaskBitTest"))
	int32 Mask = 0;
};

UCLASS()
class USeinPoolObjectTypedFlagsTestObject final : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY()
	ESeinPoolObjectTypedFlagsTest Value =
		ESeinPoolObjectTypedFlagsTest::None;
};

UCLASS()
class USeinPoolObjectCodecTestObject final : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY()
	TArray<int32> Values;

	UPROPERTY()
	FString Label;

	UPROPERTY()
	FName ExistingName;
};

UCLASS()
class USeinPoolObjectArrayOnlyTestObject final : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY()
	TArray<int32> Values;
};

USTRUCT()
struct FSeinPoolObjectFilteredElementTestValue
{
	GENERATED_BODY()

	UPROPERTY()
	int32 RestoredValue = 0;

	UPROPERTY()
	int32 LocalOnlyValue = 7;
};

UCLASS()
class USeinPoolObjectFilteredArrayTestObject final : public UObject
{
	GENERATED_BODY()

public:
	USeinPoolObjectFilteredArrayTestObject()
	{
		Values.SetNum(1);
		Values[0].LocalOnlyValue = 73;
	}

	UPROPERTY()
	TArray<FSeinPoolObjectFilteredElementTestValue> Values;
};

USTRUCT()
struct FSeinPoolObjectHiddenNativeStateTestValue
{
	GENERATED_BODY()

	UPROPERTY()
	int32 VisibleValue = 0;

	// Deliberately unreflected: pool admission must not assume that a native
	// constructor/destructor owns no storage merely because reflection is fixed.
	FString HiddenState = TEXT("hidden allocation");
};

UCLASS()
class USeinPoolObjectHiddenNativeArrayTestObject final : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY()
	TArray<FSeinPoolObjectHiddenNativeStateTestValue> Values;
};

UCLASS()
class USeinPoolObjectQueryTestObject final : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FGameplayTagQuery Query;
};
