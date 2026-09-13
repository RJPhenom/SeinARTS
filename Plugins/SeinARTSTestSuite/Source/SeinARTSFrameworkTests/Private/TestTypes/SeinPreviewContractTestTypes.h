#pragma once
#include "Targeter/SeinTargeterPreview.h"
#include "SeinPreviewContractTestTypes.generated.h"
UCLASS()
class ASeinPreviewContractTestActor : public ASeinTargeterPreview
{
 GENERATED_BODY()
public:
 int32 Initialized = 0, Changed = 0, Updated = 0, Ended = 0;
 ESeinPreviewEndReason EndReason = ESeinPreviewEndReason::Unavailable;
 bool bEndDuringInitialize = false;
 FSeinTargeterPreviewContext InitialContext;
 virtual void OnPreviewInitialized_Implementation() override
 {
  ++Initialized; InitialContext = Context;
  if (bEndDuringInitialize) EndPreview(ESeinPreviewEndReason::Cancelled);
 }
 virtual void OnValidityChanged_Implementation(ESeinTargeterValidity Previous) override { ++Changed; }
 virtual void OnPreviewUpdated_Implementation() override { ++Updated; }
 virtual void OnPreviewEnded_Implementation(ESeinPreviewEndReason Reason) override { ++Ended; EndReason = Reason; }
};
