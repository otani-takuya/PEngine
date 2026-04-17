#include <windows.h>

#pragma warning(push)
#include <string>
#include <format>


//C4023の警告を無効化
#pragma warning(disable:4023)
#include <cstdint>

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
            Log(ConvertString(std::format(L"WSTRING{}\n", wstringValue)));
		}
	}

	return 0;
}