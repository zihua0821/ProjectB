# 星球地形方案 - 球面地表 + 地块实例化

> **决策日期**：2026-03-12  
> **方案状态**：已确定（替代伪球面方案）

---

## 一、方案概述

### 一句话总结
使用一个现成的球体静态网格作为星球地表，在球面上按经纬度划分网格，每个网格中心放置一个独立的平面地块模型（通过 HISM 批量实例化），地块法线指向球心，实现"真球面 + 可种植地块"的视觉效果，同时数据层保持平面二维数组不变。

---

## 二、为什么选择这个方案

| 维度 | 本方案 | 伪球面（Shader 弯曲） | 真实球面（手动网格） |
|------|--------|----------------------|---------------------|
| 开发周期 | 1-2 周 | 2 周（但始终有问题） | 2-3 个月 |
| 数据层改动 | 无 | 无 | 需重构为球面坐标 |
| 视觉真实感 | 真球面，曲率自然 | 有动态变形伪影 | 真球面，但复杂 |
| 地块排列 | 独立实例，可控 | 依赖平面网格 | 需处理网格生成 |
| 性能 | 极高（HISM） | 高（全屏 Shader） | 中（大量顶点） |
| 风险 | 低 | 中（视觉问题） | 高 |

### 核心优势

1. **绕过手动生成球面网格的坑** - 直接使用引擎稳定功能
2. **地块与数据解耦** - 可独立控制每个地块
3. **性能优异** - 支持成千上万地块
4. **开发成本低** - 1-2 周可跑通原型

---

## 三、技术架构分层

```
┌─────────────────┐
│ 数据层          │
│ 二维数组存储    │
│ 地块状态        │
└────────┬────────┘
         │ 经纬度索引
         ▼
┌─────────────────┐
│ 表现层          │
│ 球体地表 + HISM │
│ 地块实例        │
└────────┬────────┘
         │ 射线检测
         ▼
┌─────────────────┐
│ 交互层          │
│ 点击、种植、    │
│ 生长逻辑        │
└─────────────────┘
```

---

## 四、实现步骤（UE5 C++）

### Phase 0：准备资源

- 一个静态球体网格（如 `Engine/BasicShapes/Sphere`），半径设为 R（建议 3000~10000 米）
- 一个地块模型（如扁平圆柱体或平面，边长 L 约 1 米）
- 材质：地表材质（草地/泥土）、地块材质（耕地）

---

### Phase 1：生成经纬网格（数据定义）

定义网格参数：

```cpp
// Planet.h
UCLASS()
class APlanet : public AActor
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere) float Radius = 5000.0f;    // 星球半径
    UPROPERTY(EditAnywhere) float MinLat = -60.0f;     // 最小纬度（度）
    UPROPERTY(EditAnywhere) float MaxLat = 60.0f;      // 最大纬度
    UPROPERTY(EditAnywhere) float LatStep = 1.0f;      // 纬度步长（度）
    UPROPERTY(EditAnywhere) float LonStep = 1.0f;      // 经度步长（度）

    int32 NumLat;  // 纬度格点数
    int32 NumLon;  // 经度格点数
    TArray<FPlotData> PlotData;  // 一维化存储，索引 = latIdx * NumLon + lonIdx
};
```

初始化时计算网格尺寸：

```cpp
void APlanet::InitGrid()
{
    NumLat = FMath::CeilToInt((MaxLat - MinLat) / LatStep) + 1;
    NumLon = FMath::CeilToInt(360.0f / LonStep);
    PlotData.SetNum(NumLat * NumLon);
}
```

---

### Phase 2：生成地块实例（HISM）

在 Actor 中添加 HISM 组件：

```cpp
// Planet.h
UPROPERTY()
UHierarchicalInstancedStaticMeshComponent* PlotHISM;
```

在 `BeginPlay` 或构建脚本中生成所有地块：

