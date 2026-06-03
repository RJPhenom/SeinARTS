/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinReplayWriter.cpp
 */

#include "SeinReplayWriter.h"
#include "SeinARTSNet.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Misc/DateTime.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"
#include "HAL/FileManager.h"

void USeinReplayWriter::StartRecording(const FSeinReplayHeader& Header)
{
	if (bRecording)
	{
		UE_LOG(LogSeinNet, Warning, TEXT("ReplayWriter::StartRecording called while already recording — discarding %d buffered turn(s) and resetting."),
			Buffer.Turns.Num());
	}

	Buffer = FSeinReplay();
	Buffer.Header = Header;
	bRecording = true;

	UE_LOG(LogSeinNet, Log, TEXT("ReplayWriter: recording started.  seed=%lld  map=%s"),
		Header.RandomSeed, *Header.MapIdentifier.ToString());
}

void USeinReplayWriter::RecordTurn(int32 TurnId, const TArray<FSeinCommand>& Commands)
{
	if (!bRecording) return;

	FSeinReplayTurnRecord& Record = Buffer.Turns.AddDefaulted_GetRef();
	Record.TurnId = TurnId;
	Record.Commands = Commands;
}

FString USeinReplayWriter::FinishRecording()
{
	if (!bRecording)
	{
		UE_LOG(LogSeinNet, Warning, TEXT("ReplayWriter::FinishRecording called but not recording — no-op."));
		return FString();
	}
	bRecording = false;

	// Stamp end-tick now that we know it. Header.StartTick was set on Start.
	// (Writer doesn't track ticks directly — caller can update Header.EndTick
	// before FinishRecording if more accuracy is needed; otherwise we leave
	// it at whatever was set in StartRecording.)

	// Build deterministic filename: <Map>_<UTCStamp>.seinreplay
	const FString MapStr = Buffer.Header.MapIdentifier.IsNone() ? TEXT("UnknownMap") : Buffer.Header.MapIdentifier.ToString();
	const FString Stamp = Buffer.Header.RecordedAt.ToString(TEXT("%Y%m%d_%H%M%S"));
	const FString FileName = FString::Printf(TEXT("%s_%s.seinreplay"), *MapStr, *Stamp);
	const FString DirPath = FPaths::ProjectSavedDir() / TEXT("Replays");
	const FString FilePath = DirPath / FileName;

	// Ensure the Saved/Replays/ directory exists.
	IFileManager::Get().MakeDirectory(*DirPath, /*Tree*/ true);

	// Serialize the FSeinReplay USTRUCT via the reflection system. We must
	// wrap the FMemoryWriter in FObjectAndNameAsStringProxyArchive because
	// FSeinCommand::Payload is an FInstancedStruct, whose Serialize path
	// touches UScriptStruct* / UObject references — plain FArchive errors
	// out with "FArchive does not support FObjectPtr serialization." The
	// proxy archive saves those refs as path strings (resolvable on load
	// via FindObject), which is the canonical UE5 pattern for offline-file
	// USTRUCT writes that contain reflected UObject members.
	TArray<uint8> Buf;
	FMemoryWriter MemWriter(Buf, /*bIsPersistent*/ true);
	FObjectAndNameAsStringProxyArchive Writer(MemWriter, /*bInLoadIfFindFails*/ false);
	FSeinReplay::StaticStruct()->SerializeItem(Writer, &Buffer, nullptr);

	const bool bOk = FFileHelper::SaveArrayToFile(Buf, *FilePath);
	if (!bOk)
	{
		UE_LOG(LogSeinNet, Error, TEXT("ReplayWriter: FAILED to write replay to %s"), *FilePath);
		return FString();
	}

	UE_LOG(LogSeinNet, Log, TEXT("ReplayWriter: wrote %d turn(s), %d bytes -> %s"),
		Buffer.Turns.Num(), Buf.Num(), *FilePath);

	// Drop the buffer to release memory now that it's persisted.
	Buffer = FSeinReplay();
	return FilePath;
}
