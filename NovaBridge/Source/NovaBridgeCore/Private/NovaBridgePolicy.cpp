#include "NovaBridgePolicy.h"

namespace NovaBridgeCore
{
const FVector& MinSpawnBounds()
{
	static const FVector Bounds(-50000.0, -50000.0, -50000.0);
	return Bounds;
}

const FVector& MaxSpawnBounds()
{
	static const FVector Bounds(50000.0, 50000.0, 50000.0);
	return Bounds;
}

const TArray<FString>& EditorAllowedSpawnClasses()
{
	static const TArray<FString> Classes =
	{
		TEXT("PointLight"),
		TEXT("DirectionalLight"),
		TEXT("SpotLight"),
		TEXT("StaticMeshActor"),
		TEXT("CameraActor"),
		TEXT("SkyLight"),
		TEXT("ExponentialHeightFog"),
		TEXT("PostProcessVolume"),
		TEXT("PlayerStart")
	};
	return Classes;
}

const TArray<FString>& RuntimeAllowedSpawnClasses()
{
	static const TArray<FString> Classes =
	{
		TEXT("StaticMeshActor"),
		TEXT("PointLight"),
		TEXT("DirectionalLight"),
		TEXT("SpotLight"),
		TEXT("CameraActor")
	};
	return Classes;
}

bool IsClassAllowed(const TArray<FString>& AllowedClasses, const FString& ClassName)
{
	for (const FString& AllowedClass : AllowedClasses)
	{
		if (AllowedClass.Equals(ClassName, ESearchCase::IgnoreCase))
		{
			return true;
		}
	}
	return false;
}

bool IsSpawnLocationInBounds(const FVector& Location)
{
	const FVector& MinBounds = MinSpawnBounds();
	const FVector& MaxBounds = MaxSpawnBounds();
	return Location.X >= MinBounds.X && Location.X <= MaxBounds.X
		&& Location.Y >= MinBounds.Y && Location.Y <= MaxBounds.Y
		&& Location.Z >= MinBounds.Z && Location.Z <= MaxBounds.Z;
}

int32 GetEditorPlanSpawnLimit(const FString& NormalizedRole)
{
	if (NormalizedRole == TEXT("admin"))
	{
		return 100;
	}
	if (NormalizedRole == TEXT("automation"))
	{
		return 25;
	}
	return 0;
}

bool IsEditorPlanActionAllowedForRole(const FString& NormalizedRole, const FString& Action)
{
	if (NormalizedRole == TEXT("admin"))
	{
		return true;
	}

	if (NormalizedRole == TEXT("automation"))
	{
		return Action == TEXT("spawn")
			|| Action == TEXT("delete")
			|| Action == TEXT("set")
			|| Action == TEXT("screenshot");
	}

	if (NormalizedRole == TEXT("read_only"))
	{
		return Action == TEXT("screenshot");
	}

	return false;
}
} // namespace NovaBridgeCore
