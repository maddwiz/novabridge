#include "NovaBridgePlanSchema.h"

#include "Dom/JsonValue.h"

namespace NovaBridgeCore
{
namespace
{
static FString NormalizeAction(const FString& InAction)
{
	FString Action = InAction;
	Action.TrimStartAndEndInline();
	Action.ToLowerInline();
	return Action;
}

static bool IsNumericJsonValue(const TSharedPtr<FJsonValue>& Value)
{
	if (!Value.IsValid())
	{
		return false;
	}
	double NumberValue = 0.0;
	return Value->TryGetNumber(NumberValue);
}

static bool IsVectorJsonValue(const TSharedPtr<FJsonValue>& Value)
{
	if (!Value.IsValid())
	{
		return false;
	}

	if (Value->Type == EJson::Array)
	{
		const TArray<TSharedPtr<FJsonValue>>& Values = Value->AsArray();
		if (Values.Num() != 3)
		{
			return false;
		}

		return IsNumericJsonValue(Values[0]) && IsNumericJsonValue(Values[1]) && IsNumericJsonValue(Values[2]);
	}

	if (Value->Type == EJson::Object)
	{
		const TSharedPtr<FJsonObject> Obj = Value->AsObject();
		if (!Obj.IsValid())
		{
			return false;
		}

		const TSharedPtr<FJsonValue>* X = Obj->Values.Find(TEXT("x"));
		const TSharedPtr<FJsonValue>* Y = Obj->Values.Find(TEXT("y"));
		const TSharedPtr<FJsonValue>* Z = Obj->Values.Find(TEXT("z"));
		return X && Y && Z && IsNumericJsonValue(*X) && IsNumericJsonValue(*Y) && IsNumericJsonValue(*Z);
	}

	return false;
}

static bool IsRotatorJsonValue(const TSharedPtr<FJsonValue>& Value)
{
	if (!Value.IsValid())
	{
		return false;
	}

	if (Value->Type == EJson::Array)
	{
		const TArray<TSharedPtr<FJsonValue>>& Values = Value->AsArray();
		if (Values.Num() != 3)
		{
			return false;
		}

		return IsNumericJsonValue(Values[0]) && IsNumericJsonValue(Values[1]) && IsNumericJsonValue(Values[2]);
	}

	if (Value->Type == EJson::Object)
	{
		const TSharedPtr<FJsonObject> Obj = Value->AsObject();
		if (!Obj.IsValid())
		{
			return false;
		}

		const TSharedPtr<FJsonValue>* Pitch = Obj->Values.Find(TEXT("pitch"));
		const TSharedPtr<FJsonValue>* Yaw = Obj->Values.Find(TEXT("yaw"));
		const TSharedPtr<FJsonValue>* Roll = Obj->Values.Find(TEXT("roll"));
		if (Pitch && Yaw && Roll)
		{
			return IsNumericJsonValue(*Pitch) && IsNumericJsonValue(*Yaw) && IsNumericJsonValue(*Roll);
		}

		const TSharedPtr<FJsonValue>* X = Obj->Values.Find(TEXT("x"));
		const TSharedPtr<FJsonValue>* Y = Obj->Values.Find(TEXT("y"));
		const TSharedPtr<FJsonValue>* Z = Obj->Values.Find(TEXT("z"));
		return X && Y && Z && IsNumericJsonValue(*X) && IsNumericJsonValue(*Y) && IsNumericJsonValue(*Z);
	}

	return false;
}

static bool RejectUnknownFields(
	const TSharedPtr<FJsonObject>& Obj,
	const TSet<FString>& AllowedFields,
	FString& OutUnknownField)
{
	if (!Obj.IsValid())
	{
		return false;
	}

	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Obj->Values)
	{
		if (!AllowedFields.Contains(Pair.Key))
		{
			OutUnknownField = Pair.Key;
			return true;
		}
	}

