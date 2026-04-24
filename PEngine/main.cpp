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

// デバッグ出力のみ
void Log(const std::string& message) {
    OutputDebugStringA(message.c_str());
}

// ファイル + デバッグ出力
void Log(std::ostream& os, const std::string& message) {
    os << message << std::endl;              // ファイルに書き込み
    OutputDebugStringA(message.c_str());     // 出力ウィンドウにも表示
}


// ==============================
// 文字列変換
// ==============================

// UTF-8 → UTF-16
std::wstring ConvertString(const std::string& str) {
    if (str.empty()) return {};

    // 必要なバッファサイズ取得
    int sizeNeeded = MultiByteToWideChar(CP_UTF8, 0, str.data(), (int)str.size(), NULL, 0);

    std::wstring result(sizeNeeded, 0);

    // 実際に変換
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

    case WM_DESTROY:
        // ウィンドウが閉じられたらアプリ終了
        PostQuitMessage(0);
        return 0;
    }

    // デフォルト処理
    return DefWindowProc(hwnd, msg, wparam, lparam);
}


// ==============================
// クラッシュ時ダンプ出力
// ==============================

static LONG WINAPI ExportDump(EXCEPTION_POINTERS* exception) {

    // 現在時刻取得（ファイル名用）
    SYSTEMTIME time;
    GetLocalTime(&time);

    // Dumpsフォルダ作成
    CreateDirectory(L"Dumps", nullptr);

    // ファイル名作成
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

    // ダンプ情報設定
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


// ==============================
// 定数
// ==============================

int kClientWidth = 1280;
int kClientHeight = 720;


// ==============================
// エントリーポイント
// ==============================

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {

    // ------------------------------
    // クラッシュハンドラ登録
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

    RECT wrc = { 0,0,kClientWidth,kClientHeight };
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
    // DXGIファクトリ生成
    // ------------------------------
    IDXGIFactory7* factory = nullptr;
    HRESULT hr = CreateDXGIFactory(IID_PPV_ARGS(&factory));
    assert(SUCCEEDED(hr));


    // ------------------------------
    // GPUアダプタ取得
    // ------------------------------
    IDXGIAdapter4* adapter = nullptr;

    for (UINT i = 0;
        factory->EnumAdapterByGpuPreference(
            i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
            IID_PPV_ARGS(&adapter)) != DXGI_ERROR_NOT_FOUND;
        ++i) {

        DXGI_ADAPTER_DESC3 desc{};
        adapter->GetDesc3(&desc);

        // ソフトウェアGPUを除外
        if (!(desc.Flags & DXGI_ADAPTER_FLAG3_SOFTWARE)) {

            Log(logStream, ConvertString(std::format(L"Use Adapter: {}", desc.Description)));
            break;
        }

        adapter = nullptr;
    }

    assert(adapter != nullptr);


    // ------------------------------
    // D3D12デバイス生成
    // ------------------------------
    ID3D12Device* device = nullptr;

    D3D_FEATURE_LEVEL levels[] = {
        D3D_FEATURE_LEVEL_12_2,
        D3D_FEATURE_LEVEL_12_1,
        D3D_FEATURE_LEVEL_12_0
    };

    const char* levelStr[] = { "12.2","12.1","12.0" };

    for (size_t i = 0; i < _countof(levels); ++i) {

        hr = D3D12CreateDevice(adapter, levels[i], IID_PPV_ARGS(&device));

        if (SUCCEEDED(hr)) {
            Log(logStream, std::format("Feature Level {} supported", levelStr[i]));
            break;
        }
    }


    // ------------------------------
    // コマンド系オブジェクト生成
    // ------------------------------

    // GPUに命令を送るキュー
    ID3D12CommandQueue* commandQueue = nullptr;
    D3D12_COMMAND_QUEUE_DESC queueDesc{};
    device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&commandQueue));

    // コマンド用メモリ管理
    ID3D12CommandAllocator* commandAllocator = nullptr;
    device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator));

    // 実際の描画命令
    ID3D12GraphicsCommandList* commandList = nullptr;
    device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator, nullptr, IID_PPV_ARGS(&commandList));


    // ------------------------------
    // スワップチェーン生成
    // ------------------------------

    IDXGISwapChain4* swapChain = nullptr;

    DXGI_SWAP_CHAIN_DESC1 swapDesc{};
    swapDesc.Width = kClientWidth;
    swapDesc.Height = kClientHeight;
    swapDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapDesc.SampleDesc.Count = 1;
    swapDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapDesc.BufferCount = 2;
    swapDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    factory->CreateSwapChainForHwnd(
        commandQueue,
        hwnd,
        &swapDesc,
        nullptr,
        nullptr,
        reinterpret_cast<IDXGISwapChain1**>(&swapChain)
    );


    // ------------------------------
    // 描画（1フレームだけ試し描き）
    // ------------------------------

    UINT backBufferIndex = swapChain->GetCurrentBackBufferIndex();

    float clearColor[] = { 0.2f,0.4f,0.6f,1.0f };

    // ※本来はRTV設定など必要
    commandList->Close();

    ID3D12CommandList* lists[] = { commandList };
    commandQueue->ExecuteCommandLists(1, lists);

    swapChain->Present(1, 0);


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
            // ゲーム処理を書く場所
        }
    }


    // ------------------------------
    // 終了処理
    // ------------------------------
    Log(logStream, "Application End");

    return 0;
}