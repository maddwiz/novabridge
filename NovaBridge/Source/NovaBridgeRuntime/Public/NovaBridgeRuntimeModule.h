#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "HttpRouteHandle.h"
#include "HttpServerRequest.h"
#include "HttpServerResponse.h"
#include "Containers/Ticker.h"

DECLARE_LOG_CATEGORY_EXTERN(LogNovaBridgeRuntime, Log, All);

class IHttpRouter;
class FJsonObject;
class IWebSocketServer;
class INetworkingWebSocket;

class FNovaBridgeRuntimeModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	struct FRuntimeUndoEntry;

	void StartHttpServer();
	void StopHttpServer();
	void StartEventWebSocketServer();
	void StopEventWebSocketServer();
	void PumpEventSocketQueue();

	bool IsRuntimeEnabledByConfig() const;
	bool HandleCorsPreflight(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	void AddCorsHeaders(TUniquePtr<FHttpServerResponse>& Response) const;
	TSharedPtr<FJsonObject> ParseRequestBody(const FHttpServerRequest& Request) const;
	void SendJsonResponse(const FHttpResultCallback& OnComplete, TSharedPtr<FJsonObject> JsonObj, int32 StatusCode = 200) const;
	void SendErrorResponse(const FHttpResultCallback& OnComplete, const FString& Error, int32 StatusCode = 400) const;
	bool IsLocalHostRequest(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete) const;
	bool IsAuthorizedRuntimeToken(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete, FString* OutResolvedRole = nullptr) const;
	FString ResolveRuntimeRoleFromRequest(const FHttpServerRequest& Request) const;
	bool IsRuntimeRouteAllowedForRole(const FString& Role, const FString& RoutePath, EHttpServerRequestVerbs Verb, FString& OutReason) const;
	int32 GetRuntimeRouteRateLimitPerMinute(const FString& Role, const FString& RoutePath) const;
	bool ConsumeRuntimeRouteRateLimit(const FString& BucketKey, int32 LimitPerMinute, FString& OutError);
	void QueueRuntimeEvent(const TSharedPtr<FJsonObject>& EventObj);
	void PushAuditEntry(const FString& Route, const FString& Action, const FString& Status, const FString& Message);
	void PushRuntimeUndoEntry(const FString& Action, const FString& ActorName, const FString& ActorLabel);
	bool PopRuntimeUndoEntry(FRuntimeUndoEntry& OutEntry);

	bool HandleHealth(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleCapabilities(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleEvents(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleAuditTrail(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandlePair(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleExecutePlan(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleUndo(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleSceneList(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleSceneGet(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleSceneSetProperty(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleViewportSetCamera(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleViewportGetCamera(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleViewportScreenshot(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleSequencerPlay(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleSequencerStop(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleSequencerInfo(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandlePcgGenerate(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	void RegisterRuntimeCapabilities();
	TSharedPtr<FJsonObject> BuildRuntimePermissionsSnapshot(const FString& Role) const;

private:
	TSharedPtr<IHttpRouter> HttpRouter;
	TArray<FHttpRouteHandle> RouteHandles;
	uint32 HttpPort = 30020;
	int32 ApiRouteCount = 0;
	bool bServerStarted = false;

	bool bRuntimeEnabled = false;
	bool bRequireRuntimeToken = true;
	FString RuntimePairingCode;
	FDateTime RuntimePairingExpiryUtc;
	FString RuntimeToken;
	FDateTime RuntimeTokenIssuedUtc;
	FDateTime RuntimeTokenExpiryUtc;
	FString RuntimeDefaultRole = TEXT("automation");
	FString RuntimeTokenRole = TEXT("automation");
	int32 MaxPlanSteps = 64;
	int32 MaxSpawnPerPlan = 25;
	int32 MaxActorsPerSession = 500;
	mutable FCriticalSection RuntimeActorCountMutex;
	int32 RuntimeActiveActorCount = 0;

	mutable FCriticalSection RuntimeExecutePlanRateLimitMutex;
	double RuntimeExecutePlanWindowStartSec = 0.0;
	int32 RuntimeExecutePlanCountInWindow = 0;
	int32 MaxExecutePlanPerMinute = 30;
	int32 MaxAdminExecutePlanPerMinute = 30;
	int32 MaxAutomationExecutePlanPerMinute = 20;
	int32 MaxReadOnlyExecutePlanPerMinute = 5;
	int32 MaxAdminRequestsPerMinute = 240;
	int32 MaxAutomationRequestsPerMinute = 120;
	int32 MaxReadOnlyRequestsPerMinute = 120;

	struct FRuntimeRateBucket
	{
		double WindowStartSec = 0.0;
		int32 Count = 0;
	};
	mutable FCriticalSection RuntimeRouteRateLimitMutex;
	TMap<FString, FRuntimeRateBucket> RuntimeRouteRateBuckets;

	struct FWsClient
	{
		INetworkingWebSocket* Socket = nullptr;
		FGuid Id;
		bool bSubscriptionConfirmed = false;
		bool bEventTypeFilterEnabled = false;
		TSet<FString> EventTypes;
	};
	TUniquePtr<IWebSocketServer> EventWsServer;
	TArray<FWsClient> EventWsClients;
	uint32 EventWsPort = 30022;
	FTSTicker::FDelegateHandle EventWsServerTickHandle;
	mutable FCriticalSection RuntimeEventQueueMutex;
	TArray<FString> RuntimePendingEventPayloads;
	TArray<FString> RuntimePendingEventTypes;
	int32 RuntimePendingEventsLimit = 2048;

	struct FRuntimeAuditEntry
	{
		FString TimestampUtc;
		FString Route;
		FString Action;
		FString Status;
		FString Message;
	};
	mutable FCriticalSection RuntimeAuditMutex;
	TArray<FRuntimeAuditEntry> RuntimeAuditTrail;
	int32 RuntimeAuditLimit = 512;

	struct FRuntimeUndoEntry
	{
		FString TimestampUtc;
		FString Action;
		FString ActorName;
		FString ActorLabel;
	};
	mutable FCriticalSection RuntimeUndoMutex;
	TArray<FRuntimeUndoEntry> RuntimeUndoStack;
	int32 RuntimeUndoLimit = 256;
};
