# SHYXSnappyHex

vespa 仓库内的 **adapter + CMake**，把官方 OpenFOAM 源码编成静态库，给 `vtkSHYXSnappyHexMesh` 提供 `shyx_snappy_run`。

官方 OpenFOAM 用 **wmake**，**不会**生成 `OpenFOAMConfig.cmake`。能 `find_package` 的是本目录安装出来的 `SHYXSnappyHexConfig.cmake`，不是 `OpenFOAM-v2412` 自己编出来的包。

vespa（MSVC / Visual Studio）把本目录当成 **clang-cl + Ninja ExternalProject**（`CMake/SHYXSnappyHexInTree.cmake`）。`VESPAPlugin.dll` 必须 `/WHOLEARCHIVE` `SHYXSnappyHex.lib`（`shyx_snappyhex_whole_archive`）。不要 WHOLEARCHIVE `OpenFOAM.lib` / `finiteVolume.lib`。`shyx_snappy_run` 在进程内调用。

滤镜说明见 [`vespa/shyx/SnappyHexMesh/README.md`](../vespa/shyx/SnappyHexMesh/README.md)。

## 源码与编译边界（不进仓库、不剪子集）

**只编译使用，不把 OpenFOAM 源码拉进 vespa。** 也不是从官方树里「提取一份 meshing 子集」拷进项目。

| 位置 | 有什么 | 会不会改 |
|------|--------|----------|
| **vespa git** `shyx-snappyhex/` | adapter、CMake、C API。没有 `src/OpenFOAM`。 | 本仓库维护 |
| **仓外** `FOAM_SOURCE_DIR`（例如 `C:\Users\18490\Documents\Github\OpenFOAM-v2412`） | 官方源码树（至少 `src/`、`wmake/`、`etc/`，以及 `applications/utilities/mesh/generation/snappyHexMesh`）。 | **只读**。不在该目录跑 `Allwmake`，不往里写 `.obj` / CMake |
| **构建目录** `vespa/build/shyx-snappyhex-build` | 按 `Make/files` 拷需要的 `.C`（给 Ninja / NTFS 大小写）、`lnInclude`、编出的 `.lib`。 | 临时产物，不进 git |
| **安装前缀** `vespa/build/shyx-snappyhex-install` | `SHYXSnappyHex.lib`、各 Foam `.lib`、`lib/cmake/SHYXSnappyHex/` | 给 vespa 链接用 |

链路：

```
OpenFOAM-v2412          ← 只读源码，不编译、不生成 CMake 包
        ↓ FOAM_SOURCE_DIR
vespa/shyx-snappyhex    ← 真正的 CMake 工程（clang-cl + Ninja）
        ↓ ExternalProject（编 vespa 时顺带编）
vespa/build/shyx-snappyhex-install/lib/cmake/SHYXSnappyHex
        ↓ 导入目标 SHYXSnappyHex::SHYXSnappyHex
VESPAPlugin.dll
```

### 编译范围（meshing 闭包）

磁盘上仍是完整官方树；编译器只吃 snappy 链得上的库（各库 `Make/files`）：

`OSspecific`、`Pstream`（dummy）、`OpenFOAM`、`fileFormats`、`surfMesh`、`meshTools`、`blockMesh`、`extrudeModel`、`finiteVolume`、`dynamicMesh`、`dynamicFvMesh`、`lagrangian`、`sampling`、`fvMotionSolver`、`decompositionMethods`、`decompose`、`distributed`、`reconstruct`、`overset`、`snappyHexMesh`。

不编 solver、tutorials、ThirdParty。还会跳过 Flex / Ragel / Lemon 等 TU；`debug.C` / `error.C` / `IOerror.C` 由 adapter overlay 进 `lnInclude`（`globals.C` 会 `#include` 它们），`IOobject.C` 换成 adapter TU。

换行：本工程不转换 `FOAM_SOURCE_DIR` 的 LF/CRLF。clang-cl 两种都能编；WSL 同步过来的官方树保持 LF 即可。不要为了 vespa 去改 `OpenFOAM-v2412` 整树换行。

`FOAM_SOURCE_DIR` 与构建目录需要 **NTFS 大小写敏感**（`lduMatrix` / `LduMatrix` 必须能共存）。`SHYX_OPENFOAM_VERSION`（默认 `2412`）是 WM 宏，可换更新的官方树。

