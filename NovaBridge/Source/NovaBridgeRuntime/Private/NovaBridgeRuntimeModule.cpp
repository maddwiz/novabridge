#include "NovaBridgeRuntimeModule.h"

#include "NovaBridgeCapabilityRegistry.h"
#include "NovaBridgeCoreTypes.h"
#include "HttpServerModule.h"
#include "IHttpRouter.h"
#include "HttpPath.h"
#include "HttpServerRequest.h"
#include "HttpServerResponse.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Async/Async.h"

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

DEFINE_LOG_CATEGORY_STATIC(LogNovaBridgeRuntime, Log, All);

namespace
{
static const TCHAR* RuntimeModeName = TEXT("runtime");

static const TCHAR* HttpVerbToString(EHttpServerRequestVerbs Verb)
{
	switch (Verb)
	{
	case EHttpServerRequestVerbs::VERB_GET: return TEXT("GET");
	case EHttpServerRequestVerbs::VERB_POST: return TEXT("POST");
	case EHttpServerRequestVerbs::VERB_PUT: return TEXT("PUT");
	case EHttpServerRequestVerbs::VERB_PATCH: return TEXT("PATCH");
	case EHttpServerRequestVerbs::VERB_DELETE: return TEXT("DELETE");
	case EHttpServerRequestVerbs::VERB_OPTIONS: return TEXT("OPTIONS");
	default: return TEXT("UNKNOWN");
	}
}

static FString GetHeaderValueCaseInsensitive(const FHttpServerRequest& Request, const FString& HeaderName)
{
	for (const TPair<FString, TArray<FString>>& Header : Request.Headers)
	{
		if (Header.Key.Equals(HeaderName, ESearchCase::IgnoreCase) && Header.Value.Num() > 0)
		{
			return Header.Value[0];
		}
	}
	return FString();
}

static FString NormalizeHostOnly(FString RawHost)
{
	RawHost.TrimStartAndEndInline();
	if (RawHost.IsEmpty())
	{
		return RawHost;
	}

	FString Host = RawHost;
	if (Host.StartsWith(TEXT("[")))
	{
		int32 ClosingBracketIndex = INDEX_NONE;
		if (Host.FindChar(TEXT(']'), ClosingBracketIndex))
		{
			Host = Host.Mid(1, ClosingBracketIndex - 1);
		}
	}
	else
	{
		FString HostOnly;
		FString Port;
		if (Host.Split(TEXT(":"), &HostOnly, &Port, ESearchCase::CaseSensitive, ESearchDir::FromEnd))
		{
			Host = HostOnly;
		}
	}

	Host.TrimStartAndEndInline();
	Host.ToLowerInline();
	return Host;
}

static bool IsLoopbackHost(const FString& HostHeader)
{
	const FString HostOnly = NormalizeHostOnly(HostHeader);
	if (HostOnly.IsEmpty())
	{
		return true;
	}

	return HostOnly == TEXT("127.0.0.1")
		|| HostOnly == TEXT("localhost")
		|| HostOnly == TEXT("::1");
}

static const TArray<FString>& RuntimeAllowedClasses();

static void RegisterRuntimeCapabilities(const int32 MaxSpawnPerPlan, const int32 MaxPlanSteps, const int32 MaxExecutePlanPerMinute)
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

	TSharedPtr<FJsonObject> SpawnBounds = MakeShared<FJsonObject>();
	TSharedPtr<FJsonObject> MinBounds = MakeShared<FJsonObject>();
	MinBounds->SetNumberField(TEXT("x"), -50000.0);
	MinBounds->SetNumberField(TEXT("y"), -50000.0);
	MinBounds->SetNumberField(TEXT("z"), -50000.0);
	TSharedPtr<FJsonObject> MaxBounds = MakeShared<FJsonObject>();
	MaxBounds->SetNumberField(TEXT("x"), 50000.0);
	MaxBounds->SetNumberField(TEXT("y"), 50000.0);
	MaxBounds->SetNumberField(TEXT("z"), 50000.0);
	SpawnBounds->SetObjectField(TEXT("min"), MinBounds);
	SpawnBounds->SetObjectField(TEXT("max"), MaxBounds);

