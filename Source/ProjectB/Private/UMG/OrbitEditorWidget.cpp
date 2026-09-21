// OrbitEditorWidget.cpp
// 轨道编辑界面实现：Slate 面板（SOrbitEditorPanel）+ UMG 包装控件（UOrbitEditorWidget）
#include "UMG/OrbitEditorWidget.h"

#include "Mass/SatelliteManagerComponent.h"
#include "Player/BasePlayerController.h"

#include "Framework/Application/SlateApplication.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Colors/SColorBlock.h"
#include "Widgets/Colors/SColorPicker.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "OrbitEditor"

// ==================== SOrbitEditorPanel（Slate 面板） ====================

void SOrbitEditorPanel::Construct(const FArguments& InArgs)
{
	Controller = InArgs._PlayerController;
	OnCloseRequested = InArgs._OnCloseRequested;

	ChildSlot
	[
		// 右上角对齐 + 固定宽度
		SNew(SBox)
		.HAlign(HAlign_Right)
		.VAlign(VAlign_Top)
		.Padding(16.f)
		[
			SNew(SBox)
			.WidthOverride(460.f)
			[
				SNew(SBorder)
				.Padding(12.f)
				.BorderImage(FCoreStyle::Get().GetBrush("ToolPanel.GroupBorder"))
				[
					SNew(SVerticalBox)

					// ---- 标题行 ----
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("OrbitEditorTitle", "轨道编辑"))
							.Font(FCoreStyle::GetDefaultFontStyle("Bold", 14))
						]
						+ SHorizontalBox::Slot().AutoWidth()
						[
							SNew(SButton)
							.Text(LOCTEXT("Close", "×"))
							.OnClicked(this, &SOrbitEditorPanel::OnCloseClicked)
						]
					]

					// ---- 列表区 ----
					+ SVerticalBox::Slot().AutoHeight()
					[
						BuildOrbitListSection()
					]

					// ---- 参数区 ----
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 0)
					[
						BuildParamSection()
					]

					// ---- 按钮区 ----
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 0)
					[
						BuildButtonSection()
					]
				]
			]
		]
	];

	RefreshOrbitList();
}

SOrbitEditorPanel::~SOrbitEditorPanel()
{
	if (ColorPickerWindow.IsValid())
	{
		ColorPickerWindow->RequestDestroyWindow();
	}
}

// ==================== 内部逻辑 ====================

void SOrbitEditorPanel::RefreshOrbitList()
{
	ABasePlayerController* PC = Controller.Get();
	OrbitNames.Reset();
	if (PC)
	{
		OrbitNames = PC->GetOrbitNames();
	}

	RebuildOrbitListRows();

	// 选中兜底：无效则选第一条
	if (!OrbitNames.Contains(SelectedOrbitName))
	{
		bHasSelection = false;
		SelectedOrbitName = NAME_None;
		if (OrbitNames.Num() > 0)
		{
			SelectOrbit(OrbitNames[0]);
		}
	}
}

void SOrbitEditorPanel::RebuildOrbitListRows()
{
	if (!OrbitListContainer.IsValid())
	{
		return;
	}

	OrbitListContainer->ClearChildren();
	for (const FName& Name : OrbitNames)
	{
		OrbitListContainer->AddSlot().AutoHeight().Padding(0, 1)
		[
			MakeOrbitRow(Name)
		];
	}
}

void SOrbitEditorPanel::SelectOrbit(const FName Name)
{
	ABasePlayerController* PC = Controller.Get();
	if (!PC)
	{
		return;
	}

	FRuntimeOrbitData Data;
	if (!PC->GetOrbitData(Name, Data))
	{
		return;
	}

	SelectedOrbitName = Name;
	WorkingData = Data;
	bHasSelection = true;

	// 反算倾角/方位（面板编辑状态；法线为轨道平面 Z 轴）
	const FVector Normal = Data.Transform.GetRotation().GetAxisZ();
	EditingTiltDeg = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(Normal.Z, -1.f, 1.f)));
	EditingAzimuthDeg = FMath::RadiansToDegrees(FMath::Atan2(Normal.Y, Normal.X));

	RebuildOrbitListRows();
}

