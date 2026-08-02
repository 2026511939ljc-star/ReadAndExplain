// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class ReadAllandExplains : ModuleRules
{
	public ReadAllandExplains(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		
		PublicIncludePaths.AddRange(
			new string[] {
				// ... add public include paths required here ...
			}
			);
				
		
		PrivateIncludePaths.AddRange(
			new string[] {
				// ... add other private include paths required here ...
			}
			);
			
		
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"DeveloperSettings",
				"NiagaraCore",
				"Niagara",
			}
			);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Slate",
				"SlateCore",
				"UnrealEd",
				"AssetRegistry",
				"BlueprintGraph",
				"Kismet",
				"KismetCompiler",
				"EditorStyle",
				"Projects",
				"InputCore",
				"ToolMenus",
				"ContentBrowser",
				"LevelEditor",
				"GraphEditor",
				"AssetManagerEditor",
				"NiagaraEditor",
				"Json",
			}
			);
		
		
		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
				// ... add any modules that your module loads dynamically here ...
			}
			);
	}
}