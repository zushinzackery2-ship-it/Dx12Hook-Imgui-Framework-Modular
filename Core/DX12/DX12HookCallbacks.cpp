#include "DX12Internal.h"
#include "../../ImGui/imgui.h"
#include "../../ImGui/imgui_impl_dx12.h"
#include "../../ImGui/imgui_impl_win32.h"

extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace Core
{
namespace DX12Internal
{

// ============================================================================
// WndProc Hook
// ============================================================================

static LRESULT CALLBACK HookedWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    // 卸载中或未初始化时直接转发
    if (g_unloading || !g_initialized)
        return CallWindowProcW(g_origWndProc, hWnd, msg, wParam, lParam);

    // TryAcquire 非阻塞，避免卡死消息泵（渲染线程持有锁时直接跳过）
    __try
    {
        if (g_imguiLockReady && TryAcquireSRWLockExclusive(&g_imguiLock))
        {
            LRESULT result = ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam);
            ReleaseSRWLockExclusive(&g_imguiLock);
            if (result)
                return TRUE;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {}

    // 菜单可见时拦截游戏输入（鼠标 + 键盘）
    bool menuVisible = g_menuVisibleCallback ? g_menuVisibleCallback() : false;
    if (menuVisible)
    {
        switch (msg)
        {
        case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_LBUTTONDBLCLK:
        case WM_RBUTTONDOWN: case WM_RBUTTONUP: case WM_RBUTTONDBLCLK:
        case WM_MBUTTONDOWN: case WM_MBUTTONUP: case WM_MBUTTONDBLCLK:
        case WM_MOUSEWHEEL: case WM_MOUSEHWHEEL: case WM_MOUSEMOVE:
        case WM_KEYDOWN: case WM_KEYUP: case WM_SYSKEYDOWN: case WM_SYSKEYUP:
        case WM_CHAR:
            return TRUE;
        }
    }

    return CallWindowProcW(g_origWndProc, hWnd, msg, wParam, lParam);
}

// ============================================================================
// 首次 Present 时手动 hook ResizeBuffers1
// ============================================================================

void TryHookResizeBuffers1(IDXGISwapChain* swapChain)
{
    if (g_origResizeBuffers1)
        return;

    // 从实际 SwapChain 对象提取 vtable
    void** vtable = *(void***)swapChain;
    if (!vtable)
        return;

    g_swapChainVtable = vtable;
    VmtPatch(vtable, 39, (void*)&OnResizeBuffers1Callback, (void**)&g_origResizeBuffers1);
}

// ============================================================================
// 渲染一帧 ImGui
// ============================================================================

void RenderImGuiFrame(IDXGISwapChain* swapChain, ID3D12CommandQueue* renderQueue)
{
    if (!g_initialized || g_deviceLost || !renderQueue)
        return;

    // 获取当前后备缓冲区索引
    IDXGISwapChain3* sc3 = nullptr;
    if (FAILED(swapChain->QueryInterface(IID_PPV_ARGS(&sc3))))
        return;
    UINT bufIdx = sc3->GetCurrentBackBufferIndex();
    sc3->Release();

    if (bufIdx >= g_bufferCount)
        return;

    // 资源空指针验证
    if (!g_backBuffers[bufIdx] || !g_cmdAllocators[bufIdx])
        return;

    // Fence 同步：等待此缓冲区上一帧完成
    if (g_fence && g_fenceValues[bufIdx] > 0)
    {
        if (g_fence->GetCompletedValue() < g_fenceValues[bufIdx])
        {
            g_fence->SetEventOnCompletion(g_fenceValues[bufIdx], g_fenceEvent);
            DWORD waitResult = WaitForSingleObject(g_fenceEvent, 500);
            // 超时则跳过此帧，避免 Reset 仍在使用的 Allocator
            if (waitResult == WAIT_TIMEOUT)
                return;
        }
    }

    // 检查 Reset 返回值，失败则跳帧
    HRESULT hr = g_cmdAllocators[bufIdx]->Reset();
    if (FAILED(hr))
        return;

    hr = g_cmdList->Reset(g_cmdAllocators[bufIdx], nullptr);
    if (FAILED(hr))
        return;
    g_cmdListRecording = true;

    // 资源屏障：PRESENT → RENDER_TARGET
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = g_backBuffers[bufIdx];
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    g_cmdList->ResourceBarrier(1, &barrier);

    g_cmdList->OMSetRenderTargets(1, &g_rtvHandles[bufIdx], FALSE, nullptr);
    g_cmdList->SetDescriptorHeaps(1, &g_srvHeap);

    // SRWLOCK 仅保护 ImGui 帧逻辑，不包括 GPU 提交
    AcquireSRWLockExclusive(&g_imguiLock);
    __try
    {
        ImGui_ImplDX12_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        bool menuVisible = g_menuVisibleCallback ? g_menuVisibleCallback() : false;
        ImGui::GetIO().MouseDrawCursor = menuVisible;

        if (g_renderCallback)
            g_renderCallback(static_cast<float>(g_width), static_cast<float>(g_height));

        ImGui::EndFrame();
        ImGui::Render();
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
    ReleaseSRWLockExclusive(&g_imguiLock);

    // RenderDrawData 在 SRWLOCK 外执行，减少锁持有时间
    __try
    {
        ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), g_cmdList);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {}

    // 资源屏障：RENDER_TARGET → PRESENT
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    g_cmdList->ResourceBarrier(1, &barrier);

    g_cmdList->Close();
    g_cmdListRecording = false;

    // 使用解析后的原始函数避免递归调用 hook
    ID3D12CommandList* lists[] = { g_cmdList };
    if (!CallOriginalExecuteCommandListsSafe(renderQueue, 1, lists))
    {
        g_deviceLost = true;
        return;
    }

    // Fence 信号
    UINT64 fv = ++g_fenceCounter;
    g_fenceValues[bufIdx] = fv;
    renderQueue->Signal(g_fence, fv);
}

} // namespace DX12Internal

// ============================================================================
// OnPresent — 主要 hook 入口
// ============================================================================

HRESULT STDMETHODCALLTYPE DX12Renderer::OnPresent(
    IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags)
{
    using namespace DX12Internal;

    // 卸载中 / device lost / 暂停渲染：不计数，直接转发
    if (g_unloading || g_deviceLost || g_suspendRendering)
    {
        DWORD exCode = 0;
        HRESULT hr = CallOriginalPresentSafe(pSwapChain, SyncInterval, Flags, &exCode);
        if (exCode)
            g_deviceLost = true;
        return hr;
    }

    // 在飞 Present 计数，防止 Shutdown 时拆卸资源
    InterlockedIncrement(&g_presentInFlight);

    // 双检 suspendRendering（防止 Increment 前后的竞态）
    if (g_suspendRendering)
    {
        InterlockedDecrement(&g_presentInFlight);
        DWORD exCode = 0;
        HRESULT hr = CallOriginalPresentSafe(pSwapChain, SyncInterval, Flags, &exCode);
        if (exCode)
            g_deviceLost = true;
        return hr;
    }

    // 跳过 DXGI_PRESENT_TEST 和最小化窗口
    if ((Flags & DXGI_PRESENT_TEST) || (g_gameHwnd && IsIconic(g_gameHwnd)))
    {
        InterlockedDecrement(&g_presentInFlight);
        DWORD exCode = 0;
        HRESULT hr = CallOriginalPresentSafe(pSwapChain, SyncInterval, Flags, &exCode);
        if (exCode)
            g_deviceLost = true;
        return hr;
    }

    // 序列化：同一时刻只有一个线程在渲染
    if (g_renderCSReady)
        EnterCriticalSection(&g_renderCS);

    __try
    {
        // 原子消费 pendingQueue（Execute 线程写入，Present 线程消费）
        ID3D12CommandQueue* pending = (ID3D12CommandQueue*)InterlockedExchangePointer(
            (volatile PVOID*)&g_pendingQueue, nullptr);
        if (pending)
        {
            if (g_cmdQueue != pending)
            {
                ID3D12CommandQueue* oldQueue = g_cmdQueue;
                g_cmdQueue = pending;
                if (oldQueue)
                    oldQueue->Release();
            }
            else
            {
                // 相同 queue，释放多余的引用
                pending->Release();
            }
        }

        // SwapChain 变更检测（游戏重建了 SwapChain，如全屏切换）
        if (g_initialized && g_trackedSwapChain != pSwapChain)
        {
            g_imguiLockReady = false;
            if (g_origWndProc && g_gameHwnd && IsWindow(g_gameHwnd))
            {
                SetWindowLongPtrW(g_gameHwnd, GWLP_WNDPROC, (LONG_PTR)g_origWndProc);
                g_origWndProc = nullptr;
            }
            if (ImGui::GetCurrentContext())
            {
                ImGui_ImplDX12_Shutdown();
                ImGui_ImplWin32_Shutdown();
                ImGui::DestroyContext();
            }
            CleanupRenderResources();
            if (g_device) { g_device->Release(); g_device = nullptr; }
            g_initialized = false;
            g_frameCount = 0;
        }

        // Device lost 恢复：新 SwapChain 或 resize 后允许重新初始化
        if (!g_initialized && g_deviceLost)
            g_deviceLost = false;

        // 首次初始化
        if (!g_initialized)
        {
            if (g_cmdQueue)
            {
                if (CreateRenderResources(pSwapChain) && InitImGui(pSwapChain))
                {
                    // WndProc 重复 hook 检测
                    WNDPROC currentProc = (WNDPROC)GetWindowLongPtrW(g_gameHwnd, GWLP_WNDPROC);
                    if (currentProc != (WNDPROC)HookedWndProc)
                    {
                        g_origWndProc = (WNDPROC)SetWindowLongPtrW(
                            g_gameHwnd, GWLP_WNDPROC, (LONG_PTR)HookedWndProc);
                    }

                    g_trackedSwapChain = pSwapChain;
                    g_initialized = true;
                    g_frameCount = 0;

                    TryHookResizeBuffers1(pSwapChain);
                }
                else
                {
                    CleanupRenderResources();
                }
            }
        }

        // 渲染
        if (g_initialized && !g_deviceLost)
        {
            g_frameCount++;
            if (g_frameCount > WARMUP_FRAMES && !g_suspendRendering)
            {
                // Device lost 检测
                if (g_device)
                {
                    HRESULT reason = g_device->GetDeviceRemovedReason();
                    if (reason != S_OK)
                    {
                        g_deviceLost = true;
                    }
                }

                if (!g_deviceLost)
                {
                    // 更新窗口尺寸
                    DXGI_SWAP_CHAIN_DESC scDesc = {};
                    if (SUCCEEDED(pSwapChain->GetDesc(&scDesc)))
                    {
                        g_width = scDesc.BufferDesc.Width;
                        g_height = scDesc.BufferDesc.Height;
                    }

                    // 快照 cmdQueue，防止渲染期间被其他线程修改
                    ID3D12CommandQueue* renderQueue = g_cmdQueue;
                    RenderImGuiFrame(pSwapChain, renderQueue);
                }
            }
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        // 异常时关闭录制中的 CommandList
        if (g_cmdListRecording && g_cmdList)
        {
            __try { g_cmdList->Close(); }
            __except (EXCEPTION_EXECUTE_HANDLER) {}
            g_cmdListRecording = false;
        }
        // 标记 device lost，防止后续渲染使用损坏的 DX12 状态
        g_deviceLost = true;
    }

    if (g_renderCSReady)
        LeaveCriticalSection(&g_renderCS);

    // 调用原始 Present 并检查返回值和异常
    DWORD presentExCode = 0;
    HRESULT hr = CallOriginalPresentSafe(pSwapChain, SyncInterval, Flags, &presentExCode);
    if (presentExCode)
        g_deviceLost = true;

    // DEVICE_REMOVED / DEVICE_RESET 检测
    if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET)
        g_deviceLost = true;

    InterlockedDecrement(&g_presentInFlight);
    return hr;
}

// ============================================================================
// OnResizeBuffers
// ============================================================================

HRESULT STDMETHODCALLTYPE DX12Renderer::OnResizeBuffers(
    IDXGISwapChain* pSwapChain, UINT BufferCount,
    UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags)
{
    using namespace DX12Internal;

    // 暂停渲染，序列化
    g_suspendRendering = true;
    if (g_renderCSReady)
        EnterCriticalSection(&g_renderCS);

    if (g_initialized)
    {
        g_imguiLockReady = false;
        ImGui_ImplDX12_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        CleanupRenderResources();
        if (g_device) { g_device->Release(); g_device = nullptr; }
        g_initialized = false;
    }

    g_trackedSwapChain = nullptr;

    if (g_renderCSReady)
        LeaveCriticalSection(&g_renderCS);

    HRESULT hr = E_FAIL;
    if (g_origResizeBuffers)
        hr = g_origResizeBuffers(pSwapChain, BufferCount, Width, Height, NewFormat, SwapChainFlags);

    g_suspendRendering = false;
    return hr;
}

// ============================================================================
// OnResizeBuffers1（手动 hook，覆盖 HDR / 格式切换场景）
// ============================================================================

HRESULT STDMETHODCALLTYPE DX12Internal::OnResizeBuffers1Callback(
    IDXGISwapChain3* pSwapChain, UINT BufferCount,
    UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags,
    const UINT* pCreationNodeMask, IUnknown* const* ppPresentQueue)
{
    using namespace DX12Internal;

    g_suspendRendering = true;
    if (g_renderCSReady)
        EnterCriticalSection(&g_renderCS);

    if (g_initialized)
    {
        g_imguiLockReady = false;
        ImGui_ImplDX12_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        CleanupRenderResources();
        if (g_device) { g_device->Release(); g_device = nullptr; }
        g_initialized = false;
    }

    g_trackedSwapChain = nullptr;

    if (g_renderCSReady)
        LeaveCriticalSection(&g_renderCS);

    HRESULT hr = E_FAIL;
    if (g_origResizeBuffers1)
    {
        hr = g_origResizeBuffers1(
            pSwapChain, BufferCount, Width, Height, NewFormat, SwapChainFlags,
            pCreationNodeMask, ppPresentQueue);
    }

    g_suspendRendering = false;
    return hr;
}

// ============================================================================
// OnExecuteCommandLists — 捕获游戏的主 CommandQueue
// ============================================================================

void STDMETHODCALLTYPE DX12Renderer::OnExecuteCommandLists(
    ID3D12CommandQueue* queue, UINT NumCommandLists,
    ID3D12CommandList* const* ppCommandLists)
{
    using namespace DX12Internal;

    // 卸载中或 device lost 时跳过队列跟踪
    if (!g_unloading && !g_deviceLost)
    {
        __try
        {
            D3D12_COMMAND_QUEUE_DESC desc = queue->GetDesc();
            if (desc.Type == D3D12_COMMAND_LIST_TYPE_DIRECT)
            {
                // 写入 pendingQueue，由 Present 线程原子消费
                queue->AddRef();
                ID3D12CommandQueue* old = (ID3D12CommandQueue*)InterlockedExchangePointer(
                    (volatile PVOID*)&g_pendingQueue, queue);
                if (old)
                    old->Release();
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            // 队列跟踪崩溃，禁用 overlay
            g_deviceLost = true;
        }
    }

    if (g_origExecuteCommandLists)
        g_origExecuteCommandLists(queue, NumCommandLists, ppCommandLists);
}

} // namespace Core
