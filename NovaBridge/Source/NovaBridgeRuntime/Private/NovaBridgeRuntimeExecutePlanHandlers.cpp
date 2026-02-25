#include "NovaBridgeRuntimeModule.h"

#include "NovaBridgePlanDispatch.h"
#include "NovaBridgePlanEvents.h"
#include "NovaBridgePlanSchema.h"
#include "NovaBridgePolicy.h"

#include "Async/Async.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/Engine.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "Misc/ScopeLock.h"
#include "UObject/Class.h"
#include "UObject/UnrealType.h"

namespace
{
static const TCHAR* RuntimeModeName = TEXT("runtime");

UWorld* ResolveRuntimeWorld()
{
	if (!GEngine)
	{
		return nullptr;
	}

	for (const FWorldContext& Context : GEngine->GetWorldContexts())
	{
		UWorld* World = Context.World();
		if (!World)
		{
			continue;
		}

		if (Context.WorldType == EWorldType::Game || Context.WorldType == EWorldType::PIE || Context.WorldType == EWorldType::GameRPC)
		{
			return World;
		}
	}

	return nullptr;
}

AActor* FindActorByNameRuntime(UWorld* World, const FString& Name)
{
	if (!World || Name.IsEmpty())
	{
		return nullptr;
	}

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor)
		{
			continue;
		}
		if (Actor->GetName() == Name)
		{
			return Actor;
		}
	}
	return nullptr;
}

const TArray<FString>& RuntimeAllowedClasses()
{
	return NovaBridgeCore::RuntimeAllowedSpawnClasses();
}

bool IsRuntimeClassAllowed(const FString& ClassName)
{
	return NovaBridgeCore::IsClassAllowed(RuntimeAllowedClasses(), ClassName);
}

UClass* ResolveRuntimeActorClass(const FString& InClassName)
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
		if (!ActorClass)
		{
			ActorClass = FindObject<UClass>(nullptr, *(FString(TEXT("/Script/Engine.")) + ClassName));
		}
	}

	return ActorClass;
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
		if (!Obj.IsValid() || !Obj->HasField(TEXT("x")) || !Obj->HasField(TEXT("y")) || !Obj->HasField(TEXT("z")))
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

bool SetRuntimeActorPropertyValue(AActor* Actor, const FString& PropertyName, const FString& Value, FString& OutError)
{
	if (!Actor)
	{
		OutError = TEXT("Actor is null");
		return false;
	}

	FProperty* Prop = Actor->GetClass()->FindPropertyByName(*PropertyName);
	if (!Prop)
	{
		for (TFieldIterator<FProperty> It(Actor->GetClass()); It; ++It)
		{
			if (It->GetName().Equals(PropertyName, ESearchCase::IgnoreCase))
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

	void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Actor);
	if (!Prop->ImportText_Direct(*Value, ValuePtr, Actor, PPF_None))
	{
		OutError = FString::Printf(TEXT("Failed to set property: %s"), *PropertyName);
		return false;
	}

	return true;
}

void ParseSpawnTransform(const TSharedPtr<FJsonObject>& Params, FVector& OutLocation, FRotator& OutRotation)
{
	OutLocation = FVector::ZeroVector;
	OutRotation = FRotator::ZeroRotator;
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
			if (const TSharedPtr<FJsonValue>* LocationValue = Transform->Values.Find(TEXT("location")))
			{
				FVector ParsedLocation = FVector::ZeroVector;
				if (JsonValueToVector(*LocationValue, ParsedLocation))
				{
					OutLocation = ParsedLocation;
				}
			}

			if (const TSharedPtr<FJsonValue>* RotationValue = Transform->Values.Find(TEXT("rotation")))
			{
				FRotator ParsedRotation = FRotator::ZeroRotator;
				if (JsonValueToRotator(*RotationValue, ParsedRotation))
				{
					OutRotation = ParsedRotation;
				}
			}
		}
	}
}
} // namespace

