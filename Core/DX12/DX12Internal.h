#pragma once
#include "DX12Renderer.h"

// 两个实现文件（DX12Renderer.cpp 和 DX12HookCallbacks.cpp）的共享内部状态
// 外部代码不应包含此头文件

namespace Core
{
namespace DX12Internal
{

static constexpr int MAX_BACK_BUFFERS = 8;
static constexpr UINT WARMUP_FRAMES = 5;

// ============================================================================
// 原始函数类型
// ============================================================================

typedef HRESULT(STDMETHODCALLTYPE* PFN_Present)(
    IDXGISwapChain*, UINT, UINT);
typedef HRESULT(STDMETHODCALLTYPE* PFN_ResizeBuffers)(
    IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);
typedef void(STDMETHODCALLTYPE* PFN_ExecuteCommandLists)(
    ID3D12CommandQueue*, UINT, ID3D12CommandList* const*);
typedef HRESULT(STDMETHODCALLTYPE* PFN_ResizeBuffers1)(
    IDXGISwapChain3*, UINT, UINT, UINT, DXGI_FORMAT, UINT,
    const UINT*, IUnknown* const*);

// ============================================================================
// 原始函数指针
// ============================================================================

extern PFN_Present              g_origPresent;
extern PFN_ResizeBuffers        g_origResizeBuffers;
extern PFN_ExecuteCommandLists  g_origExecuteCommandLists;
extern PFN_ResizeBuffers1       g_origResizeBuffers1;

// ============================================================================
// DX12 对象
// ============================================================================

extern ID3D12Device*                g_device;
extern ID3D12CommandQueue*          g_cmdQueue;
extern ID3D12CommandQueue*          g_pendingQueue;
extern ID3D12DescriptorHeap*        g_rtvHeap;
extern ID3D12DescriptorHeap*        g_srvHeap;
extern ID3D12CommandAllocator*      g_cmdAllocators[MAX_BACK_BUFFERS];
extern ID3D12GraphicsCommandList*   g_cmdList;
extern ID3D12Resource*              g_backBuffers[MAX_BACK_BUFFERS];
extern D3D12_CPU_DESCRIPTOR_HANDLE  g_rtvHandles[MAX_BACK_BUFFERS];
extern UINT                         g_bufferCount;
extern UINT                         g_rtvDescSize;

// ============================================================================
// Fence 同步（防止 GPU 仍在使用 CommandAllocator 时 Reset）
// ============================================================================

extern ID3D12Fence* g_fence;
extern HANDLE       g_fenceEvent;
extern UINT64       g_fenceValues[MAX_BACK_BUFFERS];
extern UINT64       g_fenceCounter;

// ============================================================================
// 窗口
// ============================================================================

extern HWND    g_gameHwnd;
extern WNDPROC g_origWndProc;
extern int     g_width;
extern int     g_height;

// ============================================================================
// SwapChain 跟踪
// ============================================================================

extern DXGI_FORMAT      g_backBufferFormat;
extern IDXGISwapChain*  g_trackedSwapChain;
extern void**           g_swapChainVtable;

// ============================================================================
// 状态标志
// ============================================================================

extern bool             g_initialized;
extern volatile bool    g_unloading;
extern bool             g_deviceLost;
extern volatile LONG    g_presentInFlight;
extern volatile bool    g_suspendRendering;
extern volatile bool    g_cmdListRecording;

// ============================================================================
// 线程同步
// ============================================================================

extern CRITICAL_SECTION g_renderCS;
extern bool             g_renderCSReady;
extern SRWLOCK          g_imguiLock;
extern bool             g_imguiLockReady;

// ============================================================================
// 帧计数 + 回调
// ============================================================================

extern UINT g_frameCount;

extern PFN_RenderCallback       g_renderCallback;
extern PFN_MenuVisibleCallback  g_menuVisibleCallback;
extern PFN_ImGuiInitCallback    g_imguiInitCallback;

// ============================================================================
// 内部辅助函数
// ============================================================================

// VMT 手动修补（用于 ResizeBuffers1 等超出框架范围的 hook）
bool VmtPatch(void** vtable, int index, void* hookFn, void** origFn);
bool VmtRestore(void** vtable, int index, void* origFn);

// 安全调用原始 Present（SEH 保护）
HRESULT CallOriginalPresentSafe(
    IDXGISwapChain* sc, UINT sync, UINT flags,
    DWORD* outExCode = nullptr);

// 安全调用原始 ExecuteCommandLists（解析真实函数指针避免递归）
bool CallOriginalExecuteCommandListsSafe(
    ID3D12CommandQueue* queue, UINT num, ID3D12CommandList* const* lists);

// DX12 资源生命周期
bool CreateRenderResources(IDXGISwapChain* swapChain);
void CleanupRenderResources();

// ImGui 初始化
bool InitImGui(IDXGISwapChain* swapChain);

// 渲染一帧（在 Present 内部调用）
void RenderImGuiFrame(IDXGISwapChain* swapChain, ID3D12CommandQueue* queue);

// ResizeBuffers1 回调（手动 hook，不走 HookManager）
HRESULT STDMETHODCALLTYPE OnResizeBuffers1Callback(
    IDXGISwapChain3* pSwapChain, UINT BufferCount,
    UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags,
    const UINT* pCreationNodeMask, IUnknown* const* ppPresentQueue);

// 在首次 Present 时手动 hook ResizeBuffers1
void TryHookResizeBuffers1(IDXGISwapChain* swapChain);

} // namespace DX12Internal
} // namespace Core
