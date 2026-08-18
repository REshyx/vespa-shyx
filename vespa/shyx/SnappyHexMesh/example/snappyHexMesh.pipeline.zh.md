# snappyHexMesh 管线笔记（对照 OpenFOAM-v2412 源码）

对照官方用户指南 [4.4 snappyHexMesh](https://www.openfoam.com/documentation/user-guide/4-mesh-generation-and-conversion/4.4-mesh-generation-with-the-snappyhexmesh-utility) 与 sibling 树 `OpenFOAM-v2412/src/mesh/snappyHexMesh/`。关键字仍以英文 dict 为准。本目录另有：

- `snappyHexMeshDict`：与当前 SHYX 滤镜默认接近的可跑配置
- `snappyHexMeshDict.official.zh`：官方 annotated dict 的中文注释译本

源码入口：`applications/utilities/mesh/generation/snappyHexMesh/snappyHexMesh.C`（`doRefine` → `doSnap` → `doLayers`）。细化顺序在 `snappyRefineDriver::doRefine`。

---

## 1. 入口/出口怎么标

OpenFOAM **不在体网格上打“入口标签”**，只认 `constant/polyMesh/boundary` 的 **patch 名 + `type`**。管道流约定三块面：

| patch | `type` | 角色 |
|-------|--------|------|
| `inlet` / `outlet` | `patch` | 开口 |
| `wall` | `wall` | 固壁（才能挂 `noSlip` 等） |

物理条件写在 `0/U`、`0/p`（以及 `k`/`omega`/`nut`），名字必须与 patch **完全一致**。经典管道：入口给速度（`flowRateInletVelocity` / `fixedValue` / `surfaceNormalFixedValue`），出口给压力（`fixedValue` + `U` 的 `zeroGradient` 或回流时的 `inletOutlet`）。

几何上常见三种来源：

1. **blockMesh** 直接给面命名。开口管把 STL 两端对齐盒子端面，入口继承盒面子。
2. **snappy 分片 STL**（或多 `solid`）：`inlet.stl` / `outlet.stl` / `wall.stl`，在 `refinementSurfaces` 里 `patchInfo { type patch/wall; }`。
3. 网格后再 `topoSet` + `createPatch`。

一张整壳 STL 只会生成 **一个** patch（本滤镜默认 `geometry.stl` → `geometry`）。要做管道入口必须先拆面或事后切 patch。

---

## 2. 开口几何 vs 小孔洞

snappy **可以**吃两端开口的管壁：体积不是 STL 自己围出来的，而是 **背景 hex + 表面切割 + `locationInMesh` 漫水**。

小孔洞不是入口，是切面上的 **漏**。漫水会从洞钻到另一侧。真正起作用的是 **castellated 之后、孔边当地格子**（`level (0 2)` 时贴面边长约是背景的 1/4），不是最初那层粗盒子。洞大约 1 个当地格子宽就已经危险。

即便 castellated 阶段格子钻不过去、内外暂时分开，也只说明体积不会整块漏掉：snap 仍会往洞边吸，质量差、layer 易挂。针孔应先补；故意开口的入口留给盒面或独立 inlet patch。

`refinementRegions` 的 `mode inside` / `outside` 需要能判内外的 **封闭** searchable 表面。开口 STL 做不了可靠的体内/体外加密。官方 `leakLevel` / `blockLevel` 是漏检和堵窄缝，默认常关。

本滤镜头文件写 “closed surface” 是血管 lumens 的推荐用法，不是 OpenFOAM 的硬限制。

---

## 3. 一份 geometry 表，不是每阶段换 STL

官方支持「封闭体做体积加密 + 分片表面做切/吸/命名」，但是 **一次 dict、多块几何、按用途引用**，不是 castellated 用封闭体、snap 再换分片体。

| 几何 | 要不要封闭 | 写在哪 | 干什么 |
|------|------------|--------|--------|
| box / sphere / 封闭 STL | 要（inside/outside） | `refinementRegions` | 体积加密，**不生成 patch** |
| 分片或开口 STL | 开口也可以 | `refinementSurfaces` | 贴面加密、求交面当漫水挡板、命名 patch、snap 的对象 |
| `blockMesh` 盒子 | 背景 | dict 外 | 漫水底盘；开口对齐盒面时入口落在这些面上 |

layer **不再读 STL**，只认最终 **patch / faceZone 名**（官方原话：不是 geometry 名）。分片更好：`wall` 铺层，`inlet`/`outlet` 写 `nSurfaceLayers 0`。整张封闭成一块 patch 时，入口帽也会被当墙铺层。

一次运行不能换几何。想换表面只能分两次跑（先 castellated 停住、改 dict、再 snap），不是官方默认。

---

## 4. 三个阶段（外加第 0 步背景）

0. **blockMesh** 背景纯 hex（本滤镜全关 castellated/snap/layers 时只出笛卡尔盒）。
1. **castellated** — 八叉加密、沿已有面挡板 + **cell removal**、建 patch（锯齿台阶边界）。
2. **snap** — 把台阶上的点吸到 `refinementSurfaces` 那张 STL（日志是 `Morphing phase`）。
3. **addLayers** — 缩网格、在已有 patch 上插棱柱层。

v2412 可选 `type castellatedBufferLayer`（切完再加缓冲层），仍是同一套几何。

用户指南图 4.14 的 snap 是 **morph**，不是第二次沿 STL 斜切六面体。

---

## 5. `refinementSurfaces` vs `refinementRegions`

都在 **castellated**（`castellatedMeshControls`），都是给 `hexRef8` 挑「哪些格子一分八」，**都不斜切 STL**。

| | `refinementSurfaces` | `refinementRegions` |
|--|----------------------|---------------------|
| 驱动 | `surfaceOnlyRefine` → `markSurfaceRefinement` | `shellRefine` → `markInternalRefinement` |
| 判定 | 相邻格子中心连线打到面；`level (min max)`：min=相交至少到这级，max=交线夹角 > `resolveFeatureAngle` 再到这级 | 格子中心 `inside` / `outside` / `distance`（距离须递增） |
| 顺序（v2412） | **先** | **后**（表面细化并粗删外侧之后） |
| 切边界 / 建 patch | 求交面供后面 **cell removal** 当挡板，并命名 patch | 否（源码称 refinement shells） |
| snap 吸不吸 | 吸这一张 | 不吸加密盒子 |

`distance` 开口也可以（按到面距离）；`inside`/`outside` 必须封闭。

日常就记用户指南点名的三档加密：**特征边 → 表面 → 区域**（面必须在体前面）。v2412 `snappyRefineDriver::doRefine` 完整顺序如下；空 dict 时 gap / dangling / 定向等基本空过。

| # | 源码 | 对应 dict | 做什么 | 算不算加密 |
|---|------|-----------|--------|------------|
| 1 | `featureEdgeRefine` | `features`（`.eMesh`） | 被特征边穿过的 hex 一分八 | 是（边） |
| 2 | （可选）`surfaceOnlyRefine` + `smallFeatureRefine` | 开了 gap/曲率才预跑 | 窄缝预细化 | 是（附加） |
| 3 | `surfaceOnlyRefine` | **`refinementSurfaces`** | 贴面相交格子一分八 | 是（面） |
| 4 | `gapOnlyRefine` | `gapLevel` 等 | 窄缝里再加密 | 是（附加） |
| 5 | `surfaceProximityBlock` | 两表面很近 | 缝里的格子标掉 | 偏删除 |
| 6 | `removeInsideCells`（buffer=1） | `limitRegions` / 种子 | 先丢掉刀缝外侧一大块 | **cell removal** |
| 7 | `bigGapOnlyRefine` | gap pass 2 | 宽一点的缝再加密 | 是（附加） |
| 8 | `shellRefine` | **`refinementRegions`** | inside / outside / distance | 是（体） |
| 9 | `removeInsideCells`（buffer=0） | `limitRegions` level -1 | 再清一遍超限区域 | **cell removal** |
| 10 | `danglingCellRefine` ×2 | （无独立关键字） | 5/6 个邻面已加密的 hex 补劈，把交界补顺 | 是（过渡） |
| 11 | `refinementInterfaceRefine` | `nCellsBetweenLevels` 相关 | 两侧 level 不同再补 | 是（过渡） |
| 12 | `directionalShellRefine` | `levelIncrement` 等 | 定向劈分 | 是（附加） |
| 13 | `blockLeakFaces` | 有 `locationsOutsideMesh` 才跑 | 堵漏 | 偏删除 |
| 14 | `directionalSmooth` | `nSmoothExpansion` 等 | 定向膨胀比光滑 | 不是加密 |
| 15 | **`baffleAndSplitMesh`** | `locationInMesh` | 日志写 *Splitting mesh at surface intersections*。在**已有 hex 面**上插挡板，然后删掉种子走不到的连通块。 **不是切开单个 hex**，效果就是 **cell removal** + 留下的挡板变成边界 patch | **cell removal** |
| 16 | `zonify` / `addFaceZones` | `faceZone` / `cellZone` | 分区 | 不是加密 |
| 17 | `splitAndMergeBaffles` | — | 把挡板拉开成两侧边界 | 拓扑整理 |
| 18 | `mergePatchFaces` 等 | `mergePatchFaces` | 合并边界面，给 snap 用 | 拓扑整理 |

手册 4.4.3→4.4.4→4.4.5 把「删细胞」画在区域加密之前；源码是 **表面细化 → 粗删外侧 → 区域加密 → `baffleAndSplitMesh` 定稿删除**。

容易混的词：`gapMode inside/outside/mixed` 是窄缝，不是体加密；`cellZoneInside` 是划 zone。

---

## 6. castellated 实质只有两类操作：八叉加密 + cell removal

用户指南 4.4.3 的 *Cell splitting*、以及 `baffleAndSplitMesh` 日志里的 *Splitting mesh at surface intersections*，都容易让人以为在沿 STL **斜切单个 hex**。源码不是那样。

**加密（split）** — `hexRef8::setRefinement`：选中的 hex **2×2×2 一分八**，仍轴对齐。粗细交界处未加密的格子带挂点，变成 **split-hex**。每细化一格大约 +7 个单元。特征边 / `refinementSurfaces` / `refinementRegions` / gap / dangling 全部都是在挑「谁该一分八」。

**删除（cell removal）** — 包括提前的 `removeInsideCells`，以及定稿的 `baffleAndSplitMesh`：

- 两相邻格子中心连线穿过 STL → 把 **已经存在的那张 hex 面** 标成挡板（`createBaffles`）。格子本身不沿三角面剖开，不会出现半个斜切多面体。
- 从 `locationInMesh` 漫水：穿不过这些挡板。种子这边留下，另一边 **整格扔掉**。
- 留下的挡板成为锯齿边界 patch。

所以「面剪开 / split mesh at surface」**不是切开单个 hex**，就是 **cell removal**：挡板只是给漫水当墙，真正改变网格的是删掉走不到的单元。城垛台阶来自「删哪一层现成的 hex 面」，不是斜切。

真正让格子「看起来被表面切开」的是下一阶段 **snap** 把台阶顶点吸到 STL 上，体还是原来那些 hex/split-hex，只是变形了。可选 `nFaceSplitInterval`（默认 -1 关）只在 snap 里裂 **边界面**，也不是 castellated 斜切体。

---

## 7. 特征边：不是 TetGen 那种保证

TetGen / 约束 Delaunay：指定的 segment 必须出现在网格里（不够就在棱上插 Steiner 点）。snappy **没有 edge recovery**。

给了 `.eMesh` 之后：

- **castellated**：几乎只加密。`featureEdgeRefine` 用 `trackedParticle` 沿特征边走，穿过的格子 `hexRef8`。棱仍可能从某个 hex **脸中间**穿过。
- **snap**：不是纯加密。先算到 STL 的 `patchDisp`，再算到 `.eMesh` 的 `patchAttraction`，然后 **用特征覆盖普通贴面**（`snappySnapDriverFeature.C`，`featureAttract` 随 `nFeatureSnapIter` 从 0 抬到 1）。`explicitFeatureSnap` 用 `.eMesh`；`implicitFeatureSnap` 从 STL 估锐边。
- **layer**：不读 `.eMesh`。`handleFeatureAngle` 在锐边上 **关掉挤出**。

「保证特征边」做不到；工程上只能叠：`.eMesh` 加密够 + `explicitFeatureSnap` + 足够 `nFeatureSnapIter` + 必要时裂面。本滤镜默认 `features ()` 空、`implicitFeatureSnap true`：没有 castellated 那一档显式棱加密。

---

## 8. 质量迭代在哪；和贴特征是对冲的

`meshQualityControls` 是共用闸门（`errorReduction` 默认 0.75，`nSmoothScale`，以及 layer 用的 `relaxed`）。

**castellated：基本没有质量迭代。** 最多 `handleSnapProblems` 预先扔掉以后很难吸的格子。

**snap：两层套在一起，目标对冲。**

- 外层 `nFeatureSnapIter`：提议「往特征上吸」的位移。
- 内层 `scaleMesh`（约 `2*nSnap`）：`checkMesh`，不合格面上的点位移 × `errorReduction`；过了 `nSnap` 后 `errorReduction=0`，这些点 **冻住**。

外层要把点拽到锐边；内层一看到非正交/凹面就把位移缩回去。质量阈值越严，锐边越先被牺牲。日志甚至会写动成功了但 *will not satisfy your quality constraints*。

**layer：又一套质量迭代，目标是层厚不是棱。** `nLayerIter` 每轮：往里缩 → 插层 → `checkMesh` → 失败则减厚度/取消挤出。超过 `nRelaxedIter` 改用 `relaxed`（例如 `maxNonOrtho 75`）。棱已经在 snap 里定了（或被质量冻在半途）；layer 在锐边附近典型结果是 **层断掉**，不会把点吸回 `.eMesh`。

收紧质量 = snap 更不敢贴锐边，layer 更不敢在锐边挤层。没有「质量循环也必须保住特征边」这一条。

---

## 9. 和本滤镜的对应

见 `ParaViewPlugin/SHYXSnappyHexMesh.xml`。当前默认接近单张 STL → 一个 `geometry` patch，`features` 空，implicit 特征吸附开。要管道入口、体积加密、显式棱：

- 拆 STL / 多 `solid`，或开口对齐背景盒面
- 封闭包络或盒子进 `refinementRegions`（`inside` / `distance`）
- 抽出 `.eMesh` 填 `features`，并开 `explicitFeatureSnap`
- layer 按最终 patch 名写 `nSurfaceLayers`，入口写 0
