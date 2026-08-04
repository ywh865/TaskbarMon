# TaskbarMon

基于 [TrafficMonitor](https://github.com/zhongyang219/TrafficMonitor) 重构的 **纯任务栏监控软件**。

显示当前网速、CPU、内存利用率于 Windows 任务栏窗口，无主悬浮窗。全部功能内嵌，无需插件。

## 特性

- 任务栏窗口显示网速 / CPU / 内存 / 显卡 / 硬盘利用率、温度、CPU 频率
- 支持 Win10 / Win11 / Wine 任务栏（D2D 渲染，透明色键控）
- 历史流量统计（日 / 周 / 月 / 年视图）
- 流量 / 内存 / 温度超出提醒（托盘气泡）
- 网络连接自动选择与手动选择、连接详情
- 通知区图标、深色 / 浅色主题自适应

## 与上游的差异

| 项目 | TrafficMonitor | TaskbarMon |
|---|---|---|
| 主悬浮窗 | 有 | **移除**（仅任务栏窗口） |
| 插件系统（dll） | 有 | **移除**，硬件监控内嵌（设置中开关） |
| 主窗口皮肤系统 | 有 | **移除**，保留任务栏样式预设 |
| 架构 | 单体（App 全局状态 + 悬浮窗上帝类） | **分层**：core（采样引擎 MonitorService / 配置 ConfigStore）+ UI |
| 渲染 | 100ms 无条件重绘 | **脏标记 + 节流 + 文本缓存**（空闲时零重绘） |

## 构建

需要 Visual Studio 2022（v143 工具集）+ MFC + Windows 10 SDK。

```bat
msbuild TaskbarMon.sln -p:configuration=Release -p:platform=x64 -m
```

- `Release`：完整版（含硬件监控，默认关闭）
- `Release (lite)`：不含温度监控的精简版

CI：`.github/workflows/main.yml` 自动构建 x64 / x86 / Lite。

## 目录结构

```
TaskbarMon/
├── TaskbarMon/            # 主工程
│   ├── core/              # 核心层（无 UI 依赖）
│   │   ├── MonitorService.h/cpp   # 统一采样引擎（连接/网速/CPU/内存/硬件/历史流量）
│   │   ├── MonitorTypes.h         # 数据快照与硬件数据提供者接口
│   │   └── ConfigStore.h/cpp      # 配置类型化读写与校验
│   ├── TaskBarDlg.*       # 任务栏窗口（Win10 经典 / Win11 / Wine）
│   ├── TrafficMonitorDlg.*        # 隐藏宿主窗口（托盘 + 任务栏管理 + 调度）
│   └── dialogs/           # 设置 / 历史流量 / 网络详情等对话框
├── OpenHardwareMonitorApi # 硬件监控 API（LibreHardwareMonitor 封装）
└── include/OpenHardwareMonitor
```

## 配置

配置文件位于 `%APPDATA%\TaskbarMon\config.ini`（或便携模式下的程序目录），键名与上游兼容，旧配置可直接沿用。

## 许可证

保持上游协议（[Anti 996](LICENSE)）。
