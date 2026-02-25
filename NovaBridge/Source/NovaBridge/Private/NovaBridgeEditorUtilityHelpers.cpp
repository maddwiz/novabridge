#include "NovaBridgeEditorInternals.h"

#include "Components/ActorComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Editor.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/SkyLight.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInterface.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UnrealType.h"

namespace
{
FString NormalizeComponentKey(FString Value)
{
	FString Out;
	Out.Reserve(Value.Len());
	for (TCHAR Ch : Value)
	{
		if (FChar::IsAlnum(Ch))
		{
			Out.AppendChar(FChar::ToLower(Ch));
		}
	}

	// Ignore trailing numeric suffixes often used in component names (e.g. LightComponent0).
	while (Out.Len() > 0 && FChar::IsDigit(Out[Out.Len() - 1]))
	{
		Out.LeftChopInline(1, EAllowShrinking::No);
	}

	return Out;
}
} // namespace

AActor* FindActorByName(const FString& Name)
{
	if (!GEditor) return nullptr;
	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World) return nullptr;

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (It->GetName() == Name || It->GetActorLabel() == Name)
		{
			return *It;
		}
	}
	return nullptr;
}

TSharedPtr<FJsonObject> ActorToJson(AActor* Actor)
{
	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetStringField(TEXT("name"), Actor->GetName());
	Obj->SetStringField(TEXT("label"), Actor->GetActorLabel());
	Obj->SetStringField(TEXT("class"), Actor->GetClass()->GetName());
	Obj->SetStringField(TEXT("path"), Actor->GetPathName());

	FVector Loc = Actor->GetActorLocation();
	FRotator Rot = Actor->GetActorRotation();
	FVector Scale = Actor->GetActorScale3D();

	TSharedPtr<FJsonObject> Transform = MakeShareable(new FJsonObject);
	TSharedPtr<FJsonObject> LocObj = MakeShareable(new FJsonObject);
	LocObj->SetNumberField(TEXT("x"), Loc.X);
	LocObj->SetNumberField(TEXT("y"), Loc.Y);
	LocObj->SetNumberField(TEXT("z"), Loc.Z);
	Transform->SetObjectField(TEXT("location"), LocObj);

	TSharedPtr<FJsonObject> RotObj = MakeShareable(new FJsonObject);
	RotObj->SetNumberField(TEXT("pitch"), Rot.Pitch);
	RotObj->SetNumberField(TEXT("yaw"), Rot.Yaw);
	RotObj->SetNumberField(TEXT("roll"), Rot.Roll);
	Transform->SetObjectField(TEXT("rotation"), RotObj);

	TSharedPtr<FJsonObject> ScaleObj = MakeShareable(new FJsonObject);
	ScaleObj->SetNumberField(TEXT("x"), Scale.X);
	ScaleObj->SetNumberField(TEXT("y"), Scale.Y);
	ScaleObj->SetNumberField(TEXT("z"), Scale.Z);
	Transform->SetObjectField(TEXT("scale"), ScaleObj);

	Obj->SetObjectField(TEXT("transform"), Transform);
	return Obj;
}

bool JsonValueToVector(const TSharedPtr<FJsonValue>& Value, FVector& OutVector)
{
	if (!Value.IsValid())
	{
		return false;
	}

	if (Value->Type == EJson::Array)
	{
		const TArray<TSharedPtr<FJsonValue>>& Arr = Value->AsArray();
		if (Arr.Num() != 3)
		{
			return false;
		}

		double X = 0.0;
		double Y = 0.0;
		double Z = 0.0;
		if (!Arr[0].IsValid() || !Arr[1].IsValid() || !Arr[2].IsValid()
			|| !Arr[0]->TryGetNumber(X) || !Arr[1]->TryGetNumber(Y) || !Arr[2]->TryGetNumber(Z))
		{
			return false;
		}

		OutVector = FVector(X, Y, Z);
		return true;
	}

	if (Value->Type == EJson::Object)
	{
		const TSharedPtr<FJsonObject> Obj = Value->AsObject();
		if (!Obj.IsValid())
		{
			return false;
		}

		if (!Obj->HasField(TEXT("x")) || !Obj->HasField(TEXT("y")) || !Obj->HasField(TEXT("z")))
		{
			return false;
		}

		OutVector = FVector(
			Obj->GetNumberField(TEXT("x")),
			Obj->GetNumberField(TEXT("y")),
			Obj->GetNumberField(TEXT("z"))
		);
		return true;
	}

	return false;
}