void SOrbitEditorPanel::PushWorkingDataToManager()
{
	if (!bHasSelection)
	{
		return;
	}
	if (ABasePlayerController* PC = Controller.Get())
	{
		PC->SetOrbitData(WorkingData);   // 广播 → 可视化节流刷新（实时预览）
	}
}

FVector SOrbitEditorPanel::MakeNormalFromTiltAzimuth() const
{
	const float TiltRad = FMath::DegreesToRadians(EditingTiltDeg);
	const float AzimuthRad = FMath::DegreesToRadians(EditingAzimuthDeg);
	return FVector(
		FMath::Sin(TiltRad) * FMath::Cos(AzimuthRad),
		FMath::Sin(TiltRad) * FMath::Sin(AzimuthRad),
		FMath::Cos(TiltRad));
}

// ==================== 参数变更 ====================

void SOrbitEditorPanel::SetCenterAxis(const int32 Axis, const float Value)
{
	if (!bHasSelection)
	{
		return;
	}
	FVector Location = WorkingData.Transform.GetLocation();
	Location.Component(Axis) = Value;
	WorkingData.Transform.SetLocation(Location);
	PushWorkingDataToManager();
}

void SOrbitEditorPanel::SetTiltDegrees(const float Value)
{
	EditingTiltDeg = Value;
	if (!bHasSelection)
	{
		return;
	}
	WorkingData.Transform.SetRotation(FRotationMatrix::MakeFromZ(MakeNormalFromTiltAzimuth()).Rotator().Quaternion());
	PushWorkingDataToManager();
}

void SOrbitEditorPanel::SetAzimuthDegrees(const float Value)
{
	EditingAzimuthDeg = Value;
	if (!bHasSelection)
	{
		return;
	}
	WorkingData.Transform.SetRotation(FRotationMatrix::MakeFromZ(MakeNormalFromTiltAzimuth()).Rotator().Quaternion());
	PushWorkingDataToManager();
}

void SOrbitEditorPanel::SetOrbitRadius(const float Value)
{
	if (!bHasSelection)
	{
		return;
	}
	WorkingData.Radius = FMath::Max(1.f, Value);
	PushWorkingDataToManager();
}

void SOrbitEditorPanel::SetServiceDistance(const float Value)
{
	if (!bHasSelection)
	{
		return;
	}
	WorkingData.ServiceDistance = FMath::Max(0.f, Value);
	PushWorkingDataToManager();
}

void SOrbitEditorPanel::SetSatelliteCount(const int32 Value)
{
	if (!bHasSelection)
	{
		return;
	}
	WorkingData.SatelliteCount = FMath::Max(0, Value);
	PushWorkingDataToManager();
}

void SOrbitEditorPanel::SetAngularVelocity(const float Value)
{
	if (!bHasSelection)
	{
		return;
	}
	WorkingData.AngularVelocity = Value;
	PushWorkingDataToManager();
}

void SOrbitEditorPanel::SetClockwise(const bool bClockwise)
{
	if (!bHasSelection)
	{
		return;
	}
	WorkingData.bClockwise = bClockwise;
	PushWorkingDataToManager();
}

void SOrbitEditorPanel::ApplyPresetColor(const int32 PresetIndex, const bool bVisual)
{
	if (!bHasSelection)
	{
		return;
	}
	if (bVisual)
	{
		WorkingData.VisualColor = USatelliteManagerComponent::GetPresetVisualColor(PresetIndex);
	}
	else
	{
		WorkingData.CoverageColor = USatelliteManagerComponent::GetPresetCoverageColor(PresetIndex);
	}
	PushWorkingDataToManager();
}

void SOrbitEditorPanel::ApplyPickedColor(const FLinearColor NewColor, const bool bVisual)
{
	if (!bHasSelection)
	{
		return;
	}
	if (bVisual)
	{
		WorkingData.VisualColor = NewColor;
	}
	else
	{
		WorkingData.CoverageColor = NewColor;
	}
	PushWorkingDataToManager();
}

