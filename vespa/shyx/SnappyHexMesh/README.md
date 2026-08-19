# vtkSHYXSnappyHexMesh

进程内调用 **shyx-snappyhex** adapter（仓库根目录 `shyx-snappyhex/`）编进来的 `SHYXSnappyHex.lib`。菜单在 **Filters → SHYX**（不进 Vascular 工具条）。

**源码不进 vespa。** 官方树（`FOAM_SOURCE_DIR`，例如 sibling `OpenFOAM-v2412`）只读，不在那里 `Allwmake`、不生成 `OpenFOAMConfig.cmake`。CMake 只按 meshing 闭包编译需要的 `.C`，不把子集拷进仓库。边界、库列表、单独编译命令见 [`shyx-snappyhex/README.md`](../../../shyx-snappyhex/README.md)。

开启：`VESPA_USE_SNAPPYHEXMESH=ON`。默认探测 `../OpenFOAM-v2412`（需含 `src/wmake/etc`）。vespa 用 clang-cl + Ninja ExternalProject 编 adapter，不必先在 OpenFOAM 目录里安装。

MSVC 下 `VESPAPlugin.dll` 必须 `/WHOLEARCHIVE` 该 `.lib`（插件 CMake 已接）。不要用 WSL/MinGW 的 `.a`。滤镜在进程内调用静态库，不需要旁边再放 `snappy_cli.exe`。

加载时会尽量删除 `%TEMP%\shyx-snappy-*` 残留目录；目录里只要有文件被占用，整棵都不删。每次 Apply 的参数/ABI 诊断写在该次 case 的 `run-diag.txt`（滤镜侧 + 适配器侧各一段；两边 `sizeof`/`offset` 必须一致）。Case Folder 指向 `%TEMP%\shyx-snappy-<id>-<mtime>\case`。若只有滤镜段、错误仍是 `null argument`，说明 `SHYXSnappyHex.lib` 是旧的：先编 `shyx_snappyhex_ep` 再编 `VESPAPlugin`。加载时用 adapter 内存中的 `controlDict`，不读磁盘 etc。真正跑 snappy 时把内嵌 `cellModels` 写到 `%TEMP%\shyx-openfoam\etc`。

## 输入

- **Input**：`vtkPartitionedDataSetCollection`（推荐上游 **SHYX Selection Append Patches**）。每个分块写成一张整体 STL（`type triSurfaceMesh`），作为一个 searchable / patch。**不**解析 STL 多 `solid`，也**不**写 `regions { firstSolid / secondSolid }`。Append Patches 的 selection / pipeline / box / sphere 行在输出里都是同等分块；要做体积加密时把封闭的 box、sphere 或管线封闭面加进 Append Patches，再到本滤镜 **Region patches** 里按名引用（`inside` / `outside` / `distance`）。
- 单张 **`vtkPolyData`** 仍可直连，内部当成名为 `geometry` 的一块。
- **Feature edges**（可选，Properties 面板 pipeline 下拉，创建滤镜时不必选）：管线里另一个 `vtkPolyData` 节点，只收 `VTK_LINE` / `VTK_POLY_LINE`，写成 `constant/triSurface/features.eMesh`，并打开 `explicitFeatureSnap`。留空 `(none)` 则不写 `.eMesh`。

Properties 三张表（Add partition 从 Input 分块名下拉，不是 3D 选择）：

- **Castellated** 组内
  - **Surface patches**（写入 `refinementSurfaces`）：Patch、Level min/max、`patchInfo` type（`wall` / `patch`）。空表 = 全部分块，用 Default surface level。
  - **Region patches**（写入 `refinementRegions`）：Patch、mode（`inside` / `outside` / `distance`）、Level；distance 时用 Distance。空表 = 不写体积加密。
- **Layer patches**（在 Layers 组后，写入 `addLayers`）：Patch、`nSurfaceLayers`（0 = 该 patch 不铺层）。空表且 Add layers 开 = 每个 surface patch 用 Default surface layers。全局 expansion / thickness 仍在 Layers 组。

关掉 castellated / snap / layers 时，用输入表面 AABB（加 Bounds margin）直接生成笛卡尔 `VTK_HEXAHEDRON` 背景盒，不写 OpenFOAM ASCII、不调用 snappyHexMesh。格子尺寸：`Background cell size > 0` 用该值，否则取加 margin 后最长边的 1/16。

每次跑完（成功或失败）都保留这一次的 Foam case（新的 `%TEMP%\shyx-snappy-<id>-<mtime>\case`），并在 case 根目录写 `case.foam`。下一次 Apply 会另开新目录，并尽量删掉上一轮的目录。滤镜输出用 ParaView 自带的 `vtkOpenFOAMReader` 读整棵 `vtkMultiBlockDataSet`（`internalMesh` 和 boundary patches），与 **File → Open** Case Folder 里的 `case.foam` 相同。关掉 castellated 时背景盒也包成单块 MultiBlock。分块 STL / `.eMesh` 在该 case 的 `constant/triSurface/`，面板只显示 **Case Folder**，不另列 STL 路径。

**Inside points** 列表可 Add insidePoint：选中一行后视图里出现可拖动手柄。空列表仍用 AABB 中心；多个点写成 OpenFOAM `locationsInMesh`（zone `none`）。点必须落在要保留的单元格内，不要贴在面上。

示例 dict 与笔记：

- `example/snappyHexMeshDict`：与当前滤镜默认接近的可跑配置（可含多张 STL）。
- `example/snappyHexMeshDict.official.zh`：OpenFOAM-v2412 官方 `etc/caseDicts/annotated/snappyHexMeshDict` 的中文注释译本（关键字仍是英文）。
- `example/snappyHexMesh.pipeline.zh.md`：三阶段、开口/孔洞、refinementSurfaces vs Regions、特征边与质量迭代（对照 v2412 源码）。
