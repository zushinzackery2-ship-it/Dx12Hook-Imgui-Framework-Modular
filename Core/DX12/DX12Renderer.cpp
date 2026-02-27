#include "DX12Internal.h"
#include "../../Hook/HookManager.h"
#include "../../ImGui/imgui.h"
#include "../../ImGui/imgui_impl_dx12.h"
#include "../../ImGui/imgui_impl_win32.h"

namespace Core
{
namespace DX12Internal
{

// ============================================================================
// 状态变量定义
// ============================================================================

PFN_Present              g_origPresent = nullptr;
PFN_ResizeBuffers        g_origResizeBuffers = nullptr;
PFN_ExecuteCommandLists  g_origExecuteCommandLists = nullptr;
PFN_ResizeBuffers1       g_origResizeBuffers1 = nullptr;

ID3D12Device*               g_device = nullptr;
ID3D12CommandQueue*         g_cmdQueue = nullptr;
ID3D12CommandQueue*         g_pendingQueue = nullptr;
ID3D12DescriptorHeap*       g_rtvHeap = nullptr;
ID3D12DescriptorHeap*       g_srvHeap = nullptr;
ID3D12CommandAllocator*     g_cmdAllocators[MAX_BACK_BUFFERS] = {};
ID3D12GraphicsCommandList*  g_cmdList = nullptr;
ID3D12Resource*             g_backBuffers[MAX_BACK_BUFFERS] = {};
D3D12_CPU_DESCRIPTOR_HANDLE g_rtvHandles[MAX_BACK_BUFFERS] = {};
UINT                        g_bufferCount = 0;
UINT                        g_rtvDescSize = 0;

ID3D12Fence* g_fence = nullptr;
HANDLE       g_fenceEvent = nullptr;
UINT64       g_fenceValues[MAX_BACK_BUFFERS] = {};
UINT64       g_fenceCounter = 0;

HWND    g_gameHwnd = nullptr;
WNDPROC g_origWndProc = nullptr;
int     g_width = 0;
int     g_height = 0;

DXGI_FORMAT     g_backBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
IDXGISwapChain* g_trackedSwapChain = nullptr;
void**          g_swapChainVtable = nullptr;

bool             g_initialized = false;
volatile bool    g_unloading = false;
bool             g_deviceLost = false;
volatile LONG    g_presentInFlight = 0;
volatile bool    g_suspendRendering = false;
volatile bool    g_cmdListRecording = false;

CRITICAL_SECTION g_renderCS = {};
bool             g_renderCSReady = false;
SRWLOCK          g_imguiLock = SRWLOCK_INIT;
bool             g_imguiLockReady = false;

UINT g_frameCount = 0;

PFN_RenderCallback       g_renderCallback = nullptr;
PFN_MenuVisibleCallback  g_menuVisibleCallback = nullptr;
PFN_ImGuiInitCallback    g_imguiInitCallback = nullptr;

// ============================================================================
// VMT 手动修补（VirtualProtect 方式）
// ============================================================================

bool VmtPatch(void** vtable, int index, void* hookFn, void** origFn)
{
    if (!vtable || !hookFn)
        return false;

    DWORD oldProtect = 0;
    if (!VirtualProtect(&vtable[index], sizeof(void*), PAGE_READWRITE, &oldProtect))
        return false;

    if (origFn)
        *origFn = vtable[index];
    vtable[index] = hookFn;

    VirtualProtect(&vtable[index], sizeof(void*), oldProtect, &oldProtect);
    return true;
}

bool VmtRestore(void** vtable, int index, void* origFn)
{
    if (!vtable || !origFn)
        return false;

    DWORD oldProtect = 0;
    if (!VirtualProtect(&vtable[index], sizeof(void*), PAGE_READWRITE, &oldProtect))
        return false;

    vtable[index] = origFn;
    VirtualProtect(&vtable[index], sizeof(void*), oldProtect, &oldProtect);
    return true;
}

// ============================================================================
// 函数指针解析（通过 vtable 获取真实函数，避免反作弊恢复 vtable 后调回自己）
// ============================================================================

static PFN_Present ResolvePresentFn(IDXGISwapChain* swapChain)
{
    if (swapChain)
    {
        void** vtable = *reinterpret_cast<void***>(swapChain);
        if (vtable)
        {
            auto fn = reinterpret_cast<PFN_Present>(vtable[8]);
            if (fn && fn != reinterpret_cast<PFN_Present>(&DX12Renderer::OnPresent))
                return fn;
        }
    }
    return g_origPresent;
}

static PFN_ExecuteCommandLists ResolveExecuteCommandListsFn(ID3D12CommandQueue* queue)
{
    if (queue)
    {
        void** vtable = *reinterpret_cast<void***>(queue);
        if (vtable)
        {
            auto fn = reinterpret_cast<PFN_ExecuteCommandLists>(vtable[10]);
            if (fn && fn != reinterpret_cast<PFN_ExecuteCommandLists>(&DX12Renderer::OnExecuteCommandLists))
                return fn;
        }
    }
    return g_origExecuteCommandLists;
}

// ============================================================================
// 安全调用原始 Present（SEH 包裹 + vtable 解析）
// ============================================================================

HRESULT CallOriginalPresentSafe(
    IDXGISwapChain* sc, UINT sync, UINT flags, DWORD* outExCode)
{
    if (outExCode)
        *outExCode = 0;

    PFN_Present fn = ResolvePresentFn(sc);
    if (!fn)
        return E_FAIL;

    __try
    {
        return fn(sc, sync, flags);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        if (outExCode)
            *outExCode = GetExceptionCode();
        return E_FAIL;
    }
}

// ============================================================================
// 安全调用原始 ExecuteCommandLists（避免递归调用 hook）
// ============================================================================

bool CallOriginalExecuteCommandListsSafe(
    ID3D12CommandQueue* queue, UINT num, ID3D12CommandList* const* lists)
{
    PFN_ExecuteCommandLists fn = ResolveExecuteCommandListsFn(queue);
    if (!fn)
        return false;
    fn(queue, num, lists);
    return true;
}

// ============================================================================
// 资源清理
// ============================================================================

void CleanupRenderResources()
{
    // 等待 GPU 完成所有帧，避免释放仍在使用的资源
    if (g_fence && g_cmdQueue && g_fenceEvent)
    {
        UINT64 waitValue = ++g_fenceCounter;
        __try
        {
            g_cmdQueue->Signal(g_fence, waitValue);
            if (g_fence->GetCompletedValue() < waitValue)
            {
                g_fence->SetEventOnCompletion(waitValue, g_fenceEvent);
                WaitForSingleObject(g_fenceEvent, 5000);
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    // 关闭录制中的 CommandList
    if (g_cmdListRecording && g_cmdList)
    {
        __try { g_cmdList->Close(); }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
        g_cmdListRecording = false;
    }

    for (UINT i = 0; i < MAX_BACK_BUFFERS; i++)
    {
        if (g_backBuffers[i]) { g_backBuffers[i]->Release(); g_backBuffers[i] = nullptr; }
        if (g_cmdAllocators[i]) { g_cmdAllocators[i]->Release(); g_cmdAllocators[i] = nullptr; }
        g_fenceValues[i] = 0;
    }

    if (g_cmdList) { g_cmdList->Release(); g_cmdList = nullptr; }
    if (g_rtvHeap) { g_rtvHeap->Release(); g_rtvHeap = nullptr; }
    if (g_srvHeap) { g_srvHeap->Release(); g_srvHeap = nullptr; }
    if (g_fence) { g_fence->Release(); g_fence = nullptr; }
    if (g_fenceEvent) { CloseHandle(g_fenceEvent); g_fenceEvent = nullptr; }

    g_bufferCount = 0;
    g_fenceCounter = 0;
}

// ============================================================================
// 资源创建
// ============================================================================

bool CreateRenderResources(IDXGISwapChain* swapChain)
{
    DXGI_SWAP_CHAIN_DESC desc = {};
    if (FAILED(swapChain->GetDesc(&desc)))
        return false;

    g_bufferCount = desc.BufferCount;
    if (g_bufferCount > MAX_BACK_BUFFERS)
        g_bufferCount = MAX_BACK_BUFFERS;
    g_backBufferFormat = desc.BufferDesc.Format;
    if (g_backBufferFormat == DXGI_FORMAT_UNKNOWN)
        g_backBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

    if (!g_device)
    {
        if (FAILED(swapChain->GetDevice(IID_PPV_ARGS(&g_device))))
            return false;
    }

    // RTV 描述符堆
    D3D12_DESCRIPTOR_HEAP_DESC rtvDesc = {};
    rtvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvDesc.NumDescriptors = g_bufferCount;
    rtvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    if (FAILED(g_device->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&g_rtvHeap))))
        return false;

    g_rtvDescSize = g_device->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    // SRV 描述符堆（ImGui 字体纹理）
    D3D12_DESCRIPTOR_HEAP_DESC srvDesc = {};
    srvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvDesc.NumDescriptors = 1;
    srvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(g_device->CreateDescriptorHeap(&srvDesc, IID_PPV_ARGS(&g_srvHeap))))
        return false;

    // CommandAllocator + BackBuffer + RTV（每个后备缓冲区一组）
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = g_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    for (UINT i = 0; i < g_bufferCount; i++)
    {
        if (FAILED(g_device->CreateCommandAllocator(
                D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&g_cmdAllocators[i]))))
            return false;

        if (FAILED(swapChain->GetBuffer(i, IID_PPV_ARGS(&g_backBuffers[i]))))
            return false;

        g_rtvHandles[i] = rtvHandle;
        g_device->CreateRenderTargetView(g_backBuffers[i], nullptr, rtvHandle);
        rtvHandle.ptr += g_rtvDescSize;
    }