// ==================== 回调 ====================

FReply SOrbitEditorPanel::OnAddOrbitClicked()
{
	ABasePlayerController* PC = Controller.Get();
	if (!PC)
	{
		return FReply::Handled();
	}

	// 新轨道默认参数：基于当前选中（无选中则首条）；调色板按序号自动换色
	FRuntimeOrbitData NewData = WorkingData;
	if (!bHasSelection)
	{
		const TArray<FName> Names = PC->GetOrbitNames();
		if (Names.Num() > 0)
		{
			PC->GetOrbitData(Names[0], NewData);
		}
	}

	// 唯一名称
	int32 Suffix = PC->GetOrbitNames().Num() + 1;
	FName NewName;
	do
	{
		NewName = FName(*FString::Printf(TEXT("Orbit_%02d"), Suffix));
		++Suffix;
	}
	while (PC->GetOrbitNames().Contains(NewName));

	NewData.OrbitName = NewName;
	const int32 PresetIndex = PC->GetOrbitNames().Num();
	NewData.VisualColor = USatelliteManagerComponent::GetPresetVisualColor(PresetIndex);
	NewData.CoverageColor = USatelliteManagerComponent::GetPresetCoverageColor(PresetIndex);
	// 沿法线偏移一点，避免与来源轨道完全重叠
	NewData.Transform.AddToTranslation(NewData.Transform.GetRotation().GetAxisZ() * NewData.Radius * 0.15f);

	if (PC->CreateOrbit(NewData))
	{
		RefreshOrbitList();
		SelectOrbit(NewName);
	}
	return FReply::Handled();
}

FReply SOrbitEditorPanel::OnDeleteOrbitClicked()
{
	ABasePlayerController* PC = Controller.Get();
	if (!PC || !bHasSelection)
	{
		return FReply::Handled();
	}
	// 保护：至少保留一条轨道
	if (PC->GetOrbitNames().Num() <= 1)
	{
		return FReply::Handled();
	}

	PC->DeleteOrbit(SelectedOrbitName);
	SelectedOrbitName = NAME_None;
	bHasSelection = false;
	RefreshOrbitList();
	return FReply::Handled();
}

FReply SOrbitEditorPanel::OnApplyClicked()
{
	if (ABasePlayerController* PC = Controller.Get())
	{
		PC->ApplyOrbitChangesToSatellites();   // 重建卫星实体
	}
	return FReply::Handled();
}

FReply SOrbitEditorPanel::OnCloseClicked()
{
	// 优先交给宿主（UMG 包装等）；未绑定时回退到控制器（Viewport 模式）
	if (OnCloseRequested.IsBound())
	{
		OnCloseRequested.Execute();
		return FReply::Handled();
	}
	if (ABasePlayerController* PC = Controller.Get())
	{
		PC->CloseOrbitEditor();   // Viewport 模式：移除 Viewport 内容 + 退出编辑模式
	}
	return FReply::Handled();
}

void SOrbitEditorPanel::OpenColorPicker(const bool bVisual)
{
	if (!bHasSelection)
	{
		return;
	}

	if (ColorPickerWindow.IsValid())
	{
		ColorPickerWindow->RequestDestroyWindow();
		ColorPickerWindow.Reset();
	}

	ColorPickerWindow = SNew(SWindow)
		.Title(FText::FromString(bVisual ? TEXT("视觉色") : TEXT("覆盖色")))
		.ClientSize(FVector2D(320.f, 420.f))
		.SupportsMinimize(false)
		.SupportsMaximize(false)
		[
			SNew(SColorPicker)
			.UseAlpha(true)
			.DisplayGamma(2.2f)
			.TargetColorAttribute(bVisual ? WorkingData.VisualColor : WorkingData.CoverageColor)
			.OnColorCommitted_Lambda([this, bVisual](FLinearColor NewColor)
			{
				ApplyPickedColor(NewColor, bVisual);
			})
		];

	FSlateApplication::Get().AddWindow(ColorPickerWindow.ToSharedRef());
}

