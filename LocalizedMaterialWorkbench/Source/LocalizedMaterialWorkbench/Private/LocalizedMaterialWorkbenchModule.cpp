#include "CoreMinimal.h"

#include "AssetRegistry/AssetData.h"
#include "Components/MeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "ContentBrowserModule.h"
#include "Editor.h"
#include "Editor/EditorEngine.h"
#include "Engine/Selection.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/IConsoleManager.h"
#include "IContentBrowserSingleton.h"
#include "MeshDescription.h"
#include "Modules/ModuleManager.h"
#include "StaticMeshAttributes.h"
#include "Styling/CoreStyle.h"
#include "ToolMenu.h"
#include "ToolMenuEntry.h"
#include "ToolMenuSection.h"
#include "ToolMenus.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "FLocalizedMaterialWorkbenchModule"

namespace LocalizedMaterialWorkbench
{

enum class EWorkflowStep : uint8
{
    Inspect = 0,
    Bake = 1,
    Paint = 2,
    Export = 3
};

struct FSelectionSnapshot
{
    TWeakObjectPtr<AActor> Actor;
    TWeakObjectPtr<UMeshComponent> Component;
    TWeakObjectPtr<UObject> MeshAsset;
    int32 MaterialSlotCount = 0;
    int32 UVChannelCount = 0;
    bool bSkeletalMesh = false;
    bool bFromLevelSelection = false;

    bool IsValid() const
    {
        return MeshAsset.IsValid();
    }

