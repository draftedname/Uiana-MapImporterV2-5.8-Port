#pragma once

#include "Engine/EngineTypes.h"
#include "UianaDataSettings.generated.h"

USTRUCT(BlueprintType)
struct FUianaMapPreset
{
	GENERATED_BODY()

	UPROPERTY(config, EditAnywhere, Category = "Import Settings")
	bool ImportMeshes = true;

	UPROPERTY(config, EditAnywhere, Category = "Import Settings", meta = (DisplayName = "Filter Meshes"))
	bool bFilterMeshes = true;

	UPROPERTY(config, EditAnywhere, Category = "Import Settings", meta = (DisplayName = "Mesh Filter Prefixes", EditCondition = "bFilterMeshes"))
	TArray<FString> MeshFilterPrefixes;

	UPROPERTY(config, EditAnywhere, Category = "Import Settings", meta = (DisplayName = "Make Meshes Invisible"))
	bool bMakeMeshesInvisible = true;

	UPROPERTY(config, EditAnywhere, Category = "Import Settings", meta = (DisplayName = "Invisible Mesh Prefixes", EditCondition = "bMakeMeshesInvisible"))
	TArray<FString> InvisibleMeshPrefixes;

	UPROPERTY(config, EditAnywhere, Category = "SubLevels")
	bool UseSubLevels = true;

	UPROPERTY(config, EditAnywhere, Category = "Import Settings")
	bool ImportMaterials = true;

	UPROPERTY(config, EditAnywhere, Category = "Import Settings")
	bool ImportDecals = false;

	UPROPERTY(config, EditAnywhere, Category = "Import Settings", meta = (DisplayName = "Skip Barrier and Out-Of-Bounds Decals"))
	bool bSkipBarrierDecals = true;

	UPROPERTY(config, EditAnywhere, Category = "Import Settings", meta = (DisplayName = "Decal Filter Prefixes", EditCondition = "bSkipBarrierDecals"))
	TArray<FString> DecalFilterPrefixes;

	UPROPERTY(config, EditAnywhere, Category = "Import Settings")
	bool ImportLights = true;

	UPROPERTY(config, EditAnywhere, Category = "Import Settings", meta = (DisplayName = "Spawn Default Sun & SkyLight", ToolTip = "Spawns Valorant_DirectionalLight (Static, 2.0 lux) and Valorant_SkyLight (default options)"))
	bool bSpawnDefaultSunAndSky = true;

	UPROPERTY(config, EditAnywhere, Category = "Import Settings", meta = (DisplayName = "Unitless Light Intensity Scale", ClampMin = 0.001, ClampMax = 10.0))
	float UnitlessIntensityScale = 0.1f;

	UPROPERTY(config, EditAnywhere, Category = "Import Settings", meta = (DisplayName = "Honor JSON Mobility"))
	bool bHonorJsonMobility = true;

	UPROPERTY(config, EditAnywhere, Category = "Import Settings", meta = (DisplayName = "Spawn Sky Atmosphere"))
	bool bSpawnSkyAtmosphere = false;

	UPROPERTY(config, EditAnywhere, Category = "Import Settings", meta = (DisplayName = "Import Fog"))
	bool bImportFog = true;

	UPROPERTY(config, EditAnywhere, Category = "Import Settings")
	bool ImportBlueprints = true;

	UPROPERTY(config, EditAnywhere, Category = "Import Settings", meta = (ClampMin = 0.01, ClampMax = 5))
	float LightmapResolutionMultiplier = 1.0f;
};

UCLASS(config = EditorPerProjectUserSettings, defaultconfig)
class UIANA_API UUianaDataSettings : public UObject
{
	GENERATED_BODY()
public:
	UUianaDataSettings(const FObjectInitializer& ObjectInitializer);

	virtual void PostInitProperties() override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	UPROPERTY(config, EditAnywhere, Category = "Settings Folders", meta = (RelativeToGameContentDir, ContentDir))
	FDirectoryPath ExportFolder;

	UPROPERTY(config, EditAnywhere, Category = "Settings Folders", meta = (RelativeToGameContentDir, ContentDir))
	FDirectoryPath PaksFolder;

	UFUNCTION(CallInEditor, Category = "Settings Folders")
	void RefreshMaps();