// ==================== UI 构建 ====================

TSharedRef<SWidget> SOrbitEditorPanel::BuildOrbitListSection()
{
	return SNew(SVerticalBox)
		// 标题 + 新建
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 4)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("OrbitListHeader", "轨道列表"))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SButton)
				.Text(LOCTEXT("AddOrbit", "+ 新建"))
				.OnClicked(this, &SOrbitEditorPanel::OnAddOrbitClicked)
			]
		]
		// 列表（滚动，限高）
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SBox)
			.MaxDesiredHeight(120.f)
			[
				SNew(SScrollBox)
				+ SScrollBox::Slot()
				[
					SAssignNew(OrbitListContainer, SVerticalBox)
				]
			]
		]
		// 删除
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 0)
		[
			SNew(SButton)
			.Text(LOCTEXT("DeleteOrbit", "删除选中"))
			.OnClicked(this, &SOrbitEditorPanel::OnDeleteOrbitClicked)
		];
}

TSharedRef<SWidget> SOrbitEditorPanel::MakeParamRow(const FText& Label, const TSharedRef<SWidget>& Content)
{
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
		[
			SNew(SBox)
			.WidthOverride(80.f)
			[
				SNew(STextBlock).Text(Label)
			]
		]
		+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
		[
			Content
		];
}

