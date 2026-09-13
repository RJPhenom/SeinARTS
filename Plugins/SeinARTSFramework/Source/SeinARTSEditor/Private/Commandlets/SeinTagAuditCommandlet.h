/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file         SeinTagAuditCommandlet.h
 * @author       RJ Macklem
 * @created      7 Sep 2026
 * @latest       7 Sep 2026
 * @brief        Runs the generated tag audit without opening a map or saving content assets.
 * @disclaimer   Generated with assistance from an AI language model.
 */
#pragma once
#include "Commandlets/Commandlet.h"
#include "SeinTagAuditCommandlet.generated.h"

UCLASS()
class USeinTagAuditCommandlet : public UCommandlet
{
	GENERATED_BODY()
public:
	USeinTagAuditCommandlet();
	virtual int32 Main(const FString& Params) override;
};
