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
}
