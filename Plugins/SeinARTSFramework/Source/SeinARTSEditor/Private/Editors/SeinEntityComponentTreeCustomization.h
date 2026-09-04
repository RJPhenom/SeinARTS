/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file         SeinEntityComponentTreeCustomization.h
 * @author       RJ Macklem
 * @created      4 Sep 2026
 * @latest       4 Sep 2026
 * @brief        Editor-only visual grouping of entity authoring components
 *               beneath the owning actor's entity bridge.
 *
 *               The customization changes only Slate tree-node parentage.
 *               Unreal component ownership, SCS data, attachment, and runtime
 *               behavior remain untouched.
 *
 * @disclaimer   This code was generated in whole or in part with the assistance
 *               of an AI language model.
 */

#pragma once

#include "Containers/Ticker.h"
#include "ISCSEditorUICustomization.h"
#include "Templates/SharedPointer.h"

class SSubobjectEditor;
struct FSubobjectData;

/** Applies SeinARTS' fixed Components-panel hierarchy to actor Blueprints. */
class FSeinEntityComponentTreeCustomization final
	: public ISCSEditorUICustomization
	, public TSharedFromThis<FSeinEntityComponentTreeCustomization>
{
public:
	virtual ~FSeinEntityComponentTreeCustomization() override;

	virtual TSharedPtr<SWidget> GetControlsWidget(
		TSharedRef<SSubobjectEditor>& SubobjectEditor,
		const FSubobjectData& Data) const override;

	/** Stop deferred work before unregistering this customization. */
	void Deactivate();

	/** Rebuild every observed tree after the customization is unregistered. */
	void RefreshTrackedEditors();

private:
	void ScheduleGrouping(TSharedRef<SSubobjectEditor> SubobjectEditor) const;
	bool ApplyGrouping(SSubobjectEditor& SubobjectEditor) const;
	void CancelPendingWork();

	mutable TMap<const SSubobjectEditor*, FTSTicker::FDelegateHandle>
		PendingEditors;
	mutable TArray<TWeakPtr<SSubobjectEditor>> TrackedEditors;
	bool bActive = true;
};
