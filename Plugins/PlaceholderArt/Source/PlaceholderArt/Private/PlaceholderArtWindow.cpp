// Copyright Epic Games, Inc. All Rights Reserved.
#include "PlaceholderArtWindow.h"

#include "AssetToolsModule.h"
#include "ContentBrowserModule.h"
#include "DesktopPlatformModule.h"
#include "FileHelpers.h"
#include "IAssetTools.h"
#include "IContentBrowserSingleton.h"
#include "IDesktopPlatform.h"
#include "PropertyCustomizationHelpers.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Canvas.h"
#include "Engine/Font.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/SBoxPanel.h"
#include "Misc/MessageDialog.h"
#include "Runtime/AppFramework/Public/Widgets/Colors/SColorPicker.h"
#include "UObject/SavePackage.h"
#include "Widgets/Colors/SColorBlock.h"
#include "Widgets/Input/STextComboBox.h"

void SPlaceholderArtWindow::Construct(const FArguments& InArgs)
{
    // 初始化选项数组
    MeshTypeOptions.Add(MakeShareable(new FString("Cube")));
    MeshTypeOptions.Add(MakeShareable(new FString("Cylinder")));
    MeshTypeOptions.Add(MakeShareable(new FString("Plane")));
    MeshTypeOptions.Add(MakeShareable(new FString("Sphere")));

    // 加载默认字体
    SelectedFont = LoadObject<UFont>(nullptr, TEXT("/Engine/EngineFonts/Roboto.Roboto"));
    
    ChildSlot
    [
        MakeMainLayout()
    ];
}

TSharedRef<SWidget> SPlaceholderArtWindow::MakeMainLayout()
{
    return SNew(SVerticalBox)
    
    // 主标题
    + SVerticalBox::Slot()
    .AutoHeight()
    .Padding(10.0f, 10.0f, 10.0f, 5.0f)
    [
        SNew(STextBlock)
        .Text(FText::FromString(TEXT("🎨 占位美术工具")))
        .Font(FCoreStyle::Get().GetFontStyle("HeadingFont"))
    ]
    
    // 分隔线
    + SVerticalBox::Slot()
    .AutoHeight()
    .Padding(0.0f, 0.0f, 0.0f, 10.0f)
    [
        SNew(SSeparator)
    ]
    
    // 创建占位网格体区域
    + SVerticalBox::Slot()
    .AutoHeight()
    .Padding(10.0f, 0.0f, 10.0f, 10.0f)
    [
        MakeMeshSection()
    ]
    
    // 文字设置区域
    + SVerticalBox::Slot()
    .AutoHeight()
    .Padding(10.0f, 0.0f, 10.0f, 10.0f)
    [
        MakeTextSection()
    ]
    
    // 文件路径区域
    + SVerticalBox::Slot()
    .AutoHeight()
    .Padding(10.0f, 0.0f, 10.0f, 10.0f)
    [
        MakePathSection()
    ]
    
    // 按钮区域
    + SVerticalBox::Slot()
    .AutoHeight()
    .Padding(10.0f, 0.0f, 10.0f, 10.0f)
    [
        MakeButtonSection()
    ]
    
    // 底部状态栏
    + SVerticalBox::Slot()
    .FillHeight(1.0f)
    .VAlign(VAlign_Bottom)
    .Padding(10.0f)
    [
        SNew(STextBlock)
        .Text(FText::FromString(TEXT("版本 1.0 | 就绪")))
        .ColorAndOpacity(FSlateColor::UseSubduedForeground())
    ];
}

