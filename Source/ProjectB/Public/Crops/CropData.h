#pragma once

#include "CoreMinimal.h"
#include "CropData.generated.h"

class UCropDataAsset;

/**
 * 地块状态位掩码枚举
 * 使用位操作可以同时拥有多个状态
 */
UENUM(BlueprintType, meta = (Bitflags, UseEnumValuesAsMaskValuesInEditor = "true"))
enum class EPlotState : uint8
{
    // 基础状态
    Empty          = 0      UMETA(DisplayName = "空地"),        // 0000 0000
    Plowed         = 1 << 0 UMETA(DisplayName = "已耕地"),      // 0000 0001
    Watered        = 1 << 1 UMETA(DisplayName = "已灌溉"),      // 0000 0010
    Fertilized     = 1 << 2 UMETA(DisplayName = "已施肥"),      // 0000 0100
    WeedInfested   = 1 << 3 UMETA(DisplayName = "有杂草"),      // 0000 1000
    PestInfested   = 1 << 4 UMETA(DisplayName = "有虫害"),      // 0001 0000
    
};
ENUM_CLASS_FLAGS(EPlotState); 

/**
 * 地块状态数据结构
 */
USTRUCT(BlueprintType)
struct FPlotStateData
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PlotState")
    EPlotState StateFlags = EPlotState::Empty;

    bool IsEmpty() const
    {
        return StateFlags == EPlotState::Empty;
    }
    
    bool IsValidSingleState()
    {
        // 空状态不算单一状态（如果需要允许空状态，可以移除这个检查）
        if (StateFlags == EPlotState::Empty)
        {
            return false;
        }

        // 使用位运算技巧：如果一个数是2的幂（只有一个位为1），
        // 那么 (n & (n - 1)) == 0
        const uint8 Value = static_cast<uint8>(StateFlags);
        return (Value & (Value - 1)) == 0;
    }
    
    bool HasState(const EPlotState& State) const
    {
        return (StateFlags & State) != EPlotState::Empty;
    }
    
    void AddState(const EPlotState& State)
    {
        StateFlags |= State;
    }

    void RemoveState(const EPlotState& State)
    {
        StateFlags &= ~State;
    }

    void ToggleState(const EPlotState& State)
    {
        StateFlags ^= State;
    }
    
    FString ToString() const
    {
        // 特殊处理：完全空状态
        if (StateFlags == EPlotState::Empty)
        {
            return TEXT("空地");
        }

        TArray<FString> ActiveStates;
        
        if (HasState(EPlotState::Plowed))       ActiveStates.Add(TEXT("已耕地"));
        if (HasState(EPlotState::Watered))      ActiveStates.Add(TEXT("已灌溉"));
        if (HasState(EPlotState::Fertilized))   ActiveStates.Add(TEXT("已施肥"));
        if (HasState(EPlotState::WeedInfested)) ActiveStates.Add(TEXT("有杂草"));
        if (HasState(EPlotState::PestInfested)) ActiveStates.Add(TEXT("有虫害"));

        // 拼接结果
        FString Result;
        for (int32 i = 0; i < ActiveStates.Num(); ++i)
        {
            if (i > 0) Result.Append(TEXT(" | "));
            Result.Append(ActiveStates[i]);
        }
        return Result;
    }
};

/**
 *  FarmLand 地块数据结构
 */
USTRUCT(BlueprintType)
struct FFarmLandPlotInfo
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FarmLandPlotInfo")
    FTransform Transform;
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FarmLandPlotInfo")
    FPlotStateData StateData;
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FarmLandPlotInfo")
    int32 InstanceIndex;
};

/**
 * 农作物生长阶段
 */
UENUM(BlueprintType)
enum class ECropGrowthStage : uint8
{
    Seed        UMETA(DisplayName = "种子"),
    Germination UMETA(DisplayName = "发芽"),
    Seedling    UMETA(DisplayName = "幼苗"),
    Vegetative  UMETA(DisplayName = "生长期"),
    Flowering   UMETA(DisplayName = "开花期"),
    Fruiting    UMETA(DisplayName = "结果期"),
    Mature      UMETA(DisplayName = "成熟"),
    Withered    UMETA(DisplayName = "枯萎"),
    Dead        UMETA(DisplayName = "死亡")
};

/**
 * 农作物健康状况
 */
UENUM(BlueprintType)
enum class ECropHealth : uint8
{
    Excellent   UMETA(DisplayName = "极佳"),
    Good        UMETA(DisplayName = "良好"),
    Fair        UMETA(DisplayName = "一般"),
    Poor        UMETA(DisplayName = "不佳"),
    Critical    UMETA(DisplayName = "濒危")
};

/**
 * 农作物数据行结构（用于DataTable配置）
 */
