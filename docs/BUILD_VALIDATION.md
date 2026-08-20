# 构建与静态验证

## 不依赖 Visual Studio 的检查

在仓库根目录运行：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/ValidateProject.ps1
```

该脚本会解析 `TaskbarMon.vcxproj`、`TaskbarMon.vcxproj.filters` 和解决方案配置映射，检查所有源文件、头文件和资源是否存在且只声明一次，确认两个项目文件的条目完全一致，并验证 12 个配置都定义了 `WITHOUT_TEMPERATURE`。它还会确认解决方案没有引入 `OpenHardwareMonitorApi`，输出不会包含 `LibreHardwareMonitorLib.dll`，没有危险测试入口或后台 UI 更新入口，所有配置使用 `AsInvoker`，全局启用 SDL、栈保护、Conformance、DEP、ASLR、CFG，且 Release/Release (lite) 将警告视为错误；更新辅助类也不得重新引入上游更新地址或清单。

## Visual Studio 构建

需要 Visual Studio 2022 v143、MFC 和 Windows 10 SDK。建议从对应的 **Developer PowerShell for VS 2022** 执行：

```powershell
msbuild TaskbarMon.sln /m /nologo /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=v143 /p:TreatWarningAsError=true /bl:artifacts/build-logs/local-release-x64.binlog
```

发布前还需要分别构建 Release 与 Release (lite) 的 x86、x64 和 ARM64EC 组合。仓库中的 GitHub Actions 工作流会自动执行这六组构建，并上传文本日志和二进制 MSBuild 日志。

## 手工宿主验收

静态检查和编译不能替代 Explorer 集成验收。请按 [任务栏宿主验证清单](TASKBAR_HOST_VALIDATION.md) 在真实 Windows 10/11 环境完成附加、退出、Explorer 重启、辅助显示器、Wine 和注销/关机场景，并保留截图与日志。
