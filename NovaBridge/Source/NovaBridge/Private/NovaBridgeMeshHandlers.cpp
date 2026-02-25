#include "NovaBridgeModule.h"

#include "Async/Async.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Dom/JsonValue.h"
#include "Engine/StaticMesh.h"
#include "Math/UnrealMathUtility.h"
#include "MeshDescription.h"
#include "MeshDescriptionBuilder.h"
#include "StaticMeshAttributes.h"
#include "UObject/Package.h"

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
