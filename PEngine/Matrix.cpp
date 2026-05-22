#include "Matrix.h"

#include <cmath>

// ==============================
// 単位行列
// ==============================
Matrix4x4 MakeIdentity4x4() {
	Matrix4x4 result{};

	result.m[0][0] = 1.0f;
	result.m[1][1] = 1.0f;
	result.m[2][2] = 1.0f;
	result.m[3][3] = 1.0f;

	return result;
}

// ==============================
// 平行移動行列
// ==============================
Matrix4x4 MakeTranslateMatrix(const Vector3& translate) {
	Matrix4x4 result = MakeIdentity4x4();

	result.m[3][0] = translate.x;
	result.m[3][1] = translate.y;
	result.m[3][2] = translate.z;

	return result;
}

// ==============================
// 拡大縮小行列
// ==============================
Matrix4x4 MakeScaleMatrix(const Vector3& scale) {
	Matrix4x4 result{};

	result.m[0][0] = scale.x;
	result.m[1][1] = scale.y;
	result.m[2][2] = scale.z;
	result.m[3][3] = 1.0f;

	return result;
}

// ==============================
// X軸回転行列
// ==============================
Matrix4x4 MakeRotateXMatrix(float radian) {
	Matrix4x4 result = MakeIdentity4x4();

	result.m[1][1] = std::cos(radian);
	result.m[1][2] = std::sin(radian);

	result.m[2][1] = -std::sin(radian);
	result.m[2][2] = std::cos(radian);

	return result;
}

// ==============================
// Y軸回転行列
// ==============================
Matrix4x4 MakeRotateYMatrix(float radian) {
	Matrix4x4 result = MakeIdentity4x4();

	result.m[0][0] = std::cos(radian);
	result.m[0][2] = -std::sin(radian);

	result.m[2][0] = std::sin(radian);
	result.m[2][2] = std::cos(radian);

	return result;
}

// ==============================
// Z軸回転行列
// ==============================
Matrix4x4 MakeRotateZMatrix(float radian) {
	Matrix4x4 result = MakeIdentity4x4();

	result.m[0][0] = std::cos(radian);
	result.m[0][1] = std::sin(radian);

	result.m[1][0] = -std::sin(radian);
	result.m[1][1] = std::cos(radian);

	return result;
}

// ==============================
// 行列積
// ==============================
Matrix4x4 Multiply(const Matrix4x4& m1, const Matrix4x4& m2) {
	Matrix4x4 result{};

	for (int row = 0; row < 4; ++row) {
		for (int column = 0; column < 4; ++column) {

			result.m[row][column] =
				m1.m[row][0] * m2.m[0][column] +
				m1.m[row][1] * m2.m[1][column] +
				m1.m[row][2] * m2.m[2][column] +
				m1.m[row][3] * m2.m[3][column];
		}
	}

	return result;
}

// ==============================
// アフィン変換行列
// ==============================
Matrix4x4 MakeAffineMatrix(
	const Vector3& scale,
	const Vector3& rotate,
	const Vector3& translate
) {

	Matrix4x4 scaleMatrix = MakeScaleMatrix(scale);

	Matrix4x4 rotateXMatrix = MakeRotateXMatrix(rotate.x);
	Matrix4x4 rotateYMatrix = MakeRotateYMatrix(rotate.y);
	Matrix4x4 rotateZMatrix = MakeRotateZMatrix(rotate.z);

	Matrix4x4 rotateMatrix =
		Multiply(
			Multiply(rotateXMatrix, rotateYMatrix),
			rotateZMatrix
		);

	Matrix4x4 translateMatrix = MakeTranslateMatrix(translate);

	Matrix4x4 result =
		Multiply(
			Multiply(scaleMatrix, rotateMatrix),
			translateMatrix
		);

	return result;
}

// ==============================
// 転置行列
// ==============================
Matrix4x4 Transpose(const Matrix4x4& matrix) {
	Matrix4x4 result{};

	for (int row = 0; row < 4; ++row) {
		for (int column = 0; column < 4; ++column) {
			result.m[row][column] = matrix.m[column][row];
		}
	}

	return result;
}

