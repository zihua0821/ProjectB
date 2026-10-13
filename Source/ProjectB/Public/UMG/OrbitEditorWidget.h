// OrbitEditorWidget.h
// 轨道编辑界面（一体两件）：Slate 面板（SOrbitEditorPanel）+ UMG 包装控件（UOrbitEditorWidget）
// - UMG 用法：蓝图中放置 UOrbitEditorWidget（承载整个面板内容）
// - 纯 Runtime（Slate + UMG）；不依赖任何编辑器模块
#pragma once

#include "CoreMinimal.h"
#include "Components/Widget.h"
#include "Mass/OrbitTypes.h"                     // FOrbitDefinition（WorkingData 值成员需完整类型）
#include "Widgets/SCompoundWidget.h"
#include "OrbitEditorWidget.generated.h"

class ABasePlayerController;
class USatelliteManagerComponent;
class SVerticalBox;

/**
 * 轨道编辑面板（Slate）
 * - 轨道列表（新建/删除/选中）+ 参数编辑（实时预览）+ 应用（重建卫星）
 * - 数据经 ABasePlayerController 接口转发（UpdateOrbitData / CreateOrbit / RebuildOrbitEntities...）
 * - 颜色：调色板快选 + SColorPicker 微调（独立数值模式，非编辑器属性系统）
 */
class SOrbitEditorPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SOrbitEditorPanel) {}
		SLATE_ARGUMENT(TWeakObjectPtr<ABasePlayerController>, PlayerController)
		/** 关闭请求（× 按钮触发）；由宿主处理（UMG 包装收起自身等） */
		SLATE_EVENT(FSimpleDelegate, OnCloseRequested)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SOrbitEditorPanel() override;

private:
	// ---- 数据 ----

	/** 宿主控制器（提供全部轨道接口） */
	TWeakObjectPtr<ABasePlayerController> Controller;

	/** 关闭请求（× 按钮；由宿主绑定处理——Construct 时从 InArgs 保存） */
	FSimpleDelegate OnCloseRequested;

	/** 轨道名列表（面板列表） */
	TArray<FName> OrbitNames;

	/** 当前选中轨道名 */
	FName SelectedOrbitName = NAME_None;

	/** 编辑副本（控件真值源；变更实时推送到运行时数据） */
	FOrbitDefinition WorkingData;

	/** 拖动统一节拍：最近一次更新时刻（Slate 秒） */
	double LastThrottledUpdateTime = 0.0;

	/** 颜色轻节拍：最近一次推送时刻（Slate 秒） */
	double LastColorPushTime = 0.0;

	/** 是否有有效选中 */
	bool bHasSelection = false;

	/** 朝向编辑状态（三自由度：绕 X / 绕 Y 定平面朝向；相位为绕法线的自转，与轨道定义 FTransform 对齐） */
	float EditingAngleXDeg = 0.f;
	float EditingAngleYDeg = 0.f;
	float EditingPhaseDeg = 0.f;

	// ---- 控件 ----

	TSharedPtr<SVerticalBox> OrbitListContainer;
	TSharedPtr<SWindow> ColorPickerWindow;

	// ---- 内部逻辑 ----

	/** 拉取轨道名 + 重建列表 + 选中兜底 */
	void RefreshOrbitList();

	/** 仅重建列表行（选中态高亮） */
	void RebuildOrbitListRows();

	/** 选中轨道（加载数据 + 反算倾角方位） */
	void SelectOrbit(const FName Name);

	/** 推送编辑副本到运行时数据（纯推送——频率由节拍器保证；事件驱动可视化直接刷新） */
	void PushWorkingDataToManager();

	/** 提交编辑（参数控件的回车/失焦落定）：先推送数据（最终值必达）再重建该轨实体——所见即所得 */
	void CommitEdit();

	/** 拖动中统一节拍（值变化时调用；最多 0.2s 一次）：到点依次推送数据 + 提交重建
	 *  （源头单节流——下游组件/管理器零节流）；松手/回车的提交回调做最终落实 */
	void RequestThrottledUpdate();

	/** 颜色轻节拍（拖动色轮高频；最多 0.2s 一次）：只推送数据（可视化直接刷新）
	 *  ——颜色为可视化属性，不触发实体重建（防卫星相位/作业被重置） */
	void RequestThrottledPush();

	/** 由三自由度（绕 X / 绕 Y / 相位）构造轨道朝向四元数（与 SelectOrbit 反算互逆） */
	FQuat MakeOrbitRotation() const;

	// ---- 参数变更 ----

	void SetCenterAxis(const int32 Axis, const float Value);
	void SetAngleXDegrees(const float Value);
	void SetAngleYDegrees(const float Value);
	void SetPhaseDegrees(const float Value);
	void SetOrbitRadius(const float Value);
	void SetServiceDistance(const float Value);
	void SetSatelliteCount(const int32 Value);
	void SetAngularVelocity(const float Value);
	void SetClockwise(const bool bClockwise);
	void ApplyPickedColor(const FLinearColor NewColor, const bool bVisual);

	// ---- 回调 ----

	FReply OnAddOrbitClicked();
	FReply OnDeleteOrbitClicked();
	FReply OnCloseClicked();
	void OpenColorPicker(const bool bVisual);

	// ---- UI 构建 ----

	TSharedRef<SWidget> BuildOrbitListSection();
	TSharedRef<SWidget> BuildParamSection();
	TSharedRef<SWidget> BuildButtonSection();
	TSharedRef<SWidget> MakeOrbitRow(const FName Name);
	TSharedRef<SWidget> MakeParamRow(const FText& Label, const TSharedRef<SWidget>& Content);
	TSharedRef<SWidget> BuildColorRow(const bool bVisual);
};

/**
 * 轨道编辑面板（UMG 包装）
 * 用法：
 *   1. UMG 蓝图中放置本控件（默认隐藏）
 *   2. 打开：SetVisibility(Visible) + EnterOrbitEditMode()
 *   3. 关闭：× 按钮（内置收起 + Exit）或手动 ExitOrbitEditMode()
 */
UCLASS()
class PROJECTB_API UOrbitEditorWidget : public UWidget
{
	GENERATED_BODY()

public:
	/** 进入轨道编辑模式（开启覆盖可视化 + 悬停互斥）——蓝图在显示控件时调用 */
	UFUNCTION(BlueprintCallable, Category = "OrbitEditor")
	void EnterOrbitEditMode();

	/** 退出轨道编辑模式 */
	UFUNCTION(BlueprintCallable, Category = "OrbitEditor")
	void ExitOrbitEditMode();

	virtual void ReleaseSlateResources(bool bReleaseChildren) override;

protected:
	// UWidget interface
	/** UMG 构建回调：首次 TakeWidget 时被调用（惰性构建）；此后缓存复用不再调用——缓存失效时会再次调用（故名为 Rebuild） */
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	/** Slate 面板实例 */
	TSharedPtr<SOrbitEditorPanel> OrbitPanel;
};