TSharedRef<SWidget> SPlaceholderArtWindow::MakeMeshSection()
{
    return SNew(SBorder)
    .Padding(10.0f)
    .BorderImage(FCoreStyle::Get().GetBrush("ToolPanel.GroupBorder"))
    [
        SNew(SVerticalBox)
        
        // 子标题
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(0.0f, 0.0f, 0.0f, 10.0f)
        [
            SNew(STextBlock)
            .Text(FText::FromString(TEXT("创建占位网格体")))
            .Font(FCoreStyle::Get().GetFontStyle("BoldFont"))
        ]
        
        // 基础网格体下拉框
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(0.0f, 0.0f, 0.0f, 10.0f)
        [
            SNew(SHorizontalBox)
            
            + SHorizontalBox::Slot()
            .AutoWidth()
            .VAlign(VAlign_Center)
            .Padding(0.0f, 0.0f, 10.0f, 0.0f)
            [
                SNew(STextBlock)
                .Text(FText::FromString(TEXT("基础网格体:")))
                .MinDesiredWidth(100.0f)
            ]
            
            + SHorizontalBox::Slot()
            .FillWidth(1.0f)
            .HAlign(HAlign_Left)
            [
                SNew(SBox)
                .WidthOverride(200.0f)
                [
                    SAssignNew(MeshTypeComboBox, STextComboBox)
                    .OptionsSource(&MeshTypeOptions)
                    .InitiallySelectedItem(MeshTypeOptions[0])
                ]
            ]
        ]
        
        // 基础颜色选择器
        + SVerticalBox::Slot()
        .AutoHeight()
        [
            SNew(SHorizontalBox)
            
            + SHorizontalBox::Slot()
            .AutoWidth()
            .VAlign(VAlign_Center)
            .Padding(0.0f, 0.0f, 10.0f, 0.0f)
            [
                SNew(STextBlock)
                .Text(FText::FromString(TEXT("基础颜色:")))
                .MinDesiredWidth(100.0f)
            ]
            
            + SHorizontalBox::Slot()
            .FillWidth(1.0f)
            .HAlign(HAlign_Left)
            [
                SNew(SColorBlock)
                .Size(FVector2D(100.0f, 20.0f))
                .CornerRadius(FVector4(4.0f, 4.0f, 4.0f, 4.0f))
                .Color(this, &SPlaceholderArtWindow::GetBaseColor)
                .OnMouseButtonDown_Lambda([this](const FGeometry& Geometry, const FPointerEvent& Event)
                {
                    // 关闭已打开的选择器
                    if (ColorPickerWindow.IsValid())
                    {
                        ColorPickerWindow->RequestDestroyWindow();
                    }

                    // 创建颜色选择器窗口
                    ColorPickerWindow = SNew(SWindow)
                        .Title(FText::FromString(TEXT("选择颜色")))
                        .SizingRule(ESizingRule::Autosized)
                        .AutoCenter(EAutoCenter::PreferredWorkArea)
                        .SupportsMaximize(false)
                        .SupportsMinimize(false)
                        .IsTopmostWindow(true)
                        [
                            SNew(SColorPicker)
                            .TargetColorAttribute(BaseColor)
                            .OnColorCommitted_Lambda([this](FLinearColor NewColor)
                            {
                                BaseColor = NewColor;

                                // 刷新ColorBlock
                                this->Invalidate(EInvalidateWidgetReason::Paint);

                                // 关闭选择器窗口
                                if (ColorPickerWindow.IsValid())
                                {
                                    ColorPickerWindow->RequestDestroyWindow();
                                    ColorPickerWindow.Reset();
                                }
                            })
                        ];

                    FSlateApplication::Get().AddWindow(ColorPickerWindow.ToSharedRef());

                    return FReply::Handled();
                })
                .ShowBackgroundForAlpha(true)
            ]
        ]
    ];
}

