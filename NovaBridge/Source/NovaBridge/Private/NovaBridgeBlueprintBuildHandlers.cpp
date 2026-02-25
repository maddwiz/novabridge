#include "NovaBridgeModule.h"

#include "Async/Async.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/ActorComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "GameFramework/Actor.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "UObject/Package.h"

bool FNovaBridgeModule::HandleBlueprintCreate(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	TSharedPtr<FJsonObject> Body = ParseRequestBody(Request);
	if (!Body)
	{
		SendErrorResponse(OnComplete, TEXT("Invalid JSON body"));
		return true;
	}

	const FString Name = Body->GetStringField(TEXT("name"));
	const FString Path = Body->HasField(TEXT("path")) ? Body->GetStringField(TEXT("path")) : TEXT("/Game");
	const FString ParentClass = Body->HasField(TEXT("parent_class")) ? Body->GetStringField(TEXT("parent_class")) : TEXT("Actor");

	AsyncTask(ENamedThreads::GameThread, [this, OnComplete, Name, Path, ParentClass]()
	{
		UClass* Parent = AActor::StaticClass();
		if (ParentClass != TEXT("Actor"))
		{
			UClass* Found = FindObject<UClass>(nullptr, *ParentClass);
			if (!Found)
			{
				Found = LoadClass<UObject>(nullptr, *ParentClass);
			}
			if (Found)
			{
				Parent = Found;
			}
		}

		const FString PackagePath = Path / Name;
		UPackage* Package = CreatePackage(*PackagePath);

		UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(
			Parent,
			Package,
			FName(*Name),
			BPTYPE_Normal,
			UBlueprint::StaticClass(),
			UBlueprintGeneratedClass::StaticClass());
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

	const FString BlueprintPath = Body->GetStringField(TEXT("blueprint"));
	const FString ComponentClass = Body->GetStringField(TEXT("component_class"));
	const FString ComponentName = Body->HasField(TEXT("component_name")) ? Body->GetStringField(TEXT("component_name")) : TEXT("");

	AsyncTask(ENamedThreads::GameThread, [this, OnComplete, BlueprintPath, ComponentClass, ComponentName]()
	{
		UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *BlueprintPath);
		if (!Blueprint)
		{
			SendErrorResponse(OnComplete, TEXT("Blueprint not found"), 404);
			return;
		}

		UClass* CompClass = FindObject<UClass>(nullptr, *ComponentClass);
		if (!CompClass)
		{
			CompClass = LoadClass<UActorComponent>(nullptr, *ComponentClass);
		}
		if (!CompClass && ComponentClass == TEXT("StaticMeshComponent"))
		{
			CompClass = UStaticMeshComponent::StaticClass();
		}

		if (!CompClass)
		{
			SendErrorResponse(OnComplete, FString::Printf(TEXT("Component class not found: %s"), *ComponentClass));
			return;
		}

		const FName CompName = ComponentName.IsEmpty() ? FName(*CompClass->GetName()) : FName(*ComponentName);
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

	const FString BlueprintPath = Body->GetStringField(TEXT("blueprint"));

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

bool FNovaBridgeModule::HandleBuildLighting(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	(void)Request;
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

	const FString Command = Body->GetStringField(TEXT("command"));

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
