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
static const TCHAR* RuntimeModeName = TEXT("runtime");
using NovaBridgeCore::HttpVerbToString;
using NovaBridgeCore::MakeJsonStringArray;
using NovaBridgeCore::NormalizeEventType;

static const TArray<FString>& SupportedRuntimeEventTypes()
{
	static const TArray<FString> Types =
	{
		TEXT("audit"),
		TEXT("spawn"),
		TEXT("delete"),
		TEXT("plan_step"),
		TEXT("plan_complete"),
		TEXT("error")
	};
	return Types;
}

static const TArray<FString>& RuntimeAllowedClasses();

static TSharedPtr<FJsonObject> BuildRuntimeSpawnBoundsJson()
{
	const FVector& MinSpawnBounds = NovaBridgeCore::MinSpawnBounds();
	const FVector& MaxSpawnBounds = NovaBridgeCore::MaxSpawnBounds();

	TSharedPtr<FJsonObject> SpawnBounds = MakeShared<FJsonObject>();
	TSharedPtr<FJsonObject> MinBounds = MakeShared<FJsonObject>();
	MinBounds->SetNumberField(TEXT("x"), MinSpawnBounds.X);
	MinBounds->SetNumberField(TEXT("y"), MinSpawnBounds.Y);
	MinBounds->SetNumberField(TEXT("z"), MinSpawnBounds.Z);
	TSharedPtr<FJsonObject> MaxBounds = MakeShared<FJsonObject>();
	MaxBounds->SetNumberField(TEXT("x"), MaxSpawnBounds.X);
	MaxBounds->SetNumberField(TEXT("y"), MaxSpawnBounds.Y);
	MaxBounds->SetNumberField(TEXT("z"), MaxSpawnBounds.Z);
	SpawnBounds->SetObjectField(TEXT("min"), MinBounds);
	SpawnBounds->SetObjectField(TEXT("max"), MaxBounds);
	return SpawnBounds;
}

static TSharedPtr<FJsonObject> BuildRuntimePermissionsSnapshot(const int32 MaxSpawnPerPlan, const int32 MaxPlanSteps, const int32 MaxExecutePlanPerMinute)
{
	TSharedPtr<FJsonObject> Permissions = MakeShared<FJsonObject>();
	Permissions->SetStringField(TEXT("mode"), TEXT("runtime"));
	Permissions->SetBoolField(TEXT("localhost_only"), true);
	Permissions->SetBoolField(TEXT("pairing_required"), true);
	Permissions->SetBoolField(TEXT("token_required"), true);

	TArray<TSharedPtr<FJsonValue>> AllowedClassValues;
	for (const FString& AllowedClass : RuntimeAllowedClasses())
	{
		AllowedClassValues.Add(MakeShared<FJsonValueString>(AllowedClass));
	}

	TSharedPtr<FJsonObject> SpawnPolicy = MakeShared<FJsonObject>();
	SpawnPolicy->SetBoolField(TEXT("allowed"), true);
	SpawnPolicy->SetArrayField(TEXT("allowedClasses"), AllowedClassValues);
	SpawnPolicy->SetObjectField(TEXT("bounds"), BuildRuntimeSpawnBoundsJson());
	SpawnPolicy->SetNumberField(TEXT("max_spawn_per_plan"), MaxSpawnPerPlan);
	Permissions->SetObjectField(TEXT("spawn"), SpawnPolicy);

	TSharedPtr<FJsonObject> ExecutePlanPolicy = MakeShared<FJsonObject>();
	ExecutePlanPolicy->SetBoolField(TEXT("allowed"), true);
	ExecutePlanPolicy->SetArrayField(
		TEXT("allowed_actions"),
		MakeJsonStringArray(NovaBridgeCore::GetSupportedPlanActionsRef(NovaBridgeCore::ENovaBridgePlanMode::Runtime)));
	ExecutePlanPolicy->SetNumberField(TEXT("max_steps"), MaxPlanSteps);
	ExecutePlanPolicy->SetNumberField(TEXT("max_requests_per_minute"), MaxExecutePlanPerMinute);
	Permissions->SetObjectField(TEXT("executePlan"), ExecutePlanPolicy);

	TSharedPtr<FJsonObject> UndoPolicy = MakeShared<FJsonObject>();
	UndoPolicy->SetBoolField(TEXT("allowed"), true);
	UndoPolicy->SetStringField(TEXT("supported"), TEXT("spawn"));
	Permissions->SetObjectField(TEXT("undo"), UndoPolicy);

	TSharedPtr<FJsonObject> EventsPolicy = MakeShared<FJsonObject>();
	EventsPolicy->SetBoolField(TEXT("allowed"), true);
	EventsPolicy->SetBoolField(TEXT("subscription_ack_required"), true);
	Permissions->SetObjectField(TEXT("events"), EventsPolicy);

	return Permissions;
}

