#pragma once

#define DIRECTINPUT_VERSION 0x0800

#include <Windows.h>
#include <dinput.h>
#include <Xinput.h>
#include <wrl.h>

#pragma comment(lib, "dinput8.lib")
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "xinput.lib")

// ==============================
// スティック入力
// ==============================
struct GamePadStick {
	float x;
	float y;
};

class Input {
public:
	// DirectInput・キーボードを初期化
	static void Initialize(HINSTANCE hInstance, HWND hwnd);

	// 毎フレーム呼ぶ
	static void Update();

	// ==============================
	// キーボード
	// ==============================

	// 押している間
	static bool PushKey(BYTE key);

	// 押した瞬間
	static bool TriggerKey(BYTE key);

	// 離した瞬間
	static bool ReleaseKey(BYTE key);

	// ==============================
	// ゲームパッド
	// ==============================

	// 接続されているか
	static bool IsGamePadConnected();

	// ボタンを押している間
	static bool PushButton(WORD button);

	// ボタンを押した瞬間
	static bool TriggerButton(WORD button);

	// ボタンを離した瞬間
	static bool ReleaseButton(WORD button);

	// 左スティック
	static GamePadStick GetLeftStick();

	// 右スティック
	static GamePadStick GetRightStick();

	// 左トリガー 0.0～1.0
	static float GetLeftTrigger();

	// 右トリガー 0.0～1.0
	static float GetRightTrigger();

	// 終了処理
	static void Finalize();

private:
	// スティック値を-1.0～1.0へ変換
	static float NormalizeStick(
		SHORT value,
		SHORT deadZone
	);

private:
	// ==============================
	// キーボード
	// ==============================

	static Microsoft::WRL::ComPtr<IDirectInput8>
		directInput_;

	static Microsoft::WRL::ComPtr<IDirectInputDevice8>
		keyboard_;

	static BYTE keys_[256];
	static BYTE preKeys_[256];

	// ==============================
	// ゲームパッド
	// ==============================

	static XINPUT_STATE gamePadState_;
	static XINPUT_STATE preGamePadState_;

	static bool isGamePadConnected_;
};