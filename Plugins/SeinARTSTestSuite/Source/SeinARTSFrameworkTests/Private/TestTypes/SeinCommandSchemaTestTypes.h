#pragma once

#include "CoreMinimal.h"
#include "Core/SeinEntityHandle.h"
#include "Input/SeinCommandSchemaRegistry.h"
#include "StructUtils/InstancedStruct.h"
#include "SeinCommandSchemaTestTypes.generated.h"

USTRUCT(meta = (SeinDeterministic))
struct FSeinCommandSchemaNestedTestValue
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<int32> Values;
};

/** Valid payload with recursively nested containers for aggregate-budget tests. */
USTRUCT(meta = (SeinDeterministic))
struct FSeinCommandSchemaTestPayload
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FSeinCommandSchemaNestedTestValue> Groups;
};

USTRUCT(meta = (SeinDeterministic))
struct FSeinCommandSchemaAlternateTestPayload
{
	GENERATED_BODY()

	UPROPERTY()
	int32 Marker = 0;
};

/** Non-alphabetical declaration order proves the manifest retains wire order. */
USTRUCT(meta = (SeinDeterministic))
struct FSeinCommandSchemaWireOrderTestPayload
{
	GENERATED_BODY()

	UPROPERTY()
	int32 ZetaDeclaredFirst = 0;

	UPROPERTY()
	int32 AlphaDeclaredSecond = 0;
};

USTRUCT(meta = (SeinDeterministic))
struct FSeinCommandSchemaIdentityWireTestPayload
{
	GENERATED_BODY()

	UPROPERTY()
	FName Name;

	UPROPERTY()
	FGameplayTag Tag;
};

USTRUCT(meta = (SeinDeterministic))
struct FSeinCommandSchemaEntityHandleWireTestPayload
{
	GENERATED_BODY()

	UPROPERTY()
	FSeinEntityHandle Entity;
};

/** Compact reflected value with deliberately large trusted in-memory expansion. */
USTRUCT(meta = (SeinDeterministic))
struct FSeinCommandSchemaLargeWireElement
{
	GENERATED_BODY()

	UPROPERTY()
	int32 Marker = 0;

	uint8 NativePadding[4096]{};
};

USTRUCT(meta = (SeinDeterministic))
struct FSeinCommandSchemaLargeWireArrayPayload
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FSeinCommandSchemaLargeWireElement> Values;
};

/** Same reflected wire shape as the padded element, without native-only data. */
USTRUCT(meta = (SeinDeterministic))
struct FSeinCommandSchemaCompactWireElement
{
	GENERATED_BODY()

	UPROPERTY()
	int32 Marker = 0;
};

USTRUCT(meta = (SeinDeterministic))
struct FSeinCommandSchemaCompactWireArrayPayload
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FSeinCommandSchemaCompactWireElement> Values;
};

USTRUCT(meta = (SeinDeterministic))
struct FSeinCommandSchemaStringGroup
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FString> Values;
};

USTRUCT(meta = (SeinDeterministic))
struct FSeinCommandSchemaNestedStringPayload
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FSeinCommandSchemaStringGroup> Groups;
};

/** Missing the explicit deterministic schema marker. */
USTRUCT()
struct FSeinCommandSchemaUnmarkedTestPayload
{
	GENERATED_BODY()

	UPROPERTY()
	int32 Marker = 0;
};

/** Marker alone cannot make a floating-point field safe for lockstep payloads. */
USTRUCT(meta = (SeinDeterministic))
struct FSeinCommandSchemaUnsupportedTestPayload
{
	GENERATED_BODY()

	UPROPERTY()
	float Value = 0.0f;
};

/** Marker alone cannot make hash-bucket iteration deterministic across peers. */
USTRUCT(meta = (SeinDeterministic))
struct FSeinCommandSchemaUnorderedTestPayload
{
	GENERATED_BODY()

	UPROPERTY()
	TMap<FName, int32> Values;
};

/** Exercises recursively validated dynamic extension values. */
USTRUCT(meta = (SeinDeterministic))
struct FSeinCommandSchemaDynamicTestPayload
{
	GENERATED_BODY()

	UPROPERTY(meta = (SeinDeterministicOnly))
	TArray<FInstancedStruct> Extensions;
};

UCLASS()
class USeinCommandSchemaTestHandler : public USeinCommandHandler
{
	GENERATED_BODY()
};

UCLASS()
class USeinCommandSchemaAlternateTestHandler : public USeinCommandHandler
{
	GENERATED_BODY()
};

/** Native stand-in for a Blueprint handler that authors its schema on the CDO. */
UCLASS()
class USeinCommandSchemaConfiguredTestHandler : public USeinCommandHandler
{
	GENERATED_BODY()

public:
	USeinCommandSchemaConfiguredTestHandler();
};
