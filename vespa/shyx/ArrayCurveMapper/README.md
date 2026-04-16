# vtkArrayCurveMapper

## 1. 目的与功能算法详细解释

**目的**：
`vtkArrayCurveMapper` 的主要使命是充当一位“数据翻译官”。它接收一个用户指定的点数据数组（无论是单薄的标量还是丰满的多维向量），通过一个精心配置的分段线性传递函数（`vtkPiecewiseFunction`），将这些数据平滑、优雅地映射到一个全新的数值范围内。最终，它会把这些焕然一新的数据作为一个全新的标量点数据（Point Data）数组输出，深藏功与名。

**算法流程**：
1. **输入验证与浅拷贝**：首先，它会严谨地检查输入数据（`vtkDataSet`）是否为空或者点数是否为 0。确认无误后，它会使用浅拷贝（`ShallowCopy`）将输入数据复制到输出中，从而完美保留原有网格结构和其他不相关的数据。
2. **提取与检查目标数组**：根据你配置的 `InputArrayName`，它会在点数据中四处寻觅目标数组。如果找不到，或者你压根没给它配置传递曲线，它会优雅地罢工（直接返回 1 保持原样），绝不给你抛出致命崩溃。
3. **获取与限制原始数值**：它会不知疲倦地遍历每一个点的数据：
   - 如果是多维向量（比如三维坐标或速度），它会先通过勾股定理求出其模长（Magnitude）。
   - 如果是普通标量，直接拿来用。
   - 拿到原始值后，它会使用 `[InputRangeMin, InputRangeMax]` 将其“无情”地夹紧（Clamp）。任何试图越界的野数据都会被按在这个区间内，治愈一切强迫症。
4. **曲线映射（Curve Evaluation）**：拿着被限制好的值，去 `vtkPiecewiseFunction`（曲线函数）里查询，通常会得到一个介于 0 到 1 之间的映射比例参数 `t`。
5. **范围重映射（Remapping）**：通过经典线性插值公式 `MappedValue = OutputRangeMin + t * (OutputRangeMax - OutputRangeMin)`，将结果拉伸或压缩到你指定的目标范围，并填入新数组。
6. **合体输出**：将这个名为 `OutputArrayName` 的新数组挂载到输出的 `PointData` 上，大功告成！

---

## 2. 参数列表及其效果和含义

各位客官，请看它的“装备栏”。（注：由于该类具备极强的前瞻性，部分参数可能是为未来的高级渲染、动画效果保留的备用魔法，但在当前映射算法中同样值得拥有姓名）：

### 核心映射参数
* **`InputArrayName`** (string): 输入数组的名称。你要让它处理哪个数据，就得在这里报上名来。
* **`OutputArrayName`** (string): 输出数组的名称。默认是 `"MappedArray"`，这是新生数据在管线中的新身份证。
* **`InputRangeMin` / `InputRangeMax`** (double): 输入数据的限制范围（默认 `[0.0, 1.0]`）。低于 Min 或高于 Max 的原始值都会被强制限制（Clamp）到这个边界上。
* **`OutputRangeMin` / `OutputRangeMax`** (double): 输出数据的目标物理范围（默认 `[0.0, 1.0]`）。数据经过曲线映射后，最终会被按比例缩放并落脚于此。
* **`CurveTransferFunction`** (`vtkPiecewiseFunction*`): 分段线性传递函数。这是整个类的灵魂，决定了数据是如何随曲线起伏而产生非线性变化的。

### 渲染与视觉表现参数 (预留/扩展)
* **`RepresentationMode`** (int): 表现模式。包括表面 (`REPRESENTATION_SURFACE`=0)、体积 (`REPRESENTATION_VOLUME`=1) 和点高斯 (`REPRESENTATION_POINT_GAUSSIAN`=2)。
* **`Opacity`** (double): 不透明度（默认 `0.8`）。决定了未来在视觉呈现时的透明质感。
* **`Trunc`** (double): 截断参数（默认 `1.2`）。
* **`Pow`** (double): 幂次参数（默认 `1.5`）。为后续的非线性计算或指数操作预留。

### 动画与高级特征参数 (预留/扩展)
* **`IntegrationScale` / `TimeScale` / `Time`**: 时间与积分相关的参数，大概是为了流线积分或者动态随时间演变的效果准备的。
* **`AnimationArrayName`** (string): 动画相关的数组名（默认 `"IntegrationTime"`）。
* **`AnimatedOpacityArrayName`** (string): 动画透明度数组名（默认 `"AnimatedOpacity"`）。
* **`PointGaussianRadiusArrayName`** (string): 点高斯半径数组名（默认 `"AnimatedPointRadius"`）。
* **`VolumeDensityArrayName`** (string): 体积密度数组名（默认 `"AnimatedVolumeDensity"`）。