    FString GetMeshTypeName() const
    {
        if (!IsValid())
        {
            return TEXT("No Mesh");
        }
        return bSkeletalMesh ? TEXT("Skeletal Mesh") : TEXT("Static Mesh");
    }
};

static UObject* GetMeshAsset(const UMeshComponent* Component)
{
    if (const UStaticMeshComponent* StaticComponent =
        Cast<UStaticMeshComponent>(Component))
    {
        return StaticComponent->GetStaticMesh();
    }
    if (const USkeletalMeshComponent* SkeletalComponent =
        Cast<USkeletalMeshComponent>(Component))
    {
        return SkeletalComponent->GetSkeletalMeshAsset();
    }
    return nullptr;
}

static int32 GetUVChannelCount(UObject* MeshAsset)
{
    const FMeshDescription* MeshDescription = nullptr;
    if (UStaticMesh* StaticMesh = Cast<UStaticMesh>(MeshAsset))
    {
        MeshDescription = StaticMesh->GetMeshDescription(0);
    }
    else if (USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(MeshAsset))
    {
        MeshDescription = SkeletalMesh->GetMeshDescription(0);
    }

    if (!MeshDescription)
    {
        return 0;
    }

    FStaticMeshConstAttributes Attributes(*MeshDescription);
    return Attributes.GetVertexInstanceUVs().GetNumChannels();
}

static FSelectionSnapshot ResolveSelection()
{
    FSelectionSnapshot Snapshot;

    if (GEditor)
    {
        USelection* SelectedActors = GEditor->GetSelectedActors();
        if (SelectedActors && SelectedActors->Num() == 1)
        {
            AActor* Actor = Cast<AActor>(SelectedActors->GetSelectedObject(0));
            if (Actor)
            {
                TInlineComponentArray<UMeshComponent*> Components(Actor);
                for (UMeshComponent* Component : Components)
                {
                    UObject* MeshAsset = GetMeshAsset(Component);
                    if (!Component || !MeshAsset)
                    {
                        continue;
                    }

                    Snapshot.Actor = Actor;
                    Snapshot.Component = Component;
                    Snapshot.MeshAsset = MeshAsset;
                    Snapshot.MaterialSlotCount = Component->GetNumMaterials();
                    Snapshot.UVChannelCount = GetUVChannelCount(MeshAsset);
                    Snapshot.bSkeletalMesh = MeshAsset->IsA<USkeletalMesh>();
                    Snapshot.bFromLevelSelection = true;
                    return Snapshot;
                }
            }
        }
    }

    if (FModuleManager::Get().ModuleExists(TEXT("ContentBrowser")))
    {
        FContentBrowserModule& ContentBrowserModule =
            FModuleManager::LoadModuleChecked<FContentBrowserModule>(
                TEXT("ContentBrowser"));
        TArray<FAssetData> SelectedAssets;
        ContentBrowserModule.Get().GetSelectedAssets(SelectedAssets);
        for (const FAssetData& AssetData : SelectedAssets)
        {
            UObject* MeshAsset = AssetData.GetAsset();
            UStaticMesh* StaticMesh = Cast<UStaticMesh>(MeshAsset);
            USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(MeshAsset);
            if (!StaticMesh && !SkeletalMesh)
            {
                continue;
            }

            Snapshot.MeshAsset = MeshAsset;
            Snapshot.MaterialSlotCount = StaticMesh
                ? StaticMesh->GetStaticMaterials().Num()
                : SkeletalMesh->GetMaterials().Num();
            Snapshot.UVChannelCount = GetUVChannelCount(MeshAsset);
            Snapshot.bSkeletalMesh = SkeletalMesh != nullptr;
            Snapshot.bFromLevelSelection = false;
            return Snapshot;
        }
    }

    return Snapshot;
}

static FString MakeSelectionSummary(const FSelectionSnapshot& Snapshot)
{
    if (!Snapshot.IsValid())
    {
        return TEXT("未检测到模型。请在关卡中选择一个模型 Actor，或在内容浏览器中选择 Static/Skeletal Mesh。");
    }

    const FString Source = Snapshot.bFromLevelSelection
        ? TEXT("关卡 Actor")
        : TEXT("内容浏览器资产");
    return FString::Printf(
        TEXT("%s | %s | 材质槽 %d | UV 通道 %d | 来源：%s"),
        *Snapshot.MeshAsset->GetName(),
        *Snapshot.GetMeshTypeName(),
        Snapshot.MaterialSlotCount,
        Snapshot.UVChannelCount,
        *Source);
}

static FString MakeUVStrategySummary(const FSelectionSnapshot& Snapshot)
{
    if (!Snapshot.IsValid())
    {
        return TEXT("等待模型选择。");
    }
    if (Snapshot.UVChannelCount >= 2)
    {
        return TEXT("检测到 UV1。Glow Painter 将继续审计重叠；安全时直接使用，不安全时才生成派生 Unique Mask UV。");
    }
    if (Snapshot.UVChannelCount == 1)
    {
        return TEXT("模型只有 UV0。烘焙可继续；绘制阶段建议由 Glow Painter 非破坏式生成 Unique Mask UV。");
    }
    return TEXT("LOD0 缺少可读取的 MeshDescription/UV；请先保存或重新导入源模型。");
}

static bool HasModule(const FName ModuleName)
{
    return FModuleManager::Get().ModuleExists(*ModuleName.ToString());
}

static bool HasToolMenuEntry(const FName SectionName, const FName EntryName)
{
    UToolMenus* ToolMenus = UToolMenus::TryGet();
    if (!ToolMenus)
    {
        return false;
    }

    UToolMenu* Menu = ToolMenus->FindMenu(TEXT("LevelEditor.MainMenu.Tools"));
    FToolMenuSection* Section = Menu ? Menu->FindSection(SectionName) : nullptr;
    return Section && Section->FindEntry(EntryName) != nullptr;
}

static bool WidgetTreeContainsText(
    const TSharedRef<SWidget>& Widget,
    const FString& TargetText)
{
    if (Widget->GetTypeAsString() == TEXT("STextBlock"))
    {
        const TSharedRef<STextBlock> TextBlock =
            StaticCastSharedRef<STextBlock>(Widget);
        if (TextBlock->GetText().ToString().Contains(
            TargetText,
            ESearchCase::IgnoreCase))
        {
            return true;
        }
    }

    FChildren* Children = Widget->GetChildren();
    for (int32 Index = 0; Children && Index < Children->Num(); ++Index)
    {
        if (WidgetTreeContainsText(Children->GetChildAt(Index), TargetText))
        {
            return true;
        }
    }
    return false;
}

static bool SimulateMenuButtonClick(
    const TSharedRef<SWidget>& Widget,
    const FString& TargetText)
{
    const FString WidgetType = Widget->GetTypeAsString();
    if ((WidgetType == TEXT("SButton") ||
         WidgetType == TEXT("SMenuEntryButton")) &&
        WidgetTreeContainsText(Widget, TargetText))
    {
        StaticCastSharedRef<SButton>(Widget)->SimulateClick();
        return true;
    }

    FChildren* Children = Widget->GetChildren();
    for (int32 Index = 0; Children && Index < Children->Num(); ++Index)
    {
        if (SimulateMenuButtonClick(Children->GetChildAt(Index), TargetText))
        {
            return true;
        }
    }
    return false;
}

static bool InvokeExistingTool(
    const FName ModuleName,
    const FName SectionName,
    const FName EntryName,
    FString& OutError)
{
    OutError.Reset();
    if (!HasModule(ModuleName))
    {
        OutError = FString::Printf(
            TEXT("模块 %s 未安装或未被当前项目发现。"),
            *ModuleName.ToString());
        return false;
    }

    if (!FModuleManager::Get().LoadModule(ModuleName))
    {
        OutError = FString::Printf(
            TEXT("模块 %s 加载失败。"),
            *ModuleName.ToString());
        return false;
    }

    UToolMenus* ToolMenus = UToolMenus::TryGet();
    if (!ToolMenus)
    {
        OutError = TEXT("ToolMenus 尚未初始化。");
        return false;
    }

    ToolMenus->RefreshAllWidgets();
    UToolMenu* Menu = ToolMenus->FindMenu(TEXT("LevelEditor.MainMenu.Tools"));
    if (!Menu)
    {
        OutError = TEXT("无法读取 Unreal Editor 的 Tools 菜单。");
        return false;
    }

    FToolMenuSection* Section = Menu->FindSection(SectionName);
    FToolMenuEntry* Entry = Section ? Section->FindEntry(EntryName) : nullptr;
    if (!Entry)
    {
        OutError = FString::Printf(
            TEXT("找不到工具入口 %s。请确认原插件已经启用，然后重启编辑器。"),
            *EntryName.ToString());
        return false;
    }

    FString TargetLabel;
    if (EntryName == TEXT("LocalizedMapBaker_OpenPanel"))
    {
        TargetLabel = TEXT("Localized Map Baker");
    }
    else if (EntryName == TEXT("LocalizedGlowPainter_OpenPanel"))
    {
        TargetLabel = TEXT("Localized Glow Painter");
    }
    else
    {
        OutError = FString::Printf(
            TEXT("Tool entry %s does not have a recognized menu label."),
            *EntryName.ToString());
        return false;
    }

    const TSharedRef<SWidget> GeneratedMenu =
        ToolMenus->GenerateWidget(
            TEXT("LevelEditor.MainMenu.Tools"), Menu->Context);
    const bool bLabelPresent =
        WidgetTreeContainsText(GeneratedMenu, TargetLabel);
    const bool bClicked =
        SimulateMenuButtonClick(GeneratedMenu, TargetLabel);
    UE_LOG(
        LogTemp,
        Display,
        TEXT("[LocalizedMaterialWorkbench] Menu probe target=%s root=%s label=%d clicked=%d"),
        *TargetLabel,
        *GeneratedMenu->GetTypeAsString(),
        bLabelPresent ? 1 : 0,
        bClicked ? 1 : 0);
    if (!bClicked)
    {
        OutError = FString::Printf(
            TEXT("工具入口 %s 存在，但执行动作失败。"),
            *EntryName.ToString());
        return false;
    }
    return true;
}

static TSharedPtr<SWindow> FindTopLevelWindowByTitle(const FString& WindowTitle)
{
    if (!FSlateApplication::IsInitialized())
    {
        return nullptr;
    }

    for (const TSharedRef<SWindow>& Window :
        FSlateApplication::Get().GetTopLevelWindows())
    {
        if (Window->GetTitle().ToString().Equals(
            WindowTitle, ESearchCase::CaseSensitive))
        {
            return Window;
        }
    }
    return nullptr;
}

} // namespace LocalizedMaterialWorkbench

