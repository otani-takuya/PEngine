#pragma once

#include <Windows.h>
#include <cstdint>

class WinApp {
public:

	static const int32_t kClientWidth = 1280;
	static const int32_t kClientHeight = 720;

	void Initialize();
	void Finalize();

	HWND GetHwnd() const {
		return hwnd_;
	}

	// ホイール入力取得
	float GetWheelDelta() const {
		return wheelDelta_;
	}

	// ホイール入力リセット
	void ResetWheelDelta() {
		wheelDelta_ = 0.0f;
	}

	// ==============================
	// Windowsメッセージ処理
	// ==============================

	// メッセージを処理する
	// WM_QUITを受け取ったらfalse
	bool ProcessMessage();

	// 今回の呼び出しでメッセージを処理したか
	bool HasProcessedMessage() const {
		return hasProcessedMessage_;
	}

	// アプリケーションインスタンスを取得
	HINSTANCE GetHInstance() const {
		return wc_.hInstance;
	}

private:

	static LRESULT CALLBACK WindowProc(
		HWND hwnd,
		UINT msg,
		WPARAM wparam,
		LPARAM lparam
	);

private:

	HWND hwnd_ = nullptr;
	WNDCLASS wc_{};

	float wheelDelta_ = 0.0f;

	// メッセージを処理したか
	bool hasProcessedMessage_ = false;

	// 終了要求を受け取ったか
	bool isQuitRequested_ = false;
};