bool FNovaBridgeRuntimeModule::HandleExecutePlan(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	{
		const double NowSec = FPlatformTime::Seconds();
		FScopeLock Lock(&RuntimeExecutePlanRateLimitMutex);
		if (RuntimeExecutePlanWindowStartSec <= 0.0 || (NowSec - RuntimeExecutePlanWindowStartSec) >= 60.0)
		{
			RuntimeExecutePlanWindowStartSec = NowSec;
			RuntimeExecutePlanCountInWindow = 0;
		}

		RuntimeExecutePlanCountInWindow++;
		if (RuntimeExecutePlanCountInWindow > MaxExecutePlanPerMinute)
		{
			PushAuditEntry(TEXT("/nova/executePlan"), TEXT("executePlan"), TEXT("rate_limited"), TEXT("Runtime executePlan per-minute limit exceeded"));
			SendErrorResponse(OnComplete, FString::Printf(TEXT("Rate limit: max %d runtime executePlan requests per minute"), MaxExecutePlanPerMinute), 429);
			return true;
		}
	}

	const TSharedPtr<FJsonObject> Body = ParseRequestBody(Request);
	if (!Body)
	{
		PushAuditEntry(TEXT("/nova/executePlan"), TEXT("executePlan"), TEXT("error"), TEXT("Invalid JSON body"));
		SendErrorResponse(OnComplete, TEXT("Invalid JSON body"));
		return true;
	}

	NovaBridgeCore::FPlanSchemaError SchemaError;
	if (!NovaBridgeCore::ValidateExecutePlanSchema(Body, NovaBridgeCore::ENovaBridgePlanMode::Runtime, MaxPlanSteps, SchemaError))
	{
		const FString ValidationMessage = (SchemaError.StepIndex >= 0)
			? FString::Printf(TEXT("Schema validation failed at step %d: %s"), SchemaError.StepIndex, *SchemaError.Message)
			: FString::Printf(TEXT("Schema validation failed: %s"), *SchemaError.Message);
		PushAuditEntry(TEXT("/nova/executePlan"), TEXT("executePlan"), TEXT("error"), ValidationMessage);
		SendErrorResponse(OnComplete, ValidationMessage, 400);
		return true;
	}

	const TArray<TSharedPtr<FJsonValue>> Steps = Body->GetArrayField(TEXT("steps"));
	const FString PlanId = Body->HasTypedField<EJson::String>(TEXT("plan_id"))
		? Body->GetStringField(TEXT("plan_id"))
		: FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower);

	AsyncTask(ENamedThreads::GameThread, [this, OnComplete, Steps, PlanId]()
	{
		UWorld* World = ResolveRuntimeWorld();
		if (!World)
		{
			PushAuditEntry(TEXT("/nova/executePlan"), TEXT("executePlan"), TEXT("error"), TEXT("No runtime world is available"));
			SendErrorResponse(OnComplete, TEXT("No runtime world is available"), 500);
			return;
		}

		TArray<TSharedPtr<FJsonValue>> StepResults;
		StepResults.Reserve(Steps.Num());
		TArray<FString> StepActions;
		StepActions.SetNum(Steps.Num());
		int32 SpawnCount = 0;
		int32 SuccessCount = 0;
		int32 ErrorCount = 0;

		NovaBridgeCore::FPlanCommandRouter CommandRouter;
		CommandRouter.Register(TEXT("spawn"), [this, World, &SpawnCount, PlanId](const NovaBridgeCore::FPlanStepContext& Step)
		{
			const TSharedPtr<FJsonObject> Params = Step.Params.IsValid() ? Step.Params : MakeShared<FJsonObject>();

			if (SpawnCount >= MaxSpawnPerPlan)
			{
				return NovaBridgeCore::MakePlanStepResult(
					Step.StepIndex,
					TEXT("error"),
					FString::Printf(TEXT("Max spawn per plan reached (%d)"), MaxSpawnPerPlan));
			}

			FString ClassName;
			if (Params->HasTypedField<EJson::String>(TEXT("class")))
			{
				ClassName = Params->GetStringField(TEXT("class"));
			}
			else if (Params->HasTypedField<EJson::String>(TEXT("type")))
			{
				ClassName = Params->GetStringField(TEXT("type"));
			}

			if (ClassName.IsEmpty())
			{
				return NovaBridgeCore::MakePlanStepResult(Step.StepIndex, TEXT("error"), TEXT("spawn.params.class or spawn.params.type is required"));
			}
			if (!IsRuntimeClassAllowed(ClassName))
			{
				return NovaBridgeCore::MakePlanStepResult(
					Step.StepIndex,
					TEXT("error"),
					FString::Printf(TEXT("Class not allowed in runtime mode: %s"), *ClassName));
			}

			FVector SpawnLocation = FVector::ZeroVector;
			FRotator SpawnRotation = FRotator::ZeroRotator;
			ParseSpawnTransform(Params, SpawnLocation, SpawnRotation);
			if (!NovaBridgeCore::IsSpawnLocationInBounds(SpawnLocation))
			{
				return NovaBridgeCore::MakePlanStepResult(Step.StepIndex, TEXT("error"), TEXT("Spawn location is outside runtime bounds"));
			}

			UClass* ActorClass = ResolveRuntimeActorClass(ClassName);
			if (!ActorClass)
			{
				return NovaBridgeCore::MakePlanStepResult(
					Step.StepIndex,
					TEXT("error"),
					FString::Printf(TEXT("Class not found: %s"), *ClassName));
			}

			FString RequestedName = Params->HasTypedField<EJson::String>(TEXT("label")) ? Params->GetStringField(TEXT("label")) : FString();
			RequestedName.TrimStartAndEndInline();

			FActorSpawnParameters SpawnParams;
			if (!RequestedName.IsEmpty())
			{
				SpawnParams.Name = FName(*RequestedName);
			}

			AActor* NewActor = World->SpawnActor<AActor>(ActorClass, SpawnLocation, SpawnRotation, SpawnParams);
			if (!NewActor && !RequestedName.IsEmpty())
			{
				SpawnParams.Name = NAME_None;
				NewActor = World->SpawnActor<AActor>(ActorClass, SpawnLocation, SpawnRotation, SpawnParams);
			}
			if (!NewActor)
			{
				return NovaBridgeCore::MakePlanStepResult(Step.StepIndex, TEXT("error"), TEXT("Failed to spawn actor"));
			}

			TSharedPtr<FJsonObject> StepResult = NovaBridgeCore::MakePlanStepResult(Step.StepIndex, TEXT("success"), TEXT("Spawned actor"));
			StepResult->SetStringField(TEXT("object_id"), NewActor->GetName());
			if (!RequestedName.IsEmpty())
			{
				StepResult->SetStringField(TEXT("requested_name"), RequestedName);
			}

			const TSharedPtr<FJsonObject> SpawnEvent = NovaBridgeCore::BuildSpawnEvent(
				RuntimeModeName,
				PlanId,
				Step.StepIndex,
				Step.Action,
				NewActor->GetName(),
				ClassName,
				FString(),
				FString(),
				RequestedName);
			QueueRuntimeEvent(SpawnEvent);
			PushRuntimeUndoEntry(TEXT("spawn"), NewActor->GetName(), NewActor->GetActorLabel());
			SpawnCount++;

			return StepResult;
		});

		CommandRouter.Register(TEXT("delete"), [this, World, PlanId](const NovaBridgeCore::FPlanStepContext& Step)
		{
			const TSharedPtr<FJsonObject> Params = Step.Params.IsValid() ? Step.Params : MakeShared<FJsonObject>();

			FString ActorName = Params->HasTypedField<EJson::String>(TEXT("name")) ? Params->GetStringField(TEXT("name")) : FString();
			if (ActorName.IsEmpty() && Params->HasTypedField<EJson::String>(TEXT("target")))
			{
				ActorName = Params->GetStringField(TEXT("target"));
			}
			if (ActorName.IsEmpty())
			{
				return NovaBridgeCore::MakePlanStepResult(Step.StepIndex, TEXT("error"), TEXT("delete.params.name is required"));
			}

			AActor* Actor = FindActorByNameRuntime(World, ActorName);
			if (!Actor)
			{
				return NovaBridgeCore::MakePlanStepResult(
					Step.StepIndex,
					TEXT("error"),
					FString::Printf(TEXT("Actor not found: %s"), *ActorName));
			}

			Actor->Destroy();

			TSharedPtr<FJsonObject> StepResult = NovaBridgeCore::MakePlanStepResult(
				Step.StepIndex,
				TEXT("success"),
				FString::Printf(TEXT("Deleted actor %s"), *ActorName));

			const TSharedPtr<FJsonObject> DeleteEvent = NovaBridgeCore::BuildDeleteEvent(
				RuntimeModeName,
				PlanId,
				Step.StepIndex,
				Step.Action,
				ActorName);
			QueueRuntimeEvent(DeleteEvent);

			return StepResult;
		});

		CommandRouter.Register(TEXT("set"), [this, World](const NovaBridgeCore::FPlanStepContext& Step)
		{
			const TSharedPtr<FJsonObject> Params = Step.Params.IsValid() ? Step.Params : MakeShared<FJsonObject>();

			FString TargetName = Params->HasTypedField<EJson::String>(TEXT("target")) ? Params->GetStringField(TEXT("target")) : FString();
			if (TargetName.IsEmpty() && Params->HasTypedField<EJson::String>(TEXT("name")))
			{
				TargetName = Params->GetStringField(TEXT("name"));
			}
			if (TargetName.IsEmpty())
			{
				return NovaBridgeCore::MakePlanStepResult(Step.StepIndex, TEXT("error"), TEXT("set.params.target is required"));
			}

			AActor* Actor = FindActorByNameRuntime(World, TargetName);
			if (!Actor)
			{
				return NovaBridgeCore::MakePlanStepResult(
					Step.StepIndex,
					TEXT("error"),
					FString::Printf(TEXT("Actor not found: %s"), *TargetName));
			}
			if (!Params->HasTypedField<EJson::Object>(TEXT("props")))
			{
				return NovaBridgeCore::MakePlanStepResult(Step.StepIndex, TEXT("error"), TEXT("set.params.props object is required"));
			}

			const TSharedPtr<FJsonObject> Props = Params->GetObjectField(TEXT("props"));
			bool bSetAnyProperty = false;
			FString SetErrorMessage;
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Props->Values)
			{
				const FString Key = Pair.Key;
				const TSharedPtr<FJsonValue> PropValue = Pair.Value;
				if (!PropValue.IsValid())
				{
					continue;
				}

				if (Key.Equals(TEXT("location"), ESearchCase::IgnoreCase))
				{
					FVector ParsedLocation = FVector::ZeroVector;
					if (JsonValueToVector(PropValue, ParsedLocation))
					{
						Actor->SetActorLocation(ParsedLocation);
						bSetAnyProperty = true;
						continue;
					}
				}

				if (Key.Equals(TEXT("rotation"), ESearchCase::IgnoreCase))
				{
					FRotator ParsedRotation = FRotator::ZeroRotator;
					if (JsonValueToRotator(PropValue, ParsedRotation))
					{
						Actor->SetActorRotation(ParsedRotation);
						bSetAnyProperty = true;
						continue;
					}
				}

				if (Key.Equals(TEXT("scale"), ESearchCase::IgnoreCase))
				{
					FVector ParsedScale = FVector(1.0, 1.0, 1.0);
					if (JsonValueToVector(PropValue, ParsedScale))
					{
						Actor->SetActorScale3D(ParsedScale);
						bSetAnyProperty = true;
						continue;
					}
				}

				FString ImportText;
				if (!JsonValueToImportText(PropValue, ImportText))
				{
					SetErrorMessage = FString::Printf(TEXT("Unsupported value type for property '%s'"), *Key);
					break;
				}

				FString PropertyError;
				if (!SetRuntimeActorPropertyValue(Actor, Key, ImportText, PropertyError))
				{
					SetErrorMessage = PropertyError;
					break;
				}
				bSetAnyProperty = true;
			}

			if (!SetErrorMessage.IsEmpty())
			{
				return NovaBridgeCore::MakePlanStepResult(Step.StepIndex, TEXT("error"), SetErrorMessage);
			}
			if (!bSetAnyProperty)
			{
				return NovaBridgeCore::MakePlanStepResult(Step.StepIndex, TEXT("error"), TEXT("No valid properties were applied"));
			}

			return NovaBridgeCore::MakePlanStepResult(
				Step.StepIndex,
				TEXT("success"),
				FString::Printf(TEXT("Updated actor %s"), *TargetName));
		});

		CommandRouter.Register(TEXT("screenshot"), [](const NovaBridgeCore::FPlanStepContext& Step)
		{
			return NovaBridgeCore::MakePlanStepResult(
				Step.StepIndex,
				TEXT("error"),
				TEXT("screenshot is not supported in runtime mode yet"));
		});

		for (int32 StepIndex = 0; StepIndex < Steps.Num(); ++StepIndex)
		{
			NovaBridgeCore::FPlanStepContext StepContext;
			TSharedPtr<FJsonObject> ParseErrorResult;
			if (!NovaBridgeCore::ExtractPlanStep(Steps[StepIndex], StepIndex, StepContext, ParseErrorResult))
			{
				StepActions[StepIndex] = TEXT("unknown");
				StepResults.Add(MakeShared<FJsonValueObject>(ParseErrorResult));
				ErrorCount++;
				continue;
			}

			StepActions[StepIndex] = StepContext.Action;
			TSharedPtr<FJsonObject> StepResult = CommandRouter.Dispatch(StepContext);
			if (!StepResult.IsValid())
			{
				StepResult = NovaBridgeCore::MakePlanStepResult(StepIndex, TEXT("error"), TEXT("Step execution failed"));
			}
			StepResults.Add(MakeShared<FJsonValueObject>(StepResult));

			const FString ResultStatus = StepResult->HasTypedField<EJson::String>(TEXT("status"))
				? NovaBridgeCore::NormalizePlanAction(StepResult->GetStringField(TEXT("status")))
				: TEXT("error");
			if (ResultStatus == TEXT("success"))
			{
				SuccessCount++;
			}
			else
			{
				ErrorCount++;
			}
		}

		TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
		Result->SetStringField(TEXT("status"), TEXT("ok"));
		Result->SetStringField(TEXT("mode"), RuntimeModeName);
		Result->SetStringField(TEXT("plan_id"), PlanId);
		Result->SetArrayField(TEXT("results"), StepResults);
		Result->SetNumberField(TEXT("step_count"), Steps.Num());
		Result->SetNumberField(TEXT("success_count"), SuccessCount);
		Result->SetNumberField(TEXT("error_count"), ErrorCount);

		for (const TSharedPtr<FJsonValue>& StepResultValue : StepResults)
		{
			if (!StepResultValue.IsValid() || StepResultValue->Type != EJson::Object)
			{
				continue;
			}
			const TSharedPtr<FJsonObject> StepResultObj = StepResultValue->AsObject();
			if (!StepResultObj.IsValid())
			{
				continue;
			}

			const int32 ResultStepIndex = StepResultObj->HasTypedField<EJson::Number>(TEXT("step"))
				? static_cast<int32>(StepResultObj->GetNumberField(TEXT("step")))
				: INDEX_NONE;
			const FString ResultAction = (ResultStepIndex >= 0 && ResultStepIndex < StepActions.Num() && !StepActions[ResultStepIndex].IsEmpty())
				? StepActions[ResultStepIndex]
				: TEXT("unknown");

			const TSharedPtr<FJsonObject> PlanStepEvent = NovaBridgeCore::BuildPlanStepEvent(RuntimeModeName, PlanId, ResultAction, StepResultObj);
			if (PlanStepEvent.IsValid())
			{
				QueueRuntimeEvent(PlanStepEvent);
			}
		}

		const TSharedPtr<FJsonObject> PlanCompleteEvent = NovaBridgeCore::BuildPlanCompleteEvent(
			RuntimeModeName,
			PlanId,
			Steps.Num(),
			SuccessCount,
			ErrorCount);
		QueueRuntimeEvent(PlanCompleteEvent);

		SendJsonResponse(OnComplete, Result);
		PushAuditEntry(TEXT("/nova/executePlan"), TEXT("executePlan.complete"), TEXT("success"),
			FString::Printf(TEXT("Plan %s complete: %d success, %d error"), *PlanId, SuccessCount, ErrorCount));
	});

	return true;
}

