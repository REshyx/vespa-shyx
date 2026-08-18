---
name: vespa-paraview-shyx
description: >-
  Maps the VESPA/vespa “shyx” ParaView plugin layout (VTK modules, CMake, server-manager XML)
  and how SHYX capabilities are wired: pipeline filters, Display representations (Pulse Glyphs,
  Animated Streamline, Point Label), RenderView title-bar selection tools, and the SHYX AI
  Assistant catalog/parameter tools (list_filters, lookup_shyx_docs, describe_proxy). Use when
  building or extending SHYX filters, vtkSHYX* classes, ParaViewPlugin XML, RepresentationType
  hints, custom representations, Sphere/Grow selection, or when adding a SHYX feature that the
  assistant cannot list or describe_proxy. New non-filter features must be wired into describe_proxy
  or the assistant will only see Filters.
---

# VESPA: SHYX-style ParaView 插件架构

在实现或扩展 `vespa/shyx` 下与 ParaView 集成的算子/表示层之前，先按下面顺序**扫一遍现状**，再按**清单**落代码与构建改动。

## 1. 架构总览（必读）

- **顶层**：根目录 `CMakeLists.txt` 用 `vtk_module_find_modules(... "${CMAKE_CURRENT_SOURCE_DIR}/vespa")` 递归发现所有 `vtk.module`，再 `vtk_module_scan` / `vtk_module_build` 编进 export（`vespa`）。`vespa/` 按**作者/来源**分第一层目录，不是按 CGAL 堆模块：
  - `vespa/Algorithm/`：公用 VTK↔CGAL 辅助（`vtkCGALPolyDataAlgorithm`），谁用谁 `DEPENDS`。
  - `vespa/Core/`：公用纯 VTK 工具（`vtkVESPAAttributeTransfer` 形变后点/胞数据映射），不拉 CGAL。
  - `vespa/vespa/`：Kitware 原版 VESPA（`vtkCGAL*`）。
  - `vespa/shyx/`：SHYX；新作者应新建 `vespa/<id>/`，不要往 shyx 或 vespa/vespa 里塞。
- **SHYX 不只是 Filters 菜单**。用户可见能力分几类（清单见 `vespa/INVENTORY.md`；AI 查询约定见 **§9**）：
  - **Pipeline filter / source**：`vespa/shyx/<Feature>/` + `ParaViewPlugin/SHYX*.xml`，`ProxyGroup name="filters"`（或 sources）。
  - **Display 表示**：`PulseGlyphRepresentation` / `AnimatedStreamlineRepresentation` / `PointLabelRepresentation`，挂在 Display 下拉框，不是 pipeline 节点。
  - **RenderView 标题栏选择工具**：`ParaViewPlugin/SphereSelection/`、`GrowSelectionWithSimilar/`，纯客户端 Qt，**没有** SM proxy。
  - **3D widget 表示**：支架/圆柱（`SHYX*WidgetRepresentation.xml`），不是 Display 下拉项。
  - **View 停靠窗**：SHYX AI Assistant（`ParaViewPlugin/pqSHYXAI*`），不是 pipeline filter。
- **SHYX 实现**：每个 **VTK 算子或表示** 在 `vespa/shyx/<FeatureName>/` 下；至少包含 `vtk.module` 与 `CMakeLists.txt`（`vtk_module_add_module`，`shyx` 里通常带 `FORCE_STATIC`）。标题栏选择工具只有 `ParaViewPlugin/` 下的 `pq*`。AI 面板 UI 是 `pqSHYXAI*`；`vespa/shyx/AIAssistant/` 里仍有遗留 `vtkSHYXAIAssistant`，对应 XML **不要**再写入 `SERVER_MANAGER_XML`。
- **ParaView 插件包**：`ParaViewPlugin/CMakeLists.txt` 中 `paraview_add_plugin(VESPAPlugin ...)` 的 `SERVER_MANAGER_XML` 列表注册每个 `SHYX*.xml`；**一个 DLL（VESPAPlugin）** 聚合已构建模块，而不是每个算子一个插件目标。
- **注册入口**：`ParaViewPlugin/paraview.plugin` 只描述插件名；`paraview_plugin_build` 与主工程里的 `VESPA_BUILD_PV_PLUGIN` 一起驱动构建。
- **与 CGAL 的边界**：只有真正调用 CGAL 的算子才依赖 `vtkCGALAlgorithm` / `vtkCGALPolyDataAlgorithm`；纯 VTK / TetGen / VMTK 算子只写自己的 `DEPENDS`。根 `CMakeLists.txt` 在扫完 `vtk.module` 后，仅当仍有模块声明 `vtkCGALAlgorithm` 或 `CGAL::CGAL` 且 `VESPA_USE_CGAL=ON` 时才 `find_package(CGAL)`。