static void RegisterRuntimeCapabilities(const int32 MaxSpawnPerPlan, const int32 MaxPlanSteps, const int32 MaxExecutePlanPerMinute, const uint32 InEventWsPort)
{
	using namespace NovaBridgeCore;

	FCapabilityRegistry& Registry = FCapabilityRegistry::Get();
	Registry.Reset();

	auto RegisterCapability = [&Registry](const FString& Action, const TSharedPtr<FJsonObject>& Data)
	{
		FCapabilityRecord Capability;
		Capability.Action = Action;
		Capability.Data = Data.IsValid() ? Data : MakeShared<FJsonObject>();
		Registry.RegisterCapability(Capability);
	};

	TArray<TSharedPtr<FJsonValue>> AllowedClassValues;
	for (const FString& AllowedClass : RuntimeAllowedClasses())
	{
		AllowedClassValues.Add(MakeShared<FJsonValueString>(AllowedClass));
	}

	TSharedPtr<FJsonObject> SpawnData = MakeShared<FJsonObject>();
	SpawnData->SetArrayField(TEXT("allowedClasses"), AllowedClassValues);
	SpawnData->SetObjectField(TEXT("bounds"), BuildRuntimeSpawnBoundsJson());
	SpawnData->SetNumberField(TEXT("max_spawn_per_plan"), MaxSpawnPerPlan);
	RegisterCapability(TEXT("spawn"), SpawnData);

	RegisterCapability(TEXT("delete"), MakeShared<FJsonObject>());
	RegisterCapability(TEXT("set"), MakeShared<FJsonObject>());

	TSharedPtr<FJsonObject> ExecutePlanData = MakeShared<FJsonObject>();
	ExecutePlanData->SetNumberField(TEXT("max_steps"), MaxPlanSteps);
	ExecutePlanData->SetNumberField(TEXT("max_requests_per_minute"), MaxExecutePlanPerMinute);
	ExecutePlanData->SetArrayField(
		TEXT("actions"),
		MakeJsonStringArray(NovaBridgeCore::GetSupportedPlanActionsRef(NovaBridgeCore::ENovaBridgePlanMode::Runtime)));
	RegisterCapability(TEXT("executePlan"), ExecutePlanData);

	TSharedPtr<FJsonObject> UndoData = MakeShared<FJsonObject>();
	UndoData->SetStringField(TEXT("supported"), TEXT("spawn"));
	RegisterCapability(TEXT("undo"), UndoData);

	TSharedPtr<FJsonObject> AuditData = MakeShared<FJsonObject>();
	AuditData->SetStringField(TEXT("endpoint"), TEXT("/nova/audit"));
	RegisterCapability(TEXT("audit"), AuditData);

	TSharedPtr<FJsonObject> EventsData = MakeShared<FJsonObject>();
	EventsData->SetStringField(TEXT("endpoint"), TEXT("/nova/events"));
	EventsData->SetStringField(TEXT("ws_url"), FString::Printf(TEXT("ws://localhost:%d"), InEventWsPort));
	EventsData->SetArrayField(TEXT("supported_types"), MakeJsonStringArray(SupportedRuntimeEventTypes()));
	EventsData->SetStringField(TEXT("filter_query_param"), TEXT("types"));
	EventsData->SetStringField(TEXT("subscription_action"), TEXT("{\"action\":\"subscribe\",\"types\":[\"spawn\",\"error\"]}"));
	RegisterCapability(TEXT("events"), EventsData);
}

