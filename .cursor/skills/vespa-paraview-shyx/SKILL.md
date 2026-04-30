---
name: vespa-paraview-shyx
description: >-
  Maps the VESPA/vespa “shyx” ParaView plugin layout (VTK modules, CMake, server-manager XML) and
  the steps to add filters like those under vespa/shyx. Use when building or extending SHYX ParaView
  filters, vtkSHYX* classes, ParaViewPlugin XML, or when the user asks how shyx plugins are wired
  into VESPAPlugin.
---

# VESPA: SHYX-style ParaView 插件架构

在实现或扩展 `vespa/shyx` 下与 ParaView 集成的算子/表示层之前，先按下面顺序**扫一遍现状**，再按**清单**落代码与构建改动。

## 1. 架构总览（必读）

- **顶层**：根目录 `CMakeLists.txt` 用 `vtk_module_find_modules(... "${CMAKE_CURRENT_SOURCE_DIR}/vespa")` 发现所有 `vtk.module`，再 `vtk_module_scan` / `vtk_module_build` 把模块编进 VESPA 的 CMake export（`vespa`）。
- **SHYX 实现**：每个功能在 `vespa/shyx/<FeatureName>/` 下；至少包含 `vtk.module` 与 `CMakeLists.txt`（`vtk_module_add_module`，`shyx` 里通常带 `FORCE_STATIC`）。
- **ParaView 插件包**：`ParaViewPlugin/CMakeLists.txt` 中 `paraview_add_plugin(VESPAPlugin ...)` 的 `SERVER_MANAGER_XML` 列表注册每个 `SHYX*.xml`；**一个 DLL（VESPAPlugin）** 里打包多个 proxy，而不是每个 shyx 算子一个独立插件目标。
- **注册入口**：`ParaViewPlugin/paraview.plugin` 只描述插件名；`paraview_plugin_build` 与主工程里的 `VESPA_BUILD_PV_PLUGIN` 一起驱动构建。
- **与 CGAL 的边界**：很多算子基于 `vespa/` 里 vtkCGAL* 或 `vtkCGALPolyDataAlgorithm` 等；纯 VTK 算子只依赖 `VTK::Filters*`。依赖写在对应目录的 `vtk.module` 的 `DEPENDS` / `PRIVATE_DEPENDS`。

**构建注意**：`VESPA_BUILD_PV_PLUGIN=ON` 时，工程会 **关闭** 共享库以便 vespa 模块以静态方式链进 `VESPAPlugin.dll`；根 `CMakeLists.txt` 中有说明。新增模块后不需要单独为 shyx 配置 PATH 安装 DLL，除非改动了插件/VTK 的加载方式。

## 2. 调查“当前”代码时应看的文件

按优先级阅读（可配合仓库搜索）：

| 目的 | 看哪里 |
|------|--------|
| 全仓库有哪些 SHYX/vespa 模块 | `vtk_module_find_modules` 会扫整个 `vespa/` 下 `vtk.module`；直接 `ls vespa/shyx/` 最快 |
| 新模块是否进构建、是否被条件排除 | 根 `CMakeLists.txt` 里对 `vtkcgal_module_files` 的 `list(FILTER ... EXCLUDE ...)`：VMTK、CGAL 版本、非 ParaView 下排除 `Representation` 等 |
| 单模块 CMake 与类列表 | `vespa/shyx/<Name>/CMakeLists.txt`（`set(classes ...)` + `vtk_module_add_module`） |
| 模块对外 VTK 依赖 | 同目录 `vtk.module` 的 `NAME` / `DEPENDS` / `PRIVATE_DEPENDS` / `GROUPS` |
| ParaView UI/proxy 定义 | `ParaViewPlugin/SHYX<Name>.xml`（`SourceProxy` 的 `class="vtkSHYX..."` 与属性） |
| 哪些 XML 已挂进插件 | `ParaViewPlugin/CMakeLists.txt` → `paraview_add_plugin` → `SERVER_MANAGER_XML` 列表 |
| 菜单/图标 | XML 里 `<Hints><ShowInMenu category="SHYX" .../></Hints>`；资源常挂在 `ParaViewPlugin/VESPAIcons.qrc` |
| 需要 Qt 的客户端逻辑 | 少数在 `vespa/shyx/...` 的 `pq*` 源文件，在 `ParaViewPlugin/CMakeLists.txt` 里以 `paraview_plugin_add_auto_start` 或 `UI_INTERFACES` / `SOURCES` 显式列出；并 `target_link_libraries(VESPAPlugin ... ParaView::...)` |
| 可选的聚合 XML | 例如 `ParaViewPlugin/VESPAFilters.xml`：是否要把新算子也列进去取决于项目惯例，以现有同类型算子为准 |

**搜索技巧**：在仓库内 `rg "vtkSHYX"`, `rg "SHYX" ParaViewPlugin`, `rg "vtk_module_add_module" vespa/shyx` 可快速定位命名与已接入项。

## 3. 参考实现（选一类对照）

