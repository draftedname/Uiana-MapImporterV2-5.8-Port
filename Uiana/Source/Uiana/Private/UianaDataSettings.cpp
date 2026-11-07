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
	: Super(ObjectInitializer)
{
	ExportFolder.Path = TEXT("C:/Exports");
	PaksFolder.Path = TEXT("C:/Riot Games/VALORANT/live/ShooterGame/Content/Paks");
	Map = TEXT("Ascent");
	CurrentLoadedMap = Map;

	InitializeDefaultPresets();
	LoadPresetForMap(Map);
}

void UUianaDataSettings::PostInitProperties()
{
	Super::PostInitProperties();
	InitializeDefaultPresets();
	if (Map.IsEmpty())
	{
		Map = TEXT("Ascent");
	}
	CurrentLoadedMap = Map;
	LoadPresetForMap(Map);
}

#if WITH_EDITOR
void UUianaDataSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FName PropName = (PropertyChangedEvent.Property != nullptr) ? PropertyChangedEvent.Property->GetFName() : NAME_None;
	const FName MemberPropName = (PropertyChangedEvent.MemberProperty != nullptr) ? PropertyChangedEvent.MemberProperty->GetFName() : NAME_None;

	if (PropName == GET_MEMBER_NAME_CHECKED(UUianaDataSettings, Map) || MemberPropName == GET_MEMBER_NAME_CHECKED(UUianaDataSettings, Map))
	{
		LoadPresetForMap(Map);
	}
	else if (PropName == GET_MEMBER_NAME_CHECKED(UUianaDataSettings, MapPresets) || MemberPropName == GET_MEMBER_NAME_CHECKED(UUianaDataSettings, MapPresets))
	{
		LoadPresetForMap(Map);
	}
	else if (PropName != NAME_None &&
		PropName != GET_MEMBER_NAME_CHECKED(UUianaDataSettings, ExportFolder) &&
		PropName != GET_MEMBER_NAME_CHECKED(UUianaDataSettings, PaksFolder))
	{
		SaveCurrentPresetForMap(Map);
	}
}
#endif

