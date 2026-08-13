/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file         SeinNavigationAStarDiagnostics.inl
 * @author       RJ Macklem
 * @created      13 Aug 2026
 * @latest       13 Aug 2026
 * @brief        Implements non-shipping A* path diagnostics.
 *
 *               Included once by SeinNavigationAStar.cpp so the reporters
 *               retain access to its private grid-walk implementation.
 *
 * @disclaimer   This code was generated in whole or in part with the assistance
 *               of an AI language model.
 */

#pragma once

void USeinNavigationAStar::ReportAStarPartial(
	FIntPoint Start,
	FIntPoint End,
	int32 StartIdx,
	int32 EndIdx,
	int32 BestCellIdx,
	int32 Iterations,
	int32 IterCap,
	int32 RequiredClearance,
	FAStarScratch& Scratch) const
{
	// Diagnostic: WHY did A* give up? Iter cap means "ran out of work budget,
	// might still be reachable" — likely fix is bumping AStarMaxIterations.
	// Exhaustion means "literally no C-space path exists to End" — destination
	// is disconnected from start in the agent's C-space (different connected
	// component, or End cell itself is WD < RequiredClearance). Different bugs,
	// different fixes. Gated on Verbose activity so the EndWD lookup + format
	// is skipped when the channel is off.
	if (UE_LOG_ACTIVE(LogSeinNavigationAStar, Verbose))
	{
		const bool bIterCapHit = (Iterations >= IterCap);
		const int32 EndWD = WallDistance.IsValidIndex(EndIdx)
			? static_cast<int32>(WallDistance[EndIdx]) : -1;
		UE_LOG(LogSeinNavigationAStar, Verbose,
			TEXT("  AStarSearch partial: reason=%s  Iterations=%d/%d  EndCell=(%d,%d) WD=%d  RequiredClearance=%d  BestH cell idx=%d"),
			bIterCapHit ? TEXT("ITER_CAP") : TEXT("OPEN_LIST_EXHAUSTED"),
			Iterations, IterCap,
			End.X, End.Y, EndWD,
			RequiredClearance,
			BestCellIdx);
	}

	// "Stuck at start" diagnostic — fires at Warning level when A* couldn't
	// expand AT ALL from the start cell (BestCellIdx == StartIdx). This is
	// the case that surfaces as the "0 drivable segments / Moving →
	// Completed → Cancelled" instant-no-op behavior on the action side.
	// Walks the 8 neighbors of Start and logs each one's rejection reason
	// so the root cause is visible without enabling Verbose: was it the
	// connection bit (slope/step gate), the passability check (wall or
	// dynamic blocker), or the clearance gate (Required > NeighborWD with
	// no strict-improvement escape available)? Gated on Warning activity AND
	// the stuck-at-start condition before any neighbor WD ring scans run.
	if (BestCellIdx == StartIdx && Iterations > 0
		&& UE_LOG_ACTIVE(LogSeinNavigationAStar, Warning))
	{
		const int32 StartWD = WallDistance.IsValidIndex(StartIdx)
			? static_cast<int32>(WallDistance[StartIdx]) : -1;
		const uint8 StartConn = (CellConnections.IsValidIndex(StartIdx))
			? CellConnections[StartIdx] : 0;
		const int32 EffWD0 = (RequiredClearance > 0)
			? GetEffectiveWD(Start.X, Start.Y, RequiredClearance, Scratch) : 0;
		// Mirrors the search's RequiredFromHere (non-decreasing escape rule).
		const int32 RequiredFromStart = (RequiredClearance > 0)
			? FMath::Min(RequiredClearance, EffWD0) : 0;

		FString NeighborReport;
		static const TCHAR* NDName[8] = {
			TEXT("E"), TEXT("W"), TEXT("N"), TEXT("S"),
			TEXT("NE"), TEXT("SE"), TEXT("NW"), TEXT("SW")
		};
		for (int32 n = 0; n < 8; ++n)
		{
			const int32 NX = Start.X + SeinNeighborDX[n];
			const int32 NY = Start.Y + SeinNeighborDY[n];
			NeighborReport += FString::Printf(
				TEXT("  %s(%d,%d): "), NDName[n], NX, NY);

			if (!IsValidCoord(NX, NY))
			{
				NeighborReport += TEXT("OUT_OF_BOUNDS\n");
				continue;
			}
			const bool bConnOk = (StartConn & (1 << n)) != 0;
			const bool bPassable =
				IsCellPassableForPath(NX, NY, Scratch);
			const int32 NIdx = CellIndex(NX, NY);
			const int32 NWD = (RequiredClearance > 0)
				? GetEffectiveWD(NX, NY, RequiredClearance, Scratch)
				: (WallDistance.IsValidIndex(NIdx)
					? static_cast<int32>(WallDistance[NIdx]) : -1);
			const bool bClearanceOk = (RequiredClearance == 0)
				|| (NWD >= RequiredFromStart);

			// Diagonal anti-squeeze (matches A*'s expansion gate for n>=4).
			// A diagonal step IS rejected if either flanking cardinal cell
			// fails its connection bit OR its clearance check — A* refuses
			// to slip a chassis through a wall corner via a diagonal even
			// when the diagonal target cell itself looks fine.
			bool bDiagSqueezeOk = true;
			if (n >= 4)
			{
				const uint8 AIdx = SeinDiagCardinalA[n - 4];
				const uint8 BIdx = SeinDiagCardinalB[n - 4];
				if ((StartConn & (1 << AIdx)) == 0
					|| (StartConn & (1 << BIdx)) == 0)
				{
					bDiagSqueezeOk = false;
				}
				else if (RequiredClearance > 0)
				{
					const int32 CardAX = Start.X + SeinNeighborDX[AIdx];
					const int32 CardAY = Start.Y + SeinNeighborDY[AIdx];
					const int32 CardBX = Start.X + SeinNeighborDX[BIdx];
					const int32 CardBY = Start.Y + SeinNeighborDY[BIdx];
					if (IsValidCoord(CardAX, CardAY)
						&& IsValidCoord(CardBX, CardBY))
					{
						const int32 CardAWD = GetEffectiveWD(
							CardAX, CardAY, RequiredClearance, Scratch);
						const int32 CardBWD = GetEffectiveWD(
							CardBX, CardBY, RequiredClearance, Scratch);
						if (CardAWD < RequiredFromStart
							|| CardBWD < RequiredFromStart)
						{
							bDiagSqueezeOk = false;
						}
					}
				}
			}

			const TCHAR* Verdict =
				(bConnOk && bPassable && bClearanceOk && bDiagSqueezeOk)
					? TEXT("ALLOWED (unexpected)")
					: (!bConnOk
						? TEXT("BLOCKED_BY_CONNECTION_BIT")
						: !bPassable
							? TEXT("BLOCKED_BY_PASSABILITY")
							: !bClearanceOk
								? TEXT("BLOCKED_BY_CLEARANCE_GATE")
								: TEXT("BLOCKED_BY_DIAGONAL_SQUEEZE"));

			NeighborReport += FString::Printf(
				TEXT("conn=%d passable=%d WD=%d need=%d → %s\n"),
				bConnOk ? 1 : 0,
				bPassable ? 1 : 0,
				NWD,
				RequiredFromStart,
				Verdict);
		}

		UE_LOG(LogSeinNavigationAStar, Warning,
			TEXT("A* stuck at start: cell=(%d,%d) WD=%d EffWD=%d RequiredClearance=%d "
			     "RequiredFromStart=%d ConnMask=0x%02X. Neighbors:\n%s"),
			Start.X, Start.Y, StartWD, EffWD0, RequiredClearance,
			RequiredFromStart, StartConn, *NeighborReport);
	}
}

