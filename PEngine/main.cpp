//ファイルやディレクトリに関する操作を行うライブラリ
#include <filesystem>
//ファイルに書いたり読んだりするためのライブラリ
#include <fstream>
//時間を扱うライブラリ
#include <chrono>

#include <windows.h>
#include <string>

#pragma warning(push)
#include <format>


//C4023の警告を無効化
#pragma warning(disable:4023)
#include <cstdint>


#include <d3d12.h>
#include <dxgi1_6.h>
#include <cassert>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")


#pragma warning(pop)

void Log(const std::string& message) {
    OutputDebugStringA(message.c_str());
}


std::wstring ConvertString(const std::string& str) {
    if (str.empty()) {
        return std::wstring();
    }

    auto sizeNeeded = MultiByteToWideChar(CP_UTF8, 0, reinterpret_cast<const char*>(&str[0]), static_cast<int>(str.size()), NULL, 0);
    if (sizeNeeded == 0) {
        return std::wstring();
    }
    std::wstring result(sizeNeeded, 0);
    MultiByteToWideChar(CP_UTF8, 0, reinterpret_cast<const char*>(&str[0]), static_cast<int>(str.size()), &result[0], sizeNeeded);
    return result;
}

std::string ConvertString(const std::wstring& str) {
    if (str.empty()) {
        return std::string();
    }

    auto sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), NULL, 0, NULL, NULL);
    if (sizeNeeded == 0) {
        return std::string();
    }
    std::string result(sizeNeeded, 0);
    WideCharToMultiByte(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), result.data(), sizeNeeded, NULL, NULL);
    return result;
}

void Log(std::ostream& os, const std::string& message) {
    os << message << std::endl;
    OutputDebugStringA(message.c_str());
}


//ウィンドウプロシージャの定義
LRESULT CALLBACK WindowProc(
    HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
	//メッセージに応じてゲーム固有の処理を行う
    switch (msg) {
		//ウィンドウが破棄されたときの処理
    case WM_DESTROY:
        //05に対して、アプリの終了を伝える
        PostQuitMessage(0);
        return 0;
    }

	//標準のメッセージ処理を行う
    return DefWindowProc(hwnd, msg, wparam, lparam);
}

