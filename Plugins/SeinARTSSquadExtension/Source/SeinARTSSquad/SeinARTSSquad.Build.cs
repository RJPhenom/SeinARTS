/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 */

using UnrealBuildTool;

public class SeinARTSSquad : ModuleRules
{
	public SeinARTSSquad(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"DeveloperSettings",            // USeinARTSSquadSettings : UDeveloperSettings
			"GameplayTags",
			"SeinARTSCore",
			"SeinARTSCoreEntity"
		});
	}
}