void UUianaDataSettings::InitializeDefaultPresets()
{
	if (!MapPresets.Contains(TEXT("Ascent")))
	{
		FUianaMapPreset AscentPreset;
		AscentPreset.ImportMeshes = true;
		AscentPreset.bFilterMeshes = true;
		AscentPreset.MeshFilterPrefixes = {
			TEXT("Floater_"),
			TEXT("Wires_"),
			TEXT("Cloth_"),
			TEXT("BWall_"),
			TEXT("Plant_0_GroundLeavesC_"),
			TEXT("Plant_0_GroundLeavesB_"),
			TEXT("Plant_0_GroundLeavesA_"),
			TEXT("Papers_"),
			TEXT("Gravel_"),
			TEXT("Rock_"),
			TEXT("Brick_"),
			TEXT("Shell_4_Asylum")
		};
		AscentPreset.bMakeMeshesInvisible = true;
		AscentPreset.InvisibleMeshPrefixes = {
			TEXT("SuperGrid_"),
			TEXT("BP_"),
			TEXT("Box_6_")
		};
		AscentPreset.UseSubLevels = true;
		AscentPreset.ImportMaterials = true;
		AscentPreset.ImportDecals = false;
		AscentPreset.bSkipBarrierDecals = true;
		AscentPreset.DecalFilterPrefixes = {
			TEXT("DomeSpawnMarker_"),
			TEXT("GameObject_"),
			TEXT("RGM_"),
			TEXT("SpawnBarrier_")
		};
		AscentPreset.ImportLights = true;
		AscentPreset.bSpawnDefaultSunAndSky = true;
		AscentPreset.UnitlessIntensityScale = 0.1f;
		AscentPreset.bHonorJsonMobility = true;
		AscentPreset.bSpawnSkyAtmosphere = false;
		AscentPreset.bImportFog = true;
		AscentPreset.ImportBlueprints = true;
		AscentPreset.LightmapResolutionMultiplier = 1.0f;

		MapPresets.Add(TEXT("Ascent"), AscentPreset);
	}

	if (!MapPresets.Contains(TEXT("Bind")))
	{
		FUianaMapPreset BindPreset;
		BindPreset.ImportMeshes = true;
		BindPreset.bFilterMeshes = true;
		BindPreset.MeshFilterPrefixes = {
			TEXT("Floater_"),
			TEXT("Wires_"),
			TEXT("Cloth_"),
			TEXT("Papers_"),
			TEXT("Gravel_")
		};
		BindPreset.bMakeMeshesInvisible = true;
		BindPreset.InvisibleMeshPrefixes = {
			TEXT("SuperGrid_"),
			TEXT("BP_"),
			TEXT("Box_For_Volumes"),
			TEXT("Cylinder_For_Volumes"),
			TEXT("Slope_For_Volumes"),
			TEXT("Box_6_")
		};
		BindPreset.UseSubLevels = true;
		BindPreset.ImportMaterials = true;
		BindPreset.ImportDecals = false;
		BindPreset.bSkipBarrierDecals = true;
		BindPreset.DecalFilterPrefixes = {
			TEXT("DomeSpawnMarker_"),
			TEXT("GameObject_"),
			TEXT("RGM_"),
			TEXT("SpawnBarrier_")
		};
		BindPreset.ImportLights = true;
		BindPreset.bSpawnDefaultSunAndSky = true;
		BindPreset.UnitlessIntensityScale = 0.1f;
		BindPreset.bHonorJsonMobility = true;
		BindPreset.bSpawnSkyAtmosphere = false;
		BindPreset.bImportFog = true;
		BindPreset.ImportBlueprints = true;
		BindPreset.LightmapResolutionMultiplier = 1.0f;

		MapPresets.Add(TEXT("Bind"), BindPreset);
	}
}

FUianaMapPreset UUianaDataSettings::GetDefaultPresetForMap(const FString& InMapName) const
{
	FUianaMapPreset DefaultPreset;
	DefaultPreset.ImportMeshes = true;
	DefaultPreset.bFilterMeshes = true;
	DefaultPreset.MeshFilterPrefixes = {
		TEXT("Floater_"),
		TEXT("Wires_"),
		TEXT("Cloth_"),
		TEXT("Papers_"),
		TEXT("Gravel_")
	};
	DefaultPreset.bMakeMeshesInvisible = true;
	DefaultPreset.InvisibleMeshPrefixes = {
		TEXT("SuperGrid_"),
		TEXT("BP_"),
		TEXT("Box_For_Volumes"),
		TEXT("Cylinder_For_Volumes"),
		TEXT("Slope_For_Volumes"),
		TEXT("Box_6_")
	};
	DefaultPreset.UseSubLevels = true;
	DefaultPreset.ImportMaterials = true;
	DefaultPreset.ImportDecals = false;
	DefaultPreset.bSkipBarrierDecals = true;
	DefaultPreset.DecalFilterPrefixes = {
		TEXT("DomeSpawnMarker_"),
		TEXT("GameObject_"),
		TEXT("RGM_"),
		TEXT("SpawnBarrier_")
	};
	DefaultPreset.ImportLights = true;
	DefaultPreset.bSpawnDefaultSunAndSky = true;
	DefaultPreset.UnitlessIntensityScale = 0.1f;
	DefaultPreset.bHonorJsonMobility = true;
	DefaultPreset.bSpawnSkyAtmosphere = false;
	DefaultPreset.bImportFog = true;
	DefaultPreset.ImportBlueprints = true;
	DefaultPreset.LightmapResolutionMultiplier = 1.0f;
	return DefaultPreset;
}

