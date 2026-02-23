#pragma once

#include "CoreMinimal.h"
#include "CropData.generated.h"

class UCropBase;
class UNiagaraComponent;
class UCropDataAsset;


/**
 * 地块自定义数据索引
 */
namespace PlotCustomData
{
    constexpr int32 Opacity = 0;
    constexpr int32 Wetness = 1;
    constexpr int32 Fertility = 2;
    constexpr int32 Count = 3;
}

/**
 * 农作物自定义数据索引
 */
namespace CropCustomData
{
    constexpr int32 Disease = 0;
    constexpr int32 IsLeaf = 1;
    constexpr int32 Count = 2;
}

/**
 * 编辑模式
 */
UENUM(BlueprintType)
enum EEditMode : uint8
{
    None          UMETA(DisplayName = "无"),
    Reset         UMETA(DisplayName = "重置"),
    Plow          UMETA(DisplayName = "耕地"),
    UnPlow        UMETA(DisplayName = "反耕地"),
    Wet           UMETA(DisplayName = "灌溉"),
    UnWet         UMETA(DisplayName = "反灌溉"),
    Fertilize     UMETA(DisplayName = "施肥"),
    UnFertilize   UMETA(DisplayName = "反施肥"),
    Weed          UMETA(DisplayName = "杂草"),
    UnWeed        UMETA(DisplayName = "反杂草"),
    Pest          UMETA(DisplayName = "虫害"),
    UnPest        UMETA(DisplayName = "反虫害"),
    PlantCrop     UMETA(DisplayName = "种植"),
    HarvestCrop   UMETA(DisplayName = "收获"),
    CropDried     UMETA(DisplayName = "作物干枯"),
    UnCropDried   UMETA(DisplayName = "干枯恢复"),
    RemoveCrop    UMETA(DisplayName = "移除作物"),


    // 哨兵：不用于逻辑，仅用于计数
    Count         UMETA(Hidden)
};

/**
 * 农作物生长阶段
 */
