#include "NovaBridgeRuntimeModule.h"

#include "NovaBridgeCoreTypes.h"
#include "NovaBridgeHttpUtils.h"

#include "Dom/JsonObject.h"
#include "HttpServerRequest.h"
#include "HttpServerResponse.h"
#include "HAL/PlatformTime.h"
#include "Misc/ScopeLock.h"
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

int32 RuntimeRoleRank(const FString& NormalizedRole)
{
	if (NormalizedRole == TEXT("admin"))
	{
		return 3;
	}
	if (NormalizedRole == TEXT("automation"))
	{
		return 2;
	}
	if (NormalizedRole == TEXT("read_only"))
	{
		return 1;
	}
	return 0;
}

bool IsRuntimeReadOnlyRoute(const FString& RoutePath)
{
	return RoutePath == TEXT("/nova/health")
		|| RoutePath == TEXT("/nova/caps")
		|| RoutePath == TEXT("/nova/events")
		|| RoutePath == TEXT("/nova/audit")
		|| RoutePath == TEXT("/nova/scene/list")
		|| RoutePath == TEXT("/nova/scene/get")
		|| RoutePath == TEXT("/nova/viewport/camera/get")
		|| RoutePath == TEXT("/nova/viewport/screenshot")
		|| RoutePath == TEXT("/nova/sequencer/info");
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

FString FNovaBridgeRuntimeModule::ResolveRuntimeRoleFromRequest(const FHttpServerRequest& Request) const
{
	FString CandidateRole = NovaBridgeCore::GetHeaderValueCaseInsensitive(Request, TEXT("X-NovaBridge-Role"));
	if (CandidateRole.IsEmpty() && Request.QueryParams.Contains(TEXT("role")))
	{
		CandidateRole = Request.QueryParams[TEXT("role")];
	}

	FString NormalizedCandidate = NovaBridgeCore::NormalizeRoleName(CandidateRole);
	const FString NormalizedTokenRole = NovaBridgeCore::NormalizeRoleName(RuntimeTokenRole);
	const FString NormalizedDefaultRole = NovaBridgeCore::NormalizeRoleName(RuntimeDefaultRole);
	const FString BaselineRole = !NormalizedTokenRole.IsEmpty()
		? NormalizedTokenRole
		: (!NormalizedDefaultRole.IsEmpty() ? NormalizedDefaultRole : FString(TEXT("automation")));

	if (NormalizedCandidate.IsEmpty())
	{
		return BaselineRole;
	}

	const int32 CandidateRank = RuntimeRoleRank(NormalizedCandidate);
	const int32 BaselineRank = RuntimeRoleRank(BaselineRole);
	if (CandidateRank <= BaselineRank)
	{
		return NormalizedCandidate;
	}

	return BaselineRole;
}

bool FNovaBridgeRuntimeModule::IsRuntimeRouteAllowedForRole(const FString& Role, const FString& RoutePath, EHttpServerRequestVerbs Verb, FString& OutReason) const
{
	if (Role == TEXT("admin"))
	{
		return true;
	}

	if (Role == TEXT("automation"))
	{
		if (RoutePath == TEXT("/nova/runtime/pair"))
		{
			OutReason = TEXT("Pair endpoint is unauthenticated-only");
			return false;
		}
		return true;
	}

	if (Role == TEXT("read_only"))
	{
		if (Verb == EHttpServerRequestVerbs::VERB_GET && IsRuntimeReadOnlyRoute(RoutePath))
		{
			return true;
		}
		OutReason = TEXT("Role does not permit write runtime endpoints");
		return false;
	}

	OutReason = TEXT("Unknown runtime role");
	return false;
}

int32 FNovaBridgeRuntimeModule::GetRuntimeRouteRateLimitPerMinute(const FString& Role, const FString& RoutePath) const
{
	if (RoutePath == TEXT("/nova/executePlan"))
	{
		if (Role == TEXT("admin"))
		{
			return MaxAdminExecutePlanPerMinute;
		}
		if (Role == TEXT("automation"))
		{
			return MaxAutomationExecutePlanPerMinute;
		}
		if (Role == TEXT("read_only"))
		{
			return MaxReadOnlyExecutePlanPerMinute;
		}
		return 0;
	}

	if (Role == TEXT("admin"))
	{
		return MaxAdminRequestsPerMinute;
	}
	if (Role == TEXT("automation"))
	{
		return MaxAutomationRequestsPerMinute;
	}
	if (Role == TEXT("read_only"))
	{
		return MaxReadOnlyRequestsPerMinute;
	}
	return 0;
}

bool FNovaBridgeRuntimeModule::ConsumeRuntimeRouteRateLimit(const FString& BucketKey, const int32 LimitPerMinute, FString& OutError)
{
	if (LimitPerMinute <= 0)
	{
		OutError = TEXT("Rate limit denied for this runtime role/action");
		return false;
	}

	const double NowSec = FPlatformTime::Seconds();
	FScopeLock Lock(&RuntimeRouteRateLimitMutex);
	FRuntimeRateBucket& Bucket = RuntimeRouteRateBuckets.FindOrAdd(BucketKey);
	if (Bucket.WindowStartSec <= 0.0 || (NowSec - Bucket.WindowStartSec) >= 60.0)
	{
		Bucket.WindowStartSec = NowSec;
		Bucket.Count = 0;
	}

	Bucket.Count++;
	if (Bucket.Count > LimitPerMinute)
	{
		OutError = FString::Printf(TEXT("Rate limit: max %d requests/minute for this runtime role/action"), LimitPerMinute);
		return false;
	}

	return true;
}

bool FNovaBridgeRuntimeModule::IsAuthorizedRuntimeToken(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete, FString* OutResolvedRole) const
{
	if (Request.Verb == EHttpServerRequestVerbs::VERB_OPTIONS || !bRequireRuntimeToken)
	{
		if (OutResolvedRole)
		{
			*OutResolvedRole = ResolveRuntimeRoleFromRequest(Request);
		}
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
		if (OutResolvedRole)
		{
			*OutResolvedRole = ResolveRuntimeRoleFromRequest(Request);
		}
		return true;
	}

	SendErrorResponse(OnComplete, TEXT("Unauthorized runtime request. Pair first and provide X-NovaBridge-Token."), 401);
	return false;
}
