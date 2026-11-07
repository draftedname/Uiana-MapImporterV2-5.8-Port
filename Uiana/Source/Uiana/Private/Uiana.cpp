// Copyright Epic Games, Inc. All Rights Reserved.

#include "Uiana.h"
#include "UianaStyle.h"
#include "Engine/World.h"
#include "UianaCommands.h"
#include "Misc/MessageDialog.h"
#include "ToolMenus.h"
#include "LevelEditor.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "ToolMenus.h"
#include "PropertyEditorModule.h"
#include "UianaDataSettings.h"
#include "UianaImporter.h"
#include "Misc/Paths.h"
#include "ISettingsModule.h"
static const FName UianaTabName("Uiana");

#define LOCTEXT_NAMESPACE "FUianaModule"

void FUianaModule::StartupModule()
{
	// This code will execute after your module is loaded into memory; the exact timing is specified in the .uplugin file per-module
	
	FUianaStyle::Initialize();
	FUianaStyle::ReloadTextures();
	RegisterSettings();
	FUianaCommands::Register();
	
	PluginCommands = MakeShareable(new FUICommandList);

	PluginCommands->MapAction(
		FUianaCommands::Get().OpenPluginWindow,
		FExecuteAction::CreateRaw(this, &FUianaModule::PluginButtonClicked),
		FCanExecuteAction());

	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FUianaModule::RegisterMenus));
	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(UianaTabName, FOnSpawnTab::CreateRaw(this, &FUianaModule::OnSpawnPluginTab))
		.SetDisplayName(LOCTEXT("FeditortestTabTitle", "Valorant Map Importer"))
		.SetMenuType(ETabSpawnerMenuType::Hidden);
}
void FUianaModule::RegisterSettings()
{
	if (ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
	{
		SettingsModule->RegisterSettings("Project", "Plugins", "Uiana",
			LOCTEXT("RuntimeSettingsName", "Uiana"),
			LOCTEXT("RuntimeSettingsDescription", "Configure Uiana settings"),
			GetMutableDefault<UUianaDataSettings>()
		);
	}
}
void FUianaModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.

	UToolMenus::UnRegisterStartupCallback(this);

	UToolMenus::UnregisterOwner(this);

	FUianaStyle::Shutdown();

	FUianaCommands::Unregister();
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(UianaTabName);
}




void FUianaModule::PluginButtonClicked()
{
	FGlobalTabmanager::Get()->TryInvokeTab(UianaTabName);
}

FReply FUianaModule::ExecuteFunction()
{
	if (Stun)
	{
		Stun->SaveCurrentPresetForMap(Stun->Map);
		Stun->SaveConfig();
		FUianaImporter::Run(*Stun);
	}
	return FReply::Handled();
}


void FUianaModule::RegisterMenus()
{
	// Owner will be used for cleanup in call to UToolMenus::UnregisterOwner
	FToolMenuOwnerScoped OwnerScoped(this);

	{
		UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Window");
		{
			FToolMenuSection& Section = Menu->FindOrAddSection("WindowLayout");
			Section.AddMenuEntryWithCommandList(FUianaCommands::Get().OpenPluginWindow, PluginCommands);
		}
	}

	{
		UToolMenu* ToolbarMenu = UToolMenus::Get()->ExtendMenu("LevelEditor.LevelEditorToolBar.PlayToolBar");
		{
			FToolMenuSection& Section = ToolbarMenu->FindOrAddSection("PluginTools");
			{
				FToolMenuEntry& Entry = Section.AddEntry(FToolMenuEntry::InitToolBarButton(FUianaCommands::Get().OpenPluginWindow));
				Entry.SetCommandList(PluginCommands);
			}
		}
	}
}

