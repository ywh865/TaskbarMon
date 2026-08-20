# TaskbarMon

基于 [TrafficMonitor](https://github.com/zhongyang219/TrafficMonitor) 重构的 **纯任务栏监控软件**。

显示当前网速、CPU、内存利用率于 Windows 任务栏窗口。对于未经验证的 Explorer
层级、Windows 版本或 Wine，程序会保守地回退为浮动条，而不会猜测窗口句柄或修改
未知的任务栏区域。全部功能内嵌，无需插件。

## 特性

- 任务栏窗口显示网速 / CPU / 内存 / 显卡 / 硬盘利用率、CPU 频率
- 已知任务栏布局可尝试 Win10 经典嵌入或 Win11 覆盖；未知布局与 Wine 安全回退为浮动条
- 历史流量统计（日 / 周 / 月 / 年视图）
- 流量 / 内存超出提醒（托盘气泡）
- 网络连接自动选择与手动选择、连接详情
- 通知区图标、深色 / 浅色主题自适应

## 与上游的差异

| 项目 | TrafficMonitor | TaskbarMon |
|---|---|---|
| 主悬浮窗 | 有 | **移除**（仅任务栏窗口） |
| 插件系统（dll） | 有 | **移除**；温度监控当前禁用，待重新设计为 helper process + IPC |
| 主窗口皮肤系统 | 有 | **移除**，保留任务栏样式预设 |
| 架构 | 单体（App 全局状态 + 悬浮窗上帝类） | **分层**：core（采样引擎 MonitorService / 配置 ConfigStore）+ UI；服务层当前仍依赖 Windows/MFC 适配器 |
| 渲染 | 100ms 无条件重绘 | **脏标记 + 节流 + 文本缓存**（空闲时零重绘） |

## 构建

需要 Visual Studio 2022（v143 工具集）+ MFC + Windows 10 SDK。

```bat
msbuild TaskbarMon.sln -p:configuration=Release -p:platform=x64 -m
```

- `Release`：兼容配置名；当前同样不编译温度监控
- `Release (lite)`：兼容配置名；不编译温度监控

CI：`.github/workflows/main.yml` 构建 Release 与 Release (lite) 的 x86、x64 和 ARM64EC 组合；每个任务只校验并打包 `TaskbarMon.exe`，同时上传文本和 MSBuild 二进制日志。

提交前可在 PowerShell 中运行工程结构校验：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/ValidateProject.ps1
```

该校验不需要 Visual Studio，会解析项目 XML、核对项目文件与磁盘文件、检查配置矩阵和温度监控禁用策略。完整编译仍需在 Visual Studio Developer Prompt 或 CI 的 Windows runner 中执行。

## 安全与发布

- 自动更新检查目前**有意禁用**。TaskbarMon 不再读取或打开上游 TrafficMonitor 的更新清单或下载链接，避免把其他项目的发布信任边界带入本项目。
- 在项目拥有自己的发布渠道之前，请从受信任的项目发布页或经过审核的 CI 产物获取版本。重新启用自动更新前，必须引入项目自有的 HTTPS 清单、签名和每个二进制文件的 SHA-256 校验。
- 硬件温度功能目前**有意禁用**。在完成独立 helper process、受限 IPC 协议、超时与崩溃隔离设计前，主进程不会加载或发布 LibreHardwareMonitor 的 C++/CLI DLL。
- 任务栏宿主会在修改 Explorer 前保存完整状态；只有父窗口、样式、扩展样式、位置与尺寸均恢复并验证后，程序才允许正常退出。真实 Win10/Win11 Explorer 集成仍需在目标系统上验收。
- CI 使用固定提交 SHA 的 GitHub Actions、最小化 token 权限和 Windows 2022 构建映像；Release 构建将警告视为错误并保留构建日志作为审查证据。
- 依赖清单、DLL 加载边界和更新流程见 [供应链与发布策略](docs/SUPPLY_CHAIN.md)。

## 目录结构

```
TaskbarMon/
├── TaskbarMon/            # 主工程
│   ├── core/              # 采样/配置服务层（仍依赖 Windows/MFC 适配器）
│   │   ├── MonitorService.h/cpp   # 统一采样引擎（连接/网速/CPU/内存/历史流量）
│   │   ├── MonitorTypes.h         # 数据快照与预留的硬件数据提供者接口
│   │   └── ConfigStore.h/cpp      # 配置类型化读写与校验
│   ├── TaskBarDlg.*       # 任务栏窗口（Win10 经典 / Win11 / Wine）
│   ├── TrafficMonitorDlg.* # 隐藏宿主窗口（托盘 + 任务栏管理 + 调度）
│   └── *Dlg.cpp / *Dlg.h   # 设置 / 历史流量 / 网络详情等对话框，平铺在 TaskbarMon/ 下
├── OpenHardwareMonitorApi # 保留的旧封装源码/二进制，不参与构建或发布
└── include/OpenHardwareMonitor
```

## 配置

配置文件位于 `%APPDATA%\TaskbarMon\config.ini`（或便携模式下的程序目录）。首次运行会只读复制旧 `TrafficMonitor` 目录中已知的配置、流量历史和日志文件，绝不覆盖新目录或删除旧文件。

## 许可证

保持上游协议（[Anti 996](LICENSE)）。
