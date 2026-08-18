# vtkSHYXSnappyHexMesh

进程内调用 **shyx-snappyhex** adapter（仓库根目录 `shyx-snappyhex/`）编进来的 `SHYXSnappyHex.lib`。菜单在 **Filters → SHYX**（不进 Vascular 工具条）。

**源码不进 vespa。** 官方树（`FOAM_SOURCE_DIR`，例如 sibling `OpenFOAM-v2412`）只读，不在那里 `Allwmake`、不生成 `OpenFOAMConfig.cmake`。CMake 只按 meshing 闭包编译需要的 `.C`，不把子集拷进仓库。边界、库列表、单独编译命令见 [`shyx-snappyhex/README.md`](../../../shyx-snappyhex/README.md)。

开启：`VESPA_USE_SNAPPYHEXMESH=ON`。默认探测 `../OpenFOAM-v2412`（需含 `src/wmake/etc`）。vespa 用 clang-cl + Ninja ExternalProject 编 adapter，不必先在 OpenFOAM 目录里安装。

MSVC 下 `VESPAPlugin.dll` 必须 `/WHOLEARCHIVE` 该 `.lib`（插件 CMake 已接）。不要用 WSL/MinGW 的 `.a`。滤镜在进程内调用静态库，不需要旁边再放 `snappy_cli.exe`。

插件 LoadLibrary 跟踪写在 `%TEMP%\vespa-snappy-load.log`（每行 flush）。卡死时看最后一行。加载时用 adapter 内存中的 `controlDict`，不读磁盘 etc。真正跑 snappy 时把内嵌 `cellModels` 写到 `%TEMP%\shyx-openfoam\etc`。

关掉 castellated / snap / layers 时，用输入表面 AABB（加 Bounds margin）直接生成笛卡尔 `VTK_HEXAHEDRON` 背景盒，不写 OpenFOAM ASCII、不调用 snappyHexMesh。格子尺寸：`Background cell size > 0` 用该值，否则取加 margin 后最长边的 1/16。

每次跑完（成功或失败）都保留 Foam case，并在 case 根目录写空的 `case.foam`。滤镜输出用 ParaView 自带的 `vtkOpenFOAMReader` 读 `internalMesh`，不再自己解析 `faces`/`owner`。**File → Open** 选 `case.foam` 可读整棵 case（含 patch 分块）。固定入口：`%TEMP%\shyx-snappy-last\case.foam`；当次目录路径写在同目录 `last-case-path.txt`。

参数见 `ParaViewPlugin/SHYXSnappyHexMesh.xml`。**Inside points** 列表可 Add insidePoint：选中一行后视图里出现可拖动手柄。空列表仍用 AABB 中心；多个点写成 OpenFOAM `locationsInMesh`（zone `none`）。点必须落在要保留的单元格内，不要贴在面上。

示例 dict：

- `example/snappyHexMeshDict`：与当前滤镜默认接近的可跑配置。
- `example/snappyHexMeshDict.official.zh`：OpenFOAM-v2412 官方 `etc/caseDicts/annotated/snappyHexMeshDict` 的中文注释译本（关键字仍是英文）。
