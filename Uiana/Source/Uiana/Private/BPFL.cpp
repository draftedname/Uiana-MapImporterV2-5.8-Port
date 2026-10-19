// Fill out your copyright notice in the Description page of Project Settings.


#include "BPFL.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMeshActor.h"
#include "GameFramework/Actor.h"
#include "VectorTypes.h"
#include "Engine/StaticMesh.h"
#include "AssetToolsModule.h"
#include "Engine/SCS_Node.h"
#include "json.hpp"
#include "KismetProceduralMeshLibrary.h"
#include "AutomatedAssetImportData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/World.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "EditorAssetLibrary.h"
#include "Factories/TextureFactory.h"
#include "StaticMeshDescription.h"
#include "Misc/ScopedSlowTask.h"
#include "Engine/RendererSettings.h"
#include "Readers/UEFModelReader.h"
#include "Factories/UEFModelFactory.h"
#include "StaticMeshComponentLODInfo.h"
#include "UObject/SavePackage.h"
#include "UianaDataSettings.h"
#include "UianaImporter.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"


UActorComponent* UBPFL::GetComponentByName(AActor* Actor, FName CompName)
{
	UStaticMeshComponent* Trolley = Cast<UStaticMeshComponent>(Actor->GetDefaultSubobjectByName(CompName));
	if (!Trolley)
	{
		return nullptr;
	}
	return Trolley;
}
void UBPFL::SetOverrideMaterial(AActor* Actor, FName CompName, TArray<UMaterialInterface*> MatOvr)
{
	UStaticMeshComponent* Trolley = Cast<UStaticMeshComponent>(Actor->GetDefaultSubobjectByName(CompName));
	if (!Trolley)
	{
		return;
	}
	Trolley->OverrideMaterials = MatOvr;
}
USCS_Node* UBPFL::CreateNode(UObject* Object, UClass* ClassToUse, FName CompName,UActorComponent*& ComponentReturn)
{
	IAssetTools& AssetTools = FModuleManager::GetModuleChecked<FAssetToolsModule>("AssetTools").Get();
	UBlueprint* Blueprint = Cast<UBlueprint>(Object);
	USCS_Node* Node = Blueprint->SimpleConstructionScript->CreateNode(ClassToUse, CompName);
	ComponentReturn = Node->ComponentTemplate;
	return Node;
}
UActorComponent* UBPFL::CreateBPComp(UObject* Object, UClass* ClassToUse, FName CompName,TArray<USCS_Node*> AttachNodes)
{
	IAssetTools& AssetTools = FModuleManager::GetModuleChecked<FAssetToolsModule>("AssetTools").Get();
	UBlueprint* Blueprint = Cast<UBlueprint>(Object);
	USCS_Node* Node = Blueprint->SimpleConstructionScript->CreateNode(ClassToUse, CompName);
	auto Component = Node->ComponentTemplate;
	Blueprint->SimpleConstructionScript->AddNode(Node);
	for (auto AttchNode : AttachNodes)
	{
		Node->AddChildNode(AttchNode);
	}
	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	return Component.Get();
}
void UBPFL::PaintSMVertices(UStaticMeshComponent* SMComp, TArray<FColor> VtxColorsArray, FString FileName)
{
	TArray<FColor> FinalColors;
	UStaticMesh* SM = SMComp->GetStaticMesh();
	if (SM)
	{
		SMComp->SetLODDataCount(1, SMComp->LODData.Num());
		FStaticMeshComponentLODInfo* LODInfo = &SMComp->LODData[0];
		FStaticMeshLODResources& LodResources = SM->GetRenderData()->LODResources[0];
		LODInfo->PaintedVertices.Empty();
		LODInfo->OverrideVertexColors = new FColorVertexBuffer();

		UEFModelReader Reader(FileName);
		if (!Reader.Read() || Reader.LODs.Num() == 0) return;
		const TArray<FVector3f>& ReaderVerts = Reader.LODs[0].Vertices;
		TArray<FVector3f> CurrentVerts = ReturnCurrentVerts(SM);
		if (VtxColorsArray.Num() != CurrentVerts.Num())
		{
			FinalColors = FixBrokenMesh(SM, FileName, VtxColorsArray, ReaderVerts);
		}
		else
		{
			auto Hashmap = MakeHashmap(ReaderVerts, VtxColorsArray);
			for (auto vt : CurrentVerts)
			{
				vt.Y = -vt.Y;
				auto idaa = FVector3f(vt);
				auto finder = Hashmap.Find(idaa);
				if (finder)
				{
					FinalColors.Add(*finder);
				}
			}
		}
		if (FinalColors.Num() != CurrentVerts.Num())
		{
			UE_LOG(LogTemp, Warning, TEXT("This one has wrong FinalColors %s"), *SM->GetName());
		}
		if (FinalColors.IsEmpty())
		{
			UE_LOG(LogTemp, Warning, TEXT("This one has no  FinalColors %s"), *SM->GetName());
			return;
		}
		LODInfo->OverrideVertexColors->InitFromColorArray(FinalColors);
		BeginInitResource(LODInfo->OverrideVertexColors);
	}
}

