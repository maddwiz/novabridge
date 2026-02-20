#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "HttpRouteHandle.h"
#include "HttpServerRequest.h"
#include "HttpServerResponse.h"
#include "Dom/JsonObject.h"
#include "Containers/Ticker.h"

class IHttpRouter;
class ASceneCapture2D;
class UTextureRenderTarget2D;
class IWebSocketServer;
class INetworkingWebSocket;
class ULevelSequencePlayer;
class ALevelSequenceActor;

class FNovaBridgeModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	void StartHttpServer();
	void StopHttpServer();
	bool HandleCorsPreflight(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	void AddCorsHeaders(TUniquePtr<struct FHttpServerResponse>& Response) const;
	bool IsApiKeyAuthorized(const struct FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);

	// JSON helpers
	TSharedPtr<FJsonObject> ParseRequestBody(const struct FHttpServerRequest& Request);
	void SendJsonResponse(const FHttpResultCallback& OnComplete, TSharedPtr<FJsonObject> JsonObj, int32 StatusCode = 200);
	void SendErrorResponse(const FHttpResultCallback& OnComplete, const FString& Error, int32 StatusCode = 400);
	void SendOkResponse(const FHttpResultCallback& OnComplete);

	// Health
	bool HandleHealth(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleProjectInfo(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);

	// Scene handlers
	bool HandleSceneList(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleSceneSpawn(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleSceneDelete(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleSceneTransform(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleSceneGet(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleSceneSetProperty(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);

	// Asset handlers
	bool HandleAssetList(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleAssetCreate(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleAssetDuplicate(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleAssetDelete(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleAssetRename(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleAssetInfo(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleAssetImport(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);

	// Mesh handlers
	bool HandleMeshCreate(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleMeshGet(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleMeshPrimitive(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);

	// Material handlers
	bool HandleMaterialCreate(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleMaterialSetParam(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleMaterialGet(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleMaterialCreateInstance(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);

	// Viewport handlers
	bool HandleViewportScreenshot(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleViewportSetCamera(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleViewportGetCamera(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);

	// Blueprint handlers
	bool HandleBlueprintCreate(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleBlueprintAddComponent(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleBlueprintCompile(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);

	// Build handlers
	bool HandleBuildLighting(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleExecCommand(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);

	// Stream handlers
	bool HandleStreamStart(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleStreamStop(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleStreamConfig(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleStreamStatus(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);

	// PCG handlers
	bool HandlePcgListGraphs(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandlePcgCreateVolume(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandlePcgGenerate(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandlePcgSetParam(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandlePcgCleanup(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);

	// Sequencer handlers
	bool HandleSequencerCreate(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleSequencerAddTrack(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleSequencerSetKeyframe(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleSequencerPlay(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleSequencerStop(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleSequencerScrub(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleSequencerRender(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleSequencerInfo(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);

	// Optimization handlers
	bool HandleOptimizeNanite(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleOptimizeLod(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleOptimizeLumen(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleOptimizeStats(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleOptimizeTextures(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleOptimizeCollision(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);

	// WebSocket stream infrastructure
	void StartWebSocketServer();
	void StopWebSocketServer();
	void StartStreamTicker();
	void StopStreamTicker();
	void StreamTick();

	// Scene capture for offscreen viewport
	void EnsureCaptureSetup();
	void CleanupCapture();
	void EnsureStreamCaptureSetup();
	void CleanupStreamCapture();

	struct FWsClient
	{
		INetworkingWebSocket* Socket = nullptr;
		FGuid Id;
	};

	TSharedPtr<IHttpRouter> HttpRouter;
	TArray<FHttpRouteHandle> RouteHandles;
	uint32 HttpPort = 30010;
	int32 ApiRouteCount = 0;
	FString RequiredApiKey;

	// WebSocket streaming state
	TUniquePtr<IWebSocketServer> WsServer;
	TArray<FWsClient> WsClients;
	uint32 WsPort = 30011;
	int32 StreamFps = 10;
	int32 StreamWidth = 640;
	int32 StreamHeight = 360;
	int32 StreamQuality = 50;
	bool bStreamActive = false;
	double LastStreamFrameTime = 0.0;
	FTSTicker::FDelegateHandle WsServerTickHandle;
	FTSTicker::FDelegateHandle StreamTickHandle;

	// Offscreen capture state
	TWeakObjectPtr<ASceneCapture2D> CaptureActor;
	TWeakObjectPtr<UTextureRenderTarget2D> RenderTarget;
	TWeakObjectPtr<ASceneCapture2D> StreamCaptureActor;
	TWeakObjectPtr<UTextureRenderTarget2D> StreamRenderTarget;
	FVector CameraLocation = FVector(0, 0, 500);
	FRotator CameraRotation = FRotator(-45, 0, 0);
	float CameraFOV = 90.0f;
	int32 CaptureWidth = 1280;
	int32 CaptureHeight = 720;

	// Runtime sequencer state
	TMap<FString, TWeakObjectPtr<ULevelSequencePlayer>> SequencePlayers;
	TMap<FString, TWeakObjectPtr<ALevelSequenceActor>> SequenceActors;
};
