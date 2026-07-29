using UnrealBuildTool;

public class SeinARTSLevelData : ModuleRules
{
    public SeinARTSLevelData(ReadOnlyTargetRules Target) : base(Target)
    {
        PublicDependencyModuleNames.AddRange(new string[] {
            "SeinARTSCoreEntity"
        });

        PrivateDependencyModuleNames.AddRange(new string[] {
            "Core", "CoreUObject", "Engine",
            "SeinARTSCore",
            "GameplayTags",
            "PhysicsCore",   // bake resolves a trace hit's physical material → terrain type
            "RenderCore", "RHI"
        });

        // Editor-only deps for the bake pipeline (slow-task progress + asset save)
        // and the level-volume details panel + "Bake Level Data" button.
        // Stripped from shipping builds.
        if (Target.bBuildEditor)
        {
            PrivateDependencyModuleNames.AddRange(new string[] {
                "Slate", "SlateCore",
                "UnrealEd", "AssetRegistry",
                "LevelEditor",
                "PropertyEditor"
            });
        }
    }
}
