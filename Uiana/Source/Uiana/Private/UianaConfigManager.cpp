// Copyright 2026 Uiana Authors. All Rights Reserved.

#include "UianaConfigManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Interfaces/IPluginManager.h"
#include "JsonObjectConverter.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Materials/MaterialInterface.h"

DEFINE_LOG_CATEGORY(LogUianaConfig);

FUianaConfigManager::FUianaConfigManager()
{
}

FUianaConfigManager& FUianaConfigManager::Get()
{
	static FUianaConfigManager Instance;
	return Instance;
}

bool FUianaConfigManager::EnsureLoaded()
{
	if (!bIsLoaded)
	{
		FText OutError;
		return LoadRules(OutError);
	}
	return true;
}

bool FUianaConfigManager::LoadRules(FText& OutErrorMessage)
{
	bIsLoaded = false;
	RuleMatchCounts.Empty();

	TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("Uiana"));
	if (!Plugin.IsValid())
	{
		OutErrorMessage = FText::FromString(TEXT("Uiana plugin could not be found via IPluginManager."));
		UE_LOG(LogUianaConfig, Error, TEXT("%s"), *OutErrorMessage.ToString());
		return false;
	}

	const FString ConfigPath = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Config"), TEXT("MaterialRules.json"));
	if (!FPaths::FileExists(ConfigPath))
	{
		OutErrorMessage = FText::Format(FText::FromString(TEXT("Required material rules file is missing: {0}. Import aborted.")), FText::FromString(ConfigPath));
		UE_LOG(LogUianaConfig, Error, TEXT("%s"), *OutErrorMessage.ToString());
		return false;
	}

	FString JsonString;
	if (!FFileHelper::LoadFileToString(JsonString, *ConfigPath))
	{
		OutErrorMessage = FText::Format(FText::FromString(TEXT("Failed to read material rules file from disk: {0}. Import aborted.")), FText::FromString(ConfigPath));
		UE_LOG(LogUianaConfig, Error, TEXT("%s"), *OutErrorMessage.ToString());
		return false;
	}

	TSharedPtr<FJsonObject> JsonObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
	if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
	{
		OutErrorMessage = FText::Format(FText::FromString(TEXT("MaterialRules.json is malformed JSON ({0}). Import aborted.")), FText::FromString(Reader->GetErrorMessage()));
		UE_LOG(LogUianaConfig, Error, TEXT("%s"), *OutErrorMessage.ToString());
		return false;
	}

	FUianaMaterialRules ParsedRules;
	if (!FJsonObjectConverter::JsonObjectToUStruct(JsonObject.ToSharedRef(), &ParsedRules, 0, 0))
	{
		OutErrorMessage = FText::FromString(TEXT("Failed to convert MaterialRules.json to FUianaMaterialRules struct. Check field types and naming. Import aborted."));
		UE_LOG(LogUianaConfig, Error, TEXT("%s"), *OutErrorMessage.ToString());
		return false;
	}

	CurrentRules = MoveTemp(ParsedRules);

	if (!ValidateRules(OutErrorMessage))
	{
		return false;
	}

	// Sort ParentRules by Priority ascending (lower number = higher priority)
	CurrentRules.ParentRules.Sort([](const FUianaParentRule& A, const FUianaParentRule& B)
	{
		return A.Priority < B.Priority;
	});

	// Sort SlotOverrides by Priority ascending
	CurrentRules.SlotOverrides.Sort([](const FUianaSlotOverride& A, const FUianaSlotOverride& B)
	{
		return A.Priority < B.Priority;
	});

	bIsLoaded = true;
	UE_LOG(LogUianaConfig, Display, TEXT("FUianaConfigManager: Successfully loaded MaterialRules.json (SchemaVersion %d, %d ParentRules, %d SlotOverrides, %d Aliases)"),
		CurrentRules.SchemaVersion, CurrentRules.ParentRules.Num(), CurrentRules.SlotOverrides.Num(), CurrentRules.ParameterAliases.Num());

	return true;
}

