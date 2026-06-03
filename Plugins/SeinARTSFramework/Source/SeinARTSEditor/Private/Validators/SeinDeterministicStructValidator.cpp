/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinDeterministicStructValidator.cpp
 * @brief   Post-selection enforcement of the determinism whitelist for Sein UDSes.
 */

#include "Validators/SeinDeterministicStructValidator.h"
#include "Factories/SeinSimComponentFactory.h"
#include "UserDefinedStructure/UserDefinedStructEditorData.h"
#include "EdGraphSchema_K2.h"
#include "StructUtils/UserDefinedStruct.h"
#include "UObject/Class.h"
#include "UObject/UnrealType.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Logging/LogMacros.h"

#define LOCTEXT_NAMESPACE "SeinDeterministicStructValidator"

DEFINE_LOG_CATEGORY_STATIC(LogSeinUDSValidator, Log, All);

static bool IsPinTypeDeterministic(const FEdGraphPinType& PinType)
{
	const FName Cat = PinType.PinCategory;

	// Whitelist of pin categories safe for deterministic sim data.
	if (Cat == UEdGraphSchema_K2::PC_Boolean
	 || Cat == UEdGraphSchema_K2::PC_Byte
	 || Cat == UEdGraphSchema_K2::PC_Int
	 || Cat == UEdGraphSchema_K2::PC_Int64
	 || Cat == UEdGraphSchema_K2::PC_Name
	 || Cat == UEdGraphSchema_K2::PC_Enum)
	{
		return true;
	}

	// Struct fields: only allow structs marked SeinDeterministic (both native
	// USTRUCTs with the meta and UDSes tagged by the factory).
	if (Cat == UEdGraphSchema_K2::PC_Struct)
	{
		const UStruct* SubStruct = Cast<UStruct>(PinType.PinSubCategoryObject.Get());
		return USeinSimComponentFactory::IsSeinDeterministicStruct(SubStruct);
	}

	// Everything else (float/double/real, object/class/interface refs, soft
	// refs, delegates, string/text, wildcard, fieldpath) is rejected.
	return false;
}

void FSeinDeterministicStructValidator::PreChange(const UUserDefinedStruct* /*Changed*/, FStructureEditorUtils::EStructureEditorChangeInfo /*ChangeInfo*/)
{
	// No-op; validation happens after the change is applied so we can inspect
	// the current variable list and remove whatever failed the whitelist.
}

void FSeinDeterministicStructValidator::PostChange(const UUserDefinedStruct* Changed, FStructureEditorUtils::EStructureEditorChangeInfo ChangeInfo)
{
	if (!Changed || !USeinSimComponentFactory::IsSeinDeterministicStruct(Changed))
	{
		return;
	}

	// Only react to the transitions that can introduce a bad type.
	if (ChangeInfo != FStructureEditorUtils::AddedVariable
	 && ChangeInfo != FStructureEditorUtils::VariableTypeChanged)
	{
		return;
	}

	UUserDefinedStruct* Mutable = const_cast<UUserDefinedStruct*>(Changed);
	const TArray<FStructVariableDescription>& Vars = FStructureEditorUtils::GetVarDesc(Mutable);

	// Snapshot guids first — RemoveVariable mutates the var-desc list.
	TArray<FGuid> ToRemove;
	TArray<FString> BadSummaries;
	for (const FStructVariableDescription& Var : Vars)
	{
		if (!IsPinTypeDeterministic(Var.ToPinType()))
		{
			ToRemove.Add(Var.VarGuid);
			BadSummaries.Add(FString::Printf(TEXT("%s : %s"),
				*Var.VarName.ToString(),
				*Var.Category.ToString()));
		}
	}

	if (ToRemove.Num() == 0)
	{
		return;
	}

	for (const FGuid& Guid : ToRemove)
	{
		FStructureEditorUtils::RemoveVariable(Mutable, Guid);
	}

	const FString Joined = FString::Join(BadSummaries, TEXT(", "));
	UE_LOG(LogSeinUDSValidator, Warning,
		TEXT("[Sein UDS '%s'] Removed %d non-deterministic field(s): %s. Allowed: bool, integer types, FName, enums, and SeinDeterministic-marked structs (including FFixedPoint/FFixedVector/...)."),
		*Changed->GetName(), ToRemove.Num(), *Joined);

	FNotificationInfo Info(FText::Format(
		LOCTEXT("RemovedFieldsTitle", "Sein component '{0}': removed non-deterministic field(s)"),
		FText::FromString(Changed->GetName())));
	Info.SubText = FText::Format(
		LOCTEXT("RemovedFieldsSub", "{0} — use FFixedPoint or other SeinDeterministic types."),
		FText::FromString(Joined));
	Info.ExpireDuration = 5.0f;
	Info.bUseThrobber = false;
	Info.bUseSuccessFailIcons = true;

	TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info);
	if (Item.IsValid())
	{
		Item->SetCompletionState(SNotificationItem::CS_Fail);
	}
}

#undef LOCTEXT_NAMESPACE