static const TArray<FString>& RuntimeAllowedClasses()
{
	return NovaBridgeCore::RuntimeAllowedSpawnClasses();
}
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
	RegisterRuntimeCapabilities(MaxSpawnPerPlan, MaxPlanSteps, MaxExecutePlanPerMinute, EventWsPort);

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

void FNovaBridgeRuntimeModule::QueueRuntimeEvent(const TSharedPtr<FJsonObject>& EventObj)
{
	if (!EventObj.IsValid())
	{
		return;
	}
	if (!EventObj->HasTypedField<EJson::String>(TEXT("type")))
	{
		EventObj->SetStringField(TEXT("type"), TEXT("audit"));
	}
	if (!EventObj->HasTypedField<EJson::String>(TEXT("mode")))
	{
		EventObj->SetStringField(TEXT("mode"), RuntimeModeName);
	}
	if (!EventObj->HasTypedField<EJson::String>(TEXT("timestamp_utc")))
	{
		EventObj->SetStringField(TEXT("timestamp_utc"), FDateTime::UtcNow().ToIso8601());
	}

	const FString EventType = NormalizeEventType(EventObj->GetStringField(TEXT("type")));
	FString SerializedEvent;
	{
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&SerializedEvent);
		FJsonSerializer::Serialize(EventObj.ToSharedRef(), Writer);
	}

	FScopeLock EventLock(&RuntimeEventQueueMutex);
	RuntimePendingEventPayloads.Add(MoveTemp(SerializedEvent));
	RuntimePendingEventTypes.Add(EventType);
	if (RuntimePendingEventPayloads.Num() > RuntimePendingEventsLimit)
	{
		const int32 RemoveCount = RuntimePendingEventPayloads.Num() - RuntimePendingEventsLimit;
		RuntimePendingEventPayloads.RemoveAt(0, RemoveCount, EAllowShrinking::No);
		RuntimePendingEventTypes.RemoveAt(0, RemoveCount, EAllowShrinking::No);
	}
}

void FNovaBridgeRuntimeModule::PushAuditEntry(const FString& Route, const FString& Action, const FString& Status, const FString& Message)
{
	FRuntimeAuditEntry Entry;
	Entry.TimestampUtc = FDateTime::UtcNow().ToIso8601();
	Entry.Route = Route;
	Entry.Action = Action;
	Entry.Status = Status;
	Entry.Message = Message;

	FScopeLock Lock(&RuntimeAuditMutex);
	RuntimeAuditTrail.Add(Entry);
	if (RuntimeAuditTrail.Num() > RuntimeAuditLimit)
	{
		RuntimeAuditTrail.RemoveAt(0, RuntimeAuditTrail.Num() - RuntimeAuditLimit, EAllowShrinking::No);
	}

	TSharedPtr<FJsonObject> EventObj = MakeShareable(new FJsonObject);
	EventObj->SetStringField(TEXT("type"), TEXT("audit"));
	EventObj->SetStringField(TEXT("mode"), RuntimeModeName);
	EventObj->SetStringField(TEXT("timestamp_utc"), Entry.TimestampUtc);
	EventObj->SetStringField(TEXT("route"), Entry.Route);
	EventObj->SetStringField(TEXT("action"), Entry.Action);
	EventObj->SetStringField(TEXT("status"), Entry.Status);
	EventObj->SetStringField(TEXT("message"), Entry.Message);
	QueueRuntimeEvent(EventObj);
}

void FNovaBridgeRuntimeModule::PushRuntimeUndoEntry(const FString& Action, const FString& ActorName, const FString& ActorLabel)
{
	FRuntimeUndoEntry Entry;
	Entry.TimestampUtc = FDateTime::UtcNow().ToIso8601();
	Entry.Action = Action;
	Entry.ActorName = ActorName;
	Entry.ActorLabel = ActorLabel;

	FScopeLock Lock(&RuntimeUndoMutex);
	RuntimeUndoStack.Add(Entry);
	if (RuntimeUndoStack.Num() > RuntimeUndoLimit)
	{
		RuntimeUndoStack.RemoveAt(0, RuntimeUndoStack.Num() - RuntimeUndoLimit, EAllowShrinking::No);
	}
}