    // CommandList
    if (FAILED(g_device->CreateCommandList(
            0, D3D12_COMMAND_LIST_TYPE_DIRECT,
            g_cmdAllocators[0], nullptr, IID_PPV_ARGS(&g_cmdList))))
        return false;
    g_cmdList->Close();

    // Fence（GPU 同步）
    if (FAILED(g_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&g_fence))))
        return false;
    g_fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!g_fenceEvent)
        return false;

    g_fenceCounter = 0;
    for (UINT i = 0; i < g_bufferCount; i++)
        g_fenceValues[i] = 0;

    return true;
}

// ============================================================================
// ImGui 初始化
// ============================================================================

bool InitImGui(IDXGISwapChain* swapChain)
{
    DXGI_SWAP_CHAIN_DESC desc = {};
    swapChain->GetDesc(&desc);

    g_gameHwnd = desc.OutputWindow;
    g_width = desc.BufferDesc.Width;
    g_height = desc.BufferDesc.Height;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;

    // 回调：让外部代码设置字体和样式
    if (g_imguiInitCallback)
        g_imguiInitCallback();

    if (!ImGui_ImplWin32_Init(g_gameHwnd))
        return false;
    if (!ImGui_ImplDX12_Init(
            g_device,
            static_cast<int>(g_bufferCount),
            g_backBufferFormat,
            g_srvHeap,
            g_srvHeap->GetCPUDescriptorHandleForHeapStart(),
            g_srvHeap->GetGPUDescriptorHandleForHeapStart()))
        return false;
    ImGui_ImplDX12_CreateDeviceObjects();

    // ImGui 已就绪，允许 WndProc 访问
    g_imguiLockReady = true;

    return true;
}

} // namespace DX12Internal