FColor UBPFL::ReturnFromHex(FString Beka)
{
	return FColor::FromHex(Beka);
}

TMap<FVector3f, FColor> UBPFL::MakeHashmap(TArray<FVector3f> arr1, TArray<FColor> TestVtx)
{
	TMap<FVector3f, FColor> FruitMap;
	FruitMap.Empty();
	int idx = 0;
	for (auto j : arr1)
	{
		FruitMap.Add(j, TestVtx[idx]);
		idx++;
	}
	return FruitMap;
}


TArray<FColor> UBPFL::FixBrokenMesh(UStaticMesh* SMesh, FString ReaderFile, TArray<FColor> BrokenVtxColorArray,TArray<FVector3f> ReaderVerts)
{
	TArray<FColor> LocalVtxColors;
	auto CurrentVerticesPosition = ReturnCurrentVerts(SMesh);
	int32  ida = 0;
	for (auto vtx : ReaderVerts)
	{
		ReaderVerts[ida].Y = -vtx.Y;
		ida++;
	}
	auto Hasher = MakeHashmap(ReaderVerts, BrokenVtxColorArray);
	for (auto vt : CurrentVerticesPosition)
	{
		auto finder = Hasher.Find(FVector3f(vt));
		LocalVtxColors.Add(*finder);
	}
	return LocalVtxColors;
}

TArray<FVector3f> UBPFL::ReturnCurrentVerts(UStaticMesh* Mesh)
{
	TArray<FVector3f> ReturnArray;
	if (Mesh->GetRenderData()->LODResources.Num() > 0)
	{
		FPositionVertexBuffer* VertexBuffer = &Mesh->GetRenderData()->LODResources[0].VertexBuffers.PositionVertexBuffer;
		if (VertexBuffer)
		{
			const int32 VertexCount = VertexBuffer->GetNumVertices();
			for (int32 Index = 0; Index < VertexCount; Index++)
			{
				const FVector3f Vertex = VertexBuffer->VertexPosition(Index);
				ReturnArray.Add(Vertex);
			}
		}
	}
	return ReturnArray;
}
void UBPFL::ChangeProjectSettings()
{
	URendererSettings* Settings = GetMutableDefault<URendererSettings>();
	Settings->DefaultLightUnits = ELightUnits::Unitless;
	Settings->DynamicGlobalIllumination = EDynamicGlobalIlluminationMethod::None;
	Settings->Reflections = EReflectionMethod::None;
	Settings->SaveConfig();
}
UActorComponent* UBPFL::GetComponent(AActor* Actor)
{
	Actor->SpriteScale;
	UActorComponent* RootComp = Actor->GetRootComponent();
	return Actor->GetRootComponent();
}
void UBPFL::ExecuteConsoleCommand(FString ConsoleCommand) {
	if (GEditor) {
		UWorld* World = GEditor->GetEditorWorldContext().World();
		if (World) {
			GEditor->Exec(World, *ConsoleCommand, *GLog);
		}
	}
}
UObject* UBPFL::SetMeshReference(FString MeshObjectName, FString MeshType)
{
	FString PathToGo = FString::Printf(TEXT("/Game/ValorantContent/%s/%s"), *MeshType, *MeshObjectName);
	PathToGo.RemoveFromEnd("'");
	PathToGo.RemoveFromStart("'");
	PathToGo = PathToGo.Replace(TEXT("StaticMesh'"), TEXT(""));
	auto Asset = UEditorAssetLibrary::LoadAsset(PathToGo);
	return Asset;
}

