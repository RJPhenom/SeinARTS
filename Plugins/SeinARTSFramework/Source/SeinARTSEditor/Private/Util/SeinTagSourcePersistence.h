/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file         SeinTagSourcePersistence.h
 * @author       RJ Macklem
 * @created      7 Sep 2026
 * @latest       7 Sep 2026
 * @brief        Verifies complete tag definitions and redirects before dictionary mutation.
 * @disclaimer   Generated with assistance from an AI language model.
 */
#pragma once
#include "GameplayTagsSettings.h"

namespace SeinTagSourcePersistence
{
	bool MatchesDisk(const FString& Path, const UGameplayTagsList* List);
	bool MatchesDisk(const FString& Path, const FString& Section,
		const TArray<FGameplayTagTableRow>& Definitions, const TArray<FGameplayTagRedirect>& Redirects);
}