	TSharedPtr<FJsonObject> SpawnData = MakeShared<FJsonObject>();
	SpawnData->SetArrayField(TEXT("allowedClasses"), AllowedClassValues);
	SpawnData->SetObjectField(TEXT("bounds"), SpawnBounds);
	SpawnData->SetNumberField(TEXT("max_spawn_per_plan"), MaxSpawnPerPlan);
	RegisterCapability(TEXT("spawn"), SpawnData);

	RegisterCapability(TEXT("delete"), MakeShared<FJsonObject>());
	RegisterCapability(TEXT("set"), MakeShared<FJsonObject>());

	TSharedPtr<FJsonObject> ExecutePlanData = MakeShared<FJsonObject>();
	ExecutePlanData->SetNumberField(TEXT("max_steps"), MaxPlanSteps);
	ExecutePlanData->SetNumberField(TEXT("max_requests_per_minute"), MaxExecutePlanPerMinute);
	RegisterCapability(TEXT("executePlan"), ExecutePlanData);

	TSharedPtr<FJsonObject> AuditData = MakeShared<FJsonObject>();
	AuditData->SetStringField(TEXT("endpoint"), TEXT("/nova/audit"));
	RegisterCapability(TEXT("audit"), AuditData);
}

static UWorld* ResolveRuntimeWorld()
{
	if (!GEngine)
	{
		return nullptr;
	}

	for (const FWorldContext& Context : GEngine->GetWorldContexts())
	{
		UWorld* World = Context.World();
		if (!World)
		{
			continue;
		}

		if (Context.WorldType == EWorldType::Game || Context.WorldType == EWorldType::PIE || Context.WorldType == EWorldType::GameRPC)
		{
			return World;
		}
	}

	return nullptr;
}

static AActor* FindActorByNameRuntime(UWorld* World, const FString& Name)
{
	if (!World || Name.IsEmpty())
	{
		return nullptr;
	}

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor)
		{
			continue;
		}
		if (Actor->GetName() == Name)
		{
			return Actor;
		}
	}
	return nullptr;
}

static const TArray<FString>& RuntimeAllowedClasses()
{
	static const TArray<FString> Classes =
	{
		TEXT("StaticMeshActor"),
		TEXT("PointLight"),
		TEXT("DirectionalLight"),
		TEXT("SpotLight"),
		TEXT("CameraActor")
	};
	return Classes;
}

static bool IsRuntimeClassAllowed(const FString& ClassName)
{
	for (const FString& AllowedClass : RuntimeAllowedClasses())
	{
		if (AllowedClass.Equals(ClassName, ESearchCase::IgnoreCase))
		{
			return true;
		}
	}
	return false;
}

static UClass* ResolveRuntimeActorClass(const FString& InClassName)
{
	FString ClassName = InClassName;
	ClassName.TrimStartAndEndInline();
	if (ClassName.IsEmpty())
	{
		return nullptr;
	}

	UClass* ActorClass = FindObject<UClass>(nullptr, *ClassName);
	if (!ActorClass)
	{
		ActorClass = LoadClass<AActor>(nullptr, *ClassName);
	}

	if (!ActorClass)
	{
		if (ClassName == TEXT("StaticMeshActor")) ActorClass = AStaticMeshActor::StaticClass();
		else if (ClassName == TEXT("PointLight")) ActorClass = FindObject<UClass>(nullptr, TEXT("/Script/Engine.PointLight"));
		else if (ClassName == TEXT("DirectionalLight")) ActorClass = FindObject<UClass>(nullptr, TEXT("/Script/Engine.DirectionalLight"));
		else if (ClassName == TEXT("SpotLight")) ActorClass = FindObject<UClass>(nullptr, TEXT("/Script/Engine.SpotLight"));
		else if (ClassName == TEXT("CameraActor")) ActorClass = FindObject<UClass>(nullptr, TEXT("/Script/Engine.CameraActor"));
		if (!ActorClass)
		{
			ActorClass = FindObject<UClass>(nullptr, *(FString(TEXT("/Script/Engine.")) + ClassName));
		}
	}

	return ActorClass;
}

