/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file:		SeinWidgetBlueprint.h
 * @date:		4/16/2026
 * @author:		RJ Macklem
 * @brief:		WidgetBlueprint asset subclass for SeinUserWidget UI assets.
 *				Exists so the editor can assign a unique asset type color,
 *				icon, and thumbnail to SeinARTS Widget Blueprints.
 */

#pragma once

#include "CoreMinimal.h"
#include "WidgetBlueprint.h"
#include "SeinWidgetBlueprint.generated.h"

/**
 * Thin UWidgetBlueprint subclass that identifies a Widget Blueprint as a
 * SeinARTS UI widget. Follows the same pattern as USeinActorBlueprint /
 * USeinAbilityBlueprint — gives the editor a unique SupportedClass for
 * asset definitions, colors, icons, and filtering.
 *
 * Does NOT override GetBlueprintClass(): the generated class remains the
 * engine's UWidgetBlueprintGeneratedClass so runtime instantiation
 * (CreateWidget + AddToViewport) goes through stock UMG paths.
 *
 * NOTE: For widget tree compilation to work, the editor module MUST register
 * this class with the widget compiler factory at startup:
 *
 *   FKismetCompilerContext::RegisterCompilerForBP(
 *       USeinWidgetBlueprint::StaticClass(),
 *       &UWidgetBlueprint::GetCompilerForWidgetBP);
 *
 * Without that registration the Kismet compiler lookup misses (it's a direct
 * map lookup, no hierarchy walk), falls back to the generic compiler, and
 * the WidgetTree is never copied into the generated class — the widget
 * appears to compile but renders nothing on AddToViewport.
 */
UCLASS(BlueprintType)
class SEINARTSEDITOR_API USeinWidgetBlueprint : public UWidgetBlueprint
{
	GENERATED_BODY()
};
