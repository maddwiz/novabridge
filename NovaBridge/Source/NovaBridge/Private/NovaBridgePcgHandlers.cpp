#include "NovaBridgeModule.h"

#include "Async/Async.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Editor.h"
#include "Engine/World.h"

#if NOVABRIDGE_WITH_PCG
#include "PCGComponent.h"
#include "PCGGraph.h"
#include "PCGVolume.h"
#endif

AActor* FindActorByName(const FString& Name);
TSharedPtr<FJsonObject> ActorToJson(AActor* Actor);

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