bool JsonValueToRotator(const TSharedPtr<FJsonValue>& Value, FRotator& OutRotator)
{
	if (!Value.IsValid())
	{
		return false;
	}

	if (Value->Type == EJson::Array)
	{
		const TArray<TSharedPtr<FJsonValue>>& Arr = Value->AsArray();
		if (Arr.Num() != 3)
		{
			return false;
		}

		double Pitch = 0.0;
		double Yaw = 0.0;
		double Roll = 0.0;
		if (!Arr[0].IsValid() || !Arr[1].IsValid() || !Arr[2].IsValid()
			|| !Arr[0]->TryGetNumber(Pitch) || !Arr[1]->TryGetNumber(Yaw) || !Arr[2]->TryGetNumber(Roll))
		{
			return false;
		}

		OutRotator = FRotator(Pitch, Yaw, Roll);
		return true;
	}

	if (Value->Type == EJson::Object)
	{
		const TSharedPtr<FJsonObject> Obj = Value->AsObject();
		if (!Obj.IsValid())
		{
			return false;
		}

		if (Obj->HasField(TEXT("pitch")) && Obj->HasField(TEXT("yaw")) && Obj->HasField(TEXT("roll")))
		{
			OutRotator = FRotator(
				Obj->GetNumberField(TEXT("pitch")),
				Obj->GetNumberField(TEXT("yaw")),
				Obj->GetNumberField(TEXT("roll"))
			);
			return true;
		}

		// Allow x/y/z naming as fallback for planner compatibility.
		if (Obj->HasField(TEXT("x")) && Obj->HasField(TEXT("y")) && Obj->HasField(TEXT("z")))
		{
			OutRotator = FRotator(
				Obj->GetNumberField(TEXT("x")),
				Obj->GetNumberField(TEXT("y")),
				Obj->GetNumberField(TEXT("z"))
			);
			return true;
		}
	}

	return false;
}

bool JsonValueToImportText(const TSharedPtr<FJsonValue>& Value, FString& OutText)
{
	if (!Value.IsValid())
	{
		return false;
	}

	switch (Value->Type)
	{
	case EJson::String:
		OutText = Value->AsString();
		return true;
	case EJson::Number:
		OutText = FString::SanitizeFloat(Value->AsNumber());
		return true;
	case EJson::Boolean:
		OutText = Value->AsBool() ? TEXT("True") : TEXT("False");
		return true;
	case EJson::Null:
		OutText.Empty();
		return true;
	default:
		return false;
	}
}

