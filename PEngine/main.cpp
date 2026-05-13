// ==============================
// ライブラリ
// ==============================

#include <filesystem>
#include <fstream>
#include <chrono>

#include <windows.h>
#include <string>

#pragma warning(push)
#include <format>

#pragma warning(disable:4023)
#include <cstdint>

// DirectX12
#include <d3d12.h>
#include <dxgi1_6.h>
#include <cassert>
#include <dxgidebug.h>

// ダンプ出力
#include <dbghelp.h>
#include <strsafe.h>

#pragma comment(lib, "dbghelp.lib")
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dxguid.lib")

#pragma warning(pop)


// ==============================
// ログ関連
// ==============================

void Log(const std::string& message) {
	OutputDebugStringA(message.c_str());
}

void Log(std::ostream& os, const std::string& message) {
	os << message << std::endl;
	OutputDebugStringA(message.c_str());
}


// ==============================
// 文字列変換
// ==============================

// UTF-8 → UTF-16
std::wstring ConvertString(const std::string& str) {

	if (str.empty()) {
		return {};
	}

	int sizeNeeded = MultiByteToWideChar(
		CP_UTF8,
		0,
		str.data(),
		static_cast<int>(str.size()),
		nullptr,
		0
	);

	std::wstring result(sizeNeeded, 0);

	MultiByteToWideChar(
		CP_UTF8,
		0,
		str.data(),
		static_cast<int>(str.size()),
		result.data(),
		sizeNeeded
	);

	return result;
}

// UTF-16 → UTF-8
std::string ConvertString(const std::wstring& str) {

	if (str.empty()) {
		return {};
	}

	int sizeNeeded = WideCharToMultiByte(
		CP_UTF8,
		0,
		str.data(),
		static_cast<int>(str.size()),
		nullptr,
		0,
		nullptr,
		nullptr
	);

	std::string result(sizeNeeded, 0);

	WideCharToMultiByte(
		CP_UTF8,
		0,
		str.data(),
		static_cast<int>(str.size()),
		result.data(),
		sizeNeeded,
		nullptr,
		nullptr
	);

	return result;
}


// ==============================
// ウィンドウプロシージャ
// ==============================

LRESULT CALLBACK WindowProc(
	HWND hwnd,
	UINT msg,
	WPARAM wparam,
	LPARAM lparam
) {

	switch (msg) {

	case WM_DESTROY:

		PostQuitMessage(0);
		return 0;
	}

	return DefWindowProc(hwnd, msg, wparam, lparam);
}


// ==============================
// クラッシュダンプ出力
// ==============================

static LONG WINAPI ExportDump(EXCEPTION_POINTERS* exception) {

	SYSTEMTIME time;
	GetLocalTime(&time);

	CreateDirectory(L"Dumps", nullptr);

	wchar_t filePath[MAX_PATH] = {};

	StringCchPrintfW(
		filePath,
		MAX_PATH,
		L"Dumps/%04d-%02d%02d-%02d%02d.dmp",
		time.wYear,
		time.wMonth,
		time.wDay,
		time.wHour,
		time.wMinute
	);

	HANDLE fileHandle = CreateFile(
		filePath,
		GENERIC_WRITE,
		FILE_SHARE_WRITE,
		nullptr,
		CREATE_ALWAYS,
		FILE_ATTRIBUTE_NORMAL,
		nullptr
	);

	MINIDUMP_EXCEPTION_INFORMATION dumpInfo{};
	dumpInfo.ThreadId = GetCurrentThreadId();
	dumpInfo.ExceptionPointers = exception;
	dumpInfo.ClientPointers = TRUE;

	MiniDumpWriteDump(
		GetCurrentProcess(),
		GetCurrentProcessId(),
		fileHandle,
		MiniDumpNormal,
		&dumpInfo,
		nullptr,
		nullptr
	);

	CloseHandle(fileHandle);

	return EXCEPTION_EXECUTE_HANDLER;
}


// ==============================
// 定数
// ==============================

const int kClientWidth = 1280;
const int kClientHeight = 720;


// ==============================
// エントリーポイント
// ==============================