bool FNovaBridgeRuntimeModule::PopRuntimeUndoEntry(FRuntimeUndoEntry& OutEntry)
{
	FScopeLock Lock(&RuntimeUndoMutex);
	if (RuntimeUndoStack.Num() == 0)
	{
		return false;
	}

	OutEntry = RuntimeUndoStack.Last();
	RuntimeUndoStack.Pop(EAllowShrinking::No);
	return true;
}

bool FNovaBridgeRuntimeModule::HandleHealth(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	(void)Request;
	const int32 AuditCount = [&]()
	{
		FScopeLock Lock(&RuntimeAuditMutex);
		return RuntimeAuditTrail.Num();
	}();

	TSharedPtr<FJsonObject> JsonObj = MakeShareable(new FJsonObject);
	JsonObj->SetStringField(TEXT("status"), TEXT("ok"));
	JsonObj->SetStringField(TEXT("version"), NovaBridgeCore::PluginVersion);
	JsonObj->SetStringField(TEXT("mode"), RuntimeModeName);
	JsonObj->SetStringField(TEXT("engine"), TEXT("UnrealEngine"));
	JsonObj->SetStringField(TEXT("project_name"), FApp::GetProjectName());
	JsonObj->SetNumberField(TEXT("port"), HttpPort);
	JsonObj->SetNumberField(TEXT("routes"), ApiRouteCount);
	JsonObj->SetNumberField(TEXT("events_ws_port"), EventWsPort);
	JsonObj->SetBoolField(TEXT("token_required"), bRequireRuntimeToken);
	JsonObj->SetBoolField(TEXT("localhost_only"), true);
	JsonObj->SetNumberField(TEXT("audit_entries"), AuditCount);
	JsonObj->SetStringField(TEXT("token_expires_utc"), RuntimeTokenExpiryUtc.ToIso8601());
	SendJsonResponse(OnComplete, JsonObj);
	return true;
}