- **多端口 + CGAL 诊断/修复**：`vespa/shyx/MeshChecker/` + `ParaViewPlugin/SHYXMeshChecker.xml`；类继承自 `vtkCGALPolyDataAlgorithm`，`vtk.module` 依赖 `vtkCGALAlgorithm` / `vtkCGALPMP` 等。
- **纯 VTK 链**：`vespa/shyx/ConvexHullFilter/` + `SHYXConvexHullFilter.xml`；`vtk.module` 仅 `VTK::FiltersCore` / `Geometry` 等。

新增功能时，**在 shyx 里找输入/输出类型最相近的已有目录**，复用其 CMake/`vtk.module`/XML 结构，而不是从零猜 ParaView 域名。

## 4. 新增一个 SHYX 类 ParaView 算子（检查清单）

复制本清单，逐项打勾：

1. **目录** `vespa/shyx/<YourName>/`：实现 `vtkSHYX<YourName>.h` / `.cxx`（及模块导出头若生成脚本需要）。
2. **vtk.module**：`NAME` 与 `CMakeLists` 中 `vtk_module_add_module( NAME ...)` 一致；`DEPENDS` 覆盖算法用到的 VTK 模块与 CGAL 封装（若有）。
3. **CMakeLists.txt**：`set(classes vtkSHYX<YourName>)`，`vtk_module_add_module(... FORCE_STATIC CLASSES ${classes})`；需要 CGAL/TBB/第三方时在目标上 `vtk_module_link` 或 `target_link_libraries`（与旁邻 shyx 目录同风格）。
4. **C++ 约定**：
   - 类名 `vtkSHYX*`；基类与项目内同类算子一致（`vtkImageAlgorithm` / `vtkPolyDataAlgorithm` / `vtkCGALPolyDataAlgorithm` 等）。
   - `Set*` / `Get*` 与 XML 里 `command="Set..."` 一致；多输出端口在 XML 中声明 `<OutputPort ... index="N"/>`。
5. **Server-manager XML** `ParaViewPlugin/SHYX<YourName>.xml`：`<SourceProxy class="vtkSHYX<YourName>" label="...">`，`Input` / 属性与 Hints 与现有一致风格。
6. **注册 XML**：在 `ParaViewPlugin/CMakeLists.txt` 的 `SERVER_MANAGER_XML` 中**追加**一行 `"SHYX<YourName>.xml"`（保持字母顺序可维护性更好，与现有块一致即可）。
7. **若需图标**：`VESPAIcons.qrc` 增加资源并在 XML `ShowInMenu` 里引用 `:/...` 路径（对照已有条目）。
8. **若根 CMake 有条件排除**：如果新目录名命中 `list(FILTER ... EXCLUDE` 的规则，或依赖可选组件（VMTK/CGAL 6），在根 `CMakeLists.txt` 的注释与逻辑中保持一致，并在 README 里说明开启方式。

**不必改**：`paraview.plugin` 的 NAME/描述在增加普通 filter 时通常不需要动；`vtk_module_find_modules` 会自动发现新 `vtk.module`。

## 5. 与“仅 VTK 库/测试”开发模式的差异

- `VESPA_BUILD_PV_PLUGIN` **关闭** 时，代表类仍通过 `vtk_module_build` 编译，但**不会**走 `ParaViewPlugin/`，`Representation` 类会从模块列表里被过滤掉。若新算子只在 ParaView 用，可接受；若需无 ParaView 的库用途，不要依赖 `ParaView::` 头文件。

## 6. 常见失败点

- **XML 里 `class` 与 VTK 类名不一致**：ParaView 找不到 `vtkObjectFactory` 注册的类，或在服务端报错。
- **忘把 XML 加进 `SERVER_MANAGER_XML`**：算子 C++ 已进 DLL，但 UI 不出现。
- **vtk.module 少写了依赖**：链接期或 `vtk_header_dep` 类错误。
- **条件构建**：VMTK/CGAL 6 相关模块被根 CMake 排除后，与 XML 仍引用该 filter 会不一致——改 CMake 或改 XML/文档，避免“一半发布”。

完成一次端到端修改后，应用户环境重新配置/编译 ParaView 插件目标（工程里为 `VESPAPVPLUGIN` 相关与 `VESPAPlugin` 产物，以本仓库 CMake 为准）并在 ParaView 中 `Tools → Manage Plugins` 验证加载与菜单项。

## 7. 附：仓库内关键路径

- 根构建与模块扫描：`CMakeLists.txt`（`vtk_module_find_modules` / `vtk_module_build` / `paraview_plugin_build` 段）
- 插件与 XML 列表：`ParaViewPlugin/CMakeLists.txt`
- SHYX 实现根：`vespa/shyx/*/`
- **ParaView 上游源码（本地参考）**：`C:\SoftWare\ParaView` — 可对照 Proxy、Server Manager XML、插件 CMake、`paraview_add_plugin` 等与版本一致的实现；本路径为开发机约定，若不存在则以实际安装的 ParaView 源码目录为准。

更细的 ParaView 代理 XML 域（`DataTypeDomain`、`IntVectorProperty` 等）以**已有 `SHYX*.xml`** 为模板，查阅 ParaView 版本对应文档为辅。
