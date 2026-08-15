# vtkSHYXSnappyHexMesh

静态链接 sibling 仓库 [shyx-snappyhex](https://github.com) 的 `SHYXSnappyHex.lib`（OpenFOAM-v2412 snappyHexMesh，MSVC ABI）做以六面体为主的体积网格。菜单在 **Filters → SHYX**（不进 Vascular 工具条）。

开启：

```
cmake --install <shyx-snappyhex>/build-msvc --prefix <shyx-snappyhex>/install
```

vespa 配置加 `-DVESPA_USE_SNAPPYHEXMESH=ON`，`SHYXSnappyHex_DIR` 指向 `install/lib/cmake/SHYXSnappyHex`。MSVC 下 `VESPAPlugin.dll` 必须 `/WHOLEARCHIVE` 该 `.lib`（插件 CMake 已接）。不要用 WSL/MinGW 的 `.a`。

运行时仍读取编译期写入的 `WM_PROJECT_DIR`（OpenFOAM `etc/`，在 shyx-snappyhex 的 `third_party/openfoam-v2412`）。源树不要挪走。

关掉 castellated / snap / layers 时，用输入表面 AABB（加 Bounds margin）直接生成笛卡尔 `VTK_HEXAHEDRON` 背景盒，不写 OpenFOAM ASCII、不调用 snappyHexMesh。格子尺寸：`Background cell size > 0` 用该值，否则取加 margin 后最长边的 1/16。

每次跑完（成功或失败）都保留 Foam case，并在 case 根目录写空的 `case.foam`。滤镜输出用 ParaView 自带的 `vtkOpenFOAMReader` 读 `internalMesh`，不再自己解析 `faces`/`owner`。**File → Open** 选 `case.foam` 可读整棵 case（含 patch 分块）。固定入口：`%TEMP%\shyx-snappy-last\case.foam`；当次目录路径写在同目录 `last-case-path.txt`。

参数见 `ParaViewPlugin/SHYXSnappyHexMesh.xml`。**Inside points** 列表可 Add insidePoint：选中一行后视图里出现可拖动手柄。空列表仍用 AABB 中心；多个点写成 OpenFOAM `locationsInMesh`（zone `none`）。点必须落在要保留的单元格内，不要贴在面上。

示例 dict：

- `example/snappyHexMeshDict`：与当前滤镜默认接近的可跑配置。
- `example/snappyHexMeshDict.official.zh`：OpenFOAM-v2412 官方 `etc/caseDicts/annotated/snappyHexMeshDict` 的中文注释译本（关键字仍是英文）。
