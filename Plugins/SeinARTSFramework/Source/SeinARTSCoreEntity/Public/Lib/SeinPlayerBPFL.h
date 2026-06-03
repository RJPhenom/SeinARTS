/**
 * SeinARTS Framework
 * Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file:		SeinPlayerBPFL.h
 * @date:		3/27/2026
 * @author:		RJ Macklem
 * @brief:		Blueprint Function Library for per-player entity queries.
 *				Resource reads/writes moved to USeinResourceBPFL (see DESIGN §6).
 * @disclaimer: This code was generated in part by an AI language model.
 */

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Core/SeinEntityHandle.h"
#include "Core/SeinPlayerID.h"
#include "SeinPlayerBPFL.generated.h"

class USeinWorldSubsystem;

UCLASS(meta = (DisplayName = "SeinARTS Player Library"))
class SEINARTSCOREENTITY_API USeinPlayerBPFL : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	// Entity Queries
	// ====================================================================================================

	/** Get all entities owned by a specific player */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Player", meta = (WorldContext = "WorldContextObject", DisplayName = "Get Player Entities"))
	static TArray<FSeinEntityHandle> SeinGetPlayerEntities(const UObject* WorldContextObject, FSeinPlayerID PlayerID);

private:
	static USeinWorldSubsystem* GetWorldSubsystem(const UObject* WorldContextObject);
};