bool FUianaConfigManager::ValidateRules(FText& OutErrorMessage)
{
	if (CurrentRules.SchemaVersion != 1)
	{
		OutErrorMessage = FText::Format(FText::FromString(TEXT("Unsupported SchemaVersion {0} in MaterialRules.json (expected 1). Import aborted.")), FText::AsNumber(CurrentRules.SchemaVersion));
		UE_LOG(LogUianaConfig, Error, TEXT("%s"), *OutErrorMessage.ToString());
		return false;
	}

	TSet<FString> SeenParentRuleNames;
	for (int32 Idx = 0; Idx < CurrentRules.ParentRules.Num(); ++Idx)
	{
		const FUianaParentRule& Rule = CurrentRules.ParentRules[Idx];
		if (Rule.RuleName.IsEmpty())
		{
			OutErrorMessage = FText::Format(FText::FromString(TEXT("ParentRule at index {0} has an empty RuleName. Import aborted.")), FText::AsNumber(Idx));
			UE_LOG(LogUianaConfig, Error, TEXT("%s"), *OutErrorMessage.ToString());
			return false;
		}

		if (SeenParentRuleNames.Contains(Rule.RuleName))
		{
			OutErrorMessage = FText::Format(FText::FromString(TEXT("Duplicate ParentRule name '{0}' at index {1}. Import aborted.")), FText::FromString(Rule.RuleName), FText::AsNumber(Idx));
			UE_LOG(LogUianaConfig, Error, TEXT("%s"), *OutErrorMessage.ToString());
			return false;
		}
		SeenParentRuleNames.Add(Rule.RuleName);

		if (Rule.TargetParent.IsEmpty())
		{
			OutErrorMessage = FText::Format(FText::FromString(TEXT("ParentRule '{0}' specifies an empty TargetParent. Import aborted.")), FText::FromString(Rule.RuleName));
			UE_LOG(LogUianaConfig, Error, TEXT("%s"), *OutErrorMessage.ToString());
			return false;
		}

		// Verify that static target master materials exist in the project or plugin
		if (Rule.TargetParent.StartsWith(TEXT("/Uiana/Materials/")) && !Rule.TargetParent.Contains(TEXT("{MaterialName}")))
		{
			if (StaticLoadObject(UMaterialInterface::StaticClass(), nullptr, *Rule.TargetParent) == nullptr)
			{
				OutErrorMessage = FText::Format(FText::FromString(TEXT("ParentRule '{0}' references nonexistent master material '{1}'. Import aborted.")),
					FText::FromString(Rule.RuleName), FText::FromString(Rule.TargetParent));
				UE_LOG(LogUianaConfig, Error, TEXT("%s"), *OutErrorMessage.ToString());
				return false;
			}
		}
	}

	TSet<FString> SeenSlotRuleNames;
	for (int32 Idx = 0; Idx < CurrentRules.SlotOverrides.Num(); ++Idx)
	{
		const FUianaSlotOverride& Override = CurrentRules.SlotOverrides[Idx];
		if (!Override.RuleName.IsEmpty())
		{
			if (SeenSlotRuleNames.Contains(Override.RuleName))
			{
				OutErrorMessage = FText::Format(FText::FromString(TEXT("Duplicate SlotOverride name '{0}' at index {1}. Import aborted.")), FText::FromString(Override.RuleName), FText::AsNumber(Idx));
				UE_LOG(LogUianaConfig, Error, TEXT("%s"), *OutErrorMessage.ToString());
				return false;
			}
			SeenSlotRuleNames.Add(Override.RuleName);
		}
	}

	return true;
}

FString FUianaConfigManager::ResolveParentMaterial(const FString& MaterialName, const FString& IncomingParentName, const FString& PackagePath)
{
	if (!bIsLoaded)
	{
		return IncomingParentName.IsEmpty() ? TEXT("/Uiana/Materials/BaseEnv_MAT_V4.BaseEnv_MAT_V4") : IncomingParentName;
	}

	for (const FUianaParentRule& Rule : CurrentRules.ParentRules)
	{
		FString TargetString;
		if (Rule.MatchTarget.Equals(TEXT("ParentName"), ESearchCase::IgnoreCase))
		{
			TargetString = IncomingParentName;
		}
		else if (Rule.MatchTarget.Equals(TEXT("PackagePath"), ESearchCase::IgnoreCase))
		{
			TargetString = PackagePath;
		}
		else
		{
			TargetString = MaterialName;
		}

		bool bMatched = false;
		const ESearchCase::Type CaseOption = Rule.bCaseSensitive ? ESearchCase::CaseSensitive : ESearchCase::IgnoreCase;

		// Check exclusion patterns first
		bool bExcluded = false;
		for (const FString& Excl : Rule.ExcludePatterns)
		{
			if (!Excl.IsEmpty() && MaterialName.Contains(Excl, CaseOption))
			{
				bExcluded = true;
				break;
			}
		}
		if (bExcluded)
		{
			continue;
		}

		for (const FString& Pat : Rule.Patterns)
		{
			if (Pat.IsEmpty()) continue;

			switch (Rule.MatchType)
			{
			case EUianaMatchType::Exact:
				bMatched = TargetString.Equals(Pat, CaseOption);
				break;
			case EUianaMatchType::Prefix:
				bMatched = TargetString.StartsWith(Pat, CaseOption);
				break;
			case EUianaMatchType::Contains:
				bMatched = TargetString.Contains(Pat, CaseOption) || PackagePath.Contains(Pat, CaseOption);
				break;
			case EUianaMatchType::Regex:
				// Fallback to Contains if regex not compiled
				bMatched = TargetString.Contains(Pat, CaseOption);
				break;
			}

			if (bMatched)
			{
				break;
			}
		}

		if (bMatched)
		{
			FString Target = Rule.TargetParent;
			if (Target.Contains(TEXT("{MaterialName}")))
			{
				Target = Target.Replace(TEXT("{MaterialName}"), *MaterialName);
				if (Target.StartsWith(TEXT("/Uiana/Materials/")))
				{
					if (StaticLoadObject(UMaterialInterface::StaticClass(), nullptr, *Target) == nullptr)
					{
						// Dynamic target asset does not exist; proceed to next rule
						continue;
					}
				}
			}

			RuleMatchCounts.FindOrAdd(Rule.RuleName)++;
			UE_LOG(LogUianaConfig, Verbose, TEXT("FUianaConfigManager: Mat '%s' matched ParentRule '%s' -> '%s'"),
				*MaterialName, *Rule.RuleName, *Target);
			return Target;
		}
	}

	return IncomingParentName;
}