class FLocalizedMaterialWorkbenchModule final : public IModuleInterface
{
public:
    virtual void StartupModule() override
    {
        UToolMenus::RegisterStartupCallback(
            FSimpleMulticastDelegate::FDelegate::CreateRaw(
                this,
                &FLocalizedMaterialWorkbenchModule::RegisterMenus));

        SelfTestCommand = IConsoleManager::Get().RegisterConsoleCommand(
            TEXT("LocalizedMaterialWorkbench.SelfTest"),
            TEXT("Validate the workbench, source tools, menu actions, and current selection."),
            FConsoleCommandDelegate::CreateRaw(
                this,
                &FLocalizedMaterialWorkbenchModule::RunSelfTest),
            ECVF_Default);
        OpenCommand = IConsoleManager::Get().RegisterConsoleCommand(
            TEXT("LocalizedMaterialWorkbench.Open"),
            TEXT("Open the Localized Material Workbench window."),
            FConsoleCommandDelegate::CreateRaw(
                this,
                &FLocalizedMaterialWorkbenchModule::OpenWorkbench),
            ECVF_Default);
    }

    virtual void ShutdownModule() override
    {
        CloseEmbeddedToolWindows();
        if (!IsEngineExitRequested())
        {
            if (TSharedPtr<SWindow> Window = WorkbenchWindow.Pin())
            {
                Window->RequestDestroyWindow();
            }
        }
        WorkbenchWindow.Reset();
        WorkflowSwitcher.Reset();
        SelectedAssetText.Reset();
        UVStrategyText.Reset();
        CompatibilityText.Reset();
        StatusText.Reset();
        MapBakerHostBox.Reset();
        GlowPainterHostBox.Reset();
        MapBakerSourceWindow.Reset();
        GlowPainterSourceWindow.Reset();

        if (OpenCommand)
        {
            IConsoleManager::Get().UnregisterConsoleObject(OpenCommand, false);
            OpenCommand = nullptr;
        }

        if (SelfTestCommand)
        {
            IConsoleManager::Get().UnregisterConsoleObject(SelfTestCommand, false);
            SelfTestCommand = nullptr;
        }

        if (UToolMenus::IsToolMenuUIEnabled())
        {
            UToolMenus::UnregisterOwner(this);
        }
    }

private:
    inline static const FName MapBakerModuleName = TEXT("LocalizedMapBaker");
    inline static const FName GlowPainterModuleName = TEXT("LocalizedGlowPainter");
    inline static const FName MapBakerEntryName = TEXT("LocalizedMapBaker_OpenPanel");
    inline static const FName GlowPainterEntryName = TEXT("LocalizedGlowPainter_OpenPanel");
    inline static const FString MapBakerWindowTitle = TEXT("Localized Map Baker");
    inline static const FString GlowPainterWindowTitle = TEXT("Localized Glow Painter");

    TWeakPtr<SWindow> WorkbenchWindow;
    TSharedPtr<SWidgetSwitcher> WorkflowSwitcher;
    TSharedPtr<STextBlock> SelectedAssetText;
    TSharedPtr<STextBlock> UVStrategyText;
    TSharedPtr<STextBlock> CompatibilityText;
    TSharedPtr<STextBlock> StatusText;
    LocalizedMaterialWorkbench::FSelectionSnapshot Selection;
    TSharedPtr<SBox> MapBakerHostBox;
    TSharedPtr<SBox> GlowPainterHostBox;
    TSharedPtr<SWindow> MapBakerSourceWindow;
    TSharedPtr<SWindow> GlowPainterSourceWindow;
    IConsoleObject* OpenCommand = nullptr;
    IConsoleObject* SelfTestCommand = nullptr;