TSharedRef<SWidget> SOrbitEditorPanel::BuildParamSection()
{
	// 圆心（XYZ 数值框）
	TSharedRef<SWidget> CenterRow = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.f).Padding(2.f, 0.f)
		[
			SNew(SSpinBox<float>)
			.MinValue(-1.0e7f).MaxValue(1.0e7f).MinDesiredWidth(70.f)
			.Value_Lambda([this]() { return bHasSelection ? static_cast<float>(WorkingData.Transform.GetLocation().X) : 0.f; })
			.OnValueChanged_Lambda([this](float Value) { SetCenterAxis(0, Value); })
		]
		+ SHorizontalBox::Slot().FillWidth(1.f).Padding(2.f, 0.f)
		[
			SNew(SSpinBox<float>)
			.MinValue(-1.0e7f).MaxValue(1.0e7f).MinDesiredWidth(70.f)
			.Value_Lambda([this]() { return bHasSelection ? static_cast<float>(WorkingData.Transform.GetLocation().Y) : 0.f; })
			.OnValueChanged_Lambda([this](float Value) { SetCenterAxis(1, Value); })
		]
		+ SHorizontalBox::Slot().FillWidth(1.f).Padding(2.f, 0.f)
		[
			SNew(SSpinBox<float>)
			.MinValue(-1.0e7f).MaxValue(1.0e7f).MinDesiredWidth(70.f)
			.Value_Lambda([this]() { return bHasSelection ? static_cast<float>(WorkingData.Transform.GetLocation().Z) : 0.f; })
			.OnValueChanged_Lambda([this](float Value) { SetCenterAxis(2, Value); })
		];

	TSharedRef<SWidget> TiltRow = SNew(SSpinBox<float>)
		.MinValue(0.f).MaxValue(180.f).MinDesiredWidth(100.f)
		.Value_Lambda([this]() { return EditingTiltDeg; })
		.OnValueChanged_Lambda([this](float Value) { SetTiltDegrees(Value); });

	TSharedRef<SWidget> AzimuthRow = SNew(SSpinBox<float>)
		.MinValue(-180.f).MaxValue(180.f).MinDesiredWidth(100.f)
		.Value_Lambda([this]() { return EditingAzimuthDeg; })
		.OnValueChanged_Lambda([this](float Value) { SetAzimuthDegrees(Value); });

	TSharedRef<SWidget> OrbitRadiusRow = SNew(SSpinBox<float>)
		.MinValue(1.f).MaxValue(1.0e7f).MinDesiredWidth(100.f)
		.Value_Lambda([this]() { return bHasSelection ? WorkingData.Radius : 0.f; })
		.OnValueChanged_Lambda([this](float Value) { SetOrbitRadius(Value); });

	TSharedRef<SWidget> ServiceDistanceRow = SNew(SSpinBox<float>)
		.MinValue(0.f).MaxValue(1.0e7f).MinDesiredWidth(100.f)
		.Value_Lambda([this]() { return bHasSelection ? WorkingData.ServiceDistance : 0.f; })
		.OnValueChanged_Lambda([this](float Value) { SetServiceDistance(Value); });

	TSharedRef<SWidget> SatelliteCountRow = SNew(SSpinBox<int32>)
		.MinValue(0).MaxValue(10000).MinDesiredWidth(100.f)
		.Value_Lambda([this]() { return bHasSelection ? WorkingData.SatelliteCount : 0; })
		.OnValueChanged_Lambda([this](int32 Value) { SetSatelliteCount(Value); });

	TSharedRef<SWidget> AngularVelocityRow = SNew(SSpinBox<float>)
		.MinValue(-360.f).MaxValue(360.f).MinDesiredWidth(100.f)
		.Value_Lambda([this]() { return bHasSelection ? WorkingData.AngularVelocity : 0.f; })
		.OnValueChanged_Lambda([this](float Value) { SetAngularVelocity(Value); });

	TSharedRef<SWidget> ClockwiseRow = SNew(SCheckBox)
		.IsChecked_Lambda([this]() { return (bHasSelection && WorkingData.bClockwise) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
		.OnCheckStateChanged_Lambda([this](ECheckBoxState NewState) { SetClockwise(NewState == ECheckBoxState::Checked); })
		[
			SNew(STextBlock).Text(LOCTEXT("Clockwise", "顺时针"))
		];

	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)[ MakeParamRow(LOCTEXT("Center", "圆心偏移"), CenterRow) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)[ MakeParamRow(LOCTEXT("Tilt", "平面倾角(°)"), TiltRow) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)[ MakeParamRow(LOCTEXT("Azimuth", "平面方位(°)"), AzimuthRow) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)[ MakeParamRow(LOCTEXT("OrbitRadius", "轨道半径"), OrbitRadiusRow) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)[ MakeParamRow(LOCTEXT("ServiceDistance", "服务距离"), ServiceDistanceRow) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)[ MakeParamRow(LOCTEXT("SatelliteCount", "卫星数量"), SatelliteCountRow) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)[ MakeParamRow(LOCTEXT("AngularVelocity", "角速度"), AngularVelocityRow) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)[ MakeParamRow(LOCTEXT("Direction", "运行方向"), ClockwiseRow) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)[ MakeParamRow(LOCTEXT("VisualColor", "视觉色"), BuildColorRow(true)) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)[ MakeParamRow(LOCTEXT("CoverageColor", "覆盖色"), BuildColorRow(false)) ];
}

TSharedRef<SWidget> SOrbitEditorPanel::BuildButtonSection()
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f)
			[
				SNew(SBox)   // 左侧撑开（按钮靠右）
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SButton)
				.Text(LOCTEXT("Apply", "应用（重建卫星）"))
				.OnClicked(this, &SOrbitEditorPanel::OnApplyClicked)
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 0)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("Hint", "参数实时预览；应用后卫星位置将重置"))
			.Font(FCoreStyle::GetDefaultFontStyle("Italic", 9))
			.ColorAndOpacity(FSlateColor(FLinearColor(0.7f, 0.7f, 0.7f)))
		];
}

