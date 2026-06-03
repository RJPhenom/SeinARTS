/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file:		SeinARTSEditorStyle.h
 * @date:		3/27/2026
 * @author:		RJ Macklem
 * @brief:		Slate style set for the SeinARTS editor module.
 */

#pragma once

#include "Styling/SlateStyle.h"

/**
 * Manages the Slate style set for SeinARTS editor tooling.
 * Registers class icons, thumbnails, and branding assets.
 */
class FSeinARTSEditorStyle
{
public:
	static void Initialize();
	static void Shutdown();
	static const FSlateStyleSet& Get();

	/** Get the absolute path to an icon file in the BrandKit directory. */
	static FString GetIconPath(const FString& IconFilename);

	/** Get a cached UTexture2D loaded from a BrandKit PNG. Returns nullptr if not found. */
	static UTexture2D* GetIconTexture(const FName& TextureName);

private:
	static TSharedPtr<FSlateStyleSet> StyleSet;
	static FString IconsDir;

	/** Cached UTexture2D objects loaded from PNG files for use in thumbnail renderers. */
	static TMap<FName, TObjectPtr<UTexture2D>> IconTextures;

	/** Load a PNG from the BrandKit directory and cache it as a transient UTexture2D. */
	static UTexture2D* LoadAndCacheIcon(const FName& TextureName, const FString& Filename);
};
