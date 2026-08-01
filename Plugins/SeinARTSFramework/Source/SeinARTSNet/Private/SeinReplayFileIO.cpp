#include "SeinReplayFileIO.h"

#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
	constexpr int64 HardReplayJournalBytes =
		64LL * 1024LL * 1024LL * 1024LL;
}

bool SeinReplayFileIO::QueryBoundedSize(
	const FString& FilePath,
	int64 MinBytes,
	int64 MaxBytes,
	int64& OutSize,
	FString& OutError)
{
	OutError.Reset();
	if (FilePath.IsEmpty() || MinBytes < 0 || MaxBytes < MinBytes)
	{
		OutError = TEXT("invalid replay file-size query");
		return false;
	}

	TUniquePtr<FArchive> Reader(
		IFileManager::Get().CreateFileReader(*FilePath, FILEREAD_Silent));
	if (!Reader)
	{
		OutError = TEXT("could not open the replay file");
		return false;
	}
	const int64 CandidateSize = Reader->TotalSize();
	if (CandidateSize < MinBytes || CandidateSize > MaxBytes)
	{
		OutError = FString::Printf(
			TEXT("file size %lld is outside the allowed %lld..%lld bytes"),
			static_cast<long long>(CandidateSize),
			static_cast<long long>(MinBytes),
			static_cast<long long>(MaxBytes));
		return false;
	}
	const bool bClosed = Reader->Close();
	if (!bClosed || Reader->IsError() || Reader->IsCriticalError())
	{
		OutError = TEXT("could not query and close the replay file");
		return false;
	}
	OutSize = CandidateSize;
	return true;
}

bool SeinReplayFileIO::ReadRange(
	const FString& FilePath,
	int64 Offset,
	int64 NumBytes,
	int64 MaxFileBytes,
	TArray<uint8>& OutBytes,
	FString& OutError)
{
	OutError.Reset();
	if (FilePath.IsEmpty() || Offset < 0 || NumBytes < 0
		|| NumBytes > MAX_int32 || MaxFileBytes < 0)
	{
		OutError = TEXT("invalid bounded replay read range");
		return false;
	}

	TUniquePtr<FArchive> Reader(
		IFileManager::Get().CreateFileReader(*FilePath, FILEREAD_Silent));
	if (!Reader)
	{
		OutError = TEXT("could not open the replay file");
		return false;
	}
	const int64 Size = Reader->TotalSize();
	if (Size < 0 || Size > MaxFileBytes)
	{
		OutError = TEXT("replay file exceeds the allowed bounded size");
		return false;
	}
	if (Offset > Size || NumBytes > Size - Offset)
	{
		OutError = TEXT("replay read range extends beyond the open file");
		return false;
	}

	TArray<uint8> Candidate;
	Candidate.SetNumUninitialized(static_cast<int32>(NumBytes));
	Reader->Seek(Offset);
	if (Reader->Tell() != Offset || Reader->IsError())
	{
		OutError = TEXT("could not seek to the replay read range");
		return false;
	}
	if (NumBytes > 0)
	{
		Reader->Serialize(Candidate.GetData(), NumBytes);
	}
	const bool bClosed = Reader->Close();
	if (!bClosed || Reader->IsError() || Reader->IsCriticalError())
	{
		OutError = TEXT("could not read and close the replay file range");
		return false;
	}
	OutBytes = MoveTemp(Candidate);
	return true;
}

bool SeinReplayFileIO::CreateNew(
	const FString& FilePath,
	TConstArrayView<uint8> Bytes,
	FString& OutError)
{
	OutError.Reset();
	if (FilePath.IsEmpty())
	{
		OutError = TEXT("replay file path is empty");
		return false;
	}
	IFileManager& FileManager = IFileManager::Get();
	const FString Parent = FPaths::GetPath(FilePath);
	if (!Parent.IsEmpty() && !FileManager.DirectoryExists(*Parent)
		&& !FileManager.MakeDirectory(*Parent, /*Tree=*/true))
	{
		OutError = TEXT("could not create the replay directory");
		return false;
	}
	if (FileManager.FileExists(*FilePath))
	{
		OutError = TEXT("refusing to replace an existing replay file");
		return false;
	}

	TUniquePtr<FArchive> Writer(FileManager.CreateFileWriter(
		*FilePath,
		FILEWRITE_NoReplaceExisting | FILEWRITE_AllowRead | FILEWRITE_Silent));
	if (!Writer)
	{
		OutError = TEXT("could not create the new replay file");
		return false;
	}
	if (!Bytes.IsEmpty())
	{
		Writer->Serialize(
			const_cast<uint8*>(Bytes.GetData()),
			Bytes.Num());
	}
	Writer->Flush();
	const bool bClosed = Writer->Close();
	const bool bWritten = bClosed
		&& !Writer->IsError() && !Writer->IsCriticalError();
	Writer.Reset();
	if (!bWritten)
	{
		FileManager.Delete(*FilePath, false, true, true);
		OutError = TEXT("could not write and close the new replay file");
		return false;
	}

	IPlatformFile& PlatformFile =
		FPlatformFileManager::Get().GetPlatformFile();
	TUniquePtr<IFileHandle> DurabilityHandle(
		PlatformFile.OpenWrite(*FilePath, /*bAppend=*/true, /*bAllowRead=*/true));
	if (!DurabilityHandle
		|| DurabilityHandle->Size() != Bytes.Num()
		|| !DurabilityHandle->Flush(/*bFullFlush=*/true))
	{
		DurabilityHandle.Reset();
		FileManager.Delete(*FilePath, false, true, true);
		OutError = TEXT("could not durably flush the new replay file");
		return false;
	}
	return true;
}

