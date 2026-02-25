#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace NovaBridgeCore
{
enum class ENovaBridgePlanMode : uint8
{
	Editor,
	Runtime
};

struct NOVABRIDGECORE_API FPlanSchemaError
{
	int32 StepIndex = INDEX_NONE;
	FString Message;
};

NOVABRIDGECORE_API TArray<FString> GetSupportedPlanActions(ENovaBridgePlanMode Mode);
NOVABRIDGECORE_API const TArray<FString>& GetSupportedPlanActionsRef(ENovaBridgePlanMode Mode);
NOVABRIDGECORE_API bool IsPlanActionSupported(ENovaBridgePlanMode Mode, const FString& Action);

NOVABRIDGECORE_API bool ValidateExecutePlanSchema(
	const TSharedPtr<FJsonObject>& Body,
	ENovaBridgePlanMode Mode,
	int32 MaxPlanSteps,
	FPlanSchemaError& OutError);
} // namespace NovaBridgeCore