TSharedRef<SWidget> SOrbitEditorPanel::MakeOrbitRow(const FName Name)
{
	FLinearColor VisualColor = FLinearColor::White;
	FLinearColor CoverageColor = FLinearColor::White;
	if (ABasePlayerController* PC = Controller.Get())
	{
		FRuntimeOrbitData Data;
		if (PC->GetOrbitData(Name, Data))
		{
			VisualColor = Data.VisualColor;
			CoverageColor = Data.CoverageColor;
		}
	}

	const bool bSelected = (Name == SelectedOrbitName);
	return SNew(SButton)
		.ButtonColorAndOpacity(bSelected ? FLinearColor(0.12f, 0.32f, 0.50f, 1.f) : FLinearColor(0.10f, 0.10f, 0.10f, 1.f))
		.ContentPadding(4.f)
		.OnClicked_Lambda([this, Name]() { SelectOrbit(Name); return FReply::Handled(); })
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				SNew(STextBlock).Text(FText::FromName(Name))
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(4.f, 0.f).VAlign(VAlign_Center)
			[
				SNew(SBox).WidthOverride(16.f).HeightOverride(14.f)
				[
					SNew(SColorBlock).Color(VisualColor)
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SBox).WidthOverride(16.f).HeightOverride(14.f)
				[
					SNew(SColorBlock).Color(CoverageColor)
				]
			]
		];
}

TSharedRef<SWidget> SOrbitEditorPanel::BuildColorRow(const bool bVisual)
{
	TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);

	// 当前色块（点击打开取色器）
	Row->AddSlot().AutoWidth().Padding(2.f, 0.f)
	[
		SNew(SButton)
		.ContentPadding(0.f)
		.ToolTipText(LOCTEXT("OpenColorPicker", "点击打开取色器"))
		.OnClicked_Lambda([this, bVisual]() { OpenColorPicker(bVisual); return FReply::Handled(); })
		[
			SNew(SBox).WidthOverride(30.f).HeightOverride(16.f)
			[
				SNew(SColorBlock)
				.Color_Lambda([this, bVisual]()
				{
					if (!bHasSelection)
					{
						return FLinearColor(0.4f, 0.4f, 0.4f);
					}
					return bVisual ? WorkingData.VisualColor : WorkingData.CoverageColor;
				})
			]
		]
	];

	// 调色板快选
	const int32 PresetCount = USatelliteManagerComponent::GetColorPresetCount();
	for (int32 Index = 0; Index < PresetCount; ++Index)
	{
		const FLinearColor PresetColor = bVisual
			? USatelliteManagerComponent::GetPresetVisualColor(Index)
			: USatelliteManagerComponent::GetPresetCoverageColor(Index);

		Row->AddSlot().AutoWidth().Padding(2.f, 0.f)
		[
			SNew(SButton)
			.ContentPadding(0.f)
			.OnClicked_Lambda([this, Index, bVisual]() { ApplyPresetColor(Index, bVisual); return FReply::Handled(); })
			[
				SNew(SBox).WidthOverride(20.f).HeightOverride(16.f)
				[
					SNew(SColorBlock).Color(PresetColor)
				]
			]
		];
	}

	return Row;
}

// ==================== UOrbitEditorWidget（UMG 包装） ====================

TSharedRef<SWidget> UOrbitEditorWidget::RebuildWidget()
{
	ABasePlayerController* PlayerController = Cast<ABasePlayerController>(GetOwningPlayer());

	OrbitPanel = SNew(SOrbitEditorPanel)
		.PlayerController(PlayerController)
		.OnCloseRequested(FSimpleDelegate::CreateWeakLambda(this, [this]()
		{
			// × 按钮：收起控件 + 退出编辑模式（内置行为）
			SetVisibility(ESlateVisibility::Collapsed);
			if (ABasePlayerController* OwningPC = Cast<ABasePlayerController>(GetOwningPlayer()))
			{
				OwningPC->ExitOrbitEditMode();
			}
		}));

	return OrbitPanel.ToSharedRef();
}

void UOrbitEditorWidget::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	OrbitPanel.Reset();
}

void UOrbitEditorWidget::EnterOrbitEditMode()
{
	if (ABasePlayerController* PC = Cast<ABasePlayerController>(GetOwningPlayer()))
	{
		PC->EnterOrbitEditMode();
	}
}

void UOrbitEditorWidget::ExitOrbitEditMode()
{
	if (ABasePlayerController* PC = Cast<ABasePlayerController>(GetOwningPlayer()))
	{
		PC->ExitOrbitEditMode();
	}
}

#undef LOCTEXT_NAMESPACE