void USeinNavigationAStar::ReportUnreachableSegments(
	const FSeinPathRequest& Request,
	const FSeinPath& OutPath,
	FAStarScratch& Scratch) const
{
	// After all path-pipeline stages (A* topology, smoother LoS-pull, push, segment
	// derivation), walk every waypoint-to-waypoint segment via HasLineOfSight
	// (clearance=0) — passability + connection bits only. A failure here means
	// SOMETHING in the pipeline emitted a segment the planner's own rules consider
	// unreachable: chassis would try to drive through a wall along that line.
	//
	// On failure, replay the Bresenham trace (via the shared WalkGridLine primitive)
	// to log WHICH cell + step bit + diagonal anti-squeeze rejected the line. Pure
	// observation — never modifies OutPath. Stripped in shipping; gated on Warning.
	//
	// This is the diagnostic of last resort for "occasional bad-path flash" bugs
	// where the visible flash is too brief to step-through with a debugger but the
	// log can capture the smoking gun.
	if (!UE_LOG_ACTIVE(LogSeinNavigationAStar, Warning)) return;

	for (int32 i = 0; i + 1 < OutPath.Waypoints.Num(); ++i)
	{
		int32 AX, AY, BX, BY;
		if (!WorldToGrid(OutPath.Waypoints[i], AX, AY)) continue;
		if (!WorldToGrid(OutPath.Waypoints[i + 1], BX, BY)) continue;
		if (AX == BX && AY == BY) continue;

		if (HasLineOfSight(AX, AY, BX, BY, Scratch, 0)) continue;

		FString Trace;
		int32 Step = 0;
		const ESeinGridWalk Walk = WalkGridLine(AX, AY, BX, BY,
			[&](int32 CurX, int32 CurY) -> bool
			{
				if (!IsValidCoord(CurX, CurY))
				{
					Trace += FString::Printf(
						TEXT("[%d] cell(%d,%d) OUT_OF_BOUNDS — REJECTED\n"),
						Step, CurX, CurY);
					return false;
				}
				if (!IsCellPassableForPath(CurX, CurY, Scratch))
				{
					const int32 CIdx = CellIndex(CurX, CurY);
					const uint8 CC =
						CellCost.IsValidIndex(CIdx) ? CellCost[CIdx] : 0;
					Trace += FString::Printf(
						TEXT("[%d] cell(%d,%d) CellCost=%d IsCellPassableForPath=false — REJECTED\n"),
						Step, CurX, CurY, CC);
					return false;
				}
				return true;
			},
			[&](
				int32 CurX,
				int32 CurY,
				int32 NextX,
				int32 NextY,
				int32 DirIdx) -> bool
			{
				if (DirIdx >= 0)
				{
					const int32 CIdx = CellIndex(CurX, CurY);
					const uint8 CC = CellConnections.IsValidIndex(CIdx)
						? CellConnections[CIdx] : 0;
					const bool bBitSet = (CC & (1 << DirIdx)) != 0;
					bool bDiagOk = true;
					int32 ABit = -1;
					int32 BBit = -1;
					if (DirIdx >= 4)
					{
						ABit = SeinDiagCardinalA[DirIdx - 4];
						BBit = SeinDiagCardinalB[DirIdx - 4];
						bDiagOk = (CC & (1 << ABit)) != 0
							&& (CC & (1 << BBit)) != 0;
					}
					Trace += FString::Printf(
						TEXT("[%d] cell(%d,%d)→(%d,%d) dir=%d connMask=0x%02X bitSet=%d diagOk=%d (cardA=%d cardB=%d)\n"),
						Step, CurX, CurY, NextX, NextY, DirIdx, CC,
						bBitSet ? 1 : 0, bDiagOk ? 1 : 0, ABit, BBit);
					if (!bBitSet || !bDiagOk)
					{
						Trace += TEXT(
							"    REJECTED HERE (step bit unset or diagonal squeeze)\n");
						return false;
					}
				}
				++Step;
				return true;
			});

		if (Walk == ESeinGridWalk::ReachedEnd)
		{
			Trace += FString::Printf(
				TEXT("[%d] cell(%d,%d) REACHED ENDPOINT — should have returned true; investigate\n"),
				Step, BX, BY);
		}

		UE_LOG(LogSeinNavigationAStar, Warning,
			TEXT("FindPath: emitted path has UNREACHABLE segment from "
			     "WP[%d]=(%.1f,%.1f) cell=(%d,%d) to WP[%d]=(%.1f,%.1f) cell=(%d,%d) "
			     "— chassis may try to drive through a wall along this line. "
			     "Path total waypoints=%d, Request.End=(%.1f,%.1f), bIsPartial=%d. "
			     "Bresenham trace:\n%s"),
			i, OutPath.Waypoints[i].X.ToFloat(),
			OutPath.Waypoints[i].Y.ToFloat(), AX, AY,
			i + 1, OutPath.Waypoints[i + 1].X.ToFloat(),
			OutPath.Waypoints[i + 1].Y.ToFloat(), BX, BY,
			OutPath.Waypoints.Num(),
			Request.End.X.ToFloat(), Request.End.Y.ToFloat(),
			OutPath.bIsPartial ? 1 : 0, *Trace);
	}
}

