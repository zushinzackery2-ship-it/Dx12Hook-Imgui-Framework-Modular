#pragma once
#include <Windows.h>
#include <cstdint>

namespace Config {
    // ========== 游戏偏移配置 ==========
    namespace Offsets {
        // 通用 UE 偏移
        constexpr DWORD64 GWorld = 0x04D4C258;
        
        // 游戏特定偏移
        // APawn + 0x98 = CustomTimeDilation (float)
        constexpr DWORD64 PlayerSpeedOffset = 0x98;
    }
    
    // ========== 功能开关 ==========
    namespace Features {
        inline bool ESP_Enabled = false;
        inline bool SpeedBoost_Enabled = false;
        inline float SpeedMultiplier = 1.0f;  // 速度倍率 (1.0 = 正常, 2.0 = 2倍速)
    }
    
    // ========== UI配置 ==========
    namespace UI {
        constexpr float FontScale = 1.0f;
        constexpr int MenuWidth = 480;
        constexpr int MenuHeight = 320;
    }
}