**构建注意**：`VESPA_BUILD_PV_PLUGIN=ON` 时，工程会 **关闭** 共享库以便 vespa 模块以静态方式链进 `VESPAPlugin.dll`；根 `CMakeLists.txt` 中有说明。新增模块后不需要单独为 shyx 配置 PATH 安装 DLL，除非改动了插件/VTK 的加载方式。SnappyHexMesh 的 OpenFOAM 适配器在仓库根 `shyx-snappyhex/`（不改官方源码）；`FOAM_SOURCE_DIR` 指向仓外只读树，由 vespa CMake 用 clang-cl/Ninja ExternalProject 编进来。

## 2. 调查“当前”代码时应看的文件

按优先级阅读（可配合仓库搜索）：

| 目的 | 看哪里 |
|------|--------|
| 全仓库有哪些 VTK 模块 | `vtk_module_find_modules` 扫整个 `vespa/`；作者目录见 `vespa/README.md`；对照表 `vespa/INVENTORY.md` |
| 新模块是否进构建、是否被条件排除 | 根 `CMakeLists.txt` 里对 `vespa_module_files` 的 `list(FILTER ... EXCLUDE ...)`：VMTK、CGAL 版本、非 ParaView 下排除 `Representation` 等 |
| SHYX 实现根 | `vespa/shyx/*/` |
| 上游 VESPA（CGAL） | `vespa/vespa/*/` |
| 单模块 CMake 与类列表 | `vespa/shyx/<Name>/CMakeLists.txt`（`set(classes ...)` + `vtk_module_add_module`） |
| 模块对外 VTK 依赖 | 同目录 `vtk.module` 的 `NAME` / `DEPENDS` / `PRIVATE_DEPENDS` / `GROUPS`（`Core` `Meshing` `Vascular` `Flow` `PointCloud`；表示层 `ParaView`） |
| ParaView UI/proxy 定义 | `ParaViewPlugin/SHYX<Name>.xml`（`SourceProxy` 的 `class="vtkSHYX..."` 与属性） |
| 哪些 XML 已挂进插件 | `ParaViewPlugin/CMakeLists.txt` → `paraview_add_plugin` → `SERVER_MANAGER_XML` 列表 |
| 菜单/图标 | XML 里 `<Hints><ShowInMenu category="SHYX" .../></Hints>`；资源常挂在 `ParaViewPlugin/VESPAIcons.qrc` |
| 某输出端口默认表示法（如 Point Gaussian） | `<Hints>` 里 `<RepresentationType view="RenderView" type="..." port="N"/>`；**§8.6.7** |
| 需要 Qt 的客户端逻辑 | `ParaViewPlugin/` 下的 `pq*`（含 Pulse Glyph / Animated Streamline 动画管理器、Sphere / Grow 选择）；用 `paraview_plugin_add_auto_start` 或 `UI_INTERFACES` / `SOURCES` 列出 |
| SHYX AI Assistant 能力目录与参数查询 | `ParaViewPlugin/pqSHYXAIAgentTools.cxx`（`list_filters` / `lookup_shyx_docs` / `describe_proxy`）；系统提示 `pqSHYXAIAssistantPanel.cxx` 的 `kSystemPrompt`；**§9** |
| 上游 VESPA 的聚合 XML | `ParaViewPlugin/VESPAFilters.xml` 只注册 Kitware 原版滤镜；**不要**把 SHYX 代理再塞回去。SHYX 一律 `SHYX*.xml` |

**搜索技巧**：在仓库内 `rg "vtkSHYX"`, `rg "SHYX" ParaViewPlugin`, `rg "vtk_module_add_module" vespa/shyx` 可快速定位命名与已接入项。

## 3. 参考实现（选一类对照）

