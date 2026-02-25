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

		bool bHasWebSocketNetworking = Directory.Exists(Path.Combine(EngineDirectory, "Plugins", "Experimental", "WebSocketNetworking", "Source", "WebSocketNetworking"));
		PublicDefinitions.Add($"NOVABRIDGE_WITH_WEBSOCKET_NETWORKING={(bHasWebSocketNetworking ? 1 : 0)}");
		if (bHasWebSocketNetworking)
		{
			PrivateDependencyModuleNames.Add("WebSocketNetworking");
		}
	}
}