	UPROPERTY(config, EditAnywhere, Category = "Import Settings", meta = (GetOptions = "GetMapNames"))
	FString Map;

	UFUNCTION(BlueprintCallable, Category = "Import Settings")
	TArray<FString> GetMapNames() const;

	UPROPERTY(config, EditAnywhere, Category = "Import Settings")
	bool ImportMeshes;

	UPROPERTY(config, EditAnywhere, Category = "Import Settings", meta = (DisplayName = "Filter Meshes"))
	bool bFilterMeshes;

	UPROPERTY(config, EditAnywhere, Category = "Import Settings", meta = (DisplayName = "Mesh Filter Prefixes", EditCondition = "bFilterMeshes"))
	TArray<FString> MeshFilterPrefixes;

	UPROPERTY(config, EditAnywhere, Category = "Import Settings", meta = (DisplayName = "Make Meshes Invisible"))
	bool bMakeMeshesInvisible;

	UPROPERTY(config, EditAnywhere, Category = "Import Settings", meta = (DisplayName = "Invisible Mesh Prefixes", EditCondition = "bMakeMeshesInvisible"))
	TArray<FString> InvisibleMeshPrefixes;

	UPROPERTY(config, EditAnywhere, Category = "SubLevels")
	bool UseSubLevels;

	UPROPERTY(config, EditAnywhere, Category = "Import Settings")
	bool ImportMaterials;

	UPROPERTY(config, EditAnywhere, Category = "Import Settings")
	bool ImportDecals;

	UPROPERTY(config, EditAnywhere, Category = "Import Settings", meta = (DisplayName = "Skip Barrier and Out-Of-Bounds Decals"))
	bool bSkipBarrierDecals;

	UPROPERTY(config, EditAnywhere, Category = "Import Settings", meta = (DisplayName = "Decal Filter Prefixes", EditCondition = "bSkipBarrierDecals"))
	TArray<FString> DecalFilterPrefixes;

	UPROPERTY(config, EditAnywhere, Category = "Import Settings")
	bool ImportLights;

	UPROPERTY(config, EditAnywhere, Category = "Import Settings", meta = (DisplayName = "Spawn Default Sun & SkyLight", ToolTip = "Spawns Valorant_DirectionalLight (Static, 2.0 lux) and Valorant_SkyLight (default options)"))
	bool bSpawnDefaultSunAndSky = true;

	UPROPERTY(config, EditAnywhere, Category = "Import Settings", meta = (DisplayName = "Unitless Light Intensity Scale", ClampMin = 0.001, ClampMax = 10.0))
	float UnitlessIntensityScale = 0.1f;

	UPROPERTY(config, EditAnywhere, Category = "Import Settings", meta = (DisplayName = "Honor JSON Mobility"))
	bool bHonorJsonMobility = true;

	UPROPERTY(config, EditAnywhere, Category = "Import Settings", meta = (DisplayName = "Spawn Sky Atmosphere"))
	bool bSpawnSkyAtmosphere = false;

	UPROPERTY(config, EditAnywhere, Category = "Import Settings", meta = (DisplayName = "Import Fog"))
	bool bImportFog = true;

	UPROPERTY(config, EditAnywhere, Category = "Import Settings")
	bool ImportBlueprints;

	UPROPERTY(config, EditAnywhere, Category = "Import Settings", meta=(ClampMin=0.01, ClampMax=5))
	float LightmapResolutionMultiplier;

	UPROPERTY(config, EditAnywhere, Category = "Map Presets", meta = (DisplayName = "Map Presets"))
	TMap<FString, FUianaMapPreset> MapPresets;

	UFUNCTION(BlueprintCallable, Category = "Map Presets")
	void LoadPresetForMap(const FString& InMapName);

	UFUNCTION(BlueprintCallable, Category = "Map Presets")
	void SaveCurrentPresetForMap(const FString& InMapName);

	UFUNCTION(CallInEditor, Category = "Map Presets", meta = (DisplayName = "Reset Current Map Preset to Default"))
	void ResetCurrentMapPreset();

	FUianaMapPreset GetDefaultPresetForMap(const FString& InMapName) const;
	void InitializeDefaultPresets();

private:
	UPROPERTY(Transient)
	FString CurrentLoadedMap;
};