void UBPFL::ImportTextures(TArray<FString> AllTexturesPath)
{
	auto AutomatedData = NewObject<UAutomatedAssetImportData>();
	AutomatedData->bReplaceExisting = false;
	auto TextureFactory = NewObject<UTextureFactory>();
	TextureFactory->NoCompression = true;
	TextureFactory->AutomatedImportData = AutomatedData;
	FScopedSlowTask ImportTask(AllTexturesPath.Num(), FText::FromString("Importing Textures"));
	ImportTask.MakeDialog(true);
	auto ActorIdx = -1;
	for (auto tx : AllTexturesPath)
	{
		if (!FPaths::FileExists(tx))
		{
			continue;
		}
		ActorIdx++;
		FString TexGamePath, TexName;
		tx.Split(TEXT("\\"), &TexGamePath, &TexName, ESearchCase::IgnoreCase, ESearchDir::FromEnd);
		FString NewTxName = TexName.Replace(TEXT(".png"), TEXT(""));
		FString PathForTextures = FString::Printf(TEXT("/Game/ValorantContent/Textures/%s"), *NewTxName);
		const FString PackageFileName = FPackageName::LongPackageNameToFilename(PathForTextures, FPackageName::GetAssetPackageExtension());
		if (FPaths::FileExists(PackageFileName))
		{
			continue;
		}
		auto TexPackage = CreatePackage(*PathForTextures);
		TexPackage->FullyLoad();
		auto bCancelled = false;
		auto CreatedTexture = TextureFactory->FactoryCreateFile(UTexture2D::StaticClass(), TexPackage, FName(*NewTxName), RF_Public | RF_Standalone, tx, NULL, GWarn, bCancelled); 
		if (CreatedTexture == nullptr)
		{
			continue;
		}
		auto Tex = CastChecked<UTexture2D>(CreatedTexture);
		auto CompressionSetting = Tex->CompressionSettings;
		if (NewTxName.EndsWith("MRA") && CompressionSetting != TC_Masks)
		{
			Tex->SRGB = false;
			Tex->CompressionSettings = TC_Masks;
		}
		if (NewTxName.EndsWith("NM") && CompressionSetting != TC_Normalmap)
		{
			Tex->SRGB = false;
			Tex->CompressionSettings = TC_Normalmap;
		}
		if (NewTxName.EndsWith("DF") && CompressionSetting != TC_Normalmap)
		{
			Tex->SRGB = true;
			Tex->CompressionSettings = TC_Default;
		}
		ImportTask.DefaultMessage = FText::FromString(FString::Printf(TEXT("Importing Texture : %d of %d: %s"), ActorIdx + 1, AllTexturesPath.Num() + 1, *NewTxName));
		ImportTask.EnterProgressFrame();
		
		Tex->UpdateResource();
		FAssetRegistryModule::AssetCreated(Tex);
		FSavePackageArgs SaveArgs;
		UPackage::SavePackage(TexPackage, Tex, *PackageFileName, SaveArgs);
	}
}