- **多端口 + CGAL 诊断/修复**：`vespa/shyx/MeshChecker/` + `ParaViewPlugin/SHYXMeshChecker.xml`；类继承自 `vtkCGALPolyDataAlgorithm`，`vtk.module` 依赖 `vtkCGALAlgorithm` / `vtkCGALPMP` 等。
- **纯 VTK 链**：`vespa/shyx/ConvexHullFilter/` + `SHYXConvexHullFilter.xml`；`vtk.module` 仅 `VTK::FiltersCore` / `Geometry` 等。
- **CGAL PMP + ProtectAngle/FeatureMask 约束 + 多输出**：`vespa/shyx/AdaptiveIsotropicRemesher/` + `ParaViewPlugin/SHYXAdaptiveIsotropicRemesher.xml`；XML 用多 `PropertyGroup` 分节、`panel_visibility="advanced"` 整组进 Advanced、`enabled_state` + `inverse=1` 做条件灰显。可作为「面板布局」模板。
- **single proxy / 多算法切换**：`vespa/shyx/ShapeSmoothing/` + `ParaViewPlugin/SHYXShapeSmoothing.xml`；顶层 `SmoothingMethod` 枚举驱动 `smooth_shape` / `angle_and_area_smoothing` / `fair` 三套子参数，每套挂 `GenericDecorator value="N"`，跨算法共享的属性用 `values="0 1"`。详见 **§8.6.5**。

新增功能时，**在 shyx 里找输入/输出类型最相近的已有目录**，复用其 CMake/`vtk.module`/XML 结构，而不是从零猜 ParaView 域名。

## 4. 新增一个 SHYX 类 ParaView 算子（检查清单）

复制本清单，逐项打勾：

1. **目录** `vespa/shyx/<YourName>/`：实现 `vtkSHYX<YourName>.h` / `.cxx`（及模块导出头若生成脚本需要）。
2. **vtk.module**：`NAME` 与 `CMakeLists` 中 `vtk_module_add_module( NAME ...)` 一致；`DEPENDS` 覆盖算法用到的 VTK 模块与 CGAL 封装（若有）。
3. **CMakeLists.txt**：`set(classes vtkSHYX<YourName>)`，`vtk_module_add_module(... FORCE_STATIC CLASSES ${classes})`；需要 CGAL/TBB/第三方时在目标上 `vtk_module_link` 或 `target_link_libraries`（与旁邻 shyx 目录同风格）。
4. **C++ 约定**：
   - 类名 `vtkSHYX*`；基类与项目内同类算子一致（`vtkImageAlgorithm` / `vtkPolyDataAlgorithm` / `vtkCGALPolyDataAlgorithm` 等）。
   - `Set*` / `Get*` 与 XML 里 `command="Set..."` 一致；多输出端口在 XML 中声明 `<OutputPort ... index="N"/>`。
5. **Server-manager XML** `ParaViewPlugin/SHYX<YourName>.xml`：`<SourceProxy class="vtkSHYX<YourName>" label="...">`，`Input` / 属性与 Hints 与现有一致风格。布局上按功能切 `<PropertyGroup>` 分节、把整组次要项压到 `panel_visibility="advanced"`、条件性子项用 `GenericDecorator`（`enabled_state` 或 `visibility`，按 §8.6.3 的取舍）。多算法 single-proxy 走 §8.6.5 模板。
6. **注册 XML**：在 `ParaViewPlugin/CMakeLists.txt` 追加 `"SHYX<YourName>.xml"`。纯 VTK / TetGen 等放无条件 `SERVER_MANAGER_XML` 列表；多数 CGAL 滤镜放 `SHYX_CGAL_SERVER_MANAGER_XML`（随 `VESPA_BUILT_WITH_CGAL`）。CGAL ≥ 6 重网格 → `ADAPTIVE_REMESHING_SERVER_MANAGER_XML`；Alpha Wrap / Selection Fill → 随内部变量 `VESPA_ALPHA_WRAPPING`；VMTK / Snappy → 各自 `if()` 列表。**不要**写入 `VESPAFilters.xml`。
7. **若需图标**：`VESPAIcons.qrc` 增加资源并在 XML `ShowInMenu` 里引用 `:/...` 路径（对照已有条目）。
8. **若根 CMake 有条件排除**：如果新目录名命中 `list(FILTER ... EXCLUDE` 的规则，或依赖可选组件（VMTK/CGAL 6），在根 `CMakeLists.txt` 的注释与逻辑中保持一致，并在 README 里说明开启方式。
9. **AI Assistant（必做，见 §9）**：UI 进插件 ≠ 助手能列出或查出参数。普通 filter 一般能被 `describe_proxy` 扫到；**Display 表示、无 proxy 的客户端工具、以及需要用法备注的 filter 必须在 `pqSHYXAIAgentTools.cxx` 里补上**，并改 `kSystemPrompt`。漏了助手就只看得到 Filters。
10. **对照表**：`vespa/INVENTORY.md`（及 shyx README / 根目录功能说明，若该功能要出现在文档里）。