	return false;
}

static bool ValidateSpawnParams(
	const TSharedPtr<FJsonObject>& Params,
	ENovaBridgePlanMode Mode,
	FString& OutError)
{
	const TSet<FString> AllowedSpawnFields =
	{
		TEXT("class"),
		TEXT("type"),
		TEXT("label"),
		TEXT("x"),
		TEXT("y"),
		TEXT("z"),
		TEXT("pitch"),
		TEXT("yaw"),
		TEXT("roll"),
		TEXT("transform")
	};

	FString UnknownField;
	if (RejectUnknownFields(Params, AllowedSpawnFields, UnknownField))
	{
		OutError = FString::Printf(TEXT("Unknown spawn param field: %s"), *UnknownField);
		return false;
	}

	if (Params->HasField(TEXT("class")) && !Params->HasTypedField<EJson::String>(TEXT("class")))
	{
		OutError = TEXT("spawn.params.class must be a string");
		return false;
	}
	if (Params->HasField(TEXT("type")) && !Params->HasTypedField<EJson::String>(TEXT("type")))
	{
		OutError = TEXT("spawn.params.type must be a string");
		return false;
	}
	if (Params->HasField(TEXT("label")) && !Params->HasTypedField<EJson::String>(TEXT("label")))
	{
		OutError = TEXT("spawn.params.label must be a string");
		return false;
	}

	const TArray<FString> NumericFields = { TEXT("x"), TEXT("y"), TEXT("z"), TEXT("pitch"), TEXT("yaw"), TEXT("roll") };
	for (const FString& NumericField : NumericFields)
	{
		if (!Params->HasField(NumericField))
		{
			continue;
		}

		const TSharedPtr<FJsonValue>* Value = Params->Values.Find(NumericField);
		if (!Value || !IsNumericJsonValue(*Value))
		{
			OutError = FString::Printf(TEXT("spawn.params.%s must be numeric"), *NumericField);
			return false;
		}
	}

	if (Params->HasField(TEXT("transform")))
	{
		if (!Params->HasTypedField<EJson::Object>(TEXT("transform")))
		{
			OutError = TEXT("spawn.params.transform must be an object");
			return false;
		}

		const TSharedPtr<FJsonObject> Transform = Params->GetObjectField(TEXT("transform"));
		TSet<FString> AllowedTransformFields = { TEXT("location"), TEXT("rotation") };
		if (Mode == ENovaBridgePlanMode::Editor)
		{
			AllowedTransformFields.Add(TEXT("scale"));
		}
		else
		{
			// Runtime currently ignores scale, but keep it schema-legal for compatibility.
			AllowedTransformFields.Add(TEXT("scale"));
		}

		if (RejectUnknownFields(Transform, AllowedTransformFields, UnknownField))
		{
			OutError = FString::Printf(TEXT("Unknown spawn.transform field: %s"), *UnknownField);
			return false;
		}

		if (Transform->HasField(TEXT("location")))
		{
			const TSharedPtr<FJsonValue>* Location = Transform->Values.Find(TEXT("location"));
			if (!Location || !IsVectorJsonValue(*Location))
			{
				OutError = TEXT("spawn.params.transform.location must be [x,y,z] or {x,y,z}");
				return false;
			}
		}
		if (Transform->HasField(TEXT("rotation")))
		{
			const TSharedPtr<FJsonValue>* Rotation = Transform->Values.Find(TEXT("rotation"));
			if (!Rotation || !IsRotatorJsonValue(*Rotation))
			{
				OutError = TEXT("spawn.params.transform.rotation must be [pitch,yaw,roll] or object");
				return false;
			}
		}
		if (Transform->HasField(TEXT("scale")))
		{
			const TSharedPtr<FJsonValue>* Scale = Transform->Values.Find(TEXT("scale"));
			if (!Scale || !IsVectorJsonValue(*Scale))
			{
				OutError = TEXT("spawn.params.transform.scale must be [x,y,z] or {x,y,z}");
				return false;
			}
		}
	}

	return true;
}

static bool ValidateDeleteParams(const TSharedPtr<FJsonObject>& Params, FString& OutError)
{
	const TSet<FString> AllowedDeleteFields =
	{
		TEXT("name"),
		TEXT("target")
	};

	FString UnknownField;
	if (RejectUnknownFields(Params, AllowedDeleteFields, UnknownField))
	{
		OutError = FString::Printf(TEXT("Unknown delete param field: %s"), *UnknownField);
		return false;
	}

	if (Params->HasField(TEXT("name")) && !Params->HasTypedField<EJson::String>(TEXT("name")))
	{
		OutError = TEXT("delete.params.name must be a string");
		return false;
	}
	if (Params->HasField(TEXT("target")) && !Params->HasTypedField<EJson::String>(TEXT("target")))
	{
		OutError = TEXT("delete.params.target must be a string");
		return false;
	}
	return true;
}

static bool ValidateSetParams(const TSharedPtr<FJsonObject>& Params, FString& OutError)
{
	const TSet<FString> AllowedSetFields =
	{
		TEXT("target"),
		TEXT("name"),
		TEXT("props")
	};

	FString UnknownField;
	if (RejectUnknownFields(Params, AllowedSetFields, UnknownField))
	{
		OutError = FString::Printf(TEXT("Unknown set param field: %s"), *UnknownField);
		return false;
	}

	if (Params->HasField(TEXT("target")) && !Params->HasTypedField<EJson::String>(TEXT("target")))
	{
		OutError = TEXT("set.params.target must be a string");
		return false;
	}
	if (Params->HasField(TEXT("name")) && !Params->HasTypedField<EJson::String>(TEXT("name")))
	{
		OutError = TEXT("set.params.name must be a string");
		return false;
	}
	if (!Params->HasTypedField<EJson::Object>(TEXT("props")))
	{
		OutError = TEXT("set.params.props must be an object");
		return false;
	}
	return true;
}

static bool ValidateScreenshotParams(const TSharedPtr<FJsonObject>& Params, FString& OutError)
{
	const TSet<FString> AllowedScreenshotFields =
	{
		TEXT("width"),
		TEXT("height"),
		TEXT("inline"),
		TEXT("return_base64"),
		TEXT("format")
	};

	FString UnknownField;
	if (RejectUnknownFields(Params, AllowedScreenshotFields, UnknownField))
	{
		OutError = FString::Printf(TEXT("Unknown screenshot param field: %s"), *UnknownField);
		return false;
	}

	if (Params->HasField(TEXT("width")))
	{
		const TSharedPtr<FJsonValue>* Width = Params->Values.Find(TEXT("width"));
		if (!Width || !IsNumericJsonValue(*Width))
		{
			OutError = TEXT("screenshot.params.width must be numeric");
			return false;
		}
	}
	if (Params->HasField(TEXT("height")))
	{
		const TSharedPtr<FJsonValue>* Height = Params->Values.Find(TEXT("height"));
		if (!Height || !IsNumericJsonValue(*Height))
		{
			OutError = TEXT("screenshot.params.height must be numeric");
			return false;
		}
	}
	if (Params->HasField(TEXT("inline")) && !Params->HasTypedField<EJson::Boolean>(TEXT("inline")))
	{
		OutError = TEXT("screenshot.params.inline must be boolean");
		return false;
	}
	if (Params->HasField(TEXT("return_base64")) && !Params->HasTypedField<EJson::Boolean>(TEXT("return_base64")))
	{
		OutError = TEXT("screenshot.params.return_base64 must be boolean");
		return false;
	}
	if (Params->HasField(TEXT("format")) && !Params->HasTypedField<EJson::String>(TEXT("format")))
	{
		OutError = TEXT("screenshot.params.format must be a string");
		return false;
	}
	return true;
}
} // namespace