    void RegisterMenus()
    {
        FToolMenuOwnerScoped OwnerScoped(this);
        UToolMenu* Menu =
            UToolMenus::Get()->ExtendMenu(TEXT("LevelEditor.MainMenu.Tools"));
        FToolMenuSection& Section =
            Menu->FindOrAddSection(TEXT("LocalizedMaterialWorkbench"));
        Section.AddEntry(FToolMenuEntry::InitMenuEntry(
            TEXT("LocalizedMaterialWorkbench_OpenPanel"),
            LOCTEXT("OpenWorkbenchLabel", "Localized Material Workbench..."),
            LOCTEXT(
                "OpenWorkbenchTooltip",
                "Coordinate Localized Map Baker and Localized Glow Painter without changing either plugin."),
            FSlateIcon(),
            FUIAction(FExecuteAction::CreateRaw(
                this,
                &FLocalizedMaterialWorkbenchModule::OpenWorkbench))));
    }

    void SetStep(LocalizedMaterialWorkbench::EWorkflowStep Step)
    {
        if (WorkflowSwitcher.IsValid())
        {
            WorkflowSwitcher->SetActiveWidgetIndex(static_cast<int32>(Step));
        }
    }

    void SetStatus(const FString& Message)
    {
        if (StatusText.IsValid())
        {
            StatusText->SetText(FText::FromString(Message));
        }
    }

    FString GetCompatibilitySummary() const
    {
        const bool bHasBaker =
            LocalizedMaterialWorkbench::HasModule(MapBakerModuleName);
        const bool bHasPainter =
            LocalizedMaterialWorkbench::HasModule(GlowPainterModuleName);
        const bool bHasBakerEntry =
            LocalizedMaterialWorkbench::HasToolMenuEntry(MapBakerModuleName, MapBakerEntryName);
        const bool bHasPainterEntry =
            LocalizedMaterialWorkbench::HasToolMenuEntry(GlowPainterModuleName, GlowPainterEntryName);

        return FString::Printf(
            TEXT("Map Baker: %s%s    |    Glow Painter: %s%s"),
            bHasBaker ? TEXT("已安装") : TEXT("缺失"),
            bHasBaker && !bHasBakerEntry ? TEXT("（等待菜单注册）") : TEXT(""),
            bHasPainter ? TEXT("已安装") : TEXT("缺失"),
            bHasPainter && !bHasPainterEntry ? TEXT("（等待菜单注册）") : TEXT(""));
    }

    void RefreshSelection()
    {
        Selection = LocalizedMaterialWorkbench::ResolveSelection();
        if (SelectedAssetText.IsValid())
        {
            SelectedAssetText->SetText(FText::FromString(
                LocalizedMaterialWorkbench::MakeSelectionSummary(Selection)));
        }
        if (UVStrategyText.IsValid())
        {
            UVStrategyText->SetText(FText::FromString(
                LocalizedMaterialWorkbench::MakeUVStrategySummary(Selection)));
        }
        if (CompatibilityText.IsValid())
        {
            CompatibilityText->SetText(FText::FromString(
                GetCompatibilitySummary()));
        }
        SetStatus(Selection.IsValid()
            ? TEXT("分析完成：选择信息已共享；专业 UV 审计将在对应原插件中继续执行。")
            : TEXT("等待选择：请在关卡或内容浏览器中选择一个模型。"));
    }

    void SyncMeshAssetToContentBrowser()
    {
        UObject* MeshAsset = Selection.MeshAsset.Get();
        if (!MeshAsset || !GEditor)
        {
            return;
        }
        TArray<UObject*> AssetsToSync;
        AssetsToSync.Add(MeshAsset);
        GEditor->SyncBrowserToObjects(AssetsToSync, false);
    }

    FReply OpenMapBaker()
    {
        RefreshSelection();
        if (!Selection.IsValid())
        {
            SetStatus(TEXT("无法打开 Baker：请先选择一个 Static/Skeletal Mesh。"));
            return FReply::Handled();
        }

        SyncMeshAssetToContentBrowser();
        FString Error;
        if (!LocalizedMaterialWorkbench::InvokeExistingTool(
            MapBakerModuleName,
            MapBakerModuleName,
            MapBakerEntryName,
            Error))
        {
            SetStatus(FString::Printf(TEXT("Map Baker 启动失败：%s"), *Error));
            return FReply::Handled();
        }

        SetStatus(TEXT("已把当前网格体同步到内容浏览器，并打开原生 Localized Map Baker。"));
        return FReply::Handled();
    }

    FReply OpenGlowPainter()
    {
        RefreshSelection();
        if (!Selection.IsValid())
        {
            SetStatus(TEXT("无法打开 Painter：请先选择一个模型 Actor。"));
            return FReply::Handled();
        }
        if (!Selection.bFromLevelSelection)
        {
            SetStatus(TEXT("Glow Painter 需要关卡中的模型 Actor；当前只有内容浏览器资产选择。"));
            return FReply::Handled();
        }

        FString Error;
        if (!LocalizedMaterialWorkbench::InvokeExistingTool(
            GlowPainterModuleName,
            GlowPainterModuleName,
            GlowPainterEntryName,
            Error))
        {
            SetStatus(FString::Printf(TEXT("Glow Painter 启动失败：%s"), *Error));
            return FReply::Handled();
        }

        SetStatus(TEXT("已保留当前关卡 Actor 选择，并打开原生 Localized Glow Painter。"));
        return FReply::Handled();
    }

