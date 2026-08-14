/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinBalanceTableExport.h
 * @brief   Gather, write-back, and drift-check engine for Balance Data assets.
 *          Builds a flat DataTable and paired row-struct UDS from matched entity
 *          components or abilities. Editor-only and user-triggered.
 */

#pragma once

#include "CoreMinimal.h"

class USeinBalanceProfile;
class UDataTable;

namespace SeinBalanceTable
{
	/** Gather the profile's matched entity components or ability fields into a flat DataTable,
	 *  creating/syncing a paired row-struct UDS (rename-safe via a per-field source stamp).
	 *
	 *  Gather rebuilds every row from the authored source. Re-Gather is destructive and
	 *  confirms before discarding edits made directly in the generated table. The row UDS
	 *  is reconciled by stable source identity, and rows for no-longer-matched classes are
	 *  dropped.
	 *
	 *  Returns the table (opened in the asset editor), or null on failure / empty scope. */
	SEINARTSEDITOR_API UDataTable* GatherToTable(USeinBalanceProfile* Profile);

	/** Push edited table values back into the matched source Blueprints
	 *  (the write-back half of the round-trip). Only cells that differ from the current authored
	 *  value are written — so an unedited gather→push is a no-op and never perturbs fixed-point
	 *  values. Fixed-point columns convert float→FFixedPoint via FromFloat. Identity columns are
	 *  display-only (never pushed). Modal-confirmed; marks touched Blueprints dirty (the user saves).
	 *  Returns the number of changed values written, or -1 if cancelled, structurally stale, or an
	 *  unexpected source/schema error occurs. `OutSkippedCells` receives the write-error count;
	 *  union columns that do not apply to a particular target are ignored rather than treated as errors.
	 *  Editor-only. */
	SEINARTSEDITOR_API int32 PushToEntities(
		USeinBalanceProfile* Profile,
		int32& OutSkippedCells);

	/** Compare the generated table to the live source WITHOUT writing anything. Returns the number of
	 *  tuning cells whose table value differs from the current authored value (`OutCellsChecked` = total
	 *  compared). A stale row or column schema returns 1 with no partial comparison, requiring Gather
	 *  before Push. Returns -1 if there is no generated table. Editor-only. */
	SEINARTSEDITOR_API int32 CheckSync(
		USeinBalanceProfile* Profile,
		int32& OutCellsChecked);

#if WITH_DEV_AUTOMATION_TESTS
	namespace Testing
	{
		/** Execute Gather without modal confirmation or opening an asset editor. */
		SEINARTSEDITOR_API UDataTable* GatherToTableWithoutUI(
			USeinBalanceProfile* Profile);

		/** Execute Push without modal confirmation. */
		SEINARTSEDITOR_API int32 PushToEntitiesWithoutUI(
			USeinBalanceProfile* Profile,
			int32& OutSkippedCells);
	}
#endif
}
