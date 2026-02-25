#include "NovaBridgeRuntimeModule.h"

#include "NovaBridgeCoreTypes.h"
#include "NovaBridgeHttpUtils.h"

#include "HAL/PlatformMisc.h"
#include "HttpPath.h"
#include "HttpServerModule.h"
#include "HttpServerRequest.h"
#include "IHttpRouter.h"
#include "Math/UnrealMathUtility.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/ScopeLock.h"

using NovaBridgeCore::HttpVerbToString;

bool FNovaBridgeRuntimeModule::IsRuntimeEnabledByConfig() const
{
	int32 RuntimeEnabledValue = 0;
	if (FParse::Value(FCommandLine::Get(), TEXT("NovaBridgeRuntime="), RuntimeEnabledValue))
	{
		return RuntimeEnabledValue == 1;
	}

	FString EnvRuntime = FPlatformMisc::GetEnvironmentVariable(TEXT("NOVABRIDGE_RUNTIME"));
	EnvRuntime.TrimStartAndEndInline();
	EnvRuntime.ToLowerInline();
	return EnvRuntime == TEXT("1") || EnvRuntime == TEXT("true") || EnvRuntime == TEXT("yes");
}

void FNovaBridgeRuntimeModule::StartHttpServer()
{
	if (!bRuntimeEnabled || bServerStarted)
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
	{
		FScopeLock RateLock(&RuntimeRouteRateLimitMutex);
		RuntimeRouteRateBuckets.Empty();
	}
	{
		FScopeLock ActorLock(&RuntimeActorCountMutex);
		RuntimeActiveActorCount = 0;
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

	FString ParsedDefaultRole;
	if (FParse::Value(FCommandLine::Get(), TEXT("NovaBridgeRuntimeDefaultRole="), ParsedDefaultRole))
	{
		const FString NormalizedRole = NovaBridgeCore::NormalizeRoleName(ParsedDefaultRole);
		if (!NormalizedRole.IsEmpty())
		{
			RuntimeDefaultRole = NormalizedRole;
		}
	}
	if (RuntimeDefaultRole.IsEmpty())
	{
		const FString EnvRole = FPlatformMisc::GetEnvironmentVariable(TEXT("NOVABRIDGE_RUNTIME_DEFAULT_ROLE"));
		const FString NormalizedEnvRole = NovaBridgeCore::NormalizeRoleName(EnvRole);
		if (!NormalizedEnvRole.IsEmpty())
		{
			RuntimeDefaultRole = NormalizedEnvRole;
		}
	}
	if (RuntimeDefaultRole.IsEmpty())
	{
		RuntimeDefaultRole = TEXT("automation");
	}
	RuntimeTokenRole = RuntimeDefaultRole;

	int32 ParsedMaxActors = 0;
	if (FParse::Value(FCommandLine::Get(), TEXT("NovaBridgeRuntimeMaxActors="), ParsedMaxActors))
	{
		if (ParsedMaxActors > 0)
		{
			MaxActorsPerSession = ParsedMaxActors;
		}
	}
	if (MaxActorsPerSession == 500)
	{
		const FString EnvMaxActors = FPlatformMisc::GetEnvironmentVariable(TEXT("NOVABRIDGE_RUNTIME_MAX_ACTORS"));
		if (!EnvMaxActors.IsEmpty())
		{
			const int32 EnvMaxActorsInt = FCString::Atoi(*EnvMaxActors);
			if (EnvMaxActorsInt > 0)
			{
				MaxActorsPerSession = EnvMaxActorsInt;
			}
		}
	}

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

				FString ResolvedRole = RuntimeDefaultRole;
				if (bRequireAuth && !IsAuthorizedRuntimeToken(Request, OnComplete, &ResolvedRole))
				{
					PushAuditEntry(RoutePath, RoutePath, TEXT("denied"), TEXT("Unauthorized runtime token"));
					return true;
				}

				if (bRequireAuth)
				{
					FString RoutePermissionError;
					if (!IsRuntimeRouteAllowedForRole(ResolvedRole, RoutePath, Request.Verb, RoutePermissionError))
					{
						PushAuditEntry(RoutePath, RoutePath, TEXT("denied"),
							FString::Printf(TEXT("Role '%s' denied: %s"), *ResolvedRole, *RoutePermissionError));
						SendErrorResponse(OnComplete, TEXT("Permission denied for runtime role on this endpoint"), 403);
						return true;
					}

					FString PresentedToken = NovaBridgeCore::GetHeaderValueCaseInsensitive(Request, TEXT("X-NovaBridge-Token"));
					if (PresentedToken.IsEmpty())
					{
						const FString Authorization = NovaBridgeCore::GetHeaderValueCaseInsensitive(Request, TEXT("Authorization"));
						static const FString BearerPrefix = TEXT("Bearer ");
						if (Authorization.StartsWith(BearerPrefix, ESearchCase::IgnoreCase))
						{
							PresentedToken = Authorization.Mid(BearerPrefix.Len());
						}
					}
					PresentedToken.TrimStartAndEndInline();
					if (PresentedToken.IsEmpty())
					{
						PresentedToken = RuntimeToken;
					}

					const int32 RateLimit = GetRuntimeRouteRateLimitPerMinute(ResolvedRole, RoutePath);
					const FString RateBucket = PresentedToken + TEXT("|") + ResolvedRole + TEXT("|") + RoutePath;
					FString RateError;
					if (!ConsumeRuntimeRouteRateLimit(RateBucket, RateLimit, RateError))
					{
						PushAuditEntry(RoutePath, RoutePath, TEXT("rate_limited"), RateError);
						SendErrorResponse(OnComplete, RateError, 429);
						return true;
					}
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
	Bind(TEXT("/nova/scene/list"), EHttpServerRequestVerbs::VERB_GET, true, &FNovaBridgeRuntimeModule::HandleSceneList);
	Bind(TEXT("/nova/scene/get"), EHttpServerRequestVerbs::VERB_GET | EHttpServerRequestVerbs::VERB_POST, true, &FNovaBridgeRuntimeModule::HandleSceneGet);
	Bind(TEXT("/nova/scene/set-property"), EHttpServerRequestVerbs::VERB_POST, true, &FNovaBridgeRuntimeModule::HandleSceneSetProperty);
	Bind(TEXT("/nova/viewport/camera/set"), EHttpServerRequestVerbs::VERB_POST, true, &FNovaBridgeRuntimeModule::HandleViewportSetCamera);
	Bind(TEXT("/nova/viewport/camera/get"), EHttpServerRequestVerbs::VERB_GET, true, &FNovaBridgeRuntimeModule::HandleViewportGetCamera);
	Bind(TEXT("/nova/viewport/screenshot"), EHttpServerRequestVerbs::VERB_GET, true, &FNovaBridgeRuntimeModule::HandleViewportScreenshot);
	Bind(TEXT("/nova/sequencer/play"), EHttpServerRequestVerbs::VERB_POST, true, &FNovaBridgeRuntimeModule::HandleSequencerPlay);
	Bind(TEXT("/nova/sequencer/stop"), EHttpServerRequestVerbs::VERB_POST, true, &FNovaBridgeRuntimeModule::HandleSequencerStop);
	Bind(TEXT("/nova/sequencer/info"), EHttpServerRequestVerbs::VERB_GET, true, &FNovaBridgeRuntimeModule::HandleSequencerInfo);
	Bind(TEXT("/nova/pcg/generate"), EHttpServerRequestVerbs::VERB_POST, true, &FNovaBridgeRuntimeModule::HandlePcgGenerate);

	FHttpServerModule::Get().StartAllListeners();
	bServerStarted = true;
	StartEventWebSocketServer();

	UE_LOG(LogNovaBridgeRuntime, Log, TEXT("NovaBridgeRuntime server listening on 127.0.0.1:%d"), HttpPort);
	UE_LOG(LogNovaBridgeRuntime, Log, TEXT("Runtime pairing code: %s (valid until %s UTC)"),
		*RuntimePairingCode, *RuntimePairingExpiryUtc.ToIso8601());
	UE_LOG(LogNovaBridgeRuntime, Log, TEXT("Runtime token in memory only, expires at %s UTC"), *RuntimeTokenExpiryUtc.ToIso8601());
	UE_LOG(LogNovaBridgeRuntime, Log, TEXT("Runtime default role: %s"), *RuntimeDefaultRole);
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
