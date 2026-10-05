#include "Input.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>

// ==============================
// 静的メンバ変数
// ==============================

Microsoft::WRL::ComPtr<IDirectInput8>
Input::directInput_ = nullptr;

Microsoft::WRL::ComPtr<IDirectInputDevice8>
Input::keyboard_ = nullptr;

BYTE Input::keys_[256] = {};
BYTE Input::preKeys_[256] = {};

XINPUT_STATE Input::gamePadState_ = {};
XINPUT_STATE Input::preGamePadState_ = {};

bool Input::isGamePadConnected_ = false;

// ==============================
// 初期化
// ==============================
void Input::Initialize(
	HINSTANCE hInstance,
	HWND hwnd
) {
	// DirectInput本体を生成
	HRESULT hr = DirectInput8Create(
		hInstance,
		DIRECTINPUT_VERSION,
		IID_IDirectInput8,
		reinterpret_cast<void**>(
			directInput_.GetAddressOf()
			),
		nullptr
	);

	assert(SUCCEEDED(hr));

	// キーボードデバイスを生成
	hr = directInput_->CreateDevice(
		GUID_SysKeyboard,
		keyboard_.GetAddressOf(),
		nullptr
	);

	assert(SUCCEEDED(hr));

	// キーボード入力形式
	hr = keyboard_->SetDataFormat(
		&c_dfDIKeyboard
	);

	assert(SUCCEEDED(hr));

	// 協調レベル
	hr = keyboard_->SetCooperativeLevel(
		hwnd,
		DISCL_FOREGROUND |
		DISCL_NONEXCLUSIVE
	);

	assert(SUCCEEDED(hr));

	// 入力取得開始
	keyboard_->Acquire();

	// キーボード初期化
	std::memset(
		keys_,
		0,
		sizeof(keys_)
	);

	std::memset(
		preKeys_,
		0,
		sizeof(preKeys_)
	);

	// ゲームパッド初期化
	ZeroMemory(
		&gamePadState_,
		sizeof(gamePadState_)
	);

	ZeroMemory(
		&preGamePadState_,
		sizeof(preGamePadState_)
	);

	isGamePadConnected_ = false;
}

// ==============================
// 更新
// ==============================
void Input::Update() {
	// ==============================
	// キーボード更新
	// ==============================

	std::memcpy(
		preKeys_,
		keys_,
		sizeof(keys_)
	);

	if (keyboard_ != nullptr) {
		HRESULT hr = keyboard_->GetDeviceState(
			sizeof(keys_),
			keys_
		);

		if (FAILED(hr)) {
			keyboard_->Acquire();

			hr = keyboard_->GetDeviceState(
				sizeof(keys_),
				keys_
			);

			if (FAILED(hr)) {
				std::memset(
					keys_,
					0,
					sizeof(keys_)
				);
			}
		}
	}

	// ==============================
	// ゲームパッド更新
	// ==============================

	preGamePadState_ = gamePadState_;

	ZeroMemory(
		&gamePadState_,
		sizeof(gamePadState_)
	);

	// 0番のコントローラーを取得
	const DWORD result = XInputGetState(
		0,
		&gamePadState_
	);

	if (result == ERROR_SUCCESS) {
		isGamePadConnected_ = true;
	}
	else {
		isGamePadConnected_ = false;

		ZeroMemory(
			&gamePadState_,
			sizeof(gamePadState_)
		);
	}
}

// ==============================
// キーボード
// ==============================
bool Input::PushKey(BYTE key) {
	return keys_[key] != 0;
}

bool Input::TriggerKey(BYTE key) {
	return keys_[key] != 0 &&
		preKeys_[key] == 0;
}

bool Input::ReleaseKey(BYTE key) {
	return keys_[key] == 0 &&
		preKeys_[key] != 0;
}

// ==============================
// ゲームパッド接続確認
// ==============================
bool Input::IsGamePadConnected() {
	return isGamePadConnected_;
}

// ==============================
// ゲームパッドボタン
// ==============================
bool Input::PushButton(WORD button) {
	if (!isGamePadConnected_) {
		return false;
	}

	return (
		gamePadState_.Gamepad.wButtons &
		button
		) != 0;
}