TSharedRef<SWidget> FUianaModule::MakeDataManagementSettingsDetailsWidget() const
{
	UUianaDataSettings* Settings = GetMutableDefault<UUianaDataSettings>();
	Settings->LoadConfig();
	Settings->InitializeDefaultPresets();
	Settings->LoadPresetForMap(Settings->Map);
	Stun = Settings;
	FPropertyEditorModule& PropertyEditorModule = FModuleManager::Get().LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	FDetailsViewArgs DetailsViewArgs;
	DetailsViewArgs.bUpdatesFromSelection = false;
	DetailsViewArgs.bLockable = false;
	DetailsViewArgs.bAllowSearch = false;
	DetailsViewArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
	DetailsViewArgs.bHideSelectionTip = true;
	DetailsViewArgs.bShowScrollBar = false;
	TSharedRef<IDetailsView> Details = PropertyEditorModule.CreateDetailView(DetailsViewArgs);
	Details->SetObject(Settings);
	Details->SetEnabled(true);
	Details->OnFinishedChangingProperties().AddLambda([DetailsWeak = TWeakPtr<IDetailsView>(Details)](const FPropertyChangedEvent& Event) {
		if (UUianaDataSettings* SettingsObj = GetMutableDefault<UUianaDataSettings>())
		{
			const FName PropName = (Event.Property != nullptr) ? Event.Property->GetFName() : NAME_None;
			const FName MemberPropName = (Event.MemberProperty != nullptr) ? Event.MemberProperty->GetFName() : NAME_None;

			if (PropName == GET_MEMBER_NAME_CHECKED(UUianaDataSettings, Map) || MemberPropName == GET_MEMBER_NAME_CHECKED(UUianaDataSettings, Map))
			{
				SettingsObj->LoadPresetForMap(SettingsObj->Map);
				SettingsObj->SaveConfig();
				if (DetailsWeak.IsValid())
				{
					DetailsWeak.Pin()->ForceRefresh();
				}
			}
			else if (PropName == GET_MEMBER_NAME_CHECKED(UUianaDataSettings, MapPresets) || MemberPropName == GET_MEMBER_NAME_CHECKED(UUianaDataSettings, MapPresets))
			{
				SettingsObj->LoadPresetForMap(SettingsObj->Map);
				SettingsObj->SaveConfig();
			}
			else
			{
				SettingsObj->SaveCurrentPresetForMap(SettingsObj->Map);
				SettingsObj->SaveConfig();
			}
		}
	});

	return Details;
}

TSharedRef<class SDockTab> FUianaModule::OnSpawnPluginTab(const FSpawnTabArgs& SpawnTabArgs)
{
	static const FSlateColorBrush BackgroundBrush(FColor(5, 5, 5));
	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		[
			SNew(SBorder)
			.BorderImage(&BackgroundBrush)
			[
				SNew(SVerticalBox)

				// Scrollable Settings Area
				+ SVerticalBox::Slot()
				.FillHeight(1.0f)
				.Padding(8.f, 5.f, 8.f, 4.f)
				[
					SNew(SBorder)
					.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.DarkGroupBorder"))
					[
						SNew(SScrollBox)
						.Orientation(Orient_Vertical)
						+ SScrollBox::Slot()
						.Padding(4.f)
						[
							MakeDataManagementSettingsDetailsWidget()
						]
					]
				]

				// Action Buttons pinned at bottom
				+ SVerticalBox::Slot()
				.AutoHeight()
				.VAlign(VAlign_Bottom)
				.HAlign(HAlign_Right)
				.Padding(8.f, 4.f, 8.f, 8.f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(0.f, 0.f, 8.f, 0.f)
					[
						SNew(SButton)
						.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
						.ForegroundColor(FSlateColor::UseForeground())
						.OnClicked_Lambda([this]() -> FReply {
							if (Stun)
							{
								Stun->RefreshMaps();
							}
							return FReply::Handled();
						})
						[
							SNew(STextBlock)
							.Justification(ETextJustify::Center)
							.TextStyle(FAppStyle::Get(), "NormalText")
							.Text(FText::FromString(TEXT("Scan / Refresh Maps")))
						]
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(SButton)
						.ButtonStyle(FAppStyle::Get(), "FlatButton.Success")
						.ForegroundColor(FSlateColor::UseForeground())
						.OnClicked(FOnClicked::CreateRaw(this, &FUianaModule::ExecuteFunction))
						[
							SNew(STextBlock)
							.Justification(ETextJustify::Center)
							.TextStyle(FAppStyle::Get(), "NormalText.Important")
							.Text(NSLOCTEXT("LevelSnapshots", "NotificationFormatText_CreationForm_CreateSnapshotButton", "Generate Map"))
						]
					]
				]
			]
		];
}



#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FUianaModule, Uiana)