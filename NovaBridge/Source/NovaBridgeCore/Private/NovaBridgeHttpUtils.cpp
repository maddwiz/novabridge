#include "NovaBridgeHttpUtils.h"

namespace NovaBridgeCore
{
const TCHAR* HttpVerbToString(const EHttpServerRequestVerbs Verb)
{
	switch (Verb)
	{
	case EHttpServerRequestVerbs::VERB_GET: return TEXT("GET");
	case EHttpServerRequestVerbs::VERB_POST: return TEXT("POST");
	case EHttpServerRequestVerbs::VERB_PUT: return TEXT("PUT");
	case EHttpServerRequestVerbs::VERB_PATCH: return TEXT("PATCH");
	case EHttpServerRequestVerbs::VERB_DELETE: return TEXT("DELETE");
	case EHttpServerRequestVerbs::VERB_OPTIONS: return TEXT("OPTIONS");
	default: return TEXT("UNKNOWN");
	}
}

FString GetHeaderValueCaseInsensitive(const FHttpServerRequest& Request, const FString& HeaderName)
{
	for (const TPair<FString, TArray<FString>>& Header : Request.Headers)
	{
		if (Header.Key.Equals(HeaderName, ESearchCase::IgnoreCase) && Header.Value.Num() > 0)
		{
			return Header.Value[0];
		}
	}
	return FString();
}

FString NormalizeEventType(const FString& InType)
{
	FString Type = InType;
	Type.TrimStartAndEndInline();
	Type.ToLowerInline();
	return Type;
}

TArray<TSharedPtr<FJsonValue>> MakeJsonStringArray(const TArray<FString>& Values)
{
	TArray<TSharedPtr<FJsonValue>> JsonArray;
	JsonArray.Reserve(Values.Num());
	for (const FString& Value : Values)
	{
		JsonArray.Add(MakeShared<FJsonValueString>(Value));
	}
	return JsonArray;
}

TArray<FString> ParseEventTypeFilter(const FHttpServerRequest& Request)
{
	FString RawTypes;
	if (Request.QueryParams.Contains(TEXT("types")))
	{
		RawTypes = Request.QueryParams[TEXT("types")];
	}
	else if (Request.QueryParams.Contains(TEXT("type")))
	{
		RawTypes = Request.QueryParams[TEXT("type")];
	}

	TArray<FString> FilterTypes;
	if (RawTypes.IsEmpty())
	{
		return FilterTypes;
	}

	TArray<FString> Parts;
	RawTypes.ParseIntoArray(Parts, TEXT(","), true);
	for (FString& Part : Parts)
	{
		const FString Normalized = NormalizeEventType(Part);
		if (!Normalized.IsEmpty() && !FilterTypes.Contains(Normalized))
		{
			FilterTypes.Add(Normalized);
		}
	}
	return FilterTypes;
}
} // namespace NovaBridgeCore
