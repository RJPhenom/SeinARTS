// Copyright (c) 2026 Phenom Studios, Inc.

using UnrealBuildTool;

public class SeinARTSUIToolkit : ModuleRules
{
	public SeinARTSUIToolkit(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"UMG",
				"SlateCore",
				"Slate",
				"SeinARTSCore",
				"SeinARTSCoreEntity",
				"SeinARTSFramework",
				"SeinARTSNet",        // FSeinLobbyState is exposed by the lobby view model
				"GameplayTags",
				"DeveloperSettings",  // minimap VM reads USeinARTSCoreSettings (a UDeveloperSettings)
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"InputCore",
				"RenderCore",
				"SeinARTSLevelData",  // minimap: play-area bounds + baked background texture
				"SeinARTSFogOfWar",   // minimap: fog overlay + enemy-blip visibility culling
			}
		);
	}
}
