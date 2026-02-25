#include "NovaBridgeRuntimeModule.h"

#include "NovaBridgeCapabilityRegistry.h"
#include "NovaBridgeCoreTypes.h"
#include "NovaBridgeHttpUtils.h"
#include "NovaBridgePolicy.h"
#include "NovaBridgePlanDispatch.h"
#include "NovaBridgePlanEvents.h"
#include "NovaBridgePlanSchema.h"
#include "HttpServerModule.h"
#include "IHttpRouter.h"
#include "HttpPath.h"
#include "HttpServerRequest.h"
#include "HttpServerResponse.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Modules/ModuleManager.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Async/Async.h"
#include "Containers/Ticker.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/StaticMeshActor.h"
#include "GameFramework/Actor.h"
#include "EngineUtils.h"
#include "UObject/Class.h"
#include "UObject/UnrealType.h"

#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/App.h"
#include "Misc/ScopeLock.h"
#include "HAL/PlatformMisc.h"
#include "Math/UnrealMathUtility.h"

#if NOVABRIDGE_WITH_WEBSOCKET_NETWORKING
#include "IWebSocketNetworkingModule.h"
#include "IWebSocketServer.h"
#include "INetworkingWebSocket.h"
#include "WebSocketNetworkingDelegates.h"
#endif

DEFINE_LOG_CATEGORY(LogNovaBridgeRuntime);

namespace
{
using NovaBridgeCore::HttpVerbToString;
} // namespace

void FNovaBridgeRuntimeModule::StartupModule()
{
	if (GIsEditor)
	{
		UE_LOG(LogNovaBridgeRuntime, Log, TEXT("NovaBridgeRuntime module loaded in editor process; runtime server remains disabled."));
		return;
	}

	if (!IsRuntimeEnabledByConfig())
	{
		UE_LOG(LogNovaBridgeRuntime, Log, TEXT("NovaBridgeRuntime disabled (set -NovaBridgeRuntime=1 or NOVABRIDGE_RUNTIME=1 to enable)."));
		return;
	}

	bRuntimeEnabled = true;
	StartHttpServer();
}

void FNovaBridgeRuntimeModule::ShutdownModule()
{
	StopHttpServer();
	bRuntimeEnabled = false;
}

bool FNovaBridgeRuntimeModule::IsRuntimeEnabledByConfig() const
{
	int32 RuntimeEnabledValue = 0;
	if (FParse::Value(FCommandLine::Get(), TEXT("NovaBridgeRuntime="), RuntimeEnabledValue))
	{
		return RuntimeEnabledValue == 1;
	}

	const FString EnvRuntime = FPlatformMisc::GetEnvironmentVariable(TEXT("NOVABRIDGE_RUNTIME"));
	FString TrimmedEnvRuntime = EnvRuntime;
	TrimmedEnvRuntime.TrimStartAndEndInline();
	TrimmedEnvRuntime.ToLowerInline();
	return TrimmedEnvRuntime == TEXT("1") || TrimmedEnvRuntime == TEXT("true") || TrimmedEnvRuntime == TEXT("yes");
}