// ==============================
// 逆行列
// ==============================
Matrix4x4 Inverse(const Matrix4x4& matrix) {

	Matrix4x4 result = MakeIdentity4x4();

	float tmp[4][8]{};

	// 拡大係数行列作成
	for (int i = 0; i < 4; ++i) {
		for (int j = 0; j < 4; ++j) {
			tmp[i][j] = matrix.m[i][j];
		}

		tmp[i][i + 4] = 1.0f;
	}

	// ガウスジョルダン法
	for (int i = 0; i < 4; ++i) {

		float pivot = tmp[i][i];

		for (int j = 0; j < 8; ++j) {
			tmp[i][j] /= pivot;
		}

		for (int k = 0; k < 4; ++k) {

			if (i != k) {

				float factor = tmp[k][i];

				for (int j = 0; j < 8; ++j) {
					tmp[k][j] -= factor * tmp[i][j];
				}
			}
		}
	}

	// 右側を取り出す
	for (int i = 0; i < 4; ++i) {
		for (int j = 0; j < 4; ++j) {
			result.m[i][j] = tmp[i][j + 4];
		}
	}

	return result;
}

// ==============================
// 透視投影行列
// ==============================
Matrix4x4 MakePerspectiveFovMatrix(
	float fovY,
	float aspectRatio,
	float nearClip,
	float farClip
) {

	Matrix4x4 result{};

	float f = 1.0f / std::tan(fovY / 2.0f);

	result.m[0][0] = f / aspectRatio;
	result.m[1][1] = f;
	result.m[2][2] = farClip / (farClip - nearClip);
	result.m[2][3] = 1.0f;
	result.m[3][2] = (-nearClip * farClip) / (farClip - nearClip);

	return result;
}

// ==============================
// 正射影行列
// ==============================
Matrix4x4 MakeOrthographicMatrix(
	float left,
	float top,
	float right,
	float bottom,
	float nearClip,
	float farClip
) {

	Matrix4x4 result{};

	result.m[0][0] = 2.0f / (right - left);
	result.m[1][1] = 2.0f / (top - bottom);
	result.m[2][2] = 1.0f / (farClip - nearClip);
	result.m[3][0] = (left + right) / (left - right);
	result.m[3][1] = (top + bottom) / (bottom - top);
	result.m[3][2] = nearClip / (nearClip - farClip);
	result.m[3][3] = 1.0f;

	return result;
}

// ==============================
// ビューポート変換行列
// ==============================
Matrix4x4 MakeViewportMatrix(
	float left,
	float top,
	float width,
	float height,
	float minDepth,
	float maxDepth
) {

	Matrix4x4 result{};

	result.m[0][0] = width / 2.0f;
	result.m[1][1] = -height / 2.0f;
	result.m[2][2] = maxDepth - minDepth;

	result.m[3][0] = left + width / 2.0f;
	result.m[3][1] = top + height / 2.0f;
	result.m[3][2] = minDepth;
	result.m[3][3] = 1.0f;

	return result;
}

// ==============================
// ベクトル変換
// ==============================
Vector3 VectorTransform(const Vector3& vector, const Matrix4x4& matrix) {

	Vector3 result{};

	result.x =
		vector.x * matrix.m[0][0] +
		vector.y * matrix.m[1][0] +
		vector.z * matrix.m[2][0] +
		1.0f * matrix.m[3][0];

	result.y =
		vector.x * matrix.m[0][1] +
		vector.y * matrix.m[1][1] +
		vector.z * matrix.m[2][1] +
		1.0f * matrix.m[3][1];

	result.z =
		vector.x * matrix.m[0][2] +
		vector.y * matrix.m[1][2] +
		vector.z * matrix.m[2][2] +
		1.0f * matrix.m[3][2];

	float w =
		vector.x * matrix.m[0][3] +
		vector.y * matrix.m[1][3] +
		vector.z * matrix.m[2][3] +
		1.0f * matrix.m[3][3];

	if (w != 0.0f) {
		result.x /= w;
		result.y /= w;
		result.z /= w;
	}

	return result;
}