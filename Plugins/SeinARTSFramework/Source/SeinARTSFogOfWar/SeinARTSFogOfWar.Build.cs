using UnrealBuildTool;

public class SeinARTSFogOfWar : ModuleRules
{
    public SeinARTSFogOfWar(ReadOnlyTargetRules Target) : base(Target)
    {
        PrivateDependencyModuleNames.AddRange(new string[] {
            "Core", "CoreUObject", "Engine",
            "SeinARTSCore", "SeinARTSCoreEntity",
            "GameplayTags",
            "RenderCore", "RHI"
        });

        // Editor-side integrations (volume details panel + entity-bridge
        // vision-stamp draw callback) live in the dedicated
        // SeinARTSFogOfWarEditor module now. The deps below are still needed
        // by this Runtime module because the show-flag setter inside the
        // `Sein.FogOfWar.Show` console command (in the UE_ENABLE_DEBUG_DRAWING
        // block) reaches GEditor + FEditorViewportClient::Invalidate to push
        // ShowFlags.FogOfWar onto the level-editor viewports.
        if (Target.bBuildEditor)
        {
            PrivateDependencyModuleNames.AddRange(new string[] {
                "UnrealEd",          // GEditor
                "LevelEditor"        // FLevelEditorViewportClient
            });
        }
    }
}
