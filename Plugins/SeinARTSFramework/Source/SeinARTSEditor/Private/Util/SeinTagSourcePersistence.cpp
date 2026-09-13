/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file         SeinTagSourcePersistence.cpp
 * @author       RJ Macklem
 * @created      7 Sep 2026
 * @latest       7 Sep 2026
 * @brief        Verifies complete tag definitions and redirects before dictionary mutation.
 * @disclaimer   Generated with assistance from an AI language model.
 */
#include "Util/SeinTagSourcePersistence.h"
#include "Misc/ConfigCacheIni.h"

namespace
{
	template<typename T>
	bool MatchesDiskRows(const TArray<FString>& Lines, const TArray<T>& Loaded)
	{
		if (Lines.Num() != Loaded.Num()) return false;
		TArray<T> Remaining = Loaded;
		for (const FString& Line : Lines)
		{
			T Parsed;
			if (!T::StaticStruct()->ImportText(*Line, &Parsed, nullptr, PPF_None, GWarn, TEXT("GameplayTagMigration"))) return false;
			const int32 Index = Remaining.IndexOfByPredicate([&Parsed](const T& Row)
			{
				return T::StaticStruct()->CompareScriptStruct(&Parsed, &Row, PPF_None);
			});
			if (Index == INDEX_NONE) return false;
			Remaining.RemoveAtSwap(Index);
		}
		return Remaining.IsEmpty();
	}

}

bool SeinTagSourcePersistence::MatchesDisk(const FString& Path, const UGameplayTagsList* List)
{
	return List && MatchesDisk(Path, List->GetClass()->GetPathName(), List->GameplayTagList, List->GameplayTagRedirects);
}

bool SeinTagSourcePersistence::MatchesDisk(const FString& Path, const FString& Section,
	const TArray<FGameplayTagTableRow>& ExpectedDefinitions, const TArray<FGameplayTagRedirect>& ExpectedRedirects)
{
	FConfigFile File;
	File.Read(Path);
	TArray<FString> Definitions, Redirects;
	File.GetArray(*Section, TEXT("GameplayTagList"), Definitions);
	File.GetArray(*Section, TEXT("GameplayTagRedirects"), Redirects);
	return MatchesDiskRows(Definitions, ExpectedDefinitions) && MatchesDiskRows(Redirects, ExpectedRedirects);
}
