// OrbitEditorWidget.cpp
// 轨道编辑界面实现：Slate 面板（SOrbitEditorPanel）+ UMG 包装控件（UOrbitEditorWidget）
#include "UMG/OrbitEditorWidget.h"

#include "Mass/SatelliteManagerComponent.h"
#include "Player/BasePlayerController.h"

#include "Framework/Application/SlateApplication.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Colors/SColorBlock.h"
#include "Widgets/Colors/SColorPicker.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	/** 轴标签（UE 编辑器惯例配色：X 红 / Y 绿 / Z 蓝） */
	TSharedRef<SWidget> MakeAxisLabel(const TCHAR* AxisName, const FLinearColor& Color)
	{
		return SNew(STextBlock)
			.Text(FText::FromString(AxisName))
			.ColorAndOpacity(FSlateColor(Color))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9));
	}
}

#define LOCTEXT_NAMESPACE "OrbitEditor"

// ==================== SOrbitEditorPanel（Slate 面板） ====================

void SOrbitEditorPanel::Construct(const FArguments& InArgs)
{
	Controller = InArgs._PlayerController;
	OnCloseRequested = InArgs._OnCloseRequested;

	ChildSlot
	[
		// 右上角对齐 + 固定宽度；VAlign_Fill 让背景铺满槽位高度（避免下方内容“溢出无背景”）
		SNew(SBox)
		.HAlign(HAlign_Right)
		.VAlign(VAlign_Fill)
		.Padding(16.f)
		[
			SNew(SBox)
			.WidthOverride(460.f)
			.MinDesiredHeight(520.f)   // 面板所需最小高度（自适应槽位可据此撑开；固定槽位请≥此值）
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

	// 空列表占位提示
	if (OrbitNames.Num() == 0)
	{
		OrbitListContainer->AddSlot().AutoHeight().Padding(6.f, 6.f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("NoOrbit", "（无轨道）"))
			.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f)))
		];
		return;
	}

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

	// 反算朝向三自由度（与 MakeOrbitRotation 互逆）
	// 构造：Q = Ry(AY) · Rx(AX) · Qz(Phase)（Qz 绕旋转后的本地 Z；Q 的 Z 轴即平面法线）
	// 法线 N = Ry(AY)·Rx(AX)·Z = (sinAY·cosAX, −sinAX, cosAY·cosAX)
	const FQuat Rotation = Data.Transform.GetRotation();
	const FVector Normal = Rotation.GetAxisZ();
	EditingAngleXDeg = FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(-Normal.Y, -1.f, 1.f)));
	EditingAngleYDeg = FMath::RadiansToDegrees(FMath::Atan2(Normal.X, Normal.Z));

	// 相位 = 相对基准姿态（Ry·Rx）的剩余绕法线自转
	const FQuat BaseRotation = FQuat(FVector::YAxisVector, FMath::DegreesToRadians(EditingAngleYDeg))
		* FQuat(FVector::XAxisVector, FMath::DegreesToRadians(EditingAngleXDeg));
	const FQuat RelativeRotation = BaseRotation.Inverse() * Rotation;
	FVector PhaseAxis;
	float PhaseAngleRad = 0.f;
	RelativeRotation.ToAxisAndAngle(PhaseAxis, PhaseAngleRad);
	const float PhaseSign = (PhaseAxis.Z >= 0.f) ? 1.f : -1.f;
	EditingPhaseDeg = FMath::UnwindDegrees(FMath::RadiansToDegrees(PhaseAngleRad) * PhaseSign);

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

void SOrbitEditorPanel::CommitOrbitChanges()
{
	// 提交落定（回车/失焦/拖动节流）→ 只重建【当前选中轨道】：所见即所得（选定暂停中执行，安全）
	if (!bHasSelection)
	{
		return;
	}
	if (ABasePlayerController* PC = Controller.Get())
	{
		PC->CommitOrbitChanges(WorkingData.OrbitName);
	}
}

