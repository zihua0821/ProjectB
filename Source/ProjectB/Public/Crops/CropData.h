#pragma once

#include "CoreMinimal.h"
#include "CropData.generated.h"

class UCropBase;
class UNiagaraComponent;
class UCropDataAsset;

/**
 * 编辑模式
 */
UENUM(BlueprintType)
enum EEditMode : uint8
{
    None        UMETA(DisplayName = "无"),
    Reset       UMETA(DisplayName = "重置"),
    Plow        UMETA(DisplayName = "耕地"),
    UnPlow      UMETA(DisplayName = "反耕地"),
    Wet         UMETA(DisplayName = "灌溉"),
    UnWet       UMETA(DisplayName = "反灌溉"),
    Fertilize   UMETA(DisplayName = "施肥"),
    UnFertilize UMETA(DisplayName = "反施肥"),
    Weed        UMETA(DisplayName = "杂草"),
    UnWeed      UMETA(DisplayName = "反杂草"),
    Pest        UMETA(DisplayName = "虫害"),
    UnPest      UMETA(DisplayName = "反虫害"),

    // 哨兵：不用于逻辑，仅用于计数
    Count       UMETA(Hidden)
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

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PlotState")
    EPlotState StateFlags = EPlotState::Empty;

    void Reset()
    {
        StateFlags = EPlotState::Empty;
    }
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
    Healthy            = 1 << 1 UMETA(DisplayName = "健康"),          // 0000 0010
    NeedsWater         = 1 << 2 UMETA(DisplayName = "需要浇水"),      // 0000 0100
    NeedsFertilizer    = 1 << 3 UMETA(DisplayName = "需要施肥"),      // 0000 1000
    OverWatered        = 1 << 4 UMETA(DisplayName = "浇水过多"),      // 0001 0000
    NutrientBurn       = 1 << 5 UMETA(DisplayName = "肥料烧伤"),      // 0010 0000
    
    // 病害状态
    Disease            = 1 << 6 UMETA(DisplayName = "病害"),          // 0100 0000
};
ENUM_CLASS_FLAGS(ECropState); 

/**
 *农作物状态数据结构
 */
USTRUCT(BlueprintType)
struct FCropStateData
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CropState")
    ECropState StateFlags = ECropState::Healthy | ECropState::NeedsWater; // 默认为健康且需要浇水

    void Reset()
    {
        StateFlags = ECropState::Healthy | ECropState::NeedsWater;
    }
    bool IsEmpty() const
    {
        return StateFlags == ECropState::None;
    }
    
    bool HasState(const ECropState& State) const
    {
        return (StateFlags & State) != ECropState::None;
    }
    
    void AddState(const ECropState& State)
    {
        StateFlags |= State;
    }

    void RemoveState(const ECropState& State)
    {
        StateFlags &= ~State;
    }

    void ToggleState(const ECropState& State)
    {
        StateFlags ^= State;
    }
    
    FString ToString() const
    {
        // 特殊处理：完全空状态
        if (StateFlags == ECropState::None)
        {
            return TEXT("空状态");
        }

        TArray<FString> ActiveStates;
        
        if (HasState(ECropState::ReadyForHarvest))       ActiveStates.Add(TEXT("可收获"));
        if (HasState(ECropState::Healthy))               ActiveStates.Add(TEXT("健康"));
        if (HasState(ECropState::NeedsWater))            ActiveStates.Add(TEXT("需要浇水"));
        if (HasState(ECropState::NeedsFertilizer))       ActiveStates.Add(TEXT("需要施肥"));
        if (HasState(ECropState::OverWatered))           ActiveStates.Add(TEXT("浇水过多"));
        if (HasState(ECropState::NutrientBurn))          ActiveStates.Add(TEXT("肥料烧伤"));
        if (HasState(ECropState::Disease))               ActiveStates.Add(TEXT("病害"));

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

    // 农作物变体
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FCropMeshVariant CropMeshVariant;

    // 农作物自定义数据数量
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
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
    
};
