using UnrealBuildTool;

public class NovaBridge : ModuleRules
{
	public NovaBridge(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"HTTPServer",
			"Json",
			"JsonUtilities",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"UnrealEd",
			"EditorScriptingUtilities",
			"Slate",
			"SlateCore",
			"InputCore",
			"LevelEditor",
			"MeshDescription",
			"StaticMeshDescription",
			"MeshConversion",
			"RawMesh",
			"RenderCore",
			"RHI",
			"ImageWrapper",
			"AssetRegistry",
			"AssetTools",
			"MaterialEditor",
			"Kismet",
			"KismetCompiler",
			"BlueprintGraph",
		});
	}
}