**不必改**：`paraview.plugin` 的 NAME/描述在增加普通 filter 时通常不需要动；`vtk_module_find_modules` 会自动发现新 `vtk.module`。

## 5. 与“仅 VTK 库/测试”开发模式的差异

- `VESPA_BUILD_PV_PLUGIN` **关闭** 时，代表类仍通过 `vtk_module_build` 编译，但**不会**走 `ParaViewPlugin/`，`Representation` 类会从模块列表里被过滤掉。若新算子只在 ParaView 用，可接受；若需无 ParaView 的库用途，不要依赖 `ParaView::` 头文件。

## 6. 常见失败点

- **XML 里 `class` 与 VTK 类名不一致**：ParaView 找不到 `vtkObjectFactory` 注册的类，或在服务端报错。
- **忘把 XML 加进 `SERVER_MANAGER_XML`**：算子 C++ 已进 DLL，但 UI 不出现。
- **vtk.module 少写了依赖**：链接期或 `vtk_header_dep` 类错误。
- **条件构建**：VMTK/CGAL 6 相关模块被根 CMake 排除后，与 XML 仍引用该 filter 会不一致——改 CMake 或改 XML/文档，避免“一半发布”。
- **自定义 `panel_widget` 整组 backing 都 `never`**：`pqProxyWidget` 跳过这些属性，**根本不创建**组 widget。至少留一个可见锚点（§8.6.8）。
- **AI Assistant 没补 describe_proxy**：新功能已经在菜单/Display/标题栏出现，但助手问「有什么 / 参数」时仍只有 filter。**§9 是加功能清单的一部分，不是事后文档。**

完成一次端到端修改后，应用户环境重新配置/编译 ParaView 插件目标（工程里为 `VESPAPVPLUGIN` 相关与 `VESPAPlugin` 产物，以本仓库 CMake 为准）并在 ParaView 中 `Tools → Manage Plugins` 验证加载与菜单项。

## 7. 附：仓库内关键路径

- 根构建与模块扫描：`CMakeLists.txt`（`vtk_module_find_modules` / `vtk_module_build` / `paraview_plugin_build` 段）；变量名为 `vespa_module_files` / `vespa_provided_modules`
- 作者目录约定：`vespa/README.md`
- 插件与 XML 列表：`ParaViewPlugin/CMakeLists.txt`
- SHYX 实现根：`vespa/shyx/*/`
- 上游 VESPA（CGAL）：`vespa/vespa/*/`
- 公用 CGAL 辅助：`vespa/Algorithm/`
- 模块对照表：`vespa/INVENTORY.md`（滤镜 / 表示 / 仅客户端 Qt）
- AI Assistant 工具：`ParaViewPlugin/pqSHYXAIAgentTools.cxx`（**§9**）
- **ParaView 上游源码（本地参考）**：`C:\SoftWare\ParaView` — 可对照 Proxy、Server Manager XML、插件 CMake、`paraview_add_plugin` 等与版本一致的实现；本路径为开发机约定，若不存在则以实际安装的 ParaView 源码目录为准。

更细的 **Domain / Decorator** 与常见 Hints 见 **§8**。其余 ParaView SM 细节以**已有 `SHYX*.xml`** 为模板，对照本地 `ParaView` 源码（如 `Remoting/ServerManager`、`Qt/ApplicationComponents`）与官方文档。

## 8. ParaView 属性面板 UI（Domain / Decorator / 布局）

### 8.1 Domain vs Decorator（分工）

| 概念 | 所在层 | 作用 |
|------|--------|------|
| **`XXXDomain`**（`BoundsDomain` / `IntRangeDomain` / `EnumerationDomain` / `ArrayListDomain` …） | Server Manager：写在**属性节点下** | 约束/推导该属性的**取值范围、枚举项、与 Input 关联的数组列表**等，为面板提供合法值与建议。 |
| **`PropertyWidgetDecorator`**（`Hints` 里 `type="GenericDecorator"` 等） | 客户端 Qt：写在属性的 `<Hints>` 里 | 根据**其它属性或数据状态**控制控件的**显示/隐藏**或**启用/禁用**（与 Domain 正交）。 |

### 8.2 插件能否使用、能否自定义

