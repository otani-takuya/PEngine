#include "DebugCamera.h"

#include <algorithm>
#include <cmath>

void DebugCamera::Initialize() {

	GetCursorPos(&preMousePos_);
	isFirstMouse_ = false;

	projectionMatrix_ =
		Matrix::MakePerspectiveFovMatrix(
			0.45f,
			1280.0f / 720.0f,
			0.1f,
			1000.0f
		);

	UpdateOrbitMatrix();
}

void DebugCamera::Update() {

	// ==============================
	// マウス移動量
	// ==============================

	POINT mousePos{};
	GetCursorPos(&mousePos);

	if (isFirstMouse_) {
		preMousePos_ = mousePos;
		isFirstMouse_ = false;
	}

	const float deltaX =
		static_cast<float>(
			mousePos.x - preMousePos_.x
			);

	const float deltaY =
		static_cast<float>(
			mousePos.y - preMousePos_.y
			);

	preMousePos_ = mousePos;

	// ==============================
	// 入力状態
	// ==============================

	const bool isMiddleButton =
		(GetAsyncKeyState(VK_MBUTTON) & 0x8000) != 0;

	const bool isRightButton =
		(GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;

	const bool isShift =
		(GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;

	// ==============================
	// モードごとの更新
	// ==============================

	switch (mode_) {

	case Mode::kOrbit:

		UpdateOrbit(
			deltaX,
			deltaY,
			isMiddleButton,
			isShift
		);

		UpdateOrbitMatrix();

		break;

	case Mode::kFree:

		UpdateFree(
			deltaX,
			deltaY,
			isRightButton
		);

		UpdateFreeMatrix();

		break;
	}
}

void DebugCamera::UpdateOrbit(
	float deltaX,
	float deltaY,
	bool isMiddleButton,
	bool isShift
) {

	// ==============================
	// 中ボタン：ピボット回転
	// ==============================

	if (isMiddleButton && !isShift) {

		rotation_.y +=
			deltaX * rotateSpeed_;

		rotation_.x +=
			deltaY * rotateSpeed_;

		const float maxPitch = 1.5f;

		rotation_.x =
			std::clamp(
				rotation_.x,
				-maxPitch,
				maxPitch
			);
	}

	// ==============================
	// Shift＋中ボタン：平行移動
	// ==============================

	if (isMiddleButton && isShift) {

		const float yaw = rotation_.y;
		const float pitch = rotation_.x;

		const Vector3 right = {
			std::cos(yaw),
			0.0f,
			-std::sin(yaw)
		};

		const Vector3 up = {
			-std::sin(yaw) * std::sin(pitch),
			std::cos(pitch),
			-std::cos(yaw) * std::sin(pitch)
		};

		const float currentPanSpeed =
			panSpeed_ * distance_;

		target_.x -=
			right.x *
			deltaX *
			currentPanSpeed;

		target_.y -=
			right.y *
			deltaX *
			currentPanSpeed;

		target_.z -=
			right.z *
			deltaX *
			currentPanSpeed;

		target_.x +=
			up.x *
			deltaY *
			currentPanSpeed;

		target_.y +=
			up.y *
			deltaY *
			currentPanSpeed;

		target_.z +=
			up.z *
			deltaY *
			currentPanSpeed;
	}

	// ==============================
	// ホイール：ズーム
	// ==============================

	if (wheelDelta_ != 0.0f) {

		distance_ -=
			wheelDelta_ * zoomSpeed_;

		distance_ =
			std::clamp(
				distance_,
				0.5f,
				500.0f
			);

		wheelDelta_ = 0.0f;
	}
}

void DebugCamera::UpdateFree(
	float deltaX,
	float deltaY,
	bool isRightButton
) {

	// ==============================
	// 右ボタン：その場で回転
	// ==============================

	if (isRightButton) {

		rotation_.y +=
			deltaX * rotateSpeed_;

		rotation_.x +=
			deltaY * rotateSpeed_;

		const float maxPitch = 1.5f;

		rotation_.x =
			std::clamp(
				rotation_.x,
				-maxPitch,
				maxPitch
			);
	}

	// ==============================
	// 前方向
	// ==============================

	const Vector3 forward = {
		std::sin(rotation_.y),
		0.0f,
		std::cos(rotation_.y)
	};

	// ==============================
	// 右方向
	// ==============================

	const Vector3 right = {
		std::cos(rotation_.y),
		0.0f,
		-std::sin(rotation_.y)
	};

	// ==============================
	// WASD移動
	// ==============================

	if (GetAsyncKeyState('W') & 0x8000) {

		translation_.x +=
			forward.x * moveSpeed_;

		translation_.z +=
			forward.z * moveSpeed_;
	}

	if (GetAsyncKeyState('S') & 0x8000) {

		translation_.x -=
			forward.x * moveSpeed_;

		translation_.z -=
			forward.z * moveSpeed_;
	}

	if (GetAsyncKeyState('D') & 0x8000) {

		translation_.x +=
			right.x * moveSpeed_;

		translation_.z +=
			right.z * moveSpeed_;
	}

	if (GetAsyncKeyState('A') & 0x8000) {

		translation_.x -=
			right.x * moveSpeed_;

		translation_.z -=
			right.z * moveSpeed_;
	}

	// ==============================
	// 上下移動
	// ==============================

	if (GetAsyncKeyState(VK_SPACE) & 0x8000) {
		translation_.y += moveSpeed_;
	}

	if (GetAsyncKeyState(VK_LSHIFT) & 0x8000) {
		translation_.y -= moveSpeed_;
	}

	// Freeモードではホイール入力を使わない
	wheelDelta_ = 0.0f;
}

void DebugCamera::UpdateOrbitMatrix() {

	const float yaw = rotation_.y;
	const float pitch = rotation_.x;

	// ピボットの周囲にカメラを配置
	translation_.x =
		target_.x +
		std::sin(yaw) *
		std::cos(pitch) *
		distance_;

	translation_.y =
		target_.y -
		std::sin(pitch) *
		distance_;

	translation_.z =
		target_.z -
		std::cos(yaw) *
		std::cos(pitch) *
		distance_;

	const Matrix4x4 cameraMatrix =
		Matrix::MakeAffineMatrix(
			{ 1.0f, 1.0f, 1.0f },
			rotation_,
			translation_
		);

	viewMatrix_ =
		Matrix::Inverse(cameraMatrix);
}

void DebugCamera::UpdateFreeMatrix() {

	const Matrix4x4 cameraMatrix =
		Matrix::MakeAffineMatrix(
			{ 1.0f, 1.0f, 1.0f },
			rotation_,
			translation_
		);

	viewMatrix_ =
		Matrix::Inverse(cameraMatrix);
}

void DebugCamera::AddWheelDelta(
	float wheelDelta
) {
	wheelDelta_ += wheelDelta;
}

void DebugCamera::ToggleMode() {

	if (mode_ == Mode::kOrbit) {
		mode_ = Mode::kFree;
	}
	else {
		mode_ = Mode::kOrbit;
	}

	// 切り替え直後のマウス移動量が飛ばないようにする
	GetCursorPos(&preMousePos_);
}

void DebugCamera::Reset() {

	rotation_ = {
		0.0f,
		0.0f,
		0.0f
	};

	translation_ = {
		0.0f,
		0.0f,
		-10.0f
	};

	target_ = {
		0.0f,
		0.0f,
		0.0f
	};

	distance_ = 10.0f;

	wheelDelta_ = 0.0f;

	if (mode_ == Mode::kOrbit) {
		UpdateOrbitMatrix();
	}
	else {
		UpdateFreeMatrix();
	}
}