void FNovaBridgeRuntimeModule::StartHttpServer()
{
	if (!bRuntimeEnabled)
	{
		return;
	}
	if (bServerStarted)
	{
		return;
	}

	ApiRouteCount = 0;
	{
		FScopeLock AuditLock(&RuntimeAuditMutex);
		RuntimeAuditTrail.Empty();
	}
	{
		FScopeLock EventLock(&RuntimeEventQueueMutex);
		RuntimePendingEventPayloads.Empty();
		RuntimePendingEventTypes.Empty();
	}
	{
		FScopeLock UndoLock(&RuntimeUndoMutex);
		RuntimeUndoStack.Empty();
	}

	int32 ParsedPort = 0;
	if (FParse::Value(FCommandLine::Get(), TEXT("NovaBridgeRuntimePort="), ParsedPort))
	{
		if (ParsedPort > 0 && ParsedPort <= 65535)
		{
			HttpPort = static_cast<uint32>(ParsedPort);
		}
	}
	if (HttpPort == 30020)
	{
		const FString EnvPort = FPlatformMisc::GetEnvironmentVariable(TEXT("NOVABRIDGE_RUNTIME_PORT"));
		if (!EnvPort.IsEmpty())
		{
			const int32 EnvPortInt = FCString::Atoi(*EnvPort);
			if (EnvPortInt > 0 && EnvPortInt <= 65535)
			{
				HttpPort = static_cast<uint32>(EnvPortInt);
			}
		}
	}
	int32 ParsedEventsPort = 0;
	if (FParse::Value(FCommandLine::Get(), TEXT("NovaBridgeRuntimeEventsPort="), ParsedEventsPort))
	{
		if (ParsedEventsPort > 0 && ParsedEventsPort <= 65535)
		{
			EventWsPort = static_cast<uint32>(ParsedEventsPort);
		}
	}
	if (EventWsPort == 30022)
	{
		const FString EnvEventsPort = FPlatformMisc::GetEnvironmentVariable(TEXT("NOVABRIDGE_RUNTIME_EVENTS_PORT"));
		if (!EnvEventsPort.IsEmpty())
		{
			const int32 EnvEventsPortInt = FCString::Atoi(*EnvEventsPort);
			if (EnvEventsPortInt > 0 && EnvEventsPortInt <= 65535)
			{
				EventWsPort = static_cast<uint32>(EnvEventsPortInt);
			}
		}
	}

	FString ConfiguredToken;
	if (FParse::Value(FCommandLine::Get(), TEXT("NovaBridgeRuntimeToken="), ConfiguredToken))
	{
		ConfiguredToken.TrimStartAndEndInline();
	}
	if (ConfiguredToken.IsEmpty())
	{
		ConfiguredToken = FPlatformMisc::GetEnvironmentVariable(TEXT("NOVABRIDGE_RUNTIME_TOKEN"));
		ConfiguredToken.TrimStartAndEndInline();
	}

	RuntimeToken = ConfiguredToken.IsEmpty()
		? FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower)
		: ConfiguredToken;
	RuntimeTokenIssuedUtc = FDateTime::UtcNow();
	RuntimeTokenExpiryUtc = RuntimeTokenIssuedUtc + FTimespan::FromHours(1.0);

	const int32 PairCode = FMath::RandRange(100000, 999999);
	RuntimePairingCode = FString::Printf(TEXT("%06d"), PairCode);
	RuntimePairingExpiryUtc = FDateTime::UtcNow() + FTimespan::FromMinutes(15.0);
	RegisterRuntimeCapabilities();

	HttpRouter = FHttpServerModule::Get().GetHttpRouter(HttpPort);
	if (!HttpRouter)
	{
		UE_LOG(LogNovaBridgeRuntime, Error, TEXT("Failed to get runtime HTTP router on port %d"), HttpPort);
		return;
	}

	auto Bind = [this](const TCHAR* Path, EHttpServerRequestVerbs Verbs, bool bRequireAuth, bool (FNovaBridgeRuntimeModule::*Handler)(const FHttpServerRequest&, const FHttpResultCallback&))
	{
		const FString RoutePath(Path);
		ApiRouteCount++;
		RouteHandles.Add(HttpRouter->BindRoute(
			FHttpPath(Path), Verbs,
			FHttpRequestHandler::CreateLambda([this, Handler, bRequireAuth, RoutePath](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete) -> bool
			{
				if (!bRuntimeEnabled)
				{
					PushAuditEntry(RoutePath, RoutePath, TEXT("denied"), TEXT("Runtime server is disabled"));
					SendErrorResponse(OnComplete, TEXT("Runtime server is disabled"), 503);
					return true;
				}

				if (!IsLocalHostRequest(Request, OnComplete))
				{
					PushAuditEntry(RoutePath, RoutePath, TEXT("denied"), TEXT("Rejected non-localhost request"));
					return true;
				}

				if (bRequireAuth && !IsAuthorizedRuntimeToken(Request, OnComplete))
				{
					PushAuditEntry(RoutePath, RoutePath, TEXT("denied"), TEXT("Unauthorized runtime token"));
					return true;
				}

				UE_LOG(LogNovaBridgeRuntime, Verbose, TEXT("[%s] %s %s"),
					*FDateTime::Now().ToString(),
					HttpVerbToString(Request.Verb),
					*Request.RelativePath.GetPath());
				return (this->*Handler)(Request, OnComplete);
			})
		));

		RouteHandles.Add(HttpRouter->BindRoute(
			FHttpPath(Path), EHttpServerRequestVerbs::VERB_OPTIONS,
			FHttpRequestHandler::CreateLambda([this](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete) -> bool
			{
				return HandleCorsPreflight(Request, OnComplete);
			})
		));
	};

	Bind(TEXT("/nova/runtime/pair"), EHttpServerRequestVerbs::VERB_POST, false, &FNovaBridgeRuntimeModule::HandlePair);
	Bind(TEXT("/nova/health"), EHttpServerRequestVerbs::VERB_GET, true, &FNovaBridgeRuntimeModule::HandleHealth);
	Bind(TEXT("/nova/caps"), EHttpServerRequestVerbs::VERB_GET, true, &FNovaBridgeRuntimeModule::HandleCapabilities);
	Bind(TEXT("/nova/events"), EHttpServerRequestVerbs::VERB_GET, true, &FNovaBridgeRuntimeModule::HandleEvents);
	Bind(TEXT("/nova/audit"), EHttpServerRequestVerbs::VERB_GET, true, &FNovaBridgeRuntimeModule::HandleAuditTrail);
	Bind(TEXT("/nova/executePlan"), EHttpServerRequestVerbs::VERB_POST, true, &FNovaBridgeRuntimeModule::HandleExecutePlan);
	Bind(TEXT("/nova/undo"), EHttpServerRequestVerbs::VERB_POST, true, &FNovaBridgeRuntimeModule::HandleUndo);

	FHttpServerModule::Get().StartAllListeners();
	bServerStarted = true;
	StartEventWebSocketServer();

	UE_LOG(LogNovaBridgeRuntime, Log, TEXT("NovaBridgeRuntime server listening on 127.0.0.1:%d"), HttpPort);
	UE_LOG(LogNovaBridgeRuntime, Log, TEXT("Runtime pairing code: %s (valid until %s UTC)"),
		*RuntimePairingCode, *RuntimePairingExpiryUtc.ToIso8601());
	UE_LOG(LogNovaBridgeRuntime, Log, TEXT("Runtime token in memory only, expires at %s UTC"), *RuntimeTokenExpiryUtc.ToIso8601());
}

void FNovaBridgeRuntimeModule::StopHttpServer()
{
	if (!bServerStarted)
	{
		return;
	}
	StopEventWebSocketServer();

	if (HttpRouter)
	{
		for (const FHttpRouteHandle& Handle : RouteHandles)
		{
			HttpRouter->UnbindRoute(Handle);
		}
	}
	RouteHandles.Empty();
	ApiRouteCount = 0;
	FHttpServerModule::Get().StopAllListeners();
	bServerStarted = false;
}

IMPLEMENT_MODULE(FNovaBridgeRuntimeModule, NovaBridgeRuntime)
