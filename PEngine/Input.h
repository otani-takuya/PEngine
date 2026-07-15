#pragma once

#define DIRECTINPUT_VERSION 0x0800

#include <Windows.h>
#include <dinput.h>
#include <wrl.h>

#pragma comment(lib, "dinput8.lib")
#pragma comment(lib, "dxguid.lib")

class Input {
public:
	// DirectInputとキーボードを初期化
	static void Initialize(HINSTANCE hInstance, HWND hwnd);

	// 毎フレーム呼ぶ
	static void Update();

	// 押している間
	static bool PushKey(BYTE key);

	// 押した瞬間
	static bool TriggerKey(BYTE key);

	// 離した瞬間
	static bool ReleaseKey(BYTE key);

	// 終了処理
	static void Finalize();

private:
	static Microsoft::WRL::ComPtr<IDirectInput8> directInput_;
	static Microsoft::WRL::ComPtr<IDirectInputDevice8> keyboard_;

	static BYTE keys_[256];
	static BYTE preKeys_[256];
};