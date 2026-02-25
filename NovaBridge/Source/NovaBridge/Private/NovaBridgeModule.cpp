#include "NovaBridgeModule.h"
#include "NovaBridgeCapabilityRegistry.h"
#include "NovaBridgeCoreTypes.h"
#include "NovaBridgeHttpUtils.h"
#include "NovaBridgePolicy.h"
#include "NovaBridgePlanDispatch.h"
#include "NovaBridgePlanEvents.h"
#include "NovaBridgePlanSchema.h"
#include "NovaBridgeEditorInternals.h"
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
#include "Components/SpotLightComponent.h"
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

DEFINE_LOG_CATEGORY(LogNovaBridge);
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

using NovaBridgeCore::GetHeaderValueCaseInsensitive;
using NovaBridgeCore::HttpVerbToString;
using NovaBridgeCore::MakeJsonStringArray;
using NovaBridgeCore::NormalizeEventType;
using NovaBridgeCore::ParseEventTypeFilter;

struct FNovaBridgeRateBucket
{
	double WindowStartSec = 0.0;
	int32 Count = 0;
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
static const int32 NovaBridgeEditorMaxPlanSteps = 128;

static const TArray<FString>& NovaBridgeSpawnClassAllowList()
{
	return NovaBridgeCore::EditorAllowedSpawnClasses();
}

static FString NormalizeRoleName(const FString& RawRole)
{
	return NovaBridgeCore::NormalizeRoleName(RawRole);
}

FString ResolveRoleFromRequest(const FHttpServerRequest& Request)
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

void PushUndoEntry(const FNovaBridgeUndoEntry& Entry)
{
	FScopeLock Lock(&NovaBridgeUndoMutex);
	NovaBridgeUndoStack.Add(Entry);
	if (NovaBridgeUndoStack.Num() > NovaBridgeUndoLimit)
	{
		NovaBridgeUndoStack.RemoveAt(0, NovaBridgeUndoStack.Num() - NovaBridgeUndoLimit, EAllowShrinking::No);
	}
}

bool PopUndoEntry(FNovaBridgeUndoEntry& OutEntry)
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

void QueueEventObject(const TSharedPtr<FJsonObject>& EventObj)
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

void PushAuditEntry(const FString& Route, const FString& Action, const FString& Role, const FString& Status, const FString& Message)
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

bool IsSpawnClassAllowedForRole(const FString& Role, const FString& ClassName)
{
	if (Role == TEXT("admin"))
	{
		return true;
	}
	return NovaBridgeCore::IsClassAllowed(NovaBridgeSpawnClassAllowList(), ClassName);
}

bool IsSpawnLocationInBounds(const FVector& Location)
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
AActor* FindActorByName(const FString& Name)
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
TSharedPtr<FJsonObject> ActorToJson(AActor* Actor)
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

UClass* ResolveActorClassByName(const FString& InClassName)
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

bool SetActorPropertyValue(AActor* Actor, const FString& PropertyName, const FString& Value, FString& OutError)
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
		Client.bSubscriptionConfirmed = false;
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
			EventWsClients[ClientIndex].bSubscriptionConfirmed = true;