// ============================================================================
// DX12Renderer 公开接口
// ============================================================================

DX12Renderer::DX12Renderer() = default;
DX12Renderer::~DX12Renderer() = default;

DX12Renderer& DX12Renderer::Instance()
{
    static DX12Renderer instance;
    return instance;
}

void DX12Renderer::SetRenderCallback(PFN_RenderCallback cb)
{
    DX12Internal::g_renderCallback = cb;
}

void DX12Renderer::SetMenuVisibleCallback(PFN_MenuVisibleCallback cb)
{
    DX12Internal::g_menuVisibleCallback = cb;
}

void DX12Renderer::SetImGuiInitCallback(PFN_ImGuiInitCallback cb)
{
    DX12Internal::g_imguiInitCallback = cb;
}

HWND DX12Renderer::GetGameHWND() const { return DX12Internal::g_gameHwnd; }
int  DX12Renderer::GetWidth()    const { return DX12Internal::g_width; }
int  DX12Renderer::GetHeight()   const { return DX12Internal::g_height; }
bool DX12Renderer::IsInitialized() const { return DX12Internal::g_initialized; }

bool DX12Renderer::InstallHooks()
{
    using namespace DX12Internal;

    // 初始化线程同步原语
    if (!g_renderCSReady)
    {
        InitializeCriticalSection(&g_renderCS);
        g_renderCSReady = true;
    }
    InitializeSRWLock(&g_imguiLock);
    // g_imguiLockReady 在 InitImGui 成功后才设为 true

    g_unloading = false;
    g_suspendRendering = false;
    g_deviceLost = false;

    auto& hookMgr = Hook::HookManager::Instance();

    // 安装失败时回滚已安装的 hook
    auto rollback = [&hookMgr]()
    {
        hookMgr.UninstallHook(140);
        hookMgr.UninstallHook(145);
        hookMgr.UninstallHook(54);
    };

    // Present = SwapChain vtable index 8，统一索引 140
    if (!hookMgr.InstallHook(140, (void**)&g_origPresent, &DX12Renderer::OnPresent))
    {
        rollback();
        return false;
    }

    // ResizeBuffers = SwapChain vtable index 13，统一索引 145
    if (!hookMgr.InstallHook(145, (void**)&g_origResizeBuffers, &DX12Renderer::OnResizeBuffers))
    {
        rollback();
        return false;
    }

    // ExecuteCommandLists = CommandQueue vtable index 10，统一索引 54
    if (!hookMgr.InstallHook(54, (void**)&g_origExecuteCommandLists, &DX12Renderer::OnExecuteCommandLists))
    {
        rollback();
        return false;
    }

    // ResizeBuffers1 (IDXGISwapChain3 vtable index 39) 超出 HookManager 的范围
    // 延迟到首次 Present 时从实际 SwapChain 获取 vtable 并手动 hook

    return true;
}