static bool JsonValueToVector(const TSharedPtr<FJsonValue>& Value, FVector& OutVector)
{
	if (!Value.IsValid())
	{
		return false;
	}

	if (Value->Type == EJson::Array)
	{
		const TArray<TSharedPtr<FJsonValue>>& Arr = Value->AsArray();
		if (Arr.Num() != 3)
		{
			return false;
		}

		double X = 0.0;
		double Y = 0.0;
		double Z = 0.0;
		if (!Arr[0].IsValid() || !Arr[1].IsValid() || !Arr[2].IsValid()
			|| !Arr[0]->TryGetNumber(X) || !Arr[1]->TryGetNumber(Y) || !Arr[2]->TryGetNumber(Z))
		{
			return false;
		}

		OutVector = FVector(X, Y, Z);
		return true;
	}

	if (Value->Type == EJson::Object)
	{
		const TSharedPtr<FJsonObject> Obj = Value->AsObject();
		if (!Obj.IsValid() || !Obj->HasField(TEXT("x")) || !Obj->HasField(TEXT("y")) || !Obj->HasField(TEXT("z")))
		{
			return false;
		}

		OutVector = FVector(
			Obj->GetNumberField(TEXT("x")),
			Obj->GetNumberField(TEXT("y")),
			Obj->GetNumberField(TEXT("z"))
		);
		return true;
	}

	return false;
}

static bool JsonValueToRotator(const TSharedPtr<FJsonValue>& Value, FRotator& OutRotator)
{
	if (!Value.IsValid())
	{
		return false;
	}

	if (Value->Type == EJson::Array)
	{
		const TArray<TSharedPtr<FJsonValue>>& Arr = Value->AsArray();
		if (Arr.Num() != 3)
		{
			return false;
		}

		double Pitch = 0.0;
		double Yaw = 0.0;
		double Roll = 0.0;
		if (!Arr[0].IsValid() || !Arr[1].IsValid() || !Arr[2].IsValid()
			|| !Arr[0]->TryGetNumber(Pitch) || !Arr[1]->TryGetNumber(Yaw) || !Arr[2]->TryGetNumber(Roll))
		{
			return false;
		}

		OutRotator = FRotator(Pitch, Yaw, Roll);
		return true;
	}

	if (Value->Type == EJson::Object)
	{
		const TSharedPtr<FJsonObject> Obj = Value->AsObject();
		if (!Obj.IsValid())
		{
			return false;
		}

		if (Obj->HasField(TEXT("pitch")) && Obj->HasField(TEXT("yaw")) && Obj->HasField(TEXT("roll")))
		{
			OutRotator = FRotator(
				Obj->GetNumberField(TEXT("pitch")),
				Obj->GetNumberField(TEXT("yaw")),
				Obj->GetNumberField(TEXT("roll"))
			);
			return true;
		}

		if (Obj->HasField(TEXT("x")) && Obj->HasField(TEXT("y")) && Obj->HasField(TEXT("z")))
		{
			OutRotator = FRotator(
				Obj->GetNumberField(TEXT("x")),
				Obj->GetNumberField(TEXT("y")),
				Obj->GetNumberField(TEXT("z"))
			);
			return true;
		}
	}

	return false;
}

static bool JsonValueToImportText(const TSharedPtr<FJsonValue>& Value, FString& OutText)
{
	if (!Value.IsValid())
	{
		return false;
	}

	switch (Value->Type)
	{
	case EJson::String:
		OutText = Value->AsString();
		return true;
	case EJson::Number:
		OutText = FString::SanitizeFloat(Value->AsNumber());
		return true;
	case EJson::Boolean:
		OutText = Value->AsBool() ? TEXT("True") : TEXT("False");
		return true;
	case EJson::Null:
		OutText.Empty();
		return true;
	default:
		return false;
	}
}

static bool SetRuntimeActorPropertyValue(AActor* Actor, const FString& PropertyName, const FString& Value, FString& OutError)
{
	if (!Actor)
	{
		OutError = TEXT("Actor is null");
		return false;
	}

	FProperty* Prop = Actor->GetClass()->FindPropertyByName(*PropertyName);
	if (!Prop)
	{
		for (TFieldIterator<FProperty> It(Actor->GetClass()); It; ++It)
		{
			if (It->GetName().Equals(PropertyName, ESearchCase::IgnoreCase))
			{
				Prop = *It;
				break;
			}
		}
	}
	if (!Prop)
	{
		OutError = FString::Printf(TEXT("Property not found: %s"), *PropertyName);
		return false;
	}

	void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Actor);
	if (!Prop->ImportText_Direct(*Value, ValuePtr, Actor, PPF_None))
	{
		OutError = FString::Printf(TEXT("Failed to set property: %s"), *PropertyName);
		return false;
	}

	return true;
}

