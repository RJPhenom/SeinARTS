#include "SeinReplayFileIO.h"

#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

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

bool SeinReplayFileIO::WriteNewAtomically(
	const FString& FilePath,
	const TArray<uint8>& Bytes,
	FString& OutError)
{
	OutError.Reset();
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

	const FString TempPath = FilePath + TEXT(".tmp.")
		+ FGuid::NewGuid().ToString(EGuidFormats::Digits);
	if (!FFileHelper::SaveArrayToFile(Bytes, *TempPath))
	{
		FileManager.Delete(*TempPath, false, true, true);
		OutError = TEXT("could not write and close the sibling temporary file");
		return false;
	}
	if (!FileManager.Move(
			*FilePath, *TempPath,
			/*Replace=*/false, /*EvenIfReadOnly=*/false,
			/*Attributes=*/false, /*DoNotRetryOrError=*/true))
	{
		FileManager.Delete(*TempPath, false, true, true);
		OutError = TEXT("could not atomically publish the completed replay file");
		return false;
	}
	return true;
}
