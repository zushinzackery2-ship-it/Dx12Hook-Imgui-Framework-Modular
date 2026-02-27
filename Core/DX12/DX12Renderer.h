#pragma once
#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>

// 渲染回调：在 ImGui NewFrame 之后、EndFrame 之前调用
typedef void (*PFN_RenderCallback)(float width, float height);

// 菜单可见性回调：返回 true 时拦截游戏输入
typedef bool (*PFN_MenuVisibleCallback)();

// ImGui 初始化回调：CreateContext 之后调用，用于加载字体和设置样式
typedef void (*PFN_ImGuiInitCallback)();

namespace Core
{

// DX12 渲染管理器
// 封装全部 overlay 渲染逻辑，含 Fence 同步、SEH 异常保护、线程安全等防御性设计
class DX12Renderer
{
public:
    static DX12Renderer& Instance();

    // 回调设置（必须在 InstallHooks 前调用）
    void SetRenderCallback(PFN_RenderCallback cb);
    void SetMenuVisibleCallback(PFN_MenuVisibleCallback cb);
    void SetImGuiInitCallback(PFN_ImGuiInitCallback cb);

    // 安装所有 hook（需要 HookManager 已初始化）
    bool InstallHooks();

    // 完整卸载：unhook → 等待 → 恢复 WndProc → 销毁 ImGui → 释放资源
    void Shutdown();

    // 状态查询
    HWND GetGameHWND() const;
    int  GetWidth() const;
    int  GetHeight() const;
    bool IsInitialized() const;

    // Hook 回调（静态函数，注册到 HookManager）
    static HRESULT STDMETHODCALLTYPE OnPresent(
        IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags);
    static HRESULT STDMETHODCALLTYPE OnResizeBuffers(
        IDXGISwapChain* pSwapChain, UINT BufferCount,
        UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags);
    static void STDMETHODCALLTYPE OnExecuteCommandLists(
        ID3D12CommandQueue* queue, UINT NumCommandLists,
        ID3D12CommandList* const* ppCommandLists);

private:
    DX12Renderer();
    ~DX12Renderer();
    DX12Renderer(const DX12Renderer&) = delete;
    DX12Renderer& operator=(const DX12Renderer&) = delete;
};

} // namespace Core
