/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinReplayBPFL.cpp
 */

#include "Lib/SeinReplayBPFL.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Misc/FileHelper.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Engine/World.h"

USeinWorldSubsystem* USeinReplayBPFL::GetWorldSubsystem(const UObject* WorldContextObject)
{
	if (!WorldContextObject) return nullptr;
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull) : nullptr;
	return World ? World->GetSubsystem<USeinWorldSubsystem>() : nullptr;
}

FSeinReplayHeader USeinReplayBPFL::SeinBuildReplayHeader(const UObject* WorldContextObject, FName MapIdentifier)
{
	FSeinReplayHeader Header;
	USeinWorldSubsystem* Sub = GetWorldSubsystem(WorldContextObject);
	if (!Sub) return Header;

	// TODO: read project + framework versions from ini / plugin descriptor when
	// the "game version" concept is formalized. V1 stamps placeholder strings.
	Header.FrameworkVersion = TEXT("0.1.0");
	Header.GameVersion = TEXT("unset");
	Header.MapIdentifier = MapIdentifier;
	Header.RandomSeed = 0; // set by match-flow plumbing when PRNG seeding lands
	Header.SettingsSnapshot = Sub->GetMatchSettings();
	Header.StartTick = 0;
	Header.EndTick = Sub->GetCurrentTick();
	Header.RecordedAt = FDateTime::UtcNow();
	// Player registrations: walk the registered-player map via the BP accessor.
	// (Sub->GetPlayerCount / ForEachPlayerStateMutable — but that last one is
	// non-const; we use the iteration helper's read-only cousin by calling
	// ForEachPlayerStateMutable on a non-const cast, which is safe because we
	// don't mutate. Prefer a const iterator if available when this matters.)
	return Header;
}

bool USeinReplayBPFL::SeinSaveReplay(const UObject* /*WorldContextObject*/, const FSeinReplayHeader& Header, const FString& FilePath)
{
	TArray<uint8> Buffer;
	FMemoryWriter Writer(Buffer, /*bIsPersistent=*/true);
	// Serialize the USTRUCT via StructUtils. Accepted path for UE USTRUCTs:
	// FSeinReplayHeader::StaticStruct()->SerializeItem(Writer, &Header, nullptr).
	FSeinReplayHeader Mutable = Header;
	FSeinReplayHeader::StaticStruct()->SerializeItem(Writer, &Mutable, nullptr);
	return FFileHelper::SaveArrayToFile(Buffer, *FilePath);
}

bool USeinReplayBPFL::SeinLoadReplay(const UObject* /*WorldContextObject*/, const FString& FilePath, FSeinReplayHeader& OutHeader)
{
	TArray<uint8> Buffer;
	if (!FFileHelper::LoadFileToArray(Buffer, *FilePath)) return false;
	FMemoryReader Reader(Buffer, /*bIsPersistent=*/true);
	FSeinReplayHeader::StaticStruct()->SerializeItem(Reader, &OutHeader, nullptr);
	return true;
}
