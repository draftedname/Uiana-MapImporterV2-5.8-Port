#pragma once

#include "Engine/EngineTypes.h"
#include "UianaDataSettings.generated.h"

UCLASS(config = Engine, defaultconfig, transient)
class UIANA_API UUianaDataSettings : public UObject
{
	GENERATED_BODY()
public:
	UUianaDataSettings(const FObjectInitializer& ObjectInitializer);

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

	UPROPERTY(config, EditAnywhere, Category = "SubLevels")
	bool UseSubLevels;

	UPROPERTY(config, EditAnywhere, Category = "Import Settings")
	bool ImportMaterials;

	UPROPERTY(config, EditAnywhere, Category = "Import Settings")
	bool ImportDecals;

	UPROPERTY(config, EditAnywhere, Category = "Import Settings")
	bool ImportLights;

	UPROPERTY(config, EditAnywhere, Category = "Import Settings")
	bool ImportBlueprints;

	UPROPERTY(config, EditAnywhere, Category = "Import Settings", meta=(ClampMin=0.01, ClampMax=5))
	float LightmapResolutionMultiplier;
};