using UnrealBuildTool;

public class SeinARTSLevelData : ModuleRules
{
    public SeinARTSLevelData(ReadOnlyTargetRules Target) : base(Target)
    {
        PublicDependencyModuleNames.AddRange(new string[] {
            "Core", "CoreUObject", "Engine",
            "SeinARTSCore",
            "GameplayTags",
            "SeinARTSCoreEntity"
        });

        PrivateDependencyModuleNames.AddRange(new string[] {
            "PhysicsCore",   // bake resolves a trace hit's physical material → terrain type
            "RenderCore", "RHI"
        });

        // Editor-only deps for the bake pipeline (slow-task progress + asset save)
        // and the level-volume details panel + "Bake Level Data" button.
        // Stripped from shipping builds.
        if (Target.bBuildEditor)
        {
            // SeinLevelVolumeDetails.h is a shipped public editor header.
            PublicDependencyModuleNames.Add("PropertyEditor");

            PrivateDependencyModuleNames.AddRange(new string[] {
                "Slate", "SlateCore",
                "UnrealEd", "AssetRegistry",
                "LevelEditor",
            });
        }
    }
}