FName FUianaConfigManager::ResolveParameterAlias(const FString& RawParamName, const FName& DefaultName) const
{
	if (!bIsLoaded)
	{
		return DefaultName;
	}

	const FString Lower = RawParamName.ToLower();
	if (const FString* Target = CurrentRules.ParameterAliases.Find(Lower))
	{
		return FName(**Target);
	}

	return DefaultName;
}

void FUianaConfigManager::QuerySecondaryRoutes(const FString& CurrentParentName, const FString& RawParamName, const FString& ParamType, TArray<FName>& OutRoutes) const
{
	OutRoutes.Reset();
	if (!bIsLoaded)
	{
		return;
	}

	const FString LowerParam = RawParamName.ToLower();
	for (const FUianaSecondaryParameterRoute& Route : CurrentRules.SecondaryParameterRoutes)
	{
		if (Route.Type.Equals(ParamType, ESearchCase::IgnoreCase) &&
			(Route.ParentCondition.IsEmpty() || CurrentParentName.Contains(Route.ParentCondition)))
		{
			for (const FString& Alias : Route.SourceParamAliases)
			{
				if (LowerParam.Equals(Alias.ToLower()))
				{
					OutRoutes.AddUnique(FName(*Route.TargetParam));
					break;
				}
			}
		}
	}
}

bool FUianaConfigManager::ShouldClampVectorParameter(const FString& RawParamName) const
{
	if (!bIsLoaded || !CurrentRules.Flags.bEnableDiffuseVectorClamping)
	{
		return false;
	}

	const FString Lower = RawParamName.ToLower();
	for (const FString& Param : CurrentRules.ClampVectorParameters)
	{
		if (Lower.Equals(Param.ToLower()))
		{
			return true;
		}
	}
	return false;
}

bool FUianaConfigManager::IsDccPlaceholderSlotName(const FString& SlotName) const
{
	if (!bIsLoaded)
	{
		return false;
	}

	for (const FString& Prefix : CurrentRules.DccPlaceholderPrefixes)
	{
		if (SlotName.StartsWith(Prefix, ESearchCase::IgnoreCase))
		{
			return true;
		}
	}
	return false;
}

