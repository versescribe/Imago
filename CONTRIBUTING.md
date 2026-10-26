# 参与 Imago

Imago 的方向是 Windows 原生、轻量、少依赖。先修复真实使用中的问题，再考虑增加功能。

## 本地开发

安装 Visual Studio 2022 的 C++ 桌面开发、CMake 组件，以及 Windows SDK 10.0.22621 或更高版本。
在 PowerShell 中执行 `./tools/build.ps1`；默认只构建和打包，不启动应用、录音、录屏或访问剪贴板。
需要运行无界面的核心测试时，显式执行 `./tools/build.ps1 -RunTests`。

阅读 [架构说明](docs/ARCHITECTURE.md) 和 [Agent 规范](AGENTS.md)。现有紧凑代码逐步改善，不为格式化而修改无关文件。

## Git 约定

- `main` 保持可构建；一般修改从 `main` 创建短期分支：`feat/name`、`fix/name`、`docs/name`、`chore/name`。
- 一次提交围绕一个完整变化。提交标题采用 `type(scope): summary`，如 `fix(recording): preserve clips after export failure`；说明可以使用中文。
- 类型使用 `feat`、`fix`、`refactor`、`docs`、`test`、`build`、`ci`、`chore`。发布提交使用 `chore(release): vX.Y.Z`。
- 发 PR 描述问题、最终行为、验证方式及尚未验证的限制。优先 squash merge，PR 标题保持上述格式。
- 不提交 `build*`、`dist`、录屏、截图样本中的个人信息、密钥或本地设置。不重写共享分支与已发布标签，不 force push。
- 保留维护者已有的提交签名配置，不为自动化方便关闭签名。使用 1Password SSH 的机器在 SSH 远端操作或签名前先确认代理可用。

## 代码约定

- C++20，优先系统 API 和现有模块。引入第三方运行时依赖前先说明必要性、许可证和分发成本。
- 用 RAII 管理 HWND、GDI、COM、文件和线程；明确所有权。异常不得跨越 Win32 回调边界。
- UI 只在 UI 线程操作，网络、编码和大文件 I/O 放到后台。任务需有结束路径、超时或取消机制。
- 区分屏幕物理像素、窗口客户区坐标和 DPI 缩放单位；错误路径保留用户内容。
- 不把令牌打包进客户端；服务器响应、剪贴板和文件均视为不可信输入。更新下载页限定官方仓库。
- 不引入静默录音、遥测或自动安装。修改网络行为必须同步 [隐私说明](docs/PRIVACY.md)。

## 验证与发布

对解析、坐标、生命周期等真实风险补充有意义的测试，不写仅复述实现的测试。
CI 在 GitHub 的 Windows runner 上构建 Release 并运行不访问桌面／麦克风的测试；交互验证记录见 [检查清单](docs/TESTING.md)。

发布只修改根目录 `VERSION` 和 `CHANGELOG.md`；资源版本和包名由构建生成。
具体标签、工作流与故障处理见 [发布流程](docs/RELEASING.md)。
