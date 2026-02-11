#include "NovaBridgeModule.h"
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

// Image
#include "IImageWrapperModule.h"
#include "IImageWrapper.h"
#include "Misc/Base64.h"
#include "ShowFlags.h"

#define LOCTEXT_NAMESPACE "FNovaBridgeModule"

DEFINE_LOG_CATEGORY_STATIC(LogNovaBridge, Log, All);

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
		Out.LeftChopInline(1, false);
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

void FNovaBridgeModule::StartupModule()
{
	UE_LOG(LogNovaBridge, Log, TEXT("NovaBridge starting up..."));
	StartHttpServer();
}

void FNovaBridgeModule::ShutdownModule()
{
	UE_LOG(LogNovaBridge, Log, TEXT("NovaBridge shutting down..."));
	CleanupCapture();
	StopHttpServer();
}

void FNovaBridgeModule::StartHttpServer()
{
	ApiRouteCount = 0;
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

	HttpRouter = FHttpServerModule::Get().GetHttpRouter(HttpPort);
	if (!HttpRouter)
	{
		UE_LOG(LogNovaBridge, Error, TEXT("Failed to get HTTP router on port %d"), HttpPort);
		return;
	}

	auto Bind = [this](const TCHAR* Path, EHttpServerRequestVerbs Verbs, bool (FNovaBridgeModule::*Handler)(const FHttpServerRequest&, const FHttpResultCallback&))
	{
		ApiRouteCount++;
		RouteHandles.Add(HttpRouter->BindRoute(
			FHttpPath(Path), Verbs,
			FHttpRequestHandler::CreateLambda([this, Handler](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete) -> bool
				{
					UE_LOG(LogNovaBridge, Verbose, TEXT("[%s] %s %s"),
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

	// Health check
	Bind(TEXT("/nova/health"), EHttpServerRequestVerbs::VERB_GET, &FNovaBridgeModule::HandleHealth);
	Bind(TEXT("/nova/project/info"), EHttpServerRequestVerbs::VERB_GET, &FNovaBridgeModule::HandleProjectInfo);

	// Scene
	Bind(TEXT("/nova/scene/list"), EHttpServerRequestVerbs::VERB_GET, &FNovaBridgeModule::HandleSceneList);
	Bind(TEXT("/nova/scene/spawn"), EHttpServerRequestVerbs::VERB_POST, &FNovaBridgeModule::HandleSceneSpawn);
	Bind(TEXT("/nova/scene/delete"), EHttpServerRequestVerbs::VERB_POST, &FNovaBridgeModule::HandleSceneDelete);
	Bind(TEXT("/nova/scene/transform"), EHttpServerRequestVerbs::VERB_POST, &FNovaBridgeModule::HandleSceneTransform);
	Bind(TEXT("/nova/scene/get"), EHttpServerRequestVerbs::VERB_GET | EHttpServerRequestVerbs::VERB_POST, &FNovaBridgeModule::HandleSceneGet);
	Bind(TEXT("/nova/scene/set-property"), EHttpServerRequestVerbs::VERB_POST, &FNovaBridgeModule::HandleSceneSetProperty);

	// Assets
	Bind(TEXT("/nova/asset/list"), EHttpServerRequestVerbs::VERB_GET | EHttpServerRequestVerbs::VERB_POST, &FNovaBridgeModule::HandleAssetList);
	Bind(TEXT("/nova/asset/create"), EHttpServerRequestVerbs::VERB_POST, &FNovaBridgeModule::HandleAssetCreate);
	Bind(TEXT("/nova/asset/duplicate"), EHttpServerRequestVerbs::VERB_POST, &FNovaBridgeModule::HandleAssetDuplicate);
	Bind(TEXT("/nova/asset/delete"), EHttpServerRequestVerbs::VERB_POST, &FNovaBridgeModule::HandleAssetDelete);
	Bind(TEXT("/nova/asset/rename"), EHttpServerRequestVerbs::VERB_POST, &FNovaBridgeModule::HandleAssetRename);
	Bind(TEXT("/nova/asset/info"), EHttpServerRequestVerbs::VERB_GET | EHttpServerRequestVerbs::VERB_POST, &FNovaBridgeModule::HandleAssetInfo);
	Bind(TEXT("/nova/asset/import"), EHttpServerRequestVerbs::VERB_POST, &FNovaBridgeModule::HandleAssetImport);

	// Mesh
	Bind(TEXT("/nova/mesh/create"), EHttpServerRequestVerbs::VERB_POST, &FNovaBridgeModule::HandleMeshCreate);
	Bind(TEXT("/nova/mesh/get"), EHttpServerRequestVerbs::VERB_GET | EHttpServerRequestVerbs::VERB_POST, &FNovaBridgeModule::HandleMeshGet);
	Bind(TEXT("/nova/mesh/primitive"), EHttpServerRequestVerbs::VERB_POST, &FNovaBridgeModule::HandleMeshPrimitive);

	// Material
	Bind(TEXT("/nova/material/create"), EHttpServerRequestVerbs::VERB_POST, &FNovaBridgeModule::HandleMaterialCreate);
	Bind(TEXT("/nova/material/set-param"), EHttpServerRequestVerbs::VERB_POST, &FNovaBridgeModule::HandleMaterialSetParam);
	Bind(TEXT("/nova/material/get"), EHttpServerRequestVerbs::VERB_GET | EHttpServerRequestVerbs::VERB_POST, &FNovaBridgeModule::HandleMaterialGet);
	Bind(TEXT("/nova/material/create-instance"), EHttpServerRequestVerbs::VERB_POST, &FNovaBridgeModule::HandleMaterialCreateInstance);

	// Viewport
	Bind(TEXT("/nova/viewport/screenshot"), EHttpServerRequestVerbs::VERB_GET, &FNovaBridgeModule::HandleViewportScreenshot);
	Bind(TEXT("/nova/viewport/camera/set"), EHttpServerRequestVerbs::VERB_POST, &FNovaBridgeModule::HandleViewportSetCamera);
	Bind(TEXT("/nova/viewport/camera/get"), EHttpServerRequestVerbs::VERB_GET, &FNovaBridgeModule::HandleViewportGetCamera);

	// Blueprint
	Bind(TEXT("/nova/blueprint/create"), EHttpServerRequestVerbs::VERB_POST, &FNovaBridgeModule::HandleBlueprintCreate);
	Bind(TEXT("/nova/blueprint/add-component"), EHttpServerRequestVerbs::VERB_POST, &FNovaBridgeModule::HandleBlueprintAddComponent);
	Bind(TEXT("/nova/blueprint/compile"), EHttpServerRequestVerbs::VERB_POST, &FNovaBridgeModule::HandleBlueprintCompile);

	// Build
	Bind(TEXT("/nova/build/lighting"), EHttpServerRequestVerbs::VERB_POST, &FNovaBridgeModule::HandleBuildLighting);
	Bind(TEXT("/nova/exec/command"), EHttpServerRequestVerbs::VERB_POST, &FNovaBridgeModule::HandleExecCommand);

	FHttpServerModule::Get().StartAllListeners();
	UE_LOG(LogNovaBridge, Log, TEXT("NovaBridge HTTP server started on port %d with %d API routes"), HttpPort, ApiRouteCount);
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
	Response->Headers.FindOrAdd(TEXT("Access-Control-Allow-Headers")).Add(TEXT("Content-Type, Authorization"));
	Response->Headers.FindOrAdd(TEXT("Access-Control-Max-Age")).Add(TEXT("86400"));
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
	TSharedPtr<FJsonObject> JsonObj = MakeShareable(new FJsonObject);
	JsonObj->SetStringField(TEXT("status"), TEXT("ok"));
	JsonObj->SetStringField(TEXT("engine"), TEXT("UnrealEngine"));
	JsonObj->SetNumberField(TEXT("port"), HttpPort);
	JsonObj->SetNumberField(TEXT("routes"), ApiRouteCount);
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

	AsyncTask(ENamedThreads::GameThread, [this, OnComplete, ClassName, X, Y, Z, Pitch, Yaw, Roll, Label]()
	{
		static int32 SpawnCount = 0;
		static double SpawnWindowStart = 0.0;
		const double Now = FPlatformTime::Seconds();
		if (Now - SpawnWindowStart > 60.0)
		{
			SpawnWindowStart = Now;
			SpawnCount = 0;
		}
		if (++SpawnCount > 100)
		{
			SendErrorResponse(OnComplete, TEXT("Rate limit: max 100 scene spawns per minute"), 429);
			return;
		}

		if (!GEditor)
		{
			SendErrorResponse(OnComplete, TEXT("No editor"), 500);
			return;
		}

		UEditorActorSubsystem* ActorSub = GEditor->GetEditorSubsystem<UEditorActorSubsystem>();
		if (!ActorSub)
		{
			SendErrorResponse(OnComplete, TEXT("No EditorActorSubsystem"), 500);
			return;
		}

		// Try to find the class
		UClass* ActorClass = FindObject<UClass>(nullptr, *ClassName);
		if (!ActorClass)
		{
			ActorClass = LoadClass<AActor>(nullptr, *ClassName);
		}
		// Common class name shortcuts
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
			// Try /Script/Engine path as last resort
			if (!ActorClass) ActorClass = FindObject<UClass>(nullptr, *(FString(TEXT("/Script/Engine.")) + ClassName));
		}

		if (!ActorClass)
		{
			SendErrorResponse(OnComplete, FString::Printf(TEXT("Class not found: %s"), *ClassName));
			return;
		}

		FVector Location(X, Y, Z);
		FRotator Rotation(Pitch, Yaw, Roll);

		AActor* NewActor = ActorSub->SpawnActorFromClass(TSubclassOf<AActor>(ActorClass), Location, Rotation);
		if (!NewActor)
		{
			SendErrorResponse(OnComplete, TEXT("Failed to spawn actor"), 500);
			return;
		}

		if (!Label.IsEmpty())
		{
			NewActor->SetActorLabel(Label);
		}

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

	AsyncTask(ENamedThreads::GameThread, [this, OnComplete, ActorName]()
	{
		AActor* Actor = FindActorByName(ActorName);
		if (!Actor)
		{
			SendErrorResponse(OnComplete, FString::Printf(TEXT("Actor not found: %s"), *ActorName), 404);
			return;
		}

		// Protect the NovaBridge scene capture actor from deletion
		if (Actor->GetActorLabel() == TEXT("NovaBridge_SceneCapture") || Actor->GetName().Contains(TEXT("NovaBridge_SceneCapture")))
		{
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

	AsyncTask(ENamedThreads::GameThread, [this, OnComplete, ActorName, PropertyName, Value]()
	{
		AActor* Actor = FindActorByName(ActorName);
		if (!Actor)
		{
			SendErrorResponse(OnComplete, FString::Printf(TEXT("Actor not found: %s"), *ActorName), 404);
			return;
		}

		// Support "ComponentName.PropertyName" syntax for component properties
		FString CompName, PropName;
		UObject* TargetObj = Actor;
		UActorComponent* FoundComp = nullptr;
		if (PropertyName.Split(TEXT("."), &CompName, &PropName))
		{
			// Search actor's components for a matching component
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

						// Class names usually start with U (e.g. UPointLightComponent); allow matching without it.
						if (ClassName.StartsWith(TEXT("U")))
						{
							ClassName.RightChopInline(1, false);
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

				// Special handling: Material assignment on mesh components
				// Supports "ComponentName.Material" or "ComponentName.Material[N]"
				if (PropName.StartsWith(TEXT("Material")))
				{
					UPrimitiveComponent* PrimComp = Cast<UPrimitiveComponent>(FoundComp);
					if (PrimComp)
					{
						int32 SlotIndex = 0;
						// Parse optional index: Material[1], Material[2], etc.
						int32 BracketIdx;
						if (PropName.FindChar('[', BracketIdx))
						{
							FString IdxStr = PropName.Mid(BracketIdx + 1).LeftChop(1);
							SlotIndex = FCString::Atoi(*IdxStr);
						}

						// Load the material
						UMaterialInterface* Mat = LoadObject<UMaterialInterface>(nullptr, *Value);
						if (!Mat)
						{
							SendErrorResponse(OnComplete, FString::Printf(TEXT("Material not found: %s"), *Value));
							return;
						}

						PrimComp->SetMaterial(SlotIndex, Mat);
						PrimComp->MarkRenderStateDirty();
						Actor->PostEditChange();
						SendOkResponse(OnComplete);
						return;
					}
				}
			}
			else
			{
				// Component not found, try as a flat property name on the actor
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
			// Try case-insensitive search
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
			SendErrorResponse(OnComplete, FString::Printf(TEXT("Property not found: %s"), *PropertyName), 404);
			return;
		}

		void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(TargetObj);
		if (!Prop->ImportText_Direct(*Value, ValuePtr, TargetObj, PPF_None))
		{
			SendErrorResponse(OnComplete, FString::Printf(TEXT("Failed to set property: %s"), *PropertyName));
			return;
		}

		if (UActorComponent* Comp = Cast<UActorComponent>(TargetObj))
		{
			Comp->MarkRenderStateDirty();
		}
		Actor->PostEditChange();
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
			AssetObj->SetStringField(TEXT("path"), Asset.ObjectPath.ToString());
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
		bool Success = UEditorAssetLibrary::DuplicateAsset(Source, Destination);
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
		FAssetData AssetData = AssetRegistryModule.Get().GetAssetByObjectPath(FName(*AssetPath));

		if (!AssetData.IsValid())
		{
			SendErrorResponse(OnComplete, TEXT("Asset not found"), 404);
			return;
		}

		TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
		Result->SetStringField(TEXT("name"), AssetData.AssetName.ToString());
		Result->SetStringField(TEXT("path"), AssetData.ObjectPath.ToString());
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
	// Already valid?
	if (CaptureActor.IsValid() && RenderTarget.IsValid())
		return;

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
		return;

	// Create render target
	UTextureRenderTarget2D* RT = NewObject<UTextureRenderTarget2D>(GetTransientPackage(), NAME_None, RF_Transient);
	RT->InitAutoFormat(CaptureWidth, CaptureHeight);
	RT->UpdateResourceImmediate(true);
	RenderTarget = RT;

	// Spawn scene capture actor
	FActorSpawnParameters SpawnParams;
	SpawnParams.Name = FName(TEXT("NovaBridge_SceneCapture"));
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ASceneCapture2D* Capture = World->SpawnActor<ASceneCapture2D>(FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
	if (Capture)
	{
		Capture->SetActorLabel(TEXT("NovaBridge_SceneCapture"));
		Capture->SetActorHiddenInGame(true);

		USceneCaptureComponent2D* CaptureComp = Capture->GetCaptureComponent2D();
		CaptureComp->TextureTarget = RT;
		CaptureComp->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
		CaptureComp->bCaptureEveryFrame = false;
		CaptureComp->bCaptureOnMovement = false;
		CaptureComp->FOVAngle = CameraFOV;

		// Position at default camera location
		Capture->SetActorLocation(CameraLocation);
		Capture->SetActorRotation(CameraRotation);

		CaptureActor = Capture;
		UE_LOG(LogNovaBridge, Log, TEXT("Scene capture created: %dx%d"), CaptureWidth, CaptureHeight);
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

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FNovaBridgeModule, NovaBridge)