void SOrbitEditorPanel::RequestThrottledRebuild()
{
	// 拖动中"节流重建"：最少间隔 0.2s——拖动时也能看到卫星近似实时更新
	// （松手的 OnValueCommitted 提交回调做最终落实；打字不触发值变化回调，无此路径）
	constexpr double ThrottledRebuildIntervalSeconds = 0.2;
	const double Now = FSlateApplication::Get().GetCurrentTime();
	if (Now - LastThrottledRebuildTime < ThrottledRebuildIntervalSeconds)
	{
		return;
	}
	LastThrottledRebuildTime = Now;
	CommitOrbitChanges();
}

FQuat SOrbitEditorPanel::MakeOrbitRotation() const
{
	// 平面朝向：先绕 X 再绕 Y（正交两倾斜角）；相位：绕旋转后的本地 Z（= 平面法线）自转
	const FQuat BaseRotation = FQuat(FVector::YAxisVector, FMath::DegreesToRadians(EditingAngleYDeg))
		* FQuat(FVector::XAxisVector, FMath::DegreesToRadians(EditingAngleXDeg));
	const FQuat PhaseRotation(FVector::ZAxisVector, FMath::DegreesToRadians(EditingPhaseDeg));
	return BaseRotation * PhaseRotation;
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

void SOrbitEditorPanel::SetAngleXDegrees(const float Value)
{
	EditingAngleXDeg = Value;
	if (!bHasSelection)
	{
		return;
	}
	WorkingData.Transform.SetRotation(MakeOrbitRotation());
	PushWorkingDataToManager();
}

void SOrbitEditorPanel::SetAngleYDegrees(const float Value)
{
	EditingAngleYDeg = Value;
	if (!bHasSelection)
	{
		return;
	}
	WorkingData.Transform.SetRotation(MakeOrbitRotation());
	PushWorkingDataToManager();
}

void SOrbitEditorPanel::SetPhaseDegrees(const float Value)
{
	EditingPhaseDeg = Value;
	if (!bHasSelection)
	{
		return;
	}
	WorkingData.Transform.SetRotation(MakeOrbitRotation());
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
	// 颜色继承来源轨道（字段默认值）；需要区分时用取色器手动调整
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
	PC->DeleteOrbit(SelectedOrbitName);
	SelectedOrbitName = NAME_None;
	bHasSelection = false;
	RefreshOrbitList();
	return FReply::Handled();
}

FReply SOrbitEditorPanel::OnCloseClicked()
{
	// 关闭请求交给宿主（UMG 包装：收起自身 + 退出编辑模式）
	if (OnCloseRequested.IsBound())
	{
		OnCloseRequested.Execute();
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
		.ClientSize(SColorPicker::DEFAULT_WINDOW_SIZE)   // 引擎推荐的取色器窗口尺寸（避免调色盘被压扁）
		.SupportsMinimize(false)
		.SupportsMaximize(false)
		[
			SNew(SColorPicker)
			.UseAlpha(true)
			.DisplayGamma(2.2f)
			.ExpandAdvancedSection(true)                 // 高级区默认展开
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
		// 标题 + 操作按钮（新建 / 删除并排）
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 4)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("OrbitListHeader", "轨道列表"))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0)
			[
				SNew(SButton)
				.Text(LOCTEXT("AddOrbit", "+ 新建"))
				.OnClicked(this, &SOrbitEditorPanel::OnAddOrbitClicked)
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SButton)
				.Text(LOCTEXT("DeleteOrbit", "删除"))
				.IsEnabled_Lambda([this]() { return bHasSelection; })
				.OnClicked(this, &SOrbitEditorPanel::OnDeleteOrbitClicked)
			]
		]
		// 列表（滚动；固定 4 条高度：背景恒定不随条目数抖动，超出滚动）
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SBox)
			.HeightOverride(104.f)   // 4 × 行高(≈26)：行高含 ContentPadding 与 slot 间距，可按需微调
			[
				SNew(SScrollBox)
				+ SScrollBox::Slot()
				[
					SAssignNew(OrbitListContainer, SVerticalBox)
				]
			]
		];
}