void UBPFL::ImportMeshes(TArray<FString> AllMeshesPath, FString ObjectsPath)
{
	auto AutomatedData = NewObject<UAutomatedAssetImportData>();
	AutomatedData->bReplaceExisting = false;
	auto ModelFactory = NewObject<UEFModelFactory>();
	ModelFactory->AutomatedImportData = AutomatedData;
	FScopedSlowTask ImportTask(AllMeshesPath.Num(), FText::FromString("Importing Meshes"));
	ImportTask.MakeDialog(true); 
	int ActorIdx = -1;
	for (FString MPath : AllMeshesPath)
	{
		FString MeshGamePath, MeshName;
		MPath.Split(TEXT("\\"), &MeshGamePath, &MeshName, ESearchCase::IgnoreCase, ESearchDir::FromEnd);
		ActorIdx++;
		MeshName = MeshName.Replace(TEXT(".uemodel"), TEXT(""));
		FString PathForMeshes = FString::Printf(TEXT("/Game/ValorantContent/Meshes/%s"), *MeshName);
		const FString PackageFileName = FPackageName::LongPackageNameToFilename(PathForMeshes, FPackageName::GetAssetPackageExtension());

		UStaticMesh* StaticMesh = nullptr;
		if (FPaths::FileExists(PackageFileName))
		{
			StaticMesh = Cast<UStaticMesh>(StaticLoadObject(UStaticMesh::StaticClass(), nullptr, *PathForMeshes));
		}
		else
		{
			auto MeshPackage = CreatePackage(*PathForMeshes);
			MeshPackage->FullyLoad();
			auto bCancelled = false;
			auto CreatedMesh = ModelFactory->FactoryCreateFile(UStaticMesh::StaticClass(), MeshPackage, FName(*MeshName), RF_Public | RF_Standalone, MPath, NULL, GWarn, bCancelled);
			if (CreatedMesh != nullptr)
			{
				StaticMesh = Cast<UStaticMesh>(CreatedMesh);
				FAssetRegistryModule::AssetCreated(CreatedMesh);
				FSavePackageArgs SaveArgs;
				UPackage::SavePackage(MeshPackage, CreatedMesh, *PackageFileName, SaveArgs);
			}
		}

		if (StaticMesh)
		{
			bool bNeedsSave = false;
			FString JsonFile = FPaths::Combine(ObjectsPath, MeshName + TEXT(".json"));
			if (FPaths::FileExists(JsonFile))
			{
				FString JsonStr;
				if (FFileHelper::LoadFileToString(JsonStr, *JsonFile))
				{
					TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonStr);
					TArray<TSharedPtr<FJsonValue>> JsonArray;
					if (FJsonSerializer::Deserialize(Reader, JsonArray))
					{
						for (const auto& Val : JsonArray)
						{
							TSharedPtr<FJsonObject> Obj = Val->AsObject();
							if (Obj.IsValid() && Obj->GetStringField(TEXT("Type")) == TEXT("StaticMesh"))
							{
								const TSharedPtr<FJsonObject>* Props = nullptr;
								if (Obj->TryGetObjectField(TEXT("Properties"), Props) && Props && (*Props).IsValid())
								{
									int32 LMRes = 0;
									if ((*Props)->TryGetNumberField(TEXT("LightMapResolution"), LMRes) && LMRes > 0)
									{
										if (StaticMesh->GetLightMapResolution() != LMRes)
										{
											StaticMesh->SetLightMapResolution(LMRes);
											bNeedsSave = true;
										}
									}
									int32 LMCoord = 1;
									if ((*Props)->TryGetNumberField(TEXT("LightMapCoordinateIndex"), LMCoord))
									{
										if (StaticMesh->GetLightMapCoordinateIndex() != LMCoord)
										{
											StaticMesh->SetLightMapCoordinateIndex(LMCoord);
											bNeedsSave = true;
										}
									}

									const TArray<TSharedPtr<FJsonValue>>* StaticMaterials = nullptr;
									if ((*Props)->TryGetArrayField(TEXT("StaticMaterials"), StaticMaterials) && StaticMaterials)
									{
										const int32 NumMats = StaticMesh->GetStaticMaterials().Num();
										for (int32 SlotIdx = 0; SlotIdx < StaticMaterials->Num() && SlotIdx < NumMats; ++SlotIdx)
										{
											const TSharedPtr<FJsonObject> MatObj = (*StaticMaterials)[SlotIdx]->AsObject();
											if (!MatObj.IsValid()) continue;

											FString JsonSlotName = MatObj->GetStringField(TEXT("MaterialSlotName"));
											FString JsonImportedSlotName = MatObj->GetStringField(TEXT("ImportedMaterialSlotName"));

											if (!JsonSlotName.IsEmpty() && JsonSlotName != TEXT("WorldGridMaterial"))
											{
												if (StaticMesh->GetStaticMaterials()[SlotIdx].MaterialSlotName != FName(*JsonSlotName))
												{
													StaticMesh->GetStaticMaterials()[SlotIdx].MaterialSlotName = FName(*JsonSlotName);
													bNeedsSave = true;
												}
											}
											if (!JsonImportedSlotName.IsEmpty())
											{
												if (StaticMesh->GetStaticMaterials()[SlotIdx].ImportedMaterialSlotName != FName(*JsonImportedSlotName))
												{
													StaticMesh->GetStaticMaterials()[SlotIdx].ImportedMaterialSlotName = FName(*JsonImportedSlotName);
													bNeedsSave = true;
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
												else if (MeshName == TEXT("MidFloor_1"))
												{
													RawMatName = (SlotIdx == 0) ? TEXT("Brick_M6_HerringBoneBroken_MI") : TEXT("Brick_M6_HerringBoneAsphalt_MI");
												}
											}

											if (RawMatName == TEXT("Stone_M2_Steps_MI1"))
											{
												RawMatName = TEXT("Stone_M2_Steps_MI");
											}
											RawMatName = ResolveCleanMaterialVariation(RawMatName);
											if (!RawMatName.IsEmpty() && RawMatName != TEXT("WorldGridMaterial") && RawMatName != TEXT("DefaultMaterial"))
											{
												FString MatPath = FString::Printf(TEXT("/Game/ValorantContent/Materials/%s.%s"), *RawMatName, *RawMatName);
												UMaterialInterface* Mat = Cast<UMaterialInterface>(StaticLoadObject(UMaterialInterface::StaticClass(), nullptr, *MatPath));
												if (!Mat)
												{
													MatPath = FString::Printf(TEXT("/Uiana/Materials/%s.%s"), *RawMatName, *RawMatName);
													Mat = Cast<UMaterialInterface>(StaticLoadObject(UMaterialInterface::StaticClass(), nullptr, *MatPath));
												}
												if (Mat && (StaticMesh->GetMaterial(SlotIdx) != Mat || StaticMesh->GetStaticMaterials()[SlotIdx].MaterialInterface != Mat))
												{
													StaticMesh->SetMaterial(SlotIdx, Mat);
													StaticMesh->GetStaticMaterials()[SlotIdx].MaterialInterface = Mat;
													bNeedsSave = true;
												}
												if (Mat && (JsonSlotName.StartsWith(TEXT("lambert")) || JsonSlotName.Contains(TEXT("Default")) || JsonSlotName.StartsWith(TEXT("MaterialSlot_")) || JsonSlotName == TEXT("WorldGridMaterial")))
												{
													if (StaticMesh->GetStaticMaterials()[SlotIdx].MaterialSlotName != FName(*Mat->GetName()))
													{
														StaticMesh->GetStaticMaterials()[SlotIdx].MaterialSlotName = FName(*Mat->GetName());
														bNeedsSave = true;
													}
												}
											}
										}
									}
								}
							}
						}
					}
				}
			}
			else
			{
				if (StaticMesh->GetLightMapCoordinateIndex() == 0)
				{
					StaticMesh->SetLightMapCoordinateIndex(1);
					bNeedsSave = true;
				}
				if (StaticMesh->GetLightMapResolution() <= 4)
				{
					StaticMesh->SetLightMapResolution(64);
					bNeedsSave = true;
				}
			}

			if (bNeedsSave)
			{
				StaticMesh->MarkPackageDirty();
				UPackage* Pkg = StaticMesh->GetPackage();
				if (Pkg)
				{
					FSavePackageArgs SaveArgs;
					UPackage::SavePackage(Pkg, StaticMesh, *PackageFileName, SaveArgs);
				}
			}
		}

		ImportTask.DefaultMessage = FText::FromString(FString::Printf(TEXT("Importing Mesh : %d of %d: %s"), ActorIdx + 1, AllMeshesPath.Num() + 1, *MeshName));
		ImportTask.EnterProgressFrame();
	}
}