```cpp
void APlanet::GeneratePlots()
{
    if (!PlotHISM || !PlotHISM->GetStaticMesh()) return;

    PlotHISM->ClearInstances();

    float LatRadMin = FMath::DegreesToRadians(MinLat);
    float LatRadMax = FMath::DegreesToRadians(MaxLat);
    float LatRadStep = FMath::DegreesToRadians(LatStep);
    float LonRadStep = FMath::DegreesToRadians(LonStep);

    for (float LatRad = LatRadMin; LatRad <= LatRadMax; LatRad += LatRadStep)
    {
        for (float LonRad = 0; LonRad < 2*PI; LonRad += LonRadStep)
        {
            // 球面坐标
            FVector SurfacePoint(
                Radius * FMath::Cos(LatRad) * FMath::Cos(LonRad),
                Radius * FMath::Sin(LatRad),
                Radius * FMath::Cos(LatRad) * FMath::Sin(LonRad)
            );

            // 法线（从球心指向外）
            FVector Normal = SurfacePoint.GetSafeNormal();

            // 计算旋转：使地块的本地 Z 轴指向法线方向
            FQuat Rotation = FQuat::FindBetweenNormals(FVector::UpVector, Normal);

            FTransform Transform(Rotation, SurfacePoint, FVector::OneVector);
            PlotHISM->AddInstance(Transform);

            // 可选：记录地块索引对应的经纬度（用于数据查找）
            int32 LatIdx = (LatRad - LatRadMin) / LatRadStep;
            int32 LonIdx = LonRad / LonRadStep;
            int32 PlotIndex = LatIdx * NumLon + LonIdx;
            // 可以存储 PlotIndex 到实例的自定义数据，但通常用数组查找即可
        }
    }
}
```

---

### Phase 3：地块大小计算

为了保证地块在任何纬度都不重叠，需要计算网格最小间距：

在最高纬度（`MaxLat`）处，经线方向间距最小：
```
MinSpacing = Radius * cos(MaxLatRad) * LonStepRad
```

地块直径应略小于 `MinSpacing`，例如 `PlotSize = 0.8 * MinSpacing`

可以在生成实例时设置缩放：

```cpp
float PlotScale = PlotSize / MeshBoundsRadius;  // MeshBoundsRadius 是地块模型本身的包围盒半径
FTransform Transform(Rotation, SurfacePoint, FVector(PlotScale));
```

如果希望地块大小固定（例如 1 米），则只需确保 `MinSpacing > 1`，即网格密度足够低。通常 R=5000，MaxLat=60°，LonStep=1°时，`MinSpacing ≈ 5000 * 0.5 * 0.01745 ≈ 43.6 米`，远大于 1 米，所以地块不会重叠，间隙很大。你可以调高网格密度（例如 LonStep=0.1°）来缩小间隙。

---

### Phase 4：数据层绑定

玩家点击地块时，通过射线检测拿到实例索引，再换算为经纬索引，从而访问 `PlotData`。

**射线检测**：启用 HISM 的碰撞（默认启用），并设置碰撞通道。

**实例索引获取**：

```cpp
FHitResult Hit;
if (GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility))
{
    if (Hit.Component == PlotHISM)
    {
        int32 InstanceIndex = Hit.Item;  // 关键：Hit.Item 就是实例索引
        
        // 转换为经纬索引
        int32 LatIdx = InstanceIndex / NumLon;
        int32 LonIdx = InstanceIndex % NumLon;
        
        // 访问 PlotData[LatIdx * NumLon + LonIdx]
    }
}
```

---

### Phase 5：角色移动与重力

角色（或摄像机）的重力方向始终指向球心。可以在角色移动组件中每帧更新：

```cpp
FVector ToCenter = -GetActorLocation().GetSafeNormal();  // 指向球心的方向
// 应用重力加速度
Velocity += ToCenter * GravityStrength * DeltaTime;
```

同时需要调整角色的上方向（`GetActorUpVector()` 应为从球心指向角色的方向），这通常由移动组件自动处理，如果使用自定义物理则需手动设置。

---

### Phase 6：边界与雾效

- 设置指数高度雾，最大视野距离 500~1000 米，使远处网格边界隐入雾中
- 在纬度边界（`MinLat/MaxLat`）处放置不可见碰撞或触发区域，阻止玩家继续前进，或播放"极寒区域"提示

---

## 五、关键技术要点

### 1. 地块旋转精确性

使用 `FQuat::FindBetweenNormals(FVector::UpVector, Normal)` 确保地块平面与球面相切。

