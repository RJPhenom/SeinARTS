using UnrealBuildTool;

public class SeinARTSCombat : ModuleRules
{
    public SeinARTSCombat(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[] {
            "Core", "CoreUObject", "Engine",
            "GameplayTags",
            "SeinARTSCore",
            "SeinARTSCoreEntity"
        });
    }
}
