// SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.

using UnrealBuildTool;

public class SeinARTSCover : ModuleRules
{
	public SeinARTSCover(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"DeveloperSettings",            // USeinARTSCoverSettings : UDeveloperSettings
			"GameplayTags",
			"SeinARTSCore",
			"SeinARTSCoreEntity",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"InputCore",
			"SeinARTSFramework",
			// Cover queries gate by per-observer fog visibility — the cover-aware
			// snap resolvers + formation preview only consider cover providers
			// the relevant player can see. Helper lives on USeinFogOfWar:
			// IsEntityVisibleToObserver(Observer, Sim, Handle).
			"SeinARTSFogOfWar",
		});
	}
}