bool Input::TriggerButton(WORD button) {
	if (!isGamePadConnected_) {
		return false;
	}

	const bool current =
		(
			gamePadState_.Gamepad.wButtons &
			button
			) != 0;

	const bool previous =
		(
			preGamePadState_.Gamepad.wButtons &
			button
			) != 0;

	return current && !previous;
}

bool Input::ReleaseButton(WORD button) {
	if (!isGamePadConnected_) {
		return false;
	}

	const bool current =
		(
			gamePadState_.Gamepad.wButtons &
			button
			) != 0;

	const bool previous =
		(
			preGamePadState_.Gamepad.wButtons &
			button
			) != 0;

	return !current && previous;
}

// ==============================
// スティック正規化
// ==============================
float Input::NormalizeStick(
	SHORT value,
	SHORT deadZone
) {
	const int32_t signedValue =
		static_cast<int32_t>(value);

	const int32_t absoluteValue =
		std::abs(signedValue);

	// デッドゾーン内なら0
	if (absoluteValue <= deadZone) {
		return 0.0f;
	}

	// 正方向と負方向で最大値が違う
	const float maximum =
		signedValue >= 0
		? 32767.0f
		: 32768.0f;

	float normalized =
		static_cast<float>(signedValue) /
		maximum;

	// デッドゾーンを除いた範囲を
	// 0.0～1.0へ再調整
	const float deadZoneNormalized =
		static_cast<float>(deadZone) /
		maximum;

	if (normalized > 0.0f) {
		normalized =
			(normalized - deadZoneNormalized) /
			(1.0f - deadZoneNormalized);
	}
	else {
		normalized =
			(normalized + deadZoneNormalized) /
			(1.0f - deadZoneNormalized);
	}

	return std::clamp(
		normalized,
		-1.0f,
		1.0f
	);
}

// ==============================
// 左スティック
// ==============================
GamePadStick Input::GetLeftStick() {
	if (!isGamePadConnected_) {
		return { 0.0f, 0.0f };
	}

	return {
		NormalizeStick(
			gamePadState_.Gamepad.sThumbLX,
			XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE
		),

		NormalizeStick(
			gamePadState_.Gamepad.sThumbLY,
			XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE
		)
	};
}

// ==============================
// 右スティック
// ==============================
GamePadStick Input::GetRightStick() {
	if (!isGamePadConnected_) {
		return { 0.0f, 0.0f };
	}

	return {
		NormalizeStick(
			gamePadState_.Gamepad.sThumbRX,
			XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE
		),

		NormalizeStick(
			gamePadState_.Gamepad.sThumbRY,
			XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE
		)
	};
}

// ==============================
// 左トリガー
// ==============================
float Input::GetLeftTrigger() {
	if (!isGamePadConnected_) {
		return 0.0f;
	}

	const BYTE trigger =
		gamePadState_.Gamepad.bLeftTrigger;

	if (
		trigger <=
		XINPUT_GAMEPAD_TRIGGER_THRESHOLD
		) {
		return 0.0f;
	}

	const float threshold =
		static_cast<float>(
			XINPUT_GAMEPAD_TRIGGER_THRESHOLD
			);

	return (
		static_cast<float>(trigger) -
		threshold
		) /
		(255.0f - threshold);
}

// ==============================
// 右トリガー
// ==============================
float Input::GetRightTrigger() {
	if (!isGamePadConnected_) {
		return 0.0f;
	}

	const BYTE trigger =
		gamePadState_.Gamepad.bRightTrigger;

	if (
		trigger <=
		XINPUT_GAMEPAD_TRIGGER_THRESHOLD
		) {
		return 0.0f;
	}

	const float threshold =
		static_cast<float>(
			XINPUT_GAMEPAD_TRIGGER_THRESHOLD
			);

	return (
		static_cast<float>(trigger) -
		threshold
		) /
		(255.0f - threshold);
}

// ==============================
// 終了処理
// ==============================
void Input::Finalize() {
	if (keyboard_ != nullptr) {
		keyboard_->Unacquire();
	}

	keyboard_.Reset();
	directInput_.Reset();

	std::memset(
		keys_,
		0,
		sizeof(keys_)
	);

	std::memset(
		preKeys_,
		0,
		sizeof(preKeys_)
	);

	ZeroMemory(
		&gamePadState_,
		sizeof(gamePadState_)
	);

	ZeroMemory(
		&preGamePadState_,
		sizeof(preGamePadState_)
	);

	isGamePadConnected_ = false;
}