#include "NovaBridgeModule.h"
#include "NovaBridgeCapabilityRegistry.h"
#include "NovaBridgeCoreTypes.h"
#include "NovaBridgePolicy.h"
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
#include "Containers/Ticker.h"

// Editor
#include "Editor.h"
#include "Editor/EditorEngine.h"
#include "Subsystems/EditorActorSubsystem.h"
#include "Subsystems/UnrealEditorSubsystem.h"
#include "EditorLevelLibrary.h"
#include "EditorAssetLibrary.h"
#include "LevelEditor.h"

// Engine
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/SkyLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/PostProcessVolume.h"
#include "GameFramework/Actor.h"
#include "EngineUtils.h"
#include "Components/StaticMeshComponent.h"
#include "Components/LightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/BrushComponent.h"
#include "PhysicsEngine/BodySetup.h"
#include "PhysicsEngine/AggregateGeom.h"
#include "Engine/Texture.h"
#include "Engine/Texture2D.h"

// Assets
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "UObject/UnrealType.h"
#include "UObject/Class.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

// Mesh
#include "MeshDescription.h"
#include "MeshDescriptionBuilder.h"
#include "StaticMeshAttributes.h"
#include "Engine/StaticMesh.h"
#include "RawMesh.h"

// Material
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialExpressionConstant4Vector.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Factories/MaterialFactoryNew.h"
#include "Factories/MaterialInstanceConstantFactoryNew.h"

// Viewport / Scene Capture (offscreen rendering)
#include "Engine/SceneCapture2D.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "TextureResource.h"

// Blueprint
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"

// Asset Import
#include "AssetImportTask.h"
#include "Misc/FileHelper.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/App.h"
#include "Misc/ScopeLock.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"

// Image
#include "IImageWrapperModule.h"
#include "IImageWrapper.h"
#include "Misc/Base64.h"
#include "ShowFlags.h"
#include "Math/UnrealMathUtility.h"
#include "Runtime/Launch/Resources/Version.h"
#include "UObject/SoftObjectPath.h"

// Sequencer
#include "LevelSequence.h"
#include "LevelSequenceActor.h"
#include "LevelSequencePlayer.h"
#include "MovieScene.h"
#include "Tracks/MovieScene3DTransformTrack.h"
#include "Sections/MovieScene3DTransformSection.h"
#include "Channels/MovieSceneDoubleChannel.h"

namespace
{
static void NovaBridgeSetPlaybackTime(ULevelSequencePlayer* Player, float TimeSeconds, bool bScrub)
{
	if (!Player)
	{
		return;
	}

#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7)
	FMovieSceneSequencePlaybackParams Params;
	Params.PositionType = EMovieScenePositionType::Time;
	Params.Time = TimeSeconds;
	Params.UpdateMethod = bScrub ? EUpdatePositionMethod::Scrub : EUpdatePositionMethod::Jump;
	Player->SetPlaybackPosition(Params);
#else
	// UE < 5.7 fallback: keep explicit param-based positioning for compatibility.
	FMovieSceneSequencePlaybackParams Params;
	Params.PositionType = EMovieScenePositionType::Time;
	Params.Time = TimeSeconds;
	Params.UpdateMethod = bScrub ? EUpdatePositionMethod::Scrub : EUpdatePositionMethod::Jump;
	Player->SetPlaybackPosition(Params);
#endif
}

static FGuid NovaBridgeFindBinding(ULevelSequence* Sequence, AActor* Actor, UWorld* World)
{
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6)
	FMovieSceneSequencePlaybackSettings Settings;
	ALevelSequenceActor* SequenceActor = nullptr;
	ULevelSequencePlayer* Player = ULevelSequencePlayer::CreateLevelSequencePlayer(World, Sequence, Settings, SequenceActor);
	if (!Player)
	{
		return FGuid();
	}

	FGuid Binding = Sequence->FindBindingFromObject(Actor, Player->GetSharedPlaybackState());
	Player->Stop();
	if (SequenceActor)
	{
		SequenceActor->Destroy();
	}
	return Binding;
#else
	return Sequence->FindBindingFromObject(Actor, World);
#endif
}
}

#if NOVABRIDGE_WITH_WEBSOCKET_NETWORKING
#include "IWebSocketNetworkingModule.h"
#include "IWebSocketServer.h"
#include "INetworkingWebSocket.h"
#include "WebSocketNetworkingDelegates.h"
#endif

#if NOVABRIDGE_WITH_PCG
#include "PCGGraph.h"
#include "PCGComponent.h"
#include "PCGVolume.h"
#endif

#define LOCTEXT_NAMESPACE "FNovaBridgeModule"

DEFINE_LOG_CATEGORY_STATIC(LogNovaBridge, Log, All);
static const TCHAR* NovaBridgeVersion = NovaBridgeCore::PluginVersion;

static FString NormalizeComponentKey(FString Value)
{
	FString Out;
	Out.Reserve(Value.Len());
	for (TCHAR Ch : Value)
	{
		if (FChar::IsAlnum(Ch))
		{
			Out.AppendChar(FChar::ToLower(Ch));
		}
	}

	// Ignore trailing numeric suffixes often used in component names (e.g. LightComponent0).
	while (Out.Len() > 0 && FChar::IsDigit(Out[Out.Len() - 1]))
	{
		Out.LeftChopInline(1, EAllowShrinking::No);
	}

	return Out;
}

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

struct FNovaBridgeRateBucket
{
	double WindowStartSec = 0.0;
	int32 Count = 0;
};

struct FNovaBridgeUndoEntry
{
	FString Action;
	FString ActorName;
	FString ActorLabel;
};

struct FNovaBridgeAuditEntry
{
	FString TimestampUtc;
	FString Route;
	FString Action;
	FString Role;
	FString Status;
	FString Message;
};

static FCriticalSection NovaBridgeRateLimitMutex;
static TMap<FString, FNovaBridgeRateBucket> NovaBridgeRateBuckets;
static FCriticalSection NovaBridgeUndoMutex;
static TArray<FNovaBridgeUndoEntry> NovaBridgeUndoStack;
static FCriticalSection NovaBridgeAuditMutex;
static TArray<FNovaBridgeAuditEntry> NovaBridgeAuditTrail;
static FCriticalSection NovaBridgeEventQueueMutex;
static TArray<FString> NovaBridgePendingEventPayloads;
static TArray<FString> NovaBridgePendingEventTypes;
static FString NovaBridgeDefaultRole;
static const int32 NovaBridgeUndoLimit = 128;
static const int32 NovaBridgeAuditLimit = 512;
static const int32 NovaBridgePendingEventsLimit = 2048;

static const TArray<FString>& NovaBridgeSpawnClassAllowList()
{
	return NovaBridgeCore::EditorAllowedSpawnClasses();
}

static FString NormalizeRoleName(const FString& RawRole)
{
	return NovaBridgeCore::NormalizeRoleName(RawRole);
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

static FString ResolveRoleFromRequest(const FHttpServerRequest& Request)
{
	FString CandidateRole = GetHeaderValueCaseInsensitive(Request, TEXT("X-NovaBridge-Role"));
	if (CandidateRole.IsEmpty() && Request.QueryParams.Contains(TEXT("role")))
	{
		CandidateRole = Request.QueryParams[TEXT("role")];
	}

	FString Role = NormalizeRoleName(CandidateRole);
	if (Role.IsEmpty())
	{
		Role = NovaBridgeDefaultRole;
	}
	if (Role.IsEmpty())
	{
		Role = TEXT("admin");
	}
	return Role;
}

static bool IsReadOnlyRoute(const FString& RoutePath)
{
	return RoutePath == TEXT("/nova/health")
		|| RoutePath == TEXT("/nova/project/info")
		|| RoutePath == TEXT("/nova/caps")
		|| RoutePath == TEXT("/nova/events")
		|| RoutePath == TEXT("/nova/audit")
		|| RoutePath == TEXT("/nova/scene/list")
		|| RoutePath == TEXT("/nova/scene/get")
		|| RoutePath == TEXT("/nova/asset/list")
		|| RoutePath == TEXT("/nova/asset/info")
		|| RoutePath == TEXT("/nova/mesh/get")
		|| RoutePath == TEXT("/nova/material/get")
		|| RoutePath == TEXT("/nova/viewport/screenshot")
		|| RoutePath == TEXT("/nova/viewport/camera/get")
		|| RoutePath == TEXT("/nova/stream/status")
		|| RoutePath == TEXT("/nova/pcg/list-graphs")
		|| RoutePath == TEXT("/nova/sequencer/info")
		|| RoutePath == TEXT("/nova/optimize/stats");
}

static bool IsRouteAllowedForRole(const FString& Role, const FString& RoutePath, EHttpServerRequestVerbs Verb)
{
	if (Role == TEXT("admin"))
	{
		return true;
	}

	if (Role == TEXT("automation"))
	{
		if (RoutePath == TEXT("/nova/exec/command") || RoutePath == TEXT("/nova/build/lighting"))
		{
			return false;
		}
		return true;
	}

	if (Role == TEXT("read_only"))
	{
		if (Verb == EHttpServerRequestVerbs::VERB_GET)
		{
			return IsReadOnlyRoute(RoutePath);
		}
		return false;
	}

	return false;
}

static int32 GetRouteRateLimitPerMinute(const FString& Role, const FString& RoutePath)
{
	if (Role == TEXT("admin"))
	{
		if (RoutePath == TEXT("/nova/executePlan"))
		{
			return 30;
		}
		if (RoutePath == TEXT("/nova/scene/spawn"))
		{
			return 120;
		}
		return 240;
	}

	if (Role == TEXT("automation"))
	{
		if (RoutePath == TEXT("/nova/executePlan"))
		{
			return 20;
		}
		if (RoutePath == TEXT("/nova/scene/spawn"))
		{
			return 60;
		}
		return 120;
	}

	if (Role == TEXT("read_only"))
	{
		return 120;
	}

	return 0;
}

static bool ConsumeRateLimit(const FString& BucketKey, int32 LimitPerMinute, FString& OutError)
{
	if (LimitPerMinute <= 0)
	{
		OutError = TEXT("Rate limit denied for this role/action");
		return false;
	}

	const double NowSec = FPlatformTime::Seconds();
	FScopeLock Lock(&NovaBridgeRateLimitMutex);
	FNovaBridgeRateBucket& Bucket = NovaBridgeRateBuckets.FindOrAdd(BucketKey);
	if (Bucket.WindowStartSec <= 0.0 || (NowSec - Bucket.WindowStartSec) >= 60.0)
	{
		Bucket.WindowStartSec = NowSec;
		Bucket.Count = 0;
	}

	Bucket.Count++;
	if (Bucket.Count > LimitPerMinute)
	{
		OutError = FString::Printf(TEXT("Rate limit: max %d requests/minute for this role/action"), LimitPerMinute);
		return false;
	}
	return true;
}

static void PushUndoEntry(const FNovaBridgeUndoEntry& Entry)
{
	FScopeLock Lock(&NovaBridgeUndoMutex);
	NovaBridgeUndoStack.Add(Entry);
	if (NovaBridgeUndoStack.Num() > NovaBridgeUndoLimit)
	{
		NovaBridgeUndoStack.RemoveAt(0, NovaBridgeUndoStack.Num() - NovaBridgeUndoLimit, EAllowShrinking::No);
	}
}

static bool PopUndoEntry(FNovaBridgeUndoEntry& OutEntry)
{
	FScopeLock Lock(&NovaBridgeUndoMutex);
	if (NovaBridgeUndoStack.Num() == 0)
	{
		return false;
	}
	OutEntry = NovaBridgeUndoStack.Last();
	NovaBridgeUndoStack.Pop();
	return true;
}

static FString NormalizeEventType(const FString& InType)
{
	FString Type = InType;
	Type.TrimStartAndEndInline();
	Type.ToLowerInline();
	return Type;
}

static TArray<TSharedPtr<FJsonValue>> MakeJsonStringArray(const TArray<FString>& Values)
{
	TArray<TSharedPtr<FJsonValue>> JsonArray;
	JsonArray.Reserve(Values.Num());
	for (const FString& Value : Values)
	{
		JsonArray.Add(MakeShared<FJsonValueString>(Value));
	}
	return JsonArray;
}

static TArray<FString> ParseEventTypeFilter(const FHttpServerRequest& Request)
{
	FString RawTypes;
	if (Request.QueryParams.Contains(TEXT("types")))
	{
		RawTypes = Request.QueryParams[TEXT("types")];
	}
	else if (Request.QueryParams.Contains(TEXT("type")))
	{
		RawTypes = Request.QueryParams[TEXT("type")];
	}

	TArray<FString> FilterTypes;
	if (RawTypes.IsEmpty())
	{
		return FilterTypes;
	}

	TArray<FString> Parts;
	RawTypes.ParseIntoArray(Parts, TEXT(","), true);
	for (FString& Part : Parts)
	{
		const FString Normalized = NormalizeEventType(Part);
		if (!Normalized.IsEmpty() && !FilterTypes.Contains(Normalized))
		{
			FilterTypes.Add(Normalized);
		}
	}
	return FilterTypes;
}

static const TArray<FString>& SupportedEventTypes()
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

static bool IsSupportedEventType(const FString& EventType)
{
	return SupportedEventTypes().Contains(EventType);
}

static FString SerializeJsonObject(const TSharedPtr<FJsonObject>& JsonObj)
{
	FString Serialized;
	if (!JsonObj.IsValid())
	{
		return Serialized;
	}

	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Serialized);
	FJsonSerializer::Serialize(JsonObj.ToSharedRef(), Writer);
	return Serialized;
}

static void SendSocketJsonMessage(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& JsonObj)
{
	if (!Socket || !JsonObj.IsValid())
	{
		return;
	}

	const FString Serialized = SerializeJsonObject(JsonObj);
	if (Serialized.IsEmpty())
	{
		return;
	}

	const FTCHARToUTF8 Utf8Payload(*Serialized);
	const uint8* Data = reinterpret_cast<const uint8*>(Utf8Payload.Get());
	uint8* MutableData = const_cast<uint8*>(Data);
	Socket->Send(MutableData, Utf8Payload.Length(), false);
}

static bool ParseEventSubscriptionPayload(const FString& Message, TSet<FString>& OutTypes, bool& bOutEnableFilter, FString& OutError)
{
	OutTypes.Reset();
	bOutEnableFilter = false;

	TSharedPtr<FJsonObject> JsonObj;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Message);
	if (!FJsonSerializer::Deserialize(Reader, JsonObj) || !JsonObj.IsValid())
	{
		OutError = TEXT("Invalid subscription message JSON");
		return false;
	}

	FString Action = JsonObj->HasTypedField<EJson::String>(TEXT("action"))
		? NormalizeEventType(JsonObj->GetStringField(TEXT("action")))
		: TEXT("subscribe");
	if (Action.IsEmpty())
	{
		Action = TEXT("subscribe");
	}

	if (Action == TEXT("clear") || Action == TEXT("all") || Action == TEXT("subscribe_all") || Action == TEXT("reset"))
	{
		return true;
	}

	if (Action != TEXT("subscribe"))
	{
		OutError = FString::Printf(TEXT("Unsupported subscription action: %s"), *Action);
		return false;
	}

	if (!JsonObj->HasTypedField<EJson::Array>(TEXT("types")))
	{
		OutError = TEXT("Missing 'types' array for subscribe action");
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>& TypeValues = JsonObj->GetArrayField(TEXT("types"));
	for (const TSharedPtr<FJsonValue>& TypeValue : TypeValues)
	{
		if (!TypeValue.IsValid() || TypeValue->Type != EJson::String)
		{
			OutError = TEXT("Subscription 'types' entries must be strings");
			return false;
		}

		const FString Type = NormalizeEventType(TypeValue->AsString());
		if (Type.IsEmpty())
		{
			continue;
		}
		if (!IsSupportedEventType(Type))
		{
			OutError = FString::Printf(TEXT("Unsupported event type: %s"), *Type);
			return false;
		}
		OutTypes.Add(Type);
	}

	bOutEnableFilter = OutTypes.Num() > 0;
	return true;
}

static void QueueEventObject(const TSharedPtr<FJsonObject>& EventObj)
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
		EventObj->SetStringField(TEXT("mode"), TEXT("editor"));
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

	FScopeLock EventLock(&NovaBridgeEventQueueMutex);
	NovaBridgePendingEventPayloads.Add(MoveTemp(SerializedEvent));
	NovaBridgePendingEventTypes.Add(EventType);
	if (NovaBridgePendingEventPayloads.Num() > NovaBridgePendingEventsLimit)
	{
		const int32 RemoveCount = NovaBridgePendingEventPayloads.Num() - NovaBridgePendingEventsLimit;
		NovaBridgePendingEventPayloads.RemoveAt(0, RemoveCount, EAllowShrinking::No);
		NovaBridgePendingEventTypes.RemoveAt(0, RemoveCount, EAllowShrinking::No);
	}
}

static void PushAuditEntry(const FString& Route, const FString& Action, const FString& Role, const FString& Status, const FString& Message)
{
	FNovaBridgeAuditEntry Entry;
	Entry.TimestampUtc = FDateTime::UtcNow().ToIso8601();
	Entry.Route = Route;
	Entry.Action = Action;
	Entry.Role = Role;
	Entry.Status = Status;
	Entry.Message = Message;

	FScopeLock Lock(&NovaBridgeAuditMutex);
	NovaBridgeAuditTrail.Add(Entry);
	if (NovaBridgeAuditTrail.Num() > NovaBridgeAuditLimit)
	{
		NovaBridgeAuditTrail.RemoveAt(0, NovaBridgeAuditTrail.Num() - NovaBridgeAuditLimit, EAllowShrinking::No);
	}

	TSharedPtr<FJsonObject> EventObj = MakeShareable(new FJsonObject);
	EventObj->SetStringField(TEXT("type"), TEXT("audit"));
	EventObj->SetStringField(TEXT("mode"), TEXT("editor"));
	EventObj->SetStringField(TEXT("timestamp_utc"), Entry.TimestampUtc);
	EventObj->SetStringField(TEXT("route"), Entry.Route);
	EventObj->SetStringField(TEXT("action"), Entry.Action);
	EventObj->SetStringField(TEXT("role"), Entry.Role);
	EventObj->SetStringField(TEXT("status"), Entry.Status);
	EventObj->SetStringField(TEXT("message"), Entry.Message);
	QueueEventObject(EventObj);
}

static bool IsSpawnClassAllowedForRole(const FString& Role, const FString& ClassName)
{
	if (Role == TEXT("admin"))
	{
		return true;
	}
	return NovaBridgeCore::IsClassAllowed(NovaBridgeSpawnClassAllowList(), ClassName);
}

static bool IsSpawnLocationInBounds(const FVector& Location)
{
	return NovaBridgeCore::IsSpawnLocationInBounds(Location);
}

static int32 GetPlanSpawnLimit(const FString& Role)
{
	return NovaBridgeCore::GetEditorPlanSpawnLimit(Role);
}

static bool IsPlanActionAllowedForRole(const FString& Role, const FString& Action)
{
	return NovaBridgeCore::IsEditorPlanActionAllowedForRole(Role, Action);
}

static TSharedPtr<FJsonObject> MakePlanStepResult(int32 StepIndex, const FString& Status, const FString& Message)
{
	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetNumberField(TEXT("step"), StepIndex);
	Result->SetStringField(TEXT("status"), Status);
	Result->SetStringField(TEXT("message"), Message);
	return Result;
}

static void RegisterEditorCapabilities(uint32 InEventWsPort);

// Helper to find actor by name in editor world
static AActor* FindActorByName(const FString& Name)
{
	if (!GEditor) return nullptr;
	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World) return nullptr;

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (It->GetName() == Name || It->GetActorLabel() == Name)
		{
			return *It;
		}
	}
	return nullptr;
}

// Helper to serialize actor to JSON
static TSharedPtr<FJsonObject> ActorToJson(AActor* Actor)
{
	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetStringField(TEXT("name"), Actor->GetName());
	Obj->SetStringField(TEXT("label"), Actor->GetActorLabel());
	Obj->SetStringField(TEXT("class"), Actor->GetClass()->GetName());
	Obj->SetStringField(TEXT("path"), Actor->GetPathName());

	FVector Loc = Actor->GetActorLocation();
	FRotator Rot = Actor->GetActorRotation();
	FVector Scale = Actor->GetActorScale3D();

	TSharedPtr<FJsonObject> Transform = MakeShareable(new FJsonObject);
	TSharedPtr<FJsonObject> LocObj = MakeShareable(new FJsonObject);
	LocObj->SetNumberField(TEXT("x"), Loc.X);
	LocObj->SetNumberField(TEXT("y"), Loc.Y);
	LocObj->SetNumberField(TEXT("z"), Loc.Z);
	Transform->SetObjectField(TEXT("location"), LocObj);

	TSharedPtr<FJsonObject> RotObj = MakeShareable(new FJsonObject);
	RotObj->SetNumberField(TEXT("pitch"), Rot.Pitch);
	RotObj->SetNumberField(TEXT("yaw"), Rot.Yaw);
	RotObj->SetNumberField(TEXT("roll"), Rot.Roll);
	Transform->SetObjectField(TEXT("rotation"), RotObj);

	TSharedPtr<FJsonObject> ScaleObj = MakeShareable(new FJsonObject);
	ScaleObj->SetNumberField(TEXT("x"), Scale.X);
	ScaleObj->SetNumberField(TEXT("y"), Scale.Y);
	ScaleObj->SetNumberField(TEXT("z"), Scale.Z);
	Transform->SetObjectField(TEXT("scale"), ScaleObj);

	Obj->SetObjectField(TEXT("transform"), Transform);
	return Obj;
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
		if (!Obj.IsValid())
		{
			return false;
		}

		if (!Obj->HasField(TEXT("x")) || !Obj->HasField(TEXT("y")) || !Obj->HasField(TEXT("z")))
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

		// Allow x/y/z naming as fallback for planner compatibility.
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

static UClass* ResolveActorClassByName(const FString& InClassName)
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
		else if (ClassName == TEXT("PlayerStart")) ActorClass = FindObject<UClass>(nullptr, TEXT("/Script/Engine.PlayerStart"));
		else if (ClassName == TEXT("SkyLight")) ActorClass = ASkyLight::StaticClass();
		else if (ClassName == TEXT("ExponentialHeightFog")) ActorClass = AExponentialHeightFog::StaticClass();
		else if (ClassName == TEXT("PostProcessVolume")) ActorClass = APostProcessVolume::StaticClass();
		if (!ActorClass)
		{
			ActorClass = FindObject<UClass>(nullptr, *(FString(TEXT("/Script/Engine.")) + ClassName));
		}
	}

	return ActorClass;
}

static void ParseSpawnTransformFromParams(const TSharedPtr<FJsonObject>& Params, FVector& OutLocation, FRotator& OutRotation, FVector& OutScale)
{
	OutLocation = FVector::ZeroVector;
	OutRotation = FRotator::ZeroRotator;
	OutScale = FVector(1.0, 1.0, 1.0);
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
			if (Transform->HasField(TEXT("location")))
			{
				FVector ParsedLocation = FVector::ZeroVector;
				if (const TSharedPtr<FJsonValue>* LocationValue = Transform->Values.Find(TEXT("location")))
				{
					if (JsonValueToVector(*LocationValue, ParsedLocation))
					{
						OutLocation = ParsedLocation;
					}
				}
			}

			if (Transform->HasField(TEXT("rotation")))
			{
				FRotator ParsedRotation = FRotator::ZeroRotator;
				if (const TSharedPtr<FJsonValue>* RotationValue = Transform->Values.Find(TEXT("rotation")))
				{
					if (JsonValueToRotator(*RotationValue, ParsedRotation))
					{
						OutRotation = ParsedRotation;
					}
				}
			}

			if (Transform->HasField(TEXT("scale")))
			{
				FVector ParsedScale = FVector(1.0, 1.0, 1.0);
				if (const TSharedPtr<FJsonValue>* ScaleValue = Transform->Values.Find(TEXT("scale")))
				{
					if (JsonValueToVector(*ScaleValue, ParsedScale))
					{
						OutScale = ParsedScale;
					}
				}
			}
		}
	}
}

