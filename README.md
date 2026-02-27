<div align="center">

# D3D12 Game Hook Framework

**通用 D3D12 Hook + ImGui Overlay 框架**

*跨游戏兼容 | 模块化设计 | 生产级健壮性*

![C++](https://img.shields.io/badge/C%2B%2B-20-blue?style=flat-square)
![Platform](https://img.shields.io/badge/Platform-Windows%20x64-lightgrey?style=flat-square)
![License](https://img.shields.io/badge/License-MIT-yellow?style=flat-square)

</div>

---

> [!CAUTION]
> **免责声明**  
> 本项目仅供安全研究与学习使用。请勿将本项目用于在线或多人游戏的作弊、入侵或其他违法活动。作者对任何滥用行为导致的后果不承担责任。
> 
> **Hook 技术兼容性说明**  
> 本框架采用 **VMT Hook** 而非 Inline Hook。对于大型网络游戏的兼容性可能有限。如果注入后出现崩溃，需要自行进行调整。建议在隔离环境中先行测试。
> 
> **反作弊兼容性说明**  
> 本框架不保证在带有反作弊保护的游戏中的兼容性。对于受 EAC、BattlEye、VAC 等反作弊系统保护的游戏，可能需要额外的分析和优化才能使用。使用者需自行承担因使用本框架导致的所有风险，包括但不限于账号封禁等后果。

> [!NOTE]
> **版本兼容性说明**  
> - 支持 Windows 10/11 x64 系统
> - 需要 DirectX 12 兼容的 GPU
> - 附带 UE4/UE5 游戏示例（基于 DUMPER-7 生成的 CppSDK）

---

## 特性

| 功能 | 说明 |
|:-----|:-----|
| **通用 D3D12 Hook** | 适用于任何 D3D12 游戏，不绑定特定引擎 |
| **模块化 DX12Renderer** | Hook 回调、资源管理、ImGui 渲染完全封装，外部只需注册 3 个回调 |
| **Fence GPU 同步** | 每帧等待 GPU 完成再 Reset CommandAllocator，避免资源冲突 |
| **线程安全** | CRITICAL_SECTION 序列化渲染、SRWLOCK 非阻塞保护 ImGui 输入 |
| **SEH 异常保护** | 所有 hook 入口和 GPU 操作均有 `__try/__except` 包裹，崩溃不扩散 |
| **Device Lost 检测与恢复** | 检测 `DXGI_ERROR_DEVICE_REMOVED`，自动标记并允许重新初始化 |
| **两阶段 CommandQueue 捕获** | Execute 写 pendingQueue，Present 原子消费，渲染期间指针稳定 |
| **SwapChain 热切换** | 全屏/窗口切换时自动重建资源，无需重新注入 |
| **ResizeBuffers / ResizeBuffers1** | 同时 hook 两者，覆盖 HDR/格式切换场景 |
| **安全卸载** | 等待 in-flight Present → unhook → 恢复 WndProc → 销毁 ImGui → 释放资源 |

---

## 快速开始

### 先决条件

- Windows 10 / Windows 11 x64
- Visual Studio 2022（v143 工具集）
- Windows SDK（最新版）
- 支持 DirectX 12 的 GPU

### 编译

#### 完整版（含 UE 游戏功能）

```powershell
.\build_modular.bat
# 输出: x64\Release\Etb_Esp.dll
```

#### 纯测试版（无功能通用空白 ImGui 窗口，验证 Hook 是否工作）

```powershell
.\build_test.bat
# 输出: x64\Release\DX12HookTest.dll
```

#### 使用 Visual Studio

1. 打开 `d3d12imgui.sln`
2. 选择 `Release | x64` 配置
3. 生成解决方案

> **注意**：若在 VS 中手动编译，需手动将 `CppSDK/SDK/*.cpp` 添加到项目中。推荐使用 bat 脚本。

### 使用说明

**测试版 (`DX12HookTest.dll`)**：注入任意 DX12 游戏
- `INSERT` — 显示/隐藏 ImGui 窗口
- `END` — 卸载 DLL

**完整版 (`Etb_Esp.dll`)**：注入目标 UE 游戏
- `INSERT` — 显示/隐藏菜单
- `F1` — 物品透视
- `F2` — 加速功能

---

## 架构

```
项目根目录/
├── Core/
│   ├── DX12/                      # DX12 渲染管理（核心层）
│   │   ├── DX12Renderer.h         #   公开接口：回调注册、InstallHooks、Shutdown
│   │   ├── DX12Renderer.cpp       #   资源生命周期、ImGui 初始化、VMT Patch、安全调用
│   │   ├── DX12Internal.h         #   内部共享状态（两个 cpp 共用）
│   │   └── DX12HookCallbacks.cpp  #   Hook 回调：OnPresent、OnResize、OnExecute、WndProc
│   ├── Config.h                   # 功能开关配置
│   ├── GameData.cpp/h             # UE 游戏数据收集
│   └── GameMemory.h               # 内存读写工具
├── Hook/                          # 通用 Hook 基础设施
│   ├── D3D12Hook.cpp/h            #   创建临时设备提取 vtable 地址
│   ├── VmtHook.cpp/h              #   VMT 修补实现
│   └── HookManager.cpp/h          #   统一 Hook 管理（安装/卸载）
├── UI/                            # 界面模块
│   └── Renderer.cpp               #   ImGui 菜单渲染
├── ImGui/                         # ImGui 库（含 DX12 + Win32 后端）
├── CppSDK/                        # UE SDK（DUMPER-7 生成）
├── main.cpp                       # 完整版入口（UE 游戏）
├── main_test.cpp                  # 测试版入口（空白 ImGui）
├── build_modular.bat              # 完整版编译脚本
└── build_test.bat                 # 测试版编译脚本
```

### 层级关系

```
外部代码（main.cpp / main_test.cpp）
    │  注册 3 个回调 + 调用 InstallHooks / Shutdown
    ▼
Core::DX12Renderer（公开接口）
    │  管理 DX12 资源、ImGui 生命周期
    ▼
Core::DX12Internal（内部实现）
    │  Hook 回调、WndProc、渲染帧、线程同步
    ▼
Hook::HookManager → Hook::VmtHook
    │  VMT 修补、vtable 提取
    ▼
DirectX 12 / DXGI / ImGui
```

### 健壮性设计

| 机制 | 实现 |
|:-----|:-----|
| **GPU 同步** | 每帧 Fence wait（500ms 超时），资源释放前 Signal+Wait（5000ms） |
| **渲染序列化** | `CRITICAL_SECTION` 防止并发 Present/Resize 竞态 |
| **ImGui 线程安全** | `SRWLOCK`（`TryAcquire` 非阻塞），WndProc 不卡消息泵 |
| **异常保护** | 所有 hook 入口、GPU 操作、ImGui 调用均有 SEH 包裹 |
| **防递归** | `ResolveExecuteCommandListsFn` 从 vtable 解析真实函数，避免 hook 调回自己 |
| **两阶段队列** | Execute→`g_pendingQueue`（AddRef），Present 原子消费→`g_cmdQueue` |
| **Device Lost** | 检测 `DEVICE_REMOVED/RESET`，标记 `g_deviceLost`，下次 Present 自动恢复 |
| **安全卸载** | `g_unloading` + `g_suspendRendering` + 等待 `presentInFlight` 归零 |
| **WndProc 安全** | 重复 hook 检测 + `IsWindow` 验证 + Unicode API |
| **cmdList 录制状态追踪** | `g_cmdListRecording` 标志，异常时安全 Close |

---

## 开发指南

### 最小接入（不依赖 UE）

```cpp
#include "Core/DX12/DX12Renderer.h"

auto& renderer = Core::DX12Renderer::Instance();
renderer.SetRenderCallback([](float w, float h)
{
    ImGui::Begin("My Overlay");
    ImGui::Text("Hello DX12!");
    ImGui::End();
});
renderer.SetMenuVisibleCallback([]() { return showMenu; });
renderer.SetImGuiInitCallback([]() { /* 加载字体 */ });
renderer.InstallHooks();

// 卸载时
renderer.Shutdown();
```

### 添加 UE 游戏功能

修改 3 个文件：

```cpp
// 1. Core/Config.h — 添加开关
namespace Features {
    inline bool NewFeature_Enabled = false;
}

// 2. Core/GameData.cpp — 添加数据采集逻辑
void DataCollector::CollectData() {
    if (Config::Features::NewFeature_Enabled) { /* ... */ }
}

// 3. UI/Renderer.cpp — 添加 UI 控件
void Renderer::RenderMenu() {
    ImGui::Checkbox("新功能", &Config::Features::NewFeature_Enabled);
}
```

### 适配其他 D3D12 游戏

`Core/DX12/` 和 `Hook/` 层完全通用，无需修改。只需：

1. 用 [DUMPER-7](https://github.com/Encryqed/Dumper-7) 为目标游戏生成新的 CppSDK
2. 替换 `CppSDK/` 文件夹
3. 更新 `Core/Config.h` 中的偏移地址
4. 修改 `Core/GameData.cpp` 中的游戏逻辑

---

## 技术栈

| 组件 | 技术 |
|:-----|:-----|
| Hook 技术 | VMT Hook（VirtualProtect 修补） |
| 渲染 API | DirectX 12 + DXGI 1.4 |
| UI 框架 | ImGui（DX12 + Win32 后端） |
| 线程同步 | CRITICAL_SECTION + SRWLOCK |
| 异常保护 | SEH（`__try/__except`） |
| GPU 同步 | ID3D12Fence + Event |
| 编译器 | MSVC C++20 / MT 静态链接 |
| 构建 | bat 脚本 / Visual Studio 2022 |

## 常见故障排查

| 问题 | 解决方案 |
|:-----|:---------|
| 找不到头文件 / 编译错误 | 确认已安装 C++ 工作负载与 Windows SDK，使用 bat 脚本编译 |
| 链接错误 | 检查 `CppSDK/SDK/*.cpp` 是否被添加到构建中（bat 脚本自动处理） |
| 注入后无反应 | 先用 `DX12HookTest.dll` 测试 Hook 是否正常工作 |
| 注入崩溃 | 检查目标游戏是否使用 D3D12，反作弊可能阻止 VMT 修改 |
| 全屏切换后 overlay 消失 | 正常行为，框架会自动检测 SwapChain 变化并重建资源 |

## ⚠️ 免责声明

本项目仅供学习研究使用，请勿用于非法用途。使用本项目造成的任何后果由使用者自行承担。

## 贡献与许可证

本项目采用 MIT 许可证（见 `LICENSE`）。欢迎提交 issue 或 PR，但请确保用途合规。

---

更新时间：2026-02-28