ECollisionTraceFlag UBPFL::GetTraceFlag(FString tflag)
{
	if (tflag == "CTF_UseDefault")
	{
		return CTF_UseDefault;
	}
	if (tflag == "CTF_UseSimpleAndComplex")
	{
		return CTF_UseSimpleAndComplex;
	}
	if (tflag == "CTF_UseSimpleAsComplex")
	{
		return CTF_UseSimpleAsComplex;
	}
	if (tflag == "CTF_UseComplexAsSimple")
	{
		return CTF_UseComplexAsSimple;
	}
	if (tflag == "CTF_MAX")
	{
		return CTF_MAX;
	}
	return CTF_UseSimpleAsComplex;
}

bool UBPFL::ImportValorantMap(FString MapName, FString PaksFolder, FString ExportFolder, bool bImportMeshes, bool bImportMaterials, bool bImportLights, bool bImportDecals)
{
	UUianaDataSettings* Settings = GetMutableDefault<UUianaDataSettings>();
	Settings->LoadConfig();
	if (!ExportFolder.IsEmpty())
	{
		Settings->ExportFolder.Path = ExportFolder;
	}
	if (!PaksFolder.IsEmpty())
	{
		Settings->PaksFolder.Path = PaksFolder;
	}
	Settings->ImportMeshes = bImportMeshes;
	Settings->ImportMaterials = bImportMaterials;
	Settings->ImportLights = bImportLights;
	Settings->ImportDecals = bImportDecals;
	if (!MapName.IsEmpty())
	{
		Settings->Map = MapName;
	}

	return FUianaImporter::Run(*Settings);
}