bool FNovaBridgeRuntimeModule::HandleUndo(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	(void)Request;

	AsyncTask(ENamedThreads::GameThread, [this, OnComplete]()
	{
		FRuntimeUndoEntry Entry;
		if (!PopRuntimeUndoEntry(Entry))
		{
			PushAuditEntry(TEXT("/nova/undo"), TEXT("undo"), TEXT("error"), TEXT("Runtime undo stack is empty"));
			SendErrorResponse(OnComplete, TEXT("Undo stack is empty"), 404);
			return;
		}

		if (!Entry.Action.Equals(TEXT("spawn"), ESearchCase::IgnoreCase))
		{
			PushAuditEntry(TEXT("/nova/undo"), TEXT("undo"), TEXT("error"), TEXT("Unsupported undo action type"));
			SendErrorResponse(OnComplete, FString::Printf(TEXT("Unsupported undo action: %s"), *Entry.Action), 400);
			return;
		}

		UWorld* World = ResolveRuntimeWorld();
		if (!World)
		{
			PushAuditEntry(TEXT("/nova/undo"), TEXT("undo"), TEXT("error"), TEXT("No runtime world is available"));
			SendErrorResponse(OnComplete, TEXT("No runtime world is available"), 500);
			return;
		}

		AActor* Actor = FindActorByNameRuntime(World, Entry.ActorName);
		if (!Actor && !Entry.ActorLabel.IsEmpty())
		{
			Actor = FindActorByNameRuntime(World, Entry.ActorLabel);
		}
		if (!Actor)
		{
			PushAuditEntry(TEXT("/nova/undo"), TEXT("undo"), TEXT("error"),
				FString::Printf(TEXT("Actor not found for undo: %s"), *Entry.ActorName));
			SendErrorResponse(OnComplete, FString::Printf(TEXT("Actor not found for undo: %s"), *Entry.ActorName), 404);
			return;
		}

		Actor->Destroy();

		TSharedPtr<FJsonObject> DeleteEvent = MakeShared<FJsonObject>();
		DeleteEvent->SetStringField(TEXT("type"), TEXT("delete"));
		DeleteEvent->SetStringField(TEXT("mode"), RuntimeModeName);
		DeleteEvent->SetStringField(TEXT("timestamp_utc"), FDateTime::UtcNow().ToIso8601());
		DeleteEvent->SetStringField(TEXT("route"), TEXT("/nova/undo"));
		DeleteEvent->SetStringField(TEXT("action"), TEXT("undo"));
		DeleteEvent->SetStringField(TEXT("status"), TEXT("success"));
		DeleteEvent->SetStringField(TEXT("actor_name"), Entry.ActorName);
		DeleteEvent->SetStringField(TEXT("actor_label"), Entry.ActorLabel);
		QueueRuntimeEvent(DeleteEvent);

		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("status"), TEXT("ok"));
		Result->SetStringField(TEXT("mode"), RuntimeModeName);
		Result->SetStringField(TEXT("undone_action"), Entry.Action);
		Result->SetStringField(TEXT("actor_name"), Entry.ActorName);
		Result->SetStringField(TEXT("actor_label"), Entry.ActorLabel);
		SendJsonResponse(OnComplete, Result);

		PushAuditEntry(TEXT("/nova/undo"), TEXT("undo"), TEXT("success"),
			FString::Printf(TEXT("Undid spawn for actor '%s'"), *Entry.ActorName));
	});

	return true;
}