    TSharedRef<SWidget> MakeEmbeddedMessage(const FText& Message) const
    {
        return SNew(SBorder)
            .Padding(18.0f)
            [
                SNew(STextBlock)
                .Text(Message)
                .AutoWrapText(true)
            ];
    }

    bool MountExistingToolPanel(
        const FName ModuleName,
        const FName SectionName,
        const FName EntryName,
        const FString& WindowTitle,
        const TSharedPtr<SBox>& TargetBox,
        TSharedPtr<SWindow>& SourceWindow,
        FString& OutError)
    {
        OutError.Reset();
        if (!TargetBox.IsValid())
        {
            OutError = TEXT("Workbench embedded host container is not ready.");
            return false;
        }
        if (SourceWindow.IsValid())
        {
            return true;
        }

        if (!LocalizedMaterialWorkbench::InvokeExistingTool(
            ModuleName, SectionName, EntryName, OutError))
        {
            return false;
        }

        TSharedPtr<SWindow> ToolWindow =
            LocalizedMaterialWorkbench::FindTopLevelWindowByTitle(WindowTitle);
        if (!ToolWindow.IsValid())
        {
            OutError = FString::Printf(
                TEXT("Tool action executed, but window \"%s\" was not found."),
                *WindowTitle);
            return false;
        }

        const TSharedRef<SWidget> ToolContent = ToolWindow->GetContent();
        ToolWindow->SetContent(SNullWidget::NullWidget);
        ToolWindow->HideWindow();
        TargetBox->SetContent(ToolContent);
        SourceWindow = ToolWindow;

        UE_LOG(
            LogTemp,
            Display,
            TEXT("[LocalizedMaterialWorkbench] Embedded %s into the workbench."),
            *WindowTitle);
        return true;
    }

    void CloseEmbeddedToolWindows()
    {
        // During editor shutdown Slate owns the top-level-window destruction
        // order. Requesting destruction again from the workbench's close
        // callback can address windows that Slate has already removed and
        // corrupt its top-level window array. Only release our references in
        // that path; the source plugins and Slate finish their own shutdown.
        if (IsEngineExitRequested() || !FSlateApplication::IsInitialized())
        {
            GlowPainterSourceWindow.Reset();
            MapBakerSourceWindow.Reset();
            return;
        }

        const auto CloseWindow = [](TSharedPtr<SWindow>& Window)
        {
            if (Window.IsValid() && Window->GetNativeWindow().IsValid())
            {
                Window->RequestDestroyWindow();
            }
            Window.Reset();
        };

        CloseWindow(GlowPainterSourceWindow);
        CloseWindow(MapBakerSourceWindow);
    }

    void MountEmbeddedTools()
    {
        if (Selection.IsValid())
        {
            SyncMeshAssetToContentBrowser();
        }

        FString BakerError;
        const bool bBakerMounted = MountExistingToolPanel(
            MapBakerModuleName,
            MapBakerModuleName,
            MapBakerEntryName,
            MapBakerWindowTitle,
            MapBakerHostBox,
            MapBakerSourceWindow,
            BakerError);
        if (!bBakerMounted && MapBakerHostBox.IsValid())
        {
            UE_LOG(
                LogTemp,
                Warning,
                TEXT("[LocalizedMaterialWorkbench] Map Baker embed failed: %s"),
                *BakerError);
            MapBakerHostBox->SetContent(MakeEmbeddedMessage(
                FText::FromString(FString::Printf(
                    TEXT("Map Baker embed failed: %s"),
                    *BakerError))));
        }

        FString PainterError;
        const bool bPainterMounted = MountExistingToolPanel(
            GlowPainterModuleName,
            GlowPainterModuleName,
            GlowPainterEntryName,
            GlowPainterWindowTitle,
            GlowPainterHostBox,
            GlowPainterSourceWindow,
            PainterError);
        if (!bPainterMounted && GlowPainterHostBox.IsValid())
        {
            UE_LOG(
                LogTemp,
                Warning,
                TEXT("[LocalizedMaterialWorkbench] Glow Painter embed failed: %s"),
                *PainterError);
            GlowPainterHostBox->SetContent(MakeEmbeddedMessage(
                FText::FromString(FString::Printf(
                    TEXT("Glow Painter embed failed: %s"),
                    *PainterError))));
        }

        if (CompatibilityText.IsValid())
        {
            CompatibilityText->SetText(FText::FromString(
                GetCompatibilitySummary()));
        }

        if (bBakerMounted && bPainterMounted)
        {
            SetStatus(TEXT("Map Baker and Glow Painter are embedded in this Workbench."));
        }
        else
        {
            SetStatus(FString::Printf(
                TEXT("Embed result: Map Baker %s; Glow Painter %s."),
                bBakerMounted ? TEXT("OK") : *BakerError,
                bPainterMounted ? TEXT("OK") : *PainterError));
        }
    }