TSharedRef<SWidget> SPlaceholderArtWindow::MakeTextSection()
{
    return SNew(SBorder)
    .Padding(10.0f)
    .BorderImage(FCoreStyle::Get().GetBrush("ToolPanel.GroupBorder"))
    [
        SNew(SVerticalBox)
        
        // 子标题
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(0.0f, 0.0f, 0.0f, 10.0f)
        [
            SNew(STextBlock)
            .Text(FText::FromString(TEXT("文字设置")))
            .Font(FCoreStyle::Get().GetFontStyle("BoldFont"))
        ]
        
        // 占位文字输入框
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(0.0f, 0.0f, 0.0f, 10.0f)
        [
            SNew(SHorizontalBox)
            
            + SHorizontalBox::Slot()
            .AutoWidth()
            .VAlign(VAlign_Center)
            .Padding(0.0f, 0.0f, 10.0f, 0.0f)
            [
                SNew(STextBlock)
                .Text(FText::FromString(TEXT("占位文字:")))
                .MinDesiredWidth(100.0f)
            ]
            
            + SHorizontalBox::Slot()
            .FillWidth(1.0f)
            [
                SAssignNew(PlaceholderTextWidget, SEditableTextBox)
                .HintText(FText::FromString(TEXT("输入文字...")))
            ]
        ]
        
        // 文字大小输入框
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(0.0f, 0.0f, 0.0f, 10.0f)
        [
            SNew(SHorizontalBox)
            
            + SHorizontalBox::Slot()
            .AutoWidth()
            .VAlign(VAlign_Center)
            .Padding(0.0f, 0.0f, 10.0f, 0.0f)
            [
                SNew(STextBlock)
                .Text(FText::FromString(TEXT("文字大小:")))
                .MinDesiredWidth(100.0f)
            ]
            
            + SHorizontalBox::Slot()
            .FillWidth(1.0f)
            [
                SAssignNew(TextSizeWidget, SSpinBox<int32>)
                .Value(24)
                .MinDesiredWidth(50)
                .MinValue(1)
                .MaxValue(100)
            ]
        ]
        
        // 文字颜色选择器
        + SVerticalBox::Slot()
        .AutoHeight()
        [
            SNew(SHorizontalBox)
            
            + SHorizontalBox::Slot()
            .AutoWidth()
            .VAlign(VAlign_Center)
            .Padding(0.0f, 0.0f, 10.0f, 0.0f)
            [
                SNew(STextBlock)
                .Text(FText::FromString(TEXT("文字颜色:")))
                .MinDesiredWidth(100.0f)
            ]
            
            + SHorizontalBox::Slot()
            .FillWidth(1.0f)
            .HAlign(HAlign_Left)
            [
                SNew(SColorBlock)
                .Size(FVector2D(100.0f, 20.0f))
                .CornerRadius(FVector4(4.0f, 4.0f, 4.0f, 4.0f))
                .Color(this, &SPlaceholderArtWindow::GetTextColor)
                .OnMouseButtonDown_Lambda([this](const FGeometry& Geometry, const FPointerEvent& Event)
                {
                    // 关闭已打开的选择器
                    if (ColorPickerWindow.IsValid())
                    {
                        ColorPickerWindow->RequestDestroyWindow();
                    }

                    // 创建颜色选择器窗口
                    ColorPickerWindow = SNew(SWindow)
                        .Title(FText::FromString(TEXT("选择颜色")))
                        .SizingRule(ESizingRule::Autosized)
                        .AutoCenter(EAutoCenter::PreferredWorkArea)
                        .SupportsMaximize(false)
                        .SupportsMinimize(false)
                        .IsTopmostWindow(true)
                        [
                            SNew(SColorPicker)
                            .TargetColorAttribute(TextColor)
                            .OnColorCommitted_Lambda([this](FLinearColor NewColor)
                            {
                                TextColor = NewColor;

                                // 刷新ColorBlock
                                this->Invalidate(EInvalidateWidgetReason::Paint);

                                // 关闭选择器窗口
                                if (ColorPickerWindow.IsValid())
                                {
                                    ColorPickerWindow->RequestDestroyWindow();
                                    ColorPickerWindow.Reset();
                                }
                            })
                        ];

                    FSlateApplication::Get().AddWindow(ColorPickerWindow.ToSharedRef());

                    return FReply::Handled();
                })
                .ShowBackgroundForAlpha(true)
            ]
        ]

        // 文字字体选择
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(0.0f, 0.0f, 0.0f, 10.0f)
        [
            SNew(SHorizontalBox)
            
            + SHorizontalBox::Slot()
            .AutoWidth()
            .VAlign(VAlign_Center)
            .Padding(0.0f, 0.0f, 10.0f, 0.0f)
            [
                SNew(STextBlock)
                .Text(FText::FromString(TEXT("文字字体:")))
                .MinDesiredWidth(100.0f)
            ]
            
            + SHorizontalBox::Slot()
            .FillWidth(1.0f)
            .HAlign(HAlign_Left)
            [
                SNew(SBox)
                .WidthOverride(200.0f)
                .HeightOverride(24.0f)
                [
                    SAssignNew(FontPickerWidget, SObjectPropertyEntryBox)
                    .AllowedClass(UFont::StaticClass())  
                    .ObjectPath(this, &SPlaceholderArtWindow::GetSelectedFontPath)
                    .OnObjectChanged_Lambda([this](const FAssetData& AssetData)
                    {
                        if (AssetData.IsValid())
                        {
                            SelectedFont = Cast<UFont>(AssetData.GetAsset());
                        }
    
                        // 刷新窗口
                        this->Invalidate(EInvalidateWidgetReason::Paint);
                    })
                ]
            ]
        ]
    ];
}