//Windowsアプリでのエントリーポイント(main関数)
int WINAPI WinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ LPSTR, _In_ int){
	//出力ウィンドウへの文字出力
	OutputDebugStringA("Hello,DirectX!\n");



    WNDCLASS wc{};
    //ウィンドウプロシージャ
    wc.lpfnWndProc = WindowProc;
	//ウィンドウクラス名(何でもいい)
    wc.lpszClassName = L"CG2WindowClass";
	//インスタンスハンドル
    wc.hInstance = GetModuleHandle(nullptr);
    //カーソル
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);

	//ウィンドウクラスの登録
    RegisterClass(&wc);

    //クライアント領域のサイズ
	const int32_t kClientWidth = 1280;
	const int32_t kClientHeight = 720;

    //ウィンドウサイズを表す構造体にクライアント領域を入れる
	RECT wrc = { 0, 0, kClientWidth, kClientHeight };

	//クライアント領域をもとに実際のサイズにwrcを修正する
	AdjustWindowRect(&wrc, WS_OVERLAPPEDWINDOW, FALSE);

    //ウィンドウの生成
    HWND hwnd = CreateWindow(
		wc.lpszClassName,       //ウィンドウクラス名
		L"CG2",                 //ウィンドウタイトル
		WS_OVERLAPPEDWINDOW,    //ウィンドウスタイル
		CW_USEDEFAULT,          //ウィンドウの表示X座標(Windowsに任せる)
		CW_USEDEFAULT,          //ウィンドウの表示Y座標(WindowsOSに任せる)
		wrc.right - wrc.left,   //ウィンドウの幅
		wrc.bottom - wrc.top,   //ウィンドウの高さ
		nullptr,                //親ウィンドウハンドル
		nullptr,                //メニューハンドル
		wc.hInstance,           //インスタンスハンドル
		nullptr					//オプション
    );

    // ウィンドウを表示
    ShowWindow(hwnd, SW_SHOW);


    //ログのディレクトリを用意
    std::filesystem::create_directory("logs");

    //現在時刻を取得
	std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
    //ログファイルの名前にコンマ何秒入らないので、削って秒にする
	std::chrono::time_point<std::chrono::system_clock, std::chrono::seconds> nowseconds = std::chrono::time_point_cast<std::chrono::seconds>(now); 
    //日本時間(PCの設定時間)に変換
	std::chrono::zoned_time localTime{ std::chrono::current_zone(), nowseconds };
    //formatを使って年月日_時分秒の文字列に変換
	std::string dateString = std::format("{:%Y%m%d_%H%M%S}", localTime);
    //時刻を使ってファイル名を決定
	std::string logFilePath = std::string("logs/") + dateString + ".log";
    //ファイルを作って書き込み準備
	std::ofstream logStream(logFilePath);

    Log(logStream, "Application Start\n");


    //DXGIファクトリーの生成
    IDXGIFactory7* dxgiFactory = nullptr;
    //HRESULTはWindows系のエラーコードであり、関数が成功したかどうかをSUCCEEDEDマクロで判定できる
    HRESULT hr = CreateDXGIFactory(IID_PPV_ARGS(&dxgiFactory));
    //初期化の根本的な部分でエラーが出た場合はプログラムが間違っているか、どうにもできない場合が多いので、assertにしておく
    assert(SUCCEEDED(hr));

	//使用するアダプター用の変数。最初にnullptrを入れておく
	IDXGIAdapter4* useAdapter = nullptr;
    //いい順にアダプタを頼む
    for (UINT i = 0; dxgiFactory->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&useAdapter)) != DXGI_ERROR_NOT_FOUND; ++i) {
        //アダプタの名前を取得してログに出す
        DXGI_ADAPTER_DESC3 adapterDesc{};
        hr = useAdapter->GetDesc3(&adapterDesc);
		assert(SUCCEEDED(hr)); //取得できないのは一大事
        //ソフトウェアアダプタでなければ採用！
        if (!(adapterDesc.Flags & DXGI_ADAPTER_FLAG3_SOFTWARE)) {
            //採用したアダプタの情報ログに出力。wstringの方なので注意
            Log(logStream, ConvertString(std::format(L"Use Adapter:{}\n", adapterDesc.Description)));
            break;
        }
		useAdapter = nullptr; // ソフトウェアアダプタだった場合は見ないことにする
	}
    //適切なアダプタが見つからなかったので起動できない
	assert(useAdapter != nullptr);

    ID3D12Device* device = nullptr;
    //継続レベルとログ出力用の文字列
    D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_12_2, D3D_FEATURE_LEVEL_12_1,D3D_FEATURE_LEVEL_12_0
    };

    const char* featureLevelStrings[] = {
        "12.2", "12.1", "12.0"
	};
	//高い順に生成できるか試す
    for (size_t i = 0;i < _countof(featureLevels); ++i) {
        hr = D3D12CreateDevice(useAdapter, featureLevels[i], IID_PPV_ARGS(&device));
        if (SUCCEEDED(hr)) {
            Log(logStream, std::format("Feature Level {} is supported.\n", featureLevelStrings[i]));
            break;
        }
	}



    // wstringValue が未定義だったため定義を追加
    std::wstring wstringValue = L"Sample";

	MSG msg{};
	//ウィンドウの×ボタンが押されるまでループ
	while (msg.message != WM_QUIT) {
	//Windowにメッセージが来てたら最優先で処理させる
		if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		} else {
			//ゲームの処理↓

            // wstring->stringの変換
            //Log(ConvertString(std::format(L"WSTRING{}\n", wstringValue)));
		}
	}

    Log(logStream, "Application End\n");
	return 0;
}