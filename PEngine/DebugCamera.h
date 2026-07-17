#pragma once

#include "Matrix.h"
#include <Windows.h>



class DebugCamera {
public:
	// ==============================
	// カメラモード
	// ==============================
	enum class Mode {
		kOrbit, // ピボット回転
		kFree   // 自由移動
	};


public:
	// 初期化
	void Initialize();

	// 毎フレーム更新
	void Update();

	// WindowProcからホイール量を渡す
	void AddWheelDelta(float wheelDelta);

	// モード変更
	void SetMode(Mode mode) {
		mode_ = mode;
	}

	// モード取得
	Mode GetMode() const {
		return mode_;
	}

	// モード切り替え
	void ToggleMode();

	// 初期状態に戻す
	void Reset();


	// ビュー行列取得
	const Matrix4x4& GetViewMatrix() const {
		return viewMatrix_;
	}

	// 射影行列取得
	const Matrix4x4& GetProjectionMatrix() const {
		return projectionMatrix_;
	}

	// ==============================
	// ImGui操作用
	// ==============================

	// 注目点
	Vector3& GetTarget() {
		return target_;
	}

	// 回転
	Vector3& GetRotation() {
		return rotation_;
	}

	Vector3& GetTranslation() {
		return translation_;
	}

	// 距離
	float& GetDistance() {
		return distance_;
	}

	// 回転速度
	float& GetRotateSpeed() {
		return rotateSpeed_;
	}

	// 平行移動速度
	float& GetPanSpeed() {
		return panSpeed_;
	}

	// ズーム速度
	float& GetZoomSpeed() {
		return zoomSpeed_;
	}

	float& GetMoveSpeed() {
		return moveSpeed_;
	}

private:
	// カメラ行列を更新
	void UpdateMatrix();

private:
	// Orbitモード更新
	void UpdateOrbit(
		float deltaX,
		float deltaY,
		bool isMiddleButton,
		bool isShift
	);

	// Freeモード更新
	void UpdateFree(
		float deltaX,
		float deltaY,
		bool isRightButton
	);

	// Orbit用行列更新
	void UpdateOrbitMatrix();

	// Free用行列更新
	void UpdateFreeMatrix();


private:
	Mode mode_ = Mode::kOrbit;

	// ==============================
	// カメラ座標・回転
	// ==============================

	Vector3 rotation_ = {
		0.0f,
		0.0f,
		0.0f
	};

	Vector3 translation_ = {
		0.0f,
		0.0f,
		-10.0f
	};

	// ==============================
	// 行列
	// ==============================

	Matrix4x4 viewMatrix_{};
	Matrix4x4 projectionMatrix_{};

	// ==============================
	// マウス
	// ==============================

	POINT preMousePos_{};
	bool isFirstMouse_ = true;

	// 1フレーム分のホイール入力
	float wheelDelta_ = 0.0f;


	// ==============================
	// Orbitモード
	// ==============================

	Vector3 target_ = {
		0.0f,
		0.0f,
		0.0f
	};

	float distance_ = 10.0f;

	// ==============================
	// 操作速度
	// ==============================

	float rotateSpeed_ = 0.005f;
	float panSpeed_ = 0.01f;
	float zoomSpeed_ = 1.0f;
	float moveSpeed_ = 0.1f;


};