#include "NovaBridgeModule.h"
#include "NovaBridgeCoreTypes.h"
#include "NovaBridgeEditorInternals.h"
#include "NovaBridgeHttpUtils.h"

#include "Dom/JsonObject.h"
#include "HAL/PlatformMisc.h"
#include "HttpPath.h"
#include "HttpServerModule.h"
#include "HttpServerRequest.h"
#include "HttpServerResponse.h"
#include "IHttpRouter.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

using NovaBridgeCore::HttpVerbToString;

void FNovaBridgeModule::StartHttpServer()
{
	ApiRouteCount = 0;
	RequiredApiKey.Reset();

	int32 ParsedPort = 0;
	if (FParse::Value(FCommandLine::Get(), TEXT("NovaBridgePort="), ParsedPort))
	{
		if (ParsedPort > 0 && ParsedPort <= 65535)
		{
			HttpPort = static_cast<uint32>(ParsedPort);
		}
		else
		{
			UE_LOG(LogNovaBridge, Warning, TEXT("Invalid -NovaBridgePort=%d, falling back to default %d"), ParsedPort, HttpPort);
		}
	}

	FString ParsedApiKey;
	if (FParse::Value(FCommandLine::Get(), TEXT("NovaBridgeApiKey="), ParsedApiKey))
	{
		ParsedApiKey.TrimStartAndEndInline();
		if (!ParsedApiKey.IsEmpty())
		{
			RequiredApiKey = ParsedApiKey;
		}
	}
	if (RequiredApiKey.IsEmpty())
	{
		FString EnvApiKey = FPlatformMisc::GetEnvironmentVariable(TEXT("NOVABRIDGE_API_KEY"));
		EnvApiKey.TrimStartAndEndInline();
		if (!EnvApiKey.IsEmpty())
		{
			RequiredApiKey = EnvApiKey;
		}
	}
	if (RequiredApiKey.IsEmpty())
	{
		UE_LOG(LogNovaBridge, Warning, TEXT("[WARNING] NovaBridge running without authentication."));
	}

	FString ParsedDefaultRole;
	if (FParse::Value(FCommandLine::Get(), TEXT("NovaBridgeDefaultRole="), ParsedDefaultRole))
	{
		const FString NormalizedRole = NovaBridgeCore::NormalizeRoleName(ParsedDefaultRole);
		if (!NormalizedRole.IsEmpty())
		{
			SetNovaBridgeDefaultRole(NormalizedRole);
		}
	}
	if (GetNovaBridgeDefaultRole().IsEmpty())
	{
		const FString EnvDefaultRole = FPlatformMisc::GetEnvironmentVariable(TEXT("NOVABRIDGE_DEFAULT_ROLE"));
		const FString NormalizedEnvRole = NovaBridgeCore::NormalizeRoleName(EnvDefaultRole);
		if (!NormalizedEnvRole.IsEmpty())
		{
			SetNovaBridgeDefaultRole(NormalizedEnvRole);
		}
	}
	if (GetNovaBridgeDefaultRole().IsEmpty())
	{
		SetNovaBridgeDefaultRole(TEXT("admin"));
	}

	UE_LOG(LogNovaBridge, Log, TEXT("NovaBridge default role: %s"), *GetNovaBridgeDefaultRole());
	ResetNovaBridgeEditorControlState();
	RegisterEditorCapabilities(EventWsPort);

	HttpRouter = FHttpServerModule::Get().GetHttpRouter(HttpPort);
	if (!HttpRouter)
	{
		UE_LOG(LogNovaBridge, Error, TEXT("Failed to get HTTP router on port %d"), HttpPort);
		return;
	}

	auto Bind = [this](const TCHAR* Path, EHttpServerRequestVerbs Verbs, bool (FNovaBridgeModule::*Handler)(const FHttpServerRequest&, const FHttpResultCallback&))
	{
		const FString RoutePath(Path);
		ApiRouteCount++;
		RouteHandles.Add(HttpRouter->BindRoute(
			FHttpPath(Path), Verbs,
			FHttpRequestHandler::CreateLambda([this, Handler, RoutePath](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete) -> bool
			{
				if (!IsApiKeyAuthorized(Request, OnComplete))
				{
					PushAuditEntry(RoutePath, RoutePath, TEXT("n/a"), TEXT("denied"), TEXT("API key unauthorized"));
					return true;
				}

				const FString Role = ResolveRoleFromRequest(Request);
				if (!IsRouteAllowedForRole(Role, RoutePath, Request.Verb))
				{
					PushAuditEntry(RoutePath, RoutePath, Role, TEXT("denied"), TEXT("Role does not have permission for this endpoint"));
					SendErrorResponse(OnComplete, TEXT("Permission denied for role on this endpoint"), 403);
					return true;
				}

				const int32 RateLimit = GetRouteRateLimitPerMinute(Role, RoutePath);
				FString RateError;
				const FString RateBucket = Role + TEXT("|") + RoutePath;
				if (!ConsumeRateLimit(RateBucket, RateLimit, RateError))
				{
					PushAuditEntry(RoutePath, RoutePath, Role, TEXT("rate_limited"), RateError);
					SendErrorResponse(OnComplete, RateError, 429);
					return true;
				}

				UE_LOG(LogNovaBridge, Verbose, TEXT("[%s] %s %s role=%s"),
					*FDateTime::Now().ToString(),
					HttpVerbToString(Request.Verb),
					*Request.RelativePath.GetPath(),
					*Role);
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

	auto BindWithAuditName = [&Bind](const TCHAR* Path, EHttpServerRequestVerbs Verbs, bool (FNovaBridgeModule::*Handler)(const FHttpServerRequest&, const FHttpResultCallback&))
	{
		Bind(Path, Verbs, Handler);
	};

	// Health check
	BindWithAuditName(TEXT("/nova/health"), EHttpServerRequestVerbs::VERB_GET, &FNovaBridgeModule::HandleHealth);
	BindWithAuditName(TEXT("/nova/project/info"), EHttpServerRequestVerbs::VERB_GET, &FNovaBridgeModule::HandleProjectInfo);
	BindWithAuditName(TEXT("/nova/caps"), EHttpServerRequestVerbs::VERB_GET, &FNovaBridgeModule::HandleCapabilities);
	BindWithAuditName(TEXT("/nova/events"), EHttpServerRequestVerbs::VERB_GET, &FNovaBridgeModule::HandleEvents);
	BindWithAuditName(TEXT("/nova/audit"), EHttpServerRequestVerbs::VERB_GET, &FNovaBridgeModule::HandleAuditTrail);
	BindWithAuditName(TEXT("/nova/executePlan"), EHttpServerRequestVerbs::VERB_POST, &FNovaBridgeModule::HandleExecutePlan);
	BindWithAuditName(TEXT("/nova/undo"), EHttpServerRequestVerbs::VERB_POST, &FNovaBridgeModule::HandleUndo);

	// Scene
	BindWithAuditName(TEXT("/nova/scene/list"), EHttpServerRequestVerbs::VERB_GET, &FNovaBridgeModule::HandleSceneList);
	BindWithAuditName(TEXT("/nova/scene/spawn"), EHttpServerRequestVerbs::VERB_POST, &FNovaBridgeModule::HandleSceneSpawn);
	BindWithAuditName(TEXT("/nova/scene/delete"), EHttpServerRequestVerbs::VERB_POST, &FNovaBridgeModule::HandleSceneDelete);
	BindWithAuditName(TEXT("/nova/scene/transform"), EHttpServerRequestVerbs::VERB_POST, &FNovaBridgeModule::HandleSceneTransform);
	BindWithAuditName(TEXT("/nova/scene/get"), EHttpServerRequestVerbs::VERB_GET | EHttpServerRequestVerbs::VERB_POST, &FNovaBridgeModule::HandleSceneGet);
	BindWithAuditName(TEXT("/nova/scene/set-property"), EHttpServerRequestVerbs::VERB_POST, &FNovaBridgeModule::HandleSceneSetProperty);

	// Assets
	BindWithAuditName(TEXT("/nova/asset/list"), EHttpServerRequestVerbs::VERB_GET | EHttpServerRequestVerbs::VERB_POST, &FNovaBridgeModule::HandleAssetList);
	BindWithAuditName(TEXT("/nova/asset/create"), EHttpServerRequestVerbs::VERB_POST, &FNovaBridgeModule::HandleAssetCreate);
	BindWithAuditName(TEXT("/nova/asset/duplicate"), EHttpServerRequestVerbs::VERB_POST, &FNovaBridgeModule::HandleAssetDuplicate);
	BindWithAuditName(TEXT("/nova/asset/delete"), EHttpServerRequestVerbs::VERB_POST, &FNovaBridgeModule::HandleAssetDelete);
	BindWithAuditName(TEXT("/nova/asset/rename"), EHttpServerRequestVerbs::VERB_POST, &FNovaBridgeModule::HandleAssetRename);
	BindWithAuditName(TEXT("/nova/asset/info"), EHttpServerRequestVerbs::VERB_GET | EHttpServerRequestVerbs::VERB_POST, &FNovaBridgeModule::HandleAssetInfo);
	BindWithAuditName(TEXT("/nova/asset/import"), EHttpServerRequestVerbs::VERB_POST, &FNovaBridgeModule::HandleAssetImport);

	// Mesh
	BindWithAuditName(TEXT("/nova/mesh/create"), EHttpServerRequestVerbs::VERB_POST, &FNovaBridgeModule::HandleMeshCreate);
	BindWithAuditName(TEXT("/nova/mesh/get"), EHttpServerRequestVerbs::VERB_GET | EHttpServerRequestVerbs::VERB_POST, &FNovaBridgeModule::HandleMeshGet);
	BindWithAuditName(TEXT("/nova/mesh/primitive"), EHttpServerRequestVerbs::VERB_POST, &FNovaBridgeModule::HandleMeshPrimitive);

	// Material
	BindWithAuditName(TEXT("/nova/material/create"), EHttpServerRequestVerbs::VERB_POST, &FNovaBridgeModule::HandleMaterialCreate);
	BindWithAuditName(TEXT("/nova/material/set-param"), EHttpServerRequestVerbs::VERB_POST, &FNovaBridgeModule::HandleMaterialSetParam);
	BindWithAuditName(TEXT("/nova/material/get"), EHttpServerRequestVerbs::VERB_GET | EHttpServerRequestVerbs::VERB_POST, &FNovaBridgeModule::HandleMaterialGet);
	BindWithAuditName(TEXT("/nova/material/create-instance"), EHttpServerRequestVerbs::VERB_POST, &FNovaBridgeModule::HandleMaterialCreateInstance);

	// Viewport
	BindWithAuditName(TEXT("/nova/viewport/screenshot"), EHttpServerRequestVerbs::VERB_GET, &FNovaBridgeModule::HandleViewportScreenshot);
	BindWithAuditName(TEXT("/nova/viewport/camera/set"), EHttpServerRequestVerbs::VERB_POST, &FNovaBridgeModule::HandleViewportSetCamera);
	BindWithAuditName(TEXT("/nova/viewport/camera/get"), EHttpServerRequestVerbs::VERB_GET, &FNovaBridgeModule::HandleViewportGetCamera);

	// Blueprint
	BindWithAuditName(TEXT("/nova/blueprint/create"), EHttpServerRequestVerbs::VERB_POST, &FNovaBridgeModule::HandleBlueprintCreate);
	BindWithAuditName(TEXT("/nova/blueprint/add-component"), EHttpServerRequestVerbs::VERB_POST, &FNovaBridgeModule::HandleBlueprintAddComponent);
	BindWithAuditName(TEXT("/nova/blueprint/compile"), EHttpServerRequestVerbs::VERB_POST, &FNovaBridgeModule::HandleBlueprintCompile);

	// Build
	BindWithAuditName(TEXT("/nova/build/lighting"), EHttpServerRequestVerbs::VERB_POST, &FNovaBridgeModule::HandleBuildLighting);
	BindWithAuditName(TEXT("/nova/exec/command"), EHttpServerRequestVerbs::VERB_POST, &FNovaBridgeModule::HandleExecCommand);

	// Stream
	BindWithAuditName(TEXT("/nova/stream/start"), EHttpServerRequestVerbs::VERB_POST, &FNovaBridgeModule::HandleStreamStart);
	BindWithAuditName(TEXT("/nova/stream/stop"), EHttpServerRequestVerbs::VERB_POST, &FNovaBridgeModule::HandleStreamStop);
	BindWithAuditName(TEXT("/nova/stream/config"), EHttpServerRequestVerbs::VERB_POST, &FNovaBridgeModule::HandleStreamConfig);
	BindWithAuditName(TEXT("/nova/stream/status"), EHttpServerRequestVerbs::VERB_GET, &FNovaBridgeModule::HandleStreamStatus);

	// PCG
	BindWithAuditName(TEXT("/nova/pcg/list-graphs"), EHttpServerRequestVerbs::VERB_GET, &FNovaBridgeModule::HandlePcgListGraphs);
	BindWithAuditName(TEXT("/nova/pcg/create-volume"), EHttpServerRequestVerbs::VERB_POST, &FNovaBridgeModule::HandlePcgCreateVolume);
	BindWithAuditName(TEXT("/nova/pcg/generate"), EHttpServerRequestVerbs::VERB_POST, &FNovaBridgeModule::HandlePcgGenerate);
	BindWithAuditName(TEXT("/nova/pcg/set-param"), EHttpServerRequestVerbs::VERB_POST, &FNovaBridgeModule::HandlePcgSetParam);
	BindWithAuditName(TEXT("/nova/pcg/cleanup"), EHttpServerRequestVerbs::VERB_POST, &FNovaBridgeModule::HandlePcgCleanup);

	// Sequencer
	BindWithAuditName(TEXT("/nova/sequencer/create"), EHttpServerRequestVerbs::VERB_POST, &FNovaBridgeModule::HandleSequencerCreate);
	BindWithAuditName(TEXT("/nova/sequencer/add-track"), EHttpServerRequestVerbs::VERB_POST, &FNovaBridgeModule::HandleSequencerAddTrack);
	BindWithAuditName(TEXT("/nova/sequencer/set-keyframe"), EHttpServerRequestVerbs::VERB_POST, &FNovaBridgeModule::HandleSequencerSetKeyframe);
	BindWithAuditName(TEXT("/nova/sequencer/play"), EHttpServerRequestVerbs::VERB_POST, &FNovaBridgeModule::HandleSequencerPlay);
	BindWithAuditName(TEXT("/nova/sequencer/stop"), EHttpServerRequestVerbs::VERB_POST, &FNovaBridgeModule::HandleSequencerStop);
	BindWithAuditName(TEXT("/nova/sequencer/scrub"), EHttpServerRequestVerbs::VERB_POST, &FNovaBridgeModule::HandleSequencerScrub);
	BindWithAuditName(TEXT("/nova/sequencer/render"), EHttpServerRequestVerbs::VERB_POST, &FNovaBridgeModule::HandleSequencerRender);
	BindWithAuditName(TEXT("/nova/sequencer/info"), EHttpServerRequestVerbs::VERB_GET, &FNovaBridgeModule::HandleSequencerInfo);

	// Optimize
	BindWithAuditName(TEXT("/nova/optimize/nanite"), EHttpServerRequestVerbs::VERB_POST, &FNovaBridgeModule::HandleOptimizeNanite);
	BindWithAuditName(TEXT("/nova/optimize/lod"), EHttpServerRequestVerbs::VERB_POST, &FNovaBridgeModule::HandleOptimizeLod);
	BindWithAuditName(TEXT("/nova/optimize/lumen"), EHttpServerRequestVerbs::VERB_POST, &FNovaBridgeModule::HandleOptimizeLumen);
	BindWithAuditName(TEXT("/nova/optimize/stats"), EHttpServerRequestVerbs::VERB_GET, &FNovaBridgeModule::HandleOptimizeStats);
	BindWithAuditName(TEXT("/nova/optimize/textures"), EHttpServerRequestVerbs::VERB_POST, &FNovaBridgeModule::HandleOptimizeTextures);
	BindWithAuditName(TEXT("/nova/optimize/collision"), EHttpServerRequestVerbs::VERB_POST, &FNovaBridgeModule::HandleOptimizeCollision);

	FHttpServerModule::Get().StartAllListeners();
	UE_LOG(LogNovaBridge, Log, TEXT("NovaBridge server listening on 127.0.0.1:%d (UE HTTP default bind address) with %d API routes"), HttpPort, ApiRouteCount);
	if (!RequiredApiKey.IsEmpty())
	{
		UE_LOG(LogNovaBridge, Log, TEXT("NovaBridge API key auth is enabled"));
	}
}

void FNovaBridgeModule::StopHttpServer()
{
	if (HttpRouter)
	{
		for (const FHttpRouteHandle& Handle : RouteHandles)
		{
			HttpRouter->UnbindRoute(Handle);
		}
		RouteHandles.Empty();
	}

	ApiRouteCount = 0;
	FHttpServerModule::Get().StopAllListeners();
}

TSharedPtr<FJsonObject> FNovaBridgeModule::ParseRequestBody(const FHttpServerRequest& Request)
{
	if (Request.Body.Num() == 0)
	{
		return nullptr;
	}

	// Body bytes are NOT null-terminated. Copy and append null before converting to FString.
	TArray<uint8> NullTermBody(Request.Body);
	NullTermBody.Add(0);
	const FString BodyStr = FString(UTF8_TO_TCHAR(reinterpret_cast<const ANSICHAR*>(NullTermBody.GetData())));

	TSharedPtr<FJsonObject> JsonObj;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(BodyStr);
	if (!FJsonSerializer::Deserialize(Reader, JsonObj))
	{
		return nullptr;
	}

	return JsonObj;
}

void FNovaBridgeModule::AddCorsHeaders(TUniquePtr<FHttpServerResponse>& Response) const
{
	if (!Response)
	{
		return;
	}

	Response->Headers.FindOrAdd(TEXT("Access-Control-Allow-Origin")).Add(TEXT("*"));
	Response->Headers.FindOrAdd(TEXT("Access-Control-Allow-Methods")).Add(TEXT("GET, POST, OPTIONS"));
	Response->Headers.FindOrAdd(TEXT("Access-Control-Allow-Headers")).Add(TEXT("Content-Type, Authorization, X-API-Key, X-NovaBridge-Role"));
	Response->Headers.FindOrAdd(TEXT("Access-Control-Max-Age")).Add(TEXT("86400"));
}

bool FNovaBridgeModule::IsApiKeyAuthorized(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	if (Request.Verb == EHttpServerRequestVerbs::VERB_OPTIONS || RequiredApiKey.IsEmpty())
	{
		return true;
	}

	FString PresentedKey;
	for (const TPair<FString, TArray<FString>>& Header : Request.Headers)
	{
		if (Header.Value.Num() == 0)
		{
			continue;
		}

		if (Header.Key.Equals(TEXT("X-API-Key"), ESearchCase::IgnoreCase))
		{
			PresentedKey = Header.Value[0];
			break;
		}
		if (Header.Key.Equals(TEXT("Authorization"), ESearchCase::IgnoreCase))
		{
			const FString& RawAuth = Header.Value[0];
			static const FString BearerPrefix = TEXT("Bearer ");
			if (RawAuth.StartsWith(BearerPrefix, ESearchCase::IgnoreCase))
			{
				PresentedKey = RawAuth.Mid(BearerPrefix.Len());
				break;
			}
		}
	}

	PresentedKey.TrimStartAndEndInline();
	if (!PresentedKey.IsEmpty() && PresentedKey == RequiredApiKey)
	{
		return true;
	}

	SendErrorResponse(OnComplete, TEXT("Unauthorized. Provide X-API-Key or Authorization: Bearer <key>."), 401);
	return false;
}

bool FNovaBridgeModule::HandleCorsPreflight(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	(void)Request;
	SendOkResponse(OnComplete);
	return true;
}

void FNovaBridgeModule::SendJsonResponse(const FHttpResultCallback& OnComplete, TSharedPtr<FJsonObject> JsonObj, int32 StatusCode)
{
	FString ResponseStr;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ResponseStr);
	FJsonSerializer::Serialize(JsonObj.ToSharedRef(), Writer);
	TUniquePtr<FHttpServerResponse> Response = FHttpServerResponse::Create(ResponseStr, TEXT("application/json"));
	Response->Code = static_cast<EHttpServerResponseCodes>(StatusCode);
	AddCorsHeaders(Response);
	OnComplete(MoveTemp(Response));
}

void FNovaBridgeModule::SendErrorResponse(const FHttpResultCallback& OnComplete, const FString& Error, int32 StatusCode)
{
	const TSharedPtr<FJsonObject> JsonObj = MakeShared<FJsonObject>();
	JsonObj->SetStringField(TEXT("status"), TEXT("error"));
	JsonObj->SetStringField(TEXT("error"), Error);
	JsonObj->SetNumberField(TEXT("code"), StatusCode);
	SendJsonResponse(OnComplete, JsonObj, StatusCode);
}

void FNovaBridgeModule::SendOkResponse(const FHttpResultCallback& OnComplete)
{
	const TSharedPtr<FJsonObject> JsonObj = MakeShared<FJsonObject>();
	JsonObj->SetStringField(TEXT("status"), TEXT("ok"));
	SendJsonResponse(OnComplete, JsonObj, 200);
}
