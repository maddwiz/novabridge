#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "HttpRouteHandle.h"
#include "HttpServerRequest.h"
#include "HttpServerResponse.h"
#include "Containers/Ticker.h"

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
	bool IsAuthorizedRuntimeToken(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete) const;
	void QueueRuntimeEvent(const TSharedPtr<FJsonObject>& EventObj);
	void PushAuditEntry(const FString& Route, const FString& Action, const FString& Status, const FString& Message);

	bool HandleHealth(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleCapabilities(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleEvents(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleAuditTrail(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandlePair(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleExecutePlan(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);

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
	int32 MaxPlanSteps = 64;
	int32 MaxSpawnPerPlan = 25;
	mutable FCriticalSection RuntimeExecutePlanRateLimitMutex;
	double RuntimeExecutePlanWindowStartSec = 0.0;
	int32 RuntimeExecutePlanCountInWindow = 0;
	int32 MaxExecutePlanPerMinute = 30;

	struct FWsClient
	{
		INetworkingWebSocket* Socket = nullptr;
		FGuid Id;
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
};