static bool SetActorPropertyValue(AActor* Actor, const FString& PropertyName, const FString& Value, FString& OutError)
{
	if (!Actor)
	{
		OutError = TEXT("Actor is null");
		return false;
	}

	FString CompName;
	FString PropName;
	UObject* TargetObj = Actor;
	UActorComponent* FoundComp = nullptr;
	if (PropertyName.Split(TEXT("."), &CompName, &PropName))
	{
		TInlineComponentArray<UActorComponent*> Components;
		Actor->GetComponents(Components);
		for (UActorComponent* Comp : Components)
		{
			if (Comp->GetName() == CompName || Comp->GetClass()->GetName() == CompName
				|| Comp->GetName().Contains(CompName))
			{
				FoundComp = Comp;
				break;
			}
		}
		if (!FoundComp)
		{
			const FString RequestedKey = NormalizeComponentKey(CompName);
			if (!RequestedKey.IsEmpty())
			{
				for (UActorComponent* Comp : Components)
				{
					const FString CompNameKey = NormalizeComponentKey(Comp->GetName());
					FString ClassName = Comp->GetClass()->GetName();
					const FString ClassNameKey = NormalizeComponentKey(ClassName);
					if (ClassName.StartsWith(TEXT("U")))
					{
						ClassName.RightChopInline(1, EAllowShrinking::No);
					}
					const FString ClassNameNoPrefixKey = NormalizeComponentKey(ClassName);

					const bool bClassMatch =
						(RequestedKey == ClassNameKey) ||
						(RequestedKey == ClassNameNoPrefixKey) ||
						(RequestedKey.Contains(ClassNameKey)) ||
						(RequestedKey.Contains(ClassNameNoPrefixKey)) ||
						(ClassNameKey.Contains(RequestedKey)) ||
						(ClassNameNoPrefixKey.Contains(RequestedKey));

					const bool bNameMatch =
						(RequestedKey == CompNameKey) ||
						(RequestedKey.Contains(CompNameKey)) ||
						(CompNameKey.Contains(RequestedKey));

					if (bClassMatch || bNameMatch)
					{
						FoundComp = Comp;
						break;
					}
				}
			}
		}
		if (FoundComp)
		{
			TargetObj = FoundComp;

			if (PropName.StartsWith(TEXT("Material")))
			{
				if (UPrimitiveComponent* PrimComp = Cast<UPrimitiveComponent>(FoundComp))
				{
					int32 SlotIndex = 0;
					int32 BracketIdx = INDEX_NONE;
					if (PropName.FindChar('[', BracketIdx))
					{
						FString IdxStr = PropName.Mid(BracketIdx + 1).LeftChop(1);
						SlotIndex = FCString::Atoi(*IdxStr);
					}

					UMaterialInterface* Mat = LoadObject<UMaterialInterface>(nullptr, *Value);
					if (!Mat)
					{
						OutError = FString::Printf(TEXT("Material not found: %s"), *Value);
						return false;
					}

					PrimComp->SetMaterial(SlotIndex, Mat);
					PrimComp->MarkRenderStateDirty();
					Actor->PostEditChange();
					return true;
				}
			}
		}
		else
		{
			PropName = PropertyName;
		}
	}
	else
	{
		PropName = PropertyName;
	}

	FProperty* Prop = TargetObj->GetClass()->FindPropertyByName(*PropName);
	if (!Prop)
	{
		for (TFieldIterator<FProperty> It(TargetObj->GetClass()); It; ++It)
		{
			if (It->GetName().Equals(PropName, ESearchCase::IgnoreCase))
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

	void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(TargetObj);
	if (!Prop->ImportText_Direct(*Value, ValuePtr, TargetObj, PPF_None))
	{
		OutError = FString::Printf(TEXT("Failed to set property: %s"), *PropertyName);
		return false;
	}

	if (UActorComponent* Comp = Cast<UActorComponent>(TargetObj))
	{
		Comp->MarkRenderStateDirty();
	}
	Actor->PostEditChange();
	return true;
}

void FNovaBridgeModule::StartupModule()
{
	UE_LOG(LogNovaBridge, Log, TEXT("NovaBridge starting up..."));
	StartHttpServer();
	StartWebSocketServer();
	StartEventWebSocketServer();
}

void FNovaBridgeModule::ShutdownModule()
{
	UE_LOG(LogNovaBridge, Log, TEXT("NovaBridge shutting down..."));
	StopEventWebSocketServer();
	StopWebSocketServer();
	CleanupStreamCapture();
	CleanupCapture();
	StopHttpServer();
}

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
		const FString EnvApiKey = FPlatformMisc::GetEnvironmentVariable(TEXT("NOVABRIDGE_API_KEY"));
		FString TrimmedEnvKey = EnvApiKey;
		TrimmedEnvKey.TrimStartAndEndInline();
		if (!TrimmedEnvKey.IsEmpty())
		{
			RequiredApiKey = TrimmedEnvKey;
		}
	}
	if (RequiredApiKey.IsEmpty())
	{
		UE_LOG(LogNovaBridge, Warning, TEXT("[WARNING] NovaBridge running without authentication."));
	}
	FString ParsedDefaultRole;
	if (FParse::Value(FCommandLine::Get(), TEXT("NovaBridgeDefaultRole="), ParsedDefaultRole))
	{
		const FString NormalizedRole = NormalizeRoleName(ParsedDefaultRole);
		if (!NormalizedRole.IsEmpty())
		{
			NovaBridgeDefaultRole = NormalizedRole;
		}
	}
	if (NovaBridgeDefaultRole.IsEmpty())
	{
		const FString EnvDefaultRole = FPlatformMisc::GetEnvironmentVariable(TEXT("NOVABRIDGE_DEFAULT_ROLE"));
		const FString NormalizedEnvRole = NormalizeRoleName(EnvDefaultRole);
		if (!NormalizedEnvRole.IsEmpty())
		{
			NovaBridgeDefaultRole = NormalizedEnvRole;
		}
	}
	if (NovaBridgeDefaultRole.IsEmpty())
	{
		NovaBridgeDefaultRole = TEXT("admin");
	}
	UE_LOG(LogNovaBridge, Log, TEXT("NovaBridge default role: %s"), *NovaBridgeDefaultRole);
	{
		FScopeLock RateLock(&NovaBridgeRateLimitMutex);
		NovaBridgeRateBuckets.Empty();
	}
	{
		FScopeLock UndoLock(&NovaBridgeUndoMutex);
		NovaBridgeUndoStack.Empty();
	}
	{
		FScopeLock AuditLock(&NovaBridgeAuditMutex);
		NovaBridgeAuditTrail.Empty();
	}
		{
			FScopeLock EventLock(&NovaBridgeEventQueueMutex);
			NovaBridgePendingEventPayloads.Empty();
			NovaBridgePendingEventTypes.Empty();
		}
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

// ============================================================
// WebSocket Stream Server
// ============================================================

void FNovaBridgeModule::StartWebSocketServer()
{
#if NOVABRIDGE_WITH_WEBSOCKET_NETWORKING
	if (WsServer.IsValid())
	{
		return;
	}

	int32 ParsedWsPort = 0;
	if (FParse::Value(FCommandLine::Get(), TEXT("NovaBridgeWsPort="), ParsedWsPort))
	{
		if (ParsedWsPort > 0 && ParsedWsPort <= 65535)
		{
			WsPort = static_cast<uint32>(ParsedWsPort);
		}
	}

	FWebSocketClientConnectedCallBack ConnectedCallback;
	ConnectedCallback.BindLambda([this](INetworkingWebSocket* Socket)
	{
		if (!Socket)
		{
			return;
		}

		FWsClient Client;
		Client.Socket = Socket;
		Client.Id = FGuid::NewGuid();
		WsClients.Add(MoveTemp(Client));

		FWebSocketPacketReceivedCallBack ReceiveCallback;
		ReceiveCallback.BindLambda([](void* Data, int32 Size)
		{
			(void)Data;
			(void)Size;
		});
		Socket->SetReceiveCallBack(ReceiveCallback);

		FWebSocketInfoCallBack CloseCallback;
		CloseCallback.BindLambda([this, Socket]()
		{
			const int32 Index = WsClients.IndexOfByPredicate([Socket](const FWsClient& Client)
			{
				return Client.Socket == Socket;
			});
			if (Index != INDEX_NONE)
			{
				if (WsClients[Index].Socket)
				{
					delete WsClients[Index].Socket;
					WsClients[Index].Socket = nullptr;
				}
				WsClients.RemoveAtSwap(Index);
			}

			if (WsClients.Num() == 0)
			{
				bStreamActive = false;
				StopStreamTicker();
			}
		});
		Socket->SetSocketClosedCallBack(CloseCallback);

		// Auto-start stream when first client connects.
		if (!bStreamActive)
		{
			bStreamActive = true;
		}
		StartStreamTicker();

		UE_LOG(LogNovaBridge, Log, TEXT("NovaBridge stream client connected (%d total)"), WsClients.Num());
	});

	IWebSocketNetworkingModule* WsModule = FModuleManager::Get().LoadModulePtr<IWebSocketNetworkingModule>(TEXT("WebSocketNetworking"));
	if (!WsModule)
	{
		UE_LOG(LogNovaBridge, Warning, TEXT("WebSocketNetworking module not available; stream WebSocket server disabled"));
		return;
	}

	WsServer = WsModule->CreateServer();
	if (!WsServer.IsValid() || !WsServer->Init(WsPort, ConnectedCallback))
	{
		UE_LOG(LogNovaBridge, Warning, TEXT("NovaBridge WebSocket server failed to initialize on port %d"), WsPort);
		WsServer.Reset();
		return;
	}

	WsServerTickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([this](float DeltaTime) -> bool
	{
		(void)DeltaTime;
		if (WsServer.IsValid())
		{
			WsServer->Tick();
		}
		return true;
	}));

	UE_LOG(LogNovaBridge, Log, TEXT("NovaBridge WebSocket stream server started on port %d"), WsPort);
#else
	UE_LOG(LogNovaBridge, Warning, TEXT("WebSocketNetworking module not available; stream WebSocket server disabled"));
#endif
}

void FNovaBridgeModule::StopWebSocketServer()
{
#if NOVABRIDGE_WITH_WEBSOCKET_NETWORKING
	StopStreamTicker();
	bStreamActive = false;

	if (WsServerTickHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(WsServerTickHandle);
		WsServerTickHandle.Reset();
	}

	for (FWsClient& Client : WsClients)
	{
		if (Client.Socket)
		{
			delete Client.Socket;
			Client.Socket = nullptr;
		}
	}
	WsClients.Empty();
	WsServer.Reset();
#endif
}

void FNovaBridgeModule::StartEventWebSocketServer()
{
#if NOVABRIDGE_WITH_WEBSOCKET_NETWORKING
	if (EventWsServer.IsValid())
	{
		return;
	}

	int32 ParsedEventWsPort = 0;
	if (FParse::Value(FCommandLine::Get(), TEXT("NovaBridgeEventsPort="), ParsedEventWsPort))
	{
		if (ParsedEventWsPort > 0 && ParsedEventWsPort <= 65535)
		{
			EventWsPort = static_cast<uint32>(ParsedEventWsPort);
		}
	}
	RegisterEditorCapabilities(EventWsPort);

		FWebSocketClientConnectedCallBack ConnectedCallback;
		ConnectedCallback.BindLambda([this](INetworkingWebSocket* Socket)
		{
			if (!Socket)
			{
				return;
			}

			FWsClient Client;
			Client.Socket = Socket;
			Client.Id = FGuid::NewGuid();
			Client.bEventTypeFilterEnabled = false;
			EventWsClients.Add(MoveTemp(Client));

			FWebSocketPacketReceivedCallBack ReceiveCallback;
			ReceiveCallback.BindLambda([this, Socket](void* Data, int32 Size)
			{
				if (!Data || Size <= 0)
				{
					return;
				}

				const FUTF8ToTCHAR Converted(static_cast<const ANSICHAR*>(Data), Size);
				const FString Message(Converted.Length(), Converted.Get());
				TSet<FString> RequestedTypes;
				bool bEnableFilter = false;
				FString ParseError;
				if (!ParseEventSubscriptionPayload(Message, RequestedTypes, bEnableFilter, ParseError))
				{
					TSharedPtr<FJsonObject> ErrorReply = MakeShared<FJsonObject>();
					ErrorReply->SetStringField(TEXT("type"), TEXT("subscription"));
					ErrorReply->SetStringField(TEXT("status"), TEXT("error"));
					ErrorReply->SetStringField(TEXT("message"), ParseError);
					ErrorReply->SetArrayField(TEXT("supported_types"), MakeJsonStringArray(SupportedEventTypes()));
					SendSocketJsonMessage(Socket, ErrorReply);
					return;
				}

				const int32 ClientIndex = EventWsClients.IndexOfByPredicate([Socket](const FWsClient& InClient)
				{
					return InClient.Socket == Socket;
				});
				if (ClientIndex == INDEX_NONE)
				{
					return;
				}

				EventWsClients[ClientIndex].bEventTypeFilterEnabled = bEnableFilter;
				EventWsClients[ClientIndex].EventTypes = MoveTemp(RequestedTypes);

				TSharedPtr<FJsonObject> AckReply = MakeShared<FJsonObject>();
				AckReply->SetStringField(TEXT("type"), TEXT("subscription"));
				AckReply->SetStringField(TEXT("status"), TEXT("ok"));
				AckReply->SetBoolField(TEXT("filter_enabled"), EventWsClients[ClientIndex].bEventTypeFilterEnabled);
				AckReply->SetArrayField(TEXT("types"), MakeJsonStringArray(EventWsClients[ClientIndex].EventTypes.Array()));
				SendSocketJsonMessage(Socket, AckReply);
			});
			Socket->SetReceiveCallBack(ReceiveCallback);

		FWebSocketInfoCallBack CloseCallback;
		CloseCallback.BindLambda([this, Socket]()
		{
			const int32 Index = EventWsClients.IndexOfByPredicate([Socket](const FWsClient& Client)
			{
				return Client.Socket == Socket;
			});
			if (Index != INDEX_NONE)
			{
				if (EventWsClients[Index].Socket)
				{
					delete EventWsClients[Index].Socket;
					EventWsClients[Index].Socket = nullptr;
				}
				EventWsClients.RemoveAtSwap(Index);
			}
			});
			Socket->SetSocketClosedCallBack(CloseCallback);

			TSharedPtr<FJsonObject> WelcomeReply = MakeShared<FJsonObject>();
			WelcomeReply->SetStringField(TEXT("type"), TEXT("subscription"));
			WelcomeReply->SetStringField(TEXT("status"), TEXT("ready"));
			WelcomeReply->SetBoolField(TEXT("filter_enabled"), false);
			WelcomeReply->SetArrayField(TEXT("supported_types"), MakeJsonStringArray(SupportedEventTypes()));
			WelcomeReply->SetStringField(TEXT("hint"), TEXT("{\"action\":\"subscribe\",\"types\":[\"spawn\",\"error\"]}"));
			SendSocketJsonMessage(Socket, WelcomeReply);

			UE_LOG(LogNovaBridge, Log, TEXT("NovaBridge events client connected (%d total)"), EventWsClients.Num());
		});

	IWebSocketNetworkingModule* WsModule = FModuleManager::Get().LoadModulePtr<IWebSocketNetworkingModule>(TEXT("WebSocketNetworking"));
	if (!WsModule)
	{
		UE_LOG(LogNovaBridge, Warning, TEXT("WebSocketNetworking module not available; events WebSocket server disabled"));
		return;
	}

	EventWsServer = WsModule->CreateServer();
	if (!EventWsServer.IsValid() || !EventWsServer->Init(EventWsPort, ConnectedCallback))
	{
		UE_LOG(LogNovaBridge, Warning, TEXT("NovaBridge events WebSocket server failed to initialize on port %d"), EventWsPort);
		EventWsServer.Reset();
		return;
	}

	EventWsServerTickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([this](float DeltaTime) -> bool
	{
		(void)DeltaTime;
		if (EventWsServer.IsValid())
		{
			EventWsServer->Tick();
		}
		PumpEventSocketQueue();
		return true;
	}));

	UE_LOG(LogNovaBridge, Log, TEXT("NovaBridge events WebSocket server started on port %d"), EventWsPort);
#else
	UE_LOG(LogNovaBridge, Warning, TEXT("WebSocketNetworking module not available; events WebSocket server disabled"));
#endif
}

void FNovaBridgeModule::StopEventWebSocketServer()
{
#if NOVABRIDGE_WITH_WEBSOCKET_NETWORKING
	if (EventWsServerTickHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(EventWsServerTickHandle);
		EventWsServerTickHandle.Reset();
	}

	for (FWsClient& Client : EventWsClients)
	{
		if (Client.Socket)
		{
			delete Client.Socket;
			Client.Socket = nullptr;
		}
	}
	EventWsClients.Empty();
	EventWsServer.Reset();
#endif
}

void FNovaBridgeModule::PumpEventSocketQueue()
{
#if NOVABRIDGE_WITH_WEBSOCKET_NETWORKING
	if (EventWsClients.Num() == 0)
	{
		return;
	}

	TArray<FString> PendingPayloads;
	TArray<FString> PendingTypes;
	{
		FScopeLock EventLock(&NovaBridgeEventQueueMutex);
		if (NovaBridgePendingEventPayloads.Num() == 0)
		{
			return;
		}
		PendingPayloads = MoveTemp(NovaBridgePendingEventPayloads);
		PendingTypes = MoveTemp(NovaBridgePendingEventTypes);
		NovaBridgePendingEventPayloads.Reset();
		NovaBridgePendingEventTypes.Reset();
	}
	if (PendingTypes.Num() != PendingPayloads.Num())
	{
		PendingTypes.Init(TEXT("audit"), PendingPayloads.Num());
	}

	for (int32 PayloadIndex = 0; PayloadIndex < PendingPayloads.Num(); ++PayloadIndex)
	{
		const FTCHARToUTF8 Utf8Payload(*PendingPayloads[PayloadIndex]);
		const uint8* Data = reinterpret_cast<const uint8*>(Utf8Payload.Get());
		uint8* MutableData = const_cast<uint8*>(Data);
		const int32 DataLen = Utf8Payload.Length();
		const FString& PayloadType = PendingTypes.IsValidIndex(PayloadIndex) && !PendingTypes[PayloadIndex].IsEmpty()
			? PendingTypes[PayloadIndex]
			: TEXT("audit");

		for (int32 ClientIndex = EventWsClients.Num() - 1; ClientIndex >= 0; --ClientIndex)
		{
			if (!EventWsClients[ClientIndex].Socket)
			{
				EventWsClients.RemoveAtSwap(ClientIndex);
				continue;
			}

			if (EventWsClients[ClientIndex].bEventTypeFilterEnabled
				&& !EventWsClients[ClientIndex].EventTypes.Contains(PayloadType))
			{
				continue;
			}
			EventWsClients[ClientIndex].Socket->Send(MutableData, DataLen, false);
		}
	}
#endif
}

void FNovaBridgeModule::StartStreamTicker()
{
	if (!bStreamActive || WsClients.Num() == 0 || StreamTickHandle.IsValid())
	{
		return;
	}

	LastStreamFrameTime = 0.0;
	StreamTickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([this](float DeltaTime) -> bool
	{
		(void)DeltaTime;
		StreamTick();
		return true;
	}), 0.0f);
}

void FNovaBridgeModule::StopStreamTicker()
{
	if (StreamTickHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(StreamTickHandle);
		StreamTickHandle.Reset();
	}
}

void FNovaBridgeModule::StreamTick()
{
#if NOVABRIDGE_WITH_WEBSOCKET_NETWORKING
	if (!bStreamActive || WsClients.Num() == 0)
	{
		return;
	}

	const double Now = FPlatformTime::Seconds();
	const int32 SafeFps = FMath::Max(1, StreamFps);
	if (Now - LastStreamFrameTime < (1.0 / static_cast<double>(SafeFps)))
	{
		return;
	}
	LastStreamFrameTime = Now;

	AsyncTask(ENamedThreads::GameThread, [this]()
	{
		if (!bStreamActive || WsClients.Num() == 0)
		{
			return;
		}

		EnsureStreamCaptureSetup();
		if (!StreamCaptureActor.IsValid() || !StreamRenderTarget.IsValid())
		{
			return;
		}

		USceneCaptureComponent2D* CaptureComp = StreamCaptureActor->GetCaptureComponent2D();
		StreamCaptureActor->SetActorLocation(CameraLocation);
		StreamCaptureActor->SetActorRotation(CameraRotation);
		CaptureComp->FOVAngle = CameraFOV;
		CaptureComp->CaptureScene();

		FTextureRenderTargetResource* RTResource = StreamRenderTarget->GameThread_GetRenderTargetResource();
		if (!RTResource)
		{
			return;
		}

		TArray<FColor> Bitmap;
		if (!RTResource->ReadPixels(Bitmap) || Bitmap.Num() == 0)
		{
			return;
		}

		IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));
		TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::JPEG);
		ImageWrapper->SetRaw(Bitmap.GetData(), Bitmap.Num() * sizeof(FColor), StreamWidth, StreamHeight, ERGBFormat::BGRA, 8);
		TArray64<uint8> Encoded = ImageWrapper->GetCompressed(FMath::Clamp(StreamQuality, 1, 100));
		if (Encoded.Num() == 0)
		{
			return;
		}

		TArray<uint8> Payload;
		Payload.Append(Encoded.GetData(), static_cast<int32>(Encoded.Num()));
		for (int32 Idx = WsClients.Num() - 1; Idx >= 0; --Idx)
		{
			if (!WsClients[Idx].Socket)
			{
				WsClients.RemoveAtSwap(Idx);
				continue;
			}
			WsClients[Idx].Socket->Send(Payload.GetData(), Payload.Num(), false);
		}
	});
#endif
}

// ============================================================
// JSON Helpers
// ============================================================

TSharedPtr<FJsonObject> FNovaBridgeModule::ParseRequestBody(const FHttpServerRequest& Request)
{
	if (Request.Body.Num() == 0) return nullptr;
	// Body bytes are NOT null-terminated — must copy and add null before converting to FString
	TArray<uint8> NullTermBody(Request.Body);
	NullTermBody.Add(0);
	FString BodyStr = FString(UTF8_TO_TCHAR(reinterpret_cast<const ANSICHAR*>(NullTermBody.GetData())));
	TSharedPtr<FJsonObject> JsonObj;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(BodyStr);
	if (!FJsonSerializer::Deserialize(Reader, JsonObj)) return nullptr;
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
	TSharedPtr<FJsonObject> JsonObj = MakeShareable(new FJsonObject);
	JsonObj->SetStringField(TEXT("status"), TEXT("error"));
	JsonObj->SetStringField(TEXT("error"), Error);
	JsonObj->SetNumberField(TEXT("code"), StatusCode);
	SendJsonResponse(OnComplete, JsonObj, StatusCode);
}

void FNovaBridgeModule::SendOkResponse(const FHttpResultCallback& OnComplete)
{
	TSharedPtr<FJsonObject> JsonObj = MakeShareable(new FJsonObject);
	JsonObj->SetStringField(TEXT("status"), TEXT("ok"));
	SendJsonResponse(OnComplete, JsonObj, 200);
}

// ============================================================
// Health
// ============================================================

bool FNovaBridgeModule::HandleHealth(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	(void)Request;
	TSharedPtr<FJsonObject> JsonObj = MakeShareable(new FJsonObject);
	JsonObj->SetStringField(TEXT("status"), TEXT("ok"));
	JsonObj->SetStringField(TEXT("version"), NovaBridgeVersion);
	JsonObj->SetStringField(TEXT("engine"), TEXT("UnrealEngine"));
	JsonObj->SetStringField(TEXT("mode"), TEXT("editor"));
	JsonObj->SetNumberField(TEXT("port"), HttpPort);
	JsonObj->SetNumberField(TEXT("stream_ws_port"), WsPort);
	JsonObj->SetNumberField(TEXT("events_ws_port"), EventWsPort);
	JsonObj->SetNumberField(TEXT("routes"), ApiRouteCount);
	JsonObj->SetBoolField(TEXT("api_key_required"), !RequiredApiKey.IsEmpty());
	JsonObj->SetStringField(TEXT("default_role"), NovaBridgeDefaultRole);
	SendJsonResponse(OnComplete, JsonObj);
	return true;
}

bool FNovaBridgeModule::HandleProjectInfo(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	(void)Request;
	TSharedPtr<FJsonObject> JsonObj = MakeShareable(new FJsonObject);
	JsonObj->SetStringField(TEXT("status"), TEXT("ok"));
	JsonObj->SetStringField(TEXT("project_name"), FApp::GetProjectName());
	JsonObj->SetStringField(TEXT("project_file"), FPaths::GetProjectFilePath());
	JsonObj->SetStringField(TEXT("project_dir"), FPaths::ProjectDir());
	JsonObj->SetStringField(TEXT("content_dir"), FPaths::ProjectContentDir());
	SendJsonResponse(OnComplete, JsonObj);
	return true;
}

static TArray<FString> BuildCapabilityRoles(const bool bAdmin, const bool bAutomation, const bool bReadOnly)
{
	TArray<FString> Roles;
	if (bAdmin)
	{
		Roles.Add(TEXT("admin"));
	}
	if (bAutomation)
	{
		Roles.Add(TEXT("automation"));
	}
	if (bReadOnly)
	{
		Roles.Add(TEXT("read_only"));
	}
	return Roles;
}

static TSharedPtr<FJsonObject> BuildSpawnBoundsJson()
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

static void RegisterEditorCapabilities(uint32 InEventWsPort)
{
	using namespace NovaBridgeCore;

	FCapabilityRegistry& Registry = FCapabilityRegistry::Get();
	Registry.Reset();

	auto RegisterCapability = [&Registry](const FString& Action, const TArray<FString>& Roles, const TSharedPtr<FJsonObject>& Data)
	{
		FCapabilityRecord Capability;
		Capability.Action = Action;
		Capability.Roles = Roles;
		Capability.Data = Data.IsValid() ? Data : MakeShared<FJsonObject>();
		Registry.RegisterCapability(Capability);
	};

	TArray<TSharedPtr<FJsonValue>> AllowedClassValues;
	for (const FString& AllowedClass : NovaBridgeSpawnClassAllowList())
	{
		AllowedClassValues.Add(MakeShared<FJsonValueString>(AllowedClass));
	}

	TSharedPtr<FJsonObject> SpawnData = MakeShared<FJsonObject>();
	SpawnData->SetArrayField(TEXT("allowedClasses"), AllowedClassValues);
	SpawnData->SetObjectField(TEXT("bounds"), BuildSpawnBoundsJson());
	SpawnData->SetNumberField(TEXT("max_spawn_per_plan_admin"), GetPlanSpawnLimit(TEXT("admin")));
	SpawnData->SetNumberField(TEXT("max_spawn_per_plan_automation"), GetPlanSpawnLimit(TEXT("automation")));
	RegisterCapability(TEXT("spawn"), BuildCapabilityRoles(true, true, false), SpawnData);

	RegisterCapability(TEXT("delete"), BuildCapabilityRoles(true, true, false), MakeShared<FJsonObject>());
	RegisterCapability(TEXT("set"), BuildCapabilityRoles(true, true, false), MakeShared<FJsonObject>());
	RegisterCapability(TEXT("screenshot"), BuildCapabilityRoles(true, true, true), MakeShared<FJsonObject>());

	TSharedPtr<FJsonObject> EventsData = MakeShared<FJsonObject>();
	EventsData->SetStringField(TEXT("endpoint"), TEXT("/nova/events"));
	EventsData->SetStringField(TEXT("ws_url"), FString::Printf(TEXT("ws://localhost:%d"), InEventWsPort));
	EventsData->SetArrayField(TEXT("supported_types"), MakeJsonStringArray(SupportedEventTypes()));
	EventsData->SetStringField(TEXT("filter_query_param"), TEXT("types"));
	EventsData->SetStringField(TEXT("subscription_action"), TEXT("{\"action\":\"subscribe\",\"types\":[\"spawn\",\"error\"]}"));
	RegisterCapability(TEXT("events"), BuildCapabilityRoles(true, true, true), EventsData);

	RegisterCapability(TEXT("executePlan"), BuildCapabilityRoles(true, true, false), MakeShared<FJsonObject>());

	TSharedPtr<FJsonObject> UndoData = MakeShared<FJsonObject>();
	UndoData->SetStringField(TEXT("supported"), TEXT("spawn"));
	RegisterCapability(TEXT("undo"), BuildCapabilityRoles(true, true, false), UndoData);
}

