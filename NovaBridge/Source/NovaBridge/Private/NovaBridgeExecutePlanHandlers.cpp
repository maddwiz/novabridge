#include "NovaBridgeModule.h"
#include "NovaBridgeEditorInternals.h"

#include "NovaBridgePlanDispatch.h"
#include "NovaBridgePlanEvents.h"
#include "NovaBridgePlanSchema.h"
#include "NovaBridgeCoreTypes.h"
#include "Async/Async.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "Subsystems/EditorActorSubsystem.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/SceneCapture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "TextureResource.h"
#include "IImageWrapperModule.h"
#include "IImageWrapper.h"
#include "Misc/Base64.h"

bool FNovaBridgeModule::HandleExecutePlan(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	TSharedPtr<FJsonObject> Body = ParseRequestBody(Request);
	if (!Body)
	{
		SendErrorResponse(OnComplete, TEXT("Invalid JSON body"));
		return true;
	}

	NovaBridgeCore::FPlanSchemaError SchemaError;
	if (!NovaBridgeCore::ValidateExecutePlanSchema(Body, NovaBridgeCore::ENovaBridgePlanMode::Editor, GetNovaBridgeEditorMaxPlanSteps(), SchemaError))
	{
		const FString Prefix = (SchemaError.StepIndex >= 0)
			? FString::Printf(TEXT("Schema validation failed at step %d: "), SchemaError.StepIndex)
			: TEXT("Schema validation failed: ");
		SendErrorResponse(OnComplete, Prefix + SchemaError.Message, 400);
		return true;
	}

	const TArray<TSharedPtr<FJsonValue>> Steps = Body->GetArrayField(TEXT("steps"));

	FString PlanId = Body->HasTypedField<EJson::String>(TEXT("plan_id"))
		? Body->GetStringField(TEXT("plan_id"))
		: FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower);

	FString Role = ResolveRoleFromRequest(Request);
	if (Body->HasTypedField<EJson::String>(TEXT("role")))
	{
		const FString RequestedRole = NovaBridgeCore::NormalizeRoleName(Body->GetStringField(TEXT("role")));
		if (!RequestedRole.IsEmpty())
		{
			Role = RequestedRole;
		}
	}

	AsyncTask(ENamedThreads::GameThread, [this, OnComplete, Steps, PlanId, Role]()
	{
		TArray<TSharedPtr<FJsonValue>> StepResults;
		StepResults.Reserve(Steps.Num());
		TArray<FString> StepActions;
		StepActions.SetNum(Steps.Num());
		int32 SpawnedInPlan = 0;

		NovaBridgeCore::FPlanCommandRouter CommandRouter;
		CommandRouter.Register(TEXT("spawn"), [this, Role, PlanId, &SpawnedInPlan](const NovaBridgeCore::FPlanStepContext& Step)
		{
			const TSharedPtr<FJsonObject> Params = Step.Params.IsValid() ? Step.Params : MakeShared<FJsonObject>();

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
				return NovaBridgeCore::MakePlanStepResult(
					Step.StepIndex,
					TEXT("error"),
					TEXT("spawn.params.class or spawn.params.type is required"));
			}

			const int32 PlanSpawnLimit = GetPlanSpawnLimit(Role);
			if (SpawnedInPlan >= PlanSpawnLimit)
			{
				return NovaBridgeCore::MakePlanStepResult(
					Step.StepIndex,
					TEXT("error"),
					FString::Printf(TEXT("Plan spawn limit reached (%d) for role '%s'"), PlanSpawnLimit, *Role));
			}

			FVector SpawnLocation = FVector::ZeroVector;
			FRotator SpawnRotation = FRotator::ZeroRotator;
			FVector SpawnScale = FVector(1.0, 1.0, 1.0);
			ParseSpawnTransformFromParams(Params, SpawnLocation, SpawnRotation, SpawnScale);
			if (!IsSpawnClassAllowedForRole(Role, ClassName))
			{
				const FString Message = FString::Printf(TEXT("Class '%s' is not allowed for role '%s'"), *ClassName, *Role);
				PushAuditEntry(TEXT("/nova/executePlan"), Step.Action, Role, TEXT("denied"), Message);
				return NovaBridgeCore::MakePlanStepResult(Step.StepIndex, TEXT("error"), Message);
			}
			if (!IsSpawnLocationInBounds(SpawnLocation))
			{
				const FString Message = TEXT("Spawn location is outside allowed bounds");
				PushAuditEntry(TEXT("/nova/executePlan"), Step.Action, Role, TEXT("denied"), Message);
				return NovaBridgeCore::MakePlanStepResult(Step.StepIndex, TEXT("error"), Message);
			}

			const int32 SpawnRateLimit = GetRouteRateLimitPerMinute(Role, TEXT("/nova/scene/spawn"));
			FString SpawnRateError;
			if (!ConsumeRateLimit(Role + TEXT("|plan.spawn"), SpawnRateLimit, SpawnRateError))
			{
				PushAuditEntry(TEXT("/nova/executePlan"), Step.Action, Role, TEXT("rate_limited"), SpawnRateError);
				return NovaBridgeCore::MakePlanStepResult(Step.StepIndex, TEXT("error"), SpawnRateError);
			}

			if (!GEditor)
			{
				return NovaBridgeCore::MakePlanStepResult(Step.StepIndex, TEXT("error"), TEXT("No editor"));
			}

			UEditorActorSubsystem* ActorSub = GEditor->GetEditorSubsystem<UEditorActorSubsystem>();
			if (!ActorSub)
			{
				return NovaBridgeCore::MakePlanStepResult(Step.StepIndex, TEXT("error"), TEXT("No EditorActorSubsystem"));
			}

			UClass* ActorClass = ResolveActorClassByName(ClassName);
			if (!ActorClass)
			{
				return NovaBridgeCore::MakePlanStepResult(
					Step.StepIndex,
					TEXT("error"),
					FString::Printf(TEXT("Class not found: %s"), *ClassName));
			}

			AActor* NewActor = ActorSub->SpawnActorFromClass(TSubclassOf<AActor>(ActorClass), SpawnLocation, SpawnRotation);
			if (!NewActor)
			{
				return NovaBridgeCore::MakePlanStepResult(Step.StepIndex, TEXT("error"), TEXT("Failed to spawn actor"));
			}

			if (!SpawnScale.Equals(FVector(1.0, 1.0, 1.0)))
			{
				NewActor->SetActorScale3D(SpawnScale);
			}

			FString Label = Params->HasTypedField<EJson::String>(TEXT("label")) ? Params->GetStringField(TEXT("label")) : FString();
			if (!Label.IsEmpty())
			{
				NewActor->SetActorLabel(Label);
			}

			FNovaBridgeUndoEntry UndoEntry;
			UndoEntry.Action = TEXT("spawn");
			UndoEntry.ActorName = NewActor->GetName();
			UndoEntry.ActorLabel = NewActor->GetActorLabel();
			PushUndoEntry(UndoEntry);
			SpawnedInPlan++;

			TSharedPtr<FJsonObject> StepResult = NovaBridgeCore::MakePlanStepResult(Step.StepIndex, TEXT("success"), TEXT("Spawned actor"));
			StepResult->SetStringField(TEXT("object_id"), NewActor->GetName());
			StepResult->SetStringField(TEXT("label"), NewActor->GetActorLabel());
			StepResult->SetStringField(TEXT("class"), ClassName);

			const TSharedPtr<FJsonObject> SpawnEvent = NovaBridgeCore::BuildSpawnEvent(
				TEXT("editor"),
				PlanId,
				Step.StepIndex,
				Step.Action,
				NewActor->GetName(),
				ClassName,
				NewActor->GetName(),
				NewActor->GetActorLabel());
			QueueEventObject(SpawnEvent);

			PushAuditEntry(
				TEXT("/nova/executePlan"),
				Step.Action,
				Role,
				TEXT("success"),
				FString::Printf(TEXT("Spawned actor '%s'"), *NewActor->GetActorLabel()));
			return StepResult;
		});

		CommandRouter.Register(TEXT("delete"), [this, Role, PlanId](const NovaBridgeCore::FPlanStepContext& Step)
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

			AActor* Actor = FindActorByName(ActorName);
			if (!Actor)
			{
				return NovaBridgeCore::MakePlanStepResult(
					Step.StepIndex,
					TEXT("error"),
					FString::Printf(TEXT("Actor not found: %s"), *ActorName));
			}
			if (Actor->GetActorLabel() == TEXT("NovaBridge_SceneCapture") || Actor->GetName().Contains(TEXT("NovaBridge_SceneCapture")))
			{
				return NovaBridgeCore::MakePlanStepResult(Step.StepIndex, TEXT("error"), TEXT("Cannot delete NovaBridge_SceneCapture"));
			}

			if (UEditorActorSubsystem* ActorSub = GEditor ? GEditor->GetEditorSubsystem<UEditorActorSubsystem>() : nullptr)
			{
				ActorSub->DestroyActor(Actor);
			}
			else
			{
				Actor->Destroy();
			}

			TSharedPtr<FJsonObject> StepResult = NovaBridgeCore::MakePlanStepResult(
				Step.StepIndex,
				TEXT("success"),
				FString::Printf(TEXT("Deleted actor %s"), *ActorName));

			const TSharedPtr<FJsonObject> DeleteEvent = NovaBridgeCore::BuildDeleteEvent(
				TEXT("editor"),
				PlanId,
				Step.StepIndex,
				Step.Action,
				ActorName);
			QueueEventObject(DeleteEvent);

			PushAuditEntry(
				TEXT("/nova/executePlan"),
				Step.Action,
				Role,
				TEXT("success"),
				FString::Printf(TEXT("Deleted actor '%s'"), *ActorName));
			return StepResult;
		});

		CommandRouter.Register(TEXT("set"), [this, Role](const NovaBridgeCore::FPlanStepContext& Step)
		{
			const TSharedPtr<FJsonObject> Params = Step.Params.IsValid() ? Step.Params : MakeShared<FJsonObject>();

			FString Target = Params->HasTypedField<EJson::String>(TEXT("target")) ? Params->GetStringField(TEXT("target")) : FString();
			if (Target.IsEmpty() && Params->HasTypedField<EJson::String>(TEXT("name")))
			{
				Target = Params->GetStringField(TEXT("name"));
			}
			if (Target.IsEmpty())
			{
				return NovaBridgeCore::MakePlanStepResult(Step.StepIndex, TEXT("error"), TEXT("set.params.target is required"));
			}

			AActor* Actor = FindActorByName(Target);
			if (!Actor)
			{
				return NovaBridgeCore::MakePlanStepResult(
					Step.StepIndex,
					TEXT("error"),
					FString::Printf(TEXT("Actor not found: %s"), *Target));
			}

			if (!Params->HasTypedField<EJson::Object>(TEXT("props")))
			{
				return NovaBridgeCore::MakePlanStepResult(Step.StepIndex, TEXT("error"), TEXT("set.params.props object is required"));
			}

			const TSharedPtr<FJsonObject> Props = Params->GetObjectField(TEXT("props"));
			bool bSetAnyProperty = false;
			bool bSetError = false;
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
					bSetError = true;
					SetErrorMessage = FString::Printf(TEXT("Unsupported value type for property '%s'"), *Key);
					break;
				}

				FString PropertyError;
				if (!SetActorPropertyValue(Actor, Key, ImportText, PropertyError))
				{
					bSetError = true;
					SetErrorMessage = PropertyError;
					break;
				}
				bSetAnyProperty = true;
			}

			if (bSetError)
			{
				PushAuditEntry(TEXT("/nova/executePlan"), Step.Action, Role, TEXT("error"), SetErrorMessage);
				return NovaBridgeCore::MakePlanStepResult(Step.StepIndex, TEXT("error"), SetErrorMessage);
			}
			if (!bSetAnyProperty)
			{
				return NovaBridgeCore::MakePlanStepResult(Step.StepIndex, TEXT("error"), TEXT("No valid properties were applied"));
			}

			Actor->PostEditChange();
			PushAuditEntry(
				TEXT("/nova/executePlan"),
				Step.Action,
				Role,
				TEXT("success"),
				FString::Printf(TEXT("Updated actor '%s'"), *Target));
			return NovaBridgeCore::MakePlanStepResult(
				Step.StepIndex,
				TEXT("success"),
				FString::Printf(TEXT("Updated actor %s"), *Target));
		});

		CommandRouter.Register(TEXT("screenshot"), [this, Role](const NovaBridgeCore::FPlanStepContext& Step)
		{
			const TSharedPtr<FJsonObject> Params = Step.Params.IsValid() ? Step.Params : MakeShared<FJsonObject>();

			const int32 RequestedWidth = Params->HasField(TEXT("width")) ? static_cast<int32>(Params->GetNumberField(TEXT("width"))) : CaptureWidth;
			const int32 RequestedHeight = Params->HasField(TEXT("height")) ? static_cast<int32>(Params->GetNumberField(TEXT("height"))) : CaptureHeight;
			const bool bInlineImage = (Params->HasTypedField<EJson::Boolean>(TEXT("inline")) && Params->GetBoolField(TEXT("inline")))
				|| (Params->HasTypedField<EJson::Boolean>(TEXT("return_base64")) && Params->GetBoolField(TEXT("return_base64")));

			if (RequestedWidth > 0 && RequestedHeight > 0 && (RequestedWidth != CaptureWidth || RequestedHeight != CaptureHeight))
			{
				CaptureWidth = FMath::Clamp(RequestedWidth, 64, 3840);
				CaptureHeight = FMath::Clamp(RequestedHeight, 64, 2160);
				CleanupCapture();
			}

			EnsureCaptureSetup();
			if (!CaptureActor.IsValid() || !RenderTarget.IsValid())
			{
				return NovaBridgeCore::MakePlanStepResult(Step.StepIndex, TEXT("error"), TEXT("Failed to initialize scene capture"));
			}

			USceneCaptureComponent2D* CaptureComp = CaptureActor->GetCaptureComponent2D();
			CaptureActor->SetActorLocation(CameraLocation);
			CaptureActor->SetActorRotation(CameraRotation);
			CaptureComp->FOVAngle = CameraFOV;
			CaptureComp->CaptureScene();

			FTextureRenderTargetResource* RTResource = RenderTarget->GameThread_GetRenderTargetResource();
			if (!RTResource)
			{
				return NovaBridgeCore::MakePlanStepResult(Step.StepIndex, TEXT("error"), TEXT("No render target resource"));
			}

			TArray<FColor> Bitmap;
			if (!RTResource->ReadPixels(Bitmap) || Bitmap.Num() == 0)
			{
				return NovaBridgeCore::MakePlanStepResult(Step.StepIndex, TEXT("error"), TEXT("Failed to read render target pixels"));
			}

			IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));
			TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);
			ImageWrapper->SetRaw(Bitmap.GetData(), Bitmap.Num() * sizeof(FColor), CaptureWidth, CaptureHeight, ERGBFormat::BGRA, 8);
			TArray64<uint8> PngData = ImageWrapper->GetCompressed(0);
			if (PngData.Num() == 0)
			{
				return NovaBridgeCore::MakePlanStepResult(Step.StepIndex, TEXT("error"), TEXT("Failed to encode PNG"));
			}

			TSharedPtr<FJsonObject> StepResult = NovaBridgeCore::MakePlanStepResult(Step.StepIndex, TEXT("success"), TEXT("Captured screenshot"));
			StepResult->SetNumberField(TEXT("width"), CaptureWidth);
			StepResult->SetNumberField(TEXT("height"), CaptureHeight);
			StepResult->SetStringField(TEXT("format"), TEXT("png"));
			if (bInlineImage)
			{
				const FString Base64 = FBase64::Encode(PngData.GetData(), PngData.Num());
				StepResult->SetStringField(TEXT("image"), Base64);
			}

			PushAuditEntry(TEXT("/nova/executePlan"), Step.Action, Role, TEXT("success"), TEXT("Captured screenshot"));
			return StepResult;
		});

		for (int32 StepIndex = 0; StepIndex < Steps.Num(); ++StepIndex)
		{
			NovaBridgeCore::FPlanStepContext StepContext;
			TSharedPtr<FJsonObject> ParseErrorResult;
			if (!NovaBridgeCore::ExtractPlanStep(Steps[StepIndex], StepIndex, StepContext, ParseErrorResult))
			{
				StepActions[StepIndex] = TEXT("unknown");
				StepResults.Add(MakeShareable(new FJsonValueObject(
					ParseErrorResult.IsValid()
						? ParseErrorResult
						: NovaBridgeCore::MakePlanStepResult(StepIndex, TEXT("error"), TEXT("Invalid plan step")))));
				continue;
			}

			const FString& Action = StepContext.Action;
			StepActions[StepIndex] = Action;

			if (!IsPlanActionAllowedForRole(Role, Action))
			{
				const FString Message = FString::Printf(TEXT("Role '%s' cannot execute action '%s'"), *Role, *Action);
				StepResults.Add(MakeShareable(new FJsonValueObject(
					NovaBridgeCore::MakePlanStepResult(StepIndex, TEXT("error"), Message))));
				PushAuditEntry(TEXT("/nova/executePlan"), Action, Role, TEXT("denied"), Message);
				continue;
			}

			TSharedPtr<FJsonObject> StepResult;
			if (!CommandRouter.HasHandler(Action))
			{
				StepResult = NovaBridgeCore::MakePlanStepResult(
					StepIndex,
					TEXT("error"),
					FString::Printf(TEXT("Unsupported action: %s"), *Action));
				PushAuditEntry(TEXT("/nova/executePlan"), Action, Role, TEXT("error"), TEXT("Unsupported action"));
			}
			else
			{
				StepResult = CommandRouter.Dispatch(StepContext);
			}

			if (!StepResult.IsValid())
			{
				StepResult = NovaBridgeCore::MakePlanStepResult(StepIndex, TEXT("error"), TEXT("Step execution failed"));
			}
			StepResults.Add(MakeShareable(new FJsonValueObject(StepResult)));
		}

		TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
		Result->SetStringField(TEXT("status"), TEXT("ok"));
		Result->SetStringField(TEXT("plan_id"), PlanId);
		Result->SetStringField(TEXT("mode"), TEXT("editor"));
		Result->SetStringField(TEXT("role"), Role);
		Result->SetArrayField(TEXT("results"), StepResults);
		Result->SetNumberField(TEXT("step_count"), Steps.Num());

		int32 SuccessCount = 0;
		int32 ErrorCount = 0;
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

			const FString StatusValue = StepResultObj->HasTypedField<EJson::String>(TEXT("status"))
				? NovaBridgeCore::NormalizePlanAction(StepResultObj->GetStringField(TEXT("status")))
				: FString();
			if (StatusValue == TEXT("success"))
			{
				SuccessCount++;
			}
			else if (StatusValue == TEXT("error"))
			{
				ErrorCount++;
			}
		}
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

			const TSharedPtr<FJsonObject> PlanStepEvent = NovaBridgeCore::BuildPlanStepEvent(TEXT("editor"), PlanId, ResultAction, StepResultObj);
			if (PlanStepEvent.IsValid())
			{
				QueueEventObject(PlanStepEvent);
			}
		}

		const TSharedPtr<FJsonObject> PlanCompleteEvent = NovaBridgeCore::BuildPlanCompleteEvent(
			TEXT("editor"),
			PlanId,
			Steps.Num(),
			SuccessCount,
			ErrorCount,
			Role);
		QueueEventObject(PlanCompleteEvent);

		PushAuditEntry(TEXT("/nova/executePlan"), TEXT("executePlan.complete"), Role, TEXT("success"),
			FString::Printf(TEXT("Plan %s complete: %d success, %d error"), *PlanId, SuccessCount, ErrorCount));
		SendJsonResponse(OnComplete, Result);
	});
	return true;
}
