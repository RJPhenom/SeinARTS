#include "Gameplay/Attributes/SAFProductionAttributes.h"
#include "Structs/SAFAttributesRow.h"
#include "Math/UnrealMathUtility.h"
#include "Net/UnrealNetwork.h"

// Production
// ==================================================================================================
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

// Returns the cost attributes as a FSAFResourceBundle bundle.
FSAFResourceBundle USAFProductionAttributes::BundleResources() const {
	FSAFResourceBundle Out;

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

	Out.Resource1  = FSAFResourceBundle::ToInt(C1);
	Out.Resource2  = FSAFResourceBundle::ToInt(C2);
	Out.Resource3  = FSAFResourceBundle::ToInt(C3);
	Out.Resource4  = FSAFResourceBundle::ToInt(C4);
	Out.Resource5  = FSAFResourceBundle::ToInt(C5);
	Out.Resource6  = FSAFResourceBundle::ToInt(C6);
	Out.Resource7  = FSAFResourceBundle::ToInt(C7);
	Out.Resource8  = FSAFResourceBundle::ToInt(C8);
	Out.Resource9  = FSAFResourceBundle::ToInt(C9);
	Out.Resource10 = FSAFResourceBundle::ToInt(C10);
	Out.Resource11 = FSAFResourceBundle::ToInt(C11);
	Out.Resource12 = FSAFResourceBundle::ToInt(C12);

	return Out;
}

// Replication
// ==================================================================================================
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