- **使用**：在 **`ParaViewPlugin/SHYX*.xml`** 里直接写已存在的 Domain 名与 Decorator 的 `type=` 即可（需与所链 ParaView 版本一致）。
- **自定义 Domain**：通常需 **C++** 子类化 `vtkSMDomain` 并注册；仅 XML 发明新标签名一般不可行。多数 shyx 插件只组合现有 Domain。
- **自定义 Decorator**：**可以**。上游示例 `ParaView/Examples/Plugins/PropertyWidgets/Plugin/`：继承 `pqPropertyWidgetDecorator`，通过 `pqPropertyWidgetInterface::createWidgetDecorator()` 按 XML `type="..."` 分派；插件注册一个**额外的** `pqPropertyWidgetInterface` 与标准实现并列。若需 server-side 共用逻辑，走 `vtkPropertyDecorator`（如 `vtkGenericPropertyDecorator`）再由 Qt 侧包装。

### 8.3 内置 `createWidgetDecorator` 识别的 `type`

参考 `Qt/ApplicationComponents/pqStandardPropertyWidgetInterface.cxx`：`GenericDecorator`、`CompositeDecorator`、`EnableWidgetDecorator`、`ShowWidgetDecorator`、`InputDataTypeDecorator`、`MultiComponentsDecorator`、`OSPRayHidingDecorator`、`SessionTypeDecorator`、`CTHArraySelectionDecorator`。另有自动附加的 `pqAnimationShortcutDecorator`（不写进 XML）。

### 8.4 `GenericDecorator`（`vtkGenericPropertyDecorator`）

| 属性 | 说明 |
|------|------|
| `mode` | `visibility` → 显示/隐藏；`enabled_state` → 可见但灰显/可编辑 |
| `property` / `index` | 观察的 SM 属性名 + 可选元素下标 |
| `value` | 与 `vtkVariant::ToString()` **相等** 即匹配 |
| `values="a b c"` | 空格分隔，**任一相等**即匹配（OR） |
| `inverse="1"` | 对匹配结果取反 |
| `number_of_components="N"` | 仅适用于带 `ArrayListDomain` 的典型 5 元组数组选择字符串属性，按当前选中数组在输入上的分量数决定 |

特殊：被观察属性 **0 个元素** + `value="null"` 表示「无 proxy」分支；`vtkSMProxyProperty + ProxyListDomain` 另有按子 proxy XML 名匹配的逻辑。

### 8.5 `CompositeDecorator` 与多 decorator 的逻辑组合

- 根节点 **`<Expression type="and|or">`** 可嵌套子 `<Expression>` 与叶 `<PropertyWidgetDecorator type="GenericDecorator" .../>`。
- **同一控件挂多个并列 decorator**（在 `<Hints>` 里平级写两个 `<PropertyWidgetDecorator>`） → pqProxyWidget 取**逻辑 AND**（任一 `canShowWidget=false` 即隐藏）。**OR 必须用 `CompositeDecorator + Expression type="or"`**，不要写两个 `value=0` / `value=1` 的 GenericDecorator（结果永远 false）。

完整骨架：
```xml
<PropertyWidgetDecorator type="CompositeDecorator">
  <Expression type="or">
    <PropertyWidgetDecorator type="GenericDecorator"
                             mode="visibility" property="Mode" value="A"/>
    <Expression type="and">
      <PropertyWidgetDecorator type="GenericDecorator"
                               mode="visibility" property="Mode" value="B"/>
      <PropertyWidgetDecorator type="GenericDecorator"
                               mode="visibility" property="UseAdvanced" value="1"/>
    </Expression>
  </Expression>
</PropertyWidgetDecorator>
```

### 8.6 实战配方（cookbook）

#### 8.6.1 「非 0 / 非空才启用」用 `value="0" inverse="1"` / `value="" inverse="1"`

`enabled_state` 优于 `visibility` 的典型场景（控件存在感保留，避免面板跳动）：

```xml
<DoubleVectorProperty name="ShapeSmoothingTimeStep" command="SetShapeSmoothingTimeStep" ...>
  ...
  <Hints>
    <PropertyWidgetDecorator type="GenericDecorator"
                             mode="enabled_state"
                             property="ShapeSmoothingIterations"
                             value="0"
                             inverse="1"/>
  </Hints>
</DoubleVectorProperty>
```
含义：「Iterations==0 → 灰显；非 0 → 可编辑」。`value="" inverse="1"` 同理表示「非空字符串才启用」。

仓库内例子：`ParaViewPlugin/SHYXAdaptiveIsotropicRemesher.xml` 的 `ShapeSmoothingTimeStep`。

#### 8.6.2 OR 多值用 `values="a b c"`

```xml
<!-- 仅在 SmoothingMethod 为 0 或 1 时显示 NumberOfIterations -->
<PropertyWidgetDecorator type="GenericDecorator"
                         mode="visibility"
                         property="SmoothingMethod"
                         values="0 1"/>
```