void USeinNavigationAStar::ReportCellPathClearance(
	const TArray<FIntPoint>& CellPath,
	const FSeinPath& OutPath,
	int32 RequiredClearance,
	bool bPartial) const
{
	// Dump WD values along the cell-path AND smoothed waypoints. If any cell on
	// either has WD < RequiredClearance (excluding the start cell, exempt during
	// escape), the gating broke somewhere. The MIN value tells us the worst-case
	// clearance — should be >= RequiredClearance for a properly-gated path. Gated
	// on Verbose so the grid reads + formatting are skipped when the channel is off.
	if (!UE_LOG_ACTIVE(LogSeinNavigationAStar, Verbose)) return;

	int32 MinCellPathWD = INT32_MAX;
	for (int32 i = 1; i < CellPath.Num(); ++i)
	{
		const int32 Idx = CellIndex(CellPath[i].X, CellPath[i].Y);
		if (WallDistance.IsValidIndex(Idx))
		{
			const int32 WD = static_cast<int32>(WallDistance[Idx]);
			if (WD < MinCellPathWD) MinCellPathWD = WD;
		}
	}
	int32 MinWaypointWD = INT32_MAX;
	for (int32 i = 0; i < OutPath.Waypoints.Num(); ++i)
	{
		int32 WX, WY;
		if (WorldToGrid(OutPath.Waypoints[i], WX, WY))
		{
			const int32 Idx = CellIndex(WX, WY);
			if (WallDistance.IsValidIndex(Idx))
			{
				const int32 WD = static_cast<int32>(WallDistance[Idx]);
				if (WD < MinWaypointWD) MinWaypointWD = WD;
			}
		}
	}
	UE_LOG(LogSeinNavigationAStar, Verbose,
		TEXT("FindCellPath: RequiredClearance=%d  CellPath: %d cells, min WD (excl. start) = %d   "
		     "Smoothed waypoints: %d, min WD = %d   bPartial=%d"),
		RequiredClearance, CellPath.Num(),
		(MinCellPathWD == INT32_MAX ? -1 : MinCellPathWD),
		OutPath.Waypoints.Num(),
		(MinWaypointWD == INT32_MAX ? -1 : MinWaypointWD),
		bPartial ? 1 : 0);

	for (int32 i = 0; i < OutPath.Waypoints.Num(); ++i)
	{
		int32 WX = -1;
		int32 WY = -1;
		int32 WD = -1;
		if (WorldToGrid(OutPath.Waypoints[i], WX, WY))
		{
			const int32 Idx = CellIndex(WX, WY);
			if (WallDistance.IsValidIndex(Idx))
			{
				WD = static_cast<int32>(WallDistance[Idx]);
			}
		}
		UE_LOG(LogSeinNavigationAStar, Verbose,
			TEXT("    WP[%d] = (%.1f, %.1f) cell=(%d,%d) WD=%d"),
			i,
			OutPath.Waypoints[i].X.ToFloat(),
			OutPath.Waypoints[i].Y.ToFloat(),
			WX, WY, WD);
	}
}
