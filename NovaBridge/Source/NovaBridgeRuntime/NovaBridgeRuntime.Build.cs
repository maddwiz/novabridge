using UnrealBuildTool;
using System.IO;

public class NovaBridgeRuntime : ModuleRules
{
	public NovaBridgeRuntime(ReadOnlyTargetRules Target) : base(Target)
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
			"NovaBridgeCore",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"ImageWrapper",
			"LevelSequence",
			"MovieScene",
			"MovieSceneTracks",
		});

		bool bHasWebSocketNetworking = Directory.Exists(Path.Combine(
			EngineDirectory,
			"Plugins",
			"Experimental",
			"WebSocketNetworking",
			"Source",
			"WebSocketNetworking"));
		PublicDefinitions.Add($"NOVABRIDGE_WITH_WEBSOCKET_NETWORKING={(bHasWebSocketNetworking ? 1 : 0)}");
		if (bHasWebSocketNetworking)
		{
			PrivateDependencyModuleNames.Add("WebSocketNetworking");
		}

		bool bHasPCG = Directory.Exists(Path.Combine(
			EngineDirectory,
			"Plugins",
			"Experimental",
			"PCG",
			"Source",
			"PCG"));
		PublicDefinitions.Add($"NOVABRIDGE_WITH_PCG={(bHasPCG ? 1 : 0)}");
		if (bHasPCG)
		{
			PrivateDependencyModuleNames.Add("PCG");
		}
	}
}
