/////////////////////
// Etb ESP - D3D12 //
/////////////////////

#include "main.h"
#include "Core/Config.h"
#include "Core/GameData.h"
#include "UI/Renderer.h"

#ifndef ENABLE_MENU_MOUSE_ISOLATION
#define ENABLE_MENU_MOUSE_ISOLATION 1
#endif

// 全局变量定义
char dlldir[320];

// 数据收集器实例
static GameData::DataCollector* g_DataCollector = nullptr;

//=========================================================================================================================//

typedef HRESULT(APIENTRY* Present12) (IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags);
Present12 oPresent = NULL;

typedef void(APIENTRY* ExecuteCommandLists)(ID3D12CommandQueue* queue, UINT NumCommandLists, ID3D12CommandList* ppCommandLists);
ExecuteCommandLists oExecuteCommandLists = NULL;

typedef HRESULT(APIENTRY* ResizeBuffers12)(IDXGISwapChain* pSwapChain, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags);
ResizeBuffers12 oResizeBuffers = NULL;

//=========================================================================================================================//

bool ShowMenu = true; // 默认显示菜单
bool ImGui_Initialised = false;

namespace Process {
	DWORD ID;
	HANDLE Handle;
	HWND Hwnd;
	HMODULE Module;
	WNDPROC WndProc;
	int WindowWidth;
	int WindowHeight;
	LPCSTR Title;
	LPCSTR ClassName;
	LPCSTR Path;
}

namespace DirectX12Interface {
	ID3D12Device* Device = nullptr;
	ID3D12DescriptorHeap* DescriptorHeapBackBuffers;
	ID3D12DescriptorHeap* DescriptorHeapImGuiRender;
	ID3D12GraphicsCommandList* CommandList;
	ID3D12CommandQueue* CommandQueue;

	struct _FrameContext {
		ID3D12CommandAllocator* CommandAllocator;
		ID3D12Resource* Resource;
		D3D12_CPU_DESCRIPTOR_HANDLE DescriptorHandle;
	};

	uintx_t BuffersCounts = -1;
	_FrameContext* FrameContext;
}

//=========================================================================================================================//

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT APIENTRY WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	if (ShowMenu) {
		if (ImGui_ImplWin32_WndProcHandler(hwnd, uMsg, wParam, lParam)) {
			return TRUE;
		}

#if ENABLE_MENU_MOUSE_ISOLATION
		switch (uMsg) {
		case WM_LBUTTONDOWN:
		case WM_LBUTTONUP:
		case WM_LBUTTONDBLCLK:
		case WM_RBUTTONDOWN:
		case WM_RBUTTONUP:
		case WM_RBUTTONDBLCLK:
		case WM_MBUTTONDOWN:
		case WM_MBUTTONUP:
		case WM_MBUTTONDBLCLK:
		case WM_MOUSEMOVE:
		case WM_MOUSEWHEEL:
		case WM_MOUSEHWHEEL:
			return TRUE;
		}
#endif
	}
	return CallWindowProc(Process::WndProc, hwnd, uMsg, wParam, lParam);
}

//=========================================================================================================================//
// Present Hook - 主渲染函数
//=========================================================================================================================//