TSharedRef<SWidget> SOrbitEditorPanel::MakeParamRow(const FText& Label, const TSharedRef<SWidget>& Content)
{
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
		[
			SNew(SBox)
			.WidthOverride(100.f)
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
	// 圆心（带轴标签：X 红 / Y 绿 / Z 蓝，与 UE 编辑器惯例一致）
	TSharedRef<SWidget> CenterRow = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.f).Padding(2.f, 0.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 3.f, 0.f)
			[
				MakeAxisLabel(TEXT("X"), FLinearColor(0.90f, 0.15f, 0.15f))
			]
			+ SHorizontalBox::Slot().FillWidth(1.f)
			[
				SNew(SSpinBox<float>)
				.MinValue(-200000.f).MaxValue(200000.f).MinDesiredWidth(70.f)
				.MinFractionalDigits(0).MaxFractionalDigits(2)
				.Delta(10.f)
				.Value_Lambda([this]() { return bHasSelection ? static_cast<float>(WorkingData.Transform.GetLocation().X) : 0.f; })
				.OnValueChanged_Lambda([this](float Value) { SetCenterAxis(0, Value); RequestThrottledRebuild(); })
				.OnValueCommitted_Lambda([this](float Value, ETextCommit::Type) { SetCenterAxis(0, Value); CommitOrbitChanges(); })
			]
		]
		+ SHorizontalBox::Slot().FillWidth(1.f).Padding(2.f, 0.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 3.f, 0.f)
			[
				MakeAxisLabel(TEXT("Y"), FLinearColor(0.20f, 0.85f, 0.20f))
			]
			+ SHorizontalBox::Slot().FillWidth(1.f)
			[
				SNew(SSpinBox<float>)
				.MinValue(-200000.f).MaxValue(200000.f).MinDesiredWidth(70.f)
				.MinFractionalDigits(0).MaxFractionalDigits(2)
				.Delta(10.f)
				.Value_Lambda([this]() { return bHasSelection ? static_cast<float>(WorkingData.Transform.GetLocation().Y) : 0.f; })
				.OnValueChanged_Lambda([this](float Value) { SetCenterAxis(1, Value); RequestThrottledRebuild(); })
				.OnValueCommitted_Lambda([this](float Value, ETextCommit::Type) { SetCenterAxis(1, Value); CommitOrbitChanges(); })
			]
		]
		+ SHorizontalBox::Slot().FillWidth(1.f).Padding(2.f, 0.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 3.f, 0.f)
			[
				MakeAxisLabel(TEXT("Z"), FLinearColor(0.25f, 0.45f, 1.00f))
			]
			+ SHorizontalBox::Slot().FillWidth(1.f)
			[
				SNew(SSpinBox<float>)
				.MinValue(-200000.f).MaxValue(200000.f).MinDesiredWidth(70.f)
				.MinFractionalDigits(0).MaxFractionalDigits(2)
				.Delta(10.f)
				.Value_Lambda([this]() { return bHasSelection ? static_cast<float>(WorkingData.Transform.GetLocation().Z) : 0.f; })
				.OnValueChanged_Lambda([this](float Value) { SetCenterAxis(2, Value); RequestThrottledRebuild(); })
				.OnValueCommitted_Lambda([this](float Value, ETextCommit::Type) { SetCenterAxis(2, Value); CommitOrbitChanges(); })
			]
		];

	TSharedRef<SWidget> AngleXRow = SNew(SSpinBox<float>)
		.MinValue(-180.f).MaxValue(180.f).MinDesiredWidth(100.f)
		.MinFractionalDigits(0).MaxFractionalDigits(2)
		.Value_Lambda([this]() { return EditingAngleXDeg; })
		.OnValueChanged_Lambda([this](float Value) { SetAngleXDegrees(Value); RequestThrottledRebuild(); })
		.OnValueCommitted_Lambda([this](float Value, ETextCommit::Type) { SetAngleXDegrees(Value); CommitOrbitChanges(); });

	TSharedRef<SWidget> AngleYRow = SNew(SSpinBox<float>)
		.MinValue(-180.f).MaxValue(180.f).MinDesiredWidth(100.f)
		.MinFractionalDigits(0).MaxFractionalDigits(2)
		.Value_Lambda([this]() { return EditingAngleYDeg; })
		.OnValueChanged_Lambda([this](float Value) { SetAngleYDegrees(Value); RequestThrottledRebuild(); })
		.OnValueCommitted_Lambda([this](float Value, ETextCommit::Type) { SetAngleYDegrees(Value); CommitOrbitChanges(); });

	TSharedRef<SWidget> PhaseRow = SNew(SSpinBox<float>)
		.MinValue(-180.f).MaxValue(180.f).MinDesiredWidth(100.f)
		.MinFractionalDigits(0).MaxFractionalDigits(2)
		.Value_Lambda([this]() { return EditingPhaseDeg; })
		.OnValueChanged_Lambda([this](float Value) { SetPhaseDegrees(Value); RequestThrottledRebuild(); })
		.OnValueCommitted_Lambda([this](float Value, ETextCommit::Type) { SetPhaseDegrees(Value); CommitOrbitChanges(); });

	TSharedRef<SWidget> OrbitRadiusRow = SNew(SSpinBox<float>)
		.MinValue(1000.f).MaxValue(100000.f).MinDesiredWidth(100.f)
		.MinFractionalDigits(0).MaxFractionalDigits(2)
		.Delta(100.f)
		.Value_Lambda([this]() { return bHasSelection ? WorkingData.Radius : 0.f; })
		.OnValueChanged_Lambda([this](float Value) { SetOrbitRadius(Value); RequestThrottledRebuild(); })
		.OnValueCommitted_Lambda([this](float Value, ETextCommit::Type) { SetOrbitRadius(Value); CommitOrbitChanges(); });

	TSharedRef<SWidget> ServiceDistanceRow = SNew(SSpinBox<float>)
		.MinValue(100.f).MaxValue(5000.f).MinDesiredWidth(100.f)
		.MinFractionalDigits(0).MaxFractionalDigits(2)
		.Delta(10.f)
		.Value_Lambda([this]() { return bHasSelection ? WorkingData.ServiceDistance : 0.f; })
		.OnValueChanged_Lambda([this](float Value) { SetServiceDistance(Value); RequestThrottledRebuild(); })
		.OnValueCommitted_Lambda([this](float Value, ETextCommit::Type) { SetServiceDistance(Value); CommitOrbitChanges(); });

	TSharedRef<SWidget> SatelliteCountRow = SNew(SSpinBox<int32>)
		.MinValue(1).MaxValue(50).MinDesiredWidth(100.f)
		.Value_Lambda([this]() { return bHasSelection ? WorkingData.SatelliteCount : 0; })
		.OnValueChanged_Lambda([this](int32 Value) { SetSatelliteCount(Value); RequestThrottledRebuild(); })
		.OnValueCommitted_Lambda([this](int32 Value, ETextCommit::Type) { SetSatelliteCount(Value); CommitOrbitChanges(); });

	TSharedRef<SWidget> AngularVelocityRow = SNew(SSpinBox<float>)
		.MinValue(1.f).MaxValue(100.f).MinDesiredWidth(100.f)
		.MinFractionalDigits(0).MaxFractionalDigits(2)
		.Delta(1.f)
		.Value_Lambda([this]() { return bHasSelection ? WorkingData.AngularVelocity : 0.f; })
		.OnValueChanged_Lambda([this](float Value) { SetAngularVelocity(Value); RequestThrottledRebuild(); })
		.OnValueCommitted_Lambda([this](float Value, ETextCommit::Type) { SetAngularVelocity(Value); CommitOrbitChanges(); });

	TSharedRef<SWidget> ClockwiseRow = SNew(SCheckBox)
		.IsChecked_Lambda([this]() { return (bHasSelection && WorkingData.bClockwise) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
		.OnCheckStateChanged_Lambda([this](ECheckBoxState NewState) { SetClockwise(NewState == ECheckBoxState::Checked); CommitOrbitChanges(); })
		[
			SNew(STextBlock).Text(LOCTEXT("Clockwise", "顺时针"))
		];

	return SNew(SVerticalBox)
		.IsEnabled_Lambda([this]() { return bHasSelection; })   // 无选中时整块禁用（灰显）
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)[ MakeParamRow(LOCTEXT("Center", "圆心偏移 (cm)"), CenterRow) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)[ MakeParamRow(LOCTEXT("AngleX", "绕 X 旋转(°)"), AngleXRow) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)[ MakeParamRow(LOCTEXT("AngleY", "绕 Y 旋转(°)"), AngleYRow) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)[ MakeParamRow(LOCTEXT("Phase", "轨道相位(°)"), PhaseRow) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)[ MakeParamRow(LOCTEXT("OrbitRadius", "轨道半径 (cm)"), OrbitRadiusRow) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)[ MakeParamRow(LOCTEXT("ServiceDistance", "服务距离 (cm)"), ServiceDistanceRow) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)[ MakeParamRow(LOCTEXT("SatelliteCount", "卫星数量"), SatelliteCountRow) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)[ MakeParamRow(LOCTEXT("AngularVelocity", "角速度 (°/s)"), AngularVelocityRow) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)[ MakeParamRow(LOCTEXT("Direction", "运行方向"), ClockwiseRow) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)[ MakeParamRow(LOCTEXT("VisualColor", "视觉色"), BuildColorRow(true)) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)[ MakeParamRow(LOCTEXT("CoverageColor", "覆盖色"), BuildColorRow(false)) ];
}

