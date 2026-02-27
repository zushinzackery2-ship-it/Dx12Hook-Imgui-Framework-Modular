<div align="center">

# D3D12 Game Hook Framework

**通用 D3D12 Hook 框架 + UE 游戏功能实现**

*跨游戏兼容 | 模块化设计 | 支持 UE4/UE5*

![C++](https://img.shields.io/badge/C%2B%2B-17-blue?style=flat-square)
![Platform](https://img.shields.io/badge/Platform-Windows%20x64-lightgrey?style=flat-square)
![License](https://img.shields.io/badge/License-MIT-yellow?style=flat-square)

</div>

---

> [!CAUTION]
> **免责声明**  
> 本项目仅供安全研究与学习使用。请勿将本项目用于在线或多人游戏的作弊、入侵或其他违法活动。作者对任何滥用行为导致的后果不承担责任。
> 
> **Hook 技术兼容性说明**  
> 本框架采用 **VMT Hook** 而非 Inline Hook。因此对于网游的兼容性可能有限。如果注入后出现崩溃，需要自行进行调整。建议在隔离环境中先行测试。
> 
> **反作弊兼容性说明**  
> 本框架不保证在带有反作弊保护的游戏中的兼容性。对于受 EAC、BattlEye、VAC 等反作弊系统保护的游戏，可能需要额外的分析和优化才能使用。使用者需自行承担因使用本框架导致的所有风险，包括但不限于账号封禁等后果。

> [!NOTE]
> **版本兼容性说明**  
> - 支持 Windows 10/11 x64 系统
> - 需要 DirectX 12 兼容的 GPU
> - 支持 UE4/UE5 游戏（基于 DUMPER-7 生成的 CppSDK）

---

## 特性

| 功能 | 说明 |
|:-----|:-----|
| **通用 D3D12 Hook** | 适用于大部分 D3D12 游戏 |
| **UE 游戏支持** | 使用 DUMPER-7 生成的 CppSDK，支持 UE4/UE5 游戏 |
| **模块化架构** | 代码清晰，职责分明 |
| **ImGui 界面** | 现代化游戏内菜单 |
| **易于扩展** | 添加新功能只需修改 3 个文件 |
| **性能优化** | 高效的 Hook 实现，最小化性能影响 |

---

## 快速开始

### 先决条件

- Windows 10 / Windows 11 x64
- Visual Studio 2022（v143 工具集）
- Windows SDK（最新版）
- 支持 DirectX 12 的 GPU

### 编译

#### 方式 1：使用批处理脚本（推荐）

```bash
# 在项目根目录的 Developer Command Prompt for VS 2022 中运行：
.\build_modular.bat
# 输出文件将保存在 bin 目录下
```

> **注意**：
> - 如果遇到路径问题，请修改 `build_modular.bat` 中的 VS 安装路径
> - 默认使用 `/MT` 编译选项，如需更改请编辑脚本

#### 方式 2：使用 Visual Studio

1. 打开 `d3d12imgui.sln`
2. 选择 `Release | x64` 配置
3. 生成解决方案

> **注意**：
> - 若在 VS 中手动编译，需手动将 `CppSDK/SDK/*.cpp` 添加到项目中
> - 推荐使用 `build_modular.bat` 来自动处理依赖

### 使用说明

1. 将生成的 `Etb_Esp.dll` 注入到目标游戏进程
2. 默认快捷键：
   - `INSERT` - 显示/隐藏菜单
   - `F1` - 物品透视
   - `F2` - 加速功能
   - `速度倍率` 滑条 - 调整游戏速度 (0.5x ~ 5.0x)

---

## 项目结构

```
D3D12-Hook-ImGui/
├── Hook/              # Hook 模块 (通用 D3D12)
│   ├── VmtHook.cpp    # VMT 钩子实现
│   ├── D3D12Hook.cpp  # D3D12 初始化
│   └── HookManager.cpp # Hook 统一管理
├── Core/              # 核心模块 (UE 游戏功能)
│   ├── Config.h       # 配置管理
│   ├── GameData.cpp   # 游戏数据收集
│   └── GameMemory.h   # 内存读写
├── UI/                # 界面模块
│   └── Renderer.cpp   # ImGui 渲染
├── ImGui/             # ImGui 库
├── CppSDK/            # UE SDK (DUMPER-7 生成)
└── main.cpp           # 入口点
```

> **架构说明**：
> - `Hook/` 层是**通用的**，可用于任何 D3D12 游戏
> - `Core/` 层实现了 UE 游戏特定功能
> - `CppSDK/` 由 [DUMPER-7](https://github.com/Encryqed/Dumper-7) 生成，包含游戏的类结构

---

## 开发指南

### 添加新功能

只需修改 3 个文件：

```cpp
// 1. Core/Config.h - 添加配置
namespace Features {
    inline bool NewFeature_Enabled = false;
}

// 2. Core/GameData.cpp - 添加逻辑
void DataCollector::CollectData() {
    if (Config::Features::NewFeature_Enabled) {
        // 你的逻辑
    }
}

// 3. UI/Renderer.cpp - 添加 UI
void Renderer::RenderMenu() {
    ImGui::Checkbox("新功能", &Config::Features::NewFeature_Enabled);
}
```

## 📋 系统要求

- Windows 10/11
- Visual Studio 2022 (v143)
- DirectX 12 支持
- 目标游戏：Unreal Engine 4/5 (D3D12)

## 🔄 适配其他游戏

### 适配其他 D3D12 游戏
Hook 层无需修改，只需：
1. 使用 [DUMPER-7](https://github.com/Encryqed/Dumper-7) 为目标游戏生成新的 CppSDK
2. 替换 `CppSDK/` 文件夹
3. 更新 `Core/Config.h` 中的偏移地址
4. 修改 `Core/GameData.cpp` 中的游戏逻辑

### 适配其他渲染 API
- **D3D11**: 修改 `Hook/D3D12Hook.cpp` → `D3D11Hook.cpp`
- **D3D9/OpenGL**: 需重写 Hook 模块

## 📝 技术栈

| 组件 | 技术 |
|------|------|
| Hook 技术 | VMT Hook (通用) |
| 渲染 API | DirectX 12 |
| UI 框架 | ImGui |
| 游戏引擎 | Unreal Engine 4/5 |
| SDK 生成 | DUMPER-7 |
| 编译器 | MSVC (C++20) |
| 架构 | 模块化设计 |

## 常见故障排查（快速）

- 找不到头文件或编译错误：请确认已安装 C++ 工作负载与 Windows SDK，并在 Developer Command Prompt 中运行 `build_modular.bat`。
- 链接错误：检查是否缺少 `CppSDK/SDK/*.cpp` 被添加到构建中（脚本通常会处理，手动编译时需留意）。
- 注入失败或被杀进程：某些安全软件或游戏反作弊会阻止注入，需在受控环境中测试。

## ⚠️ 免责声明

本项目仅供学习研究使用，请勿用于非法用途。使用本项目造成的任何后果由使用者自行承担。

## 贡献与许可证

- 本项目采用 MIT 许可证（见 `LICENSE`）。欢迎提交 issue 或 PR，但请确保用途合规。

---
更新时间：2025-12-19
