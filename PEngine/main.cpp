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

    RECT wrc = { 0, 0, 1280, 720 };
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