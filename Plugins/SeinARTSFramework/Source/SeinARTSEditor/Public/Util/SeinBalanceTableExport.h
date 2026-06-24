/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinBalanceTableExport.h
 * @brief   Phase-B Gather engine for the balance-table generator. Builds/syncs a flat
 *          DataTable (with a paired row-struct UDS) from a profile's matched entities'
 *          tracked-component fields. Mirrors the UDS field-sync idiom of
 *          SeinMovementTuningExport, generalized from one BP's variables to the union of
 *          N component structs across many entity classes. Editor-only; user-triggered.
 */

#pragma once

#include "CoreMinimal.h"

class USeinBalanceProfile;
class UDataTable;

namespace SeinBalanceTable
{
	/** Gather the profile's matched entities' tracked-component fields into a flat DataTable,
	 *  creating/syncing a paired row-struct UDS (rename-safe via a per-field source stamp).
	 *
	 *  First Gather fills every cell from the authored ComponentData. Re-Gather syncs the
	 *  schema (the engine migrates existing row data across the struct change, preserving
	 *  edits and renamed columns) and fills only new rows / new columns, leaving existing
	 *  edited cells intact. Rows for no-longer-matched classes are dropped.
	 *
	 *  Returns the table (opened in the asset editor), or null on failure / empty scope. */
	UDataTable* GatherToTable(USeinBalanceProfile* Profile);

	/** Push edited table values back into the source unit Blueprints' authored ComponentData
	 *  (the write-back half of the round-trip). Only cells that differ from the current authored
	 *  value are written — so an unedited gather→push is a no-op and never perturbs fixed-point
	 *  values. Fixed-point columns convert float→FFixedPoint via FromFloat. Identity columns are
	 *  display-only (never pushed). Modal-confirmed; marks touched Blueprints dirty (the user saves).
	 *  Returns the number of changed values written, or -1 if cancelled / no table. `OutSkippedCells`
	 *  receives the count of cells that couldn't be written (component not present on that unit).
	 *  Editor-only. */
	int32 PushToEntities(USeinBalanceProfile* Profile, int32& OutSkippedCells);

	/** Compare the generated table to the live source WITHOUT writing anything. Returns the number of
	 *  tuning cells whose table value differs from the current authored value (`OutCellsChecked` = total
	 *  compared); >0 means the table has unpushed edits or a source Blueprint changed since the last
	 *  Gather. Returns -1 if there is no generated table. Editor-only. */
	int32 CheckSync(USeinBalanceProfile* Profile, int32& OutCellsChecked);
}
