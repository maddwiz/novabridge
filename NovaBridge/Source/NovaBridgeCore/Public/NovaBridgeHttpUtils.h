#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"
#include "HttpServerRequest.h"

namespace NovaBridgeCore
{
NOVABRIDGECORE_API const TCHAR* HttpVerbToString(EHttpServerRequestVerbs Verb);

NOVABRIDGECORE_API FString GetHeaderValueCaseInsensitive(const FHttpServerRequest& Request, const FString& HeaderName);

NOVABRIDGECORE_API FString NormalizeEventType(const FString& InType);

NOVABRIDGECORE_API TArray<TSharedPtr<FJsonValue>> MakeJsonStringArray(const TArray<FString>& Values);

NOVABRIDGECORE_API TArray<FString> ParseEventTypeFilter(const FHttpServerRequest& Request);
} // namespace NovaBridgeCore
