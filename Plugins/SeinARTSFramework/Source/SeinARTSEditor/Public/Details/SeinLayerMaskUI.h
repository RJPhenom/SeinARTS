/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinLayerMaskUI.h
 * @brief   Shared helper for rendering uint8 bitmask properties as a labeled
 *          combo dropdown — used by every layer-mask field that needs its
 *          per-bit labels resolved against plugin settings (named nav layers,
 *          named vision layers) rather than the enum's hardcoded UMETA names.
 */

#pragma once

#include "CoreMinimal.h"

class IDetailChildrenBuilder;
class IPropertyHandle;
class SWidget;

namespace SeinLayerMaskUI
{
	/** Resolve labels for nav layer bits 0..7. Bit 0 → "Default" (framework
	 *  reserved), bits 1..7 → `USeinARTSCoreSettings::NavLayers[bit-1]`'s
	 *  LayerName when enabled+named, else "(unused)". */
	SEINARTSEDITOR_API FText GetNavLayerLabel(int32 BitIdx);

	/** Resolve labels for fog-of-war layer bits 0..7. Bit 0 → "Explored",
	 *  bit 1 → "Visible" (framework reserved), bits 2..7 →
	 *  `USeinARTSCoreSettings::VisionLayers[bit-2]`'s LayerName when
	 *  enabled+named, else "(unused)". */
	SEINARTSEDITOR_API FText GetFogLayerLabel(int32 BitIdx);

	/** Add a custom detail row for a uint8 bitmask field. The row shows a
	 *  combo button whose summary text is the comma-joined names of set
	 *  bits, and whose menu is one checkbox per bit with `GetBitLabel(N)`
	 *  as the label.
	 *
	 *  The label getter is captured by value into the menu lambdas so the
	 *  caller can pass a local static or a free function pointer-converted
	 *  to TFunction. Re-evaluated on every menu open + summary refresh,
	 *  so renaming a layer in settings updates the dropdown without an
	 *  editor restart. */
	SEINARTSEDITOR_API void AddLayerMaskRow(
		TSharedRef<IPropertyHandle> MaskHandle,
		IDetailChildrenBuilder& ChildBuilder,
		TFunction<FText(int32 BitIdx)> GetBitLabel,
		const FText& RowFilterText);
}
