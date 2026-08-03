/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file:       SeinWidgetBlueprint.h
 * @date:       4/16/2026
 * @author:     RJ Macklem
 * @brief:      Uncooked WidgetBlueprint asset subclass for SeinUserWidget UI
 *              assets. Lives in the UncookedOnly authoring module so the
 *              source asset remains loadable in editor, commandlet, and
 *              uncooked standalone-game processes without shipping editor
 *              tooling in a cooked game.
 */

#pragma once

#include "CoreMinimal.h"
#include "WidgetBlueprint.h"
#include "SeinWidgetBlueprint.generated.h"

/**
 * Thin UWidgetBlueprint subclass that identifies a Widget Blueprint as a
 * SeinARTS UI widget. Follows the same pattern as the framework's other custom
 * Blueprint asset types: it gives editor tooling a unique SupportedClass for
 * asset definitions, colors, icons, and filtering.
 *
 * This source-asset class intentionally lives in SeinARTSGraphNodes, whose
 * module type is UncookedOnly. Runtime UI assets still generate the stock
 * UWidgetBlueprintGeneratedClass, while editor builds, cook commandlets, and
 * `UnrealEditor -game` can all deserialize the source Blueprint object. Keeping
 * it in the Editor-typed SeinARTSEditor module makes uncooked standalone load
 * the generated class without its ClassGeneratedBy object and fatally crash.
 *
 * Does NOT override GetBlueprintClass(): runtime instantiation remains on the
 * stock UMG CreateWidget/AddToViewport path.
 *
 * The owning UncookedOnly module registers this exact class with the widget
 * compiler factory at startup because the Kismet compiler map does not walk
 * parent classes:
 *
 *   FKismetCompilerContext::RegisterCompilerForBP(
 *       USeinWidgetBlueprint::StaticClass(),
 *       &UWidgetBlueprint::GetCompilerForWidgetBP);
 */
UCLASS(BlueprintType)
class SEINARTSGRAPHNODES_API USeinWidgetBlueprint : public UWidgetBlueprint
{
	GENERATED_BODY()
};
