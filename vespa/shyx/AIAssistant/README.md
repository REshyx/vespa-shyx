# SHYX AI Assistant

ParaView **View → SHYX AI Assistant**：可勾选的停靠窗口（默认停在右侧），不是 pipeline filter。

面板：

- **Question**：本轮问题。点 **Send** 发给 OpenAI 兼容接口（不跑脚本）。**Model** 在 Send 同一行；刷新从 `{Base URL}/models` 拉列表，**+** 可手写添加。**Effort** 只调推理强度：Low / Medium / High / Max，默认 Medium。思考始终打开，每次请求都带 `enable_thinking: true` 和对应的 `reasoning_effort`。不是每个模型都接受每一档，接口报不支持时改相邻一档。
- **Code**：默认 ParaView Python（`paraview.simple`）。**Run script** 在 Code 标题右侧，只执行这个框。
- **Dialog**：当前会话里已完成的回合会发给模型。请求大约超过 12.8 万 token 的七成时，先丢掉较早的工具结果和截图，仍放不下就把保护尾巴之前的对话折成一份滚动摘要，状态栏显示已压缩。最近两轮和当前这轮工具调用保持原文。接口若返回上下文超限，会再压一次并重发。**Memory** 打开本机技能列表。**Reset** 停止进行中的请求，并清空问题、截图、对话、Agent 轮次、压缩摘要和代码框（恢复默认模板）。API Key / Base URL / Model 和 Memory 文件保留。
- **API (URL / Key)**：默认折叠。

可选勾选：当前 RenderView 截图。Output Window 由 Agent 按需调用 `get_output_window` 读取，不自动附带。API Key / Base URL / Model / Effort 存在本机 `QSettings`，不进 state 文件。以前存过 Off，或还没有 Effort 且没勾过 Thinking 的，会落到 Medium；以前只勾过 Thinking、还没有 Effort 的，仍是 High。代码框、对话、Agent 勾选只在当前会话有效。

**Memory** 不走 `QSettings`，也不进注册表或 `.pvsm`。目录是 `%APPDATA%\SHYX\AIAssistant\memory\`（Windows 上即 `C:\Users\<用户>\AppData\Roaming\SHYX\AIAssistant\memory`）：

- `Agent.md`：每次请求都附在系统提示末尾，只放跨任务偏好。送出时最多约 4000 字。
- `skills/<id>/SKILL.md`：一条流程一份。请求里只带 id 和 description；正文由 `read_skill` 再读。

面板里可以新建、编辑、删除，也可以打开文件夹。模型只有在用户要求记住或修改时才写：`save_skill` 遇到同名会另存，不覆盖；`update_skill` 覆盖已有 skill（先 `read_skill`，再传完整正文；不新建、不改名；省略 description 保留目录那一行，省略 body 只改描述）；`write_agent_memory` 整篇替换 `Agent.md`。

截图与 Output Window 只能在客户端 Qt 采集。

**Run script** 使用**当前正在运行的 ParaView** 里的 Python（`pqPythonManager::executeCode`），不要求编译插件时的 SDK 打开 `PARAVIEW_USE_PYTHON`。官方安装版 `C:\Program Files\ParaView 6.0.1\bin\paraview.exe` 即可。若进程里没有 Python manager，会提示无法执行。

HTTPS 走静态链进 `VESPAPlugin.dll` 的 **libcurl**（Windows 上 TLS 为系统 Schannel）。不依赖 Qt Network，也不需要旁边的 `qschannelbackend.dll`。

实现都在 `ParaViewPlugin/AIAssistant/`（`pqSHYXAIAssistantPanel`、`pqSHYXAIAgentTools`、`pqSHYXCurlRequest` 等）。本目录只有说明文档，没有 VTK 模块、也没有 SM XML。不要创建 `SHYXAIAssistant()` pipeline 节点。

## Agent 工具（`pqSHYXAIAgentTools.cxx`）

目录：空查询 `lookup_shyx_docs`、`list_filters`。参数：`describe_proxy('XML名')`。Display：`describe_proxy('Pulse Glyphs'|'Animated Streamline'|'Point Label')`，再用 `GetDisplayProperties().Representation = '…'` 与 `disp.PG_*` / `AS_*` / `PL_*`（不要 representation 构造函数）。标题栏：`describe_proxy('sphere'|'grow')`；grow 的 `DihedralThresholdDegrees` 默认 15；选完后 `inspect_selection`。

现场状态：`inspect_pipeline`（无 name 看树，有 name 看该节点数据/属性/blocks）、`inspect_selection`、`inspect_view`（Display + LUT / 相机 / 时间）、`get_output_window`。其它：`capture_screenshot(camera={focal_point, position, view_angle}, targets=[源名或 {name, position}], label=true)`（相机写在这次调用里；省略 camera 保持当前视角；label 默认把目标名字画到 JPEG 上）、`pick_world_point`、`get_code_script`、`set_code_script`、`run_code_script`。长期记忆：`read_skill`、`save_skill`、`update_skill`、`write_agent_memory`；`describe_proxy('memory')` 说明文件位置。

**Run script** 需要**正在运行的 ParaView 进程**里有 Python manager；编译插件时不必打开 `PARAVIEW_USE_PYTHON`。
