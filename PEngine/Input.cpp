#include "Input.h"

#include <cassert>
#include <cstring>

// 静的メンバ変数の実体
Microsoft::WRL::ComPtr<IDirectInput8> Input::directInput_ = nullptr;
Microsoft::WRL::ComPtr<IDirectInputDevice8> Input::keyboard_ = nullptr;

BYTE Input::keys_[256] = {};
BYTE Input::preKeys_[256] = {};

void Input::Initialize(HINSTANCE hInstance, HWND hwnd) {

	// DirectInput本体を生成
	HRESULT hr = DirectInput8Create(
		hInstance,
		DIRECTINPUT_VERSION,
		IID_IDirectInput8,
		reinterpret_cast<void**>(directInput_.GetAddressOf()),
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

	// キーボードの入力形式を設定
	hr = keyboard_->SetDataFormat(&c_dfDIKeyboard);

	assert(SUCCEEDED(hr));

	// 協調レベルを設定
	hr = keyboard_->SetCooperativeLevel(
		hwnd,
		DISCL_FOREGROUND |
		DISCL_NONEXCLUSIVE
	);

	assert(SUCCEEDED(hr));

	// 初回の入力取得を開始
	hr = keyboard_->Acquire();

	// 初期状態を空にする
	std::memset(keys_, 0, sizeof(keys_));
	std::memset(preKeys_, 0, sizeof(preKeys_));
}

void Input::Update() {

	// 現在のキー状態を前フレームとして保存
	std::memcpy(preKeys_, keys_, sizeof(keys_));

	// キーボードが未初期化なら何もしない
	if (keyboard_ == nullptr) {
		return;
	}

	// 現在のキー状態を取得
	HRESULT hr = keyboard_->GetDeviceState(
		sizeof(keys_),
		keys_
	);

	// ウィンドウのフォーカス復帰直後などで失敗した場合
	if (FAILED(hr)) {

		// 入力を再取得
		keyboard_->Acquire();

		hr = keyboard_->GetDeviceState(
			sizeof(keys_),
			keys_
		);

		// 再取得にも失敗した場合は入力なしにする
		if (FAILED(hr)) {
			std::memset(keys_, 0, sizeof(keys_));
		}
	}
}

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

void Input::Finalize() {

	if (keyboard_ != nullptr) {
		keyboard_->Unacquire();
	}

	keyboard_.Reset();
	directInput_.Reset();

	std::memset(keys_, 0, sizeof(keys_));
	std::memset(preKeys_, 0, sizeof(preKeys_));
}