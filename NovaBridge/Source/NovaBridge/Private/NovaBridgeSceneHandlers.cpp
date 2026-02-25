#include "NovaBridgeModule.h"

#include "Async/Async.h"
#include "Components/ActorComponent.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "UObject/UnrealType.h"

AActor* FindActorByName(const FString& Name);
TSharedPtr<FJsonObject> ActorToJson(AActor* Actor);

bool FNovaBridgeModule::HandleSceneList(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	(void)Request;
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
			ActorArray.Add(MakeShared<FJsonValueObject>(ActorToJson(*It)));
		}

		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetArrayField(TEXT("actors"), ActorArray);
		Result->SetNumberField(TEXT("count"), ActorArray.Num());
		Result->SetStringField(TEXT("level"), World->GetMapName());
		SendJsonResponse(OnComplete, Result);
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
			const FVector Loc(LocObj->GetNumberField(TEXT("x")), LocObj->GetNumberField(TEXT("y")), LocObj->GetNumberField(TEXT("z")));
			Actor->SetActorLocation(Loc);
		}
		if (RotObj)
		{
			const FRotator Rot(RotObj->GetNumberField(TEXT("pitch")), RotObj->GetNumberField(TEXT("yaw")), RotObj->GetNumberField(TEXT("roll")));
			Actor->SetActorRotation(Rot);
		}
		if (ScaleObj)
		{
			const FVector Scale(ScaleObj->GetNumberField(TEXT("x")), ScaleObj->GetNumberField(TEXT("y")), ScaleObj->GetNumberField(TEXT("z")));
			Actor->SetActorScale3D(Scale);
		}

		SendJsonResponse(OnComplete, ActorToJson(Actor));
	});
	return true;
}

bool FNovaBridgeModule::HandleSceneGet(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	FString ActorName;
	if (Request.QueryParams.Contains(TEXT("name")))
	{
		ActorName = Request.QueryParams[TEXT("name")];
	}
	else
	{
		TSharedPtr<FJsonObject> Body = ParseRequestBody(Request);
		if (Body)
		{
			ActorName = Body->GetStringField(TEXT("name"));
		}
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

		TSharedPtr<FJsonObject> Props = MakeShared<FJsonObject>();
		for (TFieldIterator<FProperty> PropIt(Actor->GetClass()); PropIt; ++PropIt)
		{
			FProperty* Prop = *PropIt;
			if (!Prop->HasAnyPropertyFlags(CPF_Edit | CPF_BlueprintVisible))
			{
				continue;
			}

			FString ValueStr;
			void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Actor);
			Prop->ExportTextItem_Direct(ValueStr, ValuePtr, nullptr, Actor, PPF_None);
			Props->SetStringField(Prop->GetName(), ValueStr);
		}
		Result->SetObjectField(TEXT("properties"), Props);

		TArray<TSharedPtr<FJsonValue>> Components;
		for (UActorComponent* Comp : Actor->GetComponents())
		{
			TSharedPtr<FJsonObject> CompObj = MakeShared<FJsonObject>();
			CompObj->SetStringField(TEXT("name"), Comp->GetName());
			CompObj->SetStringField(TEXT("class"), Comp->GetClass()->GetName());

			TSharedPtr<FJsonObject> CompProps = MakeShared<FJsonObject>();
			int32 PropCount = 0;
			for (TFieldIterator<FProperty> PropIt(Comp->GetClass()); PropIt; ++PropIt)
			{
				FProperty* Prop = *PropIt;
				if (!Prop->HasAnyPropertyFlags(CPF_Edit | CPF_BlueprintVisible) || PropCount >= 30)
				{
					continue;
				}

				FString ValueStr;
				void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Comp);
				Prop->ExportTextItem_Direct(ValueStr, ValuePtr, nullptr, Comp, PPF_None);
				if (ValueStr.Len() < 200)
				{
					CompProps->SetStringField(Prop->GetName(), ValueStr);
					PropCount++;
				}
			}
			CompObj->SetObjectField(TEXT("properties"), CompProps);
			CompObj->SetStringField(TEXT("set_property_prefix"), Comp->GetName());
			Components.Add(MakeShared<FJsonValueObject>(CompObj));
		}
		Result->SetArrayField(TEXT("components"), Components);

		SendJsonResponse(OnComplete, Result);
	});
	return true;
}
