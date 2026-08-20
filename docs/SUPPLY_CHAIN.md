# 供应链与发布策略

本文件记录 TaskbarMon 当前的发布信任边界。它适用于源码构建、CI 产物和
随程序发布的二进制依赖。

## 自动更新

自动更新检查当前处于 fail-closed 状态：`CUpdateHelper` 不请求、不解析也
不打开 TrafficMonitor 或 Gitee 的上游更新清单。上游项目的版本、下载链接和
发布签名不能代表 TaskbarMon。

重新启用此功能前，维护者必须同时完成以下项目：

1. 使用 TaskbarMon 自有的 HTTPS 清单地址，并限制为项目所有的域名；
2. 对清单和每个安装包实施可离线验证的签名或 SHA-256 校验；
3. 明确版本比较、降级保护、密钥轮换和撤销流程；
4. 在 CI 中测试拒绝未签名、哈希不匹配和非预期域名的清单。

在此之前，版本只能通过审核过的源码标签或 CI 上传的产物分发。

## 运行时 DLL 边界

`TaskbarMon/DllFunctions.*` 只动态解析已知 Windows 系统 DLL 中的 API。它
先使用 `LOAD_LIBRARY_SEARCH_SYSTEM32`，在旧系统上只回退到由
`GetSystemDirectoryW` 得到的绝对 System32 路径；不得将当前目录、PATH 或
用户可写目录加入该加载路径。

## 温度硬件监控

温度硬件监控当前处于 fail-closed 状态。仓库中保留
`OpenHardwareMonitorApi/` 及其历史二进制，仅用于迁移和审查；
`TaskbarMon.sln` 不再构建该 C++/CLI 项目，所有 TaskbarMon 配置均定义
`WITHOUT_TEMPERATURE`，CI 也不会验证、打包或发布其 DLL。生产主进程不会
加载 LibreHardwareMonitor 或用户提供的硬件 DLL。

重新启用温度功能前，维护者必须完成并审查以下设计：

1. 使用低权限、可独立退出的 helper process，而非把第三方硬件库加载进 UI
   主进程；
2. 定义经版本化、严格输入验证的 IPC 协议，并处理认证、权限边界、超时、重启
   和崩溃隔离；
3. 对 helper 的来源、签名或 SHA-256、许可证及更新/撤销流程实施 CI 验证。

## CI 完整性与构建证据

`.github/workflows/main.yml` 具有以下约束：

- 仅授予 `contents: read`，且固定到 Windows 2022；
- checkout 后先运行 `scripts/ValidateProject.ps1`，解析项目 XML、核对筛选映射、磁盘文件、配置矩阵和温度监控禁用策略；
- `actions/checkout` 固定为 v4.2.2 提交
  `11bd71901bbe5b1630ceea73d27597364c9af683`；
- `actions/upload-artifact` 固定为 v4.6.2 提交
  `ea165f8d65b6e75b540449e92b4886f43607fa02`；
- 不使用浮动版本的 MSBuild setup action；工作流通过 runner 已安装的
  `vswhere` 定位 Visual Studio，并在每次构建前调用其 `VsDevCmd.bat`，从而
  使用受支持的 Windows SDK 与编译器环境；
- 所有 Release 与 Release (lite) 的 x86、x64、ARM64EC 组合均构建，且在
  上传前仅检查 `TaskbarMon.exe` 不为空并记录 SHA-256；不验证或打包硬件 DLL；
- 每次构建均产生日志和 `.binlog`，即使构建失败也上传构建证据。

文本构建日志不再被 `.gitignore` 的通配规则隐藏，以便需要时可以随问题报告
审查；`.binlog` 和应用运行时诊断仍视为生成文件。CI 产物本身是短期证据，
不是可替代经审核发布物的长期存储。

## 变更审查清单

涉及更新、DLL、Action 版本或二进制依赖的变更必须至少审查：来源、固定版本
或哈希、许可证、最小权限、失败时是否 fail-closed，以及 Release 和
Release (lite) 构建产物是否仍可启动。任何温度监控重新启用都必须先通过
helper-process + IPC 边界的专项安全审查。