HRESULT APIENTRY hkPresent(IDXGISwapChain3* pSwapChain, UINT SyncInterval, UINT Flags) {
	if (!ImGui_Initialised) {
		if (SUCCEEDED(pSwapChain->GetDevice(__uuidof(ID3D12Device), (void**)&DirectX12Interface::Device))) {
			ImGui::CreateContext();

			ImGuiIO& io = ImGui::GetIO(); (void)io;
			ImGui::GetIO().WantCaptureMouse || ImGui::GetIO().WantTextInput || ImGui::GetIO().WantCaptureKeyboard;
			io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
			io.IniFilename = nullptr; // 禁用 ini 文件

			// 加载中文字体
			io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\msyh.ttc", 22.0f, NULL, io.Fonts->GetGlyphRangesChineseFull());

			DXGI_SWAP_CHAIN_DESC Desc;
			pSwapChain->GetDesc(&Desc);
			Desc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
			Desc.OutputWindow = Process::Hwnd;
			Desc.Windowed = ((GetWindowLongPtr(Process::Hwnd, GWL_STYLE) & WS_POPUP) != 0) ? false : true;

			DirectX12Interface::BuffersCounts = Desc.BufferCount;
			DirectX12Interface::FrameContext = new DirectX12Interface::_FrameContext[DirectX12Interface::BuffersCounts];

			D3D12_DESCRIPTOR_HEAP_DESC DescriptorImGuiRender = {};
			DescriptorImGuiRender.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
			DescriptorImGuiRender.NumDescriptors = DirectX12Interface::BuffersCounts;
			DescriptorImGuiRender.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

			if (DirectX12Interface::Device->CreateDescriptorHeap(&DescriptorImGuiRender, IID_PPV_ARGS(&DirectX12Interface::DescriptorHeapImGuiRender)) != S_OK)
				return oPresent(pSwapChain, SyncInterval, Flags);

			for (size_t i = 0; i < DirectX12Interface::BuffersCounts; i++) {
				ID3D12CommandAllocator* allocator = nullptr;
				if (DirectX12Interface::Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)) != S_OK)
					return oPresent(pSwapChain, SyncInterval, Flags);
				DirectX12Interface::FrameContext[i].CommandAllocator = allocator;
			}

			if (DirectX12Interface::Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, DirectX12Interface::FrameContext[0].CommandAllocator, NULL, IID_PPV_ARGS(&DirectX12Interface::CommandList)) != S_OK ||
				DirectX12Interface::CommandList->Close() != S_OK)
				return oPresent(pSwapChain, SyncInterval, Flags);

			D3D12_DESCRIPTOR_HEAP_DESC DescriptorBackBuffers;
			DescriptorBackBuffers.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
			DescriptorBackBuffers.NumDescriptors = DirectX12Interface::BuffersCounts;
			DescriptorBackBuffers.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
			DescriptorBackBuffers.NodeMask = 1;

			if (DirectX12Interface::Device->CreateDescriptorHeap(&DescriptorBackBuffers, IID_PPV_ARGS(&DirectX12Interface::DescriptorHeapBackBuffers)) != S_OK)
				return oPresent(pSwapChain, SyncInterval, Flags);

			const auto RTVDescriptorSize = DirectX12Interface::Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
			D3D12_CPU_DESCRIPTOR_HANDLE RTVHandle = DirectX12Interface::DescriptorHeapBackBuffers->GetCPUDescriptorHandleForHeapStart();

			for (size_t i = 0; i < DirectX12Interface::BuffersCounts; i++) {
				ID3D12Resource* pBackBuffer = nullptr;
				DirectX12Interface::FrameContext[i].DescriptorHandle = RTVHandle;
				pSwapChain->GetBuffer(i, IID_PPV_ARGS(&pBackBuffer));
				DirectX12Interface::Device->CreateRenderTargetView(pBackBuffer, nullptr, RTVHandle);
				DirectX12Interface::FrameContext[i].Resource = pBackBuffer;
				RTVHandle.ptr += RTVDescriptorSize;
			}

			ImGui_ImplWin32_Init(Process::Hwnd);
			ImGui_ImplDX12_Init(DirectX12Interface::Device, DirectX12Interface::BuffersCounts, DXGI_FORMAT_R8G8B8A8_UNORM, DirectX12Interface::DescriptorHeapImGuiRender, DirectX12Interface::DescriptorHeapImGuiRender->GetCPUDescriptorHandleForHeapStart(), DirectX12Interface::DescriptorHeapImGuiRender->GetGPUDescriptorHandleForHeapStart());
			ImGui_ImplDX12_CreateDeviceObjects();
			ImGui::GetIO().ImeWindowHandle = Process::Hwnd;
			if (!Process::WndProc) {
				Process::WndProc = (WNDPROC)SetWindowLongPtr(Process::Hwnd, GWLP_WNDPROC, (__int3264)(LONG_PTR)WndProc);
			}
			
			// 使用模块化的UI初始化
			UI::Renderer::Initialize();
		}
		ImGui_Initialised = true;
	}

	if (DirectX12Interface::CommandQueue == nullptr)
		return oPresent(pSwapChain, SyncInterval, Flags);

	// 快捷键控制
	static bool key_f1_down = false, key_f2_down = false;
	if (GetAsyncKeyState(VK_F1) & 0x8000) {
		if (!key_f1_down) { Config::Features::ESP_Enabled = !Config::Features::ESP_Enabled; key_f1_down = true; }
	} else key_f1_down = false;
	
	if (GetAsyncKeyState(VK_F2) & 0x8000) {
		if (!key_f2_down) { Config::Features::SpeedBoost_Enabled = !Config::Features::SpeedBoost_Enabled; key_f2_down = true; }
	} else key_f2_down = false;
	
	if (GetAsyncKeyState(VK_INSERT) & 1) ShowMenu = !ShowMenu;

	// 开始 ImGui 帧
	ImGui_ImplDX12_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();
	
	ImGuiIO& io = ImGui::GetIO();
	io.MouseDrawCursor = ShowMenu;
	
	if (ShowMenu) {
		#if ENABLE_MENU_MOUSE_ISOLATION
		ImDrawList* bg = ImGui::GetBackgroundDrawList();
		ImVec2 displaySize = io.DisplaySize;
		bg->AddRectFilled(ImVec2(0.0f, 0.0f), ImVec2(displaySize.x, displaySize.y), IM_COL32(0, 0, 0, 120));
		#endif
	}
	
	// 使用模块化渲染
	if (ShowMenu && g_DataCollector) {
		UI::Renderer::RenderMenu(g_DataCollector);
	}
	
	if (!ShowMenu && g_DataCollector) {
		UI::Renderer::RenderESP(g_DataCollector);
	}
	
	ImGui::EndFrame();

	// D3D12 渲染提交
	DirectX12Interface::_FrameContext& CurrentFrameContext = DirectX12Interface::FrameContext[pSwapChain->GetCurrentBackBufferIndex()];
	CurrentFrameContext.CommandAllocator->Reset();

	D3D12_RESOURCE_BARRIER Barrier;
	Barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	Barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	Barrier.Transition.pResource = CurrentFrameContext.Resource;
	Barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	Barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
	Barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;

	DirectX12Interface::CommandList->Reset(CurrentFrameContext.CommandAllocator, nullptr);
	DirectX12Interface::CommandList->ResourceBarrier(1, &Barrier);
	DirectX12Interface::CommandList->OMSetRenderTargets(1, &CurrentFrameContext.DescriptorHandle, FALSE, nullptr);
	DirectX12Interface::CommandList->SetDescriptorHeaps(1, &DirectX12Interface::DescriptorHeapImGuiRender);

	ImGui::Render();
	ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), DirectX12Interface::CommandList);
	Barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
	Barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
	DirectX12Interface::CommandList->ResourceBarrier(1, &Barrier);
	DirectX12Interface::CommandList->Close();
	DirectX12Interface::CommandQueue->ExecuteCommandLists(1, reinterpret_cast<ID3D12CommandList* const*>(&DirectX12Interface::CommandList));
	return oPresent(pSwapChain, SyncInterval, Flags);
}

