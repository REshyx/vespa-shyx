# HologramRepresentation — Hologram

Display 下拉 **`Hologram`**。不要 `HologramRepresentation()`。Python：`GetDisplayProperties().Representation = 'Hologram'`，属性用 **`HG_*`**。

表面网格按原样画。体网格会把每个体单元拆成它的全部面，内部的面也画，不是只留外皮：四面体是四个三角形，六面体和体素是六个四边形（GPU 剖开时对角线不当作线框），楔形、金字塔、五/六棱柱以及 Image / Rectilinear / Structured 的三维单元同样处理。高阶单元和多面体按各个面剖成三角形。相邻单元各出一份共面，背面剔除只留朝向相机的那一份，所以从里面也能看见。同一个数据集里的二维面保留，折线不画。

线来自 VTK 的 screen-space edge mask（`Surface With Edges` 那套 `emix`），面用平坦法线做菲涅尔。不是空气里的体积晶格。`HG_Flow` 前三档是三维噪声，在数据集自己的坐标里取样。Display 里平移或旋转物体时，图案跟着网格走，不留在世界坐标轴上，也不跟着相机走。这三种噪声本身没有轴向。体网格面数大约是单元数的四五倍以上，大网格会更重。Maelstrom 是三次三维域扭曲，比另外两档重。

**Scan** 是第四档，和噪声互斥：选中后不再采噪声，只留一条扫带。**HG_ScanAxis** 选方向。X / Y / Z 沿数据集包围盒走，跟着物体的平移和旋转。View depth 是垂直于相机的平面，从包围盒靠近相机的一侧扫到远离的一侧，转相机时方向跟着视线变。**HG_BandCount** 是这条轴上有几条带子，**HG_ScanWidth** 是每条带占一个周期的比例，**HG_ScanSpeed** 为 1 时大约 5 秒走完包围盒，负值反向。Noise Scale、Noise Power 在这一档里隐藏。**HG_GroupMesh** 仍然可用。

默认每个 fragment 用自己的位置，Scan 像一层切过表面。打开 **HG_GroupMesh** 后，按原来的体单元分组：同一颗单元拆出来的所有面共用这颗单元的中心，所以整颗一起亮、一起暗，旁边的单元按自己的中心先后出现。表面网格没有体单元时，每个多边形用自己的中心。Scan 在这一档里不再切层，也不再把整块网格当成一个点：前沿沿轴向推进，扫过的单元保持点亮，还没扫到的保持暗，前沿过渡的厚度是 **HG_ScanWidth**（占包围盒的比例，不是一条带子的周期）。走完一遍会重新开始。**HG_BandCount** 不增加分组 Scan 的条数。噪声档则是每一颗单元一个值。菲涅尔仍按每个面自己的朝向。分组会把每个面的角点拆开，大网格会更重，交互时也不再降 LOD。

* **Foam**： [Foaming waves](https://www.shadertoy.com/view/7sBcRG) 的三维版。旋转梯度，细纹转得更慢。
* **Turbulence warp**： [Tribulence Warp](https://www.shadertoy.com/view/NXS3DG) 抬到三维。三角波反复拧坐标，亮的是拧到原点附近的那一缕。
* **Maelstrom**： [Maelstrom_](https://www.shadertoy.com/view/MflyRf) 的三维域扭曲。fBm 在三个方向上各取一次再推回去，相位在原地变，不沿某根轴滑。
* **Scan**：不采上面三种噪声。一条带沿 X、Y、Z 或视线深度扫过包围盒。打开 Group Mesh 后，体单元整颗整颗地显现，扫过的留在亮处，走完再来一遍。

## 两种样式

* **Lattice**：线框一直在。面平时很淡，噪声经过时亮起来。
* **Ghost**（默认）：线和面都交给菲涅尔。默认 **Rim** 是掠射反光，三角面擦过视线时才出现；**Facing camera** 则是正对相机的面出现。噪声经过时，也只在这些面上再点亮。Scan 档里，带经过的面直接填上，不乘菲涅尔。

背面剔除，避免封闭网格正反面叠两次。颜色用 Display 的 **Solid Color**（或标量着色）。**Line Width** 是线宽。整体 **Opacity** 会再乘一次。

动画由 `ParaViewPlugin/Representations/pqHologramAnimationManager` 在 `HG_Animate` 打开时连续重绘。

## 参数

| 参数 | 默认 | 作用 |
| --- | --- | --- |
| `HG_Animate` | 1 | 扫光是否随时间移动 |
| `HG_Style` | Ghost (1) | Lattice 或 Ghost |
| `HG_Flow` | Foam (0) | 0 Foam，1 Turbulence warp，2 Maelstrom，3 Scan。前三档没有轴向，Scan 单独扫带 |
| `HG_WireOpacity` | 0.01 | 三角边不透明度 |
| `HG_FaceOpacity` | 0.2 | 面的强度。Lattice 里静止时大约一成，扫光时抬高 |
| `HG_FresnelPower` | 4 | 越大，反光带越窄 |
| `HG_FresnelMode` | Rim (0) | 0 掠射反光，1 朝向相机 |
| `HG_ScanStrength` | 2 | 当前这一档的强度，0 关掉 |
| `HG_ScanSpeed` | 2 | 噪声在原地变化的快慢，或 Scan 扫带移动的快慢。Scan 里 1 大约 5 秒走完包围盒。负值反向 |
| `HG_ScanAxis` | X (0) | 仅 Scan。0 X，1 Y，2 Z，3 View depth |
| `HG_ScanWidth` | 0.08 | 仅 Scan。不分组时是每条扫带占一个周期的比例。分组时是显现前沿的过渡厚度，占包围盒的比例 |
| `HG_BandCount` | 4 | 噪声有多密，或未分组 Scan 在轴上有几条带。分组的 Scan 固定一道前沿。Foam 原 shader 从 4 起，另外两档大约 5 到 7 |
| `HG_NoiseScale` | 0.5 | 仅噪声。把噪声放大。大于 1 时亮斑更大 |
| `HG_NoisePower` | 3 | 仅噪声。大于 1 时只留峰顶，一小块亮、其余暗 |
| `HG_GroupMesh` | 0 | 开：按体单元中心取样，一颗单元一个亮度；Scan 逐颗显现。关：每个 fragment 用自己的坐标，Scan 切过表面 |

做法来自表面全息，不是体积步进：线框用重心/屏幕空间边（这里走 VTK 已有的 edge equation，见 [barycentric wireframe](https://tchayen.github.io/posts/wireframes-with-barycentric-coordinates)），鬼面用 `pow(1 - abs(N·V), power)`（[hologram fresnel + scan](https://design.solhun.com/en/three/hologram-shader/)）。三种噪声分别来自 [Foaming waves](https://www.shadertoy.com/view/7sBcRG)、[Tribulence Warp](https://www.shadertoy.com/view/NXS3DG)、[Maelstrom_](https://www.shadertoy.com/view/MflyRf)，锁在模型包围盒上，跟着物体的平移和旋转走，不跟屏幕走。Scan 是另一档：X/Y/Z 用同一套模型坐标，View depth 用相机空间的深度。