    TSharedRef<SWidget> MakeSection(
        const FText& Title,
        const TSharedRef<SWidget>& Content) const
    {
        return SNew(SBorder)
            .Padding(10.0f)
            .BorderBackgroundColor(FLinearColor(0.07f, 0.075f, 0.08f, 1.0f))
            [
                SNew(SVerticalBox)
                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(0.0f, 0.0f, 0.0f, 8.0f)
                [
                    SNew(STextBlock)
                    .Text(Title)
                    .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 11))
                ]
                + SVerticalBox::Slot()
                .AutoHeight()
                [
                    Content
                ]
            ];
    }

    TSharedRef<SWidget> MakeStepButton(
        const FText& Number,
        const FText& Label,
        LocalizedMaterialWorkbench::EWorkflowStep Step)
    {
        return SNew(SButton)
            .HAlign(HAlign_Left)
            .OnClicked_Lambda([this, Step]()
            {
                SetStep(Step);
                return FReply::Handled();
            })
            [
                SNew(STextBlock)
                .Text(FText::Format(
                    LOCTEXT("StepButtonFormat", "{0}. {1}"),
                    Number,
                    Label))
            ];
    }

    TSharedRef<SWidget> BuildInspectPage()
    {
        return SNew(SVerticalBox)
            + SVerticalBox::Slot()
            .AutoHeight()
            [
                MakeSection(
                    LOCTEXT("SelectionAnalysisTitle", "模型与工作流分析"),
                    SNew(SVerticalBox)
                    + SVerticalBox::Slot()
                    .AutoHeight()
                    [
                        SNew(STextBlock)
                        .Text_Lambda([this]()
                        {
                            return FText::FromString(
                                LocalizedMaterialWorkbench::MakeSelectionSummary(Selection));
                        })
                        .AutoWrapText(true)
                    ]
                    + SVerticalBox::Slot()
                    .AutoHeight()
                    .Padding(0.0f, 8.0f, 0.0f, 0.0f)
                    [
                        SNew(STextBlock)
                        .Text_Lambda([this]()
                        {
                            return FText::FromString(
                                LocalizedMaterialWorkbench::MakeUVStrategySummary(Selection));
                        })
                        .AutoWrapText(true)
                    ])
            ]
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0.0f, 10.0f, 0.0f, 0.0f)
            [
                MakeSection(
                    LOCTEXT("ResponsibilityTitle", "检查职责"),
                    SNew(STextBlock)
                    .Text(LOCTEXT(
                        "ResponsibilityBody",
                        "Workbench：统一读取选择、模型类型、材质槽与 UV 数量。\n"
                        "Map Baker：检查 UV 边界、跨材质重叠、开放边与烘焙条件。\n"
                        "Glow Painter：检查共享/镜像 UV，并按需创建非破坏式 Unique Mask UV。"))
                    .AutoWrapText(true))
            ]
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0.0f, 12.0f, 0.0f, 0.0f)
            .HAlign(HAlign_Right)
            [
                SNew(SButton)
                .Text(LOCTEXT("RefreshAnalysisButton", "刷新并重新分析"))
                .OnClicked_Lambda([this]()
                {
                    RefreshSelection();
                    return FReply::Handled();
                })
            ];
    }

    TSharedRef<SWidget> BuildLegacyBakePage()
    {
        return SNew(SVerticalBox)
            + SVerticalBox::Slot()
            .AutoHeight()
            [
                MakeSection(
                    LOCTEXT("BakePageTitle", "Localized Map Baker"),
                    SNew(SVerticalBox)
                    + SVerticalBox::Slot()
                    .AutoHeight()
                    [
                        SNew(STextBlock)
                        .Text(LOCTEXT(
                            "BakeDescription",
                            "生成 AO、Thickness 与 Confidence。工作台会先把当前网格体同步到内容浏览器，再启动原插件窗口；原插件设置和资产保存逻辑保持不变。"))
                        .AutoWrapText(true)
                    ]
                    + SVerticalBox::Slot()
                    .AutoHeight()
                    .Padding(0.0f, 10.0f, 0.0f, 0.0f)
                    [
                        SNew(STextBlock)
                        .Text_Lambda([this]()
                        {
                            return FText::FromString(FString::Printf(
                                TEXT("当前输入：%s"),
                                Selection.IsValid()
                                    ? *Selection.MeshAsset->GetPathName()
                                    : TEXT("None")));
                        })
                        .AutoWrapText(true)
                    ])
            ]
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0.0f, 12.0f, 0.0f, 0.0f)
            .HAlign(HAlign_Right)
            [
                SNew(SButton)
                .Text(LOCTEXT("OpenMapBakerButton", "同步选择并打开 Map Baker"))
                .OnClicked_Raw(this, &FLocalizedMaterialWorkbenchModule::OpenMapBaker)
            ];
    }

    TSharedRef<SWidget> BuildLegacyPaintPage()
    {
        return SNew(SVerticalBox)
            + SVerticalBox::Slot()
            .AutoHeight()
            [
                MakeSection(
                    LOCTEXT("PaintPageTitle", "Localized Glow Painter"),
                    SNew(SVerticalBox)
                    + SVerticalBox::Slot()
                    .AutoHeight()
                    [
                        SNew(STextBlock)
                        .Text(LOCTEXT(
                            "PaintDescription",
                            "在独立 Mask Texture 上绘制发光区域。工作台保留当前关卡 Actor 选择后启动原插件；原材质恢复、Undo/Cancel、Unique Mask UV 和 Mesh Paint 会话仍由稳定版 Glow Painter 管理。"))
                        .AutoWrapText(true)
                    ]
                    + SVerticalBox::Slot()
                    .AutoHeight()
                    .Padding(0.0f, 10.0f, 0.0f, 0.0f)
                    [
                        SNew(STextBlock)
                        .Text_Lambda([this]()
                        {
                            const FString Readiness = !Selection.IsValid()
                                ? TEXT("未选择模型")
                                : Selection.bFromLevelSelection
                                    ? TEXT("关卡 Actor 已就绪")
                                    : TEXT("请把资产放入关卡并选择 Actor");
                            return FText::FromString(FString::Printf(
                                TEXT("Painter 输入状态：%s"), *Readiness));
                        })
                    ])
            ]
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0.0f, 12.0f, 0.0f, 0.0f)
            .HAlign(HAlign_Right)
            [
                SNew(SButton)
                .Text(LOCTEXT("OpenGlowPainterButton", "保留 Actor 并打开 Glow Painter"))
                .OnClicked_Raw(this, &FLocalizedMaterialWorkbenchModule::OpenGlowPainter)
            ];
    }

    TSharedRef<SWidget> BuildBakePage()
    {
        return SAssignNew(MapBakerHostBox, SBox)
            .MinDesiredHeight(700.0f)
            [
                MakeEmbeddedMessage(LOCTEXT(
                    "MapBakerLoading",
                    "Loading the complete Localized Map Baker controls into this page..."))
            ];
    }

    TSharedRef<SWidget> BuildPaintPage()
    {
        return SAssignNew(GlowPainterHostBox, SBox)
            .MinDesiredHeight(760.0f)
            [
                MakeEmbeddedMessage(LOCTEXT(
                    "GlowPainterLoading",
                    "Loading the complete Localized Glow Painter controls into this page..."))
            ];
    }

    TSharedRef<SWidget> BuildExportPage()
    {
        return SNew(SVerticalBox)
            + SVerticalBox::Slot()
            .AutoHeight()
            [
                MakeSection(
                    LOCTEXT("ExportPageTitle", "预览与输出检查"),
                    SNew(STextBlock)
                    .Text(LOCTEXT(
                        "ExportDescription",
                        "当前测试版不重写两个原插件的资产保存机制。\n\n"
                        "Baker 输出：AO / Thickness / Confidence Texture Assets。\n"
                        "Painter 输出：按材质槽保存的 Glow Mask Texture Assets。\n"
                        "安全原则：两个模块各自保存、各自恢复；关闭 Workbench 不会结束或修改其会话。"))
                    .AutoWrapText(true))
            ]
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0.0f, 10.0f, 0.0f, 0.0f)
            [
                MakeSection(
                    LOCTEXT("NextIntegrationTitle", "下一阶段接口"),
                    SNew(STextBlock)
                    .Text(LOCTEXT(
                        "NextIntegrationBody",
                        "待本轮启动、选择交接和稳定性测试通过后，再增加只读资产清单与可选的 AO/Thickness 预览合成；不会直接修改 Paint Mask。"))
                    .AutoWrapText(true))
            ];
    }

    TSharedRef<SWidget> BuildWorkbenchPanel()
    {
        return SNew(SScrollBox)
            + SScrollBox::Slot()
            [
                SNew(SVerticalBox)
                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(16.0f, 14.0f, 16.0f, 8.0f)
                [
                    SNew(STextBlock)
                    .Text(LOCTEXT("WorkbenchTitle", "Localized Material Workbench 0.1.1-test"))
                    .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 16))
                ]
                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(16.0f, 0.0f, 16.0f, 12.0f)
                [
                    SNew(STextBlock)
                    .Text(LOCTEXT(
                        "WorkbenchSubtitle",
                        "统一面板直接承载 Map Baker 与 Glow Painter 完整控件 · 两个原插件保持独立且不被修改"))
                    .AutoWrapText(true)
                ]
                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(16.0f, 0.0f, 16.0f, 10.0f)
                [
                    SNew(SBorder)
                    .Padding(10.0f)
                    .BorderBackgroundColor(FLinearColor(0.045f, 0.05f, 0.055f, 1.0f))
                    [
                        SNew(SVerticalBox)
                        + SVerticalBox::Slot()
                        .AutoHeight()
                        [
                            SAssignNew(SelectedAssetText, STextBlock)
                            .AutoWrapText(true)
                        ]
                        + SVerticalBox::Slot()
                        .AutoHeight()
                        .Padding(0.0f, 6.0f, 0.0f, 0.0f)
                        [
                            SAssignNew(CompatibilityText, STextBlock)
                            .AutoWrapText(true)
                        ]
                    ]
                ]
                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(16.0f, 0.0f, 16.0f, 10.0f)
                [
                    SNew(SUniformGridPanel)
                    .SlotPadding(FMargin(3.0f))
                    + SUniformGridPanel::Slot(0, 0)
                    [
                        MakeStepButton(
                            LOCTEXT("Step1", "1"),
                            LOCTEXT("InspectStep", "资产分析"),
                            LocalizedMaterialWorkbench::EWorkflowStep::Inspect)
                    ]
                    + SUniformGridPanel::Slot(1, 0)
                    [
                        MakeStepButton(
                            LOCTEXT("Step2", "2"),
                            LOCTEXT("BakeStep", "辅助贴图烘焙"),
                            LocalizedMaterialWorkbench::EWorkflowStep::Bake)
                    ]
                    + SUniformGridPanel::Slot(2, 0)
                    [
                        MakeStepButton(
                            LOCTEXT("Step3", "3"),
                            LOCTEXT("PaintStep", "发光遮罩绘制"),
                            LocalizedMaterialWorkbench::EWorkflowStep::Paint)
                    ]
                    + SUniformGridPanel::Slot(3, 0)
                    [
                        MakeStepButton(
                            LOCTEXT("Step4", "4"),
                            LOCTEXT("ExportStep", "预览与导出"),
                            LocalizedMaterialWorkbench::EWorkflowStep::Export)
                    ]
                ]
                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(16.0f, 0.0f, 16.0f, 10.0f)
                [
                    SAssignNew(WorkflowSwitcher, SWidgetSwitcher)
                    + SWidgetSwitcher::Slot()[BuildInspectPage()]
                    + SWidgetSwitcher::Slot()[BuildBakePage()]
                    + SWidgetSwitcher::Slot()[BuildPaintPage()]
                    + SWidgetSwitcher::Slot()[BuildExportPage()]
                ]
                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(16.0f, 0.0f, 16.0f, 14.0f)
                [
                    SNew(SBorder)
                    .Padding(9.0f)
                    .BorderBackgroundColor(FLinearColor(0.03f, 0.09f, 0.14f, 1.0f))
                    [
                        SAssignNew(StatusText, STextBlock)
                        .AutoWrapText(true)
                    ]
                ]
            ];
    }

    void OpenWorkbench()
    {
        if (TSharedPtr<SWindow> ExistingWindow = WorkbenchWindow.Pin())
        {
            ExistingWindow->BringToFront(true);
            RefreshSelection();
            MountEmbeddedTools();
            return;
        }

        TSharedRef<SWindow> Window = SNew(SWindow)
            .Title(LOCTEXT("WorkbenchWindowTitle", "Localized Material Workbench"))
            .ClientSize(FVector2D(1180.0f, 900.0f))
            .SizingRule(ESizingRule::UserSized)
            .SupportsMaximize(true)
            .SupportsMinimize(true);

        Window->SetContent(BuildWorkbenchPanel());
        Window->SetOnWindowClosed(FOnWindowClosed::CreateLambda(
            [this](const TSharedRef<SWindow>&)
            {
                CloseEmbeddedToolWindows();
                WorkbenchWindow.Reset();
                WorkflowSwitcher.Reset();
                SelectedAssetText.Reset();
                UVStrategyText.Reset();
                CompatibilityText.Reset();
                StatusText.Reset();
                MapBakerHostBox.Reset();
                GlowPainterHostBox.Reset();
            }));
        WorkbenchWindow = Window;
        FSlateApplication::Get().AddWindow(Window);
        RefreshSelection();
        MountEmbeddedTools();
        SetStep(LocalizedMaterialWorkbench::EWorkflowStep::Inspect);
    }

    void RunSelfTest()
    {
        const LocalizedMaterialWorkbench::FSelectionSnapshot Snapshot =
            LocalizedMaterialWorkbench::ResolveSelection();
        const bool bBakerModule =
            LocalizedMaterialWorkbench::HasModule(MapBakerModuleName);
        const bool bPainterModule =
            LocalizedMaterialWorkbench::HasModule(GlowPainterModuleName);
        const bool bToolMenusReady = UToolMenus::TryGet() != nullptr;
        const bool bWorkbenchOpen = WorkbenchWindow.IsValid();
        const bool bBakerEmbedded =
            MapBakerHostBox.IsValid() && MapBakerSourceWindow.IsValid();
        const bool bPainterEmbedded =
            GlowPainterHostBox.IsValid() && GlowPainterSourceWindow.IsValid();
        const bool bPassed =
            bBakerModule && bPainterModule && bToolMenusReady &&
            (!bWorkbenchOpen || (bBakerEmbedded && bPainterEmbedded));

        UE_LOG(
            LogTemp,
            Display,
            TEXT("[LocalizedMaterialWorkbench][SelfTest] %s BakerModule=%d PainterModule=%d ToolMenus=%d WorkbenchOpen=%d BakerEmbedded=%d PainterEmbedded=%d Selection=%s"),
            bPassed ? TEXT("PASS") : TEXT("FAIL"),
            bBakerModule ? 1 : 0,
            bPainterModule ? 1 : 0,
            bToolMenusReady ? 1 : 0,
            bWorkbenchOpen ? 1 : 0,
            bBakerEmbedded ? 1 : 0,
            bPainterEmbedded ? 1 : 0,
            *LocalizedMaterialWorkbench::MakeSelectionSummary(Snapshot));
    }
};

IMPLEMENT_MODULE(
    FLocalizedMaterialWorkbenchModule,
    LocalizedMaterialWorkbench)

#undef LOCTEXT_NAMESPACE