vespa 探测顺序见 `CMake/SHYXSnappyHexInTree.cmake`：先 `../OpenFOAM-v2412`，再若干旧 `third_party` 路径。不要 patch 官方树；行为分叉只放本目录 `adapter/`。

## Adapter vs OpenFOAM tree

| Piece | Role |
|-------|------|
| `adapter/foam_debug.cxx` | Overlay `lnInclude/debug.C`（`globals.C` include）。LoadLibrary 用内存 `controlDict`，缺 switch 时 fallback，避免 `FatalError`。 |
| `adapter/foam_error.cxx` / `foam_IOerror.cxx` | Overlay `error.C` / `IOerror.C`。`throwExceptions()` 打开后，`abort()` 也抛异常（官方只对 `exit()` 抛；Windows 上 `abort()` 会 `std::exit` 杀掉 ParaView）。 |
| `adapter/foam_IOobject.cxx` | 替换 `Make/files` 里那个 TU。`getOrDefault(..., timeStamp)`，静态初始化缺 key 时不 `FatalIOError`。 |
| `src/foam_eval_stubs.cxx` / `foam_stl_flex_stub.cxx` | 跳过的 Flex/Lemon/Ragel TU 的符号。 |
| `src/foam_env_early.cxx` | Foam 静态构造前设 `FOAM_SIGFPE` / `FOAM_ABORT`。 |
| `src/foam_force_link.cxx` | MSVC RTS 注册。 |
| `src/snappyHexMesh_main.cxx` | `#define main` 包官方 `snappyHexMesh.C`。 |
| `cmake/msvc_foam_compat.h` | Forced include（`pid_t`、Windows `ERROR` 宏）。 |
| `cmake/FoamLibrary.cmake` | 解析 `Make/files` → STATIC libs；`foam_overlay_lninclude`。 |

跑 snappy 时把内嵌 `etc/cellModels` 写到 `%TEMP%/shyx-openfoam`。

## Layout

- `include/shyx_snappy.h` — C API
- `adapter/` — OpenFOAM TU 替换（overlay / 跳过官方 TU）
- `src/` — case writer、C API、Windows spawn、RTS force-link
- `apps/snappy_cli.cxx` — 可选 CLI（`SHYX_BUILD_CLI`）
- `cmake/` — Foam 库辅助 + `/WHOLEARCHIVE`
- `scripts/vespa-ep.ps1` — vespa ExternalProject：configure / build / install

## 单独编译（产出 CMake 包）

编的是**本目录**，源码仍指向仓外官方树。产物不在 `OpenFOAM-v2412` 里。

```powershell
$foam = "C:\Users\18490\Documents\Github\OpenFOAM-v2412"
$src  = "C:\Users\18490\Documents\Github\vespa\shyx-snappyhex"
$bin  = "C:\Users\18490\Documents\Github\vespa\build\shyx-snappyhex-build"
$pref = "C:\Users\18490\Documents\Github\vespa\build\shyx-snappyhex-install"
$ps1  = "$src\scripts\vespa-ep.ps1"

New-Item -ItemType Directory -Force -Path $bin,$pref | Out-Null
fsutil.exe file setCaseSensitiveInfo $bin enable

powershell -NoProfile -ExecutionPolicy Bypass -File $ps1 `
  -Step configure -Source $src -Binary $bin -Prefix $pref -FoamDir $foam -Version 2412
powershell -NoProfile -ExecutionPolicy Bypass -File $ps1 -Step build -Binary $bin
powershell -NoProfile -ExecutionPolicy Bypass -File $ps1 -Step install -Binary $bin -Prefix $pref
```

安装后的 CMake 包：

`vespa/build/shyx-snappyhex-install/lib/cmake/SHYXSnappyHex/SHYXSnappyHexConfig.cmake`

vespa 当前**不** `find_package(SHYXSnappyHex)`，而是 in-tree ExternalProject 编完再链。工具链：VS LLVM `clang-cl`、Ninja、`/MD`；构建目录必须 NTFS 大小写敏感。

License: OpenFOAM is GPLv3. 与 OpenFOAM 链接时本 adapter 按同样条款分发。
