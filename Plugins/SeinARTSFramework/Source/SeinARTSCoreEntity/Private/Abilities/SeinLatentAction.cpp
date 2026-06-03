#include "Abilities/SeinLatentAction.h"

void USeinLatentAction::Complete()
{
	bCompleted = true;
}

void USeinLatentAction::Cancel()
{
	bCancelled = true;
	OnCancel();
}

void USeinLatentAction::Fail(uint8 ReasonCode)
{
	bFailed = true;
	bCompleted = true;
	FailureReason = ReasonCode;
	OnFail(ReasonCode);
}
