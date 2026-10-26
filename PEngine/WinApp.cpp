#include "WinApp.h"
#include <cassert>

#ifdef USE_IMGUI
#include "externals/imgui/imgui.h"
#include "externals/imgui/imgui_impl_win32.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(
	HWND hwnd,
	UINT msg,
	WPARAM wParam,
	LPARAM lParam
);
#endif


void WinApp::Initialize() {

	// ==============================
	// ウィンドウクラス
	// ==============================

	wc_.lpfnWndProc = WindowProc;
	wc_.lpszClassName = L"PEngineWindowClass";
	wc_.hInstance = GetModuleHandle(nullptr);
	wc_.hCursor = LoadCursor(nullptr, IDC_ARROW);

	RegisterClass(&wc_);

	// ==============================
	// ウィンドウサイズ
	// ==============================

	RECT wrc = {
		0,
		0,
		kClientWidth,
		kClientHeight
	};

	AdjustWindowRect(
		&wrc,
		WS_OVERLAPPEDWINDOW,
		FALSE
	);

	// ==============================
	// ウィンドウ生成
	// ==============================

	hwnd_ = CreateWindow(
		wc_.lpszClassName,
		L"PEngine",
		WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		wrc.right - wrc.left,
		wrc.bottom - wrc.top,
		nullptr,
		nullptr,
		wc_.hInstance,
		this
	);

	assert(hwnd_ != nullptr);

	ShowWindow(hwnd_, SW_SHOW);
}


void WinApp::Finalize() {

	if (hwnd_ != nullptr) {
		DestroyWindow(hwnd_);
		hwnd_ = nullptr;
	}

	UnregisterClass(
		wc_.lpszClassName,
		wc_.hInstance
	);
}


bool WinApp::ProcessMessage() {

	// ==============================
	// メッセージ処理状態を初期化
	// ==============================

	hasProcessedMessage_ = false;

	// すでに終了要求を受け取っている
	if (isQuitRequested_) {
		return false;
	}

	MSG msg{};

	// ==============================
	// Windowsメッセージ取得
	// ==============================

	if (PeekMessage(
		&msg,
		nullptr,
		0,
		0,
		PM_REMOVE
	)) {

		hasProcessedMessage_ = true;

		// 終了メッセージ
		if (msg.message == WM_QUIT) {

			isQuitRequested_ = true;

			return false;
		}

		// キーボードメッセージ変換
		TranslateMessage(&msg);

		// WindowProcへメッセージを渡す
		DispatchMessage(&msg);
	}

	return true;
}


LRESULT CALLBACK WinApp::WindowProc(
	HWND hwnd,
	UINT msg,
	WPARAM wparam,
	LPARAM lparam
) {

	WinApp* winApp = nullptr;

	// ==============================
	// WinApp自身のポインタを取得
	// ==============================

	if (msg == WM_NCCREATE) {

		CREATESTRUCT* createStruct =
			reinterpret_cast<CREATESTRUCT*>(lparam);

		winApp =
			static_cast<WinApp*>(
				createStruct->lpCreateParams
				);

		SetWindowLongPtr(
			hwnd,
			GWLP_USERDATA,
			reinterpret_cast<LONG_PTR>(winApp)
		);

	}
	else {

		winApp =
			reinterpret_cast<WinApp*>(
				GetWindowLongPtr(
					hwnd,
					GWLP_USERDATA
				)
				);
	}


#ifdef USE_IMGUI

	if (ImGui_ImplWin32_WndProcHandler(
		hwnd,
		msg,
		wparam,
		lparam
	)) {
		return true;
	}

#endif


	switch (msg) {

	case WM_MOUSEWHEEL:

		if (winApp != nullptr) {

			const short wheel =
				GET_WHEEL_DELTA_WPARAM(wparam);

			winApp->wheelDelta_ +=
				static_cast<float>(wheel) /
				static_cast<float>(WHEEL_DELTA);
		}

		return 0;


	case WM_DESTROY:

		PostQuitMessage(0);

		return 0;
	}


	return DefWindowProc(
		hwnd,
		msg,
		wparam,
		lparam
	);
}