#include "NovaBridgeRuntimeModule.h"

#include "NovaBridgeHttpUtils.h"

#include "Dom/JsonObject.h"
#include "HttpServerRequest.h"
#include "HttpServerResponse.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
FString NormalizeHostOnly(FString RawHost)
{
	RawHost.TrimStartAndEndInline();
	if (RawHost.IsEmpty())
	{
		return RawHost;
	}

	FString Host = RawHost;
	if (Host.StartsWith(TEXT("[")))
	{
		int32 ClosingBracketIndex = INDEX_NONE;
		if (Host.FindChar(TEXT(']'), ClosingBracketIndex))
		{
			Host = Host.Mid(1, ClosingBracketIndex - 1);
		}
	}
	else
	{
		FString HostOnly;
		FString Port;
		if (Host.Split(TEXT(":"), &HostOnly, &Port, ESearchCase::CaseSensitive, ESearchDir::FromEnd))
		{
			Host = HostOnly;
		}
	}

	Host.TrimStartAndEndInline();
	Host.ToLowerInline();
	return Host;
}

bool IsLoopbackHost(const FString& HostHeader)
{
	const FString HostOnly = NormalizeHostOnly(HostHeader);
	if (HostOnly.IsEmpty())
	{
		return true;
	}

	return HostOnly == TEXT("127.0.0.1")
		|| HostOnly == TEXT("localhost")
		|| HostOnly == TEXT("::1");
}
} // namespace

bool FNovaBridgeRuntimeModule::HandleCorsPreflight(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	(void)Request;
	const TSharedPtr<FJsonObject> JsonObj = MakeShared<FJsonObject>();
	JsonObj->SetStringField(TEXT("status"), TEXT("ok"));
	SendJsonResponse(OnComplete, JsonObj);
	return true;
}

void FNovaBridgeRuntimeModule::AddCorsHeaders(TUniquePtr<FHttpServerResponse>& Response) const
{
	if (!Response)
	{
		return;
	}

	Response->Headers.FindOrAdd(TEXT("Access-Control-Allow-Origin")).Add(TEXT("*"));
	Response->Headers.FindOrAdd(TEXT("Access-Control-Allow-Methods")).Add(TEXT("GET, POST, OPTIONS"));
	Response->Headers.FindOrAdd(TEXT("Access-Control-Allow-Headers")).Add(TEXT("Content-Type, Authorization, X-NovaBridge-Token"));
	Response->Headers.FindOrAdd(TEXT("Access-Control-Max-Age")).Add(TEXT("86400"));
}

TSharedPtr<FJsonObject> FNovaBridgeRuntimeModule::ParseRequestBody(const FHttpServerRequest& Request) const
{
	if (Request.Body.Num() == 0)
	{
		return nullptr;
	}

	TArray<uint8> NullTermBody(Request.Body);
	NullTermBody.Add(0);
	const FString BodyStr = FString(UTF8_TO_TCHAR(reinterpret_cast<const ANSICHAR*>(NullTermBody.GetData())));
	TSharedPtr<FJsonObject> JsonObj;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(BodyStr);
	if (!FJsonSerializer::Deserialize(Reader, JsonObj))
	{
		return nullptr;
	}

	return JsonObj;
}

void FNovaBridgeRuntimeModule::SendJsonResponse(const FHttpResultCallback& OnComplete, TSharedPtr<FJsonObject> JsonObj, int32 StatusCode) const
{
	FString ResponseStr;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ResponseStr);
	FJsonSerializer::Serialize(JsonObj.ToSharedRef(), Writer);
	TUniquePtr<FHttpServerResponse> Response = FHttpServerResponse::Create(ResponseStr, TEXT("application/json"));
	Response->Code = static_cast<EHttpServerResponseCodes>(StatusCode);
	AddCorsHeaders(Response);
	OnComplete(MoveTemp(Response));
}

void FNovaBridgeRuntimeModule::SendErrorResponse(const FHttpResultCallback& OnComplete, const FString& Error, int32 StatusCode) const
{
	const TSharedPtr<FJsonObject> JsonObj = MakeShared<FJsonObject>();
	JsonObj->SetStringField(TEXT("status"), TEXT("error"));
	JsonObj->SetStringField(TEXT("error"), Error);
	JsonObj->SetNumberField(TEXT("code"), StatusCode);
	SendJsonResponse(OnComplete, JsonObj, StatusCode);
}

bool FNovaBridgeRuntimeModule::IsLocalHostRequest(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete) const
{
	const FString HostHeader = NovaBridgeCore::GetHeaderValueCaseInsensitive(Request, TEXT("Host"));
	if (IsLoopbackHost(HostHeader))
	{
		return true;
	}

	SendErrorResponse(OnComplete, TEXT("Runtime server only accepts localhost requests."), 403);
	return false;
}

bool FNovaBridgeRuntimeModule::IsAuthorizedRuntimeToken(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete) const
{
	if (Request.Verb == EHttpServerRequestVerbs::VERB_OPTIONS || !bRequireRuntimeToken)
	{
		return true;
	}

	if (FDateTime::UtcNow() > RuntimeTokenExpiryUtc)
	{
		SendErrorResponse(OnComplete, TEXT("Runtime token expired. Pair again via /nova/runtime/pair."), 401);
		return false;
	}

	FString PresentedToken = NovaBridgeCore::GetHeaderValueCaseInsensitive(Request, TEXT("X-NovaBridge-Token"));
	if (PresentedToken.IsEmpty())
	{
		const FString Authorization = NovaBridgeCore::GetHeaderValueCaseInsensitive(Request, TEXT("Authorization"));
		static const FString BearerPrefix = TEXT("Bearer ");
		if (Authorization.StartsWith(BearerPrefix, ESearchCase::IgnoreCase))
		{
			PresentedToken = Authorization.Mid(BearerPrefix.Len());
		}
	}

	PresentedToken.TrimStartAndEndInline();
	if (!PresentedToken.IsEmpty() && PresentedToken == RuntimeToken)
	{
		return true;
	}

	SendErrorResponse(OnComplete, TEXT("Unauthorized runtime request. Pair first and provide X-NovaBridge-Token."), 401);
	return false;
}