bool FNovaBridgeModule::HandleCapabilities(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	const FString Role = ResolveRoleFromRequest(Request);
	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetStringField(TEXT("status"), TEXT("ok"));
	Result->SetStringField(TEXT("mode"), TEXT("editor"));
	Result->SetStringField(TEXT("version"), NovaBridgeVersion);
	Result->SetStringField(TEXT("default_role"), NovaBridgeDefaultRole);
	Result->SetStringField(TEXT("role"), Role);

	TArray<TSharedPtr<FJsonValue>> Capabilities;
	TArray<NovaBridgeCore::FCapabilityRecord> Snapshot = NovaBridgeCore::FCapabilityRegistry::Get().Snapshot();
	if (Snapshot.Num() == 0)
	{
		RegisterEditorCapabilities(EventWsPort);
		Snapshot = NovaBridgeCore::FCapabilityRegistry::Get().Snapshot();
	}
	Capabilities.Reserve(Snapshot.Num());
	for (const NovaBridgeCore::FCapabilityRecord& Capability : Snapshot)
	{
		Capabilities.Add(MakeShareable(new FJsonValueObject(NovaBridgeCore::CapabilityToJson(Capability))));
	}

	Result->SetArrayField(TEXT("capabilities"), Capabilities);
	SendJsonResponse(OnComplete, Result);
	return true;
}

bool FNovaBridgeModule::HandleEvents(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	const TArray<FString> FilterTypes = ParseEventTypeFilter(Request);
	int32 PendingEvents = 0;
	int32 FilteredPendingEvents = 0;
	TArray<FString> PendingTypesSnapshot;
	{
		FScopeLock Lock(&NovaBridgeEventQueueMutex);
		PendingEvents = NovaBridgePendingEventPayloads.Num();
		PendingTypesSnapshot = NovaBridgePendingEventTypes;
	}

	TMap<FString, int32> PendingByType;
	for (const FString& PendingType : PendingTypesSnapshot)
	{
		PendingByType.FindOrAdd(PendingType)++;
	}
	int32 ClientsWithFilters = 0;
	for (const FWsClient& Client : EventWsClients)
	{
		if (Client.Socket && Client.bEventTypeFilterEnabled)
		{
			ClientsWithFilters++;
		}
	}

	if (FilterTypes.Num() == 0)
	{
		FilteredPendingEvents = PendingEvents;
	}
	else
	{
		for (const FString& PendingType : PendingTypesSnapshot)
		{
			if (FilterTypes.Contains(PendingType))
			{
				FilteredPendingEvents++;
			}
		}
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetStringField(TEXT("status"), TEXT("ok"));
	Result->SetStringField(TEXT("route"), TEXT("/nova/events"));
	Result->SetStringField(TEXT("transport"), TEXT("websocket"));
	Result->SetStringField(TEXT("ws_url"), FString::Printf(TEXT("ws://localhost:%d"), EventWsPort));
	Result->SetNumberField(TEXT("ws_port"), EventWsPort);
	Result->SetNumberField(TEXT("clients"), EventWsClients.Num());
	Result->SetNumberField(TEXT("clients_with_filters"), ClientsWithFilters);
	Result->SetNumberField(TEXT("pending_events"), PendingEvents);
	Result->SetNumberField(TEXT("filtered_pending_events"), FilteredPendingEvents);
	Result->SetArrayField(TEXT("supported_types"), MakeJsonStringArray(SupportedEventTypes()));
	Result->SetStringField(TEXT("subscription_action"), TEXT("{\"action\":\"subscribe\",\"types\":[\"spawn\",\"error\"]}"));
	if (FilterTypes.Num() > 0)
	{
		Result->SetArrayField(TEXT("filter_types"), MakeJsonStringArray(FilterTypes));
	}

	TSharedPtr<FJsonObject> PendingByTypeObj = MakeShared<FJsonObject>();
	for (const TPair<FString, int32>& Pair : PendingByType)
	{
		PendingByTypeObj->SetNumberField(Pair.Key, Pair.Value);
	}
	Result->SetObjectField(TEXT("pending_by_type"), PendingByTypeObj);
#if NOVABRIDGE_WITH_WEBSOCKET_NETWORKING
	Result->SetBoolField(TEXT("websocket_available"), true);
#else
	Result->SetBoolField(TEXT("websocket_available"), false);
#endif
	SendJsonResponse(OnComplete, Result);
	return true;
}

bool FNovaBridgeModule::HandleAuditTrail(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	int32 Limit = 50;
	if (Request.QueryParams.Contains(TEXT("limit")))
	{
		Limit = FMath::Clamp(FCString::Atoi(*Request.QueryParams[TEXT("limit")]), 1, 500);
	}

	TArray<FNovaBridgeAuditEntry> Snapshot;
	{
		FScopeLock Lock(&NovaBridgeAuditMutex);
		Snapshot = NovaBridgeAuditTrail;
	}

	const int32 StartIndex = FMath::Max(0, Snapshot.Num() - Limit);
	TArray<TSharedPtr<FJsonValue>> Entries;
	Entries.Reserve(Snapshot.Num() - StartIndex);
	for (int32 Index = StartIndex; Index < Snapshot.Num(); ++Index)
	{
		const FNovaBridgeAuditEntry& Entry = Snapshot[Index];
		TSharedPtr<FJsonObject> EntryObj = MakeShareable(new FJsonObject);
		EntryObj->SetStringField(TEXT("timestamp_utc"), Entry.TimestampUtc);
		EntryObj->SetStringField(TEXT("route"), Entry.Route);
		EntryObj->SetStringField(TEXT("action"), Entry.Action);
		EntryObj->SetStringField(TEXT("role"), Entry.Role);
		EntryObj->SetStringField(TEXT("status"), Entry.Status);
		EntryObj->SetStringField(TEXT("message"), Entry.Message);
		Entries.Add(MakeShareable(new FJsonValueObject(EntryObj)));
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetStringField(TEXT("status"), TEXT("ok"));
	Result->SetNumberField(TEXT("count"), Entries.Num());
	Result->SetNumberField(TEXT("total"), Snapshot.Num());
	Result->SetArrayField(TEXT("entries"), Entries);
	SendJsonResponse(OnComplete, Result);
	return true;
}

bool FNovaBridgeModule::HandleExecutePlan(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	TSharedPtr<FJsonObject> Body = ParseRequestBody(Request);
	if (!Body)
	{
		SendErrorResponse(OnComplete, TEXT("Invalid JSON body"));
		return true;
	}
	if (!Body->HasTypedField<EJson::Array>(TEXT("steps")))
	{
		SendErrorResponse(OnComplete, TEXT("Missing required field: steps"), 400);
		return true;
	}

	const TArray<TSharedPtr<FJsonValue>> Steps = Body->GetArrayField(TEXT("steps"));
	if (Steps.Num() == 0)
	{
		SendErrorResponse(OnComplete, TEXT("Plan has no steps"), 400);
		return true;
	}

	FString PlanId = Body->HasTypedField<EJson::String>(TEXT("plan_id"))
		? Body->GetStringField(TEXT("plan_id"))
		: FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower);

	FString Role = ResolveRoleFromRequest(Request);
	if (Body->HasTypedField<EJson::String>(TEXT("role")))
	{
		const FString RequestedRole = NormalizeRoleName(Body->GetStringField(TEXT("role")));
		if (!RequestedRole.IsEmpty())
		{
			Role = RequestedRole;
		}
	}

		AsyncTask(ENamedThreads::GameThread, [this, OnComplete, Steps, PlanId, Role]()
		{
			TArray<TSharedPtr<FJsonValue>> StepResults;
			StepResults.Reserve(Steps.Num());
			TArray<FString> StepActions;
			StepActions.SetNum(Steps.Num());
			int32 SpawnedInPlan = 0;

			for (int32 StepIndex = 0; StepIndex < Steps.Num(); ++StepIndex)
			{
				const TSharedPtr<FJsonValue>& StepValue = Steps[StepIndex];
				if (!StepValue.IsValid() || StepValue->Type != EJson::Object)
				{
					StepActions[StepIndex] = TEXT("unknown");
					StepResults.Add(MakeShareable(new FJsonValueObject(
						MakePlanStepResult(StepIndex, TEXT("error"), TEXT("Step must be an object")))));
					continue;
				}

				const TSharedPtr<FJsonObject> StepObj = StepValue->AsObject();
				if (!StepObj.IsValid() || !StepObj->HasTypedField<EJson::String>(TEXT("action")))
				{
					StepActions[StepIndex] = TEXT("unknown");
					StepResults.Add(MakeShareable(new FJsonValueObject(
						MakePlanStepResult(StepIndex, TEXT("error"), TEXT("Missing step action")))));
					continue;
				}

				FString Action = StepObj->GetStringField(TEXT("action"));
				Action.TrimStartAndEndInline();
				Action.ToLowerInline();
				StepActions[StepIndex] = Action;
				const TSharedPtr<FJsonObject> Params = StepObj->HasTypedField<EJson::Object>(TEXT("params"))
					? StepObj->GetObjectField(TEXT("params"))
					: MakeShareable(new FJsonObject);

			if (!IsPlanActionAllowedForRole(Role, Action))
			{
				const FString Message = FString::Printf(TEXT("Role '%s' cannot execute action '%s'"), *Role, *Action);
				StepResults.Add(MakeShareable(new FJsonValueObject(
					MakePlanStepResult(StepIndex, TEXT("error"), Message))));
				PushAuditEntry(TEXT("/nova/executePlan"), Action, Role, TEXT("denied"), Message);
				continue;
			}

			if (Action == TEXT("spawn"))
			{
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
					StepResults.Add(MakeShareable(new FJsonValueObject(
						MakePlanStepResult(StepIndex, TEXT("error"), TEXT("spawn.params.class or spawn.params.type is required")))));
					continue;
				}

				const int32 PlanSpawnLimit = GetPlanSpawnLimit(Role);
				if (SpawnedInPlan >= PlanSpawnLimit)
				{
					const FString Message = FString::Printf(TEXT("Plan spawn limit reached (%d) for role '%s'"), PlanSpawnLimit, *Role);
					StepResults.Add(MakeShareable(new FJsonValueObject(
						MakePlanStepResult(StepIndex, TEXT("error"), Message))));
					continue;
				}

				FVector SpawnLocation;
				FRotator SpawnRotation;
				FVector SpawnScale;
				ParseSpawnTransformFromParams(Params, SpawnLocation, SpawnRotation, SpawnScale);
				if (!IsSpawnClassAllowedForRole(Role, ClassName))
				{
					const FString Message = FString::Printf(TEXT("Class '%s' is not allowed for role '%s'"), *ClassName, *Role);
					StepResults.Add(MakeShareable(new FJsonValueObject(
						MakePlanStepResult(StepIndex, TEXT("error"), Message))));
					PushAuditEntry(TEXT("/nova/executePlan"), Action, Role, TEXT("denied"), Message);
					continue;
				}
				if (!IsSpawnLocationInBounds(SpawnLocation))
				{
					const FString Message = TEXT("Spawn location is outside allowed bounds");
					StepResults.Add(MakeShareable(new FJsonValueObject(
						MakePlanStepResult(StepIndex, TEXT("error"), Message))));
					PushAuditEntry(TEXT("/nova/executePlan"), Action, Role, TEXT("denied"), Message);
					continue;
				}

				const int32 SpawnRateLimit = GetRouteRateLimitPerMinute(Role, TEXT("/nova/scene/spawn"));
				FString SpawnRateError;
				if (!ConsumeRateLimit(Role + TEXT("|plan.spawn"), SpawnRateLimit, SpawnRateError))
				{
					StepResults.Add(MakeShareable(new FJsonValueObject(
						MakePlanStepResult(StepIndex, TEXT("error"), SpawnRateError))));
					PushAuditEntry(TEXT("/nova/executePlan"), Action, Role, TEXT("rate_limited"), SpawnRateError);
					continue;
				}

				if (!GEditor)
				{
					StepResults.Add(MakeShareable(new FJsonValueObject(
						MakePlanStepResult(StepIndex, TEXT("error"), TEXT("No editor")))));
					continue;
				}

				UEditorActorSubsystem* ActorSub = GEditor->GetEditorSubsystem<UEditorActorSubsystem>();
				if (!ActorSub)
				{
					StepResults.Add(MakeShareable(new FJsonValueObject(
						MakePlanStepResult(StepIndex, TEXT("error"), TEXT("No EditorActorSubsystem")))));
					continue;
				}

				UClass* ActorClass = ResolveActorClassByName(ClassName);
				if (!ActorClass)
				{
					StepResults.Add(MakeShareable(new FJsonValueObject(
						MakePlanStepResult(StepIndex, TEXT("error"), FString::Printf(TEXT("Class not found: %s"), *ClassName)))));
					continue;
				}

				AActor* NewActor = ActorSub->SpawnActorFromClass(TSubclassOf<AActor>(ActorClass), SpawnLocation, SpawnRotation);
				if (!NewActor)
				{
					StepResults.Add(MakeShareable(new FJsonValueObject(
						MakePlanStepResult(StepIndex, TEXT("error"), TEXT("Failed to spawn actor")))));
					continue;
				}

				if (!SpawnScale.Equals(FVector(1.0, 1.0, 1.0)))
				{
					NewActor->SetActorScale3D(SpawnScale);
				}

				FString Label = Params->HasTypedField<EJson::String>(TEXT("label")) ? Params->GetStringField(TEXT("label")) : FString();
				if (!Label.IsEmpty())
				{
					NewActor->SetActorLabel(Label);
				}

				FNovaBridgeUndoEntry UndoEntry;
				UndoEntry.Action = TEXT("spawn");
				UndoEntry.ActorName = NewActor->GetName();
				UndoEntry.ActorLabel = NewActor->GetActorLabel();
				PushUndoEntry(UndoEntry);
				SpawnedInPlan++;

				TSharedPtr<FJsonObject> StepResult = MakePlanStepResult(StepIndex, TEXT("success"), TEXT("Spawned actor"));
				StepResult->SetStringField(TEXT("object_id"), NewActor->GetName());
				StepResult->SetStringField(TEXT("label"), NewActor->GetActorLabel());
				StepResult->SetStringField(TEXT("class"), ClassName);
				StepResults.Add(MakeShareable(new FJsonValueObject(StepResult)));
				PushAuditEntry(TEXT("/nova/executePlan"), Action, Role, TEXT("success"),
					FString::Printf(TEXT("Spawned actor '%s'"), *NewActor->GetActorLabel()));
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
					StepResults.Add(MakeShareable(new FJsonValueObject(
						MakePlanStepResult(StepIndex, TEXT("error"), TEXT("delete.params.name is required")))));
					continue;
				}

				AActor* Actor = FindActorByName(ActorName);
				if (!Actor)
				{
					StepResults.Add(MakeShareable(new FJsonValueObject(
						MakePlanStepResult(StepIndex, TEXT("error"), FString::Printf(TEXT("Actor not found: %s"), *ActorName)))));
					continue;
				}
				if (Actor->GetActorLabel() == TEXT("NovaBridge_SceneCapture") || Actor->GetName().Contains(TEXT("NovaBridge_SceneCapture")))
				{
					StepResults.Add(MakeShareable(new FJsonValueObject(
						MakePlanStepResult(StepIndex, TEXT("error"), TEXT("Cannot delete NovaBridge_SceneCapture")))));
					continue;
				}

				if (UEditorActorSubsystem* ActorSub = GEditor ? GEditor->GetEditorSubsystem<UEditorActorSubsystem>() : nullptr)
				{
					ActorSub->DestroyActor(Actor);
				}
				else
				{
					Actor->Destroy();
				}

				StepResults.Add(MakeShareable(new FJsonValueObject(
					MakePlanStepResult(StepIndex, TEXT("success"), FString::Printf(TEXT("Deleted actor %s"), *ActorName)))));
				PushAuditEntry(TEXT("/nova/executePlan"), Action, Role, TEXT("success"),
					FString::Printf(TEXT("Deleted actor '%s'"), *ActorName));
				continue;
			}

			if (Action == TEXT("set"))
			{
				FString Target = Params->HasTypedField<EJson::String>(TEXT("target")) ? Params->GetStringField(TEXT("target")) : FString();
				if (Target.IsEmpty() && Params->HasTypedField<EJson::String>(TEXT("name")))
				{
					Target = Params->GetStringField(TEXT("name"));
				}
				if (Target.IsEmpty())
				{
					StepResults.Add(MakeShareable(new FJsonValueObject(
						MakePlanStepResult(StepIndex, TEXT("error"), TEXT("set.params.target is required")))));
					continue;
				}

				AActor* Actor = FindActorByName(Target);
				if (!Actor)
				{
					StepResults.Add(MakeShareable(new FJsonValueObject(
						MakePlanStepResult(StepIndex, TEXT("error"), FString::Printf(TEXT("Actor not found: %s"), *Target)))));
					continue;
				}

				if (!Params->HasTypedField<EJson::Object>(TEXT("props")))
				{
					StepResults.Add(MakeShareable(new FJsonValueObject(
						MakePlanStepResult(StepIndex, TEXT("error"), TEXT("set.params.props object is required")))));
					continue;
				}

				const TSharedPtr<FJsonObject> Props = Params->GetObjectField(TEXT("props"));
				bool bSetAnyProperty = false;
				bool bSetError = false;
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
						bSetError = true;
						SetErrorMessage = FString::Printf(TEXT("Unsupported value type for property '%s'"), *Key);
						break;
					}

					FString PropertyError;
					if (!SetActorPropertyValue(Actor, Key, ImportText, PropertyError))
					{
						bSetError = true;
						SetErrorMessage = PropertyError;
						break;
					}
					bSetAnyProperty = true;
				}

				if (bSetError)
				{
					StepResults.Add(MakeShareable(new FJsonValueObject(
						MakePlanStepResult(StepIndex, TEXT("error"), SetErrorMessage))));
					PushAuditEntry(TEXT("/nova/executePlan"), Action, Role, TEXT("error"), SetErrorMessage);
					continue;
				}
				if (!bSetAnyProperty)
				{
					StepResults.Add(MakeShareable(new FJsonValueObject(
						MakePlanStepResult(StepIndex, TEXT("error"), TEXT("No valid properties were applied")))));
					continue;
				}

				Actor->PostEditChange();
				StepResults.Add(MakeShareable(new FJsonValueObject(
					MakePlanStepResult(StepIndex, TEXT("success"), FString::Printf(TEXT("Updated actor %s"), *Target)))));
				PushAuditEntry(TEXT("/nova/executePlan"), Action, Role, TEXT("success"),
					FString::Printf(TEXT("Updated actor '%s'"), *Target));
				continue;
			}

			if (Action == TEXT("screenshot"))
			{
				const int32 RequestedWidth = Params->HasField(TEXT("width")) ? static_cast<int32>(Params->GetNumberField(TEXT("width"))) : CaptureWidth;
				const int32 RequestedHeight = Params->HasField(TEXT("height")) ? static_cast<int32>(Params->GetNumberField(TEXT("height"))) : CaptureHeight;
				const bool bInlineImage = (Params->HasTypedField<EJson::Boolean>(TEXT("inline")) && Params->GetBoolField(TEXT("inline")))
					|| (Params->HasTypedField<EJson::Boolean>(TEXT("return_base64")) && Params->GetBoolField(TEXT("return_base64")));

				if (RequestedWidth > 0 && RequestedHeight > 0 && (RequestedWidth != CaptureWidth || RequestedHeight != CaptureHeight))
				{
					CaptureWidth = FMath::Clamp(RequestedWidth, 64, 3840);
					CaptureHeight = FMath::Clamp(RequestedHeight, 64, 2160);
					CleanupCapture();
				}

				EnsureCaptureSetup();
				if (!CaptureActor.IsValid() || !RenderTarget.IsValid())
				{
					StepResults.Add(MakeShareable(new FJsonValueObject(
						MakePlanStepResult(StepIndex, TEXT("error"), TEXT("Failed to initialize scene capture")))));
					continue;
				}

				USceneCaptureComponent2D* CaptureComp = CaptureActor->GetCaptureComponent2D();
				CaptureActor->SetActorLocation(CameraLocation);
				CaptureActor->SetActorRotation(CameraRotation);
				CaptureComp->FOVAngle = CameraFOV;
				CaptureComp->CaptureScene();

				FTextureRenderTargetResource* RTResource = RenderTarget->GameThread_GetRenderTargetResource();
				if (!RTResource)
				{
					StepResults.Add(MakeShareable(new FJsonValueObject(
						MakePlanStepResult(StepIndex, TEXT("error"), TEXT("No render target resource")))));
					continue;
				}

				TArray<FColor> Bitmap;
				if (!RTResource->ReadPixels(Bitmap) || Bitmap.Num() == 0)
				{
					StepResults.Add(MakeShareable(new FJsonValueObject(
						MakePlanStepResult(StepIndex, TEXT("error"), TEXT("Failed to read render target pixels")))));
					continue;
				}

				IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));
				TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);
				ImageWrapper->SetRaw(Bitmap.GetData(), Bitmap.Num() * sizeof(FColor), CaptureWidth, CaptureHeight, ERGBFormat::BGRA, 8);
				TArray64<uint8> PngData = ImageWrapper->GetCompressed(0);
				if (PngData.Num() == 0)
				{
					StepResults.Add(MakeShareable(new FJsonValueObject(
						MakePlanStepResult(StepIndex, TEXT("error"), TEXT("Failed to encode PNG")))));
					continue;
				}

				TSharedPtr<FJsonObject> StepResult = MakePlanStepResult(StepIndex, TEXT("success"), TEXT("Captured screenshot"));
				StepResult->SetNumberField(TEXT("width"), CaptureWidth);
				StepResult->SetNumberField(TEXT("height"), CaptureHeight);
				StepResult->SetStringField(TEXT("format"), TEXT("png"));
				if (bInlineImage)
				{
					const FString Base64 = FBase64::Encode(PngData.GetData(), PngData.Num());
					StepResult->SetStringField(TEXT("image"), Base64);
				}
				StepResults.Add(MakeShareable(new FJsonValueObject(StepResult)));
				PushAuditEntry(TEXT("/nova/executePlan"), Action, Role, TEXT("success"), TEXT("Captured screenshot"));
				continue;
			}

			StepResults.Add(MakeShareable(new FJsonValueObject(
				MakePlanStepResult(StepIndex, TEXT("error"), FString::Printf(TEXT("Unsupported action: %s"), *Action)))));
			PushAuditEntry(TEXT("/nova/executePlan"), Action, Role, TEXT("error"), TEXT("Unsupported action"));
		}

			TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
			Result->SetStringField(TEXT("status"), TEXT("ok"));
			Result->SetStringField(TEXT("plan_id"), PlanId);
			Result->SetStringField(TEXT("mode"), TEXT("editor"));
			Result->SetStringField(TEXT("role"), Role);
			Result->SetArrayField(TEXT("results"), StepResults);
			Result->SetNumberField(TEXT("step_count"), Steps.Num());
			int32 SuccessCount = 0;
			int32 ErrorCount = 0;
			for (const TSharedPtr<FJsonValue>& StepResultValue : StepResults)
			{
				if (!StepResultValue.IsValid() || StepResultValue->Type != EJson::Object)
				{
					continue;
				}
				const TSharedPtr<FJsonObject> StepResultObj = StepResultValue->AsObject();
				if (!StepResultObj.IsValid())
				{
					continue;
				}
				const FString StatusValue = StepResultObj->HasTypedField<EJson::String>(TEXT("status"))
					? StepResultObj->GetStringField(TEXT("status"))
					: FString();
				if (StatusValue == TEXT("success"))
				{
					SuccessCount++;
				}
				else if (StatusValue == TEXT("error"))
				{
					ErrorCount++;
				}
				}
				Result->SetNumberField(TEXT("success_count"), SuccessCount);
				Result->SetNumberField(TEXT("error_count"), ErrorCount);

				for (const TSharedPtr<FJsonValue>& StepResultValue : StepResults)
				{
					if (!StepResultValue.IsValid() || StepResultValue->Type != EJson::Object)
					{
						continue;
					}
					const TSharedPtr<FJsonObject> StepResultObj = StepResultValue->AsObject();
					if (!StepResultObj.IsValid())
					{
						continue;
					}

					const int32 ResultStepIndex = StepResultObj->HasTypedField<EJson::Number>(TEXT("step"))
						? static_cast<int32>(StepResultObj->GetNumberField(TEXT("step")))
						: INDEX_NONE;
					const FString ResultStatus = StepResultObj->HasTypedField<EJson::String>(TEXT("status"))
						? StepResultObj->GetStringField(TEXT("status"))
						: TEXT("error");
					const FString ResultAction = (ResultStepIndex >= 0 && ResultStepIndex < StepActions.Num() && !StepActions[ResultStepIndex].IsEmpty())
						? StepActions[ResultStepIndex]
						: TEXT("unknown");

					TSharedPtr<FJsonObject> PlanStepEvent = MakeShared<FJsonObject>();
					PlanStepEvent->SetStringField(TEXT("type"), ResultStatus.Equals(TEXT("error"), ESearchCase::IgnoreCase) ? TEXT("error") : TEXT("plan_step"));
					PlanStepEvent->SetStringField(TEXT("mode"), TEXT("editor"));
					PlanStepEvent->SetStringField(TEXT("timestamp_utc"), FDateTime::UtcNow().ToIso8601());
					PlanStepEvent->SetStringField(TEXT("route"), TEXT("/nova/executePlan"));
					PlanStepEvent->SetStringField(TEXT("action"), ResultAction);
					PlanStepEvent->SetStringField(TEXT("status"), ResultStatus);
					PlanStepEvent->SetStringField(TEXT("plan_id"), PlanId);
					PlanStepEvent->SetNumberField(TEXT("step"), ResultStepIndex);
					if (StepResultObj->HasTypedField<EJson::String>(TEXT("message")))
					{
						PlanStepEvent->SetStringField(TEXT("message"), StepResultObj->GetStringField(TEXT("message")));
					}
					if (StepResultObj->HasTypedField<EJson::String>(TEXT("object_id")))
					{
						PlanStepEvent->SetStringField(TEXT("object_id"), StepResultObj->GetStringField(TEXT("object_id")));
					}
					QueueEventObject(PlanStepEvent);
				}

				TSharedPtr<FJsonObject> PlanCompleteEvent = MakeShared<FJsonObject>();
				PlanCompleteEvent->SetStringField(TEXT("type"), TEXT("plan_complete"));
				PlanCompleteEvent->SetStringField(TEXT("mode"), TEXT("editor"));
				PlanCompleteEvent->SetStringField(TEXT("timestamp_utc"), FDateTime::UtcNow().ToIso8601());
				PlanCompleteEvent->SetStringField(TEXT("route"), TEXT("/nova/executePlan"));
				PlanCompleteEvent->SetStringField(TEXT("action"), TEXT("executePlan.complete"));
				PlanCompleteEvent->SetStringField(TEXT("status"), ErrorCount == 0 ? TEXT("success") : TEXT("partial"));
				PlanCompleteEvent->SetStringField(TEXT("plan_id"), PlanId);
				PlanCompleteEvent->SetStringField(TEXT("role"), Role);
				PlanCompleteEvent->SetNumberField(TEXT("step_count"), Steps.Num());
				PlanCompleteEvent->SetNumberField(TEXT("success_count"), SuccessCount);
				PlanCompleteEvent->SetNumberField(TEXT("error_count"), ErrorCount);
				QueueEventObject(PlanCompleteEvent);

				PushAuditEntry(TEXT("/nova/executePlan"), TEXT("executePlan.complete"), Role, TEXT("success"),
					FString::Printf(TEXT("Plan %s complete: %d success, %d error"), *PlanId, SuccessCount, ErrorCount));
				SendJsonResponse(OnComplete, Result);
		});
	return true;
}

