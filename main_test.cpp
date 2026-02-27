// 纯测试 DLL：空白 ImGui 窗口，INSERT 切换显示/隐藏
// 不依赖任何游戏逻辑，仅验证 DX12 Hook + ImGui 渲染是否正常工作

#include <windows.h>
#include "ImGui/imgui.h"
#include "Hook/HookManager.h"
#include "Core/DX12/DX12Renderer.h"

static HMODULE g_hModule = nullptr;
static bool g_showMenu = true;

// ============================================================================
// 回调
// ============================================================================

static bool MenuVisibleCallback()
{
    return g_showMenu;
}

static void ImGuiInitCallback()
{
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
}

static void RenderCallback(float width, float height)
{
    // INSERT 切换
    if (GetAsyncKeyState(VK_INSERT) & 1)
        g_showMenu = !g_showMenu;

    if (!g_showMenu)
        return;

    // 空白测试窗口
    ImGui::SetNextWindowSize(ImVec2(400, 300), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(
        ImVec2(width * 0.5f - 200, height * 0.5f - 150),
        ImGuiCond_FirstUseEver);

    if (ImGui::Begin("DX12 Hook Test", &g_showMenu))
    {
        ImGui::Text("DX12 Hook + ImGui OK");
        ImGui::Separator();
        ImGui::Text("Resolution: %.0f x %.0f", width, height);
        ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f),
            "[INSERT] Toggle Menu");
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
            "[END] Eject DLL");
    }
    ImGui::End();
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

    // 初始化 Hook 框架
    while (!Hook::HookManager::Instance().Initialize())
        Sleep(100);

    // 配置 DX12Renderer
    auto& renderer = Core::DX12Renderer::Instance();
    renderer.SetRenderCallback(RenderCallback);
    renderer.SetMenuVisibleCallback(MenuVisibleCallback);
    renderer.SetImGuiInitCallback(ImGuiInitCallback);
    renderer.InstallHooks();

    // END 键卸载
    while (!(GetAsyncKeyState(VK_END) & 1))
        Sleep(100);

    renderer.Shutdown();
    Sleep(200);
    FreeLibraryAndExitThread(g_hModule, 0);
    return 0;
}

// ============================================================================
// DLL 入口
// ============================================================================

BOOL APIENTRY DllMain(HMODULE hModule, DWORD dwReason, LPVOID lpReserved)
{
    if (dwReason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(hModule);
        g_hModule = hModule;
        CreateThread(nullptr, 0, MainThread, nullptr, 0, nullptr);
    }
    return TRUE;
}
