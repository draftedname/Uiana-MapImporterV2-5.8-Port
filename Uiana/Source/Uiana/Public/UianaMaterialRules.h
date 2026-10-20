// Copyright 2026 Uiana Authors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "UianaMaterialRules.generated.h"

UENUM(BlueprintType)
enum class EUianaMatchType : uint8
{
	Exact UMETA(DisplayName = "Exact"),
	Prefix UMETA(DisplayName = "Prefix"),
	Contains UMETA(DisplayName = "Contains"),
	Regex UMETA(DisplayName = "Regex")
};

USTRUCT(BlueprintType)
struct FUianaParentRule
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rules")
	FString RuleName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rules")
	int32 Priority = 100;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rules")
	FString MatchTarget = TEXT("MaterialName"); // MaterialName, ParentName, or PackagePath

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rules")
	EUianaMatchType MatchType = EUianaMatchType::Prefix;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rules")
	bool bCaseSensitive = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rules")
	TArray<FString> Patterns;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rules")
	TArray<FString> ExcludePatterns;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rules")
	FString TargetParent;
};

USTRUCT(BlueprintType)
struct FUianaSecondaryParameterRoute
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rules")
	FString ParentCondition;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rules")
	TArray<FString> SourceParamAliases;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rules")
	FString TargetParam;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rules")
	FString Type = TEXT("Texture"); // Texture or Vector
};

USTRUCT(BlueprintType)
struct FUianaSlotOverride
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rules")
	FString RuleName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rules")
	int32 Priority = 100;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rules")
	FString MeshName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rules")
	int32 SlotIndex = -1; // -1 means any slot

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rules")
	TArray<FString> Patterns;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rules")
	TArray<FString> ConditionWords;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rules")
	TArray<FString> Candidates;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rules")
	FString TargetMaterial;
};

USTRUCT(BlueprintType)
struct FUianaRuleFlags
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rules")
	bool bEnableHismMixedSlotGuard = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rules")
	bool bEnableDiffuseVectorClamping = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rules")
	bool bEnableReferencedTextureFallback = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rules")
	bool bEnableVariationWordStripping = true;
};

USTRUCT(BlueprintType)
struct FUianaMaterialRules
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rules")
	int32 SchemaVersion = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rules")
	FUianaRuleFlags Flags;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rules")
	TArray<FString> ClampVectorParameters;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rules")
	TMap<FString, FString> ParameterAliases; // e.g. "materialtexture" -> "diffuse"

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rules")
	TArray<FUianaSecondaryParameterRoute> SecondaryParameterRoutes;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rules")
	TArray<FString> DccPlaceholderPrefixes;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rules")
	TArray<FUianaParentRule> ParentRules;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rules")
	TArray<FUianaSlotOverride> SlotOverrides;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rules")
	TArray<FString> VariationWords;
};