TSharedRef<SWidget> SOrbitEditorPanel::BuildButtonSection()
{
	// 水平盒：左侧提示（FillWidth → 有限宽度，窄时自动换行）+ 右侧应用按钮
	// （行高由按钮撑起，小字号中文不会下溢出；Fill 槽约束保证文字不会横溢出）
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center).Padding(0.f, 0.f, 8.f, 0.f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("Hint", "参数实时预览；回车/失焦后实时重建（所见即所得）"))
			// 不用斜体：中文无斜体字形时 Slate 走合成倾斜，度量与绘制不一致会导致文字溢出背景
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
			.ColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.6f, 0.6f)))
			.AutoWrapText(true)   // 自动换行（在 Fill 槽的有限宽度内折行）
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
				// 纯色正方形（无边框；宽度 = 高度；显式 Fill 保证铺满）
				SNew(SBox).WidthOverride(16.f).HeightOverride(16.f)
				.HAlign(HAlign_Fill).VAlign(VAlign_Fill)
				[
					SNew(SImage)
					.Image(FCoreStyle::Get().GetBrush("WhiteBrush"))
					.ColorAndOpacity(FSlateColor(VisualColor))
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SBox).WidthOverride(16.f).HeightOverride(16.f)
				.HAlign(HAlign_Fill).VAlign(VAlign_Fill)
				[
					SNew(SImage)
					.Image(FCoreStyle::Get().GetBrush("WhiteBrush"))
					.ColorAndOpacity(FSlateColor(CoverageColor))
				]
			]
		];
}

