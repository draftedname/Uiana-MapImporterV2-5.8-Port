#include "UianaDataSettings.h"
#include "Internationalization/Text.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "HAL/PlatformProcess.h"
#include "HAL/FileManager.h"
#include "Interfaces/IPluginManager.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"

UUianaDataSettings::UUianaDataSettings(const FObjectInitializer& ObjectInitializer)
{
	ExportFolder.Path = TEXT("C:/Exports");
	PaksFolder.Path = TEXT("C:/Riot Games/VALORANT/live/ShooterGame/Content/Paks");
	Map = TEXT("Ascent");
	ImportMeshes = true;
	bFilterMeshes = true;
	MeshFilterPrefixes = {
		TEXT("Floater_"),
		TEXT("Wires_"),
		TEXT("Cloth_")
	};
	ImportMaterials = true;
	ImportDecals = false;
	bSkipBarrierDecals = true;
	DecalFilterPrefixes = {
		TEXT("DomeSpawnMarker_"),
		TEXT("GameObject_"),
		TEXT("RGM_"),
		TEXT("SpawnBarrier_")
	};
	ImportLights = true;
	ImportBlueprints = true;
	UseSubLevels = true;
	LightmapResolutionMultiplier = 1.0f;
}

static FString ResolveJsonPathHelper()
{
	TArray<FString> CandidatePaths;
	TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("Uiana"));
	if (Plugin.IsValid())
	{
		CandidatePaths.Add(FPaths::Combine(Plugin->GetContentDir(), TEXT("Importer"), TEXT("assets"), TEXT("umaps.json")));
		CandidatePaths.Add(FPaths::Combine(Plugin->GetBaseDir(), TEXT("Content"), TEXT("Importer"), TEXT("assets"), TEXT("umaps.json")));
	}
	CandidatePaths.Add(FPaths::Combine(FPaths::ProjectPluginsDir(), TEXT("Uiana"), TEXT("Content"), TEXT("Importer"), TEXT("assets"), TEXT("umaps.json")));

	for (const FString& Path : CandidatePaths)
	{
		if (FPaths::FileExists(Path))
		{
			return Path;
		}
	}
	return CandidatePaths.Num() > 0 ? CandidatePaths[0] : FString();
}

static FString ResolveExtractorPathHelper()
{
	TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("Uiana"));
	if (Plugin.IsValid())
	{
		FString Exe1 = FPaths::Combine(Plugin->GetContentDir(), TEXT("Importer"), TEXT("tools"), TEXT("cue4extractor.exe"));
		if (FPaths::FileExists(Exe1)) return Exe1;
		FString Exe2 = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Content"), TEXT("Importer"), TEXT("tools"), TEXT("cue4extractor.exe"));
		if (FPaths::FileExists(Exe2)) return Exe2;
	}
	FString ProjectPluginExe = FPaths::Combine(FPaths::ProjectPluginsDir(), TEXT("Uiana"), TEXT("Content"), TEXT("Importer"), TEXT("tools"), TEXT("cue4extractor.exe"));
	if (FPaths::FileExists(ProjectPluginExe)) return ProjectPluginExe;

	return FString();
}

void UUianaDataSettings::RefreshMaps()
{
	const FString TargetJson = ResolveJsonPathHelper();
	const FString ExePath = ResolveExtractorPathHelper();

	if (ExePath.IsEmpty() || !FPaths::FileExists(ExePath))
	{
		UE_LOG(LogTemp, Warning, TEXT("Uiana: cue4extractor.exe not found. Cannot refresh maps."));
		return;
	}

	FString PaksDir = PaksFolder.Path.IsEmpty() ? TEXT("C:/Riot Games/VALORANT/live/ShooterGame/Content/Paks") : PaksFolder.Path;
	FString Args = FString::Printf(TEXT("--game-directory \"%s\" --list-maps --output-json \"%s\""), *PaksDir, *TargetJson);

	int32 ReturnCode = 0;
	FString OutStd, OutErr;
	FPlatformProcess::ExecProcess(*ExePath, *Args, &ReturnCode, &OutStd, &OutErr);

	UE_LOG(LogTemp, Display, TEXT("Uiana: Refreshed maps from paks. Output: %s"), *OutStd);

	FNotificationInfo Info(FText::FromString(TEXT("Valorant Map List Refreshed!")));
	Info.ExpireDuration = 3.0f;
	FSlateNotificationManager::Get().AddNotification(Info);
}

TArray<FString> UUianaDataSettings::GetMapNames() const
{
	TArray<FString> MapNames;
	FString JsonPath = ResolveJsonPathHelper();

	// If umaps.json doesn't exist, try to generate it
	if (!FPaths::FileExists(JsonPath))
	{
		const FString ExePath = ResolveExtractorPathHelper();
		if (!ExePath.IsEmpty() && FPaths::FileExists(ExePath))
		{
			FString PaksDir = PaksFolder.Path.IsEmpty() ? TEXT("C:/Riot Games/VALORANT/live/ShooterGame/Content/Paks") : PaksFolder.Path;
			FString Args = FString::Printf(TEXT("--game-directory \"%s\" --list-maps --output-json \"%s\""), *PaksDir, *JsonPath);
			int32 ReturnCode = 0;
			FString OutStd, OutErr;
			FPlatformProcess::ExecProcess(*ExePath, *Args, &ReturnCode, &OutStd, &OutErr);
		}
	}

	if (FPaths::FileExists(JsonPath))
	{
		FString JsonContent;
		if (FFileHelper::LoadFileToString(JsonContent, *JsonPath))
		{
			TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonContent);
			TSharedPtr<FJsonObject> RootObject;
			if (FJsonSerializer::Deserialize(Reader, RootObject) && RootObject.IsValid())
			{
				for (const auto& Kvp : RootObject->Values)
				{
					FString Key = *Kvp.Key;
					if (Key.IsEmpty())
					{
						continue;
					}
					if (FChar::IsUpper(Key[0]) || Key.Contains(TEXT(" ")))
					{
						MapNames.AddUnique(Key);
					}
					else
					{
						FString CapitalizedKey = Key;
						CapitalizedKey[0] = FChar::ToUpper(CapitalizedKey[0]);
						MapNames.AddUnique(CapitalizedKey);
					}
				}
			}
		}
	}

	MapNames.Sort();
	return MapNames;
}