TSharedRef<SWidget> SPlaceholderArtWindow::MakePathSection()
{
    return SNew(SBorder)
    .Padding(10.0f)
    .BorderImage(FCoreStyle::Get().GetBrush("ToolPanel.GroupBorder"))
    [
        SNew(SVerticalBox)
        
        // 子标题
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(0.0f, 0.0f, 0.0f, 10.0f)
        [
            SNew(STextBlock)
            .Text(FText::FromString(TEXT("输出设置")))
            .Font(FCoreStyle::Get().GetFontStyle("BoldFont"))
        ]
        
        // 文件生成路径
        + SVerticalBox::Slot()
        .AutoHeight()
        [
            SNew(SHorizontalBox)
            
            + SHorizontalBox::Slot()
            .AutoWidth()
            .VAlign(VAlign_Center)
            .Padding(0.0f, 0.0f, 10.0f, 0.0f)
            [
                SNew(STextBlock)
                .Text(FText::FromString(TEXT("生成路径:")))
                .MinDesiredWidth(100.0f)
            ]
            
            + SHorizontalBox::Slot()
            .FillWidth(1.0f)
            [
                SAssignNew(FilePathWidget, SEditableTextBox)
                .Text(FText::FromString(TEXT("Content/")))
            ]
            
            + SHorizontalBox::Slot()
            .AutoWidth()
            [
                SNew(SButton)
                .OnClicked_Lambda([this]()
                    {
                        FString DefaultPath = FPaths::ProjectContentDir();
                        FString SelectedPath;

                        // 👇 获取桌面平台模块
                        IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();

                        if (DesktopPlatform)
                        {
                            // 打开目录选择对话框
                            if (DesktopPlatform->OpenDirectoryDialog(
                                FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
                                TEXT("选择生成路径"),
                                DefaultPath,
                                SelectedPath))
                            {
                                FString RelativePath = SelectedPath;
                                FPaths::MakePathRelativeTo(RelativePath, *FPaths::ProjectContentDir());

                                // 更新输入框
                                if (FilePathWidget.IsValid())
                                {
                                    FilePathWidget->SetText(FText::FromString(TEXT("Content/") + RelativePath + TEXT("/")));
                                }
                            }
                        }

                        return FReply::Handled();
                    }
                )
                .Content()
                [
                    SNew(STextBlock)
                    .Text(FText::FromString(TEXT("📁")))
                    .MinDesiredWidth(10.0f)
                ]
            ]

        ]

        // 文件命名
        + SVerticalBox::Slot()
        .AutoHeight()
        [
            SNew(SHorizontalBox)
            
            + SHorizontalBox::Slot()
            .AutoWidth()
            .VAlign(VAlign_Center)
            .Padding(0.0f, 0.0f, 10.0f, 0.0f)
            [
                SNew(STextBlock)
                .Text(FText::FromString(TEXT("命名:")))
                .MinDesiredWidth(100.0f)
            ]
            
            + SHorizontalBox::Slot()
            .FillWidth(1.0f)
            [
                SAssignNew(FileNameWidget, SEditableTextBox)
                .HintText(FText::FromString(TEXT("输入文件命名")))
            ]
        

        ]
    ];
}

