/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file         SeinTagAudit.h
 * @author       RJ Macklem
 * @created      7 Sep 2026
 * @latest       7 Sep 2026
 * @brief        Explains generated tag ownership and conservatively reconciles unused entries.
 * @disclaimer   Generated with assistance from an AI language model.
 */
#pragma once
#include "CoreMinimal.h"

struct SEINARTSEDITOR_API FSeinTagAuditRow
{
	FName Tag;
	TArray<FString> Owners;
	FString Reason;
	bool bCanDelete = false;
};

namespace SeinTagAudit
{
	SEINARTSEDITOR_API void ShowWindow();
	/** Read a report without altering tags or assets. Unknown ownership is retained. */
	SEINARTSEDITOR_API TArray<FSeinTagAuditRow> Inspect();
	/** Recheck every requested candidate before removal. Returns a human-readable receipt. */
	SEINARTSEDITOR_API FString Cleanup(const TArray<FName>& Candidates);
	SEINARTSEDITOR_API FString Format(const TArray<FSeinTagAuditRow>& Rows);
	/** Redirect endpoints are retained across every source, including the project defaults. */
	SEINARTSEDITOR_API bool IsRedirectEndpoint(FName Tag);
	/** Require stopped PIE, complete discovery and saved content before dictionary removal/migration. */
	SEINARTSEDITOR_API FString MutationBlocker();
#if WITH_DEV_AUTOMATION_TESTS
	/** Same audit against an isolated test INI source; never repoints the production dictionary. */
	SEINARTSEDITOR_API TArray<FSeinTagAuditRow> InspectForAutomation(FName SourceName);
	SEINARTSEDITOR_API FString CleanupForAutomation(FName SourceName, const TArray<FName>& Candidates);
#endif
}