//=========================================================================================================================//

HRESULT APIENTRY hkResizeBuffers(IDXGISwapChain* pSwapChain, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags) {
	if (ImGui_Initialised) {
		ImGui_ImplDX12_Shutdown();
		ImGui_ImplWin32_Shutdown();
		ImGui::DestroyContext();
		ImGui_Initialised = false;
	}

	if (DirectX12Interface::FrameContext) {
		for (uintx_t i = 0; i < DirectX12Interface::BuffersCounts; ++i) {
			DirectX12Interface::_FrameContext& context = DirectX12Interface::FrameContext[i];
			if (context.Resource) {
				context.Resource->Release();
				context.Resource = nullptr;
			}
			if (context.CommandAllocator) {
				context.CommandAllocator->Release();
				context.CommandAllocator = nullptr;
			}
		}
		delete[] DirectX12Interface::FrameContext;
		DirectX12Interface::FrameContext = nullptr;
	}

	if (DirectX12Interface::DescriptorHeapBackBuffers) {
		DirectX12Interface::DescriptorHeapBackBuffers->Release();
		DirectX12Interface::DescriptorHeapBackBuffers = nullptr;
	}
	if (DirectX12Interface::DescriptorHeapImGuiRender) {
		DirectX12Interface::DescriptorHeapImGuiRender->Release();
		DirectX12Interface::DescriptorHeapImGuiRender = nullptr;
	}
	if (DirectX12Interface::CommandList) {
		DirectX12Interface::CommandList->Release();
		DirectX12Interface::CommandList = nullptr;
	}

	DirectX12Interface::BuffersCounts = 0;

	return oResizeBuffers(pSwapChain, BufferCount, Width, Height, NewFormat, SwapChainFlags);
}

//=========================================================================================================================//

void hkExecuteCommandLists(ID3D12CommandQueue* queue, UINT NumCommandLists, ID3D12CommandList* ppCommandLists) {
	if (!DirectX12Interface::CommandQueue)
		DirectX12Interface::CommandQueue = queue;

	oExecuteCommandLists(queue, NumCommandLists, ppCommandLists);
}

//=========================================================================================================================//

