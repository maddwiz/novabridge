#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "HttpRouteHandle.h"

class IHttpRouter;
class ASceneCapture2D;
class UTextureRenderTarget2D;

class FNovaBridgeModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	void StartHttpServer();
	void StopHttpServer();

	// JSON helpers
	TSharedPtr<FJsonObject> ParseRequestBody(const struct FHttpServerRequest& Request);
	void SendJsonResponse(const FHttpResultCallback& OnComplete, TSharedPtr<FJsonObject> JsonObj, int32 StatusCode = 200);
	void SendErrorResponse(const FHttpResultCallback& OnComplete, const FString& Error, int32 StatusCode = 400);
	void SendOkResponse(const FHttpResultCallback& OnComplete);

	// Health
	bool HandleHealth(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);

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

	// Scene capture for offscreen viewport
	void EnsureCaptureSetup();
	void CleanupCapture();

	TSharedPtr<IHttpRouter> HttpRouter;
	TArray<FHttpRouteHandle> RouteHandles;
	static constexpr uint32 HttpPort = 30010;

	// Offscreen capture state
	TWeakObjectPtr<ASceneCapture2D> CaptureActor;
	TWeakObjectPtr<UTextureRenderTarget2D> RenderTarget;
	FVector CameraLocation = FVector(0, 0, 500);
	FRotator CameraRotation = FRotator(-45, 0, 0);
	float CameraFOV = 90.0f;
	int32 CaptureWidth = 1280;
	int32 CaptureHeight = 720;
};