void UUianaDataSettings::LoadPresetForMap(const FString& InMapName)
{
	const FString TargetMap = InMapName.TrimStartAndEnd();
	if (TargetMap.IsEmpty())
	{
		return;
	}

	InitializeDefaultPresets();

	FUianaMapPreset* FoundPreset = MapPresets.Find(TargetMap);
	if (!FoundPreset)
	{
		FUianaMapPreset NewPreset = GetDefaultPresetForMap(TargetMap);
		MapPresets.Add(TargetMap, NewPreset);
		FoundPreset = MapPresets.Find(TargetMap);
	}

	if (FoundPreset)
	{
		ImportMeshes = FoundPreset->ImportMeshes;
		bFilterMeshes = FoundPreset->bFilterMeshes;
		MeshFilterPrefixes = FoundPreset->MeshFilterPrefixes;
		bMakeMeshesInvisible = FoundPreset->bMakeMeshesInvisible;
		InvisibleMeshPrefixes = FoundPreset->InvisibleMeshPrefixes;
		UseSubLevels = FoundPreset->UseSubLevels;
		ImportMaterials = FoundPreset->ImportMaterials;
		ImportDecals = FoundPreset->ImportDecals;
		bSkipBarrierDecals = FoundPreset->bSkipBarrierDecals;
		DecalFilterPrefixes = FoundPreset->DecalFilterPrefixes;
		ImportLights = FoundPreset->ImportLights;
		bSpawnDefaultSunAndSky = FoundPreset->bSpawnDefaultSunAndSky;
		UnitlessIntensityScale = FoundPreset->UnitlessIntensityScale;
		bHonorJsonMobility = FoundPreset->bHonorJsonMobility;
		bSpawnSkyAtmosphere = FoundPreset->bSpawnSkyAtmosphere;
		bImportFog = FoundPreset->bImportFog;
		ImportBlueprints = FoundPreset->ImportBlueprints;
		LightmapResolutionMultiplier = FoundPreset->LightmapResolutionMultiplier;
	}
	CurrentLoadedMap = TargetMap;
}

void UUianaDataSettings::SaveCurrentPresetForMap(const FString& InMapName)
{
	const FString TargetMap = InMapName.IsEmpty() ? CurrentLoadedMap.TrimStartAndEnd() : InMapName.TrimStartAndEnd();
	if (TargetMap.IsEmpty())
	{
		return;
	}

	FUianaMapPreset& Preset = MapPresets.FindOrAdd(TargetMap);
	Preset.ImportMeshes = ImportMeshes;
	Preset.bFilterMeshes = bFilterMeshes;
	Preset.MeshFilterPrefixes = MeshFilterPrefixes;
	Preset.bMakeMeshesInvisible = bMakeMeshesInvisible;
	Preset.InvisibleMeshPrefixes = InvisibleMeshPrefixes;
	Preset.UseSubLevels = UseSubLevels;
	Preset.ImportMaterials = ImportMaterials;
	Preset.ImportDecals = ImportDecals;
	Preset.bSkipBarrierDecals = bSkipBarrierDecals;
	Preset.DecalFilterPrefixes = DecalFilterPrefixes;
	Preset.ImportLights = ImportLights;
	Preset.bSpawnDefaultSunAndSky = bSpawnDefaultSunAndSky;
	Preset.UnitlessIntensityScale = UnitlessIntensityScale;
	Preset.bHonorJsonMobility = bHonorJsonMobility;
	Preset.bSpawnSkyAtmosphere = bSpawnSkyAtmosphere;
	Preset.bImportFog = bImportFog;
	Preset.ImportBlueprints = ImportBlueprints;
	Preset.LightmapResolutionMultiplier = LightmapResolutionMultiplier;
}

void UUianaDataSettings::ResetCurrentMapPreset()
{
	if (Map.IsEmpty())
	{
		return;
	}
	MapPresets.Remove(Map);
	InitializeDefaultPresets();
	LoadPresetForMap(Map);
	SaveConfig();
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