bool FNovaBridgeRuntimeModule::HandleCapabilities(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	(void)Request;
	TArray<TSharedPtr<FJsonValue>> Capabilities;
	TArray<NovaBridgeCore::FCapabilityRecord> Snapshot = NovaBridgeCore::FCapabilityRegistry::Get().Snapshot();
	if (Snapshot.Num() == 0)
	{
		RegisterRuntimeCapabilities(MaxSpawnPerPlan, MaxPlanSteps, MaxExecutePlanPerMinute, EventWsPort);
		Snapshot = NovaBridgeCore::FCapabilityRegistry::Get().Snapshot();
	}
	Capabilities.Reserve(Snapshot.Num());
	for (const NovaBridgeCore::FCapabilityRecord& Capability : Snapshot)
	{
		Capabilities.Add(MakeShared<FJsonValueObject>(NovaBridgeCore::CapabilityToJson(Capability)));
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetStringField(TEXT("status"), TEXT("ok"));
	Result->SetStringField(TEXT("mode"), RuntimeModeName);
	Result->SetStringField(TEXT("version"), NovaBridgeCore::PluginVersion);
	Result->SetObjectField(TEXT("permissions"), BuildRuntimePermissionsSnapshot(MaxSpawnPerPlan, MaxPlanSteps, MaxExecutePlanPerMinute));
	Result->SetArrayField(TEXT("capabilities"), Capabilities);
	SendJsonResponse(OnComplete, Result);
	return true;
}

bool FNovaBridgeRuntimeModule::HandleAuditTrail(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	int32 Limit = 50;
	if (Request.QueryParams.Contains(TEXT("limit")))
	{
		Limit = FMath::Clamp(FCString::Atoi(*Request.QueryParams[TEXT("limit")]), 1, 500);
	}

	TArray<FRuntimeAuditEntry> Snapshot;
	{
		FScopeLock Lock(&RuntimeAuditMutex);
		Snapshot = RuntimeAuditTrail;
	}

	const int32 StartIndex = FMath::Max(0, Snapshot.Num() - Limit);
	TArray<TSharedPtr<FJsonValue>> Entries;
	Entries.Reserve(Snapshot.Num() - StartIndex);
	for (int32 Index = StartIndex; Index < Snapshot.Num(); ++Index)
	{
		const FRuntimeAuditEntry& Entry = Snapshot[Index];
		TSharedPtr<FJsonObject> EntryObj = MakeShared<FJsonObject>();
		EntryObj->SetStringField(TEXT("timestamp_utc"), Entry.TimestampUtc);
		EntryObj->SetStringField(TEXT("route"), Entry.Route);
		EntryObj->SetStringField(TEXT("action"), Entry.Action);
		EntryObj->SetStringField(TEXT("status"), Entry.Status);
		EntryObj->SetStringField(TEXT("message"), Entry.Message);
		Entries.Add(MakeShared<FJsonValueObject>(EntryObj));
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("status"), TEXT("ok"));
	Result->SetNumberField(TEXT("count"), Entries.Num());
	Result->SetNumberField(TEXT("total"), Snapshot.Num());
	Result->SetArrayField(TEXT("entries"), Entries);
	SendJsonResponse(OnComplete, Result);
	return true;
}

bool FNovaBridgeRuntimeModule::HandlePair(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	const TSharedPtr<FJsonObject> Body = ParseRequestBody(Request);
	if (!Body || !Body->HasTypedField<EJson::String>(TEXT("code")))
	{
		PushAuditEntry(TEXT("/nova/runtime/pair"), TEXT("runtime.pair"), TEXT("error"), TEXT("Missing pairing code"));
		SendErrorResponse(OnComplete, TEXT("Missing pairing code. Provide {\"code\":\"<6-digit>\"}"), 400);
		return true;
	}

	const FString PresentedCode = Body->GetStringField(TEXT("code"));
	const FDateTime NowUtc = FDateTime::UtcNow();
	if (NowUtc > RuntimePairingExpiryUtc)
	{
		const int32 NewCode = FMath::RandRange(100000, 999999);
		RuntimePairingCode = FString::Printf(TEXT("%06d"), NewCode);
		RuntimePairingExpiryUtc = NowUtc + FTimespan::FromMinutes(15.0);
		UE_LOG(LogNovaBridgeRuntime, Warning, TEXT("Runtime pairing code expired; new code generated: %s"), *RuntimePairingCode);
		PushAuditEntry(TEXT("/nova/runtime/pair"), TEXT("runtime.pair"), TEXT("error"), TEXT("Pairing code expired"));
		SendErrorResponse(OnComplete, TEXT("Pairing code expired. Check runtime logs for the refreshed code."), 410);
		return true;
	}

	if (PresentedCode != RuntimePairingCode)
	{
		PushAuditEntry(TEXT("/nova/runtime/pair"), TEXT("runtime.pair"), TEXT("denied"), TEXT("Invalid pairing code"));
		SendErrorResponse(OnComplete, TEXT("Invalid pairing code."), 403);
		return true;
	}

	RuntimeToken = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower);
	RuntimeTokenIssuedUtc = NowUtc;
	RuntimeTokenExpiryUtc = NowUtc + FTimespan::FromHours(1.0);
	const int32 RefreshedCode = FMath::RandRange(100000, 999999);
	RuntimePairingCode = FString::Printf(TEXT("%06d"), RefreshedCode);
	RuntimePairingExpiryUtc = NowUtc + FTimespan::FromMinutes(15.0);
	UE_LOG(LogNovaBridgeRuntime, Log, TEXT("Runtime pair success; pairing code rotated to %s"), *RuntimePairingCode);

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetStringField(TEXT("status"), TEXT("ok"));
	Result->SetStringField(TEXT("mode"), RuntimeModeName);
	Result->SetStringField(TEXT("token"), RuntimeToken);
	Result->SetStringField(TEXT("token_expires_utc"), RuntimeTokenExpiryUtc.ToIso8601());
	SendJsonResponse(OnComplete, Result);
	PushAuditEntry(TEXT("/nova/runtime/pair"), TEXT("runtime.pair"), TEXT("success"), TEXT("Runtime pair succeeded and token rotated"));
	return true;
}

IMPLEMENT_MODULE(FNovaBridgeRuntimeModule, NovaBridgeRuntime)
