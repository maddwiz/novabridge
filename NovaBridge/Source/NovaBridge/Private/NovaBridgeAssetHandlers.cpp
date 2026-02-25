#include "NovaBridgeModule.h"

#include "Async/Async.h"
#include "AssetImportTask.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "Dom/JsonValue.h"
#include "EditorAssetLibrary.h"
#include "Factories/MaterialFactoryNew.h"
#include "IAssetTools.h"
#include "MeshDescription.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "StaticMeshAttributes.h"
#include "Engine/StaticMesh.h"
#include "Materials/Material.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/SoftObjectPath.h"

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
		if (Body && Body->HasField(TEXT("path")))
		{
			Path = Body->GetStringField(TEXT("path"));
		}
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

	const FString Type = Body->GetStringField(TEXT("type"));
	const FString Name = Body->GetStringField(TEXT("name"));
	const FString Path = Body->HasField(TEXT("path")) ? Body->GetStringField(TEXT("path")) : TEXT("/Game");

	AsyncTask(ENamedThreads::GameThread, [this, OnComplete, Type, Name, Path]()
	{
		const FString PackagePath = Path / Name;
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

	const FString Source = Body->GetStringField(TEXT("source"));
	const FString Destination = Body->GetStringField(TEXT("destination"));

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

	const FString AssetPath = Body->GetStringField(TEXT("path"));

	AsyncTask(ENamedThreads::GameThread, [this, OnComplete, AssetPath]()
	{
		const bool Success = UEditorAssetLibrary::DeleteAsset(AssetPath);
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

	const FString Source = Body->GetStringField(TEXT("source"));
	const FString Destination = Body->GetStringField(TEXT("destination"));

	AsyncTask(ENamedThreads::GameThread, [this, OnComplete, Source, Destination]()
	{
		const bool Success = UEditorAssetLibrary::RenameAsset(Source, Destination);
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
		if (Body)
		{
			AssetPath = Body->GetStringField(TEXT("path"));
		}
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

	const FString FilePath = Body->GetStringField(TEXT("file_path"));
	const FString AssetName = Body->HasField(TEXT("asset_name")) ? Body->GetStringField(TEXT("asset_name")) : TEXT("");
	const FString Destination = Body->HasField(TEXT("destination")) ? Body->GetStringField(TEXT("destination")) : TEXT("/Game");
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
		FString ObjContent;
		if (!FFileHelper::LoadFileToString(ObjContent, *FilePath))
		{
			SendErrorResponse(OnComplete, TEXT("Failed to read OBJ file"));
			return;
		}

		TArray<FVector> Positions;
		TArray<FVector2D> UVs;
		TArray<FVector> Normals;

		struct ObjFaceVert
		{
			int32 PosIdx;
			int32 UVIdx;
			int32 NormIdx;
		};
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

		const FString MeshName = AssetName.IsEmpty() ? FPaths::GetBaseFilename(FilePath) : AssetName;
		const FString PackagePath = Destination / MeshName;
		UPackage* Package = CreatePackage(*PackagePath);
		UStaticMesh* StaticMesh = NewObject<UStaticMesh>(Package, FName(*MeshName), RF_Public | RF_Standalone);

		StaticMesh->GetStaticMaterials().Add(FStaticMaterial());

		FMeshDescription MeshDesc;
		FStaticMeshAttributes Attributes(MeshDesc);
		Attributes.Register();

		TArray<FVector> ExpandedPositions;
		TArray<FVector2D> ExpandedUVs;
		TArray<FVector> ExpandedNormals;
		int32 VertIdx = 0;
		for (const auto& Face : Faces)
		{
			for (int32 i = 1; i < Face.Num() - 1; i++)
			{
				const ObjFaceVert* FVerts[3] = { &Face[0], &Face[i], &Face[i + 1] };
				for (int32 j = 0; j < 3; j++)
				{
					const ObjFaceVert& FV = *FVerts[j];
					if (FV.PosIdx >= 0 && FV.PosIdx < Positions.Num())
					{
						const FVector Pos = Positions[FV.PosIdx];
						ExpandedPositions.Add(FVector(Pos.X * ImportScale, -Pos.Y * ImportScale, Pos.Z * ImportScale));
					}
					else
					{
						ExpandedPositions.Add(FVector::ZeroVector);
					}

					if (FV.UVIdx >= 0 && FV.UVIdx < UVs.Num())
					{
						const FVector2D UV = UVs[FV.UVIdx];
						ExpandedUVs.Add(FVector2D(UV.X, 1.0f - UV.Y));
					}
					else
					{
						ExpandedUVs.Add(FVector2D(0, 0));
					}

					if (FV.NormIdx >= 0 && FV.NormIdx < Normals.Num())
					{
						const FVector N = Normals[FV.NormIdx];
						ExpandedNormals.Add(FVector(N.X, -N.Y, N.Z));
					}
					else
					{
						ExpandedNormals.Add(FVector(0, 0, 1));
					}

					VertIdx++;
				}
			}
		}

		const int32 NumVerts = ExpandedPositions.Num();
		const int32 NumTris = NumVerts / 3;
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

		FAssetRegistryModule::AssetCreated(StaticMesh);
		Package->MarkPackageDirty();
		const FString PackageFileName = FPackageName::LongPackageNameToFilename(PackagePath, FPackageName::GetAssetPackageExtension());
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
