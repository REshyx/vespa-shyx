# SHYX AI Assistant

ParaView **View → SHYX AI Assistant**：可勾选的停靠窗口（默认停在右侧），不是 pipeline filter。

面板：

- **Question**：本轮问题。点 **Send** 发给 OpenAI 兼容接口（不跑脚本）。**Model** 在 Send 同一行；刷新从 `{Base URL}/models` 拉列表，**+** 可手写添加。
- **Code**：默认 ParaView Python（`paraview.simple`）。**Run script** 在 Code 标题右侧，只执行这个框。
- **Dialog**：多轮对话上下文。**History** 滑条只控制本次进程里带几条气泡；默认 0，不写入 `QSettings`。**Reset** 停止进行中的请求，并清空问题、截图、对话、Agent 轮次和代码框（恢复默认模板）。API Key / Base URL / Model 保留。
- **API (URL / Key)**：默认折叠。

可选勾选：当前 RenderView 截图。Output Window 由 Agent 按需调用 `get_output_window` 读取，不自动附带。API Key / Base URL / Model 存在本机 `QSettings`，不进 state 文件。代码框、对话、History 滑条、Agent 勾选只在当前会话有效。

截图与 Output Window 只能在客户端 Qt 采集。

**Run script** 使用**当前正在运行的 ParaView** 里的 Python（`pqPythonManager::executeCode`），不要求编译插件时的 SDK 打开 `PARAVIEW_USE_PYTHON`。官方安装版 `C:\Program Files\ParaView 6.0.1\bin\paraview.exe` 即可。若进程里没有 Python manager，会提示无法执行。

HTTPS 走静态链进 `VESPAPlugin.dll` 的 **libcurl**（Windows 上 TLS 为系统 Schannel）。不依赖 Qt Network，也不需要旁边的 `qschannelbackend.dll`。

实现都在 `ParaViewPlugin/pqSHYXAI*` 与 `pqSHYXCurlRequest`（dock + agent tools + HTTP）。`vespa/shyx/AIAssistant/` 仍有遗留 VTK 模块 `vtkSHYXAIAssistant`；`SHYXAIAssistant.xml` **未**注册，不要创建该 pipeline 节点。

## Agent 工具（`pqSHYXAIAgentTools.cxx`）

目录：空查询 `lookup_shyx_docs`、`list_filters`。参数：`describe_proxy('XML名')`。Display：`describe_proxy('Pulse Glyphs'|'Animated Streamline'|'Point Label')`，再用 `GetDisplayProperties().Representation = '…'` 与 `disp.PG_*` / `AS_*` / `PL_*`（不要 representation 构造函数）。标题栏：`describe_proxy('sphere'|'grow')`；grow 的 `DihedralThresholdDegrees` 默认 15；选完后 `get_selection_ids`。

其它：`get_pipeline_tree`、`get_active_data`、`get_source_data`、`get_selection`、`get_display`（会列出 `PG_`/`AS_`/`PL_`/`PulseGlyph_*`）、`get_color_map`、`get_camera`、`get_time`、`get_output_window`、`capture_screenshot`、`pick_world_point`、`get_code_script`、`set_code_script`、`run_code_script`、`get_source_properties`、`get_blocks`。

**Run script** 需要**正在运行的 ParaView 进程**里有 Python manager；编译插件时不必打开 `PARAVIEW_USE_PYTHON`。
