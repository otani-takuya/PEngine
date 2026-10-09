#pragma once

#include <Windows.h>
#include <cstdint>

class WinApp {
public:
	// ==============================
	// 定数
	// ==============================

	static const int32_t kClientWidth = 1280;
	static const int32_t kClientHeight = 720;

public:
	// ==============================
	// 基本処理
	// ==============================

	void Initialize();

	void Finalize();

	bool ProcessMessage();

	// ==============================
	// Getter
	// ==============================

	HWND GetHwnd() const {
		return hwnd_;
	}

	// マウスホイールの移動量を取得
	float GetWheelDelta() const {
		return wheelDelta_;
	}

	// マウスホイールの移動量をリセット
	void ResetWheelDelta() {
		wheelDelta_ = 0.0f;
	}

private:
	// ==============================
	// Windowsメッセージ処理
	// ==============================

	static LRESULT CALLBACK WindowProc(
		HWND hwnd,
		UINT msg,
		WPARAM wparam,
		LPARAM lparam
	);

private:
	// ==============================
	// メンバ変数
	// ==============================

	HWND hwnd_ = nullptr;

	WNDCLASS wc_{};

	// マウスホイールの移動量
	float wheelDelta_ = 0.0f;
};