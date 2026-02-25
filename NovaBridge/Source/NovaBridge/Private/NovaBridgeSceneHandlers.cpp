#include "NovaBridgeModule.h"
#include "NovaBridgeEditorInternals.h"

#include "Async/Async.h"
#include "Components/ActorComponent.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "Subsystems/EditorActorSubsystem.h"
#include "UObject/UnrealType.h"

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
	const FString Role = ResolveRoleFromRequest(Request);
	const FVector RequestedLocation(X, Y, Z);
	if (!IsSpawnClassAllowedForRole(Role, ClassName))
	{
		PushAuditEntry(TEXT("/nova/scene/spawn"), TEXT("scene.spawn"), Role, TEXT("denied"),
			FString::Printf(TEXT("Class not allowed for role: %s"), *ClassName));
		SendErrorResponse(OnComplete, FString::Printf(TEXT("Class '%s' is not allowed for role '%s'"), *ClassName, *Role), 403);
		return true;
	}
	if (!IsSpawnLocationInBounds(RequestedLocation))
	{
		PushAuditEntry(TEXT("/nova/scene/spawn"), TEXT("scene.spawn"), Role, TEXT("denied"), TEXT("Spawn location out of bounds"));
		SendErrorResponse(OnComplete, TEXT("Spawn location is outside allowed bounds"), 403);
		return true;
	}

	AsyncTask(ENamedThreads::GameThread, [this, OnComplete, ClassName, X, Y, Z, Pitch, Yaw, Roll, Label, Role]()
	{
		if (!GEditor)
		{
			PushAuditEntry(TEXT("/nova/scene/spawn"), TEXT("scene.spawn"), Role, TEXT("error"), TEXT("No editor"));
			SendErrorResponse(OnComplete, TEXT("No editor"), 500);
			return;
		}

		UEditorActorSubsystem* ActorSub = GEditor->GetEditorSubsystem<UEditorActorSubsystem>();
		if (!ActorSub)
		{
			PushAuditEntry(TEXT("/nova/scene/spawn"), TEXT("scene.spawn"), Role, TEXT("error"), TEXT("No EditorActorSubsystem"));
			SendErrorResponse(OnComplete, TEXT("No EditorActorSubsystem"), 500);
			return;
		}

		UClass* ActorClass = ResolveActorClassByName(ClassName);

		if (!ActorClass)
		{
			PushAuditEntry(TEXT("/nova/scene/spawn"), TEXT("scene.spawn"), Role, TEXT("error"),
				FString::Printf(TEXT("Class not found: %s"), *ClassName));
			SendErrorResponse(OnComplete, FString::Printf(TEXT("Class not found: %s"), *ClassName));
			return;
		}

		FVector Location(X, Y, Z);
		FRotator Rotation(Pitch, Yaw, Roll);

		AActor* NewActor = ActorSub->SpawnActorFromClass(TSubclassOf<AActor>(ActorClass), Location, Rotation);
		if (!NewActor)
		{
			PushAuditEntry(TEXT("/nova/scene/spawn"), TEXT("scene.spawn"), Role, TEXT("error"), TEXT("Failed to spawn actor"));
			SendErrorResponse(OnComplete, TEXT("Failed to spawn actor"), 500);
			return;
		}

		if (!Label.IsEmpty())
		{
			NewActor->SetActorLabel(Label);
		}
		FNovaBridgeUndoEntry UndoEntry;
		UndoEntry.Action = TEXT("spawn");
		UndoEntry.ActorName = NewActor->GetName();
		UndoEntry.ActorLabel = NewActor->GetActorLabel();
		PushUndoEntry(UndoEntry);
		PushAuditEntry(TEXT("/nova/scene/spawn"), TEXT("scene.spawn"), Role, TEXT("success"),
			FString::Printf(TEXT("Spawned actor '%s' (%s)"), *NewActor->GetActorLabel(), *ClassName));
		TSharedPtr<FJsonObject> SpawnEvent = MakeShared<FJsonObject>();
		SpawnEvent->SetStringField(TEXT("type"), TEXT("spawn"));
		SpawnEvent->SetStringField(TEXT("mode"), TEXT("editor"));
		SpawnEvent->SetStringField(TEXT("timestamp_utc"), FDateTime::UtcNow().ToIso8601());
		SpawnEvent->SetStringField(TEXT("route"), TEXT("/nova/scene/spawn"));
		SpawnEvent->SetStringField(TEXT("action"), TEXT("scene.spawn"));
		SpawnEvent->SetStringField(TEXT("role"), Role);
		SpawnEvent->SetStringField(TEXT("actor_name"), NewActor->GetName());
		SpawnEvent->SetStringField(TEXT("actor_label"), NewActor->GetActorLabel());
		SpawnEvent->SetStringField(TEXT("class"), ClassName);
		QueueEventObject(SpawnEvent);

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
	const FString Role = ResolveRoleFromRequest(Request);

	AsyncTask(ENamedThreads::GameThread, [this, OnComplete, ActorName, Role]()
	{
		AActor* Actor = FindActorByName(ActorName);
		if (!Actor)
		{
			PushAuditEntry(TEXT("/nova/scene/delete"), TEXT("scene.delete"), Role, TEXT("error"),
				FString::Printf(TEXT("Actor not found: %s"), *ActorName));
			SendErrorResponse(OnComplete, FString::Printf(TEXT("Actor not found: %s"), *ActorName), 404);
			return;
		}

		// Protect the NovaBridge scene capture actor from deletion
		if (Actor->GetActorLabel() == TEXT("NovaBridge_SceneCapture") || Actor->GetName().Contains(TEXT("NovaBridge_SceneCapture")))
		{
			PushAuditEntry(TEXT("/nova/scene/delete"), TEXT("scene.delete"), Role, TEXT("denied"),
				TEXT("Attempted deletion of NovaBridge_SceneCapture"));
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

		PushAuditEntry(TEXT("/nova/scene/delete"), TEXT("scene.delete"), Role, TEXT("success"),
			FString::Printf(TEXT("Deleted actor '%s'"), *ActorName));
		TSharedPtr<FJsonObject> DeleteEvent = MakeShared<FJsonObject>();
		DeleteEvent->SetStringField(TEXT("type"), TEXT("delete"));
		DeleteEvent->SetStringField(TEXT("mode"), TEXT("editor"));
		DeleteEvent->SetStringField(TEXT("timestamp_utc"), FDateTime::UtcNow().ToIso8601());
		DeleteEvent->SetStringField(TEXT("route"), TEXT("/nova/scene/delete"));
		DeleteEvent->SetStringField(TEXT("action"), TEXT("scene.delete"));
		DeleteEvent->SetStringField(TEXT("role"), Role);
		DeleteEvent->SetStringField(TEXT("actor_name"), ActorName);
		QueueEventObject(DeleteEvent);
		SendOkResponse(OnComplete);
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
	const FString Role = ResolveRoleFromRequest(Request);

	AsyncTask(ENamedThreads::GameThread, [this, OnComplete, ActorName, PropertyName, Value, Role]()
	{
		AActor* Actor = FindActorByName(ActorName);
		if (!Actor)
		{
			SendErrorResponse(OnComplete, FString::Printf(TEXT("Actor not found: %s"), *ActorName), 404);
			return;
		}
		FString PropertyError;
		if (!SetActorPropertyValue(Actor, PropertyName, Value, PropertyError))
		{
			const int32 Code = PropertyError.StartsWith(TEXT("Property not found")) ? 404 : 400;
			SendErrorResponse(OnComplete, PropertyError, Code);
			PushAuditEntry(TEXT("/nova/scene/set-property"), TEXT("scene.set-property"), Role, TEXT("error"), PropertyError);
			return;
		}

		PushAuditEntry(TEXT("/nova/scene/set-property"), TEXT("scene.set-property"), Role, TEXT("success"),
			FString::Printf(TEXT("Set %s on %s"), *PropertyName, *ActorName));
		SendOkResponse(OnComplete);
	});
	return true;
}
