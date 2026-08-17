using UnrealBuildTool;

public class LocalizedMaterialWorkbench : ModuleRules
{
    public LocalizedMaterialWorkbench(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                "Core",
                "CoreUObject",
                "Engine",
                "InputCore",
                "Projects",
                "UnrealEd",
                "ToolMenus",
                "LevelEditor",
                "Slate",
                "SlateCore",
                "ContentBrowser",
                "AssetRegistry",
                "MeshDescription",
                "StaticMeshDescription",
                "SkeletalMeshDescription"
            }
        );
    }
}