const TArray<FString>& GetSupportedPlanActionsRef(const ENovaBridgePlanMode Mode)
{
	static const TArray<FString> RuntimeActions = { TEXT("spawn"), TEXT("delete"), TEXT("set") };
	static const TArray<FString> EditorActions = { TEXT("spawn"), TEXT("delete"), TEXT("set"), TEXT("screenshot") };

	if (Mode == ENovaBridgePlanMode::Runtime)
	{
		return RuntimeActions;
	}

	return EditorActions;
}

TArray<FString> GetSupportedPlanActions(const ENovaBridgePlanMode Mode)
{
	return GetSupportedPlanActionsRef(Mode);
}

bool IsPlanActionSupported(const ENovaBridgePlanMode Mode, const FString& Action)
{
	const FString NormalizedAction = NormalizeAction(Action);
	const TArray<FString>& SupportedActions = GetSupportedPlanActionsRef(Mode);
	return SupportedActions.Contains(NormalizedAction);
}

bool ValidateExecutePlanSchema(
	const TSharedPtr<FJsonObject>& Body,
	const ENovaBridgePlanMode Mode,
	const int32 MaxPlanSteps,
	FPlanSchemaError& OutError)
{
	OutError = FPlanSchemaError();
	if (!Body.IsValid())
	{
		OutError.Message = TEXT("Invalid JSON body");
		return false;
	}

	TSet<FString> AllowedTopLevelFields = { TEXT("plan_id"), TEXT("steps") };
	if (Mode == ENovaBridgePlanMode::Editor)
	{
		AllowedTopLevelFields.Add(TEXT("role"));
	}

	FString UnknownField;
	if (RejectUnknownFields(Body, AllowedTopLevelFields, UnknownField))
	{
		OutError.Message = FString::Printf(TEXT("Unknown plan field: %s"), *UnknownField);
		return false;
	}

	if (Body->HasField(TEXT("plan_id")) && !Body->HasTypedField<EJson::String>(TEXT("plan_id")))
	{
		OutError.Message = TEXT("plan_id must be a string");
		return false;
	}

	if (Body->HasField(TEXT("role")) && !Body->HasTypedField<EJson::String>(TEXT("role")))
	{
		OutError.Message = TEXT("role must be a string");
		return false;
	}

	if (!Body->HasTypedField<EJson::Array>(TEXT("steps")))
	{
		OutError.Message = TEXT("Missing required field: steps");
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>> Steps = Body->GetArrayField(TEXT("steps"));
	if (Steps.Num() == 0)
	{
		OutError.Message = TEXT("Plan has no steps");
		return false;
	}
	if (MaxPlanSteps > 0 && Steps.Num() > MaxPlanSteps)
	{
		OutError.Message = FString::Printf(TEXT("Plan exceeds max step count (%d)"), MaxPlanSteps);
		return false;
	}

	const TSet<FString> AllowedStepFields = { TEXT("action"), TEXT("params") };
	for (int32 StepIndex = 0; StepIndex < Steps.Num(); ++StepIndex)
	{
		const TSharedPtr<FJsonValue>& StepValue = Steps[StepIndex];
		if (!StepValue.IsValid() || StepValue->Type != EJson::Object)
		{
			OutError.StepIndex = StepIndex;
			OutError.Message = TEXT("Step must be an object");
			return false;
		}

		const TSharedPtr<FJsonObject> StepObj = StepValue->AsObject();
		if (!StepObj.IsValid())
		{
			OutError.StepIndex = StepIndex;
			OutError.Message = TEXT("Step must be an object");
			return false;
		}

		if (RejectUnknownFields(StepObj, AllowedStepFields, UnknownField))
		{
			OutError.StepIndex = StepIndex;
			OutError.Message = FString::Printf(TEXT("Unknown step field: %s"), *UnknownField);
			return false;
		}

		if (!StepObj->HasTypedField<EJson::String>(TEXT("action")))
		{
			OutError.StepIndex = StepIndex;
			OutError.Message = TEXT("Missing step action");
			return false;
		}

		const FString Action = NormalizeAction(StepObj->GetStringField(TEXT("action")));
		if (Action.IsEmpty())
		{
			OutError.StepIndex = StepIndex;
			OutError.Message = TEXT("Step action must be a non-empty string");
			return false;
		}
		if (!IsPlanActionSupported(Mode, Action))
		{
			OutError.StepIndex = StepIndex;
			OutError.Message = FString::Printf(TEXT("Unsupported action: %s"), *Action);
			return false;
		}

		if (StepObj->HasField(TEXT("params")) && !StepObj->HasTypedField<EJson::Object>(TEXT("params")))
		{
			OutError.StepIndex = StepIndex;
			OutError.Message = TEXT("Step params must be an object");
			return false;
		}

		const TSharedPtr<FJsonObject> Params = StepObj->HasTypedField<EJson::Object>(TEXT("params"))
			? StepObj->GetObjectField(TEXT("params"))
			: MakeShared<FJsonObject>();

		FString ValidationError;
		if (Action == TEXT("spawn"))
		{
			if (!ValidateSpawnParams(Params, Mode, ValidationError))
			{
				OutError.StepIndex = StepIndex;
				OutError.Message = ValidationError;
				return false;
			}
			continue;
		}

		if (Action == TEXT("delete"))
		{
			if (!ValidateDeleteParams(Params, ValidationError))
			{
				OutError.StepIndex = StepIndex;
				OutError.Message = ValidationError;
				return false;
			}
			continue;
		}

		if (Action == TEXT("set"))
		{
			if (!ValidateSetParams(Params, ValidationError))
			{
				OutError.StepIndex = StepIndex;
				OutError.Message = ValidationError;
				return false;
			}
			continue;
		}

		if (Action == TEXT("screenshot"))
		{
			if (!ValidateScreenshotParams(Params, ValidationError))
			{
				OutError.StepIndex = StepIndex;
				OutError.Message = ValidationError;
				return false;
			}
			continue;
		}
	}

	return true;
}
} // namespace NovaBridgeCore
