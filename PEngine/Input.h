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
	void Initialize(HINSTANCE hInstance, HWND hwnd);

	// 毎フレーム呼ぶ
	void Update();

	// ==============================
	// キーボード
	// ==============================

	// 押している間
	bool PushKey(BYTE key);

	// 押した瞬間
	bool TriggerKey(BYTE key);

	// 離した瞬間
	bool ReleaseKey(BYTE key);

	// ==============================
	// ゲームパッド
	// ==============================

	// 接続されているか
	bool IsGamePadConnected();

	// ボタンを押している間
	bool PushButton(WORD button);

	// ボタンを押した瞬間
	bool TriggerButton(WORD button);

	// ボタンを離した瞬間
	bool ReleaseButton(WORD button);

	// 左スティック
	GamePadStick GetLeftStick();

	// 右スティック
	GamePadStick GetRightStick();

	// 左トリガー 0.0～1.0
	float GetLeftTrigger();

	// 右トリガー 0.0～1.0
	float GetRightTrigger();

	// 終了処理
	void Finalize();

private:
	// スティック値を-1.0～1.0へ変換
	float NormalizeStick(
		SHORT value,
		SHORT deadZone
	);

private:
	// ==============================
	// キーボード
	// ==============================

	Microsoft::WRL::ComPtr<IDirectInput8>
		directInput_;

	Microsoft::WRL::ComPtr<IDirectInputDevice8>
		keyboard_;

	BYTE keys_[256];
	BYTE preKeys_[256];

	// ==============================
	// ゲームパッド
	// ==============================

	XINPUT_STATE gamePadState_;
	XINPUT_STATE preGamePadState_;

	bool isGamePadConnected_;
};