using UnrealBuildTool;

public class SeinARTSNavigation : ModuleRules
{
    public SeinARTSNavigation(ReadOnlyTargetRules Target) : base(Target)
    {
        PrivateDependencyModuleNames.AddRange(new string[] {
            "Core", "CoreUObject", "Engine",
            "SeinARTSCore", "SeinARTSCoreEntity",
            "GameplayTags",
            "RenderCore", "RHI"
        });

        // Editor-only deps for the bake pipeline (slow-task progress + asset save).
        // Stripped from shipping builds.
        if (Target.bBuildEditor)
        {
            PrivateDependencyModuleNames.AddRange(new string[] {
                "Slate", "SlateCore",
                "UnrealEd", "AssetRegistry",
                "LevelEditor",
                "PropertyEditor" // Volume details panel + bake button
            });
        }
    }
}