void DX12Renderer::Shutdown()
{
    using namespace DX12Internal;
    g_unloading = true;
    g_suspendRendering = true;

    // 等待正在执行的 Present 完成
    for (int i = 0; i < 100 && g_presentInFlight > 0; i++)
        Sleep(10);

    // 卸载 hook
    auto& hookMgr = Hook::HookManager::Instance();
    hookMgr.UninstallHook(140);
    hookMgr.UninstallHook(145);
    hookMgr.UninstallHook(54);

    // 恢复手动 hook 的 ResizeBuffers1
    if (g_swapChainVtable && g_origResizeBuffers1)
    {
        VmtRestore(g_swapChainVtable, 39, g_origResizeBuffers1);
        g_origResizeBuffers1 = nullptr;
    }

    Sleep(100);

    // 禁止 WndProc 访问 ImGui
    g_imguiLockReady = false;

    // 恢复 WndProc
    if (g_origWndProc && g_gameHwnd && IsWindow(g_gameHwnd))
    {
        SetWindowLongPtrW(g_gameHwnd, GWLP_WNDPROC, (LONG_PTR)g_origWndProc);
        g_origWndProc = nullptr;
    }

    // 销毁 ImGui
    if (g_initialized)
    {
        __try
        {
            ImGui_ImplDX12_Shutdown();
            ImGui_ImplWin32_Shutdown();
            ImGui::DestroyContext();
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    CleanupRenderResources();

    // 释放 CommandQueue 引用
    if (g_cmdQueue)
    {
        g_cmdQueue->Release();
        g_cmdQueue = nullptr;
    }

    // 释放 pendingQueue 引用
    {
        ID3D12CommandQueue* pq = (ID3D12CommandQueue*)InterlockedExchangePointer(
            (volatile PVOID*)&g_pendingQueue, nullptr);
        if (pq)
            pq->Release();
    }

    // 释放 Device 引用
    if (g_device)
    {
        g_device->Release();
        g_device = nullptr;
    }

    g_initialized = false;

    if (g_renderCSReady)
    {
        DeleteCriticalSection(&g_renderCS);
        g_renderCSReady = false;
    }
}

} // namespace Core
