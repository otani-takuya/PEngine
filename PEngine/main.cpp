// ==============================
// ライブラリ
// ==============================

#include <filesystem>   // フォルダ作成など
#include <fstream>      // ファイル入出力
#include <chrono>       // 時刻取得

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

// ダンプ出力用
#include <dbghelp.h>
#include <strsafe.h>

#pragma comment(lib, "dbghelp.lib")
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")

#pragma warning(pop)



// ==============================
// ログ関連
// ==============================

// デバッグ出力
void Log(const std::string& message) {
    OutputDebugStringA(message.c_str());
}

// ファイル + デバッグ出力
void Log(std::ostream& os, const std::string& message) {
    os << message << std::endl;
    OutputDebugStringA(message.c_str());
}


// ==============================
// 文字列変換
// ==============================

// UTF-8 → UTF-16
std::wstring ConvertString(const std::string& str) {
    if (str.empty()) return {};

    int sizeNeeded = MultiByteToWideChar(CP_UTF8, 0, str.data(), (int)str.size(), NULL, 0);
    std::wstring result(sizeNeeded, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.data(), (int)str.size(), result.data(), sizeNeeded);

    return result;
}

// UTF-16 → UTF-8
std::string ConvertString(const std::wstring& str) {
    if (str.empty()) return {};

    int sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, str.data(), (int)str.size(), NULL, 0, NULL, NULL);
    std::string result(sizeNeeded, 0);
    WideCharToMultiByte(CP_UTF8, 0, str.data(), (int)str.size(), result.data(), sizeNeeded, NULL, NULL);

    return result;
}


// ==============================
// ウィンドウプロシージャ
// ==============================

LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {

    switch (msg) {

        // ウィンドウが閉じられたとき
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProc(hwnd, msg, wparam, lparam);
}


// ==============================
// クラッシュ時ダンプ出力
// ==============================

static LONG WINAPI ExportDump(EXCEPTION_POINTERS* exception) {

    // 現在時刻取得（ファイル名用）
    SYSTEMTIME time;
    GetLocalTime(&time);

    // Dumpsフォルダ作成（なければ）
    CreateDirectory(L"Dumps", nullptr);

    // ファイルパス生成（例: Dumps/2026-04-22-1530.dmp）
    wchar_t filePath[MAX_PATH] = { 0 };
    StringCchPrintfW(
        filePath, MAX_PATH,
        L"Dumps/%04d-%02d%02d-%02d%02d.dmp",
        time.wYear, time.wMonth, time.wDay,
        time.wHour, time.wMinute
    );

    // ダンプファイル作成
    HANDLE fileHandle = CreateFile(
        filePath,
        GENERIC_WRITE,
        FILE_SHARE_WRITE,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );

    // プロセス・スレッド情報
    MINIDUMP_EXCEPTION_INFORMATION info{};
    info.ThreadId = GetCurrentThreadId();
    info.ExceptionPointers = exception;
    info.ClientPointers = TRUE;

    // ダンプ出力
    MiniDumpWriteDump(
        GetCurrentProcess(),
        GetCurrentProcessId(),
        fileHandle,
        MiniDumpNormal,
        &info,
        nullptr,
        nullptr
    );

    return EXCEPTION_EXECUTE_HANDLER;
}

int kClientWidth = 1280;
int kClientHeight = 720;


// ==============================
// エントリーポイント
// ==============================

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {

    // ------------------------------
    // クラッシュハンドラ登録（最優先）
    // ------------------------------
    SetUnhandledExceptionFilter(ExportDump);


    // ------------------------------
    // ウィンドウ作成
    // ------------------------------
    WNDCLASS wc{};
    wc.lpfnWndProc = WindowProc;
    wc.lpszClassName = L"CG2WindowClass";
    wc.hInstance = GetModuleHandle(nullptr);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClass(&wc);

    RECT wrc = { 0, 0, kClientWidth, kClientHeight };
    AdjustWindowRect(&wrc, WS_OVERLAPPEDWINDOW, FALSE);

    HWND hwnd = CreateWindow(
        wc.lpszClassName,
        L"CG2",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        wrc.right - wrc.left,
        wrc.bottom - wrc.top,
        nullptr, nullptr, wc.hInstance, nullptr
    );

    ShowWindow(hwnd, SW_SHOW);


    // ------------------------------
    // ログ初期化
    // ------------------------------
    std::filesystem::create_directory("logs");

    auto now = std::chrono::system_clock::now();
    auto nowSec = std::chrono::time_point_cast<std::chrono::seconds>(now);
    std::chrono::zoned_time localTime{ std::chrono::current_zone(), nowSec };

    std::string logFilePath =
        "logs/" + std::format("{:%Y%m%d_%H%M%S}", localTime) + ".log";

    std::ofstream logStream(logFilePath);
    Log(logStream, "Application Start");


    // ------------------------------
    // DX初期化
    // ------------------------------
    IDXGIFactory7* factory = nullptr;
    HRESULT hr = CreateDXGIFactory(IID_PPV_ARGS(&factory));
    assert(SUCCEEDED(hr));

    IDXGIAdapter4* adapter = nullptr;

    // 高性能GPUを探す
    for (UINT i = 0;
        factory->EnumAdapterByGpuPreference(
            i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
            IID_PPV_ARGS(&adapter)) != DXGI_ERROR_NOT_FOUND;
        ++i) {

        DXGI_ADAPTER_DESC3 desc{};
        adapter->GetDesc3(&desc);

        if (!(desc.Flags & DXGI_ADAPTER_FLAG3_SOFTWARE)) {
            Log(logStream, ConvertString(std::format(L"Use Adapter: {}", desc.Description)));
            break;
        }

        adapter = nullptr;
    }

    assert(adapter != nullptr);


    // ------------------------------
    // デバイス生成
    // ------------------------------
    ID3D12Device* device = nullptr;

    D3D_FEATURE_LEVEL levels[] = {
        D3D_FEATURE_LEVEL_12_2,
        D3D_FEATURE_LEVEL_12_1,
        D3D_FEATURE_LEVEL_12_0
    };

    const char* levelStr[] = { "12.2", "12.1", "12.0" };

    for (size_t i = 0; i < _countof(levels); ++i) {

        hr = D3D12CreateDevice(adapter, levels[i], IID_PPV_ARGS(&device));

        if (SUCCEEDED(hr)) {
            Log(logStream, std::format("Feature Level {} supported", levelStr[i]));
            break;
        }
    }


    //コマンドキューを生成する
    ID3D12CommandQueue* commandQueue = nullptr;
    D3D12_COMMAND_QUEUE_DESC commandQueueDesc{};
    hr = device->CreateCommandQueue(&commandQueueDesc, IID_PPV_ARGS(&commandQueue));
    //コマンドキューの生成がうまくいかなかったので起動できない
    assert(SUCCEEDED(hr));


	// コマンドアロケータを生成する
	ID3D12CommandAllocator* commandAllocator = nullptr;
	hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator));
	// コマンドアロケータの生成がうまくいかなかったので起動できない
	assert(SUCCEEDED(hr));
    

	// コマンドリストを生成する
	ID3D12GraphicsCommandList* commandList = nullptr;
	hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator, nullptr, IID_PPV_ARGS(&commandList));
	// コマンドリストの生成がうまくいかなかったので起動できない
	assert(SUCCEEDED(hr));

    //スワップチェーンを生成する
	IDXGISwapChain4* swapChain = nullptr;
	DXGI_SWAP_CHAIN_DESC1 swapChainDesc{};
    swapChainDesc.Width = kClientWidth;     //画面幅
	swapChainDesc.Height = kClientHeight;           //画面の高さ
	swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;      //色の形式
	swapChainDesc.SampleDesc.Count = 1;       //マルチサンプルしない
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; //描画ターゲットとして利用する
    swapChainDesc.BufferCount = 2; //ダブルバッファ
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD; //モニタに移したらバッファの内容は破棄
    //コマンドキュー、ウィンドウハンドル、設定を渡して生成する
    hr = factory->CreateSwapChainForHwnd(commandQueue, hwnd, &swapChainDesc, nullptr, nullptr, reinterpret_cast<IDXGISwapChain1**>(&swapChain));
    assert(SUCCEEDED(hr));


    //ディスクリプタヒープの生成
	ID3D12DescriptorHeap* rtvDescriptorHeap = nullptr;
	D3D12_DESCRIPTOR_HEAP_DESC rtvDescriptorHeapDesc{};
	rtvDescriptorHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV; //レンダーターゲットビュー用
    rtvDescriptorHeapDesc.NumDescriptors = 2; //ダブルバッファ用に2つ。多くてもいい
	hr = device->CreateDescriptorHeap(&rtvDescriptorHeapDesc, IID_PPV_ARGS(&rtvDescriptorHeap));
    //ディスクリプタヒープが作れなかったので起動できない
	assert(SUCCEEDED(hr));

    //SwapChainからResourceを取得する
	ID3D12Resource* renderTargets[2] = { nullptr };
	hr = swapChain->GetBuffer(0, IID_PPV_ARGS(&renderTargets[0]));
    //うまく取得できなければ起動できない
    assert(SUCCEEDED(hr));
	hr = swapChain->GetBuffer(1, IID_PPV_ARGS(&renderTargets[1]));
    assert(SUCCEEDED(hr));

	// RTVを作成する
	D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
	rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB; //出力結果をSRGBに変換して書き込む
	rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D; //2Dテクスチャとして扱う
    //ディスクリプタの先頭を取得する
	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = rtvDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
    //RTVを2つ作るのでディスクリプタを2つ用意
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandles[2];
	//1つ目のRTVを作成
	rtvHandles[0] = rtvHandle; 
    device->CreateRenderTargetView(renderTargets[0], &rtvDesc, rtvHandles[0]);
    //2つ目のRTVを作成
    rtvHandles[1].ptr = rtvHandles[0].ptr + device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    device->CreateRenderTargetView(renderTargets[1], &rtvDesc, rtvHandles[1]);

    //これから書き込むバックバッファのインデックスを取得
	UINT backBufferIndex = swapChain->GetCurrentBackBufferIndex();
    //描画先のRTVを設定する
	commandList->OMSetRenderTargets(1, &rtvHandles[backBufferIndex], false, nullptr);
	//指定した色で画面全体をクリアする
    float clearColor[] = { 0.2f, 0.4f, 0.6f, 1.0f }; //青っぽい色。RGBAの順
	commandList->ClearRenderTargetView(rtvHandles[backBufferIndex], clearColor, 0, nullptr);
	//コマンドリストの内容を確定させる。すべてのコマンドを積んでからCloseすること
	hr = commandList->Close();
	assert(SUCCEEDED(hr));

	//GPUにコマンドリストを実行させる
	ID3D12CommandList* commandLists[] = { commandList };
    commandQueue->ExecuteCommandLists(1, commandLists);
	//GPUとOSに画面の交換を行うように通知する
	swapChain->Present(1, 0); //垂直同期あり
	//次のフレーム用のコマンドリストを準備
	hr = commandAllocator->Reset();
	assert(SUCCEEDED(hr));
	hr = commandList->Reset(commandAllocator, nullptr);
	assert(SUCCEEDED(hr));


    // ------------------------------
    // メインループ
    // ------------------------------
    MSG msg{};

    while (msg.message != WM_QUIT) {

        if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        else {
            // ゲーム処理
        }
    }


    // ------------------------------
    // 終了処理
    // ------------------------------
    Log(logStream, "Application End");

    return 0;
}