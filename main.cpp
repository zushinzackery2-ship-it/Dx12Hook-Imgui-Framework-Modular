/////////////////////
// Etb ESP - D3D12 //
/////////////////////

#include "main.h"
#include "Core/Config.h"
#include "Core/GameData.h"
#include "Core/DX12/DX12Renderer.h"
#include "UI/Renderer.h"

// ============================================================================
// 全局状态
// ============================================================================

char dlldir[320];
static HMODULE g_hModule = nullptr;
static bool g_showMenu = true;
static GameData::DataCollector* g_dataCollector = nullptr;

// ============================================================================
// DX12Renderer 回调
// ============================================================================

// 菜单可见性查询
static bool MenuVisibleCallback()
{
    return g_showMenu;
}

// ImGui 初始化回调（字体 + 样式）
static void ImGuiInitCallback()
{
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    // 加载中文字体
    io.Fonts->AddFontFromFileTTF(
        "C:\\Windows\\Fonts\\msyh.ttc", 22.0f,
        nullptr, io.Fonts->GetGlyphRangesChineseFull());

    // 模块化 UI 样式初始化
    UI::Renderer::Initialize();
}

// 每帧渲染回调
static void RenderCallback(float width, float height)
{
    // 快捷键控制
    static bool keyF1Down = false, keyF2Down = false;

    if (GetAsyncKeyState(VK_F1) & 0x8000)
    {
        if (!keyF1Down)
        {
            Config::Features::ESP_Enabled = !Config::Features::ESP_Enabled;
            keyF1Down = true;
        }
    }
    else
    {
        keyF1Down = false;
    }

    if (GetAsyncKeyState(VK_F2) & 0x8000)
    {
        if (!keyF2Down)
        {
            Config::Features::SpeedBoost_Enabled = !Config::Features::SpeedBoost_Enabled;
            keyF2Down = true;
        }
    }
    else
    {
        keyF2Down = false;
    }

    if (GetAsyncKeyState(VK_INSERT) & 1)
        g_showMenu = !g_showMenu;

    // 菜单打开时绘制半透明遮罩
    if (g_showMenu)
    {
        ImDrawList* bg = ImGui::GetBackgroundDrawList();
        ImVec2 displaySize = ImGui::GetIO().DisplaySize;
        bg->AddRectFilled(
            ImVec2(0.0f, 0.0f), ImVec2(displaySize.x, displaySize.y),
            IM_COL32(0, 0, 0, 120));
    }

    // 模块化渲染
    if (g_showMenu && g_dataCollector)
    {
        UI::Renderer::RenderMenu(g_dataCollector);
    }

    if (!g_showMenu && g_dataCollector)
    {
        UI::Renderer::RenderESP(g_dataCollector);
    }
}

// ============================================================================
// 主线程
// ============================================================================

static DWORD WINAPI MainThread(LPVOID lpParameter)
{
    // 等待游戏窗口获得焦点
    while (true)
    {
        DWORD foregroundPid = 0;
        GetWindowThreadProcessId(GetForegroundWindow(), &foregroundPid);
        if (GetCurrentProcessId() == foregroundPid)
            break;
        Sleep(50);
    }

    // 创建并启动数据收集器
    g_dataCollector = new GameData::DataCollector();
    g_dataCollector->StartCollection();

    // 初始化 Hook 框架
    while (!Hook::HookManager::Instance().Initialize())
    {
        Sleep(100);
    }

    // 配置 DX12Renderer 回调
    auto& renderer = Core::DX12Renderer::Instance();
    renderer.SetRenderCallback(RenderCallback);
    renderer.SetMenuVisibleCallback(MenuVisibleCallback);
    renderer.SetImGuiInitCallback(ImGuiInitCallback);

    // 安装所有 hook（Present / ResizeBuffers / ExecuteCommandLists / ResizeBuffers1）
    renderer.InstallHooks();

    return 0;
}

// ============================================================================
// DLL 入口
// ============================================================================

BOOL APIENTRY DllMain(HMODULE hModule, DWORD dwReason, LPVOID lpReserved)
{
    switch (dwReason)
    {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hModule);
        g_hModule = hModule;
        GetModuleFileNameA(hModule, dlldir, 512);
        for (size_t i = strlen(dlldir); i > 0; i--)
        {
            if (dlldir[i] == '\\')
            {
                dlldir[i + 1] = 0;
                break;
            }
        }
        CreateThread(nullptr, 0, MainThread, nullptr, 0, nullptr);
        break;

    case DLL_PROCESS_DETACH:
        // 停止数据收集
        if (g_dataCollector)
        {
            g_dataCollector->StopCollection();
            delete g_dataCollector;
            g_dataCollector = nullptr;
        }

        // DX12Renderer 负责完整的 unhook + ImGui 销毁 + 资源释放
        Core::DX12Renderer::Instance().Shutdown();
        break;

    default:
        break;
    }
    return TRUE;
}
