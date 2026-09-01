<div align="center">

# Windows AppData Manager

看清 Windows 应用数据的来源、占用与风险，并以可恢复的方式清理经过验证的低风险内容。

[![Windows CI](https://img.shields.io/github/actions/workflow/status/Cloudwhile/WindowsAppDataManager/ci.yml?branch=master&style=for-the-badge&logo=githubactions&logoColor=white&logoSize=auto&label=Windows%20CI)](https://github.com/Cloudwhile/WindowsAppDataManager/actions/workflows/ci.yml)
[![Windows](https://img.shields.io/badge/Windows-10%201809%2B%20%7C%2011-0078D4?style=for-the-badge&logo=windows11&logoColor=white&logoSize=auto)](https://www.microsoft.com/windows)
[![Qt](https://img.shields.io/badge/Qt-6.9.2-41CD52?style=for-the-badge&logo=qt&logoColor=white&logoSize=auto)](https://www.qt.io/)
[![C++](https://img.shields.io/badge/C%2B%2B-20-00599C?style=for-the-badge&logo=cplusplus&logoColor=white&logoSize=auto)](https://isocpp.org/)
[![License](https://img.shields.io/github/license/Cloudwhile/WindowsAppDataManager?style=for-the-badge&logo=opensourceinitiative&logoColor=white&logoSize=auto)](LICENSE)

[下载](#下载) · [功能](#主要功能) · [构建](#从源码构建) · [测试](#运行检查) · [项目结构](#项目结构) · [参与贡献](#参与贡献)

</div>

Windows AppData Manager 是一款本地运行的 Windows 桌面工具。它扫描当前用户的 `Local`、`LocalLow` 与 `Roaming` 数据目录，将文件归属到应用，区分缓存、日志、配置、凭据等数据类型，并为符合保守条件的项目生成清理计划。

> [!IMPORTANT]
> 项目仍在积极开发中。清理流程已经包含二次验证、运行中进程检查、回收站操作和审计记录，但重要数据仍建议保留独立备份。

## 下载

前往 [GitHub Releases](https://github.com/Cloudwhile/WindowsAppDataManager/releases) 下载最新的 `WindowsAppDataManager-版本-windows-x64-setup.exe`，运行后按向导完成安装。安装器会请求管理员权限，默认安装到 `Program Files\Windows AppData Manager`，并创建开始菜单入口；桌面快捷方式可在安装时选择。

每个发布包同时提供 `.sha256` 校验文件和 GitHub 构建来源证明。可以使用 PowerShell 核对文件哈希：

```powershell
Get-FileHash .\WindowsAppDataManager-版本-windows-x64-setup.exe -Algorithm SHA256
```

卸载时可打开 Windows“设置 → 应用 → 已安装的应用”，找到 Windows AppData Manager 后选择卸载。卸载程序不会主动删除用户的设置、审计记录或其他应用数据。

当前安装包尚未进行商业代码签名，Windows SmartScreen 可能显示“未知发布者”。请只从本仓库的 Releases 页面下载，并核对校验值。

## 主要功能

| 能力 | 说明 |
| --- | --- |
| 应用归属分析 | 结合内置规则、安装注册表、Appx 包、可执行文件元数据与签名证据识别数据来源 |
| 并行渐进扫描 | 按扫描单位并行分析，完成一个单位就更新结果，并提供单调递增的进度与当前路径 |
| 数据风险分类 | 区分缓存、日志、临时文件、配置、数据库、会话、Cookie、凭据、存档等类别 |
| 残留判断 | 综合安装证据与阻断原因标记潜在卸载残留，避免只按目录名称下结论 |
| 安全清理计划 | 仅纳入通过保守规则的候选项，允许逐项确认并显示预计释放空间 |
| 可恢复执行 | 执行前重新验证路径身份与进程状态，使用 Windows 回收站而非直接永久删除 |
| 本地审计 | 使用 SQLite 保存清理结果与失败信息，完成后自动重新扫描 |
| 桌面级界面 | 支持浅色、深色、跟随系统，以及标准、减少、关闭三档动效偏好 |

## 工作流程

```mermaid
flowchart LR
    A[扫描 AppData] --> B[解析应用归属]
    B --> C[分类与风险评估]
    C --> D[渐进显示结果]
    D --> E[生成保守清理计划]
    E --> F[执行前二次验证]
    F --> G[移动到回收站]
    G --> H[记录审计并重新扫描]
```

扫描本身只读取文件系统和本机安装证据。只有用户在“清理建议”中确认所选项目后，程序才会尝试移动文件；身份变化、重解析点、运行中进程或权限异常都会阻止对应项目被处理。

## 环境要求

- Windows 10 1809 或更高版本，或 Windows 11，64 位
- Visual Studio 2022 或 Build Tools 2022，安装“使用 C++ 的桌面开发”
- CMake 3.16 或更高版本
- Qt 6.9.x MSVC 2022 64-bit，包含 `Qt Quick`、`Qt Quick Controls 2` 与 `Qt Test`
- Ninja（推荐，与项目 CI 配置一致）

当前持续集成使用 Qt 6.9.2、MSVC 2022 和 Ninja Multi-Config。

## 从源码构建

在已加载 MSVC x64 开发环境的终端中执行：

```powershell
git clone https://github.com/Cloudwhile/WindowsAppDataManager.git
cd WindowsAppDataManager

cmake -S . -B out/build/local `
  -G "Ninja Multi-Config" `
  -DBUILD_TESTING=ON `
  -DCMAKE_PREFIX_PATH="C:/Qt/6.9.2/msvc2022_64"

cmake --build out/build/local --config Release --parallel 2
```

Qt 6 的 Windows 构建会在链接后自动运行 `windeployqt`。使用上述生成器时，程序通常位于：

```text
out/build/local/WindowsAppDataManager/Release/WindowsAppDataManager.exe
```

不同生成器的配置子目录可能不同；也可以通过 `cmake --build` 的输出确认最终位置。

## 运行检查

```powershell
cmake --build out/build/local `
  --config Debug `
  --target WindowsAppDataManager_qmllint `
  --parallel 2

ctest --test-dir out/build/local `
  -C Debug `
  --output-on-failure `
  --no-tests=error
```

测试覆盖后端分类、安装证据、残留判断、Windows 文件身份、安全清理、QML 模型，以及完整根界面的启动装载。启动冒烟测试可拦截“编译成功但 QML 类型链无法加载”的回归。

## 使用方式

1. 在“扫描概览”开始扫描。扫描期间可以观察数字进度、当前路径和逐步出现的应用结果。
2. 在“应用管理”中按名称、发布者、分类、安装状态或风险筛选，并查看应用的数据构成和归属证据。
3. 在“清理建议”中检查候选项、影响和预计释放空间，只选择确认可以重新生成的内容。
4. 确认后，程序会再次验证每个项目并移动到 Windows 回收站。需要时可从回收站恢复。

程序不会把扫描结果上传到网络。主题和动效偏好保存在当前用户的应用设置中，清理历史保存在本机应用数据目录的 SQLite 数据库中。

## 项目结构

```text
WindowsAppDataManager/
├─ components/              可复用 QML 组件
├─ pages/                   概览、应用、详情、清理与设置页面
├─ theme/                   色彩与动效令牌
├─ rules/                   规则架构与内置应用规则
├─ src/
│  ├─ core/                 扫描、解析、分类、风险和清理计划
│  ├─ platform/windows/     Windows 注册表、Appx、签名、路径与回收站适配
│  ├─ qmlmodels/            QML 可观察模型与页面状态
│  ├─ repositories/         本地清理历史
│  └─ services/             异步扫描与清理编排
└─ tests/                   Qt Test 回归测试
```

## 内置规则

当前仓库包含 Chrome、Chromium、Discord、Visual Studio Code、JetBrains、Windows 临时目录和 npm 缓存等规则。规则文件位于 [`WindowsAppDataManager/rules/builtin`](WindowsAppDataManager/rules/builtin)，格式约束见 [`schema.json`](WindowsAppDataManager/rules/schema.json)。

新增或修改规则时，请同时补充对应测试，并确保未知或证据不足的数据保持保守风险级别。

## 参与贡献

欢迎通过 Issue 描述可复现的问题，或提交范围清晰的 Pull Request。提交前请至少完成：

```powershell
cmake --build out/build/local --config Debug --target WindowsAppDataManager_qmllint
cmake --build out/build/local --config Debug --parallel 2
ctest --test-dir out/build/local -C Debug --output-on-failure --no-tests=error
```

涉及清理边界、路径验证或权限处理的改动，应附带失败路径和竞态场景测试。

## 发布维护

[Windows Release 工作流](.github/workflows/release.yml)只从已经存在的语义化版本标签发布，例如 `v0.1.0` 或 `v0.2.0-beta.1`。推送标签后，GitHub Actions 会重新执行 Release 构建、QML 检查和完整测试，部署所需的 Qt 与 MSVC 运行库，再生成 Inno Setup 安装包。工作流会实际完成一次静默安装、隔离启动和静默卸载，最后发布安装程序、SHA-256 校验文件与 GitHub 构建来源证明。

```powershell
git tag -a v0.1.0 -m "Windows AppData Manager v0.1.0"
git push origin v0.1.0
```

如需重试尚未创建 Release 的失败发布，可以让工作流本身在同一个标签上手动运行；工作流不会从分支名隐式创建标签，也不会接受与运行来源不一致的标签。

```powershell
gh workflow run release.yml --ref v0.1.0 -f tag=v0.1.0
```

## 许可证

本项目基于 [MIT License](LICENSE) 开源。

Windows 安装包使用的 Qt、MSVC 运行库及对应源码提供方式见[第三方软件声明](THIRD_PARTY_NOTICES.txt)和 [Qt 源码提供说明](QT_SOURCE_OFFER.txt)。