UClass* ResolveActorClassByName(const FString& InClassName)
{
	FString ClassName = InClassName;
	ClassName.TrimStartAndEndInline();
	if (ClassName.IsEmpty())
	{
		return nullptr;
	}

	UClass* ActorClass = FindObject<UClass>(nullptr, *ClassName);
	if (!ActorClass)
	{
		ActorClass = LoadClass<AActor>(nullptr, *ClassName);
	}

	if (!ActorClass)
	{
		if (ClassName == TEXT("StaticMeshActor")) ActorClass = AStaticMeshActor::StaticClass();
		else if (ClassName == TEXT("PointLight")) ActorClass = FindObject<UClass>(nullptr, TEXT("/Script/Engine.PointLight"));
		else if (ClassName == TEXT("DirectionalLight")) ActorClass = FindObject<UClass>(nullptr, TEXT("/Script/Engine.DirectionalLight"));
		else if (ClassName == TEXT("SpotLight")) ActorClass = FindObject<UClass>(nullptr, TEXT("/Script/Engine.SpotLight"));
		else if (ClassName == TEXT("CameraActor")) ActorClass = FindObject<UClass>(nullptr, TEXT("/Script/Engine.CameraActor"));
		else if (ClassName == TEXT("PlayerStart")) ActorClass = FindObject<UClass>(nullptr, TEXT("/Script/Engine.PlayerStart"));
		else if (ClassName == TEXT("SkyLight")) ActorClass = ASkyLight::StaticClass();
		else if (ClassName == TEXT("ExponentialHeightFog")) ActorClass = AExponentialHeightFog::StaticClass();
		else if (ClassName == TEXT("PostProcessVolume")) ActorClass = APostProcessVolume::StaticClass();
		if (!ActorClass)
		{
			ActorClass = FindObject<UClass>(nullptr, *(FString(TEXT("/Script/Engine.")) + ClassName));
		}
	}

	return ActorClass;
}

void ParseSpawnTransformFromParams(const TSharedPtr<FJsonObject>& Params, FVector& OutLocation, FRotator& OutRotation, FVector& OutScale)
{
	OutLocation = FVector::ZeroVector;
	OutRotation = FRotator::ZeroRotator;
	OutScale = FVector(1.0, 1.0, 1.0);
	if (!Params.IsValid())
	{
		return;
	}

	if (Params->HasField(TEXT("x"))) OutLocation.X = Params->GetNumberField(TEXT("x"));
	if (Params->HasField(TEXT("y"))) OutLocation.Y = Params->GetNumberField(TEXT("y"));
	if (Params->HasField(TEXT("z"))) OutLocation.Z = Params->GetNumberField(TEXT("z"));
	if (Params->HasField(TEXT("pitch"))) OutRotation.Pitch = Params->GetNumberField(TEXT("pitch"));
	if (Params->HasField(TEXT("yaw"))) OutRotation.Yaw = Params->GetNumberField(TEXT("yaw"));
	if (Params->HasField(TEXT("roll"))) OutRotation.Roll = Params->GetNumberField(TEXT("roll"));

	if (Params->HasTypedField<EJson::Object>(TEXT("transform")))
	{
		const TSharedPtr<FJsonObject> Transform = Params->GetObjectField(TEXT("transform"));
		if (Transform.IsValid())
		{
			if (Transform->HasField(TEXT("location")))
			{
				FVector ParsedLocation = FVector::ZeroVector;
				if (const TSharedPtr<FJsonValue>* LocationValue = Transform->Values.Find(TEXT("location")))
				{
					if (JsonValueToVector(*LocationValue, ParsedLocation))
					{
						OutLocation = ParsedLocation;
					}
				}
			}

			if (Transform->HasField(TEXT("rotation")))
			{
				FRotator ParsedRotation = FRotator::ZeroRotator;
				if (const TSharedPtr<FJsonValue>* RotationValue = Transform->Values.Find(TEXT("rotation")))
				{
					if (JsonValueToRotator(*RotationValue, ParsedRotation))
					{
						OutRotation = ParsedRotation;
					}
				}
			}

			if (Transform->HasField(TEXT("scale")))
			{
				FVector ParsedScale = FVector(1.0, 1.0, 1.0);
				if (const TSharedPtr<FJsonValue>* ScaleValue = Transform->Values.Find(TEXT("scale")))
				{
					if (JsonValueToVector(*ScaleValue, ParsedScale))
					{
						OutScale = ParsedScale;
					}
				}
			}
		}
	}
}

