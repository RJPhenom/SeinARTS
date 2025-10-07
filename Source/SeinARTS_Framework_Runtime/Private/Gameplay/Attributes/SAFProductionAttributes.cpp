#include "Gameplay/Attributes/SAFProductionAttributes.h"
#include "Structs/SAFAttributesRow.h"
#include "Math/UnrealMathUtility.h"
#include "Net/UnrealNetwork.h"

// Production
// =========================================================================================================================
USAFProductionAttributes::USAFProductionAttributes() {
  InitBuildTime(1.f);
  InitBuildSpeed(1.f);
  InitCost1(0.f);
  InitCost2(0.f);
  InitCost3(0.f);
  InitCost4(0.f);
  InitCost5(0.f);
  InitCost6(0.f);
  InitCost7(0.f);
  InitCost8(0.f);
  InitCost9(0.f);
  InitCost10(0.f);
  InitCost11(0.f);
  InitCost12(0.f);
}

// Returns the cost attributes as a FSAFResources bundle.
FSAFResources USAFProductionAttributes::BundleResources(ESAFResourceRoundingPolicy Policy) const {
  FSAFResources Out;

  const float C1  = Cost1.GetCurrentValue();
  const float C2  = Cost2.GetCurrentValue();
  const float C3  = Cost3.GetCurrentValue();
  const float C4  = Cost4.GetCurrentValue();
  const float C5  = Cost5.GetCurrentValue();
  const float C6  = Cost6.GetCurrentValue();
  const float C7  = Cost7.GetCurrentValue();
  const float C8  = Cost8.GetCurrentValue();
  const float C9  = Cost9.GetCurrentValue();
  const float C10 = Cost10.GetCurrentValue();
  const float C11 = Cost11.GetCurrentValue();
  const float C12 = Cost12.GetCurrentValue();

  Out.Resource1  = FSAFResources::ToIntByPolicy(C1,  Policy);
  Out.Resource2  = FSAFResources::ToIntByPolicy(C2,  Policy);
  Out.Resource3  = FSAFResources::ToIntByPolicy(C3,  Policy);
  Out.Resource4  = FSAFResources::ToIntByPolicy(C4,  Policy);
  Out.Resource5  = FSAFResources::ToIntByPolicy(C5,  Policy);
  Out.Resource6  = FSAFResources::ToIntByPolicy(C6,  Policy);
  Out.Resource7  = FSAFResources::ToIntByPolicy(C7,  Policy);
  Out.Resource8  = FSAFResources::ToIntByPolicy(C8,  Policy);
  Out.Resource9  = FSAFResources::ToIntByPolicy(C9,  Policy);
  Out.Resource10 = FSAFResources::ToIntByPolicy(C10, Policy);
  Out.Resource11 = FSAFResources::ToIntByPolicy(C11, Policy);
  Out.Resource12 = FSAFResources::ToIntByPolicy(C12, Policy);

  return Out;
}

// Replication
// =========================================================================================================================
void USAFProductionAttributes::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const {
  Super::GetLifetimeReplicatedProps(OutLifetimeProps);

  // Production
  DOREPLIFETIME_CONDITION_NOTIFY(USAFProductionAttributes, BuildTime,      		COND_None, REPNOTIFY_Always);
  DOREPLIFETIME_CONDITION_NOTIFY(USAFProductionAttributes, BuildSpeed,     		COND_None, REPNOTIFY_Always);
  DOREPLIFETIME_CONDITION_NOTIFY(USAFProductionAttributes, Cost1,          		COND_None, REPNOTIFY_Always);
  DOREPLIFETIME_CONDITION_NOTIFY(USAFProductionAttributes, Cost2,          		COND_None, REPNOTIFY_Always);
  DOREPLIFETIME_CONDITION_NOTIFY(USAFProductionAttributes, Cost3,          		COND_None, REPNOTIFY_Always);
  DOREPLIFETIME_CONDITION_NOTIFY(USAFProductionAttributes, Cost4,          		COND_None, REPNOTIFY_Always);
  DOREPLIFETIME_CONDITION_NOTIFY(USAFProductionAttributes, Cost5,          		COND_None, REPNOTIFY_Always);
  DOREPLIFETIME_CONDITION_NOTIFY(USAFProductionAttributes, Cost6,          		COND_None, REPNOTIFY_Always);
  DOREPLIFETIME_CONDITION_NOTIFY(USAFProductionAttributes, Cost7,          		COND_None, REPNOTIFY_Always);
  DOREPLIFETIME_CONDITION_NOTIFY(USAFProductionAttributes, Cost8,          		COND_None, REPNOTIFY_Always);
  DOREPLIFETIME_CONDITION_NOTIFY(USAFProductionAttributes, Cost9,          		COND_None, REPNOTIFY_Always);
  DOREPLIFETIME_CONDITION_NOTIFY(USAFProductionAttributes, Cost10,         		COND_None, REPNOTIFY_Always);
  DOREPLIFETIME_CONDITION_NOTIFY(USAFProductionAttributes, Cost11,         		COND_None, REPNOTIFY_Always);
  DOREPLIFETIME_CONDITION_NOTIFY(USAFProductionAttributes, Cost12,         		COND_None, REPNOTIFY_Always);
}