TSharedRef<SWidget> SPlaceholderArtWindow::MakeButtonSection()
{
    return SNew(SHorizontalBox)
    
    // 确认按钮
    + SHorizontalBox::Slot()
    .FillWidth(1.0f)
    .HAlign(HAlign_Center)
    .Padding(0.0f, 10.0f)
    [
        SNew(SButton)
        .HAlign(HAlign_Center)
        .VAlign(VAlign_Center)
        .OnClicked(this, &SPlaceholderArtWindow::OnConfirmButtonClicked)
        .Content()
        [
            SNew(STextBlock)
            .Text(FText::FromString(TEXT("确认生成")))
        ]
    ];
}

FReply SPlaceholderArtWindow::OnConfirmButtonClicked()
{
    // 获取用户输入
    FString MeshName = *MeshTypeComboBox->GetSelectedItem();
    FString PlaceholderText = PlaceholderTextWidget->GetText().ToString();
    int32 TextSize = TextSizeWidget->GetValue();
    FString FilePath = FilePathWidget->GetText().ToString();
    FString FileName = FileNameWidget->GetText().ToString();

    // 验证路径
    FText ErrorMessage;
    FString FullPath = FPaths::Combine(FPaths::ProjectDir(), FilePath);
    if (!ValidatePath(FullPath, ErrorMessage))
    {
        FMessageDialog::Open(EAppMsgType::Ok, ErrorMessage);
        return FReply::Handled();
    }

    // 执行生成逻辑
    if (!GeneratePlaceholderAsset())
    {
        FMessageDialog::Open(
            EAppMsgType::Ok,
            FText::FromString(TEXT("❌ 生成失败\n\n无法生成占位符资产，请查看日志获取详细信息。"))
        );
        return FReply::Handled();
    }
    
    // 显示确认信息
    FString Message = FString::Printf(
        TEXT("生成配置:\n\n"
             "网格体类型: %s\n"
             "基础颜色: R=%.2f G=%.2f B=%.2f A=%.2f\n"
             "占位文字: %s\n"
             "文字大小: %d\n"
             "文字颜色: R=%.2f G=%.2f B=%.2f A=%.2f\n"
             "生成路径: %s\n"
             "文件命名: %s\n"
             ),
        *MeshName,
        BaseColor.R, BaseColor.G, BaseColor.B, BaseColor.A,
        *PlaceholderText,
        TextSize,
        TextColor.R, TextColor.G, TextColor.B, TextColor.A,
        *FilePath,
        *FileName
    );
    
    FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Message));
    
    return FReply::Handled();
}

FString SPlaceholderArtWindow::GetSelectedFontPath() const
{
    if (SelectedFont)
    {
        return SelectedFont->GetPathName();
    }
    return FString();
}

bool SPlaceholderArtWindow::ValidatePath(const FString& InPath, FText& OutErrorMessage) const
{
    // 检查是否为空
    if (InPath.IsEmpty())
    {
        OutErrorMessage = FText::FromString(TEXT("⚠️ 路径不能为空！"));
        return false;
    }
    
    // 检查是否在 Content 目录内
    FString ContentDir = FPaths::ProjectContentDir();
    if (!InPath.StartsWith(ContentDir) && !InPath.StartsWith(TEXT("/Game/")))
    {
        OutErrorMessage = FText::FromString(TEXT("⚠️ 路径必须在 Content 目录内！\n\n请选择项目 Content 目录下的路径。"));
        return false;
    }
    
    // 检查路径是否包含非法字符
    if (InPath.Contains(TEXT("?")) || InPath.Contains(TEXT("*")) || 
        InPath.Contains(TEXT("\"")) || InPath.Contains(TEXT("<")) || 
        InPath.Contains(TEXT(">")) || InPath.Contains(TEXT("|")))
    {
        OutErrorMessage = FText::FromString(TEXT("⚠️ 路径包含非法字符！"));
        return false;
    }
    
    // 检查路径长度
    if (InPath.Len() > 200)
    {
        OutErrorMessage = FText::FromString(TEXT("⚠️ 路径过长！"));
        return false;
    }
    
    return true;
}

