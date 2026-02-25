#include "NovaBridgeModule.h"
#include "NovaBridgeCapabilityRegistry.h"
#include "NovaBridgeCoreTypes.h"
#include "NovaBridgeHttpUtils.h"
#include "NovaBridgePolicy.h"
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
#include "INetworkingWebSocket.h"
#include "IWebSocketServer.h"
#endif

#if NOVABRIDGE_WITH_PCG
#include "PCGGraph.h"
#include "PCGComponent.h"
#include "PCGVolume.h"
#endif

#define LOCTEXT_NAMESPACE "FNovaBridgeModule"

DEFINE_LOG_CATEGORY(LogNovaBridge);

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
using NovaBridgeCore::NormalizeEventType;

struct FNovaBridgeRateBucket
{
	double WindowStartSec = 0.0;
	int32 Count = 0;
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

const FString& GetNovaBridgeDefaultRole()
{
	return NovaBridgeDefaultRole;
}

int32 GetNovaBridgeEditorMaxPlanSteps()
{
	return NovaBridgeEditorMaxPlanSteps;
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

bool IsRouteAllowedForRole(const FString& Role, const FString& RoutePath, EHttpServerRequestVerbs Verb)
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

int32 GetRouteRateLimitPerMinute(const FString& Role, const FString& RoutePath)
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

bool ConsumeRateLimit(const FString& BucketKey, int32 LimitPerMinute, FString& OutError)
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

const TArray<FString>& SupportedEventTypes()
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

void GetPendingEventSnapshot(int32& OutPendingEvents, TArray<FString>& OutPendingTypes)
{
	FScopeLock EventLock(&NovaBridgeEventQueueMutex);
	OutPendingEvents = NovaBridgePendingEventPayloads.Num();
	OutPendingTypes = NovaBridgePendingEventTypes;
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

TArray<FNovaBridgeAuditEntry> GetAuditTrailSnapshot()
{
	FScopeLock Lock(&NovaBridgeAuditMutex);
	return NovaBridgeAuditTrail;
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

int32 GetPlanSpawnLimit(const FString& Role)
{
	return NovaBridgeCore::GetEditorPlanSpawnLimit(Role);
}

bool IsPlanActionAllowedForRole(const FString& Role, const FString& Action)
{
	return NovaBridgeCore::IsEditorPlanActionAllowedForRole(Role, Action);
}

void RegisterEditorCapabilities(uint32 InEventWsPort);

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

bool JsonValueToVector(const TSharedPtr<FJsonValue>& Value, FVector& OutVector)
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

bool JsonValueToRotator(const TSharedPtr<FJsonValue>& Value, FRotator& OutRotator)
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

bool JsonValueToImportText(const TSharedPtr<FJsonValue>& Value, FString& OutText)
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

void ParseSpawnTransformFromParams(const TSharedPtr<FJsonObject>& Params, FVector& OutLocation, FRotator& OutRotation, FVector& OutScale)
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

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FNovaBridgeModule, NovaBridge)