FString FUianaConfigManager::ResolveSlotMaterial(const FString& MeshName, int32 SlotIndex, const FString& InMatName, TFunctionRef<bool(const FString&)> CandidateExistsFn)
{
	if (!bIsLoaded)
	{
		return InMatName;
	}

	for (const FUianaSlotOverride& Override : CurrentRules.SlotOverrides)
	{
		// 1. Check MeshName & SlotIndex if specified
		if (!Override.MeshName.IsEmpty())
		{
			if (!MeshName.Equals(Override.MeshName, ESearchCase::IgnoreCase))
			{
				continue;
			}
			if (Override.SlotIndex >= 0 && Override.SlotIndex != SlotIndex)
			{
				continue;
			}

			if (!Override.TargetMaterial.IsEmpty())
			{
				RuleMatchCounts.FindOrAdd(Override.RuleName)++;
				UE_LOG(LogUianaConfig, Verbose, TEXT("FUianaConfigManager: Mesh '%s' Slot %d matched SlotOverride '%s' -> '%s'"),
					*MeshName, SlotIndex, *Override.RuleName, *Override.TargetMaterial);
				return Override.TargetMaterial;
			}
		}

		// 2. Check Patterns
		bool bPatternMatch = Override.Patterns.Num() == 0;
		for (const FString& Pat : Override.Patterns)
		{
			if (InMatName.Contains(Pat))
			{
				bPatternMatch = true;
				break;
			}
		}
		if (!bPatternMatch)
		{
			continue;
		}

		// 3. Check ConditionWords
		bool bConditionMatch = Override.ConditionWords.Num() == 0;
		for (const FString& Word : Override.ConditionWords)
		{
			if (InMatName.Contains(Word))
			{
				bConditionMatch = true;
				break;
			}
		}
		if (!bConditionMatch)
		{
			continue;
		}

		// 4. Check direct TargetMaterial
		if (!Override.TargetMaterial.IsEmpty())
		{
			if (CandidateExistsFn(Override.TargetMaterial))
			{
				RuleMatchCounts.FindOrAdd(Override.RuleName)++;
				UE_LOG(LogUianaConfig, Verbose, TEXT("FUianaConfigManager: Mat '%s' matched SlotOverride '%s' -> '%s'"),
					*InMatName, *Override.RuleName, *Override.TargetMaterial);
				return Override.TargetMaterial;
			}
		}

		// 5. Check Candidate list in order
		for (const FString& Candidate : Override.Candidates)
		{
			if (CandidateExistsFn(Candidate))
			{
				RuleMatchCounts.FindOrAdd(Override.RuleName)++;
				UE_LOG(LogUianaConfig, Verbose, TEXT("FUianaConfigManager: Mat '%s' matched SlotOverride '%s' candidate -> '%s'"),
					*InMatName, *Override.RuleName, *Candidate);
				return Candidate;
			}
		}
	}

	// 6. Generic variation word stripping if enabled
	if (CurrentRules.Flags.bEnableVariationWordStripping)
	{
		// Preserve standalone nature materials
		if (InMatName.StartsWith(TEXT("Grass_"), ESearchCase::IgnoreCase) ||
			InMatName.StartsWith(TEXT("Dirt_"), ESearchCase::IgnoreCase) ||
			InMatName.StartsWith(TEXT("Sand_"), ESearchCase::IgnoreCase))
		{
			return InMatName;
		}

		for (const FString& Word : CurrentRules.VariationWords)
		{
			if (InMatName.Contains(Word))
			{
				if (Word.Contains(TEXT("Dirt")) && InMatName.Contains(TEXT("Dirty")))
				{
					continue;
				}

				FString Candidate = InMatName.Replace(*Word, TEXT(""));
				Candidate.ReplaceInline(TEXT("__"), TEXT("_"));
				if (Candidate.IsEmpty() || Candidate == InMatName)
				{
					continue;
				}

				if (CandidateExistsFn(Candidate))
				{
					RuleMatchCounts.FindOrAdd(TEXT("VariationWordStrip"))++;
					UE_LOG(LogUianaConfig, Verbose, TEXT("FUianaConfigManager: Stripped '%s' from '%s' -> '%s'"), *Word, *InMatName, *Candidate);
					return Candidate;
				}

				if (!Candidate.EndsWith(TEXT("_MI")) && InMatName.EndsWith(TEXT("_MI")))
				{
					const FString CandidateWithMI = Candidate + TEXT("_MI");
					if (CandidateExistsFn(CandidateWithMI))
					{
						RuleMatchCounts.FindOrAdd(TEXT("VariationWordStrip"))++;
						return CandidateWithMI;
					}
				}

				for (int32 MIdx = 0; MIdx <= 9; ++MIdx)
				{
					const FString FromM = FString::Printf(TEXT("_M%d_"), MIdx);
					if (Candidate.Contains(FromM))
					{
						FString CandM1 = Candidate.Replace(*FromM, TEXT("_M1_"));
						if (!CandM1.EndsWith(TEXT("_MI")) && InMatName.EndsWith(TEXT("_MI")))
						{
							CandM1 += TEXT("_MI");
						}
						if (CandidateExistsFn(CandM1))
						{
							RuleMatchCounts.FindOrAdd(TEXT("VariationWordStrip"))++;
							return CandM1;
						}
					}
				}
			}
		}
	}

	return InMatName;
}

void FUianaConfigManager::DumpMatchStatistics()
{
	UE_LOG(LogUianaConfig, Display, TEXT("========== Uiana MaterialRules Match Statistics =========="));
	for (const auto& Kvp : RuleMatchCounts)
	{
		UE_LOG(LogUianaConfig, Display, TEXT("  Rule '%s': %d matches"), *Kvp.Key, Kvp.Value);
	}
	UE_LOG(LogUianaConfig, Display, TEXT("=========================================================="));
	RuleMatchCounts.Empty();
}
