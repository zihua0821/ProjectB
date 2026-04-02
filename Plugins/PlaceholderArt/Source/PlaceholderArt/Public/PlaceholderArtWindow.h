#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Input/SSpinBox.h"


class SObjectPropertyEntryBox;
class SColorBlock;

class SPlaceholderArtWindow : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SPlaceholderArtWindow)
	{}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	// ----- 布局函数 -----

	// 主布局
	TSharedRef<SWidget> MakeMainLayout();

	// 模型布局
	TSharedRef<SWidget> MakeMeshSection();

	// 文本布局
	TSharedRef<SWidget> MakeTextSection();

	// 路径布局
	TSharedRef<SWidget> MakePathSection();

	// 按钮布局
	TSharedRef<SWidget> MakeButtonSection();
    
	// ----- 事件处理 -----

	// 确认按钮点击事件
	FReply OnConfirmButtonClicked();

	// ----- 参数更新 -----

	// 获取BaseColor
	FLinearColor GetBaseColor() const { return BaseColor;}

	// 获取TextColor
	FLinearColor GetTextColor() const { return TextColor;}

	// 获取字体路径
	FString GetSelectedFontPath() const;
	
	// ----- 内部操作 -----

	// 资产生成
	bool GeneratePlaceholderAsset();
	
	// 纹理生成
	UTexture* CreateTextTexture(const FString& TargetPath, const FString& AssetName);

	// 材质生成
	UMaterialInstanceConstant* CreateTextMaterial(const FString& TargetPath, const FString& AssetName, UTexture* InTexture);
	
	// 路径验证
	bool ValidatePath(const FString& InPath, FText& OutErrorMessage) const;
    
	// 获取引擎预置模型
	UStaticMesh* GetEngineBaseMesh(const FString& MeshType) const;

	// 复制资源到指定路径
	bool CopyAssetToPath(UObject* SourceAsset, const FString& TargetPath, const FString& AssetName);

private:
	// ----- 数据成员  -----

	// 模型选择文字下拉框
	TSharedPtr<STextComboBox> MeshTypeComboBox;

	// 模型选择文字下拉框选项
	TArray<TSharedPtr<FString>> MeshTypeOptions;

	// 基础颜色
	FLinearColor BaseColor = FLinearColor::White;

	// 文字颜色
	FLinearColor TextColor = FLinearColor::Black;
	
	// 颜色选择器窗口引用
	TSharedPtr<SWindow> ColorPickerWindow;

	// 文本输入框
	TSharedPtr<SEditableTextBox> PlaceholderTextWidget;

	// 字体大小输入框
	TSharedPtr<SSpinBox<int32>> TextSizeWidget;

	// 文件路径输入框
	TSharedPtr<SEditableTextBox> FilePathWidget;

	// 文件名输入框
	TSharedPtr<SEditableTextBox> FileNameWidget;

	// 字体对象
	TObjectPtr<UFont> SelectedFont;

	// 字体选择器窗口引用
	TSharedPtr<SObjectPropertyEntryBox> FontPickerWidget;
};


