#include "NovaBridgeModule.h"

#include "Async/Async.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Factories/MaterialFactoryNew.h"
#include "Factories/MaterialInstanceConstantFactoryNew.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionConstant4Vector.h"
#include "Materials/MaterialInstanceConstant.h"
#include "UObject/Package.h"

bool FNovaBridgeModule::HandleMaterialCreate(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	TSharedPtr<FJsonObject> Body = ParseRequestBody(Request);
	if (!Body)
	{
		SendErrorResponse(OnComplete, TEXT("Invalid JSON body"));
		return true;
	}

	const FString Name = Body->GetStringField(TEXT("name"));
	const FString Path = Body->HasField(TEXT("path")) ? Body->GetStringField(TEXT("path")) : TEXT("/Game");

	AsyncTask(ENamedThreads::GameThread, [this, OnComplete, Body, Name, Path]()
	{
		const FString PackagePath = Path / Name;
		UPackage* Package = CreatePackage(*PackagePath);

		UMaterialFactoryNew* Factory = NewObject<UMaterialFactoryNew>();
		UMaterial* Material = Cast<UMaterial>(
			Factory->FactoryCreateNew(UMaterial::StaticClass(), Package, FName(*Name), RF_Public | RF_Standalone, nullptr, GWarn));

		if (!Material)
		{
			SendErrorResponse(OnComplete, TEXT("Failed to create material"), 500);
			return;
		}

		if (Body->HasField(TEXT("color")))
		{
			TSharedPtr<FJsonObject> ColorObj = Body->GetObjectField(TEXT("color"));
			const float R = ColorObj->GetNumberField(TEXT("r"));
			const float G = ColorObj->GetNumberField(TEXT("g"));
			const float B = ColorObj->GetNumberField(TEXT("b"));
			const float A = ColorObj->HasField(TEXT("a")) ? ColorObj->GetNumberField(TEXT("a")) : 1.0f;

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

	const FString MaterialPath = Body->GetStringField(TEXT("path"));
	const FString ParamName = Body->GetStringField(TEXT("param"));
	const FString ParamType = Body->HasField(TEXT("type")) ? Body->GetStringField(TEXT("type")) : TEXT("scalar");

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
			const float Value = Body->GetNumberField(TEXT("value"));
			MatInst->SetScalarParameterValueEditorOnly(FName(*ParamName), Value);
		}
		else if (ParamType == TEXT("vector"))
		{
			TSharedPtr<FJsonObject> V = Body->GetObjectField(TEXT("value"));
			const FLinearColor Color(
				V->GetNumberField(TEXT("r")),
				V->GetNumberField(TEXT("g")),
				V->GetNumberField(TEXT("b")),
				V->HasField(TEXT("a")) ? V->GetNumberField(TEXT("a")) : 1.0f);
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
		if (Body)
		{
			MaterialPath = Body->GetStringField(TEXT("path"));
		}
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

	const FString ParentPath = Body->GetStringField(TEXT("parent"));
	const FString Name = Body->GetStringField(TEXT("name"));
	const FString Path = Body->HasField(TEXT("path")) ? Body->GetStringField(TEXT("path")) : TEXT("/Game");

	AsyncTask(ENamedThreads::GameThread, [this, OnComplete, ParentPath, Name, Path]()
	{
		UMaterialInterface* Parent = LoadObject<UMaterialInterface>(nullptr, *ParentPath);
		if (!Parent)
		{
			SendErrorResponse(OnComplete, TEXT("Parent material not found"), 404);
			return;
		}

		const FString PackagePath = Path / Name;
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