int WINAPI WinMain(
	HINSTANCE hInstance,
	HINSTANCE hPrevInstance,
	LPSTR lpCmdLine,
	int nCmdShow
) {

	// ==============================
	// クラッシュハンドラ登録
	// ==============================

	SetUnhandledExceptionFilter(ExportDump);


	// ==============================
	// ウィンドウ作成
	// ==============================

	WNDCLASS wc{};

	wc.lpfnWndProc = WindowProc;
	wc.lpszClassName = L"CG2WindowClass";
	wc.hInstance = GetModuleHandle(nullptr);
	wc.hCursor = LoadCursor(nullptr, IDC_ARROW);

	RegisterClass(&wc);

	RECT wrc = { 0,0,kClientWidth,kClientHeight };

	AdjustWindowRect(
		&wrc,
		WS_OVERLAPPEDWINDOW,
		FALSE
	);

	HWND hwnd = CreateWindow(
		wc.lpszClassName,
		L"CG2",
		WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		wrc.right - wrc.left,
		wrc.bottom - wrc.top,
		nullptr,
		nullptr,
		wc.hInstance,
		nullptr
	);

	assert(hwnd != nullptr);

	ShowWindow(hwnd, SW_SHOW);


	// ==============================
	// DirectX Debug Layer
	// ==============================

#ifdef _DEBUG

	ID3D12Debug1* debugController = nullptr;

	if (SUCCEEDED(
		D3D12GetDebugInterface(
			IID_PPV_ARGS(&debugController)
		)
	)) {

		debugController->EnableDebugLayer();

		debugController->SetEnableGPUBasedValidation(TRUE);
	}

#endif


	// ==============================
	// ログ初期化
	// ==============================

	std::filesystem::create_directory("logs");

	auto now = std::chrono::system_clock::now();

	auto nowSec =
		std::chrono::time_point_cast<std::chrono::seconds>(now);

	std::chrono::zoned_time localTime{
		std::chrono::current_zone(),
		nowSec
	};

	std::string logFilePath =
		"logs/" +
		std::format("{:%Y%m%d_%H%M%S}", localTime) +
		".log";

	std::ofstream logStream(logFilePath);

	Log(logStream, "Application Start");


	// ==============================
	// DXGIファクトリ生成
	// ==============================

	IDXGIFactory7* factory = nullptr;

	HRESULT hr = CreateDXGIFactory(
		IID_PPV_ARGS(&factory)
	);

	assert(SUCCEEDED(hr));


	// ==============================
	// GPUアダプタ取得
	// ==============================

	IDXGIAdapter4* adapter = nullptr;

	for (
		UINT i = 0;
		factory->EnumAdapterByGpuPreference(
			i,
			DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
			IID_PPV_ARGS(&adapter)
		) != DXGI_ERROR_NOT_FOUND;
		++i
		) {

		DXGI_ADAPTER_DESC3 desc{};

		adapter->GetDesc3(&desc);

		if (!(desc.Flags & DXGI_ADAPTER_FLAG3_SOFTWARE)) {

			Log(
				logStream,
				ConvertString(
					std::format(
						L"Use Adapter : {}",
						desc.Description
					)
				)
			);

			break;
		}

		adapter = nullptr;
	}

	assert(adapter != nullptr);


	// ==============================
	// D3D12デバイス生成
	// ==============================

	ID3D12Device* device = nullptr;

	D3D_FEATURE_LEVEL featureLevels[] = {
		D3D_FEATURE_LEVEL_12_2,
		D3D_FEATURE_LEVEL_12_1,
		D3D_FEATURE_LEVEL_12_0
	};

	const char* featureLevelStrings[] = {
		"12.2",
		"12.1",
		"12.0"
	};

	for (size_t i = 0; i < _countof(featureLevels); ++i) {

		hr = D3D12CreateDevice(
			adapter,
			featureLevels[i],
			IID_PPV_ARGS(&device)
		);

		if (SUCCEEDED(hr)) {

			Log(
				logStream,
				std::format(
					"Feature Level {} supported",
					featureLevelStrings[i]
				)
			);

			break;
		}
	}

	assert(device != nullptr);


	// ==============================
	// InfoQueue設定
	// ==============================

#ifdef _DEBUG

	ID3D12InfoQueue* infoQueue = nullptr;

	if (SUCCEEDED(
		device->QueryInterface(
			IID_PPV_ARGS(&infoQueue)
		)
	)) {

		infoQueue->SetBreakOnSeverity(
			D3D12_MESSAGE_SEVERITY_CORRUPTION,
			TRUE
		);

		infoQueue->SetBreakOnSeverity(
			D3D12_MESSAGE_SEVERITY_ERROR,
			TRUE
		);

		//infoQueue->SetBreakOnSeverity(
		//	D3D12_MESSAGE_SEVERITY_WARNING,
		//	TRUE
		//);

		D3D12_MESSAGE_ID denyIds[] = {
			D3D12_MESSAGE_ID_RESOURCE_BARRIER_MISMATCHING_COMMAND_LIST_TYPE,
		};

		D3D12_MESSAGE_SEVERITY severities[] = {
			D3D12_MESSAGE_SEVERITY_INFO
		};

		D3D12_INFO_QUEUE_FILTER filter{};

		filter.DenyList.NumIDs = _countof(denyIds);
		filter.DenyList.pIDList = denyIds;

		filter.DenyList.NumSeverities = _countof(severities);
		filter.DenyList.pSeverityList = severities;

		infoQueue->PushStorageFilter(&filter);

		infoQueue->Release();
		infoQueue = nullptr;
	}

#endif


	// ==============================
	// コマンドキュー生成
	// ==============================

	ID3D12CommandQueue* commandQueue = nullptr;

	D3D12_COMMAND_QUEUE_DESC queueDesc{};

	hr = device->CreateCommandQueue(
		&queueDesc,
		IID_PPV_ARGS(&commandQueue)
	);

	assert(SUCCEEDED(hr));


	// ==============================
	// コマンドアロケータ生成
	// ==============================

	ID3D12CommandAllocator* commandAllocator = nullptr;

	hr = device->CreateCommandAllocator(
		D3D12_COMMAND_LIST_TYPE_DIRECT,
		IID_PPV_ARGS(&commandAllocator)
	);

	assert(SUCCEEDED(hr));


	// ==============================
	// コマンドリスト生成
	// ==============================

	ID3D12GraphicsCommandList* commandList = nullptr;

	hr = device->CreateCommandList(
		0,
		D3D12_COMMAND_LIST_TYPE_DIRECT,
		commandAllocator,
		nullptr,
		IID_PPV_ARGS(&commandList)
	);

	assert(SUCCEEDED(hr));


	// 最初に閉じておく
	hr = commandList->Close();
	assert(SUCCEEDED(hr));


	// ==============================
	// スワップチェーン生成
	// ==============================

	IDXGISwapChain4* swapChain = nullptr;

	DXGI_SWAP_CHAIN_DESC1 swapChainDesc{};

	swapChainDesc.Width = kClientWidth;
	swapChainDesc.Height = kClientHeight;
	swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	swapChainDesc.SampleDesc.Count = 1;
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.BufferCount = 2;
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

	hr = factory->CreateSwapChainForHwnd(
		commandQueue,
		hwnd,
		&swapChainDesc,
		nullptr,
		nullptr,
		reinterpret_cast<IDXGISwapChain1**>(&swapChain)
	);

	assert(SUCCEEDED(hr));


	// ==============================
	// RTVヒープ生成
	// ==============================

	ID3D12DescriptorHeap* rtvHeap = nullptr;

	D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc{};

	rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	rtvHeapDesc.NumDescriptors = 2;
	rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

	hr = device->CreateDescriptorHeap(
		&rtvHeapDesc,
		IID_PPV_ARGS(&rtvHeap)
	);

	assert(SUCCEEDED(hr));


	// ==============================
	// バックバッファ取得
	// ==============================

	ID3D12Resource* backBuffers[2] = { nullptr };

	for (UINT i = 0; i < 2; ++i) {

		hr = swapChain->GetBuffer(
			i,
			IID_PPV_ARGS(&backBuffers[i])
		);

		assert(SUCCEEDED(hr));
	}


	// ==============================
	// RTV作成
	// ==============================

	D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};

	rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	rtvDesc.ViewDimension =
		D3D12_RTV_DIMENSION_TEXTURE2D;

	UINT rtvDescriptorSize =
		device->GetDescriptorHandleIncrementSize(
			D3D12_DESCRIPTOR_HEAP_TYPE_RTV
		);

	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandles[2];

	rtvHandles[0] =
		rtvHeap->GetCPUDescriptorHandleForHeapStart();

	rtvHandles[1].ptr =
		rtvHandles[0].ptr + rtvDescriptorSize;

	for (UINT i = 0; i < 2; ++i) {

		device->CreateRenderTargetView(
			backBuffers[i],
			&rtvDesc,
			rtvHandles[i]
		);
	}


	// ==============================
	// Fence生成
	// ==============================

	ID3D12Fence* fence = nullptr;
	uint64_t fenceValue = 0;

	hr = device->CreateFence(
		fenceValue,
		D3D12_FENCE_FLAG_NONE,
		IID_PPV_ARGS(&fence)
	);

	assert(SUCCEEDED(hr));

	HANDLE fenceEvent =
		CreateEvent(nullptr, FALSE, FALSE, nullptr);

	assert(fenceEvent != nullptr);


	// ==============================
	// メインループ
	// ==============================

	MSG msg{};

	while (msg.message != WM_QUIT) {

		if (PeekMessage(
			&msg,
			nullptr,
			0,
			0,
			PM_REMOVE
		)) {

			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
		else {

			// ==========================================
			// GPU待機
			// ==========================================

			if (fence->GetCompletedValue() < fenceValue) {

				hr = fence->SetEventOnCompletion(
					fenceValue,
					fenceEvent
				);

				assert(SUCCEEDED(hr));

				WaitForSingleObject(
					fenceEvent,
					INFINITE
				);
			}

			// ==========================================
			// Reset
			// ==========================================

			hr = commandAllocator->Reset();
			assert(SUCCEEDED(hr));

			hr = commandList->Reset(
				commandAllocator,
				nullptr
			);

			assert(SUCCEEDED(hr));

			// ==========================================
			// バックバッファ取得
			// ==========================================

			UINT backBufferIndex =
				swapChain->GetCurrentBackBufferIndex();

			// ==========================================
			// PRESENT → RENDER_TARGET
			// ==========================================

			D3D12_RESOURCE_BARRIER barrier{};

			barrier.Type =
				D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;

			barrier.Flags =
				D3D12_RESOURCE_BARRIER_FLAG_NONE;

			barrier.Transition.pResource =
				backBuffers[backBufferIndex];

			barrier.Transition.StateBefore =
				D3D12_RESOURCE_STATE_PRESENT;

			barrier.Transition.StateAfter =
				D3D12_RESOURCE_STATE_RENDER_TARGET;

			barrier.Transition.Subresource =
				D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

			commandList->ResourceBarrier(1, &barrier);

			// ==========================================
			// 描画先設定
			// ==========================================

			commandList->OMSetRenderTargets(
				1,
				&rtvHandles[backBufferIndex],
				false,
				nullptr
			);

			// ==========================================
			// 画面クリア
			// ==========================================

			float clearColor[] = {
				0.1f,
				0.25f,
				0.5f,
				1.0f
			};

			commandList->ClearRenderTargetView(
				rtvHandles[backBufferIndex],
				clearColor,
				0,
				nullptr
			);

			// ==========================================
			// RENDER_TARGET → PRESENT
			// ==========================================

			barrier.Transition.StateBefore =
				D3D12_RESOURCE_STATE_RENDER_TARGET;

			barrier.Transition.StateAfter =
				D3D12_RESOURCE_STATE_PRESENT;

			commandList->ResourceBarrier(1, &barrier);

			// ==========================================
			// コマンドリスト終了
			// ==========================================

			hr = commandList->Close();
			assert(SUCCEEDED(hr));

			// ==========================================
			// コマンド実行
			// ==========================================

			ID3D12CommandList* commandLists[] = {
				commandList
			};

			commandQueue->ExecuteCommandLists(
				1,
				commandLists
			);

			// ==========================================
			// 画面表示
			// ==========================================

			hr = swapChain->Present(1, 0);
			assert(SUCCEEDED(hr));

			// ==========================================
			// Fenceシグナル送信
			// ==========================================

			fenceValue++;

			hr = commandQueue->Signal(
				fence,
				fenceValue
			);

			assert(SUCCEEDED(hr));
		}
	}


	// ==============================
	// GPU終了待機
	// ==============================

	fenceValue++;

	hr = commandQueue->Signal(
		fence,
		fenceValue
	);

	assert(SUCCEEDED(hr));

	if (fence->GetCompletedValue() < fenceValue) {

		hr = fence->SetEventOnCompletion(
			fenceValue,
			fenceEvent
		);

		assert(SUCCEEDED(hr));

		WaitForSingleObject(
			fenceEvent,
			INFINITE
		);
	}


	// ==============================
	// 終了処理
	// ==============================

	CloseHandle(fenceEvent);

	Log(logStream, "Application End");

	if (fence) {
		fence->Release();
		fence = nullptr;
	}

	if (rtvHeap) {
		rtvHeap->Release();
		rtvHeap = nullptr;
	}

	for (int i = 0; i < 2; ++i) {

		if (backBuffers[i]) {
			backBuffers[i]->Release();
			backBuffers[i] = nullptr;
		}
	}

	if (swapChain) {
		swapChain->Release();
		swapChain = nullptr;
	}

	if (commandList) {
		commandList->Release();
		commandList = nullptr;
	}

	if (commandAllocator) {
		commandAllocator->Release();
		commandAllocator = nullptr;
	}

	if (commandQueue) {
		commandQueue->Release();
		commandQueue = nullptr;
	}

	if (device) {
		device->Release();
		device = nullptr;
	}

	if (adapter) {
		adapter->Release();
		adapter = nullptr;
	}

	if (factory) {
		factory->Release();
		factory = nullptr;
	}

#ifdef _DEBUG

	if (debugController) {
		debugController->Release();
		debugController = nullptr;
	}

	//リソースリークチェック
	IDXGIDebug1* debug;

	if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&debug)))) {
		debug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_ALL);
		debug->ReportLiveObjects(DXGI_DEBUG_APP, DXGI_DEBUG_RLO_ALL);
		debug->ReportLiveObjects(DXGI_DEBUG_D3D12, DXGI_DEBUG_RLO_ALL);
		debug->Release();
	}

#endif

	DestroyWindow(hwnd);

	return 0;
}