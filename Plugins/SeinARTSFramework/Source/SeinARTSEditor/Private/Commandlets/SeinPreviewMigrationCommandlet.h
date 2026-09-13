/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file         SeinPreviewMigrationCommandlet.h
 * @author       RJ Macklem
 * @created      12 Sep 2026
 * @latest       12 Sep 2026
 * @brief        Migrates the two demo previews without changing Blueprint parents.
 * @disclaimer   This code was generated in part with the assistance of an AI language model.
 */
#pragma once
#include "Commandlets/Commandlet.h"
#include "SeinPreviewMigrationCommandlet.generated.h"
UCLASS()
class USeinPreviewMigrationCommandlet : public UCommandlet
{
	GENERATED_BODY()
public:
	USeinPreviewMigrationCommandlet();
	virtual int32 Main(const FString& Params) override;
};
