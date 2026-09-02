/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file:    SeinVisionComponent.h
 * @date:    9/1/2026
 * @author:  RJ Macklem
 * @brief:   Authoring ActorComponent for the entity's vision payload. Lives in
 *           the fog-of-war module with its payload struct — the authoring base
 *           (SeinARTSCoreEntity) stays ignorant of concrete payload owners.
 *           See Authoring/SeinEntityComponent.h for the naming contract and the
 *           data-only rules.
 */

#pragma once

#include "CoreMinimal.h"
#include "Authoring/SeinEntityComponent.h"
#include "Components/SeinVisionPayload.h"
#include "SeinVisionComponent.generated.h"

/** Vision configuration (sight stamps, ranges, FoW layer contribution). */
UCLASS(NotBlueprintable, ClassGroup = (SeinARTS), meta = (BlueprintSpawnableComponent))
class SEINARTSFOGOFWAR_API USeinVisionComponent : public USeinEntityComponent
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, Category = "SeinARTS", meta = (ShowOnlyInnerProperties))
	FSeinVisionPayload Vision;

	virtual const UScriptStruct* GetPayloadStruct() const override
	{
		return FSeinVisionPayload::StaticStruct();
	}
	virtual bool WritePayload(FInstancedStruct& Out) const override
	{
		Out.InitializeAs<FSeinVisionPayload>(Vision);
		return true;
	}
};
