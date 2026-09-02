/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file:    SeinPayloadStruct.h
 * @date:    9/2/2026
 * @author:  RJ Macklem
 * @brief:   UserDefinedStruct subclass for the auto-synced payload structs
 *           embedded inside entity-component Blueprint packages. Exists for
 *           exactly one behavior: IsAsset() returns false, so the struct is
 *           genuine plumbing — saved and cross-package-referencable like any
 *           public export, but never listed by the asset registry or Content
 *           Browser. This is the SAME mechanism that hides generated classes
 *           (UClass::IsAsset() == false); a plain UUserDefinedStruct in a
 *           package IS an asset by RF_Public alone, which is why embedding
 *           one without this override still showed up after a registry scan.
 */

#pragma once

#include "CoreMinimal.h"
#include "StructUtils/UserDefinedStruct.h"
#include "SeinPayloadStruct.generated.h"

UCLASS()
class SEINARTSCOREENTITY_API USeinPayloadStruct : public UUserDefinedStruct
{
	GENERATED_BODY()

public:
	virtual bool IsAsset() const override { return false; }
};