**验证方法**：在编辑器中选中一个地块实例，查看其本地坐标轴，Z 轴应指向球心向外。

---

### 2. HISM 性能

- HISM 支持数千实例，Draw Call 为 1，性能极高
- 如果地块数量极大（如 > 10000），可以分块生成多个 HISM 组件，或使用 HLOD
- 实例的动态增删（如作物生长变化）可通过 `UpdateInstanceTransform` 高效更新

---

### 3. 地块悬空问题

根据计算，在 R=5000m、L=1m 时，边缘悬空高度仅 0.025mm，完全不可见。无需任何补偿。

---

### 4. 地块间隙

间隙中的地表由球体材质显示，视觉自然。如果希望地块紧密排列，可调整地块缩放使其刚好接触，但需注意高纬度处可能重叠。建议保持间隙，看起来像田埂。

---

### 5. 地块数据与实例索引的持久化

保存游戏时，需要保存每个地块的状态。由于地块位置完全由经纬度决定，只需保存经纬索引对应的数据，无需保存实例变换。加载时重新生成实例即可（实例变换由算法确定，与保存数据无关）。

---

## 六、潜在问题与对策

| 问题 | 对策 |
|------|------|
| 地块在高纬度处过于密集（重叠） | 调高 LonStep 或限制种植区域到更低纬度 |
| 地块在地表上的视觉"悬空"感 | 验证数值，若仍感觉明显，可将地块中心沿法线向内微移（如移动 0.5 倍悬空高度） |
| 射线检测无法精确命中地块 | 确保地块模型有碰撞，且 HISM 的碰撞复杂度设置正确 |
| 角色移动时看到球体背面的地块 | 启用背面剔除，或只生成玩家所在半球的地块（动态生成） |
| 地块与地表材质衔接生硬 | 在地块边缘添加半透明过渡纹理，或使用地表材质颜色混合 |

---

## 七、开发路线图（2 周）

| 时间 | 任务 | 交付物 |
|------|------|--------|
| Day 1-2 | 创建星球 Actor，添加球体静态网格，设置材质 | 基础星球 Actor |
| Day 3-4 | 实现经纬网格生成逻辑（C++ 循环），调试位置计算 | 网格生成函数 |
| Day 5-6 | 集成 HISM 并批量生成地块实例，调整旋转和缩放 | HISM 地块系统 |
| Day 7-8 | 实现射线检测与地块数据绑定，完成点击交互原型 | 可点击地块 |
| Day 9-10 | 实现角色球面移动（重力方向），测试移动体验 | 球面角色控制器 |
| Day 11-12 | 添加雾效、边界限制，优化视觉 | 完整视觉效果 |
| Day 13-14 | 集成现有种植逻辑（作物生长等），完成 Demo | 可玩 Demo |

---

## 八、与旧方案（伪球面）的对比

详见 `星球地形方案_伪球面.md`

### 核心差异总结

| 对比项 | 伪球面（旧） | 球面+HISM（新） |
|--------|-------------|-----------------|
| **真实性** | 视觉欺骗，有变形伪影 | 真球面，曲率自然 |
| **代码复用** | 90%+ 直接复用 | 90%+ 复用（数据层不变） |
| **地块控制** | 依赖平面网格 | 独立实例，可单独控制 |
| **性能** | 高（全屏 Shader） | 极高（HISM Draw Call=1） |
| **开发风险** | 中（Shader 调试问题） | 低（引擎原生功能） |

---

## 九、参考资料

- UE 官方文档：[Hierarchical Instanced Static Meshes](https://docs.unrealengine.com/)
- UE 官方文档：[FQuat::FindBetweenNormals](https://docs.unrealengine.com/)
- 球面坐标与旋转计算基础

---

## 十、总结

本方案用一个现成球体 + 实例化地块，完美避开了伪球面的视觉问题和真实球面的复杂网格生成。它保留了数据层的简洁性，实现了真球面视觉，且性能优异、开发可控。这是当前讨论范围内最务实、最可行的方案，**建议立即实施**。

---

**文档创建时间**: 2026-03-12  
**最后更新**: 2026-03-12  
**关联文档**: `星球地形方案_伪球面.md`（已废弃）
