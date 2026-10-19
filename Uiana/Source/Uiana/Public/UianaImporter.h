#pragma once

#include "CoreMinimal.h"

class UUianaDataSettings;

struct FUianaImportConfig
{
	FString AESKey;
	FString TextureFormat;
	FString PluginRoot;
	FString ExportRoot;
	FString PaksRoot;
	FString MapName;

	bool bImportDecals = false;
	bool bSkipBarrierDecals = true;
	TArray<FString> DecalFilterPrefixes = {
		TEXT("DomeSpawnMarker_"),
		TEXT("GameObject_"),
		TEXT("RGM_"),
		TEXT("SpawnBarrier_")
	};
	bool bImportBlueprints = true;
	bool bImportLights = true;
	bool bImportMeshes = true;
	bool bFilterMeshes = true;
	TArray<FString> MeshFilterPrefixes = {
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
	bool bImportMaterials = true;
	bool bImportSublevels = true;
	float LightmapResolutionMultiplier = 1.0f;

	FString MapsRoot() const { return FPaths::Combine(ExportRoot, TEXT("maps")); }
	FString AssetsRoot() const { return FPaths::Combine(ExportRoot, TEXT("export")); }
	FString ToolsRoot() const { return FPaths::Combine(PluginRoot, TEXT("tools")); }
	FString UMapsListPath() const { return FPaths::Combine(PluginRoot, TEXT("assets"), TEXT("umaps.json")); }
	FString Cue4ExtractorPath() const { return FPaths::Combine(ToolsRoot(), TEXT("cue4extractor.exe")); }
};

class FUianaImporter
{
public:
	static bool Run(const UUianaDataSettings& Settings);

private:
	static bool BuildConfig(const UUianaDataSettings& Settings, FUianaImportConfig& OutConfig);
};