TSharedRef<SWidget> SOrbitEditorPanel::BuildColorRow(const bool bVisual)
{
	// 正方形纯色块（宽 = 高；点击打开取色器）
	// 外包 AutoWidth 槽：MakeParamRow 的内容槽为 FillWidth（会强制拉伸子控件——WidthOverride 会被忽略）
	// 用 NoBorder 按钮样式：去掉按钮默认背景的边缘（否则色块两侧会露出深色边界）
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			SNew(SBox)
			.WidthOverride(20.f)
			.HeightOverride(20.f)
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Fill)
			[
				SNew(SButton)
				.ButtonStyle(FCoreStyle::Get(), "NoBorder")
				.ContentPadding(0.f)
				.ToolTipText(LOCTEXT("OpenColorPicker", "点击打开取色器"))
				.OnClicked_Lambda([this, bVisual]() { OpenColorPicker(bVisual); return FReply::Handled(); })
				[
					SNew(SImage)
					.Image(FCoreStyle::Get().GetBrush("WhiteBrush"))
					.ColorAndOpacity_Lambda([this, bVisual]()
					{
						if (!bHasSelection)
						{
							return FSlateColor(FLinearColor(0.4f, 0.4f, 0.4f));
						}
						return FSlateColor(bVisual ? WorkingData.VisualColor : WorkingData.CoverageColor);
					})
				]
			]
		]
		+ SHorizontalBox::Slot().FillWidth(1.f)   // 右侧撑开（色块左对齐）
		[
			SNew(SBox)
		];
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