FString UBPFL::ResolveCleanMaterialVariation(const FString& InMatName)
{
	if (InMatName.IsEmpty())
	{
		return InMatName;
	}

	if (InMatName == TEXT("Stone_M2_Steps_MI1"))
	{
		return TEXT("Stone_M2_Steps_MI");
	}

	auto CheckExists = [](const FString& Candidate) -> bool
	{
		if (Candidate.IsEmpty())
		{
			return false;
		}
		const FString GamePath = FString::Printf(TEXT("/Game/ValorantContent/Materials/%s.%s"), *Candidate, *Candidate);
		if (FPackageName::DoesPackageExist(FString::Printf(TEXT("/Game/ValorantContent/Materials/%s"), *Candidate)) ||
			StaticLoadObject(UMaterialInterface::StaticClass(), nullptr, *GamePath) != nullptr)
		{
			return true;
		}

		const FString PluginPath = FString::Printf(TEXT("/Uiana/Materials/%s.%s"), *Candidate, *Candidate);
		if (FPackageName::DoesPackageExist(FString::Printf(TEXT("/Uiana/Materials/%s"), *Candidate)) ||
			StaticLoadObject(UMaterialInterface::StaticClass(), nullptr, *PluginPath) != nullptr)
		{
			return true;
		}
		return false;
	};

	// 1. StoneTile / SquareTiles blends (e.g. Stone_M4_StoneTileGrassBlend, Stone_M4_StoneTileMossBlend, Stone_M4_StoneTileMudBlend)
	if (InMatName.Contains(TEXT("StoneTile")) || InMatName.Contains(TEXT("SquareTile")) ||
		(InMatName.StartsWith(TEXT("Stone_")) && InMatName.Contains(TEXT("Tile")) && InMatName.Contains(TEXT("Blend"))))
	{
		if (InMatName.Contains(TEXT("Blend")) || InMatName.Contains(TEXT("Grass")) ||
			InMatName.Contains(TEXT("Moss")) || InMatName.Contains(TEXT("Mud")) || InMatName.Contains(TEXT("Dirt")))
		{
			if (CheckExists(TEXT("Stone_M1_SquareTiles_MI")))
			{
				return TEXT("Stone_M1_SquareTiles_MI");
			}
		}
	}

	// 2. HerringBone variations (e.g. Brick_M6_HerringBoneGrass_MI, Brick_M6_HerringBoneWater_MI, Brick_M6_HerringBoneAsphalt_MI)
	if (InMatName.Contains(TEXT("HerringBone")))
	{
		if (InMatName.Contains(TEXT("Grass")) || InMatName.Contains(TEXT("Water")) || InMatName.Contains(TEXT("Mud")))
		{
			if (CheckExists(TEXT("Brick_M6_HerringBoneBroken_MI")))
			{
				return TEXT("Brick_M6_HerringBoneBroken_MI");
			}
			if (CheckExists(TEXT("Brick_M6_HerringBone_MI")))
			{
				return TEXT("Brick_M6_HerringBone_MI");
			}
		}
	}

	// 3. Concrete blends (e.g. Concrete_M0_BareBlend_MI)
	if (InMatName.StartsWith(TEXT("Concrete_"), ESearchCase::IgnoreCase) && InMatName.Contains(TEXT("Blend")))
	{
		if (CheckExists(TEXT("Concrete_M0_MI")))
		{
			return TEXT("Concrete_M0_MI");
		}
		if (CheckExists(TEXT("Concrete_M0_Bare_MI")))
		{
			return TEXT("Concrete_M0_Bare_MI");
		}
	}

	// Do not strip nature materials that are standalone
	if (InMatName.StartsWith(TEXT("Grass_"), ESearchCase::IgnoreCase) ||
		InMatName.StartsWith(TEXT("Dirt_"), ESearchCase::IgnoreCase) ||
		InMatName.StartsWith(TEXT("Sand_"), ESearchCase::IgnoreCase))
	{
		return InMatName;
	}

	static const TArray<FString> VariationWords = {
		TEXT("_DarkBlend"), TEXT("DarkBlend_"), TEXT("DarkBlend"),
		TEXT("_DirtBlend"), TEXT("DirtBlend_"), TEXT("DirtBlend"),
		TEXT("_BareBlend"), TEXT("BareBlend_"), TEXT("BareBlend"),
		TEXT("_GrassBlend"), TEXT("GrassBlend_"), TEXT("GrassBlend"),
		TEXT("_MossBlend"), TEXT("MossBlend_"), TEXT("MossBlend"),
		TEXT("_MudBlend"), TEXT("MudBlend_"), TEXT("MudBlend"),
		TEXT("_Blend"), TEXT("Blend_"), TEXT("Blend"),
		TEXT("_Grass"), TEXT("Grass_"), TEXT("Grass"),
		TEXT("_Dirt"), TEXT("Dirt_"), TEXT("Dirt"),
		TEXT("_Sand"), TEXT("Sand_"), TEXT("Sand"),
		TEXT("_Moss"), TEXT("Moss_"), TEXT("Moss"),
		TEXT("_Mud"), TEXT("Mud_"), TEXT("Mud"),
		TEXT("_Water"), TEXT("Water_"), TEXT("Water"),
		TEXT("_Gravel"), TEXT("Gravel_"), TEXT("Gravel")
	};

	for (const FString& Word : VariationWords)
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

			if (CheckExists(Candidate))
			{
				return Candidate;
			}

			if (!Candidate.EndsWith(TEXT("_MI")) && InMatName.EndsWith(TEXT("_MI")))
			{
				const FString CandidateWithMI = Candidate + TEXT("_MI");
				if (CheckExists(CandidateWithMI))
				{
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
					if (CheckExists(CandM1))
					{
						return CandM1;
					}
				}
			}
		}
	}

	return InMatName;
}