bool FNovaBridgeModule::HandleUndo(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	const FString Role = ResolveRoleFromRequest(Request);
	if (Role == TEXT("read_only"))
	{
		SendErrorResponse(OnComplete, TEXT("Role 'read_only' cannot execute undo"), 403);
		return true;
	}

	AsyncTask(ENamedThreads::GameThread, [this, OnComplete, Role]()
	{
		FNovaBridgeUndoEntry Entry;
		if (!PopUndoEntry(Entry))
		{
			SendErrorResponse(OnComplete, TEXT("Undo stack is empty"), 404);
			return;
		}

		if (Entry.Action == TEXT("spawn"))
		{
			AActor* Actor = FindActorByName(Entry.ActorName);
			bool bDeleted = false;
			if (Actor)
			{
				if (UEditorActorSubsystem* ActorSub = GEditor ? GEditor->GetEditorSubsystem<UEditorActorSubsystem>() : nullptr)
				{
					ActorSub->DestroyActor(Actor);
					bDeleted = true;
				}
				else
				{
					Actor->Destroy();
					bDeleted = true;
				}
			}

			TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
			Result->SetStringField(TEXT("status"), TEXT("ok"));
			Result->SetStringField(TEXT("undone_action"), Entry.Action);
			Result->SetStringField(TEXT("actor_name"), Entry.ActorName);
			Result->SetStringField(TEXT("actor_label"), Entry.ActorLabel);
			Result->SetBoolField(TEXT("deleted"), bDeleted);
			SendJsonResponse(OnComplete, Result);
			PushAuditEntry(TEXT("/nova/undo"), TEXT("undo"), Role, TEXT("success"),
				FString::Printf(TEXT("Undo spawn for actor '%s'"), *Entry.ActorName));
			return;
		}

		SendErrorResponse(OnComplete, FString::Printf(TEXT("Unsupported undo action: %s"), *Entry.Action), 400);
		PushAuditEntry(TEXT("/nova/undo"), TEXT("undo"), Role, TEXT("error"),
			FString::Printf(TEXT("Unsupported undo action: %s"), *Entry.Action));
	});
	return true;
}

// ============================================================
// Scene Handlers
// ============================================================

bool FNovaBridgeModule::HandleSceneList(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	AsyncTask(ENamedThreads::GameThread, [this, OnComplete]()
	{
		if (!GEditor)
		{
			SendErrorResponse(OnComplete, TEXT("No editor"), 500);
			return;
		}

		UWorld* World = GEditor->GetEditorWorldContext().World();
		if (!World)
		{
			SendErrorResponse(OnComplete, TEXT("No world"), 500);
			return;
		}

		TArray<TSharedPtr<FJsonValue>> ActorArray;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			ActorArray.Add(MakeShareable(new FJsonValueObject(ActorToJson(*It))));
		}

		TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
		Result->SetArrayField(TEXT("actors"), ActorArray);
		Result->SetNumberField(TEXT("count"), ActorArray.Num());
		Result->SetStringField(TEXT("level"), World->GetMapName());
		SendJsonResponse(OnComplete, Result);
	});
	return true;
}

bool FNovaBridgeModule::HandleSceneSpawn(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	TSharedPtr<FJsonObject> Body = ParseRequestBody(Request);
	if (!Body)
	{
		SendErrorResponse(OnComplete, TEXT("Invalid JSON body"));
		return true;
	}
	if (!Body->HasTypedField<EJson::String>(TEXT("class")) || Body->GetStringField(TEXT("class")).IsEmpty())
	{
		SendErrorResponse(OnComplete, TEXT("Missing required parameter: 'class'"));
		return true;
	}

	FString ClassName = Body->GetStringField(TEXT("class"));
	double X = Body->HasField(TEXT("x")) ? Body->GetNumberField(TEXT("x")) : 0.0;
	double Y = Body->HasField(TEXT("y")) ? Body->GetNumberField(TEXT("y")) : 0.0;
	double Z = Body->HasField(TEXT("z")) ? Body->GetNumberField(TEXT("z")) : 0.0;
	double Pitch = Body->HasField(TEXT("pitch")) ? Body->GetNumberField(TEXT("pitch")) : 0.0;
	double Yaw = Body->HasField(TEXT("yaw")) ? Body->GetNumberField(TEXT("yaw")) : 0.0;
	double Roll = Body->HasField(TEXT("roll")) ? Body->GetNumberField(TEXT("roll")) : 0.0;
	FString Label = Body->HasField(TEXT("label")) ? Body->GetStringField(TEXT("label")) : TEXT("");
	const FString Role = ResolveRoleFromRequest(Request);
	const FVector RequestedLocation(X, Y, Z);
	if (!IsSpawnClassAllowedForRole(Role, ClassName))
	{
		PushAuditEntry(TEXT("/nova/scene/spawn"), TEXT("scene.spawn"), Role, TEXT("denied"),
			FString::Printf(TEXT("Class not allowed for role: %s"), *ClassName));
		SendErrorResponse(OnComplete, FString::Printf(TEXT("Class '%s' is not allowed for role '%s'"), *ClassName, *Role), 403);
		return true;
	}
	if (!IsSpawnLocationInBounds(RequestedLocation))
	{
		PushAuditEntry(TEXT("/nova/scene/spawn"), TEXT("scene.spawn"), Role, TEXT("denied"), TEXT("Spawn location out of bounds"));
		SendErrorResponse(OnComplete, TEXT("Spawn location is outside allowed bounds"), 403);
		return true;
	}

	AsyncTask(ENamedThreads::GameThread, [this, OnComplete, ClassName, X, Y, Z, Pitch, Yaw, Roll, Label, Role]()
	{
		if (!GEditor)
		{
			PushAuditEntry(TEXT("/nova/scene/spawn"), TEXT("scene.spawn"), Role, TEXT("error"), TEXT("No editor"));
			SendErrorResponse(OnComplete, TEXT("No editor"), 500);
			return;
		}

		UEditorActorSubsystem* ActorSub = GEditor->GetEditorSubsystem<UEditorActorSubsystem>();
		if (!ActorSub)
		{
			PushAuditEntry(TEXT("/nova/scene/spawn"), TEXT("scene.spawn"), Role, TEXT("error"), TEXT("No EditorActorSubsystem"));
			SendErrorResponse(OnComplete, TEXT("No EditorActorSubsystem"), 500);
			return;
		}

		UClass* ActorClass = ResolveActorClassByName(ClassName);

		if (!ActorClass)
		{
			PushAuditEntry(TEXT("/nova/scene/spawn"), TEXT("scene.spawn"), Role, TEXT("error"),
				FString::Printf(TEXT("Class not found: %s"), *ClassName));
			SendErrorResponse(OnComplete, FString::Printf(TEXT("Class not found: %s"), *ClassName));
			return;
		}

		FVector Location(X, Y, Z);
		FRotator Rotation(Pitch, Yaw, Roll);

		AActor* NewActor = ActorSub->SpawnActorFromClass(TSubclassOf<AActor>(ActorClass), Location, Rotation);
		if (!NewActor)
		{
			PushAuditEntry(TEXT("/nova/scene/spawn"), TEXT("scene.spawn"), Role, TEXT("error"), TEXT("Failed to spawn actor"));
			SendErrorResponse(OnComplete, TEXT("Failed to spawn actor"), 500);
			return;
		}

		if (!Label.IsEmpty())
		{
			NewActor->SetActorLabel(Label);
		}
		FNovaBridgeUndoEntry UndoEntry;
		UndoEntry.Action = TEXT("spawn");
		UndoEntry.ActorName = NewActor->GetName();
		UndoEntry.ActorLabel = NewActor->GetActorLabel();
			PushUndoEntry(UndoEntry);
			PushAuditEntry(TEXT("/nova/scene/spawn"), TEXT("scene.spawn"), Role, TEXT("success"),
				FString::Printf(TEXT("Spawned actor '%s' (%s)"), *NewActor->GetActorLabel(), *ClassName));
			TSharedPtr<FJsonObject> SpawnEvent = MakeShared<FJsonObject>();
			SpawnEvent->SetStringField(TEXT("type"), TEXT("spawn"));
			SpawnEvent->SetStringField(TEXT("mode"), TEXT("editor"));
			SpawnEvent->SetStringField(TEXT("timestamp_utc"), FDateTime::UtcNow().ToIso8601());
			SpawnEvent->SetStringField(TEXT("route"), TEXT("/nova/scene/spawn"));
			SpawnEvent->SetStringField(TEXT("action"), TEXT("scene.spawn"));
			SpawnEvent->SetStringField(TEXT("role"), Role);
			SpawnEvent->SetStringField(TEXT("actor_name"), NewActor->GetName());
			SpawnEvent->SetStringField(TEXT("actor_label"), NewActor->GetActorLabel());
			SpawnEvent->SetStringField(TEXT("class"), ClassName);
			QueueEventObject(SpawnEvent);

			SendJsonResponse(OnComplete, ActorToJson(NewActor));
		});
	return true;
}

bool FNovaBridgeModule::HandleSceneDelete(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	TSharedPtr<FJsonObject> Body = ParseRequestBody(Request);
	if (!Body)
	{
		SendErrorResponse(OnComplete, TEXT("Invalid JSON body"));
		return true;
	}

	FString ActorName = Body->GetStringField(TEXT("name"));
	const FString Role = ResolveRoleFromRequest(Request);

	AsyncTask(ENamedThreads::GameThread, [this, OnComplete, ActorName, Role]()
	{
		AActor* Actor = FindActorByName(ActorName);
		if (!Actor)
		{
			PushAuditEntry(TEXT("/nova/scene/delete"), TEXT("scene.delete"), Role, TEXT("error"),
				FString::Printf(TEXT("Actor not found: %s"), *ActorName));
			SendErrorResponse(OnComplete, FString::Printf(TEXT("Actor not found: %s"), *ActorName), 404);
			return;
		}

		// Protect the NovaBridge scene capture actor from deletion
		if (Actor->GetActorLabel() == TEXT("NovaBridge_SceneCapture") || Actor->GetName().Contains(TEXT("NovaBridge_SceneCapture")))
		{
			PushAuditEntry(TEXT("/nova/scene/delete"), TEXT("scene.delete"), Role, TEXT("denied"),
				TEXT("Attempted deletion of NovaBridge_SceneCapture"));
			SendErrorResponse(OnComplete, TEXT("Cannot delete NovaBridge_SceneCapture — it is required for viewport screenshots"));
			return;
		}

		UEditorActorSubsystem* ActorSub = GEditor->GetEditorSubsystem<UEditorActorSubsystem>();
		if (ActorSub)
		{
			ActorSub->DestroyActor(Actor);
		}
		else
		{
			Actor->Destroy();
		}

			PushAuditEntry(TEXT("/nova/scene/delete"), TEXT("scene.delete"), Role, TEXT("success"),
				FString::Printf(TEXT("Deleted actor '%s'"), *ActorName));
			TSharedPtr<FJsonObject> DeleteEvent = MakeShared<FJsonObject>();
			DeleteEvent->SetStringField(TEXT("type"), TEXT("delete"));
			DeleteEvent->SetStringField(TEXT("mode"), TEXT("editor"));
			DeleteEvent->SetStringField(TEXT("timestamp_utc"), FDateTime::UtcNow().ToIso8601());
			DeleteEvent->SetStringField(TEXT("route"), TEXT("/nova/scene/delete"));
			DeleteEvent->SetStringField(TEXT("action"), TEXT("scene.delete"));
			DeleteEvent->SetStringField(TEXT("role"), Role);
			DeleteEvent->SetStringField(TEXT("actor_name"), ActorName);
			QueueEventObject(DeleteEvent);
			SendOkResponse(OnComplete);
		});
	return true;
}

bool FNovaBridgeModule::HandleSceneTransform(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	TSharedPtr<FJsonObject> Body = ParseRequestBody(Request);
	if (!Body)
	{
		SendErrorResponse(OnComplete, TEXT("Invalid JSON body"));
		return true;
	}

	FString ActorName = Body->GetStringField(TEXT("name"));
	TSharedPtr<FJsonObject> LocObj = Body->HasField(TEXT("location")) ? Body->GetObjectField(TEXT("location")) : nullptr;
	TSharedPtr<FJsonObject> RotObj = Body->HasField(TEXT("rotation")) ? Body->GetObjectField(TEXT("rotation")) : nullptr;
	TSharedPtr<FJsonObject> ScaleObj = Body->HasField(TEXT("scale")) ? Body->GetObjectField(TEXT("scale")) : nullptr;

	AsyncTask(ENamedThreads::GameThread, [this, OnComplete, ActorName, LocObj, RotObj, ScaleObj]()
	{
		AActor* Actor = FindActorByName(ActorName);
		if (!Actor)
		{
			SendErrorResponse(OnComplete, FString::Printf(TEXT("Actor not found: %s"), *ActorName), 404);
			return;
		}

		if (LocObj)
		{
			FVector Loc(LocObj->GetNumberField(TEXT("x")), LocObj->GetNumberField(TEXT("y")), LocObj->GetNumberField(TEXT("z")));
			Actor->SetActorLocation(Loc);
		}
		if (RotObj)
		{
			FRotator Rot(RotObj->GetNumberField(TEXT("pitch")), RotObj->GetNumberField(TEXT("yaw")), RotObj->GetNumberField(TEXT("roll")));
			Actor->SetActorRotation(Rot);
		}
		if (ScaleObj)
		{
			FVector Scale(ScaleObj->GetNumberField(TEXT("x")), ScaleObj->GetNumberField(TEXT("y")), ScaleObj->GetNumberField(TEXT("z")));
			Actor->SetActorScale3D(Scale);
		}

		SendJsonResponse(OnComplete, ActorToJson(Actor));
	});
	return true;
}

bool FNovaBridgeModule::HandleSceneGet(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	// Accept name from query params or body
	FString ActorName;
	if (Request.QueryParams.Contains(TEXT("name")))
	{
		ActorName = Request.QueryParams[TEXT("name")];
	}
	else
	{
		TSharedPtr<FJsonObject> Body = ParseRequestBody(Request);
		if (Body) ActorName = Body->GetStringField(TEXT("name"));
	}

	if (ActorName.IsEmpty())
	{
		SendErrorResponse(OnComplete, TEXT("Missing 'name' parameter"));
		return true;
	}

	AsyncTask(ENamedThreads::GameThread, [this, OnComplete, ActorName]()
	{
		AActor* Actor = FindActorByName(ActorName);
		if (!Actor)
		{
			SendErrorResponse(OnComplete, FString::Printf(TEXT("Actor not found: %s"), *ActorName), 404);
			return;
		}

		TSharedPtr<FJsonObject> Result = ActorToJson(Actor);

		// Add editable properties
		TSharedPtr<FJsonObject> Props = MakeShareable(new FJsonObject);
		for (TFieldIterator<FProperty> PropIt(Actor->GetClass()); PropIt; ++PropIt)
		{
			FProperty* Prop = *PropIt;
			if (!Prop->HasAnyPropertyFlags(CPF_Edit | CPF_BlueprintVisible)) continue;

			FString ValueStr;
			void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Actor);
			Prop->ExportTextItem_Direct(ValueStr, ValuePtr, nullptr, Actor, PPF_None);
			Props->SetStringField(Prop->GetName(), ValueStr);
		}
		Result->SetObjectField(TEXT("properties"), Props);

		// Add components with their key properties
		TArray<TSharedPtr<FJsonValue>> Components;
		for (UActorComponent* Comp : Actor->GetComponents())
		{
			TSharedPtr<FJsonObject> CompObj = MakeShareable(new FJsonObject);
			CompObj->SetStringField(TEXT("name"), Comp->GetName());
			CompObj->SetStringField(TEXT("class"), Comp->GetClass()->GetName());

			// Include editable properties of each component (limited to avoid huge responses)
			TSharedPtr<FJsonObject> CompProps = MakeShareable(new FJsonObject);
			int32 PropCount = 0;
			for (TFieldIterator<FProperty> PropIt(Comp->GetClass()); PropIt; ++PropIt)
			{
				FProperty* Prop = *PropIt;
				if (!Prop->HasAnyPropertyFlags(CPF_Edit | CPF_BlueprintVisible)) continue;
				if (PropCount >= 30) break; // Limit to prevent huge responses

				FString ValueStr;
				void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Comp);
				Prop->ExportTextItem_Direct(ValueStr, ValuePtr, nullptr, Comp, PPF_None);
				if (ValueStr.Len() < 200) // Skip very long values
				{
					CompProps->SetStringField(Prop->GetName(), ValueStr);
					PropCount++;
				}
			}
			CompObj->SetObjectField(TEXT("properties"), CompProps);

			// Include hint about how to set properties: "ComponentName.PropertyName"
			CompObj->SetStringField(TEXT("set_property_prefix"), Comp->GetName());

			Components.Add(MakeShareable(new FJsonValueObject(CompObj)));
		}
		Result->SetArrayField(TEXT("components"), Components);

		SendJsonResponse(OnComplete, Result);
	});
	return true;
}

bool FNovaBridgeModule::HandleSceneSetProperty(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	TSharedPtr<FJsonObject> Body = ParseRequestBody(Request);
	if (!Body)
	{
		SendErrorResponse(OnComplete, TEXT("Invalid JSON body"));
		return true;
	}

	FString ActorName = Body->GetStringField(TEXT("name"));
	FString PropertyName = Body->GetStringField(TEXT("property"));
	FString Value = Body->GetStringField(TEXT("value"));
	const FString Role = ResolveRoleFromRequest(Request);

	AsyncTask(ENamedThreads::GameThread, [this, OnComplete, ActorName, PropertyName, Value, Role]()
	{
		AActor* Actor = FindActorByName(ActorName);
		if (!Actor)
		{
			SendErrorResponse(OnComplete, FString::Printf(TEXT("Actor not found: %s"), *ActorName), 404);
			return;
		}
		FString PropertyError;
		if (!SetActorPropertyValue(Actor, PropertyName, Value, PropertyError))
		{
			const int32 Code = PropertyError.StartsWith(TEXT("Property not found")) ? 404 : 400;
			SendErrorResponse(OnComplete, PropertyError, Code);
			PushAuditEntry(TEXT("/nova/scene/set-property"), TEXT("scene.set-property"), Role, TEXT("error"), PropertyError);
			return;
		}

		PushAuditEntry(TEXT("/nova/scene/set-property"), TEXT("scene.set-property"), Role, TEXT("success"),
			FString::Printf(TEXT("Set %s on %s"), *PropertyName, *ActorName));
		SendOkResponse(OnComplete);
	});
	return true;
}

// ============================================================
// Asset Handlers
// ============================================================

bool FNovaBridgeModule::HandleAssetList(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	FString Path = TEXT("/Game");
	if (Request.QueryParams.Contains(TEXT("path")))
	{
		Path = Request.QueryParams[TEXT("path")];
	}
	else
	{
		TSharedPtr<FJsonObject> Body = ParseRequestBody(Request);
		if (Body && Body->HasField(TEXT("path"))) Path = Body->GetStringField(TEXT("path"));
	}

	AsyncTask(ENamedThreads::GameThread, [this, OnComplete, Path]()
	{
		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

		TArray<FAssetData> Assets;
		AssetRegistry.GetAssetsByPath(FName(*Path), Assets, true);

		TArray<TSharedPtr<FJsonValue>> AssetArray;
		for (const FAssetData& Asset : Assets)
		{
			TSharedPtr<FJsonObject> AssetObj = MakeShareable(new FJsonObject);
			AssetObj->SetStringField(TEXT("name"), Asset.AssetName.ToString());
			AssetObj->SetStringField(TEXT("path"), Asset.GetObjectPathString());
			AssetObj->SetStringField(TEXT("class"), Asset.AssetClassPath.GetAssetName().ToString());
			AssetObj->SetStringField(TEXT("package"), Asset.PackageName.ToString());
			AssetArray.Add(MakeShareable(new FJsonValueObject(AssetObj)));
		}

		TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
		Result->SetArrayField(TEXT("assets"), AssetArray);
		Result->SetNumberField(TEXT("count"), AssetArray.Num());
		SendJsonResponse(OnComplete, Result);
	});
	return true;
}

bool FNovaBridgeModule::HandleAssetCreate(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	TSharedPtr<FJsonObject> Body = ParseRequestBody(Request);
	if (!Body)
	{
		SendErrorResponse(OnComplete, TEXT("Invalid JSON body"));
		return true;
	}

	FString Type = Body->GetStringField(TEXT("type"));
	FString Name = Body->GetStringField(TEXT("name"));
	FString Path = Body->HasField(TEXT("path")) ? Body->GetStringField(TEXT("path")) : TEXT("/Game");

	AsyncTask(ENamedThreads::GameThread, [this, OnComplete, Type, Name, Path]()
	{
		FString PackagePath = Path / Name;
		UPackage* Package = CreatePackage(*PackagePath);
		if (!Package)
		{
			SendErrorResponse(OnComplete, TEXT("Failed to create package"), 500);
			return;
		}

		UObject* NewAsset = nullptr;
		if (Type == TEXT("Material"))
		{
			UMaterialFactoryNew* Factory = NewObject<UMaterialFactoryNew>();
			NewAsset = Factory->FactoryCreateNew(UMaterial::StaticClass(), Package, FName(*Name), RF_Public | RF_Standalone, nullptr, GWarn);
		}
		else if (Type == TEXT("StaticMesh"))
		{
			NewAsset = NewObject<UStaticMesh>(Package, FName(*Name), RF_Public | RF_Standalone);
		}

		if (!NewAsset)
		{
			SendErrorResponse(OnComplete, FString::Printf(TEXT("Unsupported or failed type: %s"), *Type));
			return;
		}

		FAssetRegistryModule::AssetCreated(NewAsset);
		NewAsset->MarkPackageDirty();

		TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
		Result->SetStringField(TEXT("status"), TEXT("ok"));
		Result->SetStringField(TEXT("path"), NewAsset->GetPathName());
		Result->SetStringField(TEXT("type"), Type);
		SendJsonResponse(OnComplete, Result);
	});
	return true;
}

bool FNovaBridgeModule::HandleAssetDuplicate(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	TSharedPtr<FJsonObject> Body = ParseRequestBody(Request);
	if (!Body)
	{
		SendErrorResponse(OnComplete, TEXT("Invalid JSON body"));
		return true;
	}

	FString Source = Body->GetStringField(TEXT("source"));
	FString Destination = Body->GetStringField(TEXT("destination"));

	AsyncTask(ENamedThreads::GameThread, [this, OnComplete, Source, Destination]()
	{
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7)
		UObject* DuplicatedAsset = UEditorAssetLibrary::DuplicateAsset(Source, Destination);
		bool Success = (DuplicatedAsset != nullptr);
#else
		bool Success = UEditorAssetLibrary::DuplicateAsset(Source, Destination);
#endif
		if (!Success)
		{
			SendErrorResponse(OnComplete, TEXT("Failed to duplicate asset"), 500);
			return;
		}

		TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
		Result->SetStringField(TEXT("status"), TEXT("ok"));
		Result->SetStringField(TEXT("path"), Destination);
		SendJsonResponse(OnComplete, Result);
	});
	return true;
}

bool FNovaBridgeModule::HandleAssetDelete(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	TSharedPtr<FJsonObject> Body = ParseRequestBody(Request);
	if (!Body)
	{
		SendErrorResponse(OnComplete, TEXT("Invalid JSON body"));
		return true;
	}

	FString AssetPath = Body->GetStringField(TEXT("path"));

	AsyncTask(ENamedThreads::GameThread, [this, OnComplete, AssetPath]()
	{
		bool Success = UEditorAssetLibrary::DeleteAsset(AssetPath);
		if (!Success)
		{
			SendErrorResponse(OnComplete, TEXT("Failed to delete asset"), 500);
			return;
		}
		SendOkResponse(OnComplete);
	});
	return true;
}

bool FNovaBridgeModule::HandleAssetRename(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	TSharedPtr<FJsonObject> Body = ParseRequestBody(Request);
	if (!Body)
	{
		SendErrorResponse(OnComplete, TEXT("Invalid JSON body"));
		return true;
	}

	FString Source = Body->GetStringField(TEXT("source"));
	FString Destination = Body->GetStringField(TEXT("destination"));

	AsyncTask(ENamedThreads::GameThread, [this, OnComplete, Source, Destination]()
	{
		bool Success = UEditorAssetLibrary::RenameAsset(Source, Destination);
		if (!Success)
		{
			SendErrorResponse(OnComplete, TEXT("Failed to rename asset"), 500);
			return;
		}

		TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
		Result->SetStringField(TEXT("status"), TEXT("ok"));
		Result->SetStringField(TEXT("path"), Destination);
		SendJsonResponse(OnComplete, Result);
	});
	return true;
}

bool FNovaBridgeModule::HandleAssetInfo(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	FString AssetPath;
	if (Request.QueryParams.Contains(TEXT("path")))
	{
		AssetPath = Request.QueryParams[TEXT("path")];
	}
	else
	{
		TSharedPtr<FJsonObject> Body = ParseRequestBody(Request);
		if (Body) AssetPath = Body->GetStringField(TEXT("path"));
	}

	if (AssetPath.IsEmpty())
	{
		SendErrorResponse(OnComplete, TEXT("Missing 'path' parameter"));
		return true;
	}

	AsyncTask(ENamedThreads::GameThread, [this, OnComplete, AssetPath]()
	{
		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		const FSoftObjectPath ObjectPath(AssetPath);
		FAssetData AssetData = AssetRegistryModule.Get().GetAssetByObjectPath(ObjectPath);

		if (!AssetData.IsValid())
		{
			SendErrorResponse(OnComplete, TEXT("Asset not found"), 404);
			return;
		}

		TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
		Result->SetStringField(TEXT("name"), AssetData.AssetName.ToString());
		Result->SetStringField(TEXT("path"), AssetData.GetObjectPathString());
		Result->SetStringField(TEXT("class"), AssetData.AssetClassPath.GetAssetName().ToString());
		Result->SetStringField(TEXT("package"), AssetData.PackageName.ToString());

		// Add tags
		TSharedPtr<FJsonObject> Tags = MakeShareable(new FJsonObject);
		for (const auto& TagPair : AssetData.TagsAndValues)
		{
			Tags->SetStringField(TagPair.Key.ToString(), TagPair.Value.GetValue());
		}
		Result->SetObjectField(TEXT("tags"), Tags);

		SendJsonResponse(OnComplete, Result);
	});
	return true;
}

