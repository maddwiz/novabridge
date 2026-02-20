using UnrealBuildTool;
using System.IO;

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
			"Networking",
			"Sockets",
			"LevelSequence",
			"MovieScene",
			"MovieSceneTracks",
		});

		bool bHasWebSocketNetworking = Directory.Exists(Path.Combine(EngineDirectory, "Plugins", "Experimental", "WebSocketNetworking", "Source", "WebSocketNetworking"));
		PublicDefinitions.Add($"NOVABRIDGE_WITH_WEBSOCKET_NETWORKING={(bHasWebSocketNetworking ? 1 : 0)}");
		if (bHasWebSocketNetworking)
		{
			PrivateDependencyModuleNames.Add("WebSocketNetworking");
		}

		bool bHasPCG = Directory.Exists(Path.Combine(EngineDirectory, "Plugins", "Experimental", "PCG", "Source", "PCG"));
		PublicDefinitions.Add($"NOVABRIDGE_WITH_PCG={(bHasPCG ? 1 : 0)}");
		if (bHasPCG)
		{
			PrivateDependencyModuleNames.Add("PCG");
		}

		bool bHasMovieRenderPipeline = Directory.Exists(Path.Combine(EngineDirectory, "Plugins", "MovieScene", "MovieRenderPipeline", "Source", "MovieRenderPipelineCore"));
		PublicDefinitions.Add($"NOVABRIDGE_WITH_MRQ={(bHasMovieRenderPipeline ? 1 : 0)}");
		if (bHasMovieRenderPipeline)
		{
			PrivateDependencyModuleNames.Add("MovieRenderPipelineCore");
		}
	}
}