bool SetActorPropertyValue(AActor* Actor, const FString& PropertyName, const FString& Value, FString& OutError)
{
	if (!Actor)
	{
		OutError = TEXT("Actor is null");
		return false;
	}

	FString CompName;
	FString PropName;
	UObject* TargetObj = Actor;
	UActorComponent* FoundComp = nullptr;
	if (PropertyName.Split(TEXT("."), &CompName, &PropName))
	{
		TInlineComponentArray<UActorComponent*> Components;
		Actor->GetComponents(Components);
		for (UActorComponent* Comp : Components)
		{
			if (Comp->GetName() == CompName || Comp->GetClass()->GetName() == CompName
				|| Comp->GetName().Contains(CompName))
			{
				FoundComp = Comp;
				break;
			}
		}
		if (!FoundComp)
		{
			const FString RequestedKey = NormalizeComponentKey(CompName);
			if (!RequestedKey.IsEmpty())
			{
				for (UActorComponent* Comp : Components)
				{
					const FString CompNameKey = NormalizeComponentKey(Comp->GetName());
					FString ClassName = Comp->GetClass()->GetName();
					const FString ClassNameKey = NormalizeComponentKey(ClassName);
					if (ClassName.StartsWith(TEXT("U")))
					{
						ClassName.RightChopInline(1, EAllowShrinking::No);
					}
					const FString ClassNameNoPrefixKey = NormalizeComponentKey(ClassName);

					const bool bClassMatch =
						(RequestedKey == ClassNameKey) ||
						(RequestedKey == ClassNameNoPrefixKey) ||
						(RequestedKey.Contains(ClassNameKey)) ||
						(RequestedKey.Contains(ClassNameNoPrefixKey)) ||
						(ClassNameKey.Contains(RequestedKey)) ||
						(ClassNameNoPrefixKey.Contains(RequestedKey));

					const bool bNameMatch =
						(RequestedKey == CompNameKey) ||
						(RequestedKey.Contains(CompNameKey)) ||
						(CompNameKey.Contains(RequestedKey));

					if (bClassMatch || bNameMatch)
					{
						FoundComp = Comp;
						break;
					}
				}
			}
		}
		if (FoundComp)
		{
			TargetObj = FoundComp;

			if (PropName.StartsWith(TEXT("Material")))
			{
				if (UPrimitiveComponent* PrimComp = Cast<UPrimitiveComponent>(FoundComp))
				{
					int32 SlotIndex = 0;
					int32 BracketIdx = INDEX_NONE;
					if (PropName.FindChar('[', BracketIdx))
					{
						FString IdxStr = PropName.Mid(BracketIdx + 1).LeftChop(1);
						SlotIndex = FCString::Atoi(*IdxStr);
					}

					UMaterialInterface* Mat = LoadObject<UMaterialInterface>(nullptr, *Value);
					if (!Mat)
					{
						OutError = FString::Printf(TEXT("Material not found: %s"), *Value);
						return false;
					}

					PrimComp->SetMaterial(SlotIndex, Mat);
					PrimComp->MarkRenderStateDirty();
					Actor->PostEditChange();
					return true;
				}
			}
		}
		else
		{
			PropName = PropertyName;
		}
	}
	else
	{
		PropName = PropertyName;
	}

	FProperty* Prop = TargetObj->GetClass()->FindPropertyByName(*PropName);
	if (!Prop)
	{
		for (TFieldIterator<FProperty> It(TargetObj->GetClass()); It; ++It)
		{
			if (It->GetName().Equals(PropName, ESearchCase::IgnoreCase))
			{
				Prop = *It;
				break;
			}
		}
	}
	if (!Prop)
	{
		OutError = FString::Printf(TEXT("Property not found: %s"), *PropertyName);
		return false;
	}

	void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(TargetObj);
	if (!Prop->ImportText_Direct(*Value, ValuePtr, TargetObj, PPF_None))
	{
		OutError = FString::Printf(TEXT("Failed to set property: %s"), *PropertyName);
		return false;
	}

	if (UActorComponent* Comp = Cast<UActorComponent>(TargetObj))
	{
		Comp->MarkRenderStateDirty();
	}
	Actor->PostEditChange();
	return true;
}
