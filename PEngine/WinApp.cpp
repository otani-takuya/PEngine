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

		// WindowProcへWinApp自身を渡す
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

	MSG msg{};

	if (PeekMessage(
		&msg,
		nullptr,
		0,
		0,
		PM_REMOVE
	)) {

		TranslateMessage(&msg);
		DispatchMessage(&msg);

		if (msg.message == WM_QUIT) {
			return false;
		}
	}

	return true;
}


LRESULT CALLBACK WinApp::WindowProc(
	HWND hwnd,
	UINT msg,
	WPARAM wparam,
	LPARAM lparam
) {

	// ==============================
	// WinAppインスタンス取得
	// ==============================

	WinApp* winApp = nullptr;

	if (msg == WM_NCCREATE) {

		// CreateWindowの最後の引数から
		// WinAppのポインタを取得
		CREATESTRUCT* createStruct =
			reinterpret_cast<CREATESTRUCT*>(lparam);

		winApp =
			static_cast<WinApp*>(
				createStruct->lpCreateParams
				);

		// HWNDにWinAppのポインタを保存
		SetWindowLongPtr(
			hwnd,
			GWLP_USERDATA,
			reinterpret_cast<LONG_PTR>(winApp)
		);

	}
	else {

		// 保存しておいたWinAppのポインタを取得
		winApp =
			reinterpret_cast<WinApp*>(
				GetWindowLongPtr(
					hwnd,
					GWLP_USERDATA
				)
				);
	}


#ifdef USE_IMGUI

	// ImGuiへWindowsメッセージを渡す
	if (ImGui_ImplWin32_WndProcHandler(
		hwnd,
		msg,
		wparam,
		lparam
	)) {
		return true;
	}

#endif


	// ==============================
	// Windowsメッセージ処理
	// ==============================

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