			TSharedPtr<FJsonObject> AckReply = MakeShared<FJsonObject>();
			AckReply->SetStringField(TEXT("type"), TEXT("subscription"));
			AckReply->SetStringField(TEXT("status"), TEXT("ok"));
			AckReply->SetBoolField(TEXT("subscription_confirmed"), true);
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
		WelcomeReply->SetBoolField(TEXT("subscription_confirmed"), false);
		WelcomeReply->SetBoolField(TEXT("events_paused_until_subscribe"), true);
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
			if (!EventWsClients[ClientIndex].bSubscriptionConfirmed)
			{
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

static TSharedPtr<FJsonObject> BuildEditorPermissionsSnapshot(const FString& Role)
{
	TSharedPtr<FJsonObject> Permissions = MakeShared<FJsonObject>();
	Permissions->SetStringField(TEXT("mode"), TEXT("editor"));
	Permissions->SetStringField(TEXT("role"), Role);

	const bool bSpawnRouteAllowed = IsRouteAllowedForRole(Role, TEXT("/nova/scene/spawn"), EHttpServerRequestVerbs::VERB_POST);
	const bool bExecutePlanRouteAllowed = IsRouteAllowedForRole(Role, TEXT("/nova/executePlan"), EHttpServerRequestVerbs::VERB_POST);
	const bool bUndoRouteAllowed = IsRouteAllowedForRole(Role, TEXT("/nova/undo"), EHttpServerRequestVerbs::VERB_POST);
	const bool bEventsRouteAllowed = IsRouteAllowedForRole(Role, TEXT("/nova/events"), EHttpServerRequestVerbs::VERB_GET);
	const bool bSceneDeleteRouteAllowed = IsRouteAllowedForRole(Role, TEXT("/nova/scene/delete"), EHttpServerRequestVerbs::VERB_POST);

	TArray<TSharedPtr<FJsonValue>> AllowedClassValues;
	for (const FString& AllowedClass : NovaBridgeSpawnClassAllowList())
	{
		AllowedClassValues.Add(MakeShared<FJsonValueString>(AllowedClass));
	}

	TSharedPtr<FJsonObject> SpawnPolicy = MakeShared<FJsonObject>();
	SpawnPolicy->SetBoolField(TEXT("allowed"), IsPlanActionAllowedForRole(Role, TEXT("spawn")) && bSpawnRouteAllowed);
	SpawnPolicy->SetBoolField(TEXT("classes_unrestricted"), Role == TEXT("admin"));
	SpawnPolicy->SetArrayField(TEXT("allowedClasses"), AllowedClassValues);
	SpawnPolicy->SetObjectField(TEXT("bounds"), BuildSpawnBoundsJson());
	SpawnPolicy->SetNumberField(TEXT("max_spawn_per_plan"), GetPlanSpawnLimit(Role));
	SpawnPolicy->SetNumberField(TEXT("max_requests_per_minute"), bSpawnRouteAllowed ? GetRouteRateLimitPerMinute(Role, TEXT("/nova/scene/spawn")) : 0);
	Permissions->SetObjectField(TEXT("spawn"), SpawnPolicy);

	TArray<FString> AllowedPlanActions;
	for (const FString& Action : NovaBridgeCore::GetSupportedPlanActionsRef(NovaBridgeCore::ENovaBridgePlanMode::Editor))
	{
		if (IsPlanActionAllowedForRole(Role, Action))
		{
			AllowedPlanActions.Add(Action);
		}
	}

	TSharedPtr<FJsonObject> ExecutePlanPolicy = MakeShared<FJsonObject>();
	ExecutePlanPolicy->SetBoolField(TEXT("allowed"), bExecutePlanRouteAllowed);
	ExecutePlanPolicy->SetArrayField(TEXT("allowed_actions"), MakeJsonStringArray(AllowedPlanActions));
	ExecutePlanPolicy->SetNumberField(TEXT("max_steps"), NovaBridgeEditorMaxPlanSteps);
	ExecutePlanPolicy->SetNumberField(TEXT("max_requests_per_minute"), bExecutePlanRouteAllowed ? GetRouteRateLimitPerMinute(Role, TEXT("/nova/executePlan")) : 0);
	Permissions->SetObjectField(TEXT("executePlan"), ExecutePlanPolicy);

	TSharedPtr<FJsonObject> UndoPolicy = MakeShared<FJsonObject>();
	UndoPolicy->SetBoolField(TEXT("allowed"), bUndoRouteAllowed);
	UndoPolicy->SetStringField(TEXT("supported"), TEXT("spawn"));
	Permissions->SetObjectField(TEXT("undo"), UndoPolicy);

	TSharedPtr<FJsonObject> EventsPolicy = MakeShared<FJsonObject>();
	EventsPolicy->SetBoolField(TEXT("allowed"), bEventsRouteAllowed);
	EventsPolicy->SetBoolField(TEXT("subscription_ack_required"), true);
	Permissions->SetObjectField(TEXT("events"), EventsPolicy);

	TSharedPtr<FJsonObject> RateLimits = MakeShared<FJsonObject>();
	RateLimits->SetNumberField(TEXT("executePlan"), bExecutePlanRouteAllowed ? GetRouteRateLimitPerMinute(Role, TEXT("/nova/executePlan")) : 0);
	RateLimits->SetNumberField(TEXT("scene_spawn"), bSpawnRouteAllowed ? GetRouteRateLimitPerMinute(Role, TEXT("/nova/scene/spawn")) : 0);
	RateLimits->SetNumberField(TEXT("scene_delete"), bSceneDeleteRouteAllowed ? GetRouteRateLimitPerMinute(Role, TEXT("/nova/scene/delete")) : 0);
	Permissions->SetObjectField(TEXT("route_rate_limits_per_minute"), RateLimits);

	return Permissions;
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

	TSharedPtr<FJsonObject> ExecutePlanData = MakeShared<FJsonObject>();
	ExecutePlanData->SetNumberField(TEXT("max_steps"), NovaBridgeEditorMaxPlanSteps);
	ExecutePlanData->SetArrayField(
		TEXT("actions"),
		MakeJsonStringArray(NovaBridgeCore::GetSupportedPlanActionsRef(NovaBridgeCore::ENovaBridgePlanMode::Editor)));
	RegisterCapability(TEXT("executePlan"), BuildCapabilityRoles(true, true, false), ExecutePlanData);

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
	Result->SetObjectField(TEXT("permissions"), BuildEditorPermissionsSnapshot(Role));

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
		if (!NovaBridgeCore::IsCapabilityAllowedForRole(Capability, Role))
		{
			continue;
		}
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
	int32 PendingSubscriptionClients = 0;
	for (const FWsClient& Client : EventWsClients)
	{
		if (!Client.Socket)
		{
			continue;
		}
		if (!Client.bSubscriptionConfirmed)
		{
			PendingSubscriptionClients++;
			continue;
		}
		if (Client.bEventTypeFilterEnabled)
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
	Result->SetNumberField(TEXT("clients_pending_subscription"), PendingSubscriptionClients);
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

	NovaBridgeCore::FPlanSchemaError SchemaError;
	if (!NovaBridgeCore::ValidateExecutePlanSchema(Body, NovaBridgeCore::ENovaBridgePlanMode::Editor, NovaBridgeEditorMaxPlanSteps, SchemaError))
	{
		const FString Prefix = (SchemaError.StepIndex >= 0)
			? FString::Printf(TEXT("Schema validation failed at step %d: "), SchemaError.StepIndex)
			: TEXT("Schema validation failed: ");
		SendErrorResponse(OnComplete, Prefix + SchemaError.Message, 400);
		return true;
	}

	const TArray<TSharedPtr<FJsonValue>> Steps = Body->GetArrayField(TEXT("steps"));

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

		NovaBridgeCore::FPlanCommandRouter CommandRouter;
		CommandRouter.Register(TEXT("spawn"), [this, Role, PlanId, &SpawnedInPlan](const NovaBridgeCore::FPlanStepContext& Step)
		{
			const TSharedPtr<FJsonObject> Params = Step.Params.IsValid() ? Step.Params : MakeShared<FJsonObject>();

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
				return NovaBridgeCore::MakePlanStepResult(
					Step.StepIndex,
					TEXT("error"),
					TEXT("spawn.params.class or spawn.params.type is required"));
			}

			const int32 PlanSpawnLimit = GetPlanSpawnLimit(Role);
			if (SpawnedInPlan >= PlanSpawnLimit)
			{
				return NovaBridgeCore::MakePlanStepResult(
					Step.StepIndex,
					TEXT("error"),
					FString::Printf(TEXT("Plan spawn limit reached (%d) for role '%s'"), PlanSpawnLimit, *Role));
			}

			FVector SpawnLocation = FVector::ZeroVector;
			FRotator SpawnRotation = FRotator::ZeroRotator;
			FVector SpawnScale = FVector(1.0, 1.0, 1.0);
			ParseSpawnTransformFromParams(Params, SpawnLocation, SpawnRotation, SpawnScale);
			if (!IsSpawnClassAllowedForRole(Role, ClassName))
			{
				const FString Message = FString::Printf(TEXT("Class '%s' is not allowed for role '%s'"), *ClassName, *Role);
				PushAuditEntry(TEXT("/nova/executePlan"), Step.Action, Role, TEXT("denied"), Message);
				return NovaBridgeCore::MakePlanStepResult(Step.StepIndex, TEXT("error"), Message);
			}
			if (!IsSpawnLocationInBounds(SpawnLocation))
			{
				const FString Message = TEXT("Spawn location is outside allowed bounds");
				PushAuditEntry(TEXT("/nova/executePlan"), Step.Action, Role, TEXT("denied"), Message);
				return NovaBridgeCore::MakePlanStepResult(Step.StepIndex, TEXT("error"), Message);
			}

			const int32 SpawnRateLimit = GetRouteRateLimitPerMinute(Role, TEXT("/nova/scene/spawn"));
			FString SpawnRateError;
			if (!ConsumeRateLimit(Role + TEXT("|plan.spawn"), SpawnRateLimit, SpawnRateError))
			{
				PushAuditEntry(TEXT("/nova/executePlan"), Step.Action, Role, TEXT("rate_limited"), SpawnRateError);
				return NovaBridgeCore::MakePlanStepResult(Step.StepIndex, TEXT("error"), SpawnRateError);
			}

			if (!GEditor)
			{
				return NovaBridgeCore::MakePlanStepResult(Step.StepIndex, TEXT("error"), TEXT("No editor"));
			}

			UEditorActorSubsystem* ActorSub = GEditor->GetEditorSubsystem<UEditorActorSubsystem>();
			if (!ActorSub)
			{
				return NovaBridgeCore::MakePlanStepResult(Step.StepIndex, TEXT("error"), TEXT("No EditorActorSubsystem"));
			}

			UClass* ActorClass = ResolveActorClassByName(ClassName);
			if (!ActorClass)
			{
				return NovaBridgeCore::MakePlanStepResult(
					Step.StepIndex,
					TEXT("error"),
					FString::Printf(TEXT("Class not found: %s"), *ClassName));
			}

			AActor* NewActor = ActorSub->SpawnActorFromClass(TSubclassOf<AActor>(ActorClass), SpawnLocation, SpawnRotation);
			if (!NewActor)
			{
				return NovaBridgeCore::MakePlanStepResult(Step.StepIndex, TEXT("error"), TEXT("Failed to spawn actor"));
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

			TSharedPtr<FJsonObject> StepResult = NovaBridgeCore::MakePlanStepResult(Step.StepIndex, TEXT("success"), TEXT("Spawned actor"));
			StepResult->SetStringField(TEXT("object_id"), NewActor->GetName());
			StepResult->SetStringField(TEXT("label"), NewActor->GetActorLabel());
			StepResult->SetStringField(TEXT("class"), ClassName);

			const TSharedPtr<FJsonObject> SpawnEvent = NovaBridgeCore::BuildSpawnEvent(
				TEXT("editor"),
				PlanId,
				Step.StepIndex,
				Step.Action,
				NewActor->GetName(),
				ClassName,
				NewActor->GetName(),
				NewActor->GetActorLabel());
			QueueEventObject(SpawnEvent);

			PushAuditEntry(
				TEXT("/nova/executePlan"),
				Step.Action,
				Role,
				TEXT("success"),
				FString::Printf(TEXT("Spawned actor '%s'"), *NewActor->GetActorLabel()));
			return StepResult;
		});

		CommandRouter.Register(TEXT("delete"), [this, Role, PlanId](const NovaBridgeCore::FPlanStepContext& Step)
		{
			const TSharedPtr<FJsonObject> Params = Step.Params.IsValid() ? Step.Params : MakeShared<FJsonObject>();
			FString ActorName = Params->HasTypedField<EJson::String>(TEXT("name")) ? Params->GetStringField(TEXT("name")) : FString();
			if (ActorName.IsEmpty() && Params->HasTypedField<EJson::String>(TEXT("target")))
			{
				ActorName = Params->GetStringField(TEXT("target"));
			}
			if (ActorName.IsEmpty())
			{
				return NovaBridgeCore::MakePlanStepResult(Step.StepIndex, TEXT("error"), TEXT("delete.params.name is required"));
			}

			AActor* Actor = FindActorByName(ActorName);
			if (!Actor)
			{
				return NovaBridgeCore::MakePlanStepResult(
					Step.StepIndex,
					TEXT("error"),
					FString::Printf(TEXT("Actor not found: %s"), *ActorName));
			}
			if (Actor->GetActorLabel() == TEXT("NovaBridge_SceneCapture") || Actor->GetName().Contains(TEXT("NovaBridge_SceneCapture")))
			{
				return NovaBridgeCore::MakePlanStepResult(Step.StepIndex, TEXT("error"), TEXT("Cannot delete NovaBridge_SceneCapture"));
			}

			if (UEditorActorSubsystem* ActorSub = GEditor ? GEditor->GetEditorSubsystem<UEditorActorSubsystem>() : nullptr)
			{
				ActorSub->DestroyActor(Actor);
			}
			else
			{
				Actor->Destroy();
			}

			TSharedPtr<FJsonObject> StepResult = NovaBridgeCore::MakePlanStepResult(
				Step.StepIndex,
				TEXT("success"),
				FString::Printf(TEXT("Deleted actor %s"), *ActorName));

			const TSharedPtr<FJsonObject> DeleteEvent = NovaBridgeCore::BuildDeleteEvent(
				TEXT("editor"),
				PlanId,
				Step.StepIndex,
				Step.Action,
				ActorName);
			QueueEventObject(DeleteEvent);

			PushAuditEntry(
				TEXT("/nova/executePlan"),
				Step.Action,
				Role,
				TEXT("success"),
				FString::Printf(TEXT("Deleted actor '%s'"), *ActorName));
			return StepResult;
		});

		CommandRouter.Register(TEXT("set"), [this, Role](const NovaBridgeCore::FPlanStepContext& Step)
		{
			const TSharedPtr<FJsonObject> Params = Step.Params.IsValid() ? Step.Params : MakeShared<FJsonObject>();

			FString Target = Params->HasTypedField<EJson::String>(TEXT("target")) ? Params->GetStringField(TEXT("target")) : FString();
			if (Target.IsEmpty() && Params->HasTypedField<EJson::String>(TEXT("name")))
			{
				Target = Params->GetStringField(TEXT("name"));
			}
			if (Target.IsEmpty())
			{
				return NovaBridgeCore::MakePlanStepResult(Step.StepIndex, TEXT("error"), TEXT("set.params.target is required"));
			}

			AActor* Actor = FindActorByName(Target);
			if (!Actor)
			{
				return NovaBridgeCore::MakePlanStepResult(
					Step.StepIndex,
					TEXT("error"),
					FString::Printf(TEXT("Actor not found: %s"), *Target));
			}

			if (!Params->HasTypedField<EJson::Object>(TEXT("props")))
			{
				return NovaBridgeCore::MakePlanStepResult(Step.StepIndex, TEXT("error"), TEXT("set.params.props object is required"));
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
				PushAuditEntry(TEXT("/nova/executePlan"), Step.Action, Role, TEXT("error"), SetErrorMessage);
				return NovaBridgeCore::MakePlanStepResult(Step.StepIndex, TEXT("error"), SetErrorMessage);
			}
			if (!bSetAnyProperty)
			{
				return NovaBridgeCore::MakePlanStepResult(Step.StepIndex, TEXT("error"), TEXT("No valid properties were applied"));
			}

			Actor->PostEditChange();
			PushAuditEntry(
				TEXT("/nova/executePlan"),
				Step.Action,
				Role,
				TEXT("success"),
				FString::Printf(TEXT("Updated actor '%s'"), *Target));
			return NovaBridgeCore::MakePlanStepResult(
				Step.StepIndex,
				TEXT("success"),
				FString::Printf(TEXT("Updated actor %s"), *Target));
		});

		CommandRouter.Register(TEXT("screenshot"), [this, Role](const NovaBridgeCore::FPlanStepContext& Step)
		{
			const TSharedPtr<FJsonObject> Params = Step.Params.IsValid() ? Step.Params : MakeShared<FJsonObject>();

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
				return NovaBridgeCore::MakePlanStepResult(Step.StepIndex, TEXT("error"), TEXT("Failed to initialize scene capture"));
			}

			USceneCaptureComponent2D* CaptureComp = CaptureActor->GetCaptureComponent2D();
			CaptureActor->SetActorLocation(CameraLocation);
			CaptureActor->SetActorRotation(CameraRotation);
			CaptureComp->FOVAngle = CameraFOV;
			CaptureComp->CaptureScene();

			FTextureRenderTargetResource* RTResource = RenderTarget->GameThread_GetRenderTargetResource();
			if (!RTResource)
			{
				return NovaBridgeCore::MakePlanStepResult(Step.StepIndex, TEXT("error"), TEXT("No render target resource"));
			}

			TArray<FColor> Bitmap;
			if (!RTResource->ReadPixels(Bitmap) || Bitmap.Num() == 0)
			{
				return NovaBridgeCore::MakePlanStepResult(Step.StepIndex, TEXT("error"), TEXT("Failed to read render target pixels"));
			}

			IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));
			TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);
			ImageWrapper->SetRaw(Bitmap.GetData(), Bitmap.Num() * sizeof(FColor), CaptureWidth, CaptureHeight, ERGBFormat::BGRA, 8);
			TArray64<uint8> PngData = ImageWrapper->GetCompressed(0);
			if (PngData.Num() == 0)
			{
				return NovaBridgeCore::MakePlanStepResult(Step.StepIndex, TEXT("error"), TEXT("Failed to encode PNG"));
			}

			TSharedPtr<FJsonObject> StepResult = NovaBridgeCore::MakePlanStepResult(Step.StepIndex, TEXT("success"), TEXT("Captured screenshot"));
			StepResult->SetNumberField(TEXT("width"), CaptureWidth);
			StepResult->SetNumberField(TEXT("height"), CaptureHeight);
			StepResult->SetStringField(TEXT("format"), TEXT("png"));
			if (bInlineImage)
			{
				const FString Base64 = FBase64::Encode(PngData.GetData(), PngData.Num());
				StepResult->SetStringField(TEXT("image"), Base64);
			}

			PushAuditEntry(TEXT("/nova/executePlan"), Step.Action, Role, TEXT("success"), TEXT("Captured screenshot"));
			return StepResult;
		});

		for (int32 StepIndex = 0; StepIndex < Steps.Num(); ++StepIndex)
		{
			NovaBridgeCore::FPlanStepContext StepContext;
			TSharedPtr<FJsonObject> ParseErrorResult;
			if (!NovaBridgeCore::ExtractPlanStep(Steps[StepIndex], StepIndex, StepContext, ParseErrorResult))
			{
				StepActions[StepIndex] = TEXT("unknown");
				StepResults.Add(MakeShareable(new FJsonValueObject(
					ParseErrorResult.IsValid()
						? ParseErrorResult
						: NovaBridgeCore::MakePlanStepResult(StepIndex, TEXT("error"), TEXT("Invalid plan step")))));
				continue;
			}

			const FString& Action = StepContext.Action;
			StepActions[StepIndex] = Action;

			if (!IsPlanActionAllowedForRole(Role, Action))
			{
				const FString Message = FString::Printf(TEXT("Role '%s' cannot execute action '%s'"), *Role, *Action);
				StepResults.Add(MakeShareable(new FJsonValueObject(
					NovaBridgeCore::MakePlanStepResult(StepIndex, TEXT("error"), Message))));
				PushAuditEntry(TEXT("/nova/executePlan"), Action, Role, TEXT("denied"), Message);
				continue;
			}

			TSharedPtr<FJsonObject> StepResult;
			if (!CommandRouter.HasHandler(Action))
			{
				StepResult = NovaBridgeCore::MakePlanStepResult(
					StepIndex,
					TEXT("error"),
					FString::Printf(TEXT("Unsupported action: %s"), *Action));
				PushAuditEntry(TEXT("/nova/executePlan"), Action, Role, TEXT("error"), TEXT("Unsupported action"));
			}
			else
			{
				StepResult = CommandRouter.Dispatch(StepContext);
			}

			if (!StepResult.IsValid())
			{
				StepResult = NovaBridgeCore::MakePlanStepResult(StepIndex, TEXT("error"), TEXT("Step execution failed"));
			}
			StepResults.Add(MakeShareable(new FJsonValueObject(StepResult)));
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
				? NovaBridgeCore::NormalizePlanAction(StepResultObj->GetStringField(TEXT("status")))
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
			const FString ResultAction = (ResultStepIndex >= 0 && ResultStepIndex < StepActions.Num() && !StepActions[ResultStepIndex].IsEmpty())
				? StepActions[ResultStepIndex]
				: TEXT("unknown");

			const TSharedPtr<FJsonObject> PlanStepEvent = NovaBridgeCore::BuildPlanStepEvent(TEXT("editor"), PlanId, ResultAction, StepResultObj);
			if (PlanStepEvent.IsValid())
			{
				QueueEventObject(PlanStepEvent);
			}
		}

		const TSharedPtr<FJsonObject> PlanCompleteEvent = NovaBridgeCore::BuildPlanCompleteEvent(
			TEXT("editor"),
			PlanId,
			Steps.Num(),
			SuccessCount,
			ErrorCount,
			Role);
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

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FNovaBridgeModule, NovaBridge)
