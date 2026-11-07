#include "UianaImporter.h"

#include "BPFL.h"
#include "HismActor.h"
#include "UianaDataSettings.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "Components/DecalComponent.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/RectLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/LightComponentBase.h"
#include "Engine/DecalActor.h"
#include "Engine/DirectionalLight.h"
#include "Engine/PointLight.h"
#include "Engine/RectLight.h"
#include "Engine/SkyLight.h"
#include "Engine/SpotLight.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/PostProcessVolume.h"
#include "GameFramework/WorldSettings.h"
#include "Engine/ExponentialHeightFog.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Engine/Scene.h"
#include "Engine/TextureCube.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "Engine/Blueprint.h"
#include "Editor.h"
#include "Factories/MaterialInstanceConstantFactoryNew.h"
#include "HAL/FileManager.h"
#include "Interfaces/IPluginManager.h"
#include "Kismet/GameplayStatics.h"
#include "MaterialEditingLibrary.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopedSlowTask.h"
#include "Misc/PackageName.h"
#include "Misc/MessageDialog.h"
#include "UianaConfigManager.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/SavePackage.h"

namespace
{
	static const FString GDefaultAES = TEXT("0x4BE71AF2459CF83899EC9DC2CB60E22AC4B3047E0211034BBABE9D174C069DD6");

	static EComponentMobility::Type UianaParseMobility(const FString& S)
	{
		if (S.EndsWith(TEXT("Movable")))    return EComponentMobility::Movable;
		if (S.EndsWith(TEXT("Stationary"))) return EComponentMobility::Stationary;
		return EComponentMobility::Static;
	}

	static bool UianaParseLightUnits(const FString& S, ELightUnits& Out)
	{
		if (S.EndsWith(TEXT("Candelas"))) { Out = ELightUnits::Candelas; return true; }
		if (S.EndsWith(TEXT("Lumens")))   { Out = ELightUnits::Lumens;   return true; }
		if (S.EndsWith(TEXT("EV")))       { Out = ELightUnits::EV;       return true; }
		if (S.EndsWith(TEXT("Nits")))     { Out = ELightUnits::Nits;     return true; }
		if (S.EndsWith(TEXT("Unitless"))) { Out = ELightUnits::Unitless; return true; }
		return false;
	}

	static FString UianaNormalizeObjectPath(FString P)
	{
		int32 Dot = INDEX_NONE;
		if (P.FindLastChar(TEXT('.'), Dot) && P.Mid(Dot + 1).IsNumeric())
		{
			P = P.Left(Dot);
		}
		return P;
	}

	struct FMapPaths
	{
		FString Root;
		FString Actors;
		FString Materials;
		FString MaterialsOverride;
		FString Objects;
		FString UMaps;
	};

	FString NormalizeMapName(const FString& InMapName)
	{
		FString Name = InMapName.ToLower();
		if (Name == TEXT("characterselect"))
		{
			Name = TEXT("character select");
		}
		return Name;
	}

	FMapPaths MakeMapPaths(const FUianaImportConfig& Config)
	{
		FMapPaths Paths;
		Paths.Root = FPaths::Combine(Config.MapsRoot(), Config.MapName);
		Paths.Actors = FPaths::Combine(Paths.Root, TEXT("actors"));
		Paths.Materials = FPaths::Combine(Paths.Root, TEXT("materials"));
		Paths.MaterialsOverride = FPaths::Combine(Paths.Root, TEXT("materials_ovr"));
		Paths.Objects = FPaths::Combine(Paths.Root, TEXT("objects"));
		Paths.UMaps = FPaths::Combine(Paths.Root, TEXT("umaps"));
		return Paths;
	}

	void EnsureDirectory(const FString& InPath)
	{
		IFileManager::Get().MakeDirectory(*InPath, true);
	}

	void EnsureMapDirectories(const FUianaImportConfig& Config, const FMapPaths& Paths)
	{
		EnsureDirectory(Config.ExportRoot);
		EnsureDirectory(Config.AssetsRoot());
		EnsureDirectory(Config.MapsRoot());
		EnsureDirectory(Paths.Root);
		EnsureDirectory(Paths.Actors);
		EnsureDirectory(Paths.Materials);
		EnsureDirectory(Paths.MaterialsOverride);
		EnsureDirectory(Paths.Objects);
		EnsureDirectory(Paths.UMaps);
	}