static void ParseSpawnTransform(const TSharedPtr<FJsonObject>& Params, FVector& OutLocation, FRotator& OutRotation)
{
	OutLocation = FVector::ZeroVector;
	OutRotation = FRotator::ZeroRotator;
	if (!Params.IsValid())
	{
		return;
	}

	if (Params->HasField(TEXT("x"))) OutLocation.X = Params->GetNumberField(TEXT("x"));
	if (Params->HasField(TEXT("y"))) OutLocation.Y = Params->GetNumberField(TEXT("y"));
	if (Params->HasField(TEXT("z"))) OutLocation.Z = Params->GetNumberField(TEXT("z"));
	if (Params->HasField(TEXT("pitch"))) OutRotation.Pitch = Params->GetNumberField(TEXT("pitch"));
	if (Params->HasField(TEXT("yaw"))) OutRotation.Yaw = Params->GetNumberField(TEXT("yaw"));
	if (Params->HasField(TEXT("roll"))) OutRotation.Roll = Params->GetNumberField(TEXT("roll"));

	if (Params->HasTypedField<EJson::Object>(TEXT("transform")))
	{
		const TSharedPtr<FJsonObject> Transform = Params->GetObjectField(TEXT("transform"));
		if (Transform.IsValid())
		{
			if (const TSharedPtr<FJsonValue>* LocationValue = Transform->Values.Find(TEXT("location")))
			{
				FVector ParsedLocation = FVector::ZeroVector;
				if (JsonValueToVector(*LocationValue, ParsedLocation))
				{
					OutLocation = ParsedLocation;
				}
			}

			if (const TSharedPtr<FJsonValue>* RotationValue = Transform->Values.Find(TEXT("rotation")))
			{
				FRotator ParsedRotation = FRotator::ZeroRotator;
				if (JsonValueToRotator(*RotationValue, ParsedRotation))
				{
					OutRotation = ParsedRotation;
				}
			}
		}
	}
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
	RegisterRuntimeCapabilities(MaxSpawnPerPlan, MaxPlanSteps, MaxExecutePlanPerMinute);

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
	Bind(TEXT("/nova/audit"), EHttpServerRequestVerbs::VERB_GET, true, &FNovaBridgeRuntimeModule::HandleAuditTrail);
	Bind(TEXT("/nova/executePlan"), EHttpServerRequestVerbs::VERB_POST, true, &FNovaBridgeRuntimeModule::HandleExecutePlan);

	FHttpServerModule::Get().StartAllListeners();
	bServerStarted = true;

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

bool FNovaBridgeRuntimeModule::HandleCorsPreflight(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	(void)Request;
	TSharedPtr<FJsonObject> JsonObj = MakeShareable(new FJsonObject);
	JsonObj->SetStringField(TEXT("status"), TEXT("ok"));
	SendJsonResponse(OnComplete, JsonObj);
	return true;
}

void FNovaBridgeRuntimeModule::AddCorsHeaders(TUniquePtr<FHttpServerResponse>& Response) const
{
	if (!Response)
	{
		return;
	}

	Response->Headers.FindOrAdd(TEXT("Access-Control-Allow-Origin")).Add(TEXT("*"));
	Response->Headers.FindOrAdd(TEXT("Access-Control-Allow-Methods")).Add(TEXT("GET, POST, OPTIONS"));
	Response->Headers.FindOrAdd(TEXT("Access-Control-Allow-Headers")).Add(TEXT("Content-Type, Authorization, X-NovaBridge-Token"));
	Response->Headers.FindOrAdd(TEXT("Access-Control-Max-Age")).Add(TEXT("86400"));
}

TSharedPtr<FJsonObject> FNovaBridgeRuntimeModule::ParseRequestBody(const FHttpServerRequest& Request) const
{
	if (Request.Body.Num() == 0)
	{
		return nullptr;
	}

	TArray<uint8> NullTermBody(Request.Body);
	NullTermBody.Add(0);
	FString BodyStr = FString(UTF8_TO_TCHAR(reinterpret_cast<const ANSICHAR*>(NullTermBody.GetData())));
	TSharedPtr<FJsonObject> JsonObj;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(BodyStr);
	if (!FJsonSerializer::Deserialize(Reader, JsonObj))
	{
		return nullptr;
	}
	return JsonObj;
}

void FNovaBridgeRuntimeModule::SendJsonResponse(const FHttpResultCallback& OnComplete, TSharedPtr<FJsonObject> JsonObj, int32 StatusCode) const
{
	FString ResponseStr;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ResponseStr);
	FJsonSerializer::Serialize(JsonObj.ToSharedRef(), Writer);
	TUniquePtr<FHttpServerResponse> Response = FHttpServerResponse::Create(ResponseStr, TEXT("application/json"));
	Response->Code = static_cast<EHttpServerResponseCodes>(StatusCode);
	AddCorsHeaders(Response);
	OnComplete(MoveTemp(Response));
}