仓库内例子：`ParaViewPlugin/SHYXShapeSmoothing.xml` 的 `NumberOfIterations`。

#### 8.6.3 `enabled_state` vs `visibility` 的取舍

| 场景 | 推荐 | 理由 |
|------|------|------|
| 主开关关时整组**逻辑上无意义**（如整块 FeatureMask 子项依赖 FeatureMaskEnabled=1） | `visibility` | 节省垂直空间，避免误编辑 |
| 子选项概念上属于本组、只是上下文偶尔被 CGAL 忽略（如 `RemeshCollapseConstraints` 在 `Protect` 时 ignored） | `enabled_state` | 控件常驻；面板不跳动；用户能看见它存在 |
| 不同算法/模式切换出的不同子参数集 | `visibility` | 跨算法切换时面板差异显著，隐藏更清晰 |

#### 8.6.4 整组进 Advanced 面板

`panel_visibility` 既可写在单个属性上，也可写在 **`<PropertyGroup>`** 上；整组同档时写在组上最简洁：

```xml
<PropertyGroup label="Remesh operations (CGAL)" panel_visibility="advanced">
  <Property name="RemeshDoSplit"/>
  <Property name="RemeshDoCollapse"/>
  <Property name="RemeshDoFlip"/>
</PropertyGroup>
```

仓库内例子：`ParaViewPlugin/SHYXAdaptiveIsotropicRemesher.xml`「REMESH OPERATIONS」段。

#### 8.6.5 按算法/模式切换整页面板（single proxy, multi algorithm）

当一个 `SourceProxy` 内置多种算法时（例 `SHYXShapeSmoothing.xml`：`smooth_shape` / `angle_and_area_smoothing` / `fair`）：

1. **顶层主开关**：`IntVectorProperty + EnumerationDomain`，命名如 `SmoothingMethod`。
2. **每个算法的子参数无条件声明**（C++ 端始终持有完整状态，行为不依赖 UI 是否显示）。
3. 每个子属性 `<Hints>` 里挂 `GenericDecorator value="N"`；多算法共享时用 `values="N M"`；全算法通用直接不挂 decorator。
4. 每个算法的子参数集合再包一个 `<PropertyGroup label="算法名 (CGAL fn)">`，面板自动出现可折叠分节。

骨架：
```xml
<IntVectorProperty name="SmoothingMethod" command="SetSmoothingMethod" ... default_values="0">
  <EnumerationDomain name="enum">
    <Entry text="Shape MCF"      value="0"/>
    <Entry text="Angle &amp; Area" value="1"/>
    <Entry text="Fair"           value="2"/>
  </EnumerationDomain>
</IntVectorProperty>

<DoubleVectorProperty name="ShapeTimeStep" ...>
  <Hints>
    <PropertyWidgetDecorator type="GenericDecorator" mode="visibility"
                             property="SmoothingMethod" value="0"/>
  </Hints>
</DoubleVectorProperty>
<!-- 类似地：UseAngleSmoothing 挂 value="1"；FairingContinuity 挂 value="2" -->

<PropertyGroup label="Shape MCF (smooth_shape)">
  <Property name="ShapeTimeStep"/>
  <Property name="ShapeDoScale"/>
</PropertyGroup>
```

#### 8.6.6 `PropertyGroup` 排序与放置

- 把 `<PropertyGroup>` 写在**该组所有属性声明之后**最稳妥：面板呈现顺序大致按 PropertyGroup 在 XML 里出现的顺序，未在任何组里的属性按各自声明顺序穿插。
- 不要把同一个 `Property name="..."` 同时塞进多个 PropertyGroup。
- 标签简洁化：分组标题已暗含上下文，单属性 `label` 可去掉 `"Remesh:" / "Shape Smooth:"` 之类前缀。
- 修改面板布局时**不要**改 `name=""` / `command=""` / `default_values=""`，否则破坏现有 state 文件 / Python 脚本兼容性。

#### 8.6.7 指定某输出端口在视图中的默认 Representation

