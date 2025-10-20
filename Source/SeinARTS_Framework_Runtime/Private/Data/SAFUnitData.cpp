#include "Data/SAFUnitData.h"

const FPrimaryAssetType USAFUnitData::PrimaryAssetType(TEXT("Unit"));

// // Returns an FSAFResourceBundle bundle for the default cost profile of the associated unit, using
// // the associated data table row if set, a zero-bundle if not.
// FSAFResourceBundle USAFUnitData::GetDefaultCosts(ESAFResourceRoundingPolicy Policy) const {
//   FSAFResourceBundle Out;

//   if (const FSAFAttributesRow* Row = AttributeRow.GetRow<FSAFAttributesRow>(TEXT("GetDefaultCosts"))) {
//     Out.Resource1  = FSAFResourceBundle::ToIntByPolicy(Row->Cost1,  Policy);
//     Out.Resource2  = FSAFResourceBundle::ToIntByPolicy(Row->Cost2,  Policy);
//     Out.Resource3  = FSAFResourceBundle::ToIntByPolicy(Row->Cost3,  Policy);
//     Out.Resource4  = FSAFResourceBundle::ToIntByPolicy(Row->Cost4,  Policy);
//     Out.Resource5  = FSAFResourceBundle::ToIntByPolicy(Row->Cost5,  Policy);
//     Out.Resource6  = FSAFResourceBundle::ToIntByPolicy(Row->Cost6,  Policy);
//     Out.Resource7  = FSAFResourceBundle::ToIntByPolicy(Row->Cost7,  Policy);
//     Out.Resource8  = FSAFResourceBundle::ToIntByPolicy(Row->Cost8,  Policy);
//     Out.Resource9  = FSAFResourceBundle::ToIntByPolicy(Row->Cost9,  Policy);
//     Out.Resource10 = FSAFResourceBundle::ToIntByPolicy(Row->Cost10, Policy);
//     Out.Resource11 = FSAFResourceBundle::ToIntByPolicy(Row->Cost11, Policy);
//     Out.Resource12 = FSAFResourceBundle::ToIntByPolicy(Row->Cost12, Policy);
//   }

//   return Out;
// }

// FSAFResourceBundle USAFUnitData::GetRuntimeCosts(const APlayerState* PlayerState, ESAFResourceRoundingPolicy Policy) const {
//   FSAFResourceBundle Out;

//   const FSAFAttributesRow* Row = AttributeRow.GetRow<FSAFAttributesRow>(TEXT("GetEffectiveCosts"));
//   if (!Row) return Out;

//   auto Res = [&](const FGameplayAttribute& Attr, float Base) {
//     return SAFAttributeResolver::ResolveAttribute(PlayerState, this, Attr, Base);
//   };

//   Out.Resource1  = FSAFResourceBundle::ToIntByPolicy(Res(USAFAttributes::GetCost1Attribute(),  Row->Cost1),  Policy);
//   Out.Resource2  = FSAFResourceBundle::ToIntByPolicy(Res(USAFAttributes::GetCost2Attribute(),  Row->Cost2),  Policy);
//   Out.Resource3  = FSAFResourceBundle::ToIntByPolicy(Res(USAFAttributes::GetCost3Attribute(),  Row->Cost3),  Policy);
//   Out.Resource4  = FSAFResourceBundle::ToIntByPolicy(Res(USAFAttributes::GetCost4Attribute(),  Row->Cost4),  Policy);
//   Out.Resource5  = FSAFResourceBundle::ToIntByPolicy(Res(USAFAttributes::GetCost5Attribute(),  Row->Cost5),  Policy);
//   Out.Resource6  = FSAFResourceBundle::ToIntByPolicy(Res(USAFAttributes::GetCost6Attribute(),  Row->Cost6),  Policy);
//   Out.Resource7  = FSAFResourceBundle::ToIntByPolicy(Res(USAFAttributes::GetCost7Attribute(),  Row->Cost7),  Policy);
//   Out.Resource8  = FSAFResourceBundle::ToIntByPolicy(Res(USAFAttributes::GetCost8Attribute(),  Row->Cost8),  Policy);
//   Out.Resource9  = FSAFResourceBundle::ToIntByPolicy(Res(USAFAttributes::GetCost9Attribute(),  Row->Cost9),  Policy);
//   Out.Resource10 = FSAFResourceBundle::ToIntByPolicy(Res(USAFAttributes::GetCost10Attribute(), Row->Cost10), Policy);
//   Out.Resource11 = FSAFResourceBundle::ToIntByPolicy(Res(USAFAttributes::GetCost11Attribute(), Row->Cost11), Policy);
//   Out.Resource12 = FSAFResourceBundle::ToIntByPolicy(Res(USAFAttributes::GetCost12Attribute(), Row->Cost12), Policy);

//   return Out;
// }