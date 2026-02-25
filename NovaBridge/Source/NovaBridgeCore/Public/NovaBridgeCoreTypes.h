#pragma once

#include "CoreMinimal.h"

namespace NovaBridgeCore
{
static const TCHAR* PluginVersion = TEXT("1.0.2");
static const int32 EditorDefaultHttpPort = 30010;
static const int32 RuntimeDefaultHttpPort = 30020;

enum class ENovaBridgeRole : uint8
{
	Admin,
	Automation,
	ReadOnly,
	Unknown,
};

inline FString NormalizeRoleName(FString RawRole)
{
	RawRole.TrimStartAndEndInline();
	RawRole.ToLowerInline();
	if (RawRole == TEXT("admin"))
	{
		return TEXT("admin");
	}
	if (RawRole == TEXT("automation") || RawRole == TEXT("auto"))
	{
		return TEXT("automation");
	}
	if (RawRole == TEXT("read_only") || RawRole == TEXT("read-only") || RawRole == TEXT("readonly") || RawRole == TEXT("read"))
	{
		return TEXT("read_only");
	}
	return FString();
}

inline ENovaBridgeRole ParseRole(const FString& RawRole)
{
	const FString NormalizedRole = NormalizeRoleName(RawRole);
	if (NormalizedRole == TEXT("admin"))
	{
		return ENovaBridgeRole::Admin;
	}
	if (NormalizedRole == TEXT("automation"))
	{
		return ENovaBridgeRole::Automation;
	}
	if (NormalizedRole == TEXT("read_only"))
	{
		return ENovaBridgeRole::ReadOnly;
	}
	return ENovaBridgeRole::Unknown;
}

inline FString RoleToString(const ENovaBridgeRole Role)
{
	switch (Role)
	{
	case ENovaBridgeRole::Admin: return TEXT("admin");
	case ENovaBridgeRole::Automation: return TEXT("automation");
	case ENovaBridgeRole::ReadOnly: return TEXT("read_only");
	default: return TEXT("unknown");
	}
}
} // namespace NovaBridgeCore