bool FNovaBridgeModule::HandleAssetImport(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	TSharedPtr<FJsonObject> Body = ParseRequestBody(Request);
	if (!Body)
	{
		SendErrorResponse(OnComplete, TEXT("Invalid JSON body"));
		return true;
	}

	FString FilePath = Body->GetStringField(TEXT("file_path"));
	FString AssetName = Body->HasField(TEXT("asset_name")) ? Body->GetStringField(TEXT("asset_name")) : TEXT("");
	FString Destination = Body->HasField(TEXT("destination")) ? Body->GetStringField(TEXT("destination")) : TEXT("/Game");
	const float ImportScale = Body->HasField(TEXT("scale")) ? static_cast<float>(Body->GetNumberField(TEXT("scale"))) : 100.0f;

	if (FilePath.IsEmpty())
	{
		SendErrorResponse(OnComplete, TEXT("Missing 'file_path'"));
		return true;
	}

	if (!FPaths::FileExists(FilePath))
	{
		SendErrorResponse(OnComplete, FString::Printf(TEXT("File not found: %s"), *FilePath));
		return true;
	}
	if (!FMath::IsFinite(ImportScale) || ImportScale <= 0.0f)
	{
		SendErrorResponse(OnComplete, TEXT("Invalid 'scale'. Provide a positive number."));
		return true;
	}

	const bool bIsObj = FilePath.EndsWith(TEXT(".obj"), ESearchCase::IgnoreCase);
	const bool bIsFbx = FilePath.EndsWith(TEXT(".fbx"), ESearchCase::IgnoreCase);
	if (!bIsObj && !bIsFbx)
	{
		SendErrorResponse(OnComplete, TEXT("Unsupported file format. Supported: .obj, .fbx"));
		return true;
	}

	if (bIsFbx)
	{
		AsyncTask(ENamedThreads::GameThread, [this, OnComplete, FilePath, AssetName, Destination]()
		{
			UAssetImportTask* Task = NewObject<UAssetImportTask>();
			Task->Filename = FilePath;
			Task->DestinationPath = Destination;
			Task->bAutomated = true;
			Task->bReplaceExisting = true;
			Task->bSave = true;
			if (!AssetName.IsEmpty())
			{
				Task->DestinationName = AssetName;
			}

			FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
			TArray<UAssetImportTask*> Tasks;
			Tasks.Add(Task);
			AssetToolsModule.Get().ImportAssetTasks(Tasks);

			if (Task->ImportedObjectPaths.Num() == 0)
			{
				SendErrorResponse(OnComplete, TEXT("FBX import failed. This platform/build may not have FBX importer support enabled."));
				return;
			}

			TArray<TSharedPtr<FJsonValue>> ImportedAssets;
			for (const FString& ImportedPath : Task->ImportedObjectPaths)
			{
				ImportedAssets.Add(MakeShareable(new FJsonValueString(ImportedPath)));
			}

			TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
			Result->SetStringField(TEXT("status"), TEXT("ok"));
			Result->SetStringField(TEXT("format"), TEXT("fbx"));
			Result->SetArrayField(TEXT("imported_assets"), ImportedAssets);
			Result->SetStringField(TEXT("source_file"), FilePath);
			SendJsonResponse(OnComplete, Result);
		});
		return true;
	}

	AsyncTask(ENamedThreads::GameThread, [this, OnComplete, FilePath, AssetName, Destination, ImportScale]()
	{
		// Parse OBJ file
		FString ObjContent;
		if (!FFileHelper::LoadFileToString(ObjContent, *FilePath))
		{
			SendErrorResponse(OnComplete, TEXT("Failed to read OBJ file"));
			return;
		}

		TArray<FVector> Positions;
		TArray<FVector2D> UVs;
		TArray<FVector> Normals;

		struct ObjFaceVert { int32 PosIdx; int32 UVIdx; int32 NormIdx; };
		TArray<TArray<ObjFaceVert>> Faces;

		TArray<FString> Lines;
		ObjContent.ParseIntoArrayLines(Lines);

		for (const FString& Line : Lines)
		{
			FString Trimmed = Line.TrimStartAndEnd();
			if (Trimmed.StartsWith(TEXT("v ")))
			{
				TArray<FString> Parts;
				Trimmed.Mid(2).ParseIntoArrayWS(Parts);
				if (Parts.Num() >= 3)
				{
					Positions.Add(FVector(FCString::Atof(*Parts[0]), FCString::Atof(*Parts[1]), FCString::Atof(*Parts[2])));
				}
			}
			else if (Trimmed.StartsWith(TEXT("vt ")))
			{
				TArray<FString> Parts;
				Trimmed.Mid(3).ParseIntoArrayWS(Parts);
				if (Parts.Num() >= 2)
				{
					UVs.Add(FVector2D(FCString::Atof(*Parts[0]), FCString::Atof(*Parts[1])));
				}
			}
			else if (Trimmed.StartsWith(TEXT("vn ")))
			{
				TArray<FString> Parts;
				Trimmed.Mid(3).ParseIntoArrayWS(Parts);
				if (Parts.Num() >= 3)
				{
					Normals.Add(FVector(FCString::Atof(*Parts[0]), FCString::Atof(*Parts[1]), FCString::Atof(*Parts[2])));
				}
			}
			else if (Trimmed.StartsWith(TEXT("f ")))
			{
				TArray<FString> Parts;
				Trimmed.Mid(2).ParseIntoArrayWS(Parts);
				TArray<ObjFaceVert> FaceVerts;
				for (const FString& P : Parts)
				{
					ObjFaceVert V = { -1, -1, -1 };
					TArray<FString> Indices;
					P.ParseIntoArray(Indices, TEXT("/"));
					if (Indices.Num() >= 1 && !Indices[0].IsEmpty()) V.PosIdx = FCString::Atoi(*Indices[0]) - 1;
					if (Indices.Num() >= 2 && !Indices[1].IsEmpty()) V.UVIdx = FCString::Atoi(*Indices[1]) - 1;
					if (Indices.Num() >= 3 && !Indices[2].IsEmpty()) V.NormIdx = FCString::Atoi(*Indices[2]) - 1;
					FaceVerts.Add(V);
				}
				if (FaceVerts.Num() >= 3)
				{
					Faces.Add(FaceVerts);
				}
			}
		}

		if (Positions.Num() == 0 || Faces.Num() == 0)
		{
			SendErrorResponse(OnComplete, FString::Printf(TEXT("OBJ parse failed: %d positions, %d faces"), Positions.Num(), Faces.Num()));
			return;
		}

		// Create the static mesh using MeshDescription
		FString MeshName = AssetName.IsEmpty() ? FPaths::GetBaseFilename(FilePath) : AssetName;
		FString PackagePath = Destination / MeshName;
		UPackage* Package = CreatePackage(*PackagePath);
		UStaticMesh* StaticMesh = NewObject<UStaticMesh>(Package, FName(*MeshName), RF_Public | RF_Standalone);

		StaticMesh->GetStaticMaterials().Add(FStaticMaterial());

		FMeshDescription MeshDesc;
		FStaticMeshAttributes Attributes(MeshDesc);
		Attributes.Register();

		// Build unique vertices (expand face verts to individual vertices)
		TArray<FVector> ExpandedPositions;
		TArray<FVector2D> ExpandedUVs;
		TArray<FVector> ExpandedNormals;
		TArray<int32> TriangleIndices;

		int32 VertIdx = 0;
		for (const auto& Face : Faces)
		{
			// Triangulate (fan from first vertex)
			for (int32 i = 1; i < Face.Num() - 1; i++)
			{
				const ObjFaceVert* FVerts[3] = { &Face[0], &Face[i], &Face[i+1] };
				for (int32 j = 0; j < 3; j++)
				{
					const ObjFaceVert& FV = *FVerts[j];
					if (FV.PosIdx >= 0 && FV.PosIdx < Positions.Num())
					{
						// OBJ uses right-hand coord, UE5 uses left-hand. Swap Y/Z or negate as needed.
						FVector Pos = Positions[FV.PosIdx];
						// OBJ: X right, Y up, Z towards viewer. UE5: X forward, Y right, Z up.
						// Common conversion: swap Y and Z
						ExpandedPositions.Add(FVector(Pos.X * ImportScale, -Pos.Y * ImportScale, Pos.Z * ImportScale));
					}
					else
					{
						ExpandedPositions.Add(FVector::ZeroVector);
					}

					if (FV.UVIdx >= 0 && FV.UVIdx < UVs.Num())
					{
						FVector2D UV = UVs[FV.UVIdx];
						ExpandedUVs.Add(FVector2D(UV.X, 1.0f - UV.Y)); // Flip V
					}
					else
					{
						ExpandedUVs.Add(FVector2D(0, 0));
					}

					if (FV.NormIdx >= 0 && FV.NormIdx < Normals.Num())
					{
						FVector N = Normals[FV.NormIdx];
						ExpandedNormals.Add(FVector(N.X, -N.Y, N.Z));
					}
					else
					{
						ExpandedNormals.Add(FVector(0, 0, 1));
					}

					TriangleIndices.Add(VertIdx++);
				}
			}
		}

		// Reserve space
		int32 NumVerts = ExpandedPositions.Num();
		int32 NumTris = NumVerts / 3;
		MeshDesc.ReserveNewVertices(NumVerts);
		MeshDesc.ReserveNewVertexInstances(NumVerts);
		MeshDesc.ReserveNewPolygons(NumTris);
		MeshDesc.ReserveNewEdges(NumTris * 3);

		FPolygonGroupID PolyGroup = MeshDesc.CreatePolygonGroup();

		TVertexAttributesRef<FVector3f> VertexPositions = Attributes.GetVertexPositions();
		TVertexInstanceAttributesRef<FVector3f> VertexInstanceNormals = Attributes.GetVertexInstanceNormals();
		TVertexInstanceAttributesRef<FVector2f> VertexInstanceUVs = Attributes.GetVertexInstanceUVs();

		TArray<FVertexID> VertexIDs;
		TArray<FVertexInstanceID> VertexInstanceIDs;
		VertexIDs.SetNum(NumVerts);
		VertexInstanceIDs.SetNum(NumVerts);

		for (int32 i = 0; i < NumVerts; i++)
		{
			VertexIDs[i] = MeshDesc.CreateVertex();
			VertexPositions[VertexIDs[i]] = FVector3f(ExpandedPositions[i]);
			VertexInstanceIDs[i] = MeshDesc.CreateVertexInstance(VertexIDs[i]);
			VertexInstanceNormals[VertexInstanceIDs[i]] = FVector3f(ExpandedNormals[i]);
			VertexInstanceUVs[VertexInstanceIDs[i]] = FVector2f(ExpandedUVs[i]);
		}

		// Create triangles
		for (int32 i = 0; i < NumTris; i++)
		{
			TArray<FVertexInstanceID> TriVerts;
			TriVerts.Add(VertexInstanceIDs[i * 3 + 0]);
			TriVerts.Add(VertexInstanceIDs[i * 3 + 1]);
			TriVerts.Add(VertexInstanceIDs[i * 3 + 2]);
			MeshDesc.CreatePolygon(PolyGroup, TriVerts);
		}

		TArray<const FMeshDescription*> MeshDescriptions;
		MeshDescriptions.Add(&MeshDesc);
		StaticMesh->BuildFromMeshDescriptions(MeshDescriptions);

		// Save
		FAssetRegistryModule::AssetCreated(StaticMesh);
		Package->MarkPackageDirty();
		FString PackageFileName = FPackageName::LongPackageNameToFilename(PackagePath, FPackageName::GetAssetPackageExtension());
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		UPackage::SavePackage(Package, StaticMesh, *PackageFileName, SaveArgs);

		TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
		Result->SetStringField(TEXT("status"), TEXT("ok"));
		Result->SetStringField(TEXT("asset_path"), PackagePath + TEXT(".") + MeshName);
		Result->SetNumberField(TEXT("vertices"), NumVerts);
		Result->SetNumberField(TEXT("triangles"), NumTris);
		Result->SetNumberField(TEXT("original_positions"), Positions.Num());
		Result->SetNumberField(TEXT("original_faces"), Faces.Num());
		Result->SetNumberField(TEXT("import_scale"), ImportScale);
		SendJsonResponse(OnComplete, Result);
	});
	return true;
}

// ============================================================
// Mesh Handlers
// ============================================================

bool FNovaBridgeModule::HandleMeshCreate(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	TSharedPtr<FJsonObject> Body = ParseRequestBody(Request);
	if (!Body)
	{
		SendErrorResponse(OnComplete, TEXT("Invalid JSON body"));
		return true;
	}

	FString Name = Body->GetStringField(TEXT("name"));
	FString Path = Body->HasField(TEXT("path")) ? Body->GetStringField(TEXT("path")) : TEXT("/Game");
	TArray<TSharedPtr<FJsonValue>> Vertices = Body->GetArrayField(TEXT("vertices"));
	TArray<TSharedPtr<FJsonValue>> Triangles = Body->GetArrayField(TEXT("triangles"));

	AsyncTask(ENamedThreads::GameThread, [this, OnComplete, Name, Path, Vertices, Triangles]()
	{
		FString PackagePath = Path / Name;
		UPackage* Package = CreatePackage(*PackagePath);
		UStaticMesh* StaticMesh = NewObject<UStaticMesh>(Package, FName(*Name), RF_Public | RF_Standalone);

		StaticMesh->GetStaticMaterials().Add(FStaticMaterial());

		FMeshDescription MeshDesc;
		FStaticMeshAttributes Attributes(MeshDesc);
		Attributes.Register();

		FMeshDescriptionBuilder Builder;
		Builder.SetMeshDescription(&MeshDesc);
		Builder.EnablePolyGroups();
		Builder.SetNumUVLayers(1);

		// Create polygon group
		FPolygonGroupID PolyGroup = Builder.AppendPolygonGroup();

		// Add vertices
		TArray<FVertexInstanceID> VertexInstances;
		for (const TSharedPtr<FJsonValue>& VertVal : Vertices)
		{
			TSharedPtr<FJsonObject> V = VertVal->AsObject();
			FVector Position(V->GetNumberField(TEXT("x")), V->GetNumberField(TEXT("y")), V->GetNumberField(TEXT("z")));

			FVertexID VertID = Builder.AppendVertex(Position);
			FVertexInstanceID InstanceID = Builder.AppendInstance(VertID);

			// Set UV if provided
			if (V->HasField(TEXT("u")))
			{
				Builder.SetInstanceUV(InstanceID, FVector2D(V->GetNumberField(TEXT("u")), V->GetNumberField(TEXT("v"))), 0);
			}

			// Set normal if provided
			if (V->HasField(TEXT("nx")))
			{
				Builder.SetInstanceNormal(InstanceID, FVector(V->GetNumberField(TEXT("nx")), V->GetNumberField(TEXT("ny")), V->GetNumberField(TEXT("nz"))));
			}

			VertexInstances.Add(InstanceID);
		}

		// Add triangles
		for (const TSharedPtr<FJsonValue>& TriVal : Triangles)
		{
			TSharedPtr<FJsonObject> T = TriVal->AsObject();
			int32 I0 = (int32)T->GetNumberField(TEXT("i0"));
			int32 I1 = (int32)T->GetNumberField(TEXT("i1"));
			int32 I2 = (int32)T->GetNumberField(TEXT("i2"));

			TArray<FVertexInstanceID> TriVerts;
			TriVerts.Add(VertexInstances[I0]);
			TriVerts.Add(VertexInstances[I1]);
			TriVerts.Add(VertexInstances[I2]);
			Builder.AppendTriangle(TriVerts[0], TriVerts[1], TriVerts[2], PolyGroup);
		}

		// Build mesh
		TArray<const FMeshDescription*> MeshDescriptions;
		MeshDescriptions.Add(&MeshDesc);
		StaticMesh->BuildFromMeshDescriptions(MeshDescriptions);

		FAssetRegistryModule::AssetCreated(StaticMesh);
		StaticMesh->MarkPackageDirty();

		TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
		Result->SetStringField(TEXT("status"), TEXT("ok"));
		Result->SetStringField(TEXT("path"), StaticMesh->GetPathName());
		Result->SetNumberField(TEXT("vertices"), Vertices.Num());
		Result->SetNumberField(TEXT("triangles"), Triangles.Num());
		SendJsonResponse(OnComplete, Result);
	});
	return true;
}

bool FNovaBridgeModule::HandleMeshGet(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	FString MeshPath;
	if (Request.QueryParams.Contains(TEXT("path")))
	{
		MeshPath = Request.QueryParams[TEXT("path")];
	}
	else
	{
		TSharedPtr<FJsonObject> Body = ParseRequestBody(Request);
		if (Body) MeshPath = Body->GetStringField(TEXT("path"));
	}

	if (MeshPath.IsEmpty())
	{
		SendErrorResponse(OnComplete, TEXT("Missing 'path' parameter"));
		return true;
	}

	AsyncTask(ENamedThreads::GameThread, [this, OnComplete, MeshPath]()
	{
		UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *MeshPath);
		if (!Mesh)
		{
			SendErrorResponse(OnComplete, TEXT("Mesh not found"), 404);
			return;
		}

		TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
		Result->SetStringField(TEXT("path"), Mesh->GetPathName());
		Result->SetNumberField(TEXT("lods"), Mesh->GetNumLODs());

		if (Mesh->GetNumSourceModels() > 0)
		{
			const FMeshDescription* MeshDesc = Mesh->GetMeshDescription(0);
			if (MeshDesc)
			{
				Result->SetNumberField(TEXT("vertices"), MeshDesc->Vertices().Num());
				Result->SetNumberField(TEXT("triangles"), MeshDesc->Triangles().Num());
				Result->SetNumberField(TEXT("polygons"), MeshDesc->Polygons().Num());
			}
		}

		SendJsonResponse(OnComplete, Result);
	});
	return true;
}

bool FNovaBridgeModule::HandleMeshPrimitive(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	TSharedPtr<FJsonObject> Body = ParseRequestBody(Request);
	if (!Body)
	{
		SendErrorResponse(OnComplete, TEXT("Invalid JSON body"));
		return true;
	}

	FString Type = Body->GetStringField(TEXT("type"));
	FString Name = Body->HasField(TEXT("name")) ? Body->GetStringField(TEXT("name")) : Type;
	FString Path = Body->HasField(TEXT("path")) ? Body->GetStringField(TEXT("path")) : TEXT("/Game");
	double Size = Body->HasField(TEXT("size")) ? Body->GetNumberField(TEXT("size")) : 100.0;

	AsyncTask(ENamedThreads::GameThread, [this, OnComplete, Type, Name, Path, Size]()
	{
		// Generate primitive vertices and triangles
		TArray<FVector> Verts;
		TArray<int32> Tris;

		if (Type == TEXT("cube") || Type == TEXT("box"))
		{
			float S = Size * 0.5f;
			// 8 vertices of a cube
			Verts = {
				{-S, -S, -S}, {S, -S, -S}, {S, S, -S}, {-S, S, -S},
				{-S, -S, S}, {S, -S, S}, {S, S, S}, {-S, S, S}
			};
			Tris = {
				0,2,1, 0,3,2,  // bottom
				4,5,6, 4,6,7,  // top
				0,1,5, 0,5,4,  // front
				2,3,7, 2,7,6,  // back
				0,4,7, 0,7,3,  // left
				1,2,6, 1,6,5   // right
			};
		}
		else if (Type == TEXT("plane"))
		{
			float S = Size * 0.5f;
			Verts = { {-S, -S, 0}, {S, -S, 0}, {S, S, 0}, {-S, S, 0} };
			Tris = { 0, 1, 2, 0, 2, 3 };
		}
		else if (Type == TEXT("sphere"))
		{
			float R = Size * 0.5f;
			int32 Rings = 16;
			int32 Segments = 24;

			// Top pole
			Verts.Add(FVector(0, 0, R));
			// Ring vertices
			for (int32 r = 1; r < Rings; r++)
			{
				float Phi = PI * (float)r / (float)Rings;
				float Z = R * FMath::Cos(Phi);
				float RingR = R * FMath::Sin(Phi);
				for (int32 s = 0; s < Segments; s++)
				{
					float Theta = 2.0f * PI * (float)s / (float)Segments;
					Verts.Add(FVector(RingR * FMath::Cos(Theta), RingR * FMath::Sin(Theta), Z));
				}
			}
			// Bottom pole
			Verts.Add(FVector(0, 0, -R));

			int32 BottomPole = Verts.Num() - 1;
			// Top cap
			for (int32 s = 0; s < Segments; s++)
			{
				Tris.Add(0);
				Tris.Add(1 + s);
				Tris.Add(1 + (s + 1) % Segments);
			}
			// Body quads
			for (int32 r = 0; r < Rings - 2; r++)
			{
				int32 Row0 = 1 + r * Segments;
				int32 Row1 = 1 + (r + 1) * Segments;
				for (int32 s = 0; s < Segments; s++)
				{
					int32 s1 = (s + 1) % Segments;
					Tris.Add(Row0 + s);  Tris.Add(Row1 + s);  Tris.Add(Row1 + s1);
					Tris.Add(Row0 + s);  Tris.Add(Row1 + s1); Tris.Add(Row0 + s1);
				}
			}
			// Bottom cap
			int32 LastRow = 1 + (Rings - 2) * Segments;
			for (int32 s = 0; s < Segments; s++)
			{
				Tris.Add(LastRow + s);
				Tris.Add(BottomPole);
				Tris.Add(LastRow + (s + 1) % Segments);
			}
		}
		else if (Type == TEXT("cylinder"))
		{
			float R = Size * 0.5f;
			float H = Size;
			int32 Segments = 24;

			// Bottom center (0), bottom ring (1..Segments), top center (Segments+1), top ring (Segments+2..2*Segments+1)
			Verts.Add(FVector(0, 0, 0)); // bottom center
			for (int32 s = 0; s < Segments; s++)
			{
				float Theta = 2.0f * PI * (float)s / (float)Segments;
				Verts.Add(FVector(R * FMath::Cos(Theta), R * FMath::Sin(Theta), 0));
			}
			Verts.Add(FVector(0, 0, H)); // top center
			for (int32 s = 0; s < Segments; s++)
			{
				float Theta = 2.0f * PI * (float)s / (float)Segments;
				Verts.Add(FVector(R * FMath::Cos(Theta), R * FMath::Sin(Theta), H));
			}

			int32 TopCenter = Segments + 1;
			// Bottom cap
			for (int32 s = 0; s < Segments; s++)
			{
				Tris.Add(0);
				Tris.Add(1 + (s + 1) % Segments);
				Tris.Add(1 + s);
			}
			// Top cap
			for (int32 s = 0; s < Segments; s++)
			{
				Tris.Add(TopCenter);
				Tris.Add(TopCenter + 1 + s);
				Tris.Add(TopCenter + 1 + (s + 1) % Segments);
			}
			// Side quads
			for (int32 s = 0; s < Segments; s++)
			{
				int32 s1 = (s + 1) % Segments;
				int32 b0 = 1 + s, b1 = 1 + s1;
				int32 t0 = TopCenter + 1 + s, t1 = TopCenter + 1 + s1;
				Tris.Add(b0); Tris.Add(b1); Tris.Add(t1);
				Tris.Add(b0); Tris.Add(t1); Tris.Add(t0);
			}
		}
		else
		{
			SendErrorResponse(OnComplete, FString::Printf(TEXT("Unknown primitive type: %s. Supported: cube, box, plane, sphere, cylinder"), *Type));
			return;
		}

		FString PackagePath = Path / Name;
		UPackage* Package = CreatePackage(*PackagePath);
		UStaticMesh* StaticMesh = NewObject<UStaticMesh>(Package, FName(*Name), RF_Public | RF_Standalone);
		StaticMesh->GetStaticMaterials().Add(FStaticMaterial());

		FMeshDescription MeshDesc;
		FStaticMeshAttributes Attributes(MeshDesc);
		Attributes.Register();

		FMeshDescriptionBuilder Builder;
		Builder.SetMeshDescription(&MeshDesc);
		Builder.EnablePolyGroups();
		Builder.SetNumUVLayers(1);

		FPolygonGroupID PolyGroup = Builder.AppendPolygonGroup();

		TArray<FVertexInstanceID> Instances;
		for (const FVector& V : Verts)
		{
			FVertexID VID = Builder.AppendVertex(V);
			Instances.Add(Builder.AppendInstance(VID));
		}

		for (int32 i = 0; i < Tris.Num(); i += 3)
		{
			Builder.AppendTriangle(Instances[Tris[i]], Instances[Tris[i+1]], Instances[Tris[i+2]], PolyGroup);
		}

		TArray<const FMeshDescription*> MeshDescs;
		MeshDescs.Add(&MeshDesc);
		StaticMesh->BuildFromMeshDescriptions(MeshDescs);

		FAssetRegistryModule::AssetCreated(StaticMesh);
		StaticMesh->MarkPackageDirty();

		TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
		Result->SetStringField(TEXT("status"), TEXT("ok"));
		Result->SetStringField(TEXT("path"), StaticMesh->GetPathName());
		Result->SetStringField(TEXT("type"), Type);
		SendJsonResponse(OnComplete, Result);
	});
	return true;
}

// ============================================================
// Material Handlers
// ============================================================

