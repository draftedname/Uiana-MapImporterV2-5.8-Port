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
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/SavePackage.h"

namespace
{
	static const FString GDefaultAES = TEXT("0x4BE71AF2459CF83899EC9DC2CB60E22AC4B3047E0211034BBABE9D174C069DD6");

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

		const FString MatName = (ObjectName == TEXT("Stone_M2_Steps_MI1")) ? TEXT("Stone_M2_Steps_MI") : ObjectName;
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
			const bool bKeep = MeshTypes.Contains(TypeLower) || GenericTypes.Contains(TypeLower) || TypeLower == TEXT("decalcomponent") || TypeLower.EndsWith(TEXT("_c"));
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

			const TSharedPtr<FJsonObject>* DecalMaterial = nullptr;
			if ((*Props)->TryGetObjectField(TEXT("DecalMaterial"), DecalMaterial) && DecalMaterial && (*DecalMaterial).IsValid())
			{
				OutOverrideMaterials.Add(ToBackslashObjectPath((*DecalMaterial)->GetStringField(TEXT("ObjectPath"))));
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
				const TSharedPtr<FJsonObject>* Props = nullptr;
				if (!Obj->TryGetObjectField(TEXT("Properties"), Props) || !Props || !(*Props).IsValid()) continue;
				const TArray<TSharedPtr<FJsonValue>>* TexParams = nullptr;
				if ((*Props)->TryGetArrayField(TEXT("TextureParameterValues"), TexParams) && TexParams)
				{
					for (const auto& TP : *TexParams)
					{
						TSharedPtr<FJsonObject> TPObj = TP->AsObject();
						if (!TPObj.IsValid()) continue;
						const TSharedPtr<FJsonObject>* PVal = nullptr;
						if (TPObj->TryGetObjectField(TEXT("ParameterValue"), PVal) && PVal && (*PVal).IsValid())
						{
							FString ObjPath = (*PVal)->GetStringField(TEXT("ObjectPath"));
							if (!ObjPath.IsEmpty())
							{
								int32 Dot = INDEX_NONE;
								if (ObjPath.FindChar(TEXT('.'), Dot)) ObjPath = ObjPath.Left(Dot);
								OutTexturePaths.Add(PathConvert(ObjPath));
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

		const TSharedPtr<FJsonObject>* Props = nullptr;
		if (!RootObj->TryGetObjectField(TEXT("Properties"), Props) || !(*Props).IsValid())
		{
			return;
		}

		const TArray<TSharedPtr<FJsonValue>>* TextureParams = nullptr;
		if (!(*Props)->TryGetArrayField(TEXT("TextureParameterValues"), TextureParams) || !TextureParams)
		{
			return;
		}

		for (const TSharedPtr<FJsonValue>& ParamValue : *TextureParams)
		{
			const TSharedPtr<FJsonObject> ParamObj = ParamValue->AsObject();
			if (!ParamObj.IsValid())
			{
				continue;
			}

			const TSharedPtr<FJsonObject>* ParameterValueObj = nullptr;
			if (!ParamObj->TryGetObjectField(TEXT("ParameterValue"), ParameterValueObj) || !(*ParameterValueObj).IsValid())
			{
				continue;
			}

			const FString ObjectPath = (*ParameterValueObj)->GetStringField(TEXT("ObjectPath"));
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
		}
	}

	void ImportMaterialsFromJsonFolder(const FString& FolderPath)
	{
		TArray<FString> Files;
		ListFiles(FolderPath, TEXT("json"), Files);

		IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();

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

			FString ParentName = TEXT("BaseEnv_MAT_V4");
			const TSharedPtr<FJsonObject>* ParentObj = nullptr;
			if ((*Props)->TryGetObjectField(TEXT("Parent"), ParentObj) && ParentObj && (*ParentObj).IsValid())
			{
				ParentName = GetRefName(*ParentObj);
				if (ParentName.IsEmpty())
				{
					ParentName = TEXT("BaseEnv_MAT_V4");
				}
			}
			else if (MaterialName.StartsWith(TEXT("M_Sky_")))
			{
				ParentName = TEXT("BaseEnv_Unlit_Texture_MAT_V4");
			}

			FString PhysMatName;
			const TSharedPtr<FJsonObject>* PhysMatObj = nullptr;
			if ((*Props)->TryGetObjectField(TEXT("PhysMaterial"), PhysMatObj) && PhysMatObj && (*PhysMatObj).IsValid())
			{
				PhysMatName = GetRefName(*PhysMatObj);
			}

			// Glass check: physical material or naming pattern
			const bool bIsGlass = PhysMatName.Equals(TEXT("M_Glass"), ESearchCase::IgnoreCase) ||
				MaterialName.Contains(TEXT("Glass_")) ||
				MaterialName.Contains(TEXT("GlassWarped")) ||
				MaterialName.Contains(TEXT("GlassShard")) ||
				MaterialName.Contains(TEXT("Panes")) ||
				MaterialName.Contains(TEXT("LightFixtureGlass")) ||
				MaterialName.StartsWith(TEXT("Windows_"));

			// Foliage check: package path or foliage naming pattern (excluding bark, trunk, planter)
			const FString PackagePath = RootObj->GetStringField(TEXT("Package"));
			const bool bIsFoliage = (PackagePath.Contains(TEXT("/Foliage/")) ||
				MaterialName.StartsWith(TEXT("Foliage_")) ||
				MaterialName.Contains(TEXT("VegetationLeaves")) ||
				MaterialName.Contains(TEXT("BeechGreenFoliage")) ||
				MaterialName.Contains(TEXT("Arborvitae")) ||
				MaterialName.Contains(TEXT("Conifer")) ||
				MaterialName.Contains(TEXT("Ivy_")) ||
				MaterialName.Contains(TEXT("Flowers")) ||
				MaterialName.Contains(TEXT("PlantAlpha")) ||
				MaterialName.Contains(TEXT("Grass_M0_Cards")) ||
				MaterialName.Contains(TEXT("PlantPotted_0_M0_A")) ||
				MaterialName.Contains(TEXT("PlantPotted_0_M1_B")) ||
				MaterialName.Contains(TEXT("PlantPotted_0_M4_Hanging")) ||
				MaterialName.Contains(TEXT("PlantPotted_0_M5_Hanging")) ||
				MaterialName.Contains(TEXT("Plant_0_M0_corner")) ||
				MaterialName.Contains(TEXT("Plant_0_M0_Dead"))) &&
				!MaterialName.Contains(TEXT("Trunk")) &&
				!MaterialName.Contains(TEXT("Bark")) &&
				!MaterialName.Contains(TEXT("Planter"));

			if (bIsGlass || ParentName == TEXT("BaseOpacitySpecEnv_MAT"))
			{
				ParentName = TEXT("BaseOpacity_RGB_Env_MAT");
			}
			else if (bIsFoliage)
			{
				ParentName = TEXT("BaseEnv_MAT_V4_Foliage");
			}
			else if (ParentName == TEXT("0_Waterfall_Base1") || ParentName == TEXT("0_Waterfall_Vista1"))
			{
				ParentName = TEXT("0_Waterfall_Base");
			}

			UMaterialInterface* ParentMat = nullptr;
			if (MaterialName.StartsWith(TEXT("M_Sky_")))
			{
				const FString DirectSky = FString::Printf(TEXT("/Uiana/Materials/%s.%s"), *MaterialName, *MaterialName);
				ParentMat = Cast<UMaterialInterface>(StaticLoadObject(UMaterialInterface::StaticClass(), nullptr, *DirectSky));
				if (!ParentMat)
				{
					ParentMat = Cast<UMaterialInterface>(StaticLoadObject(UMaterialInterface::StaticClass(), nullptr, TEXT("/Uiana/Materials/BaseEnv_Unlit_Texture_MAT_V4.BaseEnv_Unlit_Texture_MAT_V4")));
				}
			}
			if (!ParentMat && (ParentName == TEXT("BaseEnv_MAT_V4_Foliage") || ParentName == TEXT("BaseOpacity_RGB_Env_MAT") || ParentName == TEXT("0_Waterfall_Base")))
			{
				const FString PluginMatPath = FString::Printf(TEXT("/Uiana/Materials/%s.%s"), *ParentName, *ParentName);
				ParentMat = Cast<UMaterialInterface>(StaticLoadObject(UMaterialInterface::StaticClass(), nullptr, *PluginMatPath));
			}
			if (!ParentMat && !ParentName.IsEmpty() && ParentName != TEXT("BaseEnv_MAT_V4"))
			{
				const FString ValoMatPath = FString::Printf(TEXT("/Game/ValorantContent/Materials/%s.%s"), *ParentName, *ParentName);
				ParentMat = Cast<UMaterialInterface>(StaticLoadObject(UMaterialInterface::StaticClass(), nullptr, *ValoMatPath));
			}
			if (!ParentMat && !ParentName.IsEmpty())
			{
				const FString PluginMatPath = FString::Printf(TEXT("/Uiana/Materials/%s.%s"), *ParentName, *ParentName);
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
					const FName ParamName((*ParamInfo)->GetStringField(TEXT("Name")).ToLower());
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
					const FName ParamName(*RawParamName);
					const FLinearColor Color(
						(*ParamValue)->GetNumberField(TEXT("R")),
						(*ParamValue)->GetNumberField(TEXT("G")),
						(*ParamValue)->GetNumberField(TEXT("B")),
						(*ParamValue)->GetNumberField(TEXT("A")));
					UMaterialEditingLibrary::SetMaterialInstanceVectorParameterValue(MIC, ParamName, Color);

					if (ParentName == TEXT("BaseOpacity_RGB_Env_MAT"))
					{
						if (RawParamName == TEXT("diffusecolor") || RawParamName == TEXT("tint") || RawParamName == TEXT("color mult"))
						{
							UMaterialEditingLibrary::SetMaterialInstanceVectorParameterValue(MIC, FName(TEXT("color mult")), Color);
						}
					}
				}
			}

			const TArray<TSharedPtr<FJsonValue>>* TextureParams = nullptr;
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

					const FString TextureName = GetRefName(*ParamValue);
					const FString TexturePath = FString::Printf(TEXT("/Game/ValorantContent/Textures/%s.%s"), *TextureName, *TextureName);
					if (UTexture* Texture = Cast<UTexture>(StaticLoadObject(UTexture::StaticClass(), nullptr, *TexturePath)))
					{
						const FString RawParamName = (*ParamInfo)->GetStringField(TEXT("Name")).ToLower();
						const FName ParamName(*RawParamName);
						UMaterialEditingLibrary::SetMaterialInstanceTextureParameterValue(MIC, ParamName, Texture);

						// Map common Valorant aliases to standard Uiana master material parameter names
						if (RawParamName == TEXT("materialtexture") || RawParamName == TEXT("diffusetex") || RawParamName == TEXT("diffuse a") || RawParamName == TEXT("albedo") || RawParamName == TEXT("texture rgb"))
						{
							UMaterialEditingLibrary::SetMaterialInstanceTextureParameterValue(MIC, FName(TEXT("diffuse")), Texture);
						}
						else if (RawParamName == TEXT("additonalnormal") || RawParamName == TEXT("texture a normal") || RawParamName == TEXT("normal a"))
						{
							UMaterialEditingLibrary::SetMaterialInstanceTextureParameterValue(MIC, FName(TEXT("normal")), Texture);
						}
						else if (RawParamName == TEXT("mra a") || RawParamName == TEXT("mra_texture"))
						{
							UMaterialEditingLibrary::SetMaterialInstanceTextureParameterValue(MIC, FName(TEXT("mra")), Texture);
						}

						if (ParentName == TEXT("BaseOpacity_RGB_Env_MAT"))
						{
							if (RawParamName == TEXT("diffuse") || RawParamName == TEXT("materialtexture") || RawParamName == TEXT("diffusetex") || RawParamName == TEXT("albedo"))
							{
								UMaterialEditingLibrary::SetMaterialInstanceTextureParameterValue(MIC, FName(TEXT("RGBA")), Texture);
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

				const bool bIsSuperGrid = IsSuperGridMesh(MeshName);
				const bool bIsBpCollision = OuterName.StartsWith(TEXT("BP_"), ESearchCase::IgnoreCase) || MeshName.StartsWith(TEXT("BP_"), ESearchCase::IgnoreCase);
				const bool bIsInvisible = bIsSuperGrid || bIsBpCollision;
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
						Actor->SetFolderPath(bIsBpCollision ? TEXT("Collision/Blueprints") : TEXT("Collision/SuperGrid"));
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

					int32 OverriddenHismRes = 0;
					if ((*Props)->TryGetNumberField(TEXT("OverriddenLightMapRes"), OverriddenHismRes) && OverriddenHismRes > 0)
					{
						Actor->HismComponent->OverriddenLightMapRes = FMath::RoundToInt(OverriddenHismRes * Config.LightmapResolutionMultiplier);
						Actor->HismComponent->bOverrideLightMapRes = true;
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

					const TArray<TSharedPtr<FJsonValue>>* OverrideMaterials = nullptr;
					if (Config.bImportMaterials && (*Props)->TryGetArrayField(TEXT("OverrideMaterials"), OverrideMaterials) && OverrideMaterials)
					{
						int32 MatIdx = 0;
						for (const TSharedPtr<FJsonValue>& MatValue : *OverrideMaterials)
						{
							UMaterialInterface* Mat = LoadMaterialFromRef(MatValue->AsObject());
							if (Mat)
							{
								Actor->HismComponent->SetMaterial(MatIdx, Mat);
							}
							MatIdx++;
						}
					}

					if (Config.bImportMaterials)
					{
						const TArray<FStaticMaterial>& StaticMats = LoadedMesh->GetStaticMaterials();
						for (int32 SlotIdx = 0; SlotIdx < StaticMats.Num(); ++SlotIdx)
						{
							if (!Actor->HismComponent->GetMaterial(SlotIdx))
							{
								if (LoadedMesh->GetMaterial(SlotIdx))
								{
									Actor->HismComponent->SetMaterial(SlotIdx, LoadedMesh->GetMaterial(SlotIdx));
								}
								else
								{
									const FString SlotName = StaticMats[SlotIdx].MaterialSlotName.ToString();
									const FString MatPath = FString::Printf(TEXT("/Game/ValorantContent/Materials/%s.%s"), *SlotName, *SlotName);
									if (UMaterialInterface* FallbackMat = Cast<UMaterialInterface>(StaticLoadObject(UMaterialInterface::StaticClass(), nullptr, *MatPath)))
									{
										Actor->HismComponent->SetMaterial(SlotIdx, FallbackMat);
										LoadedMesh->SetMaterial(SlotIdx, FallbackMat);
									}
								}
							}
						}
					}
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
						Actor->SetFolderPath(bIsBpCollision ? TEXT("Collision/Blueprints") : TEXT("Collision/SuperGrid"));
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

					int32 OverriddenRes = 0;
					if ((*Props)->TryGetNumberField(TEXT("OverriddenLightMapRes"), OverriddenRes) && OverriddenRes > 0)
					{
						SMC->OverriddenLightMapRes = FMath::RoundToInt(OverriddenRes * Config.LightmapResolutionMultiplier);
						SMC->bOverrideLightMapRes = true;
					}

					const TArray<TSharedPtr<FJsonValue>>* OverrideMaterials = nullptr;
					if (Config.bImportMaterials && (*Props)->TryGetArrayField(TEXT("OverrideMaterials"), OverrideMaterials) && OverrideMaterials)
					{
						int32 MatIdx = 0;
						for (const TSharedPtr<FJsonValue>& MatValue : *OverrideMaterials)
						{
							UMaterialInterface* Mat = LoadMaterialFromRef(MatValue->AsObject());
							if (Mat)
							{
								Actor->GetStaticMeshComponent()->SetMaterial(MatIdx, Mat);
							}
							MatIdx++;
						}
					}

					if (Config.bImportMaterials)
					{
						const TArray<FStaticMaterial>& StaticMats = LoadedMesh->GetStaticMaterials();
						for (int32 SlotIdx = 0; SlotIdx < StaticMats.Num(); ++SlotIdx)
						{
							if (!SMC->GetMaterial(SlotIdx))
							{
								if (LoadedMesh->GetMaterial(SlotIdx))
								{
									SMC->SetMaterial(SlotIdx, LoadedMesh->GetMaterial(SlotIdx));
								}
								else
								{
									const FString SlotName = StaticMats[SlotIdx].MaterialSlotName.ToString();
									const FString MatPath = FString::Printf(TEXT("/Game/ValorantContent/Materials/%s.%s"), *SlotName, *SlotName);
									if (UMaterialInterface* FallbackMat = Cast<UMaterialInterface>(StaticLoadObject(UMaterialInterface::StaticClass(), nullptr, *MatPath)))
									{
										SMC->SetMaterial(SlotIdx, FallbackMat);
										LoadedMesh->SetMaterial(SlotIdx, FallbackMat);
									}
								}
							}
						}
					}
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
						SpawnedLight = SkyL;
						if (USkyLightComponent* SkyComp = SkyL->GetLightComponent())
						{
							SkyComp->SetMobility(EComponentMobility::Movable);
							SkyComp->bRealTimeCapture = false;
							float IntensityVal = 1.8f;
							if ((*Props)->TryGetNumberField(TEXT("Intensity"), IntensityVal) && IntensityVal > 0.0f)
							{
								if (IntensityVal > 100.0f)
								{
									IntensityVal /= 1000.0f;
								}
								IntensityVal = FMath::Clamp(IntensityVal, 1.2f, 2.5f);
							}
							SkyComp->SetIntensity(IntensityVal);
							SkyComp->IndirectLightingIntensity = 1.0f;
							const TSharedPtr<FJsonObject>* ColorObj = nullptr;
							if ((*Props)->TryGetObjectField(TEXT("LightColor"), ColorObj) && ColorObj && (*ColorObj).IsValid())
							{
								int32 R = 255, G = 255, B = 255;
								(*ColorObj)->TryGetNumberField(TEXT("R"), R);
								(*ColorObj)->TryGetNumberField(TEXT("G"), G);
								(*ColorObj)->TryGetNumberField(TEXT("B"), B);
								SkyComp->SetLightColor(FLinearColor(FColor(R, G, B, 255)));
							}
						}
					}
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
						LightComp->SetMobility(EComponentMobility::Movable);

						float IntensityVal = 4.5f;
						if ((*Props)->TryGetNumberField(TEXT("Intensity"), IntensityVal) && IntensityVal > 0.0f)
						{
							if (Type == TEXT("DirectionalLightComponent"))
							{
								if (IntensityVal > 100.0f)
								{
									IntensityVal /= 1000.0f;
								}
								IntensityVal = FMath::Clamp(IntensityVal, 3.5f, 6.0f);
							}
							else
							{
								if (IntensityVal > 500.0f)
								{
									IntensityVal /= 100.0f;
								}
								IntensityVal = FMath::Clamp(IntensityVal, 0.1f, 15.0f);
							}
						}
						LightComp->SetIntensity(IntensityVal);

						float IndirectIntensity = 0.0f;
						if ((*Props)->TryGetNumberField(TEXT("IndirectLightingIntensity"), IndirectIntensity) && IndirectIntensity > 0.0f)
						{
							LightComp->SetIndirectLightingIntensity(FMath::Clamp(IndirectIntensity, 0.0f, 1.0f));
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
						float WhiteTemp = 0.0f;
						if ((*SettingsObj)->TryGetNumberField(TEXT("WhiteTemp"), WhiteTemp) && WhiteTemp > 0.0f)
						{
							PPV->Settings.bOverride_WhiteTemp = true;
							PPV->Settings.WhiteTemp = WhiteTemp;
						}
						float BloomInt = 0.0f;
						if ((*SettingsObj)->TryGetNumberField(TEXT("BloomIntensity"), BloomInt) && BloomInt > 0.0f)
						{
							PPV->Settings.bOverride_BloomIntensity = true;
							PPV->Settings.BloomIntensity = BloomInt;
						}
						float IndirectInt = 0.0f;
						if ((*SettingsObj)->TryGetNumberField(TEXT("IndirectLightingIntensity"), IndirectInt) && IndirectInt > 0.0f)
						{
							PPV->Settings.bOverride_IndirectLightingIntensity = true;
							PPV->Settings.IndirectLightingIntensity = IndirectInt;
						}
					}

					PPV->Settings.bOverride_AutoExposureMethod = true;
					PPV->Settings.AutoExposureMethod = EAutoExposureMethod::AEM_Basic;
					PPV->Settings.bOverride_AutoExposureMinBrightness = true;
					PPV->Settings.AutoExposureMinBrightness = 0.5f;
					PPV->Settings.bOverride_AutoExposureMaxBrightness = true;
					PPV->Settings.AutoExposureMaxBrightness = 2.0f;
					PPV->Settings.bOverride_AutoExposureBias = true;
					PPV->Settings.AutoExposureBias = 0.0f;
					PPV->Settings.bOverride_ColorSaturation = true;
					PPV->Settings.ColorSaturation = FVector4(1.08f, 1.08f, 1.08f, 1.0f);
					PPV->Settings.bOverride_IndirectLightingIntensity = true;
					PPV->Settings.IndirectLightingIntensity = 1.0f;
					PPV->Settings.bOverride_IndirectLightingColor = true;
					PPV->Settings.IndirectLightingColor = FLinearColor(1.0f, 0.956f, 0.88f, 1.0f);
					PPV->Settings.bOverride_BloomIntensity = true;
					PPV->Settings.BloomIntensity = 0.05f;
					if (!PPV->Settings.bOverride_WhiteTemp)
					{
						PPV->Settings.bOverride_WhiteTemp = true;
						PPV->Settings.WhiteTemp = 6500.0f;
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
						const TArray<FStaticMaterial>& StaticMats = SM->GetStaticMaterials();
						for (int32 SlotIdx = 0; SlotIdx < StaticMats.Num(); ++SlotIdx)
						{
							if (!SM->GetMaterial(SlotIdx))
							{
								const FString SlotName = StaticMats[SlotIdx].MaterialSlotName.ToString();
								const FString MatPath = FString::Printf(TEXT("/Game/ValorantContent/Materials/%s.%s"), *SlotName, *SlotName);
								UMaterialInterface* SlotMat = Cast<UMaterialInterface>(StaticLoadObject(UMaterialInterface::StaticClass(), nullptr, *MatPath));
								if (!SlotMat)
								{
									const FString PluginMatPath = FString::Printf(TEXT("/Uiana/Materials/%s.%s"), *SlotName, *SlotName);
									SlotMat = Cast<UMaterialInterface>(StaticLoadObject(UMaterialInterface::StaticClass(), nullptr, *PluginMatPath));
								}
								if (SlotMat)
								{
									SM->SetMaterial(SlotIdx, SlotMat);
									bMeshModified = true;
								}
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

		if (Config.bImportLights && GEditor)
		{
			if (UWorld* World = GEditor->GetEditorWorldContext().World())
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
						PPV->Settings.AutoExposureMethod = EAutoExposureMethod::AEM_Basic;
						PPV->Settings.bOverride_AutoExposureMinBrightness = true;
						PPV->Settings.AutoExposureMinBrightness = 0.5f;
						PPV->Settings.bOverride_AutoExposureMaxBrightness = true;
						PPV->Settings.AutoExposureMaxBrightness = 2.0f;
						PPV->Settings.bOverride_AutoExposureBias = true;
						PPV->Settings.AutoExposureBias = 0.0f;
						PPV->Settings.bOverride_WhiteTemp = true;
						PPV->Settings.WhiteTemp = 6500.0f;
						PPV->Settings.bOverride_ColorSaturation = true;
						PPV->Settings.ColorSaturation = FVector4(1.08f, 1.08f, 1.08f, 1.0f);
						PPV->Settings.bOverride_BloomIntensity = true;
						PPV->Settings.BloomIntensity = 0.05f;
						PPV->Settings.bOverride_IndirectLightingIntensity = true;
						PPV->Settings.IndirectLightingIntensity = 1.0f;
						PPV->Settings.bOverride_IndirectLightingColor = true;
						PPV->Settings.IndirectLightingColor = FLinearColor(1.0f, 0.956f, 0.88f, 1.0f);
					}
				}
			}
		}

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
	OutConfig.bImportMeshes = Settings.ImportMeshes;
	OutConfig.bFilterMeshes = Settings.bFilterMeshes;
	OutConfig.MeshFilterPrefixes = Settings.MeshFilterPrefixes;
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