bool SPlaceholderArtWindow::GeneratePlaceholderAsset()
{
    // 1. 获取选择的网格体类型
    FString MeshType = *MeshTypeComboBox->GetSelectedItem();
    
    // 2. 从引擎加载基础网格体
    UStaticMesh* BaseMesh = GetEngineBaseMesh(MeshType);
    if (!BaseMesh)
    {
        UE_LOG(LogTemp, Error, TEXT("无法加载引擎基础网格体：%s"), *MeshType);
        return false;
    }
    
    // 3. 构建目标路径
    FString FilePath = FilePathWidget->GetText().ToString();
    FString FileName = FileNameWidget->GetText().ToString();
    
    // 确保路径格式正确
    FilePath = FilePath.Replace(TEXT("Content/"), TEXT("/Game/"));
    
    FString PackagePath = FilePath.Append(FileName);
    FPaths::NormalizeDirectoryName(PackagePath);
    
    // 4. 复制资产到目标路径
    if (!CopyAssetToPath(BaseMesh, PackagePath, FileName))
    {
        UE_LOG(LogTemp, Error, TEXT("无法复制资产到目标路径：%s"), *PackagePath);
        return false;
    }
    
    UE_LOG(LogTemp, Log, TEXT("成功生成占位符资产：%s"), *PackagePath);
    return true;
}