bool FNovaBridgeModule::HandleMaterialCreate(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	TSharedPtr<FJsonObject> Body = ParseRequestBody(Request);
	if (!Body)
	{
		SendErrorResponse(OnComplete, TEXT("Invalid JSON body"));
		return true;
	}

	FString Name = Body->GetStringField(TEXT("name"));
	FString Path = Body->HasField(TEXT("path")) ? Body->GetStringField(TEXT("path")) : TEXT("/Game");

	AsyncTask(ENamedThreads::GameThread, [this, OnComplete, Body, Name, Path]()
	{
		FString PackagePath = Path / Name;
		UPackage* Package = CreatePackage(*PackagePath);

		UMaterialFactoryNew* Factory = NewObject<UMaterialFactoryNew>();
		UMaterial* Material = Cast<UMaterial>(Factory->FactoryCreateNew(UMaterial::StaticClass(), Package, FName(*Name), RF_Public | RF_Standalone, nullptr, GWarn));

		if (!Material)
		{
			SendErrorResponse(OnComplete, TEXT("Failed to create material"), 500);
			return;
		}

		// Set base color if provided
		if (Body->HasField(TEXT("color")))
		{
			TSharedPtr<FJsonObject> ColorObj = Body->GetObjectField(TEXT("color"));
			float R = ColorObj->GetNumberField(TEXT("r"));
			float G = ColorObj->GetNumberField(TEXT("g"));
			float B = ColorObj->GetNumberField(TEXT("b"));
			float A = ColorObj->HasField(TEXT("a")) ? ColorObj->GetNumberField(TEXT("a")) : 1.0f;

			UMaterialExpressionConstant4Vector* ColorExpr = NewObject<UMaterialExpressionConstant4Vector>(Material);
			ColorExpr->Constant = FLinearColor(R, G, B, A);
			Material->GetExpressionCollection().AddExpression(ColorExpr);
			Material->GetEditorOnlyData()->BaseColor.Connect(0, ColorExpr);
		}

		Material->PreEditChange(nullptr);
		Material->PostEditChange();

		FAssetRegistryModule::AssetCreated(Material);
		Material->MarkPackageDirty();

		TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
		Result->SetStringField(TEXT("status"), TEXT("ok"));
		Result->SetStringField(TEXT("path"), Material->GetPathName());
		SendJsonResponse(OnComplete, Result);
	});
	return true;
}

bool FNovaBridgeModule::HandleMaterialSetParam(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	TSharedPtr<FJsonObject> Body = ParseRequestBody(Request);
	if (!Body)
	{
		SendErrorResponse(OnComplete, TEXT("Invalid JSON body"));
		return true;
	}

	FString MaterialPath = Body->GetStringField(TEXT("path"));
	FString ParamName = Body->GetStringField(TEXT("param"));
	FString ParamType = Body->HasField(TEXT("type")) ? Body->GetStringField(TEXT("type")) : TEXT("scalar");

	AsyncTask(ENamedThreads::GameThread, [this, OnComplete, Body, MaterialPath, ParamName, ParamType]()
	{
		UMaterialInstanceConstant* MatInst = LoadObject<UMaterialInstanceConstant>(nullptr, *MaterialPath);
		if (!MatInst)
		{
			SendErrorResponse(OnComplete, TEXT("Material instance not found"), 404);
			return;
		}

		if (ParamType == TEXT("scalar"))
		{
			float Value = Body->GetNumberField(TEXT("value"));
			MatInst->SetScalarParameterValueEditorOnly(FName(*ParamName), Value);
		}
		else if (ParamType == TEXT("vector"))
		{
			TSharedPtr<FJsonObject> V = Body->GetObjectField(TEXT("value"));
			FLinearColor Color(V->GetNumberField(TEXT("r")), V->GetNumberField(TEXT("g")), V->GetNumberField(TEXT("b")), V->HasField(TEXT("a")) ? V->GetNumberField(TEXT("a")) : 1.0f);
			MatInst->SetVectorParameterValueEditorOnly(FName(*ParamName), Color);
		}

		MatInst->PostEditChange();
		MatInst->MarkPackageDirty();

		SendOkResponse(OnComplete);
	});
	return true;
}

bool FNovaBridgeModule::HandleMaterialGet(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	FString MaterialPath;
	if (Request.QueryParams.Contains(TEXT("path")))
	{
		MaterialPath = Request.QueryParams[TEXT("path")];
	}
	else
	{
		TSharedPtr<FJsonObject> Body = ParseRequestBody(Request);
		if (Body) MaterialPath = Body->GetStringField(TEXT("path"));
	}

	if (MaterialPath.IsEmpty())
	{
		SendErrorResponse(OnComplete, TEXT("Missing 'path' parameter"));
		return true;
	}

	AsyncTask(ENamedThreads::GameThread, [this, OnComplete, MaterialPath]()
	{
		UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, *MaterialPath);
		if (!Material)
		{
			SendErrorResponse(OnComplete, TEXT("Material not found"), 404);
			return;
		}

		TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
		Result->SetStringField(TEXT("path"), Material->GetPathName());
		Result->SetStringField(TEXT("class"), Material->GetClass()->GetName());

		// Get parameters from material instance
		UMaterialInstanceConstant* MatInst = Cast<UMaterialInstanceConstant>(Material);
		if (MatInst)
		{
			TSharedPtr<FJsonObject> Params = MakeShareable(new FJsonObject);

			TArray<FMaterialParameterInfo> ParamInfos;
			TArray<FGuid> ParamIds;

			MatInst->GetAllScalarParameterInfo(ParamInfos, ParamIds);
			for (const FMaterialParameterInfo& Info : ParamInfos)
			{
				float Value;
				if (MatInst->GetScalarParameterValue(Info, Value))
				{
					Params->SetNumberField(Info.Name.ToString(), Value);
				}
			}

			ParamInfos.Empty();
			ParamIds.Empty();
			MatInst->GetAllVectorParameterInfo(ParamInfos, ParamIds);
			for (const FMaterialParameterInfo& Info : ParamInfos)
			{
				FLinearColor Value;
				if (MatInst->GetVectorParameterValue(Info, Value))
				{
					TSharedPtr<FJsonObject> ColorObj = MakeShareable(new FJsonObject);
					ColorObj->SetNumberField(TEXT("r"), Value.R);
					ColorObj->SetNumberField(TEXT("g"), Value.G);
					ColorObj->SetNumberField(TEXT("b"), Value.B);
					ColorObj->SetNumberField(TEXT("a"), Value.A);
					Params->SetObjectField(Info.Name.ToString(), ColorObj);
				}
			}

			Result->SetObjectField(TEXT("parameters"), Params);
		}

		SendJsonResponse(OnComplete, Result);
	});
	return true;
}

bool FNovaBridgeModule::HandleMaterialCreateInstance(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	TSharedPtr<FJsonObject> Body = ParseRequestBody(Request);
	if (!Body)
	{
		SendErrorResponse(OnComplete, TEXT("Invalid JSON body"));
		return true;
	}

	FString ParentPath = Body->GetStringField(TEXT("parent"));
	FString Name = Body->GetStringField(TEXT("name"));
	FString Path = Body->HasField(TEXT("path")) ? Body->GetStringField(TEXT("path")) : TEXT("/Game");

	AsyncTask(ENamedThreads::GameThread, [this, OnComplete, ParentPath, Name, Path]()
	{
		UMaterialInterface* Parent = LoadObject<UMaterialInterface>(nullptr, *ParentPath);
		if (!Parent)
		{
			SendErrorResponse(OnComplete, TEXT("Parent material not found"), 404);
			return;
		}

		FString PackagePath = Path / Name;
		UPackage* Package = CreatePackage(*PackagePath);

		UMaterialInstanceConstantFactoryNew* Factory = NewObject<UMaterialInstanceConstantFactoryNew>();
		Factory->InitialParent = Parent;
		UMaterialInstanceConstant* MatInst = Cast<UMaterialInstanceConstant>(
			Factory->FactoryCreateNew(UMaterialInstanceConstant::StaticClass(), Package, FName(*Name), RF_Public | RF_Standalone, nullptr, GWarn));

		if (!MatInst)
		{
			SendErrorResponse(OnComplete, TEXT("Failed to create material instance"), 500);
			return;
		}

		FAssetRegistryModule::AssetCreated(MatInst);
		MatInst->MarkPackageDirty();

		TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
		Result->SetStringField(TEXT("status"), TEXT("ok"));
		Result->SetStringField(TEXT("path"), MatInst->GetPathName());
		SendJsonResponse(OnComplete, Result);
	});
	return true;
}

// ============================================================
// Viewport Handlers (Offscreen SceneCapture2D)
// ============================================================

void FNovaBridgeModule::EnsureCaptureSetup()
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
		return;

	// Already valid for this world?
	if (CaptureActor.IsValid() && CaptureActor->GetWorld() == World && RenderTarget.IsValid())
	{
		return;
	}

	// If world changed, force rebind.
	if (CaptureActor.IsValid() && CaptureActor->GetWorld() != World)
	{
		CaptureActor.Reset();
	}

	// Reattach to an existing capture actor if one already exists in this level.
	if (!CaptureActor.IsValid())
	{
		for (TActorIterator<ASceneCapture2D> It(World); It; ++It)
		{
			ASceneCapture2D* Existing = *It;
			if (!Existing)
			{
				continue;
			}
			if (Existing->GetActorLabel() == TEXT("NovaBridge_SceneCapture") || Existing->GetName().Contains(TEXT("NovaBridge_SceneCapture")))
			{
				CaptureActor = Existing;
				break;
			}
		}
	}

	if (!RenderTarget.IsValid())
	{
		UTextureRenderTarget2D* RT = NewObject<UTextureRenderTarget2D>(GetTransientPackage(), NAME_None, RF_Transient);
		RT->InitAutoFormat(CaptureWidth, CaptureHeight);
		RT->UpdateResourceImmediate(true);
		RenderTarget = RT;
	}

	// Spawn scene capture actor
	if (!CaptureActor.IsValid())
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ASceneCapture2D* Capture = World->SpawnActor<ASceneCapture2D>(FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
		if (!Capture)
		{
			return;
		}

		Capture->SetActorLabel(TEXT("NovaBridge_SceneCapture"));
		Capture->SetActorHiddenInGame(true);
		CaptureActor = Capture;
	}

	ASceneCapture2D* Capture = CaptureActor.Get();
	UTextureRenderTarget2D* RT = RenderTarget.Get();
	if (Capture && RT)
	{
		USceneCaptureComponent2D* CaptureComp = Capture->GetCaptureComponent2D();
		CaptureComp->TextureTarget = RT;
		CaptureComp->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
		CaptureComp->bCaptureEveryFrame = false;
		CaptureComp->bCaptureOnMovement = false;
		CaptureComp->FOVAngle = CameraFOV;

		// Position at default camera location
		Capture->SetActorLocation(CameraLocation);
		Capture->SetActorRotation(CameraRotation);

		UE_LOG(LogNovaBridge, Log, TEXT("Scene capture ready: %dx%d"), CaptureWidth, CaptureHeight);
	}
}

void FNovaBridgeModule::CleanupCapture()
{
	if (CaptureActor.IsValid())
	{
		CaptureActor->Destroy();
		CaptureActor.Reset();
	}
	RenderTarget.Reset();
}

void FNovaBridgeModule::EnsureStreamCaptureSetup()
{
	if (StreamCaptureActor.IsValid() && StreamRenderTarget.IsValid())
	{
		return;
	}

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		return;
	}

	UTextureRenderTarget2D* RT = NewObject<UTextureRenderTarget2D>(GetTransientPackage(), NAME_None, RF_Transient);
	RT->InitAutoFormat(StreamWidth, StreamHeight);
	RT->UpdateResourceImmediate(true);
	StreamRenderTarget = RT;

	FActorSpawnParameters SpawnParams;
	SpawnParams.Name = FName(TEXT("NovaBridge_StreamCapture"));
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ASceneCapture2D* Capture = World->SpawnActor<ASceneCapture2D>(FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
	if (Capture)
	{
		Capture->SetActorLabel(TEXT("NovaBridge_StreamCapture"));
		Capture->SetActorHiddenInGame(true);
		USceneCaptureComponent2D* CaptureComp = Capture->GetCaptureComponent2D();
		CaptureComp->TextureTarget = RT;
		CaptureComp->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
		CaptureComp->bCaptureEveryFrame = false;
		CaptureComp->bCaptureOnMovement = false;
		CaptureComp->FOVAngle = CameraFOV;
		Capture->SetActorLocation(CameraLocation);
		Capture->SetActorRotation(CameraRotation);
		StreamCaptureActor = Capture;
	}
}

void FNovaBridgeModule::CleanupStreamCapture()
{
	if (StreamCaptureActor.IsValid())
	{
		StreamCaptureActor->Destroy();
		StreamCaptureActor.Reset();
	}
	StreamRenderTarget.Reset();
}

bool FNovaBridgeModule::HandleViewportScreenshot(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	// Parse optional width/height from query params
	int32 ReqWidth = 0, ReqHeight = 0;
	bool bRawPng = false;
	if (Request.QueryParams.Contains(TEXT("width")))
	{
		ReqWidth = FCString::Atoi(*Request.QueryParams[TEXT("width")]);
	}
	if (Request.QueryParams.Contains(TEXT("height")))
	{
		ReqHeight = FCString::Atoi(*Request.QueryParams[TEXT("height")]);
	}
	if (Request.QueryParams.Contains(TEXT("format")))
	{
		const FString Format = Request.QueryParams[TEXT("format")].ToLower();
		bRawPng = (Format == TEXT("raw") || Format == TEXT("png"));
	}

	AsyncTask(ENamedThreads::GameThread, [this, OnComplete, ReqWidth, ReqHeight, bRawPng]()
	{
		if (!GEditor)
		{
			SendErrorResponse(OnComplete, TEXT("No editor"), 500);
			return;
		}

		// Resize render target if requested
		if (ReqWidth > 0 && ReqHeight > 0 && (ReqWidth != CaptureWidth || ReqHeight != CaptureHeight))
		{
			CaptureWidth = FMath::Clamp(ReqWidth, 64, 3840);
			CaptureHeight = FMath::Clamp(ReqHeight, 64, 2160);
			CleanupCapture(); // Force re-creation at new size
		}

		EnsureCaptureSetup();

		if (!CaptureActor.IsValid() || !RenderTarget.IsValid())
		{
			SendErrorResponse(OnComplete, TEXT("Failed to create scene capture"), 500);
			return;
		}

		// Update capture component position to current camera state
		USceneCaptureComponent2D* CaptureComp = CaptureActor->GetCaptureComponent2D();
		CaptureActor->SetActorLocation(CameraLocation);
		CaptureActor->SetActorRotation(CameraRotation);
		CaptureComp->FOVAngle = CameraFOV;

		// Capture the scene
		CaptureComp->CaptureScene();

		// Read pixels from render target
		FTextureRenderTargetResource* RTResource = RenderTarget->GameThread_GetRenderTargetResource();
		if (!RTResource)
		{
			SendErrorResponse(OnComplete, TEXT("No render target resource"), 500);
			return;
		}

		TArray<FColor> Bitmap;
		bool bSuccess = RTResource->ReadPixels(Bitmap);
		if (!bSuccess || Bitmap.Num() == 0)
		{
			SendErrorResponse(OnComplete, TEXT("Failed to read render target pixels"), 500);
			return;
		}

		int32 Width = CaptureWidth;
		int32 Height = CaptureHeight;

		// Encode as PNG
		IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));
		TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);
		ImageWrapper->SetRaw(Bitmap.GetData(), Bitmap.Num() * sizeof(FColor), Width, Height, ERGBFormat::BGRA, 8);

		TArray64<uint8> PngData = ImageWrapper->GetCompressed(0);
		if (PngData.Num() > 0)
		{
			if (bRawPng)
			{
				TArray<uint8> RawPng;
				RawPng.Append(PngData.GetData(), static_cast<int32>(PngData.Num()));

				TUniquePtr<FHttpServerResponse> Response = FHttpServerResponse::Create(MoveTemp(RawPng), TEXT("image/png"));
				Response->Code = EHttpServerResponseCodes::Ok;
				Response->Headers.FindOrAdd(TEXT("X-NovaBridge-Width")).Add(FString::FromInt(Width));
				Response->Headers.FindOrAdd(TEXT("X-NovaBridge-Height")).Add(FString::FromInt(Height));
				AddCorsHeaders(Response);
				OnComplete(MoveTemp(Response));
				return;
			}

			FString Base64 = FBase64::Encode(PngData.GetData(), PngData.Num());

			TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
			Result->SetStringField(TEXT("image"), Base64);
			Result->SetNumberField(TEXT("width"), Width);
			Result->SetNumberField(TEXT("height"), Height);
			Result->SetStringField(TEXT("format"), TEXT("png"));
			SendJsonResponse(OnComplete, Result);
		}
		else
		{
			SendErrorResponse(OnComplete, TEXT("Failed to encode PNG"), 500);
		}
	});
	return true;
}

bool FNovaBridgeModule::HandleViewportSetCamera(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	TSharedPtr<FJsonObject> Body = ParseRequestBody(Request);
	if (!Body)
	{
		SendErrorResponse(OnComplete, TEXT("Invalid JSON body"));
		return true;
	}

	AsyncTask(ENamedThreads::GameThread, [this, OnComplete, Body]()
	{
		TArray<FString> UnknownShowFlags;

		if (Body->HasField(TEXT("location")))
		{
			TSharedPtr<FJsonObject> Loc = Body->GetObjectField(TEXT("location"));
			CameraLocation = FVector(
				Loc->GetNumberField(TEXT("x")),
				Loc->GetNumberField(TEXT("y")),
				Loc->GetNumberField(TEXT("z"))
			);
		}

		if (Body->HasField(TEXT("rotation")))
		{
			TSharedPtr<FJsonObject> Rot = Body->GetObjectField(TEXT("rotation"));
			CameraRotation = FRotator(
				Rot->GetNumberField(TEXT("pitch")),
				Rot->GetNumberField(TEXT("yaw")),
				Rot->GetNumberField(TEXT("roll"))
			);
		}

		if (Body->HasField(TEXT("fov")))
		{
			CameraFOV = Body->GetNumberField(TEXT("fov"));
		}

		if (Body->HasTypedField<EJson::Object>(TEXT("show_flags")))
		{
			EnsureCaptureSetup();
			if (CaptureActor.IsValid())
			{
				USceneCaptureComponent2D* CaptureComp = CaptureActor->GetCaptureComponent2D();
				TSharedPtr<FJsonObject> ShowFlagsObj = Body->GetObjectField(TEXT("show_flags"));
				for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : ShowFlagsObj->Values)
				{
					bool bEnabled = false;
					if (!Pair.Value.IsValid() || !Pair.Value->TryGetBool(bEnabled))
					{
						UnknownShowFlags.Add(Pair.Key);
						continue;
					}

					const int32 FlagIndex = FEngineShowFlags::FindIndexByName(*Pair.Key);
					if (FlagIndex < 0)
					{
						UnknownShowFlags.Add(Pair.Key);
						continue;
					}

					CaptureComp->ShowFlags.SetSingleFlag(static_cast<uint32>(FlagIndex), bEnabled);
				}
			}
		}

		// Update capture actor position if it exists
		if (CaptureActor.IsValid())
		{
			CaptureActor->SetActorLocation(CameraLocation);
			CaptureActor->SetActorRotation(CameraRotation);
			USceneCaptureComponent2D* CaptureComp = CaptureActor->GetCaptureComponent2D();
			CaptureComp->FOVAngle = CameraFOV;
		}

		// Return current camera state
		TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
		Result->SetStringField(TEXT("status"), TEXT("ok"));

		TSharedPtr<FJsonObject> LocObj = MakeShareable(new FJsonObject);
		LocObj->SetNumberField(TEXT("x"), CameraLocation.X);
		LocObj->SetNumberField(TEXT("y"), CameraLocation.Y);
		LocObj->SetNumberField(TEXT("z"), CameraLocation.Z);
		Result->SetObjectField(TEXT("location"), LocObj);

		TSharedPtr<FJsonObject> RotObj = MakeShareable(new FJsonObject);
		RotObj->SetNumberField(TEXT("pitch"), CameraRotation.Pitch);
		RotObj->SetNumberField(TEXT("yaw"), CameraRotation.Yaw);
		RotObj->SetNumberField(TEXT("roll"), CameraRotation.Roll);
		Result->SetObjectField(TEXT("rotation"), RotObj);

		Result->SetNumberField(TEXT("fov"), CameraFOV);
		if (UnknownShowFlags.Num() > 0)
		{
			TArray<TSharedPtr<FJsonValue>> Unknown;
			for (const FString& FlagName : UnknownShowFlags)
			{
				Unknown.Add(MakeShareable(new FJsonValueString(FlagName)));
			}
			Result->SetArrayField(TEXT("unknown_show_flags"), Unknown);
		}
		SendJsonResponse(OnComplete, Result);
	});
	return true;
}

bool FNovaBridgeModule::HandleViewportGetCamera(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	AsyncTask(ENamedThreads::GameThread, [this, OnComplete]()
	{
		TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);

		TSharedPtr<FJsonObject> LocObj = MakeShareable(new FJsonObject);
		LocObj->SetNumberField(TEXT("x"), CameraLocation.X);
		LocObj->SetNumberField(TEXT("y"), CameraLocation.Y);
		LocObj->SetNumberField(TEXT("z"), CameraLocation.Z);
		Result->SetObjectField(TEXT("location"), LocObj);

		TSharedPtr<FJsonObject> RotObj = MakeShareable(new FJsonObject);
		RotObj->SetNumberField(TEXT("pitch"), CameraRotation.Pitch);
		RotObj->SetNumberField(TEXT("yaw"), CameraRotation.Yaw);
		RotObj->SetNumberField(TEXT("roll"), CameraRotation.Roll);
		Result->SetObjectField(TEXT("rotation"), RotObj);

		Result->SetNumberField(TEXT("fov"), CameraFOV);
		Result->SetNumberField(TEXT("width"), CaptureWidth);
		Result->SetNumberField(TEXT("height"), CaptureHeight);
		SendJsonResponse(OnComplete, Result);
	});
	return true;
}

// ============================================================
// Blueprint Handlers
// ============================================================

bool FNovaBridgeModule::HandleBlueprintCreate(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	TSharedPtr<FJsonObject> Body = ParseRequestBody(Request);
	if (!Body)
	{
		SendErrorResponse(OnComplete, TEXT("Invalid JSON body"));
		return true;
	}

	FString Name = Body->GetStringField(TEXT("name"));
	FString Path = Body->HasField(TEXT("path")) ? Body->GetStringField(TEXT("path")) : TEXT("/Game");
	FString ParentClass = Body->HasField(TEXT("parent_class")) ? Body->GetStringField(TEXT("parent_class")) : TEXT("Actor");

	AsyncTask(ENamedThreads::GameThread, [this, OnComplete, Name, Path, ParentClass]()
	{
		UClass* Parent = AActor::StaticClass();
		if (ParentClass != TEXT("Actor"))
		{
			UClass* Found = FindObject<UClass>(nullptr, *ParentClass);
			if (!Found) Found = LoadClass<UObject>(nullptr, *ParentClass);
			if (Found) Parent = Found;
		}

		FString PackagePath = Path / Name;
		UPackage* Package = CreatePackage(*PackagePath);

		UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(Parent, Package, FName(*Name), BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
		if (!Blueprint)
		{
			SendErrorResponse(OnComplete, TEXT("Failed to create blueprint"), 500);
			return;
		}

		FAssetRegistryModule::AssetCreated(Blueprint);
		Blueprint->MarkPackageDirty();

		TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
		Result->SetStringField(TEXT("status"), TEXT("ok"));
		Result->SetStringField(TEXT("path"), Blueprint->GetPathName());
		Result->SetStringField(TEXT("parent_class"), Parent->GetName());
		SendJsonResponse(OnComplete, Result);
	});
	return true;
}

bool FNovaBridgeModule::HandleBlueprintAddComponent(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	TSharedPtr<FJsonObject> Body = ParseRequestBody(Request);
	if (!Body)
	{
		SendErrorResponse(OnComplete, TEXT("Invalid JSON body"));
		return true;
	}

	FString BlueprintPath = Body->GetStringField(TEXT("blueprint"));
	FString ComponentClass = Body->GetStringField(TEXT("component_class"));
	FString ComponentName = Body->HasField(TEXT("component_name")) ? Body->GetStringField(TEXT("component_name")) : TEXT("");

	AsyncTask(ENamedThreads::GameThread, [this, OnComplete, BlueprintPath, ComponentClass, ComponentName]()
	{
		UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *BlueprintPath);
		if (!Blueprint)
		{
			SendErrorResponse(OnComplete, TEXT("Blueprint not found"), 404);
			return;
		}

		UClass* CompClass = FindObject<UClass>(nullptr, *ComponentClass);
		if (!CompClass) CompClass = LoadClass<UActorComponent>(nullptr, *ComponentClass);
		if (!CompClass)
		{
			// Common shortcuts
			if (ComponentClass == TEXT("StaticMeshComponent")) CompClass = UStaticMeshComponent::StaticClass();
		}

		if (!CompClass)
		{
			SendErrorResponse(OnComplete, FString::Printf(TEXT("Component class not found: %s"), *ComponentClass));
			return;
		}

		FName CompName = ComponentName.IsEmpty() ? FName(*CompClass->GetName()) : FName(*ComponentName);
		USCS_Node* Node = Blueprint->SimpleConstructionScript->CreateNode(CompClass, CompName);
		Blueprint->SimpleConstructionScript->AddNode(Node);

		FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);

		TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
		Result->SetStringField(TEXT("status"), TEXT("ok"));
		Result->SetStringField(TEXT("component"), CompName.ToString());
		SendJsonResponse(OnComplete, Result);
	});
	return true;
}

bool FNovaBridgeModule::HandleBlueprintCompile(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	TSharedPtr<FJsonObject> Body = ParseRequestBody(Request);
	if (!Body)
	{
		SendErrorResponse(OnComplete, TEXT("Invalid JSON body"));
		return true;
	}

	FString BlueprintPath = Body->GetStringField(TEXT("blueprint"));

	AsyncTask(ENamedThreads::GameThread, [this, OnComplete, BlueprintPath]()
	{
		UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *BlueprintPath);
		if (!Blueprint)
		{
			SendErrorResponse(OnComplete, TEXT("Blueprint not found"), 404);
			return;
		}

		FKismetEditorUtilities::CompileBlueprint(Blueprint);

		TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
		Result->SetStringField(TEXT("status"), TEXT("ok"));
		Result->SetStringField(TEXT("path"), Blueprint->GetPathName());
		Result->SetBoolField(TEXT("compiled"), Blueprint->Status == BS_UpToDate);
		SendJsonResponse(OnComplete, Result);
	});
	return true;
}

// ============================================================
// Build Handlers
// ============================================================

bool FNovaBridgeModule::HandleBuildLighting(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	AsyncTask(ENamedThreads::GameThread, [this, OnComplete]()
	{
		if (!GEditor)
		{
			SendErrorResponse(OnComplete, TEXT("No editor"), 500);
			return;
		}

		GEditor->Exec(GEditor->GetEditorWorldContext().World(), TEXT("BUILD LIGHTING"));

		SendOkResponse(OnComplete);
	});
	return true;
}

bool FNovaBridgeModule::HandleExecCommand(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	TSharedPtr<FJsonObject> Body = ParseRequestBody(Request);
	if (!Body)
	{
		SendErrorResponse(OnComplete, TEXT("Invalid JSON body"));
		return true;
	}

	FString Command = Body->GetStringField(TEXT("command"));

	AsyncTask(ENamedThreads::GameThread, [this, OnComplete, Command]()
	{
		if (!GEditor)
		{
			SendErrorResponse(OnComplete, TEXT("No editor"), 500);
			return;
		}

		GEditor->Exec(GEditor->GetEditorWorldContext().World(), *Command);

		TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
		Result->SetStringField(TEXT("status"), TEXT("ok"));
		Result->SetStringField(TEXT("command"), Command);
		SendJsonResponse(OnComplete, Result);
	});
	return true;
}

