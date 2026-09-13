/** Test-only reflected rows exercise custom table serialization and nested tag references. */
#pragma once
#include "Engine/DataTable.h"
#include "GameplayTagContainer.h"
#include "StructUtils/InstancedStruct.h"
#include "SeinTagLifecycleTestTypes.generated.h"

USTRUCT()
struct FSeinTagLifecycleTestRow : public FTableRowBase
{
	GENERATED_BODY()
	UPROPERTY() FGameplayTag Tag;
	UPROPERTY() FGameplayTagContainer Tags;
	UPROPERTY() FGameplayTagQuery Query;
	UPROPERTY() TMap<FGameplayTag, int32> Map;
	UPROPERTY() FInstancedStruct Payload;
};