	bool RunExternalTool(const FString& ExecutablePath, const FString& Args)
	{
		if (!FPaths::FileExists(ExecutablePath))
		{
			UE_LOG(LogTemp, Error, TEXT("Uiana: missing tool: %s"), *ExecutablePath);
			return false;
		}

		UE_LOG(LogTemp, Log, TEXT("Uiana: launching tool: %s %s"), *ExecutablePath, *Args);
		FProcHandle Proc = FPlatformProcess::CreateProc(*ExecutablePath, *Args, true, false, false, nullptr, 0, nullptr, nullptr);
		if (!Proc.IsValid())
		{
			UE_LOG(LogTemp, Error, TEXT("Uiana: failed to launch tool: %s"), *ExecutablePath);
			return false;
		}

		FPlatformProcess::WaitForProc(Proc);
		int32 ReturnCode = 1;
		FPlatformProcess::GetProcReturnCode(Proc, &ReturnCode);
		FPlatformProcess::CloseProc(Proc);
		if (ReturnCode != 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("Uiana: tool returned %d: %s %s"), ReturnCode, *ExecutablePath, *Args);
		}
		return ReturnCode == 0;
	}

	bool ReadJsonArray(const FString& FilePath, TArray<TSharedPtr<FJsonValue>>& OutArray)
	{
		FString JsonText;
		if (!FFileHelper::LoadFileToString(JsonText, *FilePath))
		{
			return false;
		}

		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
		return FJsonSerializer::Deserialize(Reader, OutArray);
	}

	bool ReadJsonObject(const FString& FilePath, TSharedPtr<FJsonObject>& OutObject)
	{
		FString JsonText;
		if (!FFileHelper::LoadFileToString(JsonText, *FilePath))
		{
			return false;
		}

		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
		return FJsonSerializer::Deserialize(Reader, OutObject);
	}

	void SaveJsonArray(const FString& FilePath, const TArray<TSharedPtr<FJsonValue>>& JsonArray)
	{
		FString OutJson;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJson);
		FJsonSerializer::Serialize(JsonArray, Writer);
		FFileHelper::SaveStringToFile(OutJson, *FilePath);
	}

	void ListFiles(const FString& Dir, const FString& Extension, TArray<FString>& OutFiles)
	{
		OutFiles.Reset();
		IFileManager::Get().FindFiles(OutFiles, *FPaths::Combine(Dir, FString::Printf(TEXT("*.%s"), *Extension)), true, false);
		for (FString& Name : OutFiles)
		{
			Name = FPaths::Combine(Dir, Name);
		}
	}

	FString GetObjectPathFromRef(const TSharedPtr<FJsonObject>& Obj, const bool bIsMaterialRef)
	{
		if (!Obj.IsValid())
		{
			return FString();
		}

		if (bIsMaterialRef)
		{
			return Obj->GetStringField(TEXT("ObjectPath"));
		}

		const TSharedPtr<FJsonObject>* Props = nullptr;
		if (!Obj->TryGetObjectField(TEXT("Properties"), Props) || !Props || !(*Props).IsValid())
		{
			return FString();
		}

		const TSharedPtr<FJsonObject>* StaticMesh = nullptr;
		if (!(*Props)->TryGetObjectField(TEXT("StaticMesh"), StaticMesh) || !StaticMesh || !(*StaticMesh).IsValid())
		{
			return FString();
		}

		return (*StaticMesh)->GetStringField(TEXT("ObjectPath"));
	}

	FString ToBackslashObjectPath(FString InObjectPath)
	{
		int32 DotIndex = INDEX_NONE;
		if (InObjectPath.FindChar(TEXT('.'), DotIndex))
		{
			InObjectPath = InObjectPath.Left(DotIndex);
		}
		InObjectPath.ReplaceInline(TEXT("/"), TEXT("\\"));
		return InObjectPath;
	}

	FString ExtractCleanObjectName(const FString& InRaw)
	{
		FString Str = InRaw.TrimStartAndEnd();
		if (Str.IsEmpty())
		{
			return FString();
		}

		// 1. If wrapped in single quotes, e.g. Class'Name' or 'Name'
		int32 FirstQuote = INDEX_NONE;
		int32 LastQuote = INDEX_NONE;
		if (Str.FindChar(TEXT('\''), FirstQuote) && Str.FindLastChar(TEXT('\''), LastQuote) && LastQuote > FirstQuote)
		{
			Str = Str.Mid(FirstQuote + 1, LastQuote - FirstQuote - 1);
		}
		else
		{
			// 2. If space separated, e.g. "Class Name"
			int32 SpaceIdx = INDEX_NONE;
			if (Str.FindLastChar(TEXT(' '), SpaceIdx))
			{
				Str = Str.Mid(SpaceIdx + 1);
			}
		}

		// 3. If it looks like a path or package.export, e.g. /Game/.../MyAsset.0 or /Game/.../MyAsset.MyAsset
		int32 DotIdx = INDEX_NONE;
		if (Str.FindLastChar(TEXT('.'), DotIdx))
		{
			const FString AfterDot = Str.Mid(DotIdx + 1);
			if (AfterDot.IsNumeric())
			{
				Str = Str.Left(DotIdx);
			}
			else
			{
				Str = AfterDot;
			}
		}

		// 4. Base filename if it contains slashes (/ or \)
		Str = FPaths::GetBaseFilename(Str);

		// 5. Subobject colon, e.g. Outer:SubObject
		int32 ColonIdx = INDEX_NONE;
		if (Str.FindLastChar(TEXT(':'), ColonIdx))
		{
			Str = Str.Mid(ColonIdx + 1);
		}

		return Str;
	}

	FString GetRefName(const TSharedPtr<FJsonObject>& RefObj)
	{
		if (!RefObj.IsValid())
		{
			return FString();
		}

		FString Name;
		if (RefObj->TryGetStringField(TEXT("ObjectName"), Name) && !Name.IsEmpty())
		{
			const FString Clean = ExtractCleanObjectName(Name);
			if (!Clean.IsEmpty())
			{
				return Clean;
			}
		}

		if (RefObj->TryGetStringField(TEXT("ObjectPath"), Name) && !Name.IsEmpty())
		{
			return ExtractCleanObjectName(Name);
		}

		return FString();
	}

	FString ObjectNameFromPath(const FString& ObjectPath)
	{
		return ExtractCleanObjectName(ObjectPath);
	}

	FString ObjectNameFromObjectNameField(const FString& ObjectNameField)
	{
		return ExtractCleanObjectName(ObjectNameField);
	}

	FString PathConvert(const FString& InPath)
	{
		FString Out = InPath;
		Out.ReplaceInline(TEXT("ShooterGame\\Content"), TEXT("Game"));
		Out.ReplaceInline(TEXT("Engine\\Content"), TEXT("Engine"));
		return Out;
	}

	bool IsBlacklisted(const FString& Name)
	{
		static const TArray<FString> Blacklist = {
			TEXT("navmesh"), TEXT("_breakable"), TEXT("_collision"), TEXT("windstreaks_plane"), TEXT("sm_port_snowflakes_boundmesh"),
			TEXT("m_pitt_caustics_box"), TEXT("box_for_volumes"), TEXT("bombsitemarker_0_bombsitea_glow"),
			TEXT("bombsitemarker_0_bombsiteb_glow"), TEXT("_col"), TEXT("m_pitt_lamps_glow"), TEXT("sm_pitt_water_lid"),
			TEXT("dirtskirt"), TEXT("tech_0_rebelsupplycargotarpcollision")
		};

		const FString Lower = Name.ToLower();
		for (const FString& Blocked : Blacklist)
		{
			if (Lower.Contains(Blocked))
			{
				return true;
			}
		}
		return false;
	}

	bool ParseTransform(const TSharedPtr<FJsonObject>& Props, FTransform& OutTransform)
	{
		if (!Props.IsValid())
		{
			return false;
		}

		FVector Location = FVector::ZeroVector;
		FRotator Rotation = FRotator::ZeroRotator;
		FVector Scale = FVector(1.0, 1.0, 1.0);
		bool bHasAny = false;

		const TSharedPtr<FJsonObject>* RelLoc = nullptr;
		if (Props->TryGetObjectField(TEXT("RelativeLocation"), RelLoc) && RelLoc && (*RelLoc).IsValid())
		{
			Location.X = (*RelLoc)->GetNumberField(TEXT("X"));
			Location.Y = (*RelLoc)->GetNumberField(TEXT("Y"));
			Location.Z = (*RelLoc)->GetNumberField(TEXT("Z"));
			bHasAny = true;
		}

		const TSharedPtr<FJsonObject>* RelRot = nullptr;
		if (Props->TryGetObjectField(TEXT("RelativeRotation"), RelRot) && RelRot && (*RelRot).IsValid())
		{
			Rotation.Roll = (*RelRot)->GetNumberField(TEXT("Roll"));
			Rotation.Pitch = (*RelRot)->GetNumberField(TEXT("Pitch"));
			Rotation.Yaw = (*RelRot)->GetNumberField(TEXT("Yaw"));
			bHasAny = true;
		}

		const TSharedPtr<FJsonObject>* RelScale = nullptr;
		if (Props->TryGetObjectField(TEXT("RelativeScale3D"), RelScale) && RelScale && (*RelScale).IsValid())
		{
			Scale.X = (*RelScale)->GetNumberField(TEXT("X"));
			Scale.Y = (*RelScale)->GetNumberField(TEXT("Y"));
			Scale.Z = (*RelScale)->GetNumberField(TEXT("Z"));
			bHasAny = true;
		}

		OutTransform = FTransform(Rotation, Location, Scale);
		return bHasAny;
	}

	UMaterialInterface* LoadMaterialFromRef(const TSharedPtr<FJsonObject>& MaterialRef)
	{
		if (!MaterialRef.IsValid())
		{
			return nullptr;
		}

		const FString ObjectName = GetRefName(MaterialRef);
		if (ObjectName.IsEmpty() || ObjectName.Contains(TEXT("MaterialInstanceDynamic")))
		{
			return nullptr;
		}

		FString MatName = (ObjectName == TEXT("Stone_M2_Steps_MI1")) ? TEXT("Stone_M2_Steps_MI") : ObjectName;
		MatName = UBPFL::ResolveCleanMaterialVariation(MatName);
		if (MatName.StartsWith(TEXT("M_Sky_")))
		{
			const FString PluginSkyPath = FString::Printf(TEXT("/Uiana/Materials/%s.%s"), *MatName, *MatName);
			if (UMaterialInterface* SkyMat = Cast<UMaterialInterface>(StaticLoadObject(UMaterialInterface::StaticClass(), nullptr, *PluginSkyPath)))
			{
				return SkyMat;
			}
		}
		const FString AssetPath = FString::Printf(TEXT("/Game/ValorantContent/Materials/%s.%s"), *MatName, *MatName);
		UMaterialInterface* Mat = Cast<UMaterialInterface>(StaticLoadObject(UMaterialInterface::StaticClass(), nullptr, *AssetPath));
		if (!Mat)
		{
			const FString PluginPath = FString::Printf(TEXT("/Uiana/Materials/%s.%s"), *MatName, *MatName);
			Mat = Cast<UMaterialInterface>(StaticLoadObject(UMaterialInterface::StaticClass(), nullptr, *PluginPath));
		}
		return Mat;
	}

	void SaveUniqueList(const FString& OutPath, const TArray<FString>& InLines)
	{
		TSet<FString> Unique;
		FString OutContent;
		for (const FString& Item : InLines)
		{
			if (Item.IsEmpty() || Unique.Contains(Item))
			{
				continue;
			}
			Unique.Add(Item);
			OutContent += Item;
			OutContent += LINE_TERMINATOR;
		}
		FFileHelper::SaveStringToFile(OutContent, *OutPath);
	}

	void AppendMaterialsFromStaticMeshDefinition(const TArray<TSharedPtr<FJsonValue>>& JsonArray, TArray<FString>& OutMaterials)
	{
		for (const TSharedPtr<FJsonValue>& Value : JsonArray)
		{
			const TSharedPtr<FJsonObject> Obj = Value->AsObject();
			if (!Obj.IsValid() || Obj->GetStringField(TEXT("Type")) != TEXT("StaticMesh"))
			{
				continue;
			}

			const TSharedPtr<FJsonObject>* Props = nullptr;
			if (!Obj->TryGetObjectField(TEXT("Properties"), Props) || !(*Props).IsValid())
			{
				continue;
			}

			const TArray<TSharedPtr<FJsonValue>>* StaticMaterials = nullptr;
			if (!(*Props)->TryGetArrayField(TEXT("StaticMaterials"), StaticMaterials) || !StaticMaterials)
			{
				continue;
			}

			for (const TSharedPtr<FJsonValue>& MatValue : *StaticMaterials)
			{
				const TSharedPtr<FJsonObject> MatObject = MatValue->AsObject();
				if (!MatObject.IsValid())
				{
					continue;
				}
				const TSharedPtr<FJsonObject>* MatInterface = nullptr;
				if (!MatObject->TryGetObjectField(TEXT("MaterialInterface"), MatInterface) || !(*MatInterface).IsValid())
				{
					continue;
				}
				OutMaterials.Add(ToBackslashObjectPath((*MatInterface)->GetStringField(TEXT("ObjectPath"))));
			}
		}
	}

	void FilterUMapAndCollectAssets(
		const TArray<TSharedPtr<FJsonValue>>& InObjects,
		TArray<TSharedPtr<FJsonValue>>& OutFiltered,
		TArray<FString>& OutObjects,
		TArray<FString>& OutOverrideMaterials,
		TArray<FString>& OutActors,
		const FUianaImportConfig* Config = nullptr)
	{
		static const TSet<FString> MeshTypes = {
			TEXT("staticmesh"), TEXT("staticmeshcomponent"), TEXT("instancedstaticmeshcomponent"), TEXT("hierarchicalinstancedstaticmeshcomponent")
		};

		static const TSet<FString> GenericTypes = {
			TEXT("pointlightcomponent"), TEXT("postprocessvolume"), TEXT("culldistancevolume"), TEXT("scenecomponent"),
			TEXT("lightmasscharacterindirectdetailvolume"), TEXT("brushcomponent"), TEXT("precomputedvisibilityvolume"),
			TEXT("rectlightcomponent"), TEXT("spotlightcomponent"), TEXT("skylightcomponent"), TEXT("scenecapturecomponentcube"),
			TEXT("lightmassimportancevolume"), TEXT("billboardcomponent"), TEXT("directionallightcomponent"),
			TEXT("exponentialheightfogcomponent"), TEXT("lightmassportalcomponent"), TEXT("spherereflectioncapturecomponent")
		};

		for (const TSharedPtr<FJsonValue>& Value : InObjects)
		{
			const TSharedPtr<FJsonObject> Obj = Value->AsObject();
			if (!Obj.IsValid())
			{
				continue;
			}

			const FString Type = Obj->GetStringField(TEXT("Type"));
			const FString TypeLower = Type.ToLower();
			const bool bImportDecals = (!Config || Config->bImportDecals);
			const bool bKeep = MeshTypes.Contains(TypeLower) || GenericTypes.Contains(TypeLower) || (bImportDecals && TypeLower == TEXT("decalcomponent")) || TypeLower.EndsWith(TEXT("_c"));
			if (!bKeep)
			{
				continue;
			}

			const TSharedPtr<FJsonObject>* Props = nullptr;
			const bool bHasProps = Obj->TryGetObjectField(TEXT("Properties"), Props) && Props && (*Props).IsValid();

			if (Config && Config->bFilterMeshes && MeshTypes.Contains(TypeLower) && bHasProps)
			{
				const TSharedPtr<FJsonObject>* StaticMesh = nullptr;
				if ((*Props)->TryGetObjectField(TEXT("StaticMesh"), StaticMesh) && StaticMesh && (*StaticMesh).IsValid())
				{
					const FString MeshName = GetRefName(*StaticMesh);
					FString OuterName;
					const TSharedPtr<FJsonObject>* OuterObj = nullptr;
					if (Obj->TryGetObjectField(TEXT("Outer"), OuterObj) && OuterObj && (*OuterObj).IsValid())
					{
						OuterName = GetRefName(*OuterObj);
					}
					else
					{
						OuterName = ExtractCleanObjectName(Obj->GetStringField(TEXT("Outer")));
					}

					bool bSkipMesh = false;
					for (const FString& Prefix : Config->MeshFilterPrefixes)
					{
						if (MeshName.StartsWith(Prefix, ESearchCase::IgnoreCase) || OuterName.StartsWith(Prefix, ESearchCase::IgnoreCase))
						{
							bSkipMesh = true;
							break;
						}
					}
					if (bSkipMesh)
					{
						continue;
					}
				}
			}

			OutFiltered.Add(Value);

			if (Type.EndsWith(TEXT("_C")) && Obj->HasField(TEXT("Template")))
			{
				OutActors.Add(Obj->GetStringField(TEXT("Template")));
			}

			if (!bHasProps)
			{
				continue;
			}

			const TSharedPtr<FJsonObject>* StaticMesh = nullptr;
			if ((*Props)->TryGetObjectField(TEXT("StaticMesh"), StaticMesh) && StaticMesh && (*StaticMesh).IsValid())
			{
				OutObjects.Add(ToBackslashObjectPath((*StaticMesh)->GetStringField(TEXT("ObjectPath"))));

				const TArray<TSharedPtr<FJsonValue>>* OverrideMaterials = nullptr;
				if ((*Props)->TryGetArrayField(TEXT("OverrideMaterials"), OverrideMaterials) && OverrideMaterials)
				{
					for (const TSharedPtr<FJsonValue>& MatValue : *OverrideMaterials)
					{
						const TSharedPtr<FJsonObject> MatObj = MatValue->AsObject();
						if (!MatObj.IsValid())
						{
							continue;
						}
						OutOverrideMaterials.Add(ToBackslashObjectPath(MatObj->GetStringField(TEXT("ObjectPath"))));
					}
				}
			}

			if (bImportDecals)
			{
				const TSharedPtr<FJsonObject>* DecalMaterial = nullptr;
				if ((*Props)->TryGetObjectField(TEXT("DecalMaterial"), DecalMaterial) && DecalMaterial && (*DecalMaterial).IsValid())
				{
					OutOverrideMaterials.Add(ToBackslashObjectPath((*DecalMaterial)->GetStringField(TEXT("ObjectPath"))));
				}
			}
		}
	}

	FString ReadValorantVersion()
	{
		const FString VersionPath = TEXT("C:/ProgramData/Riot Games/Metadata/valorant.live/valorant.live.ok");
		if (!FPaths::FileExists(VersionPath))
		{
			return FString();
		}

		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *VersionPath))
		{
			return FString();
		}

		FString FirstLine;
		Text.Split(TEXT("\n"), &FirstLine, nullptr);
		TArray<FString> SlashParts;
		FirstLine.ParseIntoArray(SlashParts, TEXT("/"), true);
		if (SlashParts.Num() == 0)
		{
			return FString();
		}
		TArray<FString> DotParts;
		SlashParts.Last().ParseIntoArray(DotParts, TEXT("."), true);
		return DotParts.Num() > 0 ? DotParts[0] : FString();
	}

	bool ShouldReExport(const FUianaImportConfig& Config, const FMapPaths& Paths)
	{
		const FString MarkerPath = FPaths::Combine(Paths.Root, TEXT("exported.yo"));
		if (!FPaths::FileExists(MarkerPath))
		{
			return true;
		}

		TArray<TSharedPtr<FJsonValue>> MarkerData;
		if (!ReadJsonArray(MarkerPath, MarkerData) || MarkerData.Num() == 0)
		{
			return true;
		}

		const FString PreviousVersion = MarkerData[0]->AsString();
		const FString CurrentVersion = ReadValorantVersion();
		return PreviousVersion != CurrentVersion;
	}

	void WriteExportMarker(const FMapPaths& Paths, const FUianaImportConfig& Config)
	{
		const FString Version = ReadValorantVersion();
		const FString JsonText = FString::Printf(TEXT("[\"%s\"]"), *Version);
		FFileHelper::SaveStringToFile(JsonText, *FPaths::Combine(Paths.Root, TEXT("exported.yo")));
		FFileHelper::SaveStringToFile(JsonText, *FPaths::Combine(Config.AssetsRoot(), TEXT("exported.yo")));
	}

	bool ExtractData(const FUianaImportConfig& Config, const FString& ExportDirectory, const FString& AssetListFile)
	{
		const FString LogFilePath = FPaths::Combine(Config.ExportRoot, TEXT("cue4extractor.log"));
		const FString Args = FString::Printf(
			TEXT("--game-directory \"%s\" --aes-key %s --export-directory \"%s\" --map-name \"%s\" --file-list \"%s\" --game-umaps \"%s\" --log-file \"%s\""),
			*Config.PaksRoot,
			*Config.AESKey,
			*ExportDirectory,
			*Config.MapName,
			*AssetListFile,
			*Config.UMapsListPath(),
			*LogFilePath);
		return RunExternalTool(Config.Cue4ExtractorPath(), Args);
	}

	bool ExtractAssets(const FUianaImportConfig& Config, const FMapPaths& Paths, const FString& CustomAssetList = FString())
	{
		const FString AssetObjects = CustomAssetList.IsEmpty() ? FPaths::Combine(Paths.Root, TEXT("all_assets.txt")) : CustomAssetList;
		const FString LogFilePath = FPaths::Combine(Config.ExportRoot, TEXT("cue4extractor.log"));
		const FString Args = FString::Printf(
			TEXT("--game-directory \"%s\" --aes-key %s --export-directory \"%s\" --file-list \"%s\" --game-umaps \"%s\" --export-assets --log-file \"%s\""),
			*Config.PaksRoot,
			*Config.AESKey,
			*Config.AssetsRoot(),
			*AssetObjects,
			*Config.UMapsListPath(),
			*LogFilePath);
		return RunExternalTool(Config.Cue4ExtractorPath(), Args);
	}

	FString BuildAssetFile(const FString& OutFilePath, const TArray<FString>& Items)
	{
		SaveUniqueList(OutFilePath, Items);
		return OutFilePath;
	}

	void CollectTexturesFromMaterialFolders(const FString& MatFolderA, const FString& MatFolderB, TSet<FString>& OutTexturePaths)
	{
		TArray<FString> Files;
		ListFiles(MatFolderA, TEXT("json"), Files);
		TArray<FString> FilesB;
		ListFiles(MatFolderB, TEXT("json"), FilesB);
		Files.Append(FilesB);

		for (const FString& FilePath : Files)
		{
			TArray<TSharedPtr<FJsonValue>> JsonArray;
			if (!ReadJsonArray(FilePath, JsonArray) || JsonArray.Num() == 0) continue;
			for (const auto& Val : JsonArray)
			{
				TSharedPtr<FJsonObject> Obj = Val->AsObject();
				if (!Obj.IsValid()) continue;

				auto AddTexPath = [&](const FString& ObjPath)
				{
					if (!ObjPath.IsEmpty())
					{
						FString Normalized = ToBackslashObjectPath(ObjPath);
						Normalized = PathConvert(Normalized);
						OutTexturePaths.Add(Normalized);
					}
				};

				const TSharedPtr<FJsonObject>* Props = nullptr;
				if (Obj->TryGetObjectField(TEXT("Properties"), Props) && Props && (*Props).IsValid())
				{
					const TArray<TSharedPtr<FJsonValue>>* TexParams = nullptr;
					if ((*Props)->TryGetArrayField(TEXT("TextureParameterValues"), TexParams) && TexParams)
					{
						for (const auto& TP : *TexParams)
						{
							TSharedPtr<FJsonObject> TPObj = TP->AsObject();
							if (!TPObj.IsValid()) continue;
							const TSharedPtr<FJsonObject>* ParamInfo = nullptr;
							if (TPObj->TryGetObjectField(TEXT("ParameterInfo"), ParamInfo) && ParamInfo && (*ParamInfo).IsValid())
							{
								if (FUianaConfigManager::Get().IsParameterIgnored((*ParamInfo)->GetStringField(TEXT("Name"))))
								{
									continue;
								}
							}
							const TSharedPtr<FJsonObject>* PVal = nullptr;
							if (TPObj->TryGetObjectField(TEXT("ParameterValue"), PVal) && PVal && (*PVal).IsValid())
							{
								AddTexPath((*PVal)->GetStringField(TEXT("ObjectPath")));
							}
						}
					}
				}

				const TArray<TSharedPtr<FJsonValue>>* RootRefTextures = nullptr;
				if (Obj->TryGetArrayField(TEXT("ReferencedTextures"), RootRefTextures) && RootRefTextures)
				{
					for (const auto& RT : *RootRefTextures)
					{
						TSharedPtr<FJsonObject> RTObj = RT->AsObject();
						if (RTObj.IsValid())
						{
							AddTexPath(RTObj->GetStringField(TEXT("ObjectPath")));
						}
					}
				}

				const TSharedPtr<FJsonObject>* CachedData = nullptr;
				if (Obj->TryGetObjectField(TEXT("CachedExpressionData"), CachedData) && CachedData && (*CachedData).IsValid())
				{
					const TArray<TSharedPtr<FJsonValue>>* CachedRefTextures = nullptr;
					if ((*CachedData)->TryGetArrayField(TEXT("ReferencedTextures"), CachedRefTextures) && CachedRefTextures)
					{
						for (const auto& RT : *CachedRefTextures)
						{
							TSharedPtr<FJsonObject> RTObj = RT->AsObject();
							if (RTObj.IsValid())
							{
								AddTexPath(RTObj->GetStringField(TEXT("ObjectPath")));
							}
						}
					}
				}
			}
		}
	}

	void ImportAllTexturesFromMaterialJson(const FString& MaterialFilePath, const FUianaImportConfig& Config, TSet<FString>& InOutTextures)
	{
		TArray<TSharedPtr<FJsonValue>> JsonArray;
		if (!ReadJsonArray(MaterialFilePath, JsonArray) || JsonArray.Num() == 0)
		{
			return;
		}

		const TSharedPtr<FJsonObject> RootObj = JsonArray[0]->AsObject();
		if (!RootObj.IsValid())
		{
			return;
		}

		auto AddTexturePath = [&](const FString& ObjectPath)
		{
			if (ObjectPath.IsEmpty()) return;
			FString NoExt = ObjectPath;
			int32 DotIndex = INDEX_NONE;
			if (NoExt.FindChar(TEXT('.'), DotIndex))
			{
				NoExt = NoExt.Left(DotIndex);
			}
			const FString Relative = PathConvert(NoExt.Replace(TEXT("/"), TEXT("\\"))).Replace(TEXT("'"), TEXT("")) + Config.TextureFormat;
			const FString LocalPath = FPaths::Combine(Config.AssetsRoot(), Relative);
			if (FPaths::FileExists(LocalPath))
			{
				InOutTextures.Add(LocalPath);
			}
		};

		const TSharedPtr<FJsonObject>* Props = nullptr;
		if (RootObj->TryGetObjectField(TEXT("Properties"), Props) && (*Props).IsValid())
		{
			const TArray<TSharedPtr<FJsonValue>>* TextureParams = nullptr;
			if ((*Props)->TryGetArrayField(TEXT("TextureParameterValues"), TextureParams) && TextureParams)
			{
				for (const TSharedPtr<FJsonValue>& ParamValue : *TextureParams)
				{
					const TSharedPtr<FJsonObject> ParamObj = ParamValue->AsObject();
					if (!ParamObj.IsValid()) continue;
					const TSharedPtr<FJsonObject>* ParamInfo = nullptr;
					if (ParamObj->TryGetObjectField(TEXT("ParameterInfo"), ParamInfo) && ParamInfo && (*ParamInfo).IsValid())
					{
						if (FUianaConfigManager::Get().IsParameterIgnored((*ParamInfo)->GetStringField(TEXT("Name"))))
						{
							continue;
						}
					}
					const TSharedPtr<FJsonObject>* ParameterValueObj = nullptr;
					if (ParamObj->TryGetObjectField(TEXT("ParameterValue"), ParameterValueObj) && (*ParameterValueObj).IsValid())
					{
						AddTexturePath((*ParameterValueObj)->GetStringField(TEXT("ObjectPath")));
					}
				}
			}
		}

		auto ProcessRefTextures = [&](const TArray<TSharedPtr<FJsonValue>>* RefTextures)
		{
			if (!RefTextures) return;
			for (const auto& RT : *RefTextures)
			{
				const TSharedPtr<FJsonObject> RTObj = RT->AsObject();
				if (!RTObj.IsValid()) continue;
				AddTexturePath(RTObj->GetStringField(TEXT("ObjectPath")));
			}
		};

		const TArray<TSharedPtr<FJsonValue>>* RootRefTextures = nullptr;
		if (RootObj->TryGetArrayField(TEXT("ReferencedTextures"), RootRefTextures))
		{
			ProcessRefTextures(RootRefTextures);
		}
		const TSharedPtr<FJsonObject>* CachedData = nullptr;
		if (RootObj->TryGetObjectField(TEXT("CachedExpressionData"), CachedData) && CachedData && (*CachedData).IsValid())
		{
			const TArray<TSharedPtr<FJsonValue>>* CachedRefTextures = nullptr;
			if ((*CachedData)->TryGetArrayField(TEXT("ReferencedTextures"), CachedRefTextures))
			{
				ProcessRefTextures(CachedRefTextures);
			}
		}
	}

	void ImportMaterialsFromJsonFolder(const FString& FolderPath)
	{
		TArray<FString> Files;
		ListFiles(FolderPath, TEXT("json"), Files);

		IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
		int32 ProcessedCount = 0;

		for (const FString& FilePath : Files)
		{
			TArray<TSharedPtr<FJsonValue>> JsonArray;
			if (!ReadJsonArray(FilePath, JsonArray) || JsonArray.Num() == 0)
			{
				continue;
			}

			const TSharedPtr<FJsonObject> RootObj = JsonArray[0]->AsObject();
			if (!RootObj.IsValid())
			{
				continue;
			}

			const FString MaterialName = RootObj->GetStringField(TEXT("Name"));
			const FString MaterialAssetPath = FString::Printf(TEXT("/Game/ValorantContent/Materials/%s.%s"), *MaterialName, *MaterialName);
			UMaterialInstanceConstant* MIC = Cast<UMaterialInstanceConstant>(StaticLoadObject(UMaterialInstanceConstant::StaticClass(), nullptr, *MaterialAssetPath));
			if (!MIC)
			{
				MIC = Cast<UMaterialInstanceConstant>(AssetTools.CreateAsset(
					MaterialName,
					TEXT("/Game/ValorantContent/Materials"),
					UMaterialInstanceConstant::StaticClass(),
					NewObject<UMaterialInstanceConstantFactoryNew>()));
			}
			if (!MIC)
			{
				continue;
			}

			const TSharedPtr<FJsonObject>* Props = nullptr;
			if (!RootObj->TryGetObjectField(TEXT("Properties"), Props) || !(*Props).IsValid())
			{
				continue;
			}

			FString IncomingParentName;
			const TSharedPtr<FJsonObject>* ParentObj = nullptr;
			if ((*Props)->TryGetObjectField(TEXT("Parent"), ParentObj) && ParentObj && (*ParentObj).IsValid())
			{
				IncomingParentName = GetRefName(*ParentObj);
			}

			const FString PackagePath = RootObj->GetStringField(TEXT("Package"));
			const FString ResolvedParent = FUianaConfigManager::Get().ResolveParentMaterial(MaterialName, IncomingParentName, PackagePath);

			UMaterialInterface* ParentMat = nullptr;
			if (ResolvedParent.StartsWith(TEXT("/")))
			{
				ParentMat = Cast<UMaterialInterface>(StaticLoadObject(UMaterialInterface::StaticClass(), nullptr, *ResolvedParent));
			}
			if (!ParentMat && !ResolvedParent.IsEmpty() && ResolvedParent != TEXT("BaseEnv_MAT_V4"))
			{
				const FString ValoMatPath = FString::Printf(TEXT("/Game/ValorantContent/Materials/%s.%s"), *ResolvedParent, *ResolvedParent);
				ParentMat = Cast<UMaterialInterface>(StaticLoadObject(UMaterialInterface::StaticClass(), nullptr, *ValoMatPath));
			}
			if (!ParentMat && !ResolvedParent.IsEmpty())
			{
				const FString PluginMatPath = FString::Printf(TEXT("/Uiana/Materials/%s.%s"), *ResolvedParent, *ResolvedParent);
				ParentMat = Cast<UMaterialInterface>(StaticLoadObject(UMaterialInterface::StaticClass(), nullptr, *PluginMatPath));
			}
			if (!ParentMat)
			{
				ParentMat = Cast<UMaterialInterface>(StaticLoadObject(UMaterialInterface::StaticClass(), nullptr, TEXT("/Uiana/Materials/BaseEnv_MAT_V4.BaseEnv_MAT_V4")));
			}
			if (ParentMat)
			{
				MIC->SetParentEditorOnly(ParentMat);
			}

			const TArray<TSharedPtr<FJsonValue>>* ScalarParams = nullptr;
			if ((*Props)->TryGetArrayField(TEXT("ScalarParameterValues"), ScalarParams) && ScalarParams)
			{
				for (const TSharedPtr<FJsonValue>& Entry : *ScalarParams)
				{
					const TSharedPtr<FJsonObject> Obj = Entry->AsObject();
					if (!Obj.IsValid())
					{
						continue;
					}
					const TSharedPtr<FJsonObject>* ParamInfo = nullptr;
					if (!Obj->TryGetObjectField(TEXT("ParameterInfo"), ParamInfo) || !(*ParamInfo).IsValid())
					{
						continue;
					}
					const FString RawParamName = (*ParamInfo)->GetStringField(TEXT("Name")).ToLower();
					if (FUianaConfigManager::Get().IsParameterIgnored(RawParamName))
					{
						continue;
					}
					const FName ParamName(*RawParamName);
					UMaterialEditingLibrary::SetMaterialInstanceScalarParameterValue(MIC, ParamName, Obj->GetNumberField(TEXT("ParameterValue")));
				}
			}

			const TArray<TSharedPtr<FJsonValue>>* VectorParams = nullptr;
			if ((*Props)->TryGetArrayField(TEXT("VectorParameterValues"), VectorParams) && VectorParams)
			{
				for (const TSharedPtr<FJsonValue>& Entry : *VectorParams)
				{
					const TSharedPtr<FJsonObject> Obj = Entry->AsObject();
					if (!Obj.IsValid())
					{
						continue;
					}
					const TSharedPtr<FJsonObject>* ParamInfo = nullptr;
					const TSharedPtr<FJsonObject>* ParamValue = nullptr;
					if (!Obj->TryGetObjectField(TEXT("ParameterInfo"), ParamInfo) || !(*ParamInfo).IsValid() || !Obj->TryGetObjectField(TEXT("ParameterValue"), ParamValue) || !(*ParamValue).IsValid())
					{
						continue;
					}
					const FString RawParamName = (*ParamInfo)->GetStringField(TEXT("Name")).ToLower();
					if (FUianaConfigManager::Get().IsParameterIgnored(RawParamName))
					{
						continue;
					}
					const FName ParamName(*RawParamName);
					FLinearColor Color(
						(*ParamValue)->GetNumberField(TEXT("R")),
						(*ParamValue)->GetNumberField(TEXT("G")),
						(*ParamValue)->GetNumberField(TEXT("B")),
						(*ParamValue)->GetNumberField(TEXT("A")));

					// Clamp non-emissive diffuse/tint multipliers to [0.0, 1.0] if configured
					if (FUianaConfigManager::Get().ShouldClampVectorParameter(RawParamName))
					{
						Color.R = FMath::Clamp(Color.R, 0.0f, 1.0f);
						Color.G = FMath::Clamp(Color.G, 0.0f, 1.0f);
						Color.B = FMath::Clamp(Color.B, 0.0f, 1.0f);
					}

					UMaterialEditingLibrary::SetMaterialInstanceVectorParameterValue(MIC, ParamName, Color);

					TArray<FName> VectorSecondaryRoutes;
					FUianaConfigManager::Get().QuerySecondaryRoutes(IncomingParentName, RawParamName, TEXT("Vector"), VectorSecondaryRoutes);
					for (const FName& RouteParam : VectorSecondaryRoutes)
					{
						UMaterialEditingLibrary::SetMaterialInstanceVectorParameterValue(MIC, RouteParam, Color);
					}
				}
			}

			const TArray<TSharedPtr<FJsonValue>>* TextureParams = nullptr;
			int32 BoundTextureCount = 0;
			if ((*Props)->TryGetArrayField(TEXT("TextureParameterValues"), TextureParams) && TextureParams)
			{
				for (const TSharedPtr<FJsonValue>& Entry : *TextureParams)
				{
					const TSharedPtr<FJsonObject> Obj = Entry->AsObject();
					if (!Obj.IsValid())
					{
						continue;
					}
					const TSharedPtr<FJsonObject>* ParamInfo = nullptr;
					const TSharedPtr<FJsonObject>* ParamValue = nullptr;
					if (!Obj->TryGetObjectField(TEXT("ParameterInfo"), ParamInfo) || !(*ParamInfo).IsValid() || !Obj->TryGetObjectField(TEXT("ParameterValue"), ParamValue) || !(*ParamValue).IsValid())
					{
						continue;
					}

					const FString RawParamName = (*ParamInfo)->GetStringField(TEXT("Name")).ToLower();
					if (FUianaConfigManager::Get().IsParameterIgnored(RawParamName))
					{
						continue;
					}

					const FString TextureName = GetRefName(*ParamValue);
					const FString TexturePath = FString::Printf(TEXT("/Game/ValorantContent/Textures/%s.%s"), *TextureName, *TextureName);
					if (UTexture* Texture = Cast<UTexture>(StaticLoadObject(UTexture::StaticClass(), nullptr, *TexturePath)))
					{
						BoundTextureCount++;
						const FName ParamName(*RawParamName);
						UMaterialEditingLibrary::SetMaterialInstanceTextureParameterValue(MIC, ParamName, Texture);

						// Map common Valorant aliases to standard Uiana master material parameter names
						const FName CanonicalAlias = FUianaConfigManager::Get().ResolveParameterAlias(RawParamName, NAME_None);
						if (CanonicalAlias != NAME_None)
						{
							UMaterialEditingLibrary::SetMaterialInstanceTextureParameterValue(MIC, CanonicalAlias, Texture);
						}

						TArray<FName> TexSecondaryRoutes;
						FUianaConfigManager::Get().QuerySecondaryRoutes(IncomingParentName, RawParamName, TEXT("Texture"), TexSecondaryRoutes);
						for (const FName& RouteParam : TexSecondaryRoutes)
						{
							UMaterialEditingLibrary::SetMaterialInstanceTextureParameterValue(MIC, RouteParam, Texture);
						}
					}
				}
			}

			// Fallback: If no texture parameters were bound (e.g. Material assets like skyboxes or roofs), parse ReferencedTextures
			if (FUianaConfigManager::Get().GetRules().Flags.bEnableReferencedTextureFallback && BoundTextureCount == 0)
			{
				const TArray<TSharedPtr<FJsonValue>>* RefTextures = nullptr;
				if (!RootObj->TryGetArrayField(TEXT("ReferencedTextures"), RefTextures) || !RefTextures || RefTextures->Num() == 0)
				{
					const TSharedPtr<FJsonObject>* CachedData = nullptr;
					if (RootObj->TryGetObjectField(TEXT("CachedExpressionData"), CachedData) && CachedData && (*CachedData).IsValid())
					{
						(*CachedData)->TryGetArrayField(TEXT("ReferencedTextures"), RefTextures);
					}
				}

				if (RefTextures && RefTextures->Num() > 0)
				{
					for (const TSharedPtr<FJsonValue>& Entry : *RefTextures)
					{
						const TSharedPtr<FJsonObject> Obj = Entry->AsObject();
						if (!Obj.IsValid()) continue;
						const FString TextureName = GetRefName(Obj);
						if (TextureName.IsEmpty()) continue;
						const FString TexturePath = FString::Printf(TEXT("/Game/ValorantContent/Textures/%s.%s"), *TextureName, *TextureName);
						if (UTexture* Texture = Cast<UTexture>(StaticLoadObject(UTexture::StaticClass(), nullptr, *TexturePath)))
						{
							const FString LowerName = TextureName.ToLower();
							if (LowerName.Contains(TEXT("_nm")) || LowerName.Contains(TEXT("_normal")) || LowerName.EndsWith(TEXT("_n")))
							{
								UMaterialEditingLibrary::SetMaterialInstanceTextureParameterValue(MIC, FName(TEXT("normal")), Texture);
							}
							else if (LowerName.Contains(TEXT("_mra")) || LowerName.Contains(TEXT("_masks")) || LowerName.EndsWith(TEXT("_m")))
							{
								UMaterialEditingLibrary::SetMaterialInstanceTextureParameterValue(MIC, FName(TEXT("mra")), Texture);
							}
							else
							{
								// Diffuse / albedo / skybox default
								UMaterialEditingLibrary::SetMaterialInstanceTextureParameterValue(MIC, FName(TEXT("diffuse")), Texture);
								TArray<FName> FallbackRoutes;
								FUianaConfigManager::Get().QuerySecondaryRoutes(IncomingParentName, TEXT("diffuse"), TEXT("Texture"), FallbackRoutes);
								for (const FName& RouteParam : FallbackRoutes)
								{
									UMaterialEditingLibrary::SetMaterialInstanceTextureParameterValue(MIC, RouteParam, Texture);
								}
							}
						}
					}
				}
			}

			const TSharedPtr<FJsonObject>* StaticParamsRuntime = nullptr;
			if ((*Props)->TryGetObjectField(TEXT("StaticParametersRuntime"), StaticParamsRuntime) && StaticParamsRuntime && (*StaticParamsRuntime).IsValid())
			{
				const TArray<TSharedPtr<FJsonValue>>* SwitchParams = nullptr;
				if ((*StaticParamsRuntime)->TryGetArrayField(TEXT("StaticSwitchParameters"), SwitchParams) && SwitchParams)
				{
					for (const TSharedPtr<FJsonValue>& Entry : *SwitchParams)
					{
						const TSharedPtr<FJsonObject> SwitchObj = Entry->AsObject();
						if (!SwitchObj.IsValid())
						{
							continue;
						}

						const TSharedPtr<FJsonObject>* ParamInfo = nullptr;
						if (!SwitchObj->TryGetObjectField(TEXT("ParameterInfo"), ParamInfo) || !(*ParamInfo).IsValid())
						{
							continue;
						}

						bool bValue = false;
						if (SwitchObj->TryGetBoolField(TEXT("Value"), bValue))
						{
							const FName SwitchParamName(*((*ParamInfo)->GetStringField(TEXT("Name"))));
							UMaterialEditingLibrary::SetMaterialInstanceStaticSwitchParameterValue(MIC, SwitchParamName, bValue, EMaterialParameterAssociation::GlobalParameter, false);
						}
					}
				}
			}

			UMaterialEditingLibrary::UpdateMaterialInstance(MIC);

			UPackage* Package = MIC->GetOutermost();
			Package->FullyLoad();
			FSavePackageArgs SaveArgs;
			const FString Filename = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
			UPackage::SavePackage(Package, MIC, *Filename, SaveArgs);

			if (++ProcessedCount % 40 == 0)
			{
				if (GEngine)
				{
					GEngine->TrimMemory();
				}
				CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
			}
		}
	}

	bool IsSuperGridMesh(const FString& MeshName)
	{
		return MeshName.StartsWith(TEXT("SuperGrid_"), ESearchCase::IgnoreCase);
	}

	void ImportMapActors(const FString& UMapJsonPath, const FUianaImportConfig& Config)
	{
		UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
		if (!World)
		{
			return;
		}

		if (!Config.bImportDecals)
		{
			TArray<AActor*> ExistingDecals;
			UGameplayStatics::GetAllActorsOfClass(World, ADecalActor::StaticClass(), ExistingDecals);
			for (AActor* DecalActor : ExistingDecals)
			{
				if (DecalActor)
				{
					World->EditorDestroyActor(DecalActor, true);
				}
			}
		}

		TArray<TSharedPtr<FJsonValue>> JsonArray;
		if (!ReadJsonArray(UMapJsonPath, JsonArray))
		{
			return;
		}

		TArray<FTransform> ResolvedTransforms;
		TArray<bool> ResolvedFlags;
		ResolvedTransforms.SetNum(JsonArray.Num());
		ResolvedFlags.Init(false, JsonArray.Num());

		auto ResolveWorldTransform = [&](auto& Self, int32 Index, int32 Depth) -> FTransform
		{
			if (Depth > 16 || !JsonArray.IsValidIndex(Index))
			{
				return FTransform::Identity;
			}
			if (ResolvedFlags[Index])
			{
				return ResolvedTransforms[Index];
			}

			FTransform LocalTransform = FTransform::Identity;
			const TSharedPtr<FJsonObject> ItemObj = JsonArray[Index]->AsObject();
			if (ItemObj.IsValid())
			{
				const TSharedPtr<FJsonObject>* ItemProps = nullptr;
				if (ItemObj->TryGetObjectField(TEXT("Properties"), ItemProps) && ItemProps && (*ItemProps).IsValid())
				{
					ParseTransform(*ItemProps, LocalTransform);

					const TSharedPtr<FJsonObject>* AttachParentObj = nullptr;
					if ((*ItemProps)->TryGetObjectField(TEXT("AttachParent"), AttachParentObj) && AttachParentObj && (*AttachParentObj).IsValid())
					{
						FString ObjectPath;
						if ((*AttachParentObj)->TryGetStringField(TEXT("ObjectPath"), ObjectPath))
						{
							int32 DotIndex = INDEX_NONE;
							if (ObjectPath.FindLastChar(TEXT('.'), DotIndex) && DotIndex != INDEX_NONE)
							{
								const FString IndexStr = ObjectPath.Mid(DotIndex + 1);
								if (IndexStr.IsNumeric())
								{
									const int32 ParentIdx = FCString::Atoi(*IndexStr);
									if (ParentIdx != Index && JsonArray.IsValidIndex(ParentIdx))
									{
										const FTransform ParentWorld = Self(Self, ParentIdx, Depth + 1);
										LocalTransform = LocalTransform * ParentWorld;
									}
								}
							}
						}
					}
				}
			}

			ResolvedTransforms[Index] = LocalTransform;
			ResolvedFlags[Index] = true;
			return LocalTransform;
		};

		for (int32 Idx = 0; Idx < JsonArray.Num(); ++Idx)
		{
			ResolveWorldTransform(ResolveWorldTransform, Idx, 0);
		}

		for (int32 ItemIdx = 0; ItemIdx < JsonArray.Num(); ++ItemIdx)
		{
			const TSharedPtr<FJsonValue>& Value = JsonArray[ItemIdx];
			const TSharedPtr<FJsonObject> Obj = Value->AsObject();
			if (!Obj.IsValid())
			{
				continue;
			}

			const FString Type = Obj->GetStringField(TEXT("Type"));
			const TSharedPtr<FJsonObject>* Props = nullptr;
			if (!Obj->TryGetObjectField(TEXT("Properties"), Props) || !(*Props).IsValid())
			{
				continue;
			}

			FString OuterName;
			const TSharedPtr<FJsonObject>* OuterObj = nullptr;
			if (Obj->TryGetObjectField(TEXT("Outer"), OuterObj) && OuterObj && (*OuterObj).IsValid())
			{
				OuterName = GetRefName(*OuterObj);
			}
			else
			{
				OuterName = ExtractCleanObjectName(Obj->GetStringField(TEXT("Outer")));
			}
			if (OuterName.IsEmpty())
			{
				OuterName = Obj->GetStringField(TEXT("Name"));
			}

			const FTransform Transform = ResolvedTransforms[ItemIdx];

			if (Config.bImportMeshes && (Type == TEXT("StaticMeshComponent") || Type == TEXT("InstancedStaticMeshComponent") || Type == TEXT("HierarchicalInstancedStaticMeshComponent")))
			{
				const TSharedPtr<FJsonObject>* StaticMesh = nullptr;
				if (!(*Props)->TryGetObjectField(TEXT("StaticMesh"), StaticMesh) || !(*StaticMesh).IsValid())
				{
					continue;
				}

				const FString MeshName = GetRefName(*StaticMesh);

				if (Config.bFilterMeshes)
				{
					bool bSkipMesh = false;
					for (const FString& Prefix : Config.MeshFilterPrefixes)
					{
						if (MeshName.StartsWith(Prefix, ESearchCase::IgnoreCase) || OuterName.StartsWith(Prefix, ESearchCase::IgnoreCase))
						{
							bSkipMesh = true;
							break;
						}
					}
					if (bSkipMesh)
					{
						continue;
					}
				}

				const FString MeshAssetPath = FString::Printf(TEXT("/Game/ValorantContent/Meshes/%s.%s"), *MeshName, *MeshName);
				UStaticMesh* LoadedMesh = Cast<UStaticMesh>(StaticLoadObject(UStaticMesh::StaticClass(), nullptr, *MeshAssetPath));
				if (!LoadedMesh)
				{
					continue;
				}

				bool bIsInvisible = false;
				FString InvisibleFolder = TEXT("Collision");

				if (Config.bMakeMeshesInvisible)
				{
					for (const FString& Prefix : Config.InvisibleMeshPrefixes)
					{
						if (Prefix.IsEmpty())
						{
							continue;
						}
						if (OuterName.StartsWith(Prefix, ESearchCase::IgnoreCase) || MeshName.StartsWith(Prefix, ESearchCase::IgnoreCase))
						{
							bIsInvisible = true;
							if (Prefix.StartsWith(TEXT("BP_"), ESearchCase::IgnoreCase) || MeshName.StartsWith(TEXT("BP_"), ESearchCase::IgnoreCase))
							{
								InvisibleFolder = TEXT("Collision/Blueprints");
							}
							else if (Prefix.StartsWith(TEXT("SuperGrid"), ESearchCase::IgnoreCase) || MeshName.StartsWith(TEXT("SuperGrid"), ESearchCase::IgnoreCase))
							{
								InvisibleFolder = TEXT("Collision/SuperGrid");
							}
							else
							{
								InvisibleFolder = TEXT("Collision/Boxes");
							}
							break;
						}
					}
				}
				auto ApplyActorMaterials = [&](UMeshComponent* Comp)
				{
					if (!Config.bImportMaterials || !Comp || !LoadedMesh)
					{
						return;
					}

					const TArray<TSharedPtr<FJsonValue>>* OverrideMaterials = nullptr;
					if ((*Props)->TryGetArrayField(TEXT("OverrideMaterials"), OverrideMaterials) && OverrideMaterials)
					{
						TMap<FString, TArray<int32>> MatNameToSlots;
						for (int32 Idx = 0; Idx < OverrideMaterials->Num(); ++Idx)
						{
							const TSharedPtr<FJsonValue>& MatValue = (*OverrideMaterials)[Idx];
							if (MatValue.IsValid() && MatValue->Type == EJson::Object)
							{
								FString MatName = MatValue->AsObject()->GetStringField(TEXT("ObjectName"));
								int32 SingleQuote = INDEX_NONE;
								if (MatName.FindChar(TEXT('\''), SingleQuote))
								{
									MatName = MatName.Mid(SingleQuote + 1);
									if (MatName.FindLastChar(TEXT('\''), SingleQuote))
									{
										MatName = MatName.Left(SingleQuote);
									}
								}
								if (!MatName.IsEmpty())
								{
									MatNameToSlots.FindOrAdd(MatName).Add(Idx);
								}
							}
						}

						for (int32 MatIdx = 0; MatIdx < OverrideMaterials->Num(); ++MatIdx)
						{
							const TSharedPtr<FJsonValue>& MatValue = (*OverrideMaterials)[MatIdx];
							if (!MatValue.IsValid() || MatValue->Type != EJson::Object)
							{
								continue;
							}

							FString RawMatName = MatValue->AsObject()->GetStringField(TEXT("ObjectName"));
							int32 SingleQuote = INDEX_NONE;
							if (RawMatName.FindChar(TEXT('\''), SingleQuote))
							{
								RawMatName = RawMatName.Mid(SingleQuote + 1);
								if (RawMatName.FindLastChar(TEXT('\''), SingleQuote))
								{
									RawMatName = RawMatName.Left(SingleQuote);
								}
							}

							FString ResolvedName = RawMatName;
							const TArray<int32>* SlotsWithSameMat = MatNameToSlots.Find(RawMatName);
							const bool bIsDuplicatedOnActor = SlotsWithSameMat && SlotsWithSameMat->Num() > 1;

							if (bIsDuplicatedOnActor && (*SlotsWithSameMat)[0] == MatIdx)
							{
								if (RawMatName.Contains(TEXT("HerringBone")) && RawMatName.Contains(TEXT("Asphalt")))
								{
									ResolvedName = TEXT("Brick_M6_HerringBoneBroken_MI");
								}
								else
								{
									ResolvedName = UBPFL::ResolveCleanMaterialVariation(RawMatName);
								}
							}
							else if (bIsDuplicatedOnActor)
							{
								ResolvedName = RawMatName;
							}
							else
							{
								ResolvedName = UBPFL::ResolveCleanMaterialVariation(RawMatName);
							}

							UMaterialInterface* Mat = nullptr;
							if (!ResolvedName.IsEmpty() && ResolvedName != TEXT("WorldGridMaterial") && ResolvedName != TEXT("DefaultMaterial"))
							{
								FString MatPath = FString::Printf(TEXT("/Game/ValorantContent/Materials/%s.%s"), *ResolvedName, *ResolvedName);
								Mat = Cast<UMaterialInterface>(StaticLoadObject(UMaterialInterface::StaticClass(), nullptr, *MatPath));
								if (!Mat)
								{
									MatPath = FString::Printf(TEXT("/Uiana/Materials/%s.%s"), *ResolvedName, *ResolvedName);
									Mat = Cast<UMaterialInterface>(StaticLoadObject(UMaterialInterface::StaticClass(), nullptr, *MatPath));
								}
							}

							if (!Mat)
							{
								Mat = LoadMaterialFromRef(MatValue->AsObject());
							}

							if (Mat)
							{
								Comp->SetMaterial(MatIdx, Mat);
							}
						}
					}

					const TArray<FStaticMaterial>& StaticMats = LoadedMesh->GetStaticMaterials();
					for (int32 SlotIdx = 0; SlotIdx < StaticMats.Num(); ++SlotIdx)
					{
						UMaterialInterface* CurMat = Comp->GetMaterial(SlotIdx);
						const bool bIsPlaceholder = !CurMat || CurMat->GetName() == TEXT("WorldGridMaterial") || CurMat->GetName() == TEXT("DefaultMaterial");
						if (bIsPlaceholder)
						{
							UMaterialInterface* MeshMat = LoadedMesh->GetMaterial(SlotIdx);
							if (MeshMat && MeshMat->GetName() != TEXT("WorldGridMaterial") && MeshMat->GetName() != TEXT("DefaultMaterial"))
							{
								Comp->SetMaterial(SlotIdx, MeshMat);
							}
							else
							{
								const FString SlotName = UBPFL::ResolveCleanMaterialVariation(StaticMats[SlotIdx].MaterialSlotName.ToString(), LoadedMesh ? LoadedMesh->GetName() : FString(), SlotIdx);
								if (!SlotName.IsEmpty() && SlotName != TEXT("WorldGridMaterial") && SlotName != TEXT("DefaultMaterial") && !SlotName.StartsWith(TEXT("MaterialSlot_")))
								{
									FString MatPath = FString::Printf(TEXT("/Game/ValorantContent/Materials/%s.%s"), *SlotName, *SlotName);
									UMaterialInterface* FallbackMat = Cast<UMaterialInterface>(StaticLoadObject(UMaterialInterface::StaticClass(), nullptr, *MatPath));
									if (!FallbackMat)
									{
										MatPath = FString::Printf(TEXT("/Uiana/Materials/%s.%s"), *SlotName, *SlotName);
										FallbackMat = Cast<UMaterialInterface>(StaticLoadObject(UMaterialInterface::StaticClass(), nullptr, *MatPath));
									}
									if (FallbackMat)
									{
										Comp->SetMaterial(SlotIdx, FallbackMat);
									}
								}
							}
						}
					}

					// Optional HISM mixed opaque and translucent guard (controlled via MaterialRules.json Flags.bEnableHismMixedSlotGuard)
					if (FUianaConfigManager::Get().GetRules().Flags.bEnableHismMixedSlotGuard && Comp->IsA<UHierarchicalInstancedStaticMeshComponent>())
					{
						bool bHasOpaque = false;
						for (int32 SlotIdx = 0; SlotIdx < Comp->GetNumMaterials(); ++SlotIdx)
						{
							UMaterialInterface* M = Comp->GetMaterial(SlotIdx);
							if (M && (M->GetBlendMode() == BLEND_Opaque || M->GetBlendMode() == BLEND_Masked))
							{
								bHasOpaque = true;
								break;
							}
						}
						if (bHasOpaque)
						{
							for (int32 SlotIdx = 0; SlotIdx < Comp->GetNumMaterials(); ++SlotIdx)
							{
								UMaterialInterface* M = Comp->GetMaterial(SlotIdx);
								if (M && M->GetBlendMode() != BLEND_Opaque && M->GetBlendMode() != BLEND_Masked)
								{
									UMaterialInterface* SafeMat = Cast<UMaterialInterface>(StaticLoadObject(UMaterialInterface::StaticClass(), nullptr, TEXT("/Uiana/Materials/BaseEnv_MAT_V4.BaseEnv_MAT_V4")));
									if (SafeMat)
									{
										Comp->SetMaterial(SlotIdx, SafeMat);
									}
								}
							}
						}
					}
				};

				if (Type.Contains(TEXT("Instanced")))
				{
					AHismActor* Actor = World->SpawnActor<AHismActor>(AHismActor::StaticClass(), Transform);
					if (!Actor || !Actor->HismComponent)
					{
						continue;
					}
					Actor->SetActorLabel(OuterName);
					if (bIsInvisible)
					{
						Actor->SetFolderPath(*InvisibleFolder);
						Actor->SetActorHiddenInGame(true);
						Actor->SetIsTemporarilyHiddenInEditor(true);
						Actor->HismComponent->SetVisibility(false, true);
						Actor->HismComponent->SetHiddenInGame(true);
						Actor->HismComponent->SetCastShadow(false);
						Actor->HismComponent->bCastHiddenShadow = false;
						Actor->HismComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
						Actor->HismComponent->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
					}
					else
					{
						Actor->SetFolderPath(TEXT("Meshes/Instanced"));
					}
					Actor->HismComponent->SetStaticMesh(LoadedMesh);
					Actor->HismComponent->SetMobility(EComponentMobility::Static);

					if (LoadedMesh)
					{
						int32 NumUVs = 0;
						if (LoadedMesh->GetRenderData() && LoadedMesh->GetRenderData()->LODResources.IsValidIndex(0))
						{
							NumUVs = LoadedMesh->GetRenderData()->LODResources[0].VertexBuffers.StaticMeshVertexBuffer.GetNumTexCoords();
						}
						if (NumUVs <= 0)
						{
							NumUVs = LoadedMesh->GetNumUVChannels(0);
						}

						if (NumUVs > 0)
						{
							if (LoadedMesh->GetLightMapCoordinateIndex() >= NumUVs)
							{
								LoadedMesh->SetLightMapCoordinateIndex((NumUVs > 1) ? 1 : 0);
								LoadedMesh->MarkPackageDirty();
							}
							if (LoadedMesh->GetLightMapResolution() <= 0)
							{
								LoadedMesh->SetLightMapResolution(64);
								LoadedMesh->MarkPackageDirty();
							}
						}
						else
						{
							Actor->HismComponent->SetMobility(EComponentMobility::Movable);
						}
					}

					int32 OverriddenHismRes = 0;
					if ((*Props)->TryGetNumberField(TEXT("OverriddenLightMapRes"), OverriddenHismRes) && OverriddenHismRes > 0)
					{
						int32 ScaledRes = FMath::RoundToInt(OverriddenHismRes * Config.LightmapResolutionMultiplier);
						if (ScaledRes > 0 && Actor->HismComponent->HasLightmapTextureCoordinates())
						{
							Actor->HismComponent->OverriddenLightMapRes = ScaledRes;
							Actor->HismComponent->bOverrideLightMapRes = true;
						}
					}

					const TArray<TSharedPtr<FJsonValue>>* Instances = nullptr;
					if (Obj->TryGetArrayField(TEXT("PerInstanceSMData"), Instances) && Instances)
					{
						for (const TSharedPtr<FJsonValue>& InstanceVal : *Instances)
						{
							const TSharedPtr<FJsonObject> InstanceObj = InstanceVal->AsObject();
							const TSharedPtr<FJsonObject>* TransformData = nullptr;
							if (!InstanceObj.IsValid() || !InstanceObj->TryGetObjectField(TEXT("TransformData"), TransformData) || !(*TransformData).IsValid())
							{
								continue;
							}

							const TSharedPtr<FJsonObject>* Translation = nullptr;
							const TSharedPtr<FJsonObject>* Scale3D = nullptr;
							const TSharedPtr<FJsonObject>* RotationQuat = nullptr;
							if (!(*TransformData)->TryGetObjectField(TEXT("Translation"), Translation) ||
								!(*TransformData)->TryGetObjectField(TEXT("Scale3D"), Scale3D))
							{
								continue;
							}
							if (!(*TransformData)->TryGetObjectField(TEXT("Rotation"), RotationQuat) || !RotationQuat || !(*RotationQuat).IsValid())
							{
								(*TransformData)->TryGetObjectField(TEXT("RotationQuat"), RotationQuat);
							}
							if (!RotationQuat || !(*RotationQuat).IsValid())
							{
								continue;
							}

							const FVector Loc((*Translation)->GetNumberField(TEXT("X")), (*Translation)->GetNumberField(TEXT("Y")), (*Translation)->GetNumberField(TEXT("Z")));
							const FVector Scale((*Scale3D)->GetNumberField(TEXT("X")), (*Scale3D)->GetNumberField(TEXT("Y")), (*Scale3D)->GetNumberField(TEXT("Z")));
							const FQuat Quat(
								(*RotationQuat)->GetNumberField(TEXT("X")),
								(*RotationQuat)->GetNumberField(TEXT("Y")),
								(*RotationQuat)->GetNumberField(TEXT("Z")),
								(*RotationQuat)->GetNumberField(TEXT("W")));

							Actor->HismComponent->AddInstance(FTransform(Quat, Loc, Scale));
						}
					}

					ApplyActorMaterials(Actor->HismComponent);
				}
				else
				{
					AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), Transform);
					if (!Actor || !Actor->GetStaticMeshComponent())
					{
						continue;
					}
					Actor->SetActorLabel(OuterName);
					UStaticMeshComponent* SMC = Actor->GetStaticMeshComponent();
					if (bIsInvisible)
					{
						Actor->SetFolderPath(*InvisibleFolder);
						Actor->SetActorHiddenInGame(true);
						Actor->SetIsTemporarilyHiddenInEditor(true);
						SMC->SetVisibility(false, true);
						SMC->SetHiddenInGame(true);
						SMC->SetCastShadow(false);
						SMC->bCastHiddenShadow = false;
						SMC->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
						SMC->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
					}
					else
					{
						Actor->SetFolderPath(UMapJsonPath.Contains(TEXT("_VFX")) ? TEXT("Meshes/VFX") : TEXT("Meshes/Static"));
					}
					SMC->SetStaticMesh(LoadedMesh);
					SMC->SetMobility(EComponentMobility::Static);

					if (LoadedMesh)
					{
						int32 NumUVs = 0;
						if (LoadedMesh->GetRenderData() && LoadedMesh->GetRenderData()->LODResources.IsValidIndex(0))
						{
							NumUVs = LoadedMesh->GetRenderData()->LODResources[0].VertexBuffers.StaticMeshVertexBuffer.GetNumTexCoords();
						}
						if (NumUVs <= 0)
						{
							NumUVs = LoadedMesh->GetNumUVChannels(0);
						}

						if (NumUVs > 0)
						{
							if (LoadedMesh->GetLightMapCoordinateIndex() >= NumUVs)
							{
								LoadedMesh->SetLightMapCoordinateIndex((NumUVs > 1) ? 1 : 0);
								LoadedMesh->MarkPackageDirty();
							}
							if (LoadedMesh->GetLightMapResolution() <= 0)
							{
								LoadedMesh->SetLightMapResolution(64);
								LoadedMesh->MarkPackageDirty();
							}
						}
						else
						{
							SMC->SetMobility(EComponentMobility::Movable);
						}
					}

					int32 OverriddenRes = 0;
					if ((*Props)->TryGetNumberField(TEXT("OverriddenLightMapRes"), OverriddenRes) && OverriddenRes > 0)
					{
						int32 ScaledRes = FMath::RoundToInt(OverriddenRes * Config.LightmapResolutionMultiplier);
						if (ScaledRes > 0 && SMC->HasLightmapTextureCoordinates())
						{
							SMC->OverriddenLightMapRes = ScaledRes;
							SMC->bOverrideLightMapRes = true;
						}
					}

					ApplyActorMaterials(SMC);
				}
				continue;
			}

			if (Config.bImportDecals && Type == TEXT("DecalComponent"))
			{
				if (Config.bSkipBarrierDecals)
				{
					auto MatchesPrefix = [&](const FString& InStr) -> bool
					{
						if (InStr.IsEmpty()) return false;
						for (const FString& Prefix : Config.DecalFilterPrefixes)
						{
							if (!Prefix.IsEmpty() && InStr.StartsWith(Prefix, ESearchCase::IgnoreCase))
							{
								return true;
							}
						}
						return false;
					};

					bool bSkipBarrier = MatchesPrefix(OuterName);
					if (!bSkipBarrier && OuterObj && (*OuterObj).IsValid())
					{
						FString RawOuterName;
						if ((*OuterObj)->TryGetStringField(TEXT("ObjectName"), RawOuterName))
						{
							bSkipBarrier = MatchesPrefix(RawOuterName) || MatchesPrefix(ExtractCleanObjectName(RawOuterName));
						}
					}
					if (!bSkipBarrier)
					{
						bSkipBarrier = MatchesPrefix(Obj->GetStringField(TEXT("Name")));
					}

					if (bSkipBarrier)
					{
						continue;
					}
				}

				const TSharedPtr<FJsonObject>* DecalMaterialObj = nullptr;
				UMaterialInterface* Mat = nullptr;
				if ((*Props)->TryGetObjectField(TEXT("DecalMaterial"), DecalMaterialObj) && DecalMaterialObj && (*DecalMaterialObj).IsValid())
				{
					Mat = LoadMaterialFromRef(*DecalMaterialObj);
				}

				if (!Mat)
				{
					continue;
				}

				ADecalActor* Decal = World->SpawnActor<ADecalActor>(ADecalActor::StaticClass(), Transform);
				if (!Decal || !Decal->GetDecal())
				{
					continue;
				}
				Decal->SetActorLabel(OuterName);
				Decal->SetFolderPath(TEXT("Decals"));
				Decal->GetDecal()->SetDecalMaterial(Mat);

				const TSharedPtr<FJsonObject>* DecalSizeObj = nullptr;
				if ((*Props)->TryGetObjectField(TEXT("DecalSize"), DecalSizeObj) && DecalSizeObj && (*DecalSizeObj).IsValid())
				{
					const float DX = (*DecalSizeObj)->GetNumberField(TEXT("X"));
					const float DY = (*DecalSizeObj)->GetNumberField(TEXT("Y"));
					const float DZ = (*DecalSizeObj)->GetNumberField(TEXT("Z"));
					Decal->GetDecal()->DecalSize = FVector(DX, DY, DZ);
				}

				int32 SortOrder = 0;
				if ((*Props)->TryGetNumberField(TEXT("SortOrder"), SortOrder))
				{
					Decal->GetDecal()->SetSortOrder(SortOrder);
				}

				const TSharedPtr<FJsonObject>* LightColorOverrideObj = nullptr;
				if ((*Props)->TryGetObjectField(TEXT("LightColorOverride"), LightColorOverrideObj) && LightColorOverrideObj && (*LightColorOverrideObj).IsValid())
				{
					int32 R = 255, G = 255, B = 255, A = 255;
					(*LightColorOverrideObj)->TryGetNumberField(TEXT("R"), R);
					(*LightColorOverrideObj)->TryGetNumberField(TEXT("G"), G);
					(*LightColorOverrideObj)->TryGetNumberField(TEXT("B"), B);
					(*LightColorOverrideObj)->TryGetNumberField(TEXT("A"), A);

					const FColor SrgbColor(
						FMath::Clamp(R, 0, 255),
						FMath::Clamp(G, 0, 255),
						FMath::Clamp(B, 0, 255),
						FMath::Clamp(A, 0, 255)
					);
					Decal->GetDecal()->SetDecalColor(FLinearColor(SrgbColor));
				}

				continue;
			}

			if (Config.bImportLights && Type.EndsWith(TEXT("LightComponent")))
			{
				AActor* SpawnedLight = nullptr;
				ULightComponent* LightComp = nullptr;

				if (Type == TEXT("PointLightComponent"))
				{
					APointLight* PL = World->SpawnActor<APointLight>(APointLight::StaticClass(), Transform);
					if (PL)
					{
						PL->PointLightComponent->SetMobility(EComponentMobility::Movable);
						LightComp = PL->PointLightComponent;
						float Atten = 0.0f;
						if ((*Props)->TryGetNumberField(TEXT("AttenuationRadius"), Atten) && Atten > 0.0f)
						{
							PL->PointLightComponent->SetAttenuationRadius(Atten);
						}
						else
						{
							PL->PointLightComponent->SetAttenuationRadius(350.0f);
						}
						float SourceRad = 0.0f;
						if ((*Props)->TryGetNumberField(TEXT("SourceRadius"), SourceRad) && SourceRad > 0.0f)
						{
							PL->PointLightComponent->SetSourceRadius(SourceRad);
						}
						SpawnedLight = PL;
					}
				}
				else if (Type == TEXT("SpotLightComponent"))
				{
					ASpotLight* SL = World->SpawnActor<ASpotLight>(ASpotLight::StaticClass(), Transform);
					if (SL)
					{
						SL->SpotLightComponent->SetMobility(EComponentMobility::Movable);
						LightComp = SL->SpotLightComponent;
						float Atten = 0.0f;
						if ((*Props)->TryGetNumberField(TEXT("AttenuationRadius"), Atten) && Atten > 0.0f)
						{
							SL->SpotLightComponent->SetAttenuationRadius(Atten);
						}
						else
						{
							SL->SpotLightComponent->SetAttenuationRadius(450.0f);
						}
						float InnerCone = 0.0f;
						if ((*Props)->TryGetNumberField(TEXT("InnerConeAngle"), InnerCone))
						{
							SL->SpotLightComponent->SetInnerConeAngle(InnerCone);
						}
						float OuterCone = 0.0f;
						if ((*Props)->TryGetNumberField(TEXT("OuterConeAngle"), OuterCone) && OuterCone > 0.0f)
						{
							SL->SpotLightComponent->SetOuterConeAngle(OuterCone);
						}
						SpawnedLight = SL;
					}
				}
				else if (Type == TEXT("RectLightComponent"))
				{
					ARectLight* RL = World->SpawnActor<ARectLight>(ARectLight::StaticClass(), Transform);
					if (RL)
					{
						RL->RectLightComponent->SetMobility(EComponentMobility::Movable);
						LightComp = RL->RectLightComponent;
						float Atten = 0.0f;
						if ((*Props)->TryGetNumberField(TEXT("AttenuationRadius"), Atten) && Atten > 0.0f)
						{
							RL->RectLightComponent->SetAttenuationRadius(Atten);
						}
						else
						{
							RL->RectLightComponent->SetAttenuationRadius(350.0f);
						}
						float SourceWidth = 0.0f;
						if ((*Props)->TryGetNumberField(TEXT("SourceWidth"), SourceWidth) && SourceWidth > 0.0f)
						{
							RL->RectLightComponent->SetSourceWidth(SourceWidth);
						}
						float SourceHeight = 0.0f;
						if ((*Props)->TryGetNumberField(TEXT("SourceHeight"), SourceHeight) && SourceHeight > 0.0f)
						{
							RL->RectLightComponent->SetSourceHeight(SourceHeight);
						}
						SpawnedLight = RL;
					}
				}
				else if (Type == TEXT("SkyLightComponent"))
				{
					ASkyLight* SkyL = World->SpawnActor<ASkyLight>(ASkyLight::StaticClass(), Transform);
					if (SkyL)
					{
						SkyL->SetActorLabel(OuterName);
						SkyL->SetFolderPath(TEXT("Lights"));
						if (USkyLightComponent* Sky = SkyL->GetLightComponent())
						{
							Sky->SetMobility(EComponentMobility::Movable);
							Sky->SetRealTimeCapture(false);

							UTextureCube* Cube = nullptr;
							const TSharedPtr<FJsonObject>* CubeObj = nullptr;
							if ((*Props)->TryGetObjectField(TEXT("Cubemap"), CubeObj) && CubeObj && (*CubeObj).IsValid())
							{
								FString Path;
								if ((*CubeObj)->TryGetStringField(TEXT("ObjectPath"), Path))
								{
									Cube = LoadObject<UTextureCube>(nullptr, *UianaNormalizeObjectPath(Path));
								}
							}

							if (Cube)
							{
								Sky->SourceType = SLS_SpecifiedCubemap;
								Sky->SetCubemap(Cube);
								Sky->bLowerHemisphereIsBlack = false;
								Sky->MarkRenderStateDirty();
								Sky->RecaptureSky();
							}
							else
							{
								Sky->bLowerHemisphereIsBlack = false;
								Sky->MarkRenderStateDirty();
							}

							float I = 1.0f;
							if ((*Props)->TryGetNumberField(TEXT("Intensity"), I) && I > 0.0f)
							{
								Sky->SetIntensity(I);
							}

							float II = 1.0f;
							if ((*Props)->TryGetNumberField(TEXT("IndirectLightingIntensity"), II))
							{
								Sky->SetIndirectLightingIntensity(FMath::Clamp(II, 0.0f, 100.0f));
							}

							float Thresh = 0.0f;
							if ((*Props)->TryGetNumberField(TEXT("SkyDistanceThreshold"), Thresh) && Thresh > 0.0f)
							{
								Sky->SkyDistanceThreshold = Thresh;
								Sky->MarkRenderStateDirty();
							}

							const TSharedPtr<FJsonObject>* ColorObj = nullptr;
							if ((*Props)->TryGetObjectField(TEXT("LightColor"), ColorObj) && ColorObj && (*ColorObj).IsValid())
							{
								int32 R = 255, G = 255, B = 255;
								(*ColorObj)->TryGetNumberField(TEXT("R"), R);
								(*ColorObj)->TryGetNumberField(TEXT("G"), G);
								(*ColorObj)->TryGetNumberField(TEXT("B"), B);
								Sky->SetLightColor(FLinearColor(FColor(R, G, B, 255)));
							}
						}
					}
					continue;
				}
				else if (Type == TEXT("DirectionalLightComponent"))
				{
					if (OuterName.Contains(TEXT("directional_look_up")))
					{
						continue;
					}
					ADirectionalLight* DL = World->SpawnActor<ADirectionalLight>(ADirectionalLight::StaticClass(), Transform);
					if (DL)
					{
						LightComp = DL->GetComponent();
						if (UDirectionalLightComponent* DLC = Cast<UDirectionalLightComponent>(LightComp))
						{
							DLC->SetForwardShadingPriority(1);
						}
						SpawnedLight = DL;
					}
				}

				if (SpawnedLight)
				{
					SpawnedLight->SetActorLabel(OuterName);
					SpawnedLight->SetFolderPath(TEXT("Lights"));
					if (LightComp)
					{
						if (Type == TEXT("DirectionalLightComponent"))
						{
							float SunIntensity = 7.0f;
							(*Props)->TryGetNumberField(TEXT("Intensity"), SunIntensity);
							LightComp->SetIntensity(SunIntensity);

							float IndirectIntensity = 2.0f;
							if ((*Props)->TryGetNumberField(TEXT("IndirectLightingIntensity"), IndirectIntensity))
							{
								LightComp->SetIndirectLightingIntensity(FMath::Clamp(IndirectIntensity, 0.0f, 100.0f));
							}
							else
							{
								LightComp->SetIndirectLightingIntensity(2.0f);
							}

							const TSharedPtr<FJsonObject>* ColorObj = nullptr;
							if ((*Props)->TryGetObjectField(TEXT("LightColor"), ColorObj) && ColorObj && (*ColorObj).IsValid())
							{
								int32 R = 255, G = 255, B = 255;
								(*ColorObj)->TryGetNumberField(TEXT("R"), R);
								(*ColorObj)->TryGetNumberField(TEXT("G"), G);
								(*ColorObj)->TryGetNumberField(TEXT("B"), B);
								LightComp->SetLightColor(FLinearColor(FColor(R, G, B, 255)));
							}

							FString MobilityStr;
							EComponentMobility::Type FinalMobility = EComponentMobility::Stationary;
							if (Config.bHonorJsonMobility && (*Props)->TryGetStringField(TEXT("Mobility"), MobilityStr))
							{
								FinalMobility = UianaParseMobility(MobilityStr);
							}
							LightComp->SetMobility(FinalMobility);
						}
						else
						{
							ELightUnits Units = ELightUnits::Unitless;
							if (ULocalLightComponent* Local = Cast<ULocalLightComponent>(LightComp))
							{
								FString UnitsStr;
								if ((*Props)->TryGetStringField(TEXT("IntensityUnits"), UnitsStr))
								{
									UianaParseLightUnits(UnitsStr, Units);
								}
								Local->SetIntensityUnits(Units);
							}

							if (UPointLightComponent* Point = Cast<UPointLightComponent>(LightComp))
							{
								bool bInverse = true;
								if ((*Props)->TryGetBoolField(TEXT("bUseInverseSquaredFalloff"), bInverse))
								{
									Point->SetUseInverseSquaredFalloff(bInverse);
								}
								float Exp = 0.0f;
								if (!bInverse && (*Props)->TryGetNumberField(TEXT("LightFalloffExponent"), Exp) && Exp > 0.0f)
								{
									Point->SetLightFalloffExponent(Exp);
								}
							}

							double RawIntensity = 4.5;
							if ((*Props)->TryGetNumberField(TEXT("Intensity"), RawIntensity) && RawIntensity >= 0.0)
							{
								const double Scale = (Units == ELightUnits::Unitless) ? (double)Config.UnitlessIntensityScale : 1.0;
								LightComp->SetIntensity((float)FMath::Clamp(RawIntensity * Scale, 0.0, 1.0e7));
							}
							else
							{
								LightComp->SetIntensity(4.5f);
							}

							float IndirectIntensity = 1.0f;
							if ((*Props)->TryGetNumberField(TEXT("IndirectLightingIntensity"), IndirectIntensity) && IndirectIntensity > 0.0f)
							{
								LightComp->SetIndirectLightingIntensity(FMath::Clamp(IndirectIntensity, 0.0f, 100.0f));
							}
							else
							{
								LightComp->SetIndirectLightingIntensity(1.0f);
							}

							const TSharedPtr<FJsonObject>* ColorObj = nullptr;
							if ((*Props)->TryGetObjectField(TEXT("LightColor"), ColorObj) && ColorObj && (*ColorObj).IsValid())
							{
								int32 R = 255, G = 255, B = 255;
								(*ColorObj)->TryGetNumberField(TEXT("R"), R);
								(*ColorObj)->TryGetNumberField(TEXT("G"), G);
								(*ColorObj)->TryGetNumberField(TEXT("B"), B);
								LightComp->SetLightColor(FLinearColor(FColor(R, G, B, 255)));
							}

							bool bCastShadows = true;
							(*Props)->TryGetBoolField(TEXT("CastShadows"), bCastShadows);
							LightComp->SetCastShadows(bCastShadows);

							FString MobilityStr;
							EComponentMobility::Type FinalMobility = EComponentMobility::Static;
							if (Config.bHonorJsonMobility && (*Props)->TryGetStringField(TEXT("Mobility"), MobilityStr))
							{
								FinalMobility = UianaParseMobility(MobilityStr);
							}
							LightComp->SetMobility(FinalMobility);
						}
					}
				}
				continue;
			}

			if (Config.bImportLights && Type == TEXT("PostProcessVolume"))
			{
				APostProcessVolume* PPV = World->SpawnActor<APostProcessVolume>(APostProcessVolume::StaticClass(), Transform);
				if (PPV)
				{
					PPV->SetActorLabel(OuterName);
					PPV->SetFolderPath(TEXT("Lights"));
					PPV->bUnbound = true;

					const TSharedPtr<FJsonObject>* SettingsObj = nullptr;
					if ((*Props)->TryGetObjectField(TEXT("Settings"), SettingsObj) && SettingsObj && (*SettingsObj).IsValid())
					{
						auto GetOverride = [&](const TCHAR* Flag) -> bool
						{
							bool bFlagVal = false;
							return (*SettingsObj)->TryGetBoolField(Flag, bFlagVal) && bFlagVal;
						};

						float WhiteTemp = 0.0f;
						if ((*SettingsObj)->TryGetNumberField(TEXT("WhiteTemp"), WhiteTemp) && GetOverride(TEXT("bOverride_WhiteTemp")))
						{
							PPV->Settings.bOverride_WhiteTemp = true;
							PPV->Settings.WhiteTemp = WhiteTemp;
						}

						float BloomInt = 0.0f;
						if ((*SettingsObj)->TryGetNumberField(TEXT("BloomIntensity"), BloomInt) && GetOverride(TEXT("bOverride_BloomIntensity")))
						{
							PPV->Settings.bOverride_BloomIntensity = true;
							PPV->Settings.BloomIntensity = BloomInt;
						}

						float IndirectInt = 0.0f;
						if ((*SettingsObj)->TryGetNumberField(TEXT("IndirectLightingIntensity"), IndirectInt) && GetOverride(TEXT("bOverride_IndirectLightingIntensity")))
						{
							PPV->Settings.bOverride_IndirectLightingIntensity = true;
							PPV->Settings.IndirectLightingIntensity = IndirectInt;
						}

						const TSharedPtr<FJsonObject>* IndirectColorObj = nullptr;
						if (GetOverride(TEXT("bOverride_IndirectLightingColor")) && (*SettingsObj)->TryGetObjectField(TEXT("IndirectLightingColor"), IndirectColorObj) && IndirectColorObj && (*IndirectColorObj).IsValid())
						{
							float R = 1.0f, G = 1.0f, B = 1.0f, A = 1.0f;
							(*IndirectColorObj)->TryGetNumberField(TEXT("R"), R);
							(*IndirectColorObj)->TryGetNumberField(TEXT("G"), G);
							(*IndirectColorObj)->TryGetNumberField(TEXT("B"), B);
							(*IndirectColorObj)->TryGetNumberField(TEXT("A"), A);
							PPV->Settings.bOverride_IndirectLightingColor = true;
							PPV->Settings.IndirectLightingColor = FLinearColor(R, G, B, A);
						}

						bool bOverrideMin = GetOverride(TEXT("bOverride_AutoExposureMinBrightness"));
						bool bOverrideMax = GetOverride(TEXT("bOverride_AutoExposureMaxBrightness"));
						float MinBri = 1.0f, MaxBri = 1.0f;
						(*SettingsObj)->TryGetNumberField(TEXT("AutoExposureMinBrightness"), MinBri);
						(*SettingsObj)->TryGetNumberField(TEXT("AutoExposureMaxBrightness"), MaxBri);

						if (bOverrideMin && bOverrideMax && FMath::IsNearlyEqual(MinBri, MaxBri))
						{
							PPV->Settings.bOverride_AutoExposureMethod = true;
							PPV->Settings.AutoExposureMethod = EAutoExposureMethod::AEM_Manual;
						}
						else
						{
							if (bOverrideMin)
							{
								PPV->Settings.bOverride_AutoExposureMinBrightness = true;
								PPV->Settings.AutoExposureMinBrightness = MinBri;
							}
							if (bOverrideMax)
							{
								PPV->Settings.bOverride_AutoExposureMaxBrightness = true;
								PPV->Settings.AutoExposureMaxBrightness = MaxBri;
							}
						}
						PPV->Settings.bOverride_AutoExposureBias = true;
						PPV->Settings.AutoExposureBias = 9.5f;
					}
				}
				continue;
			}

			if (Config.bImportFog && (Type == TEXT("ExponentialHeightFogComponent") || Type == TEXT("ExponentialHeightFog")))
			{
				AExponentialHeightFog* Fog = World->SpawnActor<AExponentialHeightFog>(AExponentialHeightFog::StaticClass(), Transform);
				if (Fog)
				{
					Fog->SetActorLabel(OuterName);
					Fog->SetFolderPath(TEXT("Lights"));
					if (UExponentialHeightFogComponent* FogComp = Fog->GetComponent())
					{
						float Density = 0.05f;
						if ((*Props)->TryGetNumberField(TEXT("FogDensity"), Density))
						{
							FogComp->SetFogDensity(Density);
						}
						float Falloff = 0.35f;
						if ((*Props)->TryGetNumberField(TEXT("FogHeightFalloff"), Falloff))
						{
							FogComp->SetFogHeightFalloff(Falloff);
						}
						float StartDist = 0.0f;
						if ((*Props)->TryGetNumberField(TEXT("StartDistance"), StartDist))
						{
							FogComp->SetStartDistance(StartDist);
						}
						float MaxOpacity = 1.0f;
						if ((*Props)->TryGetNumberField(TEXT("FogMaxOpacity"), MaxOpacity))
						{
							FogComp->SetFogMaxOpacity(MaxOpacity);
						}

						const TSharedPtr<FJsonObject>* InscatterObj = nullptr;
						if ((*Props)->TryGetObjectField(TEXT("FogInscatteringLuminance"), InscatterObj) && InscatterObj && (*InscatterObj).IsValid())
						{
							float R = 1.0f, G = 1.0f, B = 1.0f, A = 1.0f;
							(*InscatterObj)->TryGetNumberField(TEXT("R"), R);
							(*InscatterObj)->TryGetNumberField(TEXT("G"), G);
							(*InscatterObj)->TryGetNumberField(TEXT("B"), B);
							(*InscatterObj)->TryGetNumberField(TEXT("A"), A);
							FogComp->SetFogInscatteringColor(FLinearColor(R, G, B, A));
						}
						else if ((*Props)->TryGetObjectField(TEXT("FogInscatteringColor"), InscatterObj) && InscatterObj && (*InscatterObj).IsValid())
						{
							float R = 1.0f, G = 1.0f, B = 1.0f, A = 1.0f;
							(*InscatterObj)->TryGetNumberField(TEXT("R"), R);
							(*InscatterObj)->TryGetNumberField(TEXT("G"), G);
							(*InscatterObj)->TryGetNumberField(TEXT("B"), B);
							(*InscatterObj)->TryGetNumberField(TEXT("A"), A);
							FogComp->SetFogInscatteringColor(FLinearColor(R, G, B, A));
						}

						const TSharedPtr<FJsonObject>* DirInscatterObj = nullptr;
						if ((*Props)->TryGetObjectField(TEXT("DirectionalInscatteringLuminance"), DirInscatterObj) && DirInscatterObj && (*DirInscatterObj).IsValid())
						{
							float R = 1.0f, G = 1.0f, B = 1.0f, A = 1.0f;
							(*DirInscatterObj)->TryGetNumberField(TEXT("R"), R);
							(*DirInscatterObj)->TryGetNumberField(TEXT("G"), G);
							(*DirInscatterObj)->TryGetNumberField(TEXT("B"), B);
							(*DirInscatterObj)->TryGetNumberField(TEXT("A"), A);
							FogComp->SetDirectionalInscatteringColor(FLinearColor(R, G, B, A));
						}
						else if ((*Props)->TryGetObjectField(TEXT("DirectionalInscatteringColor"), DirInscatterObj) && DirInscatterObj && (*DirInscatterObj).IsValid())
						{
							float R = 1.0f, G = 1.0f, B = 1.0f, A = 1.0f;
							(*DirInscatterObj)->TryGetNumberField(TEXT("R"), R);
							(*DirInscatterObj)->TryGetNumberField(TEXT("G"), G);
							(*DirInscatterObj)->TryGetNumberField(TEXT("B"), B);
							(*DirInscatterObj)->TryGetNumberField(TEXT("A"), A);
							FogComp->SetDirectionalInscatteringColor(FLinearColor(R, G, B, A));
						}

						FogComp->MarkRenderStateDirty();
					}
				}
				continue;
			}

			if (Config.bImportBlueprints && Type.EndsWith(TEXT("_C")))
			{
				const FString BPName = Type.LeftChop(2);
				const FString BPPath = FString::Printf(TEXT("/Game/ValorantContent/Blueprints/%s.%s"), *BPName, *BPName);
				UBlueprint* BPAsset = Cast<UBlueprint>(StaticLoadObject(UBlueprint::StaticClass(), nullptr, *BPPath));
				if (!BPAsset || !BPAsset->GeneratedClass)
				{
					continue;
				}

				AActor* BPActor = World->SpawnActor<AActor>(BPAsset->GeneratedClass, Transform);
				if (BPActor)
				{
					BPActor->SetActorLabel(Obj->GetStringField(TEXT("Name")));
					if (BPName.StartsWith(TEXT("BP_"), ESearchCase::IgnoreCase))
					{
						BPActor->SetFolderPath(TEXT("Collision/Blueprints"));
						BPActor->SetActorHiddenInGame(true);
						BPActor->SetIsTemporarilyHiddenInEditor(true);
					}
					else
					{
						BPActor->SetFolderPath(TEXT("Blueprints"));
					}
				}
			}
		}
	}

	bool PortedImportMap(const FUianaImportConfig& Config)
	{
		FText ErrorMessage;
		if (!FUianaConfigManager::Get().LoadRules(ErrorMessage))
		{
			UE_LOG(LogTemp, Error, TEXT("Uiana Import Aborted: %s"), *ErrorMessage.ToString());
			FMessageDialog::Open(EAppMsgType::Ok, ErrorMessage);
			return false;
		}

		const FMapPaths Paths = MakeMapPaths(Config);
		EnsureMapDirectories(Config, Paths);

		TArray<FString> UMapJsonPaths;

		if (ShouldReExport(Config, Paths))
		{
			ExtractData(Config, Paths.UMaps, FString());
			ExtractAssets(Config, Paths);

			ListFiles(Paths.UMaps, TEXT("json"), UMapJsonPaths);
			TArray<FString> ObjectList;
			TArray<FString> ActorList;
			TArray<FString> OverrideMaterialList;

			for (const FString& UMapFile : UMapJsonPaths)
			{
				TArray<TSharedPtr<FJsonValue>> UMapArray;
				if (!ReadJsonArray(UMapFile, UMapArray))
				{
					continue;
				}

				TArray<TSharedPtr<FJsonValue>> Filtered;
				FilterUMapAndCollectAssets(UMapArray, Filtered, ObjectList, OverrideMaterialList, ActorList, &Config);
				// Do not overwrite UMapFile with Filtered array — preserving full array keeps CUE4Parse export indices valid for AttachParent resolution!
			}

			const FString ActorsAssetList = BuildAssetFile(FPaths::Combine(Paths.Root, TEXT("_assets_actors.txt")), ActorList);
			ExtractData(Config, Paths.Actors, ActorsAssetList);

			TArray<FString> ActorJsonFiles;
			ListFiles(Paths.Actors, TEXT("json"), ActorJsonFiles);
			for (const FString& ActorJson : ActorJsonFiles)
			{
				TArray<TSharedPtr<FJsonValue>> ActorArray;
				if (!ReadJsonArray(ActorJson, ActorArray))
				{
					continue;
				}
				TArray<FString> DummyActors;
				TArray<TSharedPtr<FJsonValue>> DummyFiltered;
				FilterUMapAndCollectAssets(ActorArray, DummyFiltered, ObjectList, OverrideMaterialList, DummyActors, &Config);
			}

			const FString ObjectsAssetList = BuildAssetFile(FPaths::Combine(Paths.Root, TEXT("_assets_objects.txt")), ObjectList);
			const FString OverrideMatsAssetList = BuildAssetFile(FPaths::Combine(Paths.Root, TEXT("_assets_materials_ovr.txt")), OverrideMaterialList);
			ExtractData(Config, Paths.Objects, ObjectsAssetList);
			ExtractData(Config, Paths.MaterialsOverride, OverrideMatsAssetList);

			TArray<FString> ObjectJsonFiles;
			ListFiles(Paths.Objects, TEXT("json"), ObjectJsonFiles);
			TArray<FString> MaterialsList;
			for (const FString& ObjFile : ObjectJsonFiles)
			{
				TArray<TSharedPtr<FJsonValue>> ObjArray;
				if (ReadJsonArray(ObjFile, ObjArray))
				{
					AppendMaterialsFromStaticMeshDefinition(ObjArray, MaterialsList);
				}
			}

			TArray<FString> AllAssets;
			AllAssets.Reserve(ObjectList.Num() + MaterialsList.Num() + OverrideMaterialList.Num());
			for (const FString& Path : ObjectList)
			{
				AllAssets.Add(PathConvert(Path));
			}
			for (const FString& Path : MaterialsList)
			{
				AllAssets.Add(PathConvert(Path));
			}
			for (const FString& Path : OverrideMaterialList)
			{
				AllAssets.Add(PathConvert(Path));
			}
			const FString MaterialAssetList = BuildAssetFile(FPaths::Combine(Paths.Root, TEXT("_assets_materials.txt")), MaterialsList);
			ExtractData(Config, Paths.Materials, MaterialAssetList);

			TSet<FString> MaterialTexturePaths;
			CollectTexturesFromMaterialFolders(Paths.Materials, Paths.MaterialsOverride, MaterialTexturePaths);
			for (const FString& TexPath : MaterialTexturePaths)
			{
				AllAssets.Add(TexPath);
			}

			BuildAssetFile(FPaths::Combine(Paths.Root, TEXT("all_assets.txt")), AllAssets);
			ExtractAssets(Config, Paths);
			WriteExportMarker(Paths, Config);
		}
		else
		{
			ListFiles(Paths.UMaps, TEXT("json"), UMapJsonPaths);
		}

		if (Config.bImportMaterials)
		{
			// Check if any textures need to be exported (e.g. from an existing map export)
			TSet<FString> MaterialTexturePaths;
			CollectTexturesFromMaterialFolders(Paths.Materials, Paths.MaterialsOverride, MaterialTexturePaths);

			TArray<FString> MissingTextures;
			for (const FString& TexRel : MaterialTexturePaths)
			{
				const FString CleanTexRel = (TexRel.StartsWith(TEXT("/")) || TexRel.StartsWith(TEXT("\\"))) ? TexRel.RightChop(1) : TexRel;
				const FString LocalPath = FPaths::Combine(Config.AssetsRoot(), CleanTexRel + Config.TextureFormat);
				if (!FPaths::FileExists(LocalPath))
				{
					MissingTextures.Add(TexRel);
				}
			}

			if (MissingTextures.Num() > 0)
			{
				const FString MissingTexList = BuildAssetFile(FPaths::Combine(Paths.Root, TEXT("_assets_textures_missing.txt")), MissingTextures);
				ExtractAssets(Config, Paths, MissingTexList);
			}

			TSet<FString> AllTextures;
			TArray<FString> MatFiles;
			ListFiles(Paths.Materials, TEXT("json"), MatFiles);
			for (const FString& MatFile : MatFiles)
			{
				ImportAllTexturesFromMaterialJson(MatFile, Config, AllTextures);
			}
			TArray<FString> MatOvrFiles;
			ListFiles(Paths.MaterialsOverride, TEXT("json"), MatOvrFiles);
			for (const FString& MatFile : MatOvrFiles)
			{
				ImportAllTexturesFromMaterialJson(MatFile, Config, AllTextures);
			}

			if (AllTextures.Num() > 0)
			{
				TArray<FString> TextureArray = AllTextures.Array();
				UBPFL::ImportTextures(TextureArray);
			}
			ImportMaterialsFromJsonFolder(Paths.Materials);
			ImportMaterialsFromJsonFolder(Paths.MaterialsOverride);
		}

		if (Config.bImportMeshes)
		{
			TArray<FString> MeshSources;
			TArray<FString> Lines;
			FFileHelper::LoadFileToStringArray(Lines, *FPaths::Combine(Paths.Root, TEXT("_assets_objects.txt")));
			for (const FString& Line : Lines)
			{
				if (Line.IsEmpty())
				{
					continue;
				}
				const FString Lower = Line.ToLower();
				if (Lower.StartsWith(TEXT("engine\\")))
				{
					continue;
				}
				const FString Filename = FPaths::GetBaseFilename(Line);
				if (IsBlacklisted(Filename))
				{
					continue;
				}
				if (Config.bFilterMeshes)
				{
					bool bSkip = false;
					for (const FString& Prefix : Config.MeshFilterPrefixes)
					{
						if (Filename.StartsWith(Prefix, ESearchCase::IgnoreCase))
						{
							bSkip = true;
							break;
						}
					}
					if (bSkip)
					{
						continue;
					}
				}

				FString Relative = Line;
				Relative.ReplaceInline(TEXT("ShooterGame\\Content\\"), TEXT("Game\\"));
				Relative.ReplaceInline(TEXT("Engine\\Content\\"), TEXT("Engine\\"));
				const FString FullPath = FPaths::Combine(Config.AssetsRoot(), Relative + TEXT(".uemodel"));
				if (FPaths::FileExists(FullPath))
				{
					MeshSources.AddUnique(FullPath);
				}
			}

			if (MeshSources.Num() > 0)
			{
				UBPFL::ImportMeshes(MeshSources, Paths.Objects);
			}

			if (Config.bImportMaterials)
			{
				TArray<FString> MeshFiles;
				ListFiles(Paths.Objects, TEXT("uemodel"), MeshFiles);
				for (const FString& MeshFile : MeshFiles)
				{
					const FString MeshBaseName = FPaths::GetBaseFilename(MeshFile);
					const FString MeshAssetPath = FString::Printf(TEXT("/Game/ValorantContent/Meshes/%s.%s"), *MeshBaseName, *MeshBaseName);
					if (UStaticMesh* SM = Cast<UStaticMesh>(StaticLoadObject(UStaticMesh::StaticClass(), nullptr, *MeshAssetPath)))
					{
						bool bMeshModified = false;
						const FString JsonFile = FPaths::Combine(Paths.Objects, MeshBaseName + TEXT(".json"));
						TArray<TSharedPtr<FJsonValue>> MeshJson;
						TSharedPtr<FJsonObject> MeshProps;
						if (FPaths::FileExists(JsonFile) && ReadJsonArray(JsonFile, MeshJson) && MeshJson.Num() > 0)
						{
							for (const auto& Val : MeshJson)
							{
								TSharedPtr<FJsonObject> Obj = Val->AsObject();
								if (Obj.IsValid() && Obj->GetStringField(TEXT("Type")) == TEXT("StaticMesh"))
								{
									const TSharedPtr<FJsonObject>* P = nullptr;
									if (Obj->TryGetObjectField(TEXT("Properties"), P) && P && (*P).IsValid())
									{
										MeshProps = *P;
										break;
									}
								}
							}
						}

						const TArray<TSharedPtr<FJsonValue>>* JsonStaticMats = nullptr;
						if (MeshProps.IsValid())
						{
							MeshProps->TryGetArrayField(TEXT("StaticMaterials"), JsonStaticMats);
						}

						if (JsonStaticMats)
						{
							TArray<FStaticMaterial>& MeshStaticMats = SM->GetStaticMaterials();
							const int32 NumMats = MeshStaticMats.Num();
							for (int32 SlotIdx = 0; SlotIdx < JsonStaticMats->Num() && SlotIdx < NumMats; ++SlotIdx)
							{
								const TSharedPtr<FJsonObject> MatObj = (*JsonStaticMats)[SlotIdx]->AsObject();
								if (!MatObj.IsValid()) continue;

								FString JsonSlotName = MatObj->GetStringField(TEXT("MaterialSlotName"));
								FString JsonImportedSlotName = MatObj->GetStringField(TEXT("ImportedMaterialSlotName"));

								if (!JsonSlotName.IsEmpty() && JsonSlotName != TEXT("WorldGridMaterial"))
								{
									if (MeshStaticMats[SlotIdx].MaterialSlotName != FName(*JsonSlotName))
									{
										MeshStaticMats[SlotIdx].MaterialSlotName = FName(*JsonSlotName);
										bMeshModified = true;
									}
								}
								if (!JsonImportedSlotName.IsEmpty())
								{
									if (MeshStaticMats[SlotIdx].ImportedMaterialSlotName != FName(*JsonImportedSlotName))
									{
										MeshStaticMats[SlotIdx].ImportedMaterialSlotName = FName(*JsonImportedSlotName);
										bMeshModified = true;
									}
								}

								FString RawMatName;
								const TSharedPtr<FJsonObject>* MatInterface = nullptr;
								if (MatObj->TryGetObjectField(TEXT("MaterialInterface"), MatInterface) && MatInterface && (*MatInterface).IsValid())
								{
									RawMatName = (*MatInterface)->GetStringField(TEXT("ObjectName"));
									int32 SingleQuote = INDEX_NONE;
									if (RawMatName.FindChar(TEXT('\''), SingleQuote))
									{
										RawMatName = RawMatName.Mid(SingleQuote + 1);
										if (RawMatName.FindLastChar(TEXT('\''), SingleQuote))
										{
											RawMatName = RawMatName.Left(SingleQuote);
										}
									}
								}

								auto IsDccPlaceholderName = [](const FString& InName) -> bool
								{
									return InName.IsEmpty() ||
										   InName == TEXT("WorldGridMaterial") ||
										   InName == TEXT("DefaultMaterial") ||
										   InName.StartsWith(TEXT("MaterialSlot_")) ||
										   InName.StartsWith(TEXT("lambert"), ESearchCase::IgnoreCase) ||
										   InName.Contains(TEXT("Default"), ESearchCase::IgnoreCase) ||
										   InName.StartsWith(TEXT("phong"), ESearchCase::IgnoreCase) ||
										   InName.StartsWith(TEXT("blinn"), ESearchCase::IgnoreCase);
								};

								if (RawMatName.IsEmpty() || RawMatName == TEXT("WorldGridMaterial") || RawMatName == TEXT("DefaultMaterial"))
								{
									if (!JsonSlotName.IsEmpty() && !IsDccPlaceholderName(JsonSlotName))
									{
										RawMatName = JsonSlotName;
									}
									else if (MeshBaseName == TEXT("MidFloor_1"))
									{
										RawMatName = (SlotIdx == 0) ? TEXT("Brick_M6_HerringBoneBroken_MI") : TEXT("Brick_M6_HerringBoneAsphalt_MI");
									}
								}

								if (RawMatName == TEXT("Stone_M2_Steps_MI1"))
								{
									RawMatName = TEXT("Stone_M2_Steps_MI");
								}

								RawMatName = UBPFL::ResolveCleanMaterialVariation(RawMatName);

								if (!RawMatName.IsEmpty() && RawMatName != TEXT("WorldGridMaterial") && RawMatName != TEXT("DefaultMaterial"))
								{
									FString MatPath = FString::Printf(TEXT("/Game/ValorantContent/Materials/%s.%s"), *RawMatName, *RawMatName);
									UMaterialInterface* Mat = Cast<UMaterialInterface>(StaticLoadObject(UMaterialInterface::StaticClass(), nullptr, *MatPath));
									if (!Mat)
									{
										MatPath = FString::Printf(TEXT("/Uiana/Materials/%s.%s"), *RawMatName, *RawMatName);
										Mat = Cast<UMaterialInterface>(StaticLoadObject(UMaterialInterface::StaticClass(), nullptr, *MatPath));
									}
									if (Mat && (SM->GetMaterial(SlotIdx) != Mat || MeshStaticMats[SlotIdx].MaterialInterface != Mat))
									{
										SM->SetMaterial(SlotIdx, Mat);
										MeshStaticMats[SlotIdx].MaterialInterface = Mat;
										bMeshModified = true;
									}
									if (Mat && (JsonSlotName.StartsWith(TEXT("lambert")) || JsonSlotName.Contains(TEXT("Default")) || JsonSlotName.StartsWith(TEXT("MaterialSlot_")) || JsonSlotName == TEXT("WorldGridMaterial")))
									{
										if (MeshStaticMats[SlotIdx].MaterialSlotName != FName(*Mat->GetName()))
										{
											MeshStaticMats[SlotIdx].MaterialSlotName = FName(*Mat->GetName());
											bMeshModified = true;
										}
									}
								}
							}
						}

						int32 NumUVs = 0;
						if (SM->GetRenderData() && SM->GetRenderData()->LODResources.IsValidIndex(0))
						{
							NumUVs = SM->GetRenderData()->LODResources[0].VertexBuffers.StaticMeshVertexBuffer.GetNumTexCoords();
						}
						if (NumUVs <= 0)
						{
							NumUVs = SM->GetNumUVChannels(0);
						}

						if (NumUVs > 0)
						{
							if (SM->GetLightMapCoordinateIndex() >= NumUVs)
							{
								SM->SetLightMapCoordinateIndex((NumUVs > 1) ? 1 : 0);
								bMeshModified = true;
							}
							if (SM->GetLightMapResolution() <= 0)
							{
								SM->SetLightMapResolution(64);
								bMeshModified = true;
							}
						}

						if (bMeshModified)
						{
							SM->MarkPackageDirty();
						}
					}
				}
			}
		}

		ListFiles(Paths.UMaps, TEXT("json"), UMapJsonPaths);
		FScopedSlowTask SpawnTask(UMapJsonPaths.Num(), FText::FromString(TEXT("Importing levels")));
		SpawnTask.MakeDialog(true);
		for (int32 Idx = UMapJsonPaths.Num() - 1; Idx >= 0; --Idx)
		{
			const FString& UMapFile = UMapJsonPaths[Idx];
			SpawnTask.EnterProgressFrame(1.f, FText::FromString(FString::Printf(TEXT("Importing %d/%d"), UMapJsonPaths.Num() - Idx, UMapJsonPaths.Num())));
			ImportMapActors(UMapFile, Config);
		}

		if (UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr)
		{
			if (AWorldSettings* WorldSettings = World->GetWorldSettings())
			{
				WorldSettings->LightmassSettings.NumIndirectLightingBounces = 10;
				WorldSettings->LightmassSettings.NumSkyLightingBounces = 1;
				WorldSettings->LightmassSettings.EnvironmentColor = FColor(0xEA, 0xF3, 0xFF, 0xFF);
				WorldSettings->LightmassSettings.EnvironmentIntensity = 1.5f;
				WorldSettings->LightmassSettings.DiffuseBoost = 1.5f;
				WorldSettings->MarkPackageDirty();
			}

			// Ensure all SkyLights in the map are Movable as requested
			for (TActorIterator<ASkyLight> It(World); It; ++It)
			{
				if (USkyLightComponent* SkyComp = (*It)->GetLightComponent())
				{
					SkyComp->SetMobility(EComponentMobility::Movable);
				}
			}

			// Ensure all PostProcessVolumes in the map have exposure = 9.5 as requested
			for (TActorIterator<APostProcessVolume> It(World); It; ++It)
			{
				(*It)->Settings.bOverride_AutoExposureBias = true;
				(*It)->Settings.AutoExposureBias = 9.5f;
			}

			if (Config.bSpawnDefaultSunAndSky)
			{
				// Ensure DirectionalLight is present (Stationary, 7.0 lux fallback)
				int32 NumDirectionalLights = 0;
				for (TActorIterator<ADirectionalLight> It(World); It; ++It)
				{
					NumDirectionalLights++;
					if ((*It)->GetActorLabel() == TEXT("Valorant_DirectionalLight"))
					{
						if (ULightComponent* DLComp = (*It)->GetComponent())
						{
							DLComp->SetIntensity(7.0f);
							DLComp->Intensity = 7.0f;
							DLComp->SetMobility(EComponentMobility::Stationary);
							DLComp->MarkRenderStateDirty();
						}
					}
				}

				if (NumDirectionalLights == 0)
				{
					const FRotator DefaultSunRotation(-45.0f, -45.0f, 0.0f);
					const FVector DefaultSunLocation(0.0f, 0.0f, 1000.0f);
					ADirectionalLight* DL = World->SpawnActor<ADirectionalLight>(ADirectionalLight::StaticClass(), FTransform(DefaultSunRotation, DefaultSunLocation));
					if (DL)
					{
						DL->SetActorLabel(TEXT("Valorant_DirectionalLight"));
						DL->SetFolderPath(TEXT("Lights"));
						if (ULightComponent* DLComp = DL->GetComponent())
						{
							DLComp->SetIntensity(7.0f);
							DLComp->Intensity = 7.0f;
							DLComp->SetMobility(EComponentMobility::Stationary);
							DLComp->MarkRenderStateDirty();
						}
					}
				}

				// Ensure SkyLight is present on default options
				bool bHasSkyLight = false;
				for (TActorIterator<ASkyLight> It(World); It; ++It)
				{
					bHasSkyLight = true;
					break;
				}
				if (!bHasSkyLight)
				{
					ASkyLight* SkyL = World->SpawnActor<ASkyLight>(ASkyLight::StaticClass());
					if (SkyL)
					{
						SkyL->SetActorLabel(TEXT("Valorant_SkyLight"));
						SkyL->SetFolderPath(TEXT("Lights"));
						if (USkyLightComponent* SkyComp = SkyL->GetLightComponent())
						{
							SkyComp->SetMobility(EComponentMobility::Movable);
							SkyComp->MarkRenderStateDirty();
						}
					}
				}
			}
			else
			{
				for (TActorIterator<ADirectionalLight> It(World); It; ++It)
				{
					if ((*It)->GetActorLabel() == TEXT("Valorant_DirectionalLight"))
					{
						World->DestroyActor(*It);
					}
				}
				for (TActorIterator<ASkyLight> It(World); It; ++It)
				{
					if ((*It)->GetActorLabel() == TEXT("Valorant_SkyLight"))
					{
						World->DestroyActor(*It);
					}
				}
			}

			if (Config.bImportLights)
			{
				bool bHasPPV = false;
				for (TActorIterator<APostProcessVolume> It(World); It; ++It)
				{
					bHasPPV = true;
					break;
				}
				if (!bHasPPV)
				{
					APostProcessVolume* PPV = World->SpawnActor<APostProcessVolume>(APostProcessVolume::StaticClass());
					if (PPV)
					{
						PPV->SetActorLabel(TEXT("Valorant_PostProcess"));
						PPV->SetFolderPath(TEXT("Lights"));
						PPV->bUnbound = true;
						PPV->Settings.bOverride_AutoExposureMethod = true;
						PPV->Settings.AutoExposureMethod = EAutoExposureMethod::AEM_Manual;
						PPV->Settings.bOverride_AutoExposureBias = true;
						PPV->Settings.AutoExposureBias = 9.5f;
						PPV->Settings.bOverride_WhiteTemp = true;
						PPV->Settings.WhiteTemp = 6500.0f;
						PPV->Settings.bOverride_BloomIntensity = true;
						PPV->Settings.BloomIntensity = 0.05f;
					}
				}

				if (Config.bSpawnSkyAtmosphere)
				{
					bool bHasSkyAtmosphere = false;
					for (TActorIterator<ASkyAtmosphere> It(World); It; ++It)
					{
						bHasSkyAtmosphere = true;
						break;
					}
					if (!bHasSkyAtmosphere)
					{
						ASkyAtmosphere* SkyAtmo = World->SpawnActor<ASkyAtmosphere>(ASkyAtmosphere::StaticClass());
						if (SkyAtmo)
						{
							SkyAtmo->SetActorLabel(TEXT("Valorant_SkyAtmosphere"));
							SkyAtmo->SetFolderPath(TEXT("Lights"));
						}
					}
					for (TActorIterator<ASkyLight> It(World); It; ++It)
					{
						if (USkyLightComponent* S = (*It)->GetLightComponent())
						{
							S->RecaptureSky();
						}
					}
				}
				else
				{
					for (TActorIterator<ASkyAtmosphere> It(World); It; ++It)
					{
						if ((*It)->GetActorLabel() == TEXT("Valorant_SkyAtmosphere"))
						{
							World->DestroyActor(*It);
						}
					}
				}
			}
		}

		FUianaConfigManager::Get().DumpMatchStatistics();
		return true;
	}
}