// ============================================================
// Stream Handlers
// ============================================================

bool FNovaBridgeModule::HandleStreamStart(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	(void)Request;
	bStreamActive = true;
	StartStreamTicker();

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetStringField(TEXT("status"), TEXT("ok"));
	Result->SetBoolField(TEXT("active"), bStreamActive && StreamTickHandle.IsValid());
	Result->SetNumberField(TEXT("clients"), WsClients.Num());
	Result->SetNumberField(TEXT("fps"), StreamFps);
	Result->SetNumberField(TEXT("width"), StreamWidth);
	Result->SetNumberField(TEXT("height"), StreamHeight);
	Result->SetNumberField(TEXT("quality"), StreamQuality);
	Result->SetNumberField(TEXT("ws_port"), WsPort);
	Result->SetStringField(TEXT("ws_url"), FString::Printf(TEXT("ws://localhost:%d"), WsPort));
#if !NOVABRIDGE_WITH_WEBSOCKET_NETWORKING
	Result->SetStringField(TEXT("warning"), TEXT("WebSocketNetworking module unavailable in this UE build."));
#endif
	SendJsonResponse(OnComplete, Result);
	return true;
}

bool FNovaBridgeModule::HandleStreamStop(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	(void)Request;
	bStreamActive = false;
	StopStreamTicker();

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetStringField(TEXT("status"), TEXT("ok"));
	Result->SetBoolField(TEXT("active"), false);
	Result->SetNumberField(TEXT("clients"), WsClients.Num());
	SendJsonResponse(OnComplete, Result);
	return true;
}

bool FNovaBridgeModule::HandleStreamConfig(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	TSharedPtr<FJsonObject> Body = ParseRequestBody(Request);
	if (!Body)
	{
		SendErrorResponse(OnComplete, TEXT("Invalid JSON body"));
		return true;
	}

	double Value = 0.0;
	bool bResized = false;

	if (Body->TryGetNumberField(TEXT("fps"), Value))
	{
		StreamFps = FMath::Clamp(static_cast<int32>(Value), 1, 30);
	}
	if (Body->TryGetNumberField(TEXT("width"), Value))
	{
		const int32 NewWidth = FMath::Clamp(static_cast<int32>(Value), 64, 1920);
		bResized = bResized || (NewWidth != StreamWidth);
		StreamWidth = NewWidth;
	}
	if (Body->TryGetNumberField(TEXT("height"), Value))
	{
		const int32 NewHeight = FMath::Clamp(static_cast<int32>(Value), 64, 1080);
		bResized = bResized || (NewHeight != StreamHeight);
		StreamHeight = NewHeight;
	}
	if (Body->TryGetNumberField(TEXT("quality"), Value))
	{
		StreamQuality = FMath::Clamp(static_cast<int32>(Value), 1, 100);
	}

	if (bResized)
	{
		AsyncTask(ENamedThreads::GameThread, [this]()
		{
			CleanupStreamCapture();
		});
	}

	if (bStreamActive)
	{
		StopStreamTicker();
		StartStreamTicker();
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetStringField(TEXT("status"), TEXT("ok"));
	Result->SetBoolField(TEXT("active"), bStreamActive && StreamTickHandle.IsValid());
	Result->SetNumberField(TEXT("clients"), WsClients.Num());
	Result->SetNumberField(TEXT("fps"), StreamFps);
	Result->SetNumberField(TEXT("width"), StreamWidth);
	Result->SetNumberField(TEXT("height"), StreamHeight);
	Result->SetNumberField(TEXT("quality"), StreamQuality);
	Result->SetNumberField(TEXT("ws_port"), WsPort);
	Result->SetStringField(TEXT("ws_url"), FString::Printf(TEXT("ws://localhost:%d"), WsPort));
	SendJsonResponse(OnComplete, Result);
	return true;
}

bool FNovaBridgeModule::HandleStreamStatus(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	(void)Request;
	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("active"), bStreamActive && StreamTickHandle.IsValid());
	Result->SetNumberField(TEXT("clients"), WsClients.Num());
	Result->SetNumberField(TEXT("fps"), StreamFps);
	Result->SetNumberField(TEXT("width"), StreamWidth);
	Result->SetNumberField(TEXT("height"), StreamHeight);
	Result->SetNumberField(TEXT("quality"), StreamQuality);
	Result->SetNumberField(TEXT("ws_port"), WsPort);
	Result->SetStringField(TEXT("ws_url"), FString::Printf(TEXT("ws://localhost:%d"), WsPort));
	SendJsonResponse(OnComplete, Result);
	return true;
}

// ============================================================
// PCG Handlers
// ============================================================

bool FNovaBridgeModule::HandlePcgListGraphs(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	(void)Request;
#if NOVABRIDGE_WITH_PCG
	AsyncTask(ENamedThreads::GameThread, [this, OnComplete]()
	{
		FAssetRegistryModule& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		TArray<FAssetData> Assets;
		AssetRegistry.Get().GetAssetsByClass(UPCGGraph::StaticClass()->GetClassPathName(), Assets, true);

		TArray<TSharedPtr<FJsonValue>> Graphs;
		for (const FAssetData& Asset : Assets)
		{
			TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
			Obj->SetStringField(TEXT("name"), Asset.AssetName.ToString());
			Obj->SetStringField(TEXT("path"), Asset.GetObjectPathString());
			Graphs.Add(MakeShareable(new FJsonValueObject(Obj)));
		}

		TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
		Result->SetArrayField(TEXT("graphs"), Graphs);
		Result->SetNumberField(TEXT("count"), Graphs.Num());
		SendJsonResponse(OnComplete, Result);
	});
#else
	SendErrorResponse(OnComplete, TEXT("PCG module is not available in this build"), 501);
#endif
	return true;
}

bool FNovaBridgeModule::HandlePcgCreateVolume(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	TSharedPtr<FJsonObject> Body = ParseRequestBody(Request);
	if (!Body)
	{
		SendErrorResponse(OnComplete, TEXT("Invalid JSON body"));
		return true;
	}

#if NOVABRIDGE_WITH_PCG
	const FString GraphPath = Body->GetStringField(TEXT("graph_path"));
	const FString Label = Body->HasField(TEXT("label")) ? Body->GetStringField(TEXT("label")) : TEXT("NovaBridge_PCGVolume");
	const double X = Body->HasField(TEXT("x")) ? Body->GetNumberField(TEXT("x")) : 0.0;
	const double Y = Body->HasField(TEXT("y")) ? Body->GetNumberField(TEXT("y")) : 0.0;
	const double Z = Body->HasField(TEXT("z")) ? Body->GetNumberField(TEXT("z")) : 0.0;
	const double SizeX = Body->HasField(TEXT("size_x")) ? Body->GetNumberField(TEXT("size_x")) : 5000.0;
	const double SizeY = Body->HasField(TEXT("size_y")) ? Body->GetNumberField(TEXT("size_y")) : 5000.0;
	const double SizeZ = Body->HasField(TEXT("size_z")) ? Body->GetNumberField(TEXT("size_z")) : 1000.0;

	AsyncTask(ENamedThreads::GameThread, [this, OnComplete, GraphPath, Label, X, Y, Z, SizeX, SizeY, SizeZ]()
	{
		UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
		if (!World)
		{
			SendErrorResponse(OnComplete, TEXT("No world"), 500);
			return;
		}

		APCGVolume* Volume = World->SpawnActor<APCGVolume>(FVector(X, Y, Z), FRotator::ZeroRotator);
		if (!Volume)
		{
			SendErrorResponse(OnComplete, TEXT("Failed to spawn PCG volume"), 500);
			return;
		}

		Volume->SetActorLabel(Label);
		Volume->SetActorScale3D(FVector(SizeX / 200.0, SizeY / 200.0, SizeZ / 200.0));

		UPCGGraph* Graph = LoadObject<UPCGGraph>(nullptr, *GraphPath);
		if (!Graph)
		{
			Volume->Destroy();
			SendErrorResponse(OnComplete, FString::Printf(TEXT("PCG graph not found: %s"), *GraphPath), 404);
			return;
		}

		UPCGComponent* Component = Volume->PCGComponent ? Volume->PCGComponent : Volume->FindComponentByClass<UPCGComponent>();
		if (!Component)
		{
			Volume->Destroy();
			SendErrorResponse(OnComplete, TEXT("Spawned volume has no PCGComponent"), 500);
			return;
		}

		Component->SetGraph(Graph);
		Component->bActivated = true;
		Component->Generate(false);

		TSharedPtr<FJsonObject> Result = ActorToJson(Volume);
		Result->SetStringField(TEXT("graph_path"), Graph->GetPathName());
		Result->SetBoolField(TEXT("generation_triggered"), true);
		SendJsonResponse(OnComplete, Result);
	});
#else
	SendErrorResponse(OnComplete, TEXT("PCG module is not available in this build"), 501);
#endif
	return true;
}

bool FNovaBridgeModule::HandlePcgGenerate(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	TSharedPtr<FJsonObject> Body = ParseRequestBody(Request);
	if (!Body)
	{
		SendErrorResponse(OnComplete, TEXT("Invalid JSON body"));
		return true;
	}

#if NOVABRIDGE_WITH_PCG
	const FString ActorName = Body->GetStringField(TEXT("actor_name"));
	const bool bForce = !Body->HasField(TEXT("force_regenerate")) || Body->GetBoolField(TEXT("force_regenerate"));
	const int32 Seed = Body->HasField(TEXT("seed")) ? static_cast<int32>(Body->GetNumberField(TEXT("seed"))) : INT32_MIN;

	AsyncTask(ENamedThreads::GameThread, [this, OnComplete, ActorName, bForce, Seed]()
	{
		AActor* Actor = FindActorByName(ActorName);
		if (!Actor)
		{
			SendErrorResponse(OnComplete, FString::Printf(TEXT("Actor not found: %s"), *ActorName), 404);
			return;
		}

		UPCGComponent* Component = Actor->FindComponentByClass<UPCGComponent>();
		if (!Component)
		{
			if (APCGVolume* Volume = Cast<APCGVolume>(Actor))
			{
				Component = Volume->PCGComponent;
			}
		}
		if (!Component)
		{
			SendErrorResponse(OnComplete, TEXT("No PCG component on actor"), 404);
			return;
		}

		if (Seed != INT32_MIN)
		{
			Component->Seed = Seed;
		}
		Component->Generate(bForce);

		TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
		Result->SetStringField(TEXT("status"), TEXT("ok"));
		Result->SetStringField(TEXT("actor"), ActorName);
		Result->SetBoolField(TEXT("generation_triggered"), true);
		Result->SetBoolField(TEXT("force_regenerate"), bForce);
		Result->SetNumberField(TEXT("seed"), Component->Seed);
		SendJsonResponse(OnComplete, Result);
	});
#else
	SendErrorResponse(OnComplete, TEXT("PCG module is not available in this build"), 501);
#endif
	return true;
}

bool FNovaBridgeModule::HandlePcgSetParam(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	TSharedPtr<FJsonObject> Body = ParseRequestBody(Request);
	if (!Body)
	{
		SendErrorResponse(OnComplete, TEXT("Invalid JSON body"));
		return true;
	}

#if NOVABRIDGE_WITH_PCG
	const FString ActorName = Body->GetStringField(TEXT("actor_name"));
	const FString ParamName = Body->GetStringField(TEXT("param_name"));
	const FString ParamType = Body->HasField(TEXT("param_type")) ? Body->GetStringField(TEXT("param_type")).ToLower() : TEXT("");

	AsyncTask(ENamedThreads::GameThread, [this, OnComplete, ActorName, ParamName, ParamType, Body]()
	{
		AActor* Actor = FindActorByName(ActorName);
		if (!Actor)
		{
			SendErrorResponse(OnComplete, FString::Printf(TEXT("Actor not found: %s"), *ActorName), 404);
			return;
		}

		UPCGComponent* Component = Actor->FindComponentByClass<UPCGComponent>();
		if (!Component)
		{
			if (APCGVolume* Volume = Cast<APCGVolume>(Actor))
			{
				Component = Volume->PCGComponent;
			}
		}
		if (!Component)
		{
			SendErrorResponse(OnComplete, TEXT("No PCG component on actor"), 404);
			return;
		}

		if (ParamName.Equals(TEXT("seed"), ESearchCase::IgnoreCase))
		{
			const int32 Seed = Body->HasField(TEXT("value")) ? static_cast<int32>(Body->GetNumberField(TEXT("value"))) : 42;
			Component->Seed = Seed;
		}
		else if (ParamName.Equals(TEXT("activated"), ESearchCase::IgnoreCase) || ParamName.Equals(TEXT("enabled"), ESearchCase::IgnoreCase))
		{
			const bool bActivated = Body->HasField(TEXT("value")) ? Body->GetBoolField(TEXT("value")) : true;
			Component->bActivated = bActivated;
		}
		else
		{
			SendErrorResponse(OnComplete, FString::Printf(TEXT("Unsupported param '%s' in v1. Supported: seed, activated"), *ParamName), 400);
			return;
		}

		Component->MarkPackageDirty();
		TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
		Result->SetStringField(TEXT("status"), TEXT("ok"));
		Result->SetStringField(TEXT("actor"), ActorName);
		Result->SetStringField(TEXT("param_name"), ParamName);
		Result->SetStringField(TEXT("param_type"), ParamType);
		SendJsonResponse(OnComplete, Result);
	});
#else
	SendErrorResponse(OnComplete, TEXT("PCG module is not available in this build"), 501);
#endif
	return true;
}

bool FNovaBridgeModule::HandlePcgCleanup(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	TSharedPtr<FJsonObject> Body = ParseRequestBody(Request);
	if (!Body)
	{
		SendErrorResponse(OnComplete, TEXT("Invalid JSON body"));
		return true;
	}

#if NOVABRIDGE_WITH_PCG
	const FString ActorName = Body->GetStringField(TEXT("actor_name"));
	AsyncTask(ENamedThreads::GameThread, [this, OnComplete, ActorName]()
	{
		AActor* Actor = FindActorByName(ActorName);
		if (!Actor)
		{
			SendErrorResponse(OnComplete, FString::Printf(TEXT("Actor not found: %s"), *ActorName), 404);
			return;
		}

		UPCGComponent* Component = Actor->FindComponentByClass<UPCGComponent>();
		if (!Component)
		{
			if (APCGVolume* Volume = Cast<APCGVolume>(Actor))
			{
				Component = Volume->PCGComponent;
			}
		}
		if (!Component)
		{
			SendErrorResponse(OnComplete, TEXT("No PCG component on actor"), 404);
			return;
		}

		Component->Cleanup(true, false);
		TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
		Result->SetStringField(TEXT("status"), TEXT("ok"));
		Result->SetStringField(TEXT("actor"), ActorName);
		Result->SetBoolField(TEXT("cleaned"), true);
		SendJsonResponse(OnComplete, Result);
	});
#else
	SendErrorResponse(OnComplete, TEXT("PCG module is not available in this build"), 501);
#endif
	return true;
}

// ============================================================
// Sequencer Handlers
// ============================================================

bool FNovaBridgeModule::HandleSequencerCreate(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	TSharedPtr<FJsonObject> Body = ParseRequestBody(Request);
	if (!Body)
	{
		SendErrorResponse(OnComplete, TEXT("Invalid JSON body"));
		return true;
	}

	const FString Name = Body->GetStringField(TEXT("name"));
	const FString Path = Body->HasField(TEXT("path")) ? Body->GetStringField(TEXT("path")) : TEXT("/Game");
	const float DurationSeconds = Body->HasField(TEXT("duration_seconds")) ? static_cast<float>(Body->GetNumberField(TEXT("duration_seconds"))) : 10.0f;
	const int32 Fps = Body->HasField(TEXT("fps")) ? FMath::Clamp(static_cast<int32>(Body->GetNumberField(TEXT("fps"))), 1, 120) : 30;

	if (Name.IsEmpty())
	{
		SendErrorResponse(OnComplete, TEXT("Missing 'name'"));
		return true;
	}

	AsyncTask(ENamedThreads::GameThread, [this, OnComplete, Name, Path, DurationSeconds, Fps]()
	{
		const FString PackagePath = Path / Name;
		UPackage* Package = CreatePackage(*PackagePath);
		if (!Package)
		{
			SendErrorResponse(OnComplete, TEXT("Failed to create sequence package"), 500);
			return;
		}

		ULevelSequence* Sequence = NewObject<ULevelSequence>(Package, FName(*Name), RF_Public | RF_Standalone);
		if (!Sequence)
		{
			SendErrorResponse(OnComplete, TEXT("Failed to create sequence asset"), 500);
			return;
		}

		Sequence->Initialize();
		UMovieScene* MovieScene = Sequence->GetMovieScene();
		if (!MovieScene)
		{
			SendErrorResponse(OnComplete, TEXT("Failed to initialize movie scene"), 500);
			return;
		}

		const FFrameRate DisplayRate(Fps, 1);
		MovieScene->SetDisplayRate(DisplayRate);
		MovieScene->SetTickResolutionDirectly(DisplayRate);
		const FFrameNumber DurationFrames = DisplayRate.AsFrameNumber(FMath::Max(0.1f, DurationSeconds));
		MovieScene->SetPlaybackRange(0, DurationFrames.Value);

		FAssetRegistryModule::AssetCreated(Sequence);
		Sequence->MarkPackageDirty();

		TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
		Result->SetStringField(TEXT("status"), TEXT("ok"));
		Result->SetStringField(TEXT("sequence"), Sequence->GetPathName());
		Result->SetNumberField(TEXT("duration_seconds"), DurationSeconds);
		Result->SetNumberField(TEXT("fps"), Fps);
		SendJsonResponse(OnComplete, Result);
	});
	return true;
}

bool FNovaBridgeModule::HandleSequencerAddTrack(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	TSharedPtr<FJsonObject> Body = ParseRequestBody(Request);
	if (!Body)
	{
		SendErrorResponse(OnComplete, TEXT("Invalid JSON body"));
		return true;
	}

	const FString SequencePath = Body->GetStringField(TEXT("sequence"));
	const FString ActorName = Body->GetStringField(TEXT("actor_name"));
	const FString TrackType = Body->HasField(TEXT("track_type")) ? Body->GetStringField(TEXT("track_type")).ToLower() : TEXT("transform");

	AsyncTask(ENamedThreads::GameThread, [this, OnComplete, SequencePath, ActorName, TrackType]()
	{
		UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
		if (!World)
		{
			SendErrorResponse(OnComplete, TEXT("No world"), 500);
			return;
		}

		ULevelSequence* Sequence = LoadObject<ULevelSequence>(nullptr, *SequencePath);
		if (!Sequence)
		{
			SendErrorResponse(OnComplete, FString::Printf(TEXT("Sequence not found: %s"), *SequencePath), 404);
			return;
		}

		AActor* Actor = FindActorByName(ActorName);
		if (!Actor)
		{
			SendErrorResponse(OnComplete, FString::Printf(TEXT("Actor not found: %s"), *ActorName), 404);
			return;
		}

		UMovieScene* MovieScene = Sequence->GetMovieScene();
		FGuid Binding = NovaBridgeFindBinding(Sequence, Actor, World);
		if (!Binding.IsValid())
		{
			Binding = MovieScene->AddPossessable(Actor->GetActorLabel(), Actor->GetClass());
			Sequence->BindPossessableObject(Binding, *Actor, World);
		}

		if (TrackType != TEXT("transform"))
		{
			SendErrorResponse(OnComplete, TEXT("Only track_type='transform' is supported in v1"), 400);
			return;
		}

		UMovieScene3DTransformTrack* Track = MovieScene->FindTrack<UMovieScene3DTransformTrack>(Binding);
		if (!Track)
		{
			Track = MovieScene->AddTrack<UMovieScene3DTransformTrack>(Binding);
		}
		if (!Track)
		{
			SendErrorResponse(OnComplete, TEXT("Failed to create transform track"), 500);
			return;
		}

		if (Track->GetAllSections().Num() == 0)
		{
			UMovieSceneSection* NewSection = Track->CreateNewSection();
			Track->AddSection(*NewSection);
		}

		Sequence->MarkPackageDirty();
		TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
		Result->SetStringField(TEXT("status"), TEXT("ok"));
		Result->SetStringField(TEXT("sequence"), SequencePath);
		Result->SetStringField(TEXT("actor_name"), ActorName);
		Result->SetStringField(TEXT("track_type"), TrackType);
		Result->SetStringField(TEXT("binding"), Binding.ToString());
		SendJsonResponse(OnComplete, Result);
	});
	return true;
}

bool FNovaBridgeModule::HandleSequencerSetKeyframe(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	TSharedPtr<FJsonObject> Body = ParseRequestBody(Request);
	if (!Body)
	{
		SendErrorResponse(OnComplete, TEXT("Invalid JSON body"));
		return true;
	}

	const FString SequencePath = Body->GetStringField(TEXT("sequence"));
	const FString ActorName = Body->GetStringField(TEXT("actor_name"));
	const float TimeSeconds = Body->HasField(TEXT("time")) ? static_cast<float>(Body->GetNumberField(TEXT("time"))) : 0.0f;
	const FString TrackType = Body->HasField(TEXT("track_type")) ? Body->GetStringField(TEXT("track_type")).ToLower() : TEXT("transform");

	AsyncTask(ENamedThreads::GameThread, [this, OnComplete, SequencePath, ActorName, TimeSeconds, TrackType, Body]()
	{
		UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
		if (!World)
		{
			SendErrorResponse(OnComplete, TEXT("No world"), 500);
			return;
		}

		if (TrackType != TEXT("transform"))
		{
			SendErrorResponse(OnComplete, TEXT("Only track_type='transform' is supported in v1"), 400);
			return;
		}

		ULevelSequence* Sequence = LoadObject<ULevelSequence>(nullptr, *SequencePath);
		if (!Sequence)
		{
			SendErrorResponse(OnComplete, FString::Printf(TEXT("Sequence not found: %s"), *SequencePath), 404);
			return;
		}

		AActor* Actor = FindActorByName(ActorName);
		if (!Actor)
		{
			SendErrorResponse(OnComplete, FString::Printf(TEXT("Actor not found: %s"), *ActorName), 404);
			return;
		}

		UMovieScene* MovieScene = Sequence->GetMovieScene();
		FGuid Binding = NovaBridgeFindBinding(Sequence, Actor, World);
		if (!Binding.IsValid())
		{
			Binding = MovieScene->AddPossessable(Actor->GetActorLabel(), Actor->GetClass());
			Sequence->BindPossessableObject(Binding, *Actor, World);
		}

		UMovieScene3DTransformTrack* Track = MovieScene->FindTrack<UMovieScene3DTransformTrack>(Binding);
		if (!Track)
		{
			Track = MovieScene->AddTrack<UMovieScene3DTransformTrack>(Binding);
		}
		if (!Track)
		{
			SendErrorResponse(OnComplete, TEXT("Failed to create transform track"), 500);
			return;
		}

		UMovieScene3DTransformSection* Section = nullptr;
		if (Track->GetAllSections().Num() == 0)
		{
			UMovieSceneSection* NewSection = Track->CreateNewSection();
			if (!NewSection)
			{
				SendErrorResponse(OnComplete, TEXT("Failed to create transform section"), 500);
				return;
			}
			Track->AddSection(*NewSection);
			Section = Cast<UMovieScene3DTransformSection>(NewSection);
		}
		else
		{
			Section = Cast<UMovieScene3DTransformSection>(Track->GetAllSections()[0]);
		}
		if (!Section)
		{
			SendErrorResponse(OnComplete, TEXT("Failed to create transform section"), 500);
			return;
		}

		FVector Location = Actor->GetActorLocation();
		FRotator Rotation = Actor->GetActorRotation();
		FVector Scale = Actor->GetActorScale3D();

		if (Body->HasTypedField<EJson::Object>(TEXT("location")))
		{
			const TSharedPtr<FJsonObject> Loc = Body->GetObjectField(TEXT("location"));
			Location.X = Loc->HasField(TEXT("x")) ? Loc->GetNumberField(TEXT("x")) : Location.X;
			Location.Y = Loc->HasField(TEXT("y")) ? Loc->GetNumberField(TEXT("y")) : Location.Y;
			Location.Z = Loc->HasField(TEXT("z")) ? Loc->GetNumberField(TEXT("z")) : Location.Z;
		}
		if (Body->HasTypedField<EJson::Object>(TEXT("rotation")))
		{
			const TSharedPtr<FJsonObject> Rot = Body->GetObjectField(TEXT("rotation"));
			Rotation.Pitch = Rot->HasField(TEXT("pitch")) ? Rot->GetNumberField(TEXT("pitch")) : Rotation.Pitch;
			Rotation.Yaw = Rot->HasField(TEXT("yaw")) ? Rot->GetNumberField(TEXT("yaw")) : Rotation.Yaw;
			Rotation.Roll = Rot->HasField(TEXT("roll")) ? Rot->GetNumberField(TEXT("roll")) : Rotation.Roll;
		}
		if (Body->HasTypedField<EJson::Object>(TEXT("scale")))
		{
			const TSharedPtr<FJsonObject> ScaleObj = Body->GetObjectField(TEXT("scale"));
			Scale.X = ScaleObj->HasField(TEXT("x")) ? ScaleObj->GetNumberField(TEXT("x")) : Scale.X;
			Scale.Y = ScaleObj->HasField(TEXT("y")) ? ScaleObj->GetNumberField(TEXT("y")) : Scale.Y;
			Scale.Z = ScaleObj->HasField(TEXT("z")) ? ScaleObj->GetNumberField(TEXT("z")) : Scale.Z;
		}

		const FFrameNumber KeyFrame = MovieScene->GetTickResolution().AsFrameNumber(TimeSeconds);
		TArrayView<FMovieSceneDoubleChannel*> Channels = Section->GetChannelProxy().GetChannels<FMovieSceneDoubleChannel>();
		if (Channels.Num() >= 9)
		{
			Channels[0]->AddCubicKey(KeyFrame, Location.X);
			Channels[1]->AddCubicKey(KeyFrame, Location.Y);
			Channels[2]->AddCubicKey(KeyFrame, Location.Z);
			Channels[3]->AddCubicKey(KeyFrame, Rotation.Roll);
			Channels[4]->AddCubicKey(KeyFrame, Rotation.Pitch);
			Channels[5]->AddCubicKey(KeyFrame, Rotation.Yaw);
			Channels[6]->AddCubicKey(KeyFrame, Scale.X);
			Channels[7]->AddCubicKey(KeyFrame, Scale.Y);
			Channels[8]->AddCubicKey(KeyFrame, Scale.Z);
		}

		Section->SetRange(TRange<FFrameNumber>::All());
		Sequence->MarkPackageDirty();

		TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
		Result->SetStringField(TEXT("status"), TEXT("ok"));
		Result->SetStringField(TEXT("sequence"), SequencePath);
		Result->SetStringField(TEXT("actor_name"), ActorName);
		Result->SetNumberField(TEXT("time"), TimeSeconds);
		Result->SetNumberField(TEXT("frame"), KeyFrame.Value);
		SendJsonResponse(OnComplete, Result);
	});
	return true;
}