UENUM(BlueprintType)
enum class ECropGrowthStage : uint8
{
    None        UMETA(DisplayName = "无"),
    Seed        UMETA(DisplayName = "种子"),
    Seedling    UMETA(DisplayName = "幼苗"),
    Mature      UMETA(DisplayName = "成熟"),
    Harvested   UMETA(DisplayName = "已收获")
};

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

    // uint8 存储（保证存档正确性）
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PlotState")
    uint8 StateFlags = static_cast<uint8>(EPlotState::Empty);

    EPlotState GetAsEnum() const
    {
        return static_cast<EPlotState>(StateFlags);
    }

    void SetFromEnum(EPlotState NewState)
    {
        StateFlags = static_cast<uint8>(NewState);
    }
    
    // 重置为空
    void Reset()
    {
        StateFlags = static_cast<uint8>(EPlotState::Empty);
    }

    // 是否为空
    bool IsEmpty() const
    {
        return StateFlags == static_cast<uint8>(EPlotState::Empty);
    }
    
    // 是否只有一个状态（用于验证）
    bool IsValidSingleState() const
    {
        if (StateFlags == static_cast<uint8>(EPlotState::Empty))
        {
            return false;
        }
        return (StateFlags & (StateFlags - 1)) == 0;
    }

    // 检查是否包含指定状态
    bool HasState(EPlotState State) const
    {
        return (StateFlags & static_cast<uint8>(State)) != 0;
    }

    // 添加状态
    void AddState(EPlotState State)
    {
        StateFlags |= static_cast<uint8>(State);
    }

    // 移除状态
    void RemoveState(EPlotState State)
    {
        StateFlags &= ~static_cast<uint8>(State);
    }

    // 切换状态
    void ToggleState(EPlotState State)
    {
        StateFlags ^= static_cast<uint8>(State);
    }

    // 转换为显示字符串
    FString ToString() const
    {
        if (StateFlags == static_cast<uint8>(EPlotState::Empty))
        {
            return TEXT("空地");
        }

        TArray<FString> ActiveStates;
        if (HasState(EPlotState::Plowed))       ActiveStates.Add(TEXT("已耕地"));
        if (HasState(EPlotState::Watered))      ActiveStates.Add(TEXT("已灌溉"));
        if (HasState(EPlotState::Fertilized))   ActiveStates.Add(TEXT("已施肥"));
        if (HasState(EPlotState::WeedInfested)) ActiveStates.Add(TEXT("有杂草"));
        if (HasState(EPlotState::PestInfested)) ActiveStates.Add(TEXT("有虫害"));

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
 * 农作物状态位掩码枚举
 * 使用位操作可以同时拥有多个状态
 */
UENUM(BlueprintType, meta = (Bitflags, UseEnumValuesAsMaskValuesInEditor = "true"))
enum class ECropState : uint8
{
    None               = 0      UMETA(DisplayName = "无状态"),        // 0000 0000

    // 收获相关
    ReadyForHarvest    = 1 << 0 UMETA(DisplayName = "可收获"),        // 0000 0001
    
    // 健康/营养状态
    NeedsWater         = 1 << 2 UMETA(DisplayName = "需要浇水"),      // 0000 0100
    NeedsFertilizer    = 1 << 3 UMETA(DisplayName = "需要施肥"),      // 0000 1000
    OverWatered        = 1 << 4 UMETA(DisplayName = "浇水过多"),      // 0001 0000
    NutrientBurn       = 1 << 5 UMETA(DisplayName = "肥料烧伤"),      // 0010 0000
    Withers            = 1 << 6 UMETA(DisplayName = "枯萎"),          //0100 0000
    
    // 病害状态
    Disease            = 1 << 7 UMETA(DisplayName = "病害"),          // 1000 0000
};
ENUM_CLASS_FLAGS(ECropState); 

/**
 *农作物状态数据结构
 */
USTRUCT(BlueprintType)
struct FCropStateData
{
    GENERATED_BODY()

    // 使用 uint8 存储状态位（保证存档大小固定）
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CropState")
    uint8 StateFlags = static_cast<uint8>(ECropState::NeedsWater);

    // 获取当前状态作为枚举值（用于需要直接操作枚举的场景）
    ECropState GetAsEnum() const
    {
        return static_cast<ECropState>(StateFlags);
    }

    // 从枚举值设置状态
    void SetFromEnum(ECropState NewState)
    {
        StateFlags = static_cast<uint8>(NewState);
    }
    
    // 重置为默认状态
    void Reset()
    {
        StateFlags = static_cast<uint8>(ECropState::NeedsWater);
    }

    // 是否为空状态
    bool IsEmpty() const
    {
        return StateFlags == static_cast<uint8>(ECropState::None);
    }

    // 检查是否包含指定状态
    bool HasState(ECropState State) const
    {
        return (StateFlags & static_cast<uint8>(State)) != 0;
    }

    // 添加一个或多个状态
    void AddState(ECropState State)
    {
        StateFlags |= static_cast<uint8>(State);
    }

    // 移除指定状态
    void RemoveState(ECropState State)
    {
        StateFlags &= ~static_cast<uint8>(State);
    }

    // 切换指定状态
    void ToggleState(ECropState State)
    {
        StateFlags ^= static_cast<uint8>(State);
    }

    // 转换为可读字符串
    FString ToString() const
    {
        TArray<FString> ActiveStates;

        if (HasState(ECropState::ReadyForHarvest)) ActiveStates.Add(TEXT("可收获"));
        if (HasState(ECropState::NeedsWater))      ActiveStates.Add(TEXT("需要浇水"));
        if (HasState(ECropState::NeedsFertilizer)) ActiveStates.Add(TEXT("需要施肥"));
        if (HasState(ECropState::OverWatered))     ActiveStates.Add(TEXT("浇水过多"));
        if (HasState(ECropState::NutrientBurn))    ActiveStates.Add(TEXT("肥料烧伤"));
        if (HasState(ECropState::Disease))         ActiveStates.Add(TEXT("病害"));

        FString Result;
        for (int32 i = 0; i < ActiveStates.Num(); ++i)
        {
            if (i > 0) Result.Append(TEXT(" | "));
            Result.Append(ActiveStates[i]);
        }
        Result.Append(IsHealthy() ? TEXT(" # 健康") : TEXT(" # 不健康"));
        return Result;
    }

    // 检查作物是否健康
    bool IsHealthy() const
    {
        // 健康定义：没有枯萎、病害、浇水过多、肥料烧伤
        ECropState NegativeMask = ECropState::NeedsWater | ECropState::NeedsFertilizer | ECropState::OverWatered | ECropState::NutrientBurn | ECropState::Withers | ECropState::Disease;
        return (StateFlags & static_cast<uint8>(NegativeMask)) == 0;
    }
};

/**
 * 单个作物变体的所有生长阶段Mesh
 */
USTRUCT(BlueprintType)
struct FCropMeshVariant
{
    GENERATED_BODY()

    // 变体名称（可选，用于调试）
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crop Mesh", meta = (DisplayName = "变体名称"))
    FName VariantName;
    
    // 单果模型（单个果实/产出物）
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crop Mesh", meta = (DisplayName = "果实模型"))
    TObjectPtr<UStaticMesh> FruitMesh = nullptr;
    
    // 种子模型
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crop Mesh", meta = (DisplayName = "种子模型"))
    TObjectPtr<UStaticMesh> SeedMesh = nullptr;
    
    // 幼苗阶段模型（第一阶段）
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crop Mesh", meta = (DisplayName = "幼苗阶段模型"))
    TObjectPtr<UStaticMesh> SeedlingMesh = nullptr;
    
    // 成熟阶段模型（最后阶段）
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crop Mesh", meta = (DisplayName = "成熟阶段模型"))
    TObjectPtr<UStaticMesh> MatureMesh = nullptr;
    
    // 收获后模型（收获后的状态）
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crop Mesh", meta = (DisplayName = "收获后模型"))
    TObjectPtr<UStaticMesh> HarvestedMesh = nullptr;

};

/**
 *  FarmLand 地块数据结构
 */
USTRUCT(BlueprintType)
struct FFarmLandPlotInfo
{
    GENERATED_BODY()

    // ==================== 地块信息 ====================
    // 地块Transform
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FarmLand|PlotInfo")
    FTransform Transform;

    // 地块状态
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FarmLand|PlotInfo")
    FPlotStateData StateData;

    // 杂草Transform数组
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FarmLand|PlotInfo")
    TArray<FTransform> WeedsTransform;

    // 虫害Niagara组件
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FarmLand|PlotInfo")
    TObjectPtr<UNiagaraComponent> PestNiagaraComponent;

    // ==================== 农作物信息 ====================
    // 农作物
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FarmLand|CropInfo")
    TObjectPtr<UCropBase> Crop;

    // 农作物Transform
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FarmLand|CropInfo")
    FTransform CropTransform;
    
    // 农作物变体
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FarmLand|CropInfo")
    FCropMeshVariant CropMeshVariant;

    // 农作物自定义数据数量
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FarmLand|CropInfo")
    int32 CropNumCustomDataFloats;

};



/**
 * 农作物数据表
 */
USTRUCT(BlueprintType)
struct FCropTableRow : public FTableRowBase
{
    GENERATED_BODY()

    // 唯一ID
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FName CropID;
    
    // 显示名称
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FText DisplayName;
    
    // 种子-幼苗生长时间
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float GrowthSeedTime = 60.0f;

    // 幼苗-成熟生长时间
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float GrowthSeedlingTime = 60.0f;

    // 成长阶段Mesh变体
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TArray<FCropMeshVariant> MeshVariants;

    // 自定义数据数量
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 CropNumCustomDataFloats;
};

// 使用网格的软引用路径作为键
USTRUCT(BlueprintType)
struct FStaticMeshHISMKey
{
    GENERATED_BODY()
    
    // 网格的软引用路径
    UPROPERTY()
    FSoftObjectPath MeshPath;
    
    // 默认构造函数
    FStaticMeshHISMKey(){};

    FStaticMeshHISMKey(const UStaticMesh* Mesh)
        : MeshPath(Mesh)
    {}

    FStaticMeshHISMKey(const FSoftObjectPath& InMeshPath)
        : MeshPath(InMeshPath)
    {}
    
    // 获取UStaticMesh指针
    UStaticMesh* GetMesh() const
    {
        return Cast<UStaticMesh>(MeshPath.TryLoad());
    }
    
    // 比较操作符
    bool operator==(const FStaticMeshHISMKey& Other) const
    {
        return MeshPath == Other.MeshPath;
    }
    
    // 哈希函数
    friend uint32 GetTypeHash(const FStaticMeshHISMKey& Key)
    {
        return GetTypeHash(Key.MeshPath);
    }
};

USTRUCT(BlueprintType)
struct FCropData
{
    GENERATED_BODY()

    // 唯一ID
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FName CropID;

    // 农作物坐标
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FIntPoint Coords;

    // 农作物变体索引
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 VariantsIndex = 0;

    // 农作物当前生长阶段
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    ECropGrowthStage CropGrowthStage = ECropGrowthStage::Seed;
    
    // 种子-幼苗生长时间
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float GrowthSeedTime = 60.0f;

    // 幼苗-成熟生长时间
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float GrowthSeedlingTime = 60.0f;

    // 农作物状态
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FCropStateData CropStateData;

    // 农作物总生长时间
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float TotalGrowthTime = 0.f;

    // 农作物当前阶段生长时间
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float CurrentStageGrowthTime = 0.f;
    
};