bool FUianaImporter::BuildConfig(const UUianaDataSettings& Settings, FUianaImportConfig& OutConfig)
{
	if (Settings.Map.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("Uiana: Map name is empty. Please select a map."));
		return false;
	}

	OutConfig.MapName = NormalizeMapName(Settings.Map);
	OutConfig.AESKey = GDefaultAES;
	OutConfig.TextureFormat = TEXT(".png");
	OutConfig.ExportRoot = Settings.ExportFolder.Path;
	OutConfig.PaksRoot = Settings.PaksFolder.Path;
	OutConfig.bImportDecals = Settings.ImportDecals;
	OutConfig.bSkipBarrierDecals = Settings.bSkipBarrierDecals;
	OutConfig.DecalFilterPrefixes = Settings.DecalFilterPrefixes;
	OutConfig.bImportBlueprints = Settings.ImportBlueprints;
	OutConfig.bImportLights = Settings.ImportLights;
	OutConfig.bSpawnDefaultSunAndSky = Settings.bSpawnDefaultSunAndSky;
	OutConfig.UnitlessIntensityScale = Settings.UnitlessIntensityScale;
	OutConfig.bHonorJsonMobility = Settings.bHonorJsonMobility;
	OutConfig.bSpawnSkyAtmosphere = Settings.bSpawnSkyAtmosphere;
	OutConfig.bImportFog = Settings.bImportFog;
	OutConfig.bImportMeshes = Settings.ImportMeshes;
	OutConfig.bFilterMeshes = Settings.bFilterMeshes;
	OutConfig.MeshFilterPrefixes = Settings.MeshFilterPrefixes;
	OutConfig.bMakeMeshesInvisible = Settings.bMakeMeshesInvisible;
	OutConfig.InvisibleMeshPrefixes = Settings.InvisibleMeshPrefixes;
	OutConfig.bImportMaterials = Settings.ImportMaterials;
	OutConfig.bImportSublevels = Settings.UseSubLevels;
	OutConfig.LightmapResolutionMultiplier = FMath::Max(Settings.LightmapResolutionMultiplier, 0.01f);

	TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("Uiana"));
	if (!Plugin.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("Uiana: failed to resolve plugin root path."));
		return false;
	}

	const FString ContentRoot = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Content"));
	const FString PreferredRoot = FPaths::Combine(ContentRoot, TEXT("Importer"));
	if (IFileManager::Get().DirectoryExists(*PreferredRoot))
	{
		OutConfig.PluginRoot = PreferredRoot;
		return true;
	}

	TArray<FString> CandidateDirs;
	IFileManager::Get().FindFiles(CandidateDirs, *FPaths::Combine(ContentRoot, TEXT("*")), false, true);
	for (const FString& CandidateName : CandidateDirs)
	{
		const FString CandidateRoot = FPaths::Combine(ContentRoot, CandidateName);
		const FString CandidateMaps = FPaths::Combine(CandidateRoot, TEXT("assets"), TEXT("umaps.json"));
		const FString CandidateTools = FPaths::Combine(CandidateRoot, TEXT("tools"));
		if (FPaths::FileExists(CandidateMaps) && IFileManager::Get().DirectoryExists(*CandidateTools))
		{
			OutConfig.PluginRoot = CandidateRoot;
			return true;
		}
	}

	UE_LOG(LogTemp, Error, TEXT("Uiana: failed to locate importer content root (expected Content/Importer or equivalent tools+assets folder)."));
	return false;
}

bool FUianaImporter::Run(const UUianaDataSettings& Settings)
{
	FUianaImportConfig Config;
	if (!BuildConfig(Settings, Config))
	{
		return false;
	}

	if (Config.ExportRoot.IsEmpty() || Config.PaksRoot.IsEmpty() || Config.MapName.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("Uiana: invalid settings. Export/Paks/Map must be set."));
		return false;
	}

	return PortedImportMap(Config);
}
