/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file:    SeinCoverComponent.h
 * @date:    9/1/2026
 * @author:  RJ Macklem
 * @brief:   Authoring ActorComponent for the cover-provider payload. Lives in
 *           the Cover extension with its payload struct — enabling the
 *           extension adds the component to the Add menu, disabling it removes
 *           it, and the base framework never references it. See
 *           Authoring/SeinEntityComponent.h for the naming contract and the
 *           data-only rules.
 */

#pragma once

#include "CoreMinimal.h"
#include "Authoring/SeinEntityComponent.h"
#include "Components/SeinCoverPayload.h"
#include "SeinCoverComponent.generated.h"

/** Cover-provider configuration (slots, quality, geometry binding). */
UCLASS(NotBlueprintable, ClassGroup = (SeinARTS), meta = (BlueprintSpawnableComponent))
class SEINARTSCOVER_API USeinCoverComponent : public USeinEntityComponent
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, Category = "SeinARTS", meta = (ShowOnlyInnerProperties))
	FSeinCoverPayload Cover;

	virtual const UScriptStruct* GetPayloadStruct() const override
	{
		return FSeinCoverPayload::StaticStruct();
	}
	virtual bool WritePayload(FInstancedStruct& Out) const override
	{
		Out.InitializeAs<FSeinCoverPayload>(Cover);
		return true;
	}
};