bool FNovaBridgeModule::HandleSequencerPlay(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	TSharedPtr<FJsonObject> Body = ParseRequestBody(Request);
	if (!Body)
	{
		SendErrorResponse(OnComplete, TEXT("Invalid JSON body"));
		return true;
	}

	const FString SequencePath = Body->GetStringField(TEXT("sequence"));
	const bool bLoop = Body->HasField(TEXT("loop")) && Body->GetBoolField(TEXT("loop"));
	const float StartTime = Body->HasField(TEXT("start_time")) ? static_cast<float>(Body->GetNumberField(TEXT("start_time"))) : 0.0f;

	AsyncTask(ENamedThreads::GameThread, [this, OnComplete, SequencePath, bLoop, StartTime]()
	{
		UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
		if (!World)
		{
			SendErrorResponse(OnComplete, TEXT("No world"), 500);
			return;
		}

		ULevelSequence* Sequence = LoadObject<ULevelSequence>(nullptr, *SequencePath);
		if (!Sequence)
		{
			SendErrorResponse(OnComplete, FString::Printf(TEXT("Sequence not found: %s"), *SequencePath), 404);
			return;
		}

		FMovieSceneSequencePlaybackSettings Settings;
		Settings.bAutoPlay = false;
		Settings.LoopCount.Value = bLoop ? -1 : 0;

		ALevelSequenceActor* SequenceActor = nullptr;
		ULevelSequencePlayer* Player = ULevelSequencePlayer::CreateLevelSequencePlayer(World, Sequence, Settings, SequenceActor);
		if (!Player)
		{
			SendErrorResponse(OnComplete, TEXT("Failed to create sequence player"), 500);
			return;
		}

		if (StartTime > 0.f)
		{
			NovaBridgeSetPlaybackTime(Player, StartTime, false);
		}
		Player->Play();

		SequencePlayers.Add(SequencePath, Player);
		if (SequenceActor)
		{
			SequenceActors.Add(SequencePath, SequenceActor);
		}

		TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
		Result->SetStringField(TEXT("status"), TEXT("ok"));
		Result->SetStringField(TEXT("sequence"), SequencePath);
		Result->SetBoolField(TEXT("playing"), true);
		Result->SetBoolField(TEXT("loop"), bLoop);
		SendJsonResponse(OnComplete, Result);
	});
	return true;
}

bool FNovaBridgeModule::HandleSequencerStop(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	TSharedPtr<FJsonObject> Body = ParseRequestBody(Request);
	const FString SequencePath = (Body && Body->HasField(TEXT("sequence"))) ? Body->GetStringField(TEXT("sequence")) : FString();

	AsyncTask(ENamedThreads::GameThread, [this, OnComplete, SequencePath]()
	{
		int32 Stopped = 0;
		if (!SequencePath.IsEmpty())
		{
			if (TWeakObjectPtr<ULevelSequencePlayer>* PlayerPtr = SequencePlayers.Find(SequencePath))
			{
				if (PlayerPtr->IsValid())
				{
					(*PlayerPtr)->Stop();
					Stopped++;
				}
			}
		}
		else
		{
			for (TPair<FString, TWeakObjectPtr<ULevelSequencePlayer>>& Pair : SequencePlayers)
			{
				if (Pair.Value.IsValid())
				{
					Pair.Value->Stop();
					Stopped++;
				}
			}
		}

		TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
		Result->SetStringField(TEXT("status"), TEXT("ok"));
		Result->SetNumberField(TEXT("stopped_players"), Stopped);
		SendJsonResponse(OnComplete, Result);
	});
	return true;
}

bool FNovaBridgeModule::HandleSequencerScrub(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	TSharedPtr<FJsonObject> Body = ParseRequestBody(Request);
	if (!Body)
	{
		SendErrorResponse(OnComplete, TEXT("Invalid JSON body"));
		return true;
	}

	const FString SequencePath = Body->GetStringField(TEXT("sequence"));
	const float TimeSeconds = Body->HasField(TEXT("time")) ? static_cast<float>(Body->GetNumberField(TEXT("time"))) : 0.0f;

	AsyncTask(ENamedThreads::GameThread, [this, OnComplete, SequencePath, TimeSeconds]()
	{
		UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
		if (!World)
		{
			SendErrorResponse(OnComplete, TEXT("No world"), 500);
			return;
		}

		ULevelSequencePlayer* Player = nullptr;
		if (TWeakObjectPtr<ULevelSequencePlayer>* Existing = SequencePlayers.Find(SequencePath))
		{
			Player = Existing->Get();
		}
		if (!Player)
		{
			ULevelSequence* Sequence = LoadObject<ULevelSequence>(nullptr, *SequencePath);
			if (!Sequence)
			{
				SendErrorResponse(OnComplete, FString::Printf(TEXT("Sequence not found: %s"), *SequencePath), 404);
				return;
			}
			FMovieSceneSequencePlaybackSettings Settings;
			ALevelSequenceActor* SequenceActor = nullptr;
			Player = ULevelSequencePlayer::CreateLevelSequencePlayer(World, Sequence, Settings, SequenceActor);
			if (!Player)
			{
				SendErrorResponse(OnComplete, TEXT("Failed to create sequence player"), 500);
				return;
			}
			SequencePlayers.Add(SequencePath, Player);
			if (SequenceActor)
			{
				SequenceActors.Add(SequencePath, SequenceActor);
			}
		}

		NovaBridgeSetPlaybackTime(Player, TimeSeconds, true);
		TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
		Result->SetStringField(TEXT("status"), TEXT("ok"));
		Result->SetStringField(TEXT("sequence"), SequencePath);
		Result->SetNumberField(TEXT("time"), TimeSeconds);
		SendJsonResponse(OnComplete, Result);
	});
	return true;
}

bool FNovaBridgeModule::HandleSequencerRender(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	TSharedPtr<FJsonObject> Body = ParseRequestBody(Request);
	if (!Body)
	{
		SendErrorResponse(OnComplete, TEXT("Invalid JSON body"));
		return true;
	}

	const FString SequencePath = Body->GetStringField(TEXT("sequence"));
	const FString OutputPath = Body->HasField(TEXT("output_path"))
		? Body->GetStringField(TEXT("output_path"))
		: (FPaths::ProjectSavedDir() / TEXT("NovaBridgeRenders") / FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S")));
	const int32 Fps = Body->HasField(TEXT("fps")) ? FMath::Clamp(static_cast<int32>(Body->GetNumberField(TEXT("fps"))), 1, 60) : 24;
	const float Duration = Body->HasField(TEXT("duration_seconds")) ? static_cast<float>(Body->GetNumberField(TEXT("duration_seconds"))) : 5.0f;

	AsyncTask(ENamedThreads::GameThread, [this, OnComplete, SequencePath, OutputPath, Fps, Duration]()
	{
		UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
		if (!World)
		{
			SendErrorResponse(OnComplete, TEXT("No world"), 500);
			return;
		}

		ULevelSequence* Sequence = LoadObject<ULevelSequence>(nullptr, *SequencePath);
		if (!Sequence)
		{
			SendErrorResponse(OnComplete, FString::Printf(TEXT("Sequence not found: %s"), *SequencePath), 404);
			return;
		}

		IFileManager::Get().MakeDirectory(*OutputPath, true);

		FMovieSceneSequencePlaybackSettings Settings;
		ALevelSequenceActor* SequenceActor = nullptr;
		ULevelSequencePlayer* Player = ULevelSequencePlayer::CreateLevelSequencePlayer(World, Sequence, Settings, SequenceActor);
		if (!Player)
		{
			SendErrorResponse(OnComplete, TEXT("Failed to create sequence player"), 500);
			return;
		}

		const int32 FrameCount = FMath::Clamp(FMath::CeilToInt(Duration * Fps), 1, 900);
		EnsureCaptureSetup();
		if (!CaptureActor.IsValid() || !RenderTarget.IsValid())
		{
			SendErrorResponse(OnComplete, TEXT("Failed to initialize capture for render"), 500);
			return;
		}

		TArray<TSharedPtr<FJsonValue>> Frames;
		for (int32 FrameIdx = 0; FrameIdx < FrameCount; ++FrameIdx)
		{
			const float TimeSeconds = static_cast<float>(FrameIdx) / static_cast<float>(Fps);
			NovaBridgeSetPlaybackTime(Player, TimeSeconds, false);

			USceneCaptureComponent2D* CaptureComp = CaptureActor->GetCaptureComponent2D();
			CaptureActor->SetActorLocation(CameraLocation);
			CaptureActor->SetActorRotation(CameraRotation);
			CaptureComp->FOVAngle = CameraFOV;
			CaptureComp->CaptureScene();

			FTextureRenderTargetResource* RTResource = RenderTarget->GameThread_GetRenderTargetResource();
			if (!RTResource)
			{
				continue;
			}

			TArray<FColor> Bitmap;
			if (!RTResource->ReadPixels(Bitmap) || Bitmap.Num() == 0)
			{
				continue;
			}

			IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));
			TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);
			ImageWrapper->SetRaw(Bitmap.GetData(), Bitmap.Num() * sizeof(FColor), CaptureWidth, CaptureHeight, ERGBFormat::BGRA, 8);
			TArray64<uint8> PngData = ImageWrapper->GetCompressed(0);

			const FString FramePath = OutputPath / FString::Printf(TEXT("frame_%05d.png"), FrameIdx);
			TArray<uint8> PngData32;
			PngData32.Append(PngData.GetData(), static_cast<int32>(PngData.Num()));
			if (FFileHelper::SaveArrayToFile(PngData32, *FramePath))
			{
				Frames.Add(MakeShareable(new FJsonValueString(FramePath)));
			}
		}

		TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
		Result->SetStringField(TEXT("status"), TEXT("ok"));
		Result->SetStringField(TEXT("sequence"), SequencePath);
		Result->SetStringField(TEXT("output_path"), OutputPath);
		Result->SetStringField(TEXT("format"), TEXT("png-sequence"));
		Result->SetNumberField(TEXT("fps"), Fps);
		Result->SetNumberField(TEXT("frame_count"), Frames.Num());
		Result->SetArrayField(TEXT("frames"), Frames);
		Result->SetStringField(TEXT("note"), TEXT("Rendered as PNG sequence. Use ffmpeg externally for MP4 encoding."));
		SendJsonResponse(OnComplete, Result);
	});
	return true;
}

bool FNovaBridgeModule::HandleSequencerInfo(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	(void)Request;
	AsyncTask(ENamedThreads::GameThread, [this, OnComplete]()
	{
		TArray<TSharedPtr<FJsonValue>> Active;
		for (const TPair<FString, TWeakObjectPtr<ULevelSequencePlayer>>& Pair : SequencePlayers)
		{
			if (!Pair.Value.IsValid())
			{
				continue;
			}
			TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
			Obj->SetStringField(TEXT("sequence"), Pair.Key);
			Obj->SetBoolField(TEXT("playing"), Pair.Value->IsPlaying());
			Obj->SetNumberField(TEXT("time_seconds"), Pair.Value->GetCurrentTime().AsSeconds());
			Active.Add(MakeShareable(new FJsonValueObject(Obj)));
		}

		TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
		Result->SetArrayField(TEXT("players"), Active);
		Result->SetNumberField(TEXT("count"), Active.Num());
		SendJsonResponse(OnComplete, Result);
	});
	return true;
}

// ============================================================
// Optimize Handlers
// ============================================================

bool FNovaBridgeModule::HandleOptimizeNanite(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	TSharedPtr<FJsonObject> Body = ParseRequestBody(Request);
	if (!Body)
	{
		SendErrorResponse(OnComplete, TEXT("Invalid JSON body"));
		return true;
	}

	const FString MeshPath = Body->HasField(TEXT("mesh_path")) ? Body->GetStringField(TEXT("mesh_path")) : FString();
	const FString ActorName = Body->HasField(TEXT("actor_name")) ? Body->GetStringField(TEXT("actor_name")) : FString();
	const bool bEnable = !Body->HasField(TEXT("enable")) || Body->GetBoolField(TEXT("enable"));

	AsyncTask(ENamedThreads::GameThread, [this, OnComplete, MeshPath, ActorName, bEnable]()
	{
		UStaticMesh* Mesh = nullptr;
		FString ResolvedPath = MeshPath;
		if (!MeshPath.IsEmpty())
		{
			Mesh = LoadObject<UStaticMesh>(nullptr, *MeshPath);
		}
		if (!Mesh && !ActorName.IsEmpty())
		{
			if (AActor* Actor = FindActorByName(ActorName))
			{
				if (UStaticMeshComponent* MeshComp = Actor->FindComponentByClass<UStaticMeshComponent>())
				{
					Mesh = MeshComp->GetStaticMesh();
					if (Mesh)
					{
						ResolvedPath = Mesh->GetPathName();
					}
				}
			}
		}

		if (!Mesh)
		{
			SendErrorResponse(OnComplete, TEXT("Mesh not found. Provide mesh_path or actor_name with StaticMeshComponent"), 404);
			return;
		}

		Mesh->Modify();
		Mesh->NaniteSettings.bEnabled = bEnable;
		Mesh->PostEditChange();
		Mesh->MarkPackageDirty();

		TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
		Result->SetStringField(TEXT("status"), TEXT("ok"));
		Result->SetStringField(TEXT("mesh"), ResolvedPath);
		Result->SetBoolField(TEXT("nanite_enabled"), bEnable);
		SendJsonResponse(OnComplete, Result);
	});
	return true;
}

bool FNovaBridgeModule::HandleOptimizeLod(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	TSharedPtr<FJsonObject> Body = ParseRequestBody(Request);
	if (!Body)
	{
		SendErrorResponse(OnComplete, TEXT("Invalid JSON body"));
		return true;
	}

	const FString MeshPath = Body->HasField(TEXT("mesh_path")) ? Body->GetStringField(TEXT("mesh_path")) : FString();
	const int32 NumLods = Body->HasField(TEXT("num_lods")) ? FMath::Clamp(static_cast<int32>(Body->GetNumberField(TEXT("num_lods"))), 2, 8) : 4;

	AsyncTask(ENamedThreads::GameThread, [this, OnComplete, MeshPath, NumLods]()
	{
		UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *MeshPath);
		if (!Mesh)
		{
			SendErrorResponse(OnComplete, TEXT("Mesh not found"), 404);
			return;
		}

		Mesh->Modify();
		Mesh->SetNumSourceModels(NumLods);
		Mesh->GenerateLodsInPackage();
		Mesh->PostEditChange();
		Mesh->MarkPackageDirty();

		TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
		Result->SetStringField(TEXT("status"), TEXT("ok"));
		Result->SetStringField(TEXT("mesh"), MeshPath);
		Result->SetNumberField(TEXT("num_lods"), NumLods);
		SendJsonResponse(OnComplete, Result);
	});
	return true;
}

bool FNovaBridgeModule::HandleOptimizeLumen(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	TSharedPtr<FJsonObject> Body = ParseRequestBody(Request);
	if (!Body)
	{
		SendErrorResponse(OnComplete, TEXT("Invalid JSON body"));
		return true;
	}

	const bool bEnabled = !Body->HasField(TEXT("enabled")) || Body->GetBoolField(TEXT("enabled"));
	const FString Quality = Body->HasField(TEXT("quality")) ? Body->GetStringField(TEXT("quality")).ToLower() : TEXT("high");

	AsyncTask(ENamedThreads::GameThread, [this, OnComplete, bEnabled, Quality]()
	{
		UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
		if (!GEditor || !World)
		{
			SendErrorResponse(OnComplete, TEXT("No editor/world"), 500);
			return;
		}

		TArray<FString> Commands;
		Commands.Add(FString::Printf(TEXT("r.DynamicGlobalIlluminationMethod %d"), bEnabled ? 1 : 0));
		Commands.Add(FString::Printf(TEXT("r.ReflectionMethod %d"), bEnabled ? 1 : 0));

		int32 ProbeQuality = 3;
		if (Quality == TEXT("low")) ProbeQuality = 1;
		else if (Quality == TEXT("medium")) ProbeQuality = 2;
		else if (Quality == TEXT("epic")) ProbeQuality = 4;

		Commands.Add(FString::Printf(TEXT("r.Lumen.ScreenProbeGather.Quality %d"), ProbeQuality));
		Commands.Add(FString::Printf(TEXT("r.Lumen.Reflections.Quality %d"), ProbeQuality));

		for (const FString& Cmd : Commands)
		{
			GEditor->Exec(World, *Cmd);
		}

		TArray<TSharedPtr<FJsonValue>> Applied;
		for (const FString& Cmd : Commands)
		{
			Applied.Add(MakeShareable(new FJsonValueString(Cmd)));
		}

		TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
		Result->SetStringField(TEXT("status"), TEXT("ok"));
		Result->SetBoolField(TEXT("enabled"), bEnabled);
		Result->SetStringField(TEXT("quality"), Quality);
		Result->SetArrayField(TEXT("commands"), Applied);
		SendJsonResponse(OnComplete, Result);
	});
	return true;
}

bool FNovaBridgeModule::HandleOptimizeStats(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	(void)Request;
	AsyncTask(ENamedThreads::GameThread, [this, OnComplete]()
	{
		UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
		if (!World)
		{
			SendErrorResponse(OnComplete, TEXT("No world"), 500);
			return;
		}

		int32 ActorCount = 0;
		int32 StaticMeshComponentCount = 0;
		int64 TriangleCount = 0;
		int32 NaniteMeshCount = 0;
		int32 PointLights = 0;
		int32 DirectionalLights = 0;
		int32 SpotLights = 0;
		int64 ApproxTextureBytes = 0;

		for (TActorIterator<AActor> It(World); It; ++It)
		{
			++ActorCount;

			TArray<UStaticMeshComponent*> MeshComponents;
			It->GetComponents<UStaticMeshComponent>(MeshComponents);
			for (UStaticMeshComponent* Comp : MeshComponents)
			{
				if (!Comp || !Comp->GetStaticMesh())
				{
					continue;
				}
				++StaticMeshComponentCount;
				TriangleCount += Comp->GetStaticMesh()->GetNumTriangles(0);
				if (Comp->GetStaticMesh()->NaniteSettings.bEnabled)
				{
					++NaniteMeshCount;
				}
			}

			PointLights += It->FindComponentByClass<UPointLightComponent>() ? 1 : 0;
			DirectionalLights += It->FindComponentByClass<UDirectionalLightComponent>() ? 1 : 0;
			SpotLights += It->GetClass()->GetName().Contains(TEXT("SpotLight")) ? 1 : 0;
		}

		for (TObjectIterator<UTexture2D> It; It; ++It)
		{
			if (!It->GetPathName().StartsWith(TEXT("/Game")))
			{
				continue;
			}
			ApproxTextureBytes += It->CalcTextureMemorySizeEnum(TMC_AllMipsBiased);
		}

		TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
		Result->SetStringField(TEXT("status"), TEXT("ok"));
		Result->SetNumberField(TEXT("actor_count"), ActorCount);
		Result->SetNumberField(TEXT("static_mesh_components"), StaticMeshComponentCount);
		Result->SetNumberField(TEXT("triangle_count_lod0"), static_cast<double>(TriangleCount));
		Result->SetNumberField(TEXT("nanite_mesh_components"), NaniteMeshCount);
		Result->SetNumberField(TEXT("point_lights"), PointLights);
		Result->SetNumberField(TEXT("directional_lights"), DirectionalLights);
		Result->SetNumberField(TEXT("spot_lights"), SpotLights);
		Result->SetNumberField(TEXT("texture_memory_bytes_estimate"), static_cast<double>(ApproxTextureBytes));
		Result->SetNumberField(TEXT("draw_calls_estimate"), StaticMeshComponentCount);
		SendJsonResponse(OnComplete, Result);
	});
	return true;
}

bool FNovaBridgeModule::HandleOptimizeTextures(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	TSharedPtr<FJsonObject> Body = ParseRequestBody(Request);
	if (!Body)
	{
		SendErrorResponse(OnComplete, TEXT("Invalid JSON body"));
		return true;
	}

	const FString RootPath = Body->HasField(TEXT("path")) ? Body->GetStringField(TEXT("path")) : TEXT("/Game");
	const int32 MaxSize = Body->HasField(TEXT("max_size")) ? FMath::Clamp(static_cast<int32>(Body->GetNumberField(TEXT("max_size"))), 256, 8192) : 2048;
	const FString Compression = Body->HasField(TEXT("compression")) ? Body->GetStringField(TEXT("compression")).ToLower() : TEXT("default");

	AsyncTask(ENamedThreads::GameThread, [this, OnComplete, RootPath, MaxSize, Compression]()
	{
		FAssetRegistryModule& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		TArray<FAssetData> Assets;
		AssetRegistry.Get().GetAssetsByPath(*RootPath, Assets, true);

		int32 Updated = 0;
		for (const FAssetData& Asset : Assets)
		{
			if (Asset.AssetClassPath != UTexture2D::StaticClass()->GetClassPathName())
			{
				continue;
			}

			UTexture2D* Texture = Cast<UTexture2D>(Asset.GetAsset());
			if (!Texture)
			{
				continue;
			}

			Texture->Modify();
			Texture->MaxTextureSize = MaxSize;
			if (Compression == TEXT("normalmap"))
			{
				Texture->CompressionSettings = TC_Normalmap;
			}
			else if (Compression == TEXT("hdr"))
			{
				Texture->CompressionSettings = TC_HDR;
			}
			else
			{
				Texture->CompressionSettings = TC_Default;
			}
			Texture->PostEditChange();
			Texture->UpdateResource();
			Texture->MarkPackageDirty();
			Updated++;
		}

		TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
		Result->SetStringField(TEXT("status"), TEXT("ok"));
		Result->SetStringField(TEXT("path"), RootPath);
		Result->SetNumberField(TEXT("max_size"), MaxSize);
		Result->SetStringField(TEXT("compression"), Compression);
		Result->SetNumberField(TEXT("updated_textures"), Updated);
		SendJsonResponse(OnComplete, Result);
	});
	return true;
}

bool FNovaBridgeModule::HandleOptimizeCollision(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	TSharedPtr<FJsonObject> Body = ParseRequestBody(Request);
	if (!Body)
	{
		SendErrorResponse(OnComplete, TEXT("Invalid JSON body"));
		return true;
	}

	const FString MeshPath = Body->HasField(TEXT("mesh_path")) ? Body->GetStringField(TEXT("mesh_path")) : FString();
	const FString ActorName = Body->HasField(TEXT("actor_name")) ? Body->GetStringField(TEXT("actor_name")) : FString();
	const FString Type = Body->HasField(TEXT("type")) ? Body->GetStringField(TEXT("type")).ToLower() : TEXT("complex");

	AsyncTask(ENamedThreads::GameThread, [this, OnComplete, MeshPath, ActorName, Type]()
	{
		UStaticMesh* Mesh = nullptr;
		FString ResolvedPath = MeshPath;
		if (!MeshPath.IsEmpty())
		{
			Mesh = LoadObject<UStaticMesh>(nullptr, *MeshPath);
		}
		if (!Mesh && !ActorName.IsEmpty())
		{
			if (AActor* Actor = FindActorByName(ActorName))
			{
				if (UStaticMeshComponent* MeshComp = Actor->FindComponentByClass<UStaticMeshComponent>())
				{
					Mesh = MeshComp->GetStaticMesh();
					if (Mesh)
					{
						ResolvedPath = Mesh->GetPathName();
					}
				}
			}
		}

		if (!Mesh)
		{
			SendErrorResponse(OnComplete, TEXT("Mesh not found. Provide mesh_path or actor_name with StaticMeshComponent"), 404);
			return;
		}

		Mesh->Modify();
		Mesh->CreateBodySetup();
		UBodySetup* BodySetup = Mesh->GetBodySetup();
		if (!BodySetup)
		{
			SendErrorResponse(OnComplete, TEXT("Failed to create body setup"), 500);
			return;
		}

		BodySetup->Modify();
		BodySetup->RemoveSimpleCollision();
		const FBoxSphereBounds Bounds = Mesh->GetBounds();

		if (Type == TEXT("complex"))
		{
			BodySetup->CollisionTraceFlag = CTF_UseComplexAsSimple;
		}
		else
		{
			BodySetup->CollisionTraceFlag = CTF_UseSimpleAsComplex;
			if (Type == TEXT("box"))
			{
				FKBoxElem Box;
				Box.X = Bounds.BoxExtent.X * 2.0f;
				Box.Y = Bounds.BoxExtent.Y * 2.0f;
				Box.Z = Bounds.BoxExtent.Z * 2.0f;
				BodySetup->AggGeom.BoxElems.Add(Box);
			}
			else if (Type == TEXT("sphere"))
			{
				FKSphereElem Sphere;
				Sphere.Radius = Bounds.SphereRadius;
				BodySetup->AggGeom.SphereElems.Add(Sphere);
			}
			else if (Type == TEXT("capsule"))
			{
				FKSphylElem Capsule;
				Capsule.Radius = FMath::Max(Bounds.BoxExtent.X, Bounds.BoxExtent.Y);
				Capsule.Length = Bounds.BoxExtent.Z * 2.0f;
				BodySetup->AggGeom.SphylElems.Add(Capsule);
			}
			else if (Type == TEXT("convex"))
			{
				// Placeholder behavior: simple collision trace mode without explicit hull generation.
				BodySetup->CollisionTraceFlag = CTF_UseSimpleAndComplex;
			}
		}

		BodySetup->InvalidatePhysicsData();
		BodySetup->CreatePhysicsMeshes();
		Mesh->PostEditChange();
		Mesh->MarkPackageDirty();

		TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
		Result->SetStringField(TEXT("status"), TEXT("ok"));
		Result->SetStringField(TEXT("mesh"), ResolvedPath);
		Result->SetStringField(TEXT("type"), Type);
		SendJsonResponse(OnComplete, Result);
	});
	return true;
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FNovaBridgeModule, NovaBridge)