在对应 `ParaViewPlugin/SHYX*.xml` 的 `<SourceProxy>` 末尾 `<Hints>` 里写 **`RepresentationType`**（见 [ParaView Proxy Hints：RepresentationType](https://www.paraview.org/paraview-docs/latest/cxx/ProxyHints.html)）：控制**首次把该 filter 的输出显示到视图里时**，representation 子代理上 **「Representation」属性的默认值**（通过 `vtkSMRepresentationProxy::SetRepresentationType()`），不是改「创建哪个 representation 代理」。

| 属性 | 说明 |
|------|------|
| `view` | 可选；常见为 `RenderView`；省略则匹配所有视图类型。 |
| `type` | **必须与 ParaView 该视图下表示法列表里的字符串一致**（与 UI 下拉框文案一致）。例如 `Point Gaussian`、`Point Label`、`Surface`。注意 **带空格** 的写法（`Point Gaussian`），不要写成驼峰 `PointGaussian`。若字符串不匹配，Hint 无效，仍会退回视图默认（多为 Surface）。 |
| `port` | 可选；**多输出端口**时用 `port="0"`、`port="1"` 等绑定到具体 `<OutputPort index="..."/>`；省略则对所有输出端口生效。 |

多个 `<RepresentationType>` 可按**从窄到宽**的顺序写（先带 `view`+`port`，再只带 `port`），与上游文档「hints are processed in order」一致。

示例（端口 0 首次显示为点高斯；端口 1 不受影响，除非另写一条）：

```xml
<Hints>
  <RepresentationType view="RenderView" type="Point Gaussian" port="0"/>
</Hints>
```

仓库内参考：`ParaViewPlugin/SHYXVmtkOpeningCenterlines.xml`（Opening seed points → `Point Label`）；`ParaViewPlugin/SHYXVesselEndClipper.xml`（Clip Planes → `Point Label`）；`ParaViewPlugin/SHYXAutoStreamline.xml`（port 1 种子点 → `Point Gaussian`）。

#### 8.6.8 自定义 `panel_widget`：组里必须有一个**可见锚点**属性

`pqProxyWidget` 按**属性**遍历面板：`panel_visibility="never"` 的属性在碰到 `PropertyGroup` **之前就被 skip**。因此：

- **组里全部 backing 属性都写 `never` → 自定义表/widget 根本不会被创建**（看起来像「widget 没显示」）。
- **正确**：至少 **一个** 组成员保持默认可见（不要写 `never`），作为锚点；其余可 `never`。组上的 `panel_widget="..."` 会接管锚点，**不会再画出锚点那一行原始文本框**。
- **错误模式**（反复踩坑）：把 `PatchNames` / `BlockNames` 之类 newline 字符串全标 `never`，以为「只显示自定义表」。

对照：

| XML | 锚点（不能 never） | 其余可 never |
|-----|-------------------|--------------|
| `SHYXDataSetToPartitionedCollection.xml` | `BlockNames` | `BoundaryVariables`、`BoundaryWriteNormals` |
| `SHYXRemeshWithEndpoint.xml` | `UncappedSizeHistPanelAnchor`（注释写明） | — |
| `SHYXSelectionAppendPatches.xml` | `PatchNames` | `PatchMarks`、`PatchCellIds` |

源码：`Qt/Components/pqProxyWidget.cxx` 里 `skip_property` 对 `never` 直接 `continue`，到不了 `createWidgetForPropertyGroup`。

### 8.7 `BoundsDomain` 与「自动刷新」（示例：`MinEdgeLength` / `MaxEdgeLength`）

- XML：`BoundsDomain mode="scaled_extent"` + `scale_factor` + `RequiredProperties` 绑定 **`Input`**。
- **含义**：**不是** VTK 算子在 `RequestData` 里改 Min/Max；而是 **Input 的包围盒变化**时，ParaView **更新该属性的 Domain**（建议尺度、Scale/Reset 等）。`scaled_extent` 下 `scale_factor` 是相对包围盒特征长度的比例（`SHYXAdaptiveIsotropicRemesher.xml`：Min 约 **0.1%** 最长边、Max 约 **10%** 最长边）。`RequestData` 要求 `0 < Min < Max`；XML 默认 0 表示未设，需用户填正值或点 Scale/Reset。
- **不会**在后台静默覆盖用户已填的数值，除非用户在 UI 上点 **Reset/Scale** 显式采用建议值。

### 8.8 其它与面板相关的常见能力

- **`PropertyGroup`**：分节标签、`panel_visibility`、`panel_widget` 自定义整组 UI。
- **`panel_visibility`**：`default` / `advanced` / `never`。**`never` 不能用在自定义 `panel_widget` 组的全部成员上**，见 **§8.6.8**。
- **`Hints`**：`ShowInMenu`、`RepresentationType`（默认表示法，见 **§8.6.7**）、`ArraySelectionWidget`、`SelectionInput`、`panel_widget` 等。
- **`SelectionInput` 不会在 create 时自动填 widget**。`<SelectionInput/>` 只换专用选择面板（含 Copy Active Selection）。要像 **Extract Selection** 那样在创建 filter 时把 Input 上的当前选择拷进 `Selection` 属性/widget（无需点击），在 **SourceProxy** 的 `<Hints>` 里加：
  ```xml
  <InitializationHelper class="vtkSMExtractSelectionProxyInitializationHelper"/>
  ```
  该类已在 ParaView 注册；helper 要求属性名恰好是 `Input` 与 `Selection`。对照：`SHYXDeleteSelectedCells.xml`；上游 `extraction_filters.xml` 的 `ExtractSelection`。
- **输入与数据域**：`InputProperty`、`DataTypeDomain`、`InputArrayDomain`。

### 8.9 本地对照源码

- Decorator 工厂与注册：`Qt/ApplicationComponents/pqStandardPropertyWidgetInterface.cxx`
- `GenericDecorator` 逻辑：`Remoting/ApplicationComponents/vtkGenericPropertyDecorator.cxx`
- 组合逻辑：`vtkCompositePropertyDecorator.cxx`
- 面板组装：`Qt/Components/pqProxyWidget.cxx`（多 decorator 取 AND 的判定在此）

## 9. 加功能时必须接到 AI Assistant（`describe_proxy`）

**漏改助手 = 用户在 UI 里能用，助手却列不出、也查不到参数。** 实现在 `ParaViewPlugin/pqSHYXAIAgentTools.cxx`；系统提示 `pqSHYXAIAssistantPanel.cxx` 的 `kSystemPrompt`。

`describe_proxy` / `list_filters` **不会自动覆盖所有 SHYX 能力**。它们默认擅长 SM `filters`/`sources`。新类型若不补代码，助手只能当 filter 查。

### 9.1 按类型要改什么

| 新功能类型 | SM 里有没有 | 只加 XML/C++ 够不够 | 还必须改 |
|------------|-------------|---------------------|----------|
| Pipeline filter / source | `filters` / `sources` | **参数**：`describe_proxy('XML名')` 一般能扫到 | `INVENTORY.md`；有用法偏好时加 `kShyxExtra`（血管顺序、多端口、与 VESPA 成对选用） |
| Display 表示 | `representations` + GeometryRepresentation `Extension` + `exposed_name` | **不够**。空目录看不到；即使用 XML 名查到，列的是 `Animate` 不是 `PG_Animate` | `isShyxDisplayRepresentation`、`representationDisplayName`（下拉框文案，如 `Pulse Glyphs`）；确认 `exposedNamesForSubproxy` 能从 Extension 读到 `exposed_name`；`kShyxExtra`；`kSystemPrompt` 举例；`get_display` 的 `PG_`/`AS_`/`PL_` 前缀若换了也要加 |
| 标题栏 / 纯 Qt 工具 | **无 proxy** | **完全不够**。`describe_proxy` 会报找不到 | `describeClientTool()`（交互与参数，如 `DihedralThresholdDegrees`）；`kShyxExtra`；空查询 `list_filters`/`lookup_shyx_docs` 的目录句；`kSystemPrompt` |
| 3D widget 表示 | `representations` / `3d_widget_representations` | 精确 XML 名也许能 dump，但不是 Display 下拉 | `kShyxExtra` 标明「支架 widget，不是 Representation 下拉」 |

对照表：`vespa/INVENTORY.md` 的滤镜 / 表示 / 仅客户端 Qt。

### 9.2 助手侧查询入口（补完之后才成立）

- 目录：空查询 `lookup_shyx_docs` + `list_filters`。
- Filter 参数：`describe_proxy('SHYXMeshChecker')` → Python `Name(Input=..., registrationName='...')`。
- Display 参数：`describe_proxy('Pulse Glyphs')` → `GetDisplayProperties().Representation = 'Pulse Glyphs'`，属性用 **exposed 名**（`disp.PG_Animate`）。**不要** `PulseGlyphRepresentation()`。
- 无 proxy 工具：`describe_proxy('sphere')` / `describe_proxy('grow')`；用完 `get_selection_ids`。

### 9.3 检查（加完功能后）

在 `pqSHYXAIAgentTools.cxx` 确认：新 XML 名或 Display 文案能被 `describe_proxy` 命中；若无 SM 属性，`describeClientTool` 有条目。同步工具 schema 描述和 `kSystemPrompt`，否则模型仍只会对 filter 调 `describe_proxy`。