USTRUCT(BlueprintType)
struct FCropDataRow : public FTableRowBase
{
    GENERATED_BODY()
    
    // 基础信息
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Basic")
    FName CropID;
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Basic")
    FText CropName;
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Basic")
    FText CropDescription;
    
    // 生长参数（单位：游戏内分钟）
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Growth")
    float GerminationTime = 60.0f;      // 发芽时间
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Growth")
    float SeedlingTime = 120.0f;        // 幼苗时间
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Growth")
    float VegetativeTime = 240.0f;      // 生长期时间
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Growth")
    float FloweringTime = 180.0f;       // 开花时间
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Growth")
    float FruitingTime = 300.0f;        // 结果时间
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Growth")
    float TotalGrowthTime = 0.0f;       // 计算得出的总时间
    
    /*// 环境需求
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Environment")
    TArray<ESeasonType> SuitableSeasons;  // 适宜季节*/
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Environment")
    float MinTemperature = 10.0f;       // 最低温度
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Environment")
    float MaxTemperature = 30.0f;       // 最高温度
    
    // 收获信息
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Harvest")
    int32 HarvestMinAmount = 1;         // 最少收获数量
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Harvest")
    int32 HarvestMaxAmount = 3;         // 最多收获数量
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Harvest")
    FName HarvestProductID;             // 收获物ID
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Harvest")
    bool bCanRegrow = false;            // 是否可再生
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Harvest")
    float RegrowTime = 0.0f;            // 再生时间
    
    // 外观与表现
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visual")
    TSoftObjectPtr<UStaticMesh> SeedMesh;
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visual")
    TArray<TSoftObjectPtr<UStaticMesh>> GrowthMeshes;
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visual")
    TSoftObjectPtr<UTexture2D> CropIcon;
    
    // 初始化计算
    FCropDataRow()
    {
        TotalGrowthTime = GerminationTime + SeedlingTime + VegetativeTime + 
                         FloweringTime + FruitingTime;
    }
};

/**
 * 农作物运行时数据
 */
USTRUCT(BlueprintType)
struct FCropRuntimeData
{
    GENERATED_BODY()
    
    // 标识
    UPROPERTY()
    FName CropID;
    
    // 时间相关
    UPROPERTY()
    FDateTime PlantTime;                // 种植时间
    
    UPROPERTY()
    float CurrentGrowthTime = 0.0f;     // 当前生长累计时间
    
    UPROPERTY()
    ECropGrowthStage CurrentStage = ECropGrowthStage::Seed;
    
    // 状态值（0-100）
    UPROPERTY()
    float HealthValue = 100.0f;         // 健康值
    
    UPROPERTY()
    float WaterLevel = 100.0f;          // 水分值
    
    UPROPERTY()
    float NutrientLevel = 100.0f;       // 营养值
    
    UPROPERTY()
    ECropHealth HealthStatus = ECropHealth::Excellent;
    
    // 额外状态
    UPROPERTY()
    bool bHasPests = false;             // 是否有害虫
    
    UPROPERTY()
    bool bHasDisease = false;           // 是否有病害
    
    UPROPERTY()
    bool bIsWithered = false;           // 是否枯萎
    
    UPROPERTY()
    bool bIsHarvestable = false;        // 是否可收获
    
    UPROPERTY()
    int32 TimesWatered = 0;             // 浇水次数
    
    UPROPERTY()
    int32 TimesFertilized = 0;          // 施肥次数
    
    // 计算进度（0-1）
    float GetGrowthProgress() const
    {
        return FMath::Clamp(CurrentGrowthTime / GetTotalGrowthTime(), 0.0f, 1.0f);
    }
    
    float GetTotalGrowthTime() const
    {
        // 从DataTable获取，这里返回默认值
        return 900.0f; // 15小时
    }
};

/**
 * 地块配置属性
 */
USTRUCT(BlueprintType)
struct FFarmLandData
{
    GENERATED_BODY()
    
    // 单元格网格模型（每个单元格显示的土地）
    UPROPERTY()
    TObjectPtr<UStaticMesh> LandCellMesh;
    
    // 地面网格模型（整个农场的基础地面）
    UPROPERTY()
    TObjectPtr<UStaticMesh> GroundMesh;
    
    // 单元格大小
    FVector2D CellSize = FVector2D(200.f, 200.f);
    
    // X方向单元格数量
    int32 GridSizeX = 10;
    
    // Y方向单元格数量
    int32 GridSizeY = 10;
    
    // 单元格间距
    float CellSpacing = 0.0f;
    
    // 土地网格高度偏移
    float LandHeightOffset = 10.0f;

    // 地面扩展
    float GroundExtension = 0.0f;
};