void FNovaBridgeRuntimeModule::SendErrorResponse(const FHttpResultCallback& OnComplete, const FString& Error, int32 StatusCode) const
{
	TSharedPtr<FJsonObject> JsonObj = MakeShareable(new FJsonObject);
	JsonObj->SetStringField(TEXT("status"), TEXT("error"));
	JsonObj->SetStringField(TEXT("error"), Error);
	JsonObj->SetNumberField(TEXT("code"), StatusCode);
	SendJsonResponse(OnComplete, JsonObj, StatusCode);
}

bool FNovaBridgeRuntimeModule::IsLocalHostRequest(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete) const
{
	const FString HostHeader = GetHeaderValueCaseInsensitive(Request, TEXT("Host"));
	if (IsLoopbackHost(HostHeader))
	{
		return true;
	}

	SendErrorResponse(OnComplete, TEXT("Runtime server only accepts localhost requests."), 403);
	return false;
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
}

bool FNovaBridgeRuntimeModule::IsAuthorizedRuntimeToken(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete) const
{
	if (Request.Verb == EHttpServerRequestVerbs::VERB_OPTIONS || !bRequireRuntimeToken)
	{
		return true;
	}

	if (FDateTime::UtcNow() > RuntimeTokenExpiryUtc)
	{
		SendErrorResponse(OnComplete, TEXT("Runtime token expired. Pair again via /nova/runtime/pair."), 401);
		return false;
	}

	FString PresentedToken = GetHeaderValueCaseInsensitive(Request, TEXT("X-NovaBridge-Token"));
	if (PresentedToken.IsEmpty())
	{
		const FString Authorization = GetHeaderValueCaseInsensitive(Request, TEXT("Authorization"));
		static const FString BearerPrefix = TEXT("Bearer ");
		if (Authorization.StartsWith(BearerPrefix, ESearchCase::IgnoreCase))
		{
			PresentedToken = Authorization.Mid(BearerPrefix.Len());
		}
	}

	PresentedToken.TrimStartAndEndInline();
	if (!PresentedToken.IsEmpty() && PresentedToken == RuntimeToken)
	{
		return true;
	}

	SendErrorResponse(OnComplete, TEXT("Unauthorized runtime request. Pair first and provide X-NovaBridge-Token."), 401);
	return false;
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
		RegisterRuntimeCapabilities(MaxSpawnPerPlan, MaxPlanSteps, MaxExecutePlanPerMinute);
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

bool FNovaBridgeRuntimeModule::HandleExecutePlan(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	{
		const double NowSec = FPlatformTime::Seconds();
		FScopeLock Lock(&RuntimeExecutePlanRateLimitMutex);
		if (RuntimeExecutePlanWindowStartSec <= 0.0 || (NowSec - RuntimeExecutePlanWindowStartSec) >= 60.0)
		{
			RuntimeExecutePlanWindowStartSec = NowSec;
			RuntimeExecutePlanCountInWindow = 0;
		}

		RuntimeExecutePlanCountInWindow++;
		if (RuntimeExecutePlanCountInWindow > MaxExecutePlanPerMinute)
		{
			PushAuditEntry(TEXT("/nova/executePlan"), TEXT("executePlan"), TEXT("rate_limited"), TEXT("Runtime executePlan per-minute limit exceeded"));
			SendErrorResponse(OnComplete, FString::Printf(TEXT("Rate limit: max %d runtime executePlan requests per minute"), MaxExecutePlanPerMinute), 429);
			return true;
		}
	}

	const TSharedPtr<FJsonObject> Body = ParseRequestBody(Request);
	if (!Body)
	{
		PushAuditEntry(TEXT("/nova/executePlan"), TEXT("executePlan"), TEXT("error"), TEXT("Invalid JSON body"));
		SendErrorResponse(OnComplete, TEXT("Invalid JSON body"));
		return true;
	}
	if (!Body->HasTypedField<EJson::Array>(TEXT("steps")))
	{
		PushAuditEntry(TEXT("/nova/executePlan"), TEXT("executePlan"), TEXT("error"), TEXT("Missing required field: steps"));
		SendErrorResponse(OnComplete, TEXT("Missing required field: steps"), 400);
		return true;
	}

	const TArray<TSharedPtr<FJsonValue>> Steps = Body->GetArrayField(TEXT("steps"));
	if (Steps.Num() == 0)
	{
		PushAuditEntry(TEXT("/nova/executePlan"), TEXT("executePlan"), TEXT("error"), TEXT("Plan has no steps"));
		SendErrorResponse(OnComplete, TEXT("Plan has no steps"), 400);
		return true;
	}
	if (Steps.Num() > MaxPlanSteps)
	{
		PushAuditEntry(TEXT("/nova/executePlan"), TEXT("executePlan"), TEXT("error"), TEXT("Plan exceeds max step count"));
		SendErrorResponse(OnComplete, FString::Printf(TEXT("Plan exceeds max step count (%d)"), MaxPlanSteps), 400);
		return true;
	}

	const FString PlanId = Body->HasTypedField<EJson::String>(TEXT("plan_id"))
		? Body->GetStringField(TEXT("plan_id"))
		: FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower);

	AsyncTask(ENamedThreads::GameThread, [this, OnComplete, Steps, PlanId]()
	{
		UWorld* World = ResolveRuntimeWorld();
		if (!World)
		{
			PushAuditEntry(TEXT("/nova/executePlan"), TEXT("executePlan"), TEXT("error"), TEXT("No runtime world is available"));
			SendErrorResponse(OnComplete, TEXT("No runtime world is available"), 500);
			return;
		}

		TArray<TSharedPtr<FJsonValue>> StepResults;
		StepResults.Reserve(Steps.Num());
		int32 SpawnCount = 0;
		int32 SuccessCount = 0;
		int32 ErrorCount = 0;

		for (int32 StepIndex = 0; StepIndex < Steps.Num(); ++StepIndex)
		{
			auto AddStepResult = [&StepResults](int32 InStepIndex, const FString& InStatus, const FString& InMessage)
			{
				TSharedPtr<FJsonObject> StepResult = MakeShareable(new FJsonObject);
				StepResult->SetNumberField(TEXT("step"), InStepIndex);
				StepResult->SetStringField(TEXT("status"), InStatus);
				StepResult->SetStringField(TEXT("message"), InMessage);
				StepResults.Add(MakeShareable(new FJsonValueObject(StepResult)));
			};

			const TSharedPtr<FJsonValue>& StepValue = Steps[StepIndex];
			if (!StepValue.IsValid() || StepValue->Type != EJson::Object)
			{
				AddStepResult(StepIndex, TEXT("error"), TEXT("Step must be an object"));
				ErrorCount++;
				continue;
			}

			const TSharedPtr<FJsonObject> StepObj = StepValue->AsObject();
			if (!StepObj.IsValid() || !StepObj->HasTypedField<EJson::String>(TEXT("action")))
			{
				AddStepResult(StepIndex, TEXT("error"), TEXT("Missing step action"));
				ErrorCount++;
				continue;
			}

			FString Action = StepObj->GetStringField(TEXT("action"));
			Action.TrimStartAndEndInline();
			Action.ToLowerInline();
			const TSharedPtr<FJsonObject> Params = StepObj->HasTypedField<EJson::Object>(TEXT("params"))
				? StepObj->GetObjectField(TEXT("params"))
				: MakeShareable(new FJsonObject);

			if (Action == TEXT("spawn"))
			{
				if (SpawnCount >= MaxSpawnPerPlan)
				{
					AddStepResult(StepIndex, TEXT("error"), FString::Printf(TEXT("Max spawn per plan reached (%d)"), MaxSpawnPerPlan));
					ErrorCount++;
					continue;
				}

				FString ClassName;
				if (Params->HasTypedField<EJson::String>(TEXT("class")))
				{
					ClassName = Params->GetStringField(TEXT("class"));
				}
				else if (Params->HasTypedField<EJson::String>(TEXT("type")))
				{
					ClassName = Params->GetStringField(TEXT("type"));
				}

				if (ClassName.IsEmpty())
				{
					AddStepResult(StepIndex, TEXT("error"), TEXT("spawn.params.class or spawn.params.type is required"));
					ErrorCount++;
					continue;
				}
				if (!IsRuntimeClassAllowed(ClassName))
				{
					AddStepResult(StepIndex, TEXT("error"), FString::Printf(TEXT("Class not allowed in runtime mode: %s"), *ClassName));
					ErrorCount++;
					continue;
				}

				FVector SpawnLocation = FVector::ZeroVector;
				FRotator SpawnRotation = FRotator::ZeroRotator;
				ParseSpawnTransform(Params, SpawnLocation, SpawnRotation);
				if (SpawnLocation.X < -50000.0 || SpawnLocation.X > 50000.0
					|| SpawnLocation.Y < -50000.0 || SpawnLocation.Y > 50000.0
					|| SpawnLocation.Z < -50000.0 || SpawnLocation.Z > 50000.0)
				{
					AddStepResult(StepIndex, TEXT("error"), TEXT("Spawn location is outside runtime bounds"));
					ErrorCount++;
					continue;
				}

				UClass* ActorClass = ResolveRuntimeActorClass(ClassName);
				if (!ActorClass)
				{
					AddStepResult(StepIndex, TEXT("error"), FString::Printf(TEXT("Class not found: %s"), *ClassName));
					ErrorCount++;
					continue;
				}

				AActor* NewActor = World->SpawnActor<AActor>(ActorClass, SpawnLocation, SpawnRotation);
				if (!NewActor)
				{
					AddStepResult(StepIndex, TEXT("error"), TEXT("Failed to spawn actor"));
					ErrorCount++;
					continue;
				}

				TSharedPtr<FJsonObject> StepResult = MakeShareable(new FJsonObject);
				StepResult->SetNumberField(TEXT("step"), StepIndex);
				StepResult->SetStringField(TEXT("status"), TEXT("success"));
				StepResult->SetStringField(TEXT("message"), TEXT("Spawned actor"));
				StepResult->SetStringField(TEXT("object_id"), NewActor->GetName());
				StepResults.Add(MakeShareable(new FJsonValueObject(StepResult)));
				SpawnCount++;
				SuccessCount++;
				continue;
			}

			if (Action == TEXT("delete"))
			{
				FString ActorName = Params->HasTypedField<EJson::String>(TEXT("name")) ? Params->GetStringField(TEXT("name")) : FString();
				if (ActorName.IsEmpty() && Params->HasTypedField<EJson::String>(TEXT("target")))
				{
					ActorName = Params->GetStringField(TEXT("target"));
				}
				if (ActorName.IsEmpty())
				{
					AddStepResult(StepIndex, TEXT("error"), TEXT("delete.params.name is required"));
					ErrorCount++;
					continue;
				}

				AActor* Actor = FindActorByNameRuntime(World, ActorName);
				if (!Actor)
				{
					AddStepResult(StepIndex, TEXT("error"), FString::Printf(TEXT("Actor not found: %s"), *ActorName));
					ErrorCount++;
					continue;
				}

				Actor->Destroy();
				AddStepResult(StepIndex, TEXT("success"), FString::Printf(TEXT("Deleted actor %s"), *ActorName));
				SuccessCount++;
				continue;
			}

			if (Action == TEXT("set"))
			{
				FString TargetName = Params->HasTypedField<EJson::String>(TEXT("target")) ? Params->GetStringField(TEXT("target")) : FString();
				if (TargetName.IsEmpty() && Params->HasTypedField<EJson::String>(TEXT("name")))
				{
					TargetName = Params->GetStringField(TEXT("name"));
				}
				if (TargetName.IsEmpty())
				{
					AddStepResult(StepIndex, TEXT("error"), TEXT("set.params.target is required"));
					ErrorCount++;
					continue;
				}

				AActor* Actor = FindActorByNameRuntime(World, TargetName);
				if (!Actor)
				{
					AddStepResult(StepIndex, TEXT("error"), FString::Printf(TEXT("Actor not found: %s"), *TargetName));
					ErrorCount++;
					continue;
				}
				if (!Params->HasTypedField<EJson::Object>(TEXT("props")))
				{
					AddStepResult(StepIndex, TEXT("error"), TEXT("set.params.props object is required"));
					ErrorCount++;
					continue;
				}

				const TSharedPtr<FJsonObject> Props = Params->GetObjectField(TEXT("props"));
				bool bSetAnyProperty = false;
				FString SetErrorMessage;
				for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Props->Values)
				{
					const FString Key = Pair.Key;
					const TSharedPtr<FJsonValue> PropValue = Pair.Value;
					if (!PropValue.IsValid())
					{
						continue;
					}

					if (Key.Equals(TEXT("location"), ESearchCase::IgnoreCase))
					{
						FVector ParsedLocation = FVector::ZeroVector;
						if (JsonValueToVector(PropValue, ParsedLocation))
						{
							Actor->SetActorLocation(ParsedLocation);
							bSetAnyProperty = true;
							continue;
						}
					}

					if (Key.Equals(TEXT("rotation"), ESearchCase::IgnoreCase))
					{
						FRotator ParsedRotation = FRotator::ZeroRotator;
						if (JsonValueToRotator(PropValue, ParsedRotation))
						{
							Actor->SetActorRotation(ParsedRotation);
							bSetAnyProperty = true;
							continue;
						}
					}

					if (Key.Equals(TEXT("scale"), ESearchCase::IgnoreCase))
					{
						FVector ParsedScale = FVector(1.0, 1.0, 1.0);
						if (JsonValueToVector(PropValue, ParsedScale))
						{
							Actor->SetActorScale3D(ParsedScale);
							bSetAnyProperty = true;
							continue;
						}
					}

					FString ImportText;
					if (!JsonValueToImportText(PropValue, ImportText))
					{
						SetErrorMessage = FString::Printf(TEXT("Unsupported value type for property '%s'"), *Key);
						break;
					}

					FString PropertyError;
					if (!SetRuntimeActorPropertyValue(Actor, Key, ImportText, PropertyError))
					{
						SetErrorMessage = PropertyError;
						break;
					}
					bSetAnyProperty = true;
				}

				if (!SetErrorMessage.IsEmpty())
				{
					AddStepResult(StepIndex, TEXT("error"), SetErrorMessage);
					ErrorCount++;
					continue;
				}
				if (!bSetAnyProperty)
				{
					AddStepResult(StepIndex, TEXT("error"), TEXT("No valid properties were applied"));
					ErrorCount++;
					continue;
				}

				AddStepResult(StepIndex, TEXT("success"), FString::Printf(TEXT("Updated actor %s"), *TargetName));
				SuccessCount++;
				continue;
			}

			if (Action == TEXT("screenshot"))
			{
				AddStepResult(StepIndex, TEXT("error"), TEXT("screenshot is not supported in runtime mode yet"));
				ErrorCount++;
				continue;
			}

			AddStepResult(StepIndex, TEXT("error"), FString::Printf(TEXT("Unsupported action: %s"), *Action));
			ErrorCount++;
		}

		TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
		Result->SetStringField(TEXT("status"), TEXT("ok"));
		Result->SetStringField(TEXT("mode"), RuntimeModeName);
		Result->SetStringField(TEXT("plan_id"), PlanId);
		Result->SetArrayField(TEXT("results"), StepResults);
		Result->SetNumberField(TEXT("step_count"), Steps.Num());
		Result->SetNumberField(TEXT("success_count"), SuccessCount);
		Result->SetNumberField(TEXT("error_count"), ErrorCount);
		SendJsonResponse(OnComplete, Result);
		PushAuditEntry(TEXT("/nova/executePlan"), TEXT("executePlan.complete"), TEXT("success"),
			FString::Printf(TEXT("Plan %s complete: %d success, %d error"), *PlanId, SuccessCount, ErrorCount));
	});

	return true;
}

IMPLEMENT_MODULE(FNovaBridgeRuntimeModule, NovaBridgeRuntime)
