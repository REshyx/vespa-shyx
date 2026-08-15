# vtkSHYXAIAssistant

ParaView **Filters → SHYX → SHYX AI Assistant**（和其他 SHYX 滤镜一样：先选中一个数据源，否则 Filters 菜单会把该项藏掉）。

输入可选：有输入时 pass-through 该数据集；无输入时输出空 `vtkPolyData`。

面板：

- **Question**：本轮问题。点 **Send to AI** 发给 OpenAI 兼容接口（不跑脚本）。
- **Code**：默认 ParaView Python（`paraview.simple`）。**Apply 只执行这个框**。
- **Dialog**：多轮对话上下文。

可选勾选：当前 RenderView 截图、Output Window 错误/警告、管线摘要。API Key 存在本机 `QSettings`，不进 state 文件。

截图与 Output Window 只能在客户端 Qt 采集，不能在 `RequestData` 里读。

**Apply / Run script** 使用**当前正在运行的 ParaView** 里的 Python（`pqPythonManager::executeCode`），不要求编译插件时的 SDK 打开 `PARAVIEW_USE_PYTHON`。官方安装版 `C:\Program Files\ParaView 6.0.1\bin\paraview.exe` 即可。若进程里没有 Python manager，会提示无法执行。

HTTPS 走 Qt 的 TLS **插件**（Windows 上是 `qschannelbackend.dll` 去调系统 Schannel），不是把 Schannel 静态链进本插件。`PATH` 找不到这类插件；可用 `QT_PLUGIN_PATH` 指向带 `tls/` 子目录的 Qt `plugins` 根，或让本插件加载时去搜编译所用的 Qt `plugins`。构建仍会把 `qschannelbackend.dll` 放到 `VESPAPlugin.dll` 旁的 `tls/`，方便没有完整 Qt 目录的机器。
