#pragma once

#include "CoreMinimal.h"

namespace NovaBridgeCore
{
NOVABRIDGECORE_API const FVector& MinSpawnBounds();
NOVABRIDGECORE_API const FVector& MaxSpawnBounds();

NOVABRIDGECORE_API const TArray<FString>& EditorAllowedSpawnClasses();
NOVABRIDGECORE_API const TArray<FString>& RuntimeAllowedSpawnClasses();

NOVABRIDGECORE_API bool IsClassAllowed(const TArray<FString>& AllowedClasses, const FString& ClassName);
NOVABRIDGECORE_API bool IsSpawnLocationInBounds(const FVector& Location);

NOVABRIDGECORE_API int32 GetEditorPlanSpawnLimit(const FString& NormalizedRole);
NOVABRIDGECORE_API bool IsEditorPlanActionAllowedForRole(const FString& NormalizedRole, const FString& Action);
NOVABRIDGECORE_API int32 GetRuntimePlanSpawnLimit(const FString& NormalizedRole);
NOVABRIDGECORE_API bool IsRuntimePlanActionAllowedForRole(const FString& NormalizedRole, const FString& Action);
} // namespace NovaBridgeCore