bool SeinReplayFileIO::AppendAtExpectedOffset(
	const FString& FilePath,
	int64 ExpectedOffset,
	TConstArrayView<uint8> Bytes,
	FString& OutError)
{
	OutError.Reset();
	if (FilePath.IsEmpty() || ExpectedOffset < 0
		|| ExpectedOffset > MAX_int64 - static_cast<int64>(Bytes.Num())
		|| ExpectedOffset > HardReplayJournalBytes
		|| static_cast<int64>(Bytes.Num())
			> HardReplayJournalBytes - ExpectedOffset)
	{
		OutError = TEXT("invalid replay append request");
		return false;
	}
	IPlatformFile& PlatformFile =
		FPlatformFileManager::Get().GetPlatformFile();
	if (!PlatformFile.FileExists(*FilePath))
	{
		OutError = TEXT("replay append target does not exist");
		return false;
	}
	TUniquePtr<IFileHandle> Writer(
		PlatformFile.OpenWrite(*FilePath, /*bAppend=*/true, /*bAllowRead=*/true));
	if (!Writer)
	{
		OutError = TEXT("could not open the replay append target");
		return false;
	}
	const int64 ActualSize = Writer->Size();
	if (ActualSize != ExpectedOffset)
	{
		OutError = FString::Printf(
			TEXT("replay append offset mismatch (expected %lld, found %lld)"),
			static_cast<long long>(ExpectedOffset),
			static_cast<long long>(ActualSize));
		return false;
	}
	if (!Writer->SeekFromEnd(0) || Writer->Tell() != ExpectedOffset)
	{
		OutError = TEXT("could not seek to the verified replay append offset");
		return false;
	}
	if (!Bytes.IsEmpty()
		&& !Writer->Write(Bytes.GetData(), Bytes.Num()))
	{
		OutError = TEXT("could not append the complete replay journal bytes");
		return false;
	}
	const int64 ExpectedEnd = ExpectedOffset + Bytes.Num();
	if (Writer->Tell() != ExpectedEnd
		|| !Writer->Flush(/*bFullFlush=*/true))
	{
		OutError = TEXT("could not durably flush the replay journal append");
		return false;
	}
	return true;
}

bool SeinReplayFileIO::PublishExistingAtomically(
	const FString& PartialPath,
	const FString& FinalPath,
	FString& OutError)
{
	OutError.Reset();
	if (PartialPath.IsEmpty() || FinalPath.IsEmpty())
	{
		OutError = TEXT("replay publish path is empty");
		return false;
	}
	FString AbsolutePartial = FPaths::ConvertRelativePathToFull(PartialPath);
	FString AbsoluteFinal = FPaths::ConvertRelativePathToFull(FinalPath);
	FPaths::NormalizeFilename(AbsolutePartial);
	FPaths::NormalizeFilename(AbsoluteFinal);
	if (FPaths::IsSamePath(AbsolutePartial, AbsoluteFinal)
		|| !FPaths::IsSamePath(
			FPaths::GetPath(AbsolutePartial),
			FPaths::GetPath(AbsoluteFinal)))
	{
		OutError = TEXT("atomic replay publish requires distinct sibling paths");
		return false;
	}

	IFileManager& FileManager = IFileManager::Get();
	if (!FileManager.FileExists(*AbsolutePartial))
	{
		OutError = TEXT("replay partial file does not exist");
		return false;
	}
	if (FileManager.FileExists(*AbsoluteFinal))
	{
		OutError = TEXT("refusing to replace an existing replay file");
		return false;
	}
	if (!FileManager.Move(
			*AbsoluteFinal,
			*AbsolutePartial,
			/*Replace=*/false,
			/*EvenIfReadOnly=*/false,
			/*Attributes=*/false,
			/*DoNotRetryOrError=*/true))
	{
		OutError = TEXT("could not atomically publish the completed replay file");
		return false;
	}
	return true;
}

bool SeinReplayFileIO::ReadBounded(
	const FString& FilePath,
	int64 MinBytes,
	int64 MaxBytes,
	TArray<uint8>& OutBytes,
	FString& OutError)
{
	OutError.Reset();
	if (MinBytes < 0 || MaxBytes < MinBytes || MaxBytes > MAX_int32)
	{
		OutError = TEXT("invalid replay file-size bounds");
		return false;
	}

	TUniquePtr<FArchive> Reader(
		IFileManager::Get().CreateFileReader(*FilePath, FILEREAD_Silent));
	if (!Reader)
	{
		OutError = TEXT("could not open the replay file");
		return false;
	}
	const int64 Size = Reader->TotalSize();
	if (Size < MinBytes || Size > MaxBytes)
	{
		OutError = FString::Printf(
			TEXT("file size %lld is outside the allowed %lld..%lld bytes"),
			static_cast<long long>(Size),
			static_cast<long long>(MinBytes),
			static_cast<long long>(MaxBytes));
		return false;
	}

	TArray<uint8> Candidate;
	Candidate.SetNumUninitialized(static_cast<int32>(Size));
	Reader->Serialize(Candidate.GetData(), Candidate.Num());
	const bool bClosed = Reader->Close();
	if (!bClosed || Reader->IsError() || Reader->IsCriticalError())
	{
		OutError = TEXT("could not read and close the replay file");
		return false;
	}
	OutBytes = MoveTemp(Candidate);
	return true;
}

