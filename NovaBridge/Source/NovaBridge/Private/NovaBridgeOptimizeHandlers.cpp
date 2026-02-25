#include "NovaBridgeModule.h"
#include "NovaBridgeEditorInternals.h"

#include "Async/Async.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Editor.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "EngineUtils.h"
#include "Modules/ModuleManager.h"
#include "PhysicsEngine/AggregateGeom.h"
#include "PhysicsEngine/BodySetup.h"

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
			SpotLights += It->FindComponentByClass<USpotLightComponent>() ? 1 : 0;
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
