// Copyright 2026 Uiana Authors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Internationalization/Regex.h"
#include "UianaMaterialRules.h"

DECLARE_LOG_CATEGORY_EXTERN(LogUianaConfig, Log, All);

class UIANA_API FUianaConfigManager
{
public:
	static FUianaConfigManager& Get();

	/** Reloads Config/MaterialRules.json from disk and validates schema & integrity */
	bool LoadRules(FText& OutErrorMessage);

	/** Returns true if rules have been successfully loaded */
	bool IsLoaded() const { return bIsLoaded; }

	/** Ensures rules are loaded; loads them if not yet loaded */
	bool EnsureLoaded();

	/** Access currently loaded rules */
	const FUianaMaterialRules& GetRules() const { return CurrentRules; }

	/** Resolve a target parent master material given MaterialName, incoming ParentName, and PackagePath */
	FString ResolveParentMaterial(const FString& MaterialName, const FString& IncomingParentName, const FString& PackagePath);

	/** Resolve canonical parameter name given raw JSON parameter name */
	FName ResolveParameterAlias(const FString& RawParamName, const FName& DefaultName) const;

	/** Query secondary routes (e.g. RGBA for BaseOpacity_RGB_Env_MAT) */
	void QuerySecondaryRoutes(const FString& CurrentParentName, const FString& RawParamName, const FString& ParamType, TArray<FName>& OutRoutes) const;

	/** Check if vector parameter should be clamped to [0.0, 1.0] */
	bool ShouldClampVectorParameter(const FString& RawParamName) const;

	/** Check if parameter is deliberately ignored (e.g. low-LOD streaming variants, debug pins) */
	bool IsParameterIgnored(const FString& RawParamName) const;

	/** Check if slot name is a DCC placeholder (lambert, phong, etc.) */
	bool IsDccPlaceholderSlotName(const FString& SlotName) const;

	/** Resolve material slot variation / override */
	FString ResolveSlotMaterial(const FString& MeshName, int32 SlotIndex, const FString& InMatName, TFunctionRef<bool(const FString&)> CandidateExistsFn);

	/** Log per-rule match statistics and reset counters */
	void DumpMatchStatistics();

private:
	FUianaConfigManager();
	bool ValidateRules(FText& OutErrorMessage);

	/** Returns a compiled regex for Pattern, compiling and caching it on first use. Null on invalid pattern. */
	const FRegexPattern* GetCompiledRegex(const FString& Pattern, bool bCaseSensitive);

	FUianaMaterialRules CurrentRules;
	bool bIsLoaded = false;

	// Compiled regex cache for EUianaMatchType::Regex rules; cleared on every reload.
	TMap<FString, TSharedPtr<FRegexPattern>> RegexCache;

	// Runtime match counters for telemetry
	TMap<FString, int32> RuleMatchCounts;
};
