#include "Simulation/SeinTestSnapshotRestore.h"

#include "Simulation/SeinSnapshotRestoreAuthority.h"
#include "Simulation/SeinWorldSubsystem.h"

namespace
{
	const FName TestSnapshotRestoreAuthorityID(
		TEXT("SeinARTSTestSupport"));
}

bool SeinTestSnapshotRestore::RestoreTrusted(
	USeinWorldSubsystem& World,
	const FSeinWorldSnapshot& Snapshot,
	FString* OutClaimError)
{
	if (OutClaimError)
	{
		OutClaimError->Reset();
	}

	FSeinSnapshotRestoreAuthorityHandle Authority;
	FString ClaimError;
	if (!World.ClaimSnapshotRestoreAuthority(
		TestSnapshotRestoreAuthorityID,
		&World,
		Authority,
		ClaimError))
	{
		if (OutClaimError)
		{
			*OutClaimError = MoveTemp(ClaimError);
		}
		return false;
	}

	return World.RestoreSnapshot(
		MoveTemp(Authority),
		Snapshot,
		FSeinSnapshotRestoreOptions(
			ESeinSnapshotLocalStateRestorePolicy::RestoreCaptured,
			ESeinSnapshotResumePolicy::ResumeImmediately));
}