DWORD WINAPI MainThread(LPVOID lpParameter) {
	bool WindowFocus = false;
	while (WindowFocus == false) {
		DWORD ForegroundWindowProcessID;
		GetWindowThreadProcessId(GetForegroundWindow(), &ForegroundWindowProcessID);
		if (GetCurrentProcessId() == ForegroundWindowProcessID) {

			Process::ID = GetCurrentProcessId();
			Process::Handle = GetCurrentProcess();
			Process::Hwnd = GetForegroundWindow();

			RECT TempRect;
			GetWindowRect(Process::Hwnd, &TempRect);
			Process::WindowWidth = TempRect.right - TempRect.left;
			Process::WindowHeight = TempRect.bottom - TempRect.top;

			char TempTitle[MAX_PATH];
			GetWindowTextA(Process::Hwnd, TempTitle, sizeof(TempTitle));
			Process::Title = TempTitle;

			char TempClassName[MAX_PATH];
			GetClassNameA(Process::Hwnd, TempClassName, sizeof(TempClassName));
			Process::ClassName = TempClassName;

			char TempPath[MAX_PATH];
			GetModuleFileNameExA(Process::Handle, NULL, TempPath, sizeof(TempPath));
			Process::Path = TempPath;

			WindowFocus = true;
		}
		Sleep(50);
	}
	
	// 创建并启动数据收集器
	g_DataCollector = new GameData::DataCollector();
	g_DataCollector->StartCollection();
	
	// 初始化 Hook 系统
	bool InitHook = false;
	while (InitHook == false) {
		if (Hook::HookManager::Instance().Initialize()) {
			// 安装 Hook
			Hook::HookManager::Instance().InstallHook(54, (void**)&oExecuteCommandLists, hkExecuteCommandLists);
			Hook::HookManager::Instance().InstallHook(140, (void**)&oPresent, hkPresent);
			Hook::HookManager::Instance().InstallHook(145, (void**)&oResizeBuffers, hkResizeBuffers);
			InitHook = true;
		}
		else {
			Sleep(100);
		}
	}
	return 0;
}

//=========================================================================================================================//

BOOL APIENTRY DllMain(HMODULE hModule, DWORD dwReason, LPVOID lpReserved) {
	switch (dwReason) {
	case DLL_PROCESS_ATTACH:
		DisableThreadLibraryCalls(hModule);
		Process::Module = hModule;
		GetModuleFileNameA(hModule, dlldir, 512);
		for (size_t i = strlen(dlldir); i > 0; i--) { if (dlldir[i] == '\\') { dlldir[i + 1] = 0; break; } }
		CreateThread(0, 0, MainThread, 0, 0, 0);
		break;
	case DLL_PROCESS_DETACH:
		if (g_DataCollector) {
			g_DataCollector->StopCollection();
			delete g_DataCollector;
			g_DataCollector = nullptr;
		}
		if (ImGui_Initialised) {
			ImGui_ImplDX12_Shutdown();
			ImGui_ImplWin32_Shutdown();
			ImGui::DestroyContext();
			ImGui_Initialised = false;
		}
		if (Process::Hwnd && Process::WndProc) {
			SetWindowLongPtr(Process::Hwnd, GWLP_WNDPROC, (__int3264)(LONG_PTR)Process::WndProc);
			Process::WndProc = nullptr;
		}
		if (DirectX12Interface::FrameContext) {
			for (uintx_t i = 0; i < DirectX12Interface::BuffersCounts; ++i) {
				DirectX12Interface::_FrameContext& context = DirectX12Interface::FrameContext[i];
				if (context.Resource) {
					context.Resource->Release();
					context.Resource = nullptr;
				}
				if (context.CommandAllocator) {
					context.CommandAllocator->Release();
					context.CommandAllocator = nullptr;
				}
			}
			delete[] DirectX12Interface::FrameContext;
			DirectX12Interface::FrameContext = nullptr;
		}
		if (DirectX12Interface::DescriptorHeapBackBuffers) {
			DirectX12Interface::DescriptorHeapBackBuffers->Release();
			DirectX12Interface::DescriptorHeapBackBuffers = nullptr;
		}
		if (DirectX12Interface::DescriptorHeapImGuiRender) {
			DirectX12Interface::DescriptorHeapImGuiRender->Release();
			DirectX12Interface::DescriptorHeapImGuiRender = nullptr;
		}
		if (DirectX12Interface::CommandList) {
			DirectX12Interface::CommandList->Release();
			DirectX12Interface::CommandList = nullptr;
		}
		Hook::HookManager::Instance().UninstallAll();
		break;
	case DLL_THREAD_ATTACH:
		break;
	case DLL_THREAD_DETACH:
		break;
	default:
		break;
	}
	return TRUE;
}