UStaticMesh* SPlaceholderArtWindow::GetEngineBaseMesh(const FString& MeshType) const
{
    // 引擎内置基础网格体路径
    TMap<FString, FString> EngineMeshPaths;
    EngineMeshPaths.Add(TEXT("Cube"), TEXT("/Engine/BasicShapes/Cube.Cube"));
    EngineMeshPaths.Add(TEXT("Sphere"), TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    EngineMeshPaths.Add(TEXT("Cylinder"), TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
    EngineMeshPaths.Add(TEXT("Plane"), TEXT("/Engine/BasicShapes/Plane.Plane"));
    
    // 获取对应路径
    FString* MeshPath = EngineMeshPaths.Find(MeshType);
    if (!MeshPath)
    {
        UE_LOG(LogTemp, Error, TEXT("未知的网格体类型：%s"), *MeshType);
        return nullptr;
    }
    
    // 加载网格体
    UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, **MeshPath);
    if (!Mesh)
    {
        UE_LOG(LogTemp, Error, TEXT("无法加载网格体：%s"), **MeshPath);
        return nullptr;
    }
    
    return Mesh;
}

bool SPlaceholderArtWindow::CopyAssetToPath(UObject* SourceAsset, const FString& TargetPath, const FString& AssetName)
{
    if (!SourceAsset) return false;

    // 1. 创建纹理（未保存）
    UTexture* TextTexture = CreateTextTexture(TargetPath, AssetName);
    if (!TextTexture) return false;

    // 2. 创建材质实例（未保存）
    UMaterialInstanceConstant* MaterialInstance = CreateTextMaterial(TargetPath, AssetName, TextTexture);
    if (!MaterialInstance) return false;

    // 3. 复制静态网格体
    FString MeshPackageName = TargetPath;
    UPackage* MeshPackage = CreatePackage(*MeshPackageName);
    MeshPackage->SetFlags(RF_Public | RF_Standalone);

    UStaticMesh* SourceMesh = Cast<UStaticMesh>(SourceAsset);
    if (!SourceMesh) return false;

    UStaticMesh* NewMesh = DuplicateObject<UStaticMesh>(SourceMesh, MeshPackage, *AssetName);
    if (!NewMesh) return false;

    NewMesh->SetFlags(RF_Public | RF_Standalone);

    // 应用材质
    NewMesh->SetMaterial(0, MaterialInstance);

    // 标记包为脏（未保存）
    MeshPackage->MarkPackageDirty();

    // 通知资产注册表
    FAssetRegistryModule::AssetCreated(NewMesh);

    // 4. 刷新内容浏览器，显示三个资产
    FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
    TArray<UObject*> AssetsToSync = { TextTexture, MaterialInstance, NewMesh };
    ContentBrowserModule.Get().SyncBrowserToAssets(AssetsToSync);

    return true;
}

UMaterialInstanceConstant* SPlaceholderArtWindow::CreateTextMaterial(const FString& TargetPath, const FString& AssetName, UTexture* InTexture)
{
    UMaterial* BaseMaterial = LoadObject<UMaterial>(nullptr, TEXT("/Engine/EngineMaterials/EmissiveTexturedMaterial.EmissiveTexturedMaterial"));
    if (!BaseMaterial)
    {
        UE_LOG(LogTemp, Error, TEXT("无法加载材质：/Engine/EngineMaterials/EmissiveTexturedMaterial.EmissiveTexturedMaterial"));
        return nullptr;
    }

    // 构建包名和资产名
    FString MaterialAssetName = AssetName + TEXT("_Material");
    FString MaterialPackageName = TargetPath + TEXT("_Material");

    UPackage* MaterialPackage = CreatePackage(*MaterialPackageName);
    MaterialPackage->SetFlags(RF_Public | RF_Standalone);
    
    // 创建材质实例常量
    UMaterialInstanceConstant* MaterialInstance = NewObject<UMaterialInstanceConstant>(
        MaterialPackage,
        *MaterialAssetName,
        RF_Public | RF_Standalone
    );

    if (!MaterialInstance)
    {
        UE_LOG(LogTemp, Error, TEXT("无法创建材质实例：%s"), *MaterialAssetName);
        return nullptr;
    }

    // 设置父材质和参数
    MaterialInstance->SetParentEditorOnly(BaseMaterial);
    MaterialInstance->SetTextureParameterValueEditorOnly(FName("Texture"), InTexture);
    
    // 应用更改并标记包为脏
    MaterialInstance->PostEditChange();
    MaterialPackage->MarkPackageDirty();

    // 通知资产注册表
    FAssetRegistryModule::AssetCreated(MaterialInstance);
    return MaterialInstance;
}

UTexture* SPlaceholderArtWindow::CreateTextTexture(const FString& TargetPath, const FString& AssetName)
{
    // 创建渲染目标
    UTextureRenderTarget2D* TextRenderTarget = UKismetRenderingLibrary::CreateRenderTarget2D(
        GEditor->GetEditorWorldContext().World(),
        512,
        512,
        RTF_RGBA16f,
        BaseColor
        );

    // 开始渲染 获取画布
    UCanvas* Canvas;
    FVector2D Size;
    FDrawToRenderTargetContext Context;
    UKismetRenderingLibrary::BeginDrawCanvasToRenderTarget(
        GEditor->GetEditorWorldContext().World(),
        TextRenderTarget,
        Canvas,
        Size,
        Context
        );

    // 设置字体大小
    SelectedFont->LegacyFontSize = TextSizeWidget->GetValue();

    // 绘制文本
    const FVector2D TextSize = Canvas->K2_TextSize(SelectedFont, PlaceholderTextWidget->GetText().ToString(), FVector2D::One());
    const FVector2D Position = (Size - TextSize) / 2.0f;
    Canvas->K2_DrawText(
        SelectedFont,
        PlaceholderTextWidget->GetText().ToString(),
        Position,
        FVector2D::One(),
        TextColor
        );

    // 结束渲染
    UKismetRenderingLibrary::EndDrawCanvasToRenderTarget(GEditor->GetEditorWorldContext().World(), Context);

    // 创建静态纹理
    const FString TexturePackageName = TargetPath + TEXT("_Texture");
    return UKismetRenderingLibrary::RenderTargetCreateStaticTexture2DEditorOnly(
        TextRenderTarget,
        TexturePackageName
        );
}

