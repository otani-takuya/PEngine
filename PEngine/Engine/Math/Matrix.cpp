#include "Matrix.h"

#include <algorithm>
#include <cassert>
#include <cmath>

// ==============================
// 4x4単位行列
// ==============================
Matrix4x4 Matrix::MakeIdentity4x4() {

	Matrix4x4 result{};

	result.m[0][0] = 1.0f;
	result.m[1][1] = 1.0f;
	result.m[2][2] = 1.0f;
	result.m[3][3] = 1.0f;

	return result;
}

// ==============================
// 3x3単位行列
// ==============================
Matrix3x3 Matrix::MakeIdentity3x3() {

	Matrix3x3 result{};

	result.m[0][0] = 1.0f;
	result.m[1][1] = 1.0f;
	result.m[2][2] = 1.0f;

	return result;
}

// ==============================
// 平行移動行列
// ==============================
Matrix4x4 Matrix::MakeTranslateMatrix(
	const Vector3& translate
) {

	Matrix4x4 result = MakeIdentity4x4();

	result.m[3][0] = translate.x;
	result.m[3][1] = translate.y;
	result.m[3][2] = translate.z;

	return result;
}

// ==============================
// 拡大縮小行列
// ==============================
Matrix4x4 Matrix::MakeScaleMatrix(
	const Vector3& scale
) {

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
Matrix4x4 Matrix::MakeRotateXMatrix(
	float radian
) {

	Matrix4x4 result = MakeIdentity4x4();

	const float cosine = std::cos(radian);
	const float sine = std::sin(radian);

	result.m[1][1] = cosine;
	result.m[1][2] = sine;

	result.m[2][1] = -sine;
	result.m[2][2] = cosine;

	return result;
}

// ==============================
// Y軸回転行列
// ==============================
Matrix4x4 Matrix::MakeRotateYMatrix(
	float radian
) {

	Matrix4x4 result = MakeIdentity4x4();

	const float cosine = std::cos(radian);
	const float sine = std::sin(radian);

	result.m[0][0] = cosine;
	result.m[0][2] = -sine;

	result.m[2][0] = sine;
	result.m[2][2] = cosine;

	return result;
}

// ==============================
// Z軸回転行列
// ==============================
Matrix4x4 Matrix::MakeRotateZMatrix(
	float radian
) {

	Matrix4x4 result = MakeIdentity4x4();

	const float cosine = std::cos(radian);
	const float sine = std::sin(radian);

	result.m[0][0] = cosine;
	result.m[0][1] = sine;

	result.m[1][0] = -sine;
	result.m[1][1] = cosine;

	return result;
}

// ==============================
// 行列積
// ==============================
Matrix4x4 Matrix::Multiply(
	const Matrix4x4& m1,
	const Matrix4x4& m2
) {

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
Matrix4x4 Matrix::MakeAffineMatrix(
	const Vector3& scale,
	const Vector3& rotate,
	const Vector3& translate
) {

	const Matrix4x4 scaleMatrix =
		MakeScaleMatrix(scale);

	const Matrix4x4 rotateXMatrix =
		MakeRotateXMatrix(rotate.x);

	const Matrix4x4 rotateYMatrix =
		MakeRotateYMatrix(rotate.y);

	const Matrix4x4 rotateZMatrix =
		MakeRotateZMatrix(rotate.z);

	// 回転順序：X → Y → Z
	const Matrix4x4 rotateMatrix =
		Multiply(
			Multiply(
				rotateXMatrix,
				rotateYMatrix
			),
			rotateZMatrix
		);

	const Matrix4x4 translateMatrix =
		MakeTranslateMatrix(translate);

	// 拡大縮小 → 回転 → 平行移動
	return Multiply(
		Multiply(
			scaleMatrix,
			rotateMatrix
		),
		translateMatrix
	);
}

// ==============================
// 転置行列
// ==============================
Matrix4x4 Matrix::Transpose(
	const Matrix4x4& matrix
) {

	Matrix4x4 result{};

	for (int row = 0; row < 4; ++row) {
		for (int column = 0; column < 4; ++column) {
			result.m[row][column] =
				matrix.m[column][row];
		}
	}

	return result;
}

// ==============================
// 逆行列
// ==============================
Matrix4x4 Matrix::Inverse(
	const Matrix4x4& matrix
) {

	float augmentedMatrix[4][8]{};

	// 左側に元の行列、右側に単位行列を入れる
	for (int row = 0; row < 4; ++row) {

		for (int column = 0; column < 4; ++column) {
			augmentedMatrix[row][column] =
				matrix.m[row][column];
		}

		augmentedMatrix[row][row + 4] = 1.0f;
	}

	// ガウス・ジョルダン法
	for (int pivotColumn = 0; pivotColumn < 4; ++pivotColumn) {

		// ピボットの絶対値が最大になる行を探す
		int pivotRow = pivotColumn;

		for (int row = pivotColumn + 1; row < 4; ++row) {

			if (std::abs(augmentedMatrix[row][pivotColumn]) >
				std::abs(augmentedMatrix[pivotRow][pivotColumn])) {

				pivotRow = row;
			}
		}

		// ピボットが0に近い場合は逆行列を作れない
		assert(
			std::abs(augmentedMatrix[pivotRow][pivotColumn]) >
			0.000001f
		);

		// 必要なら行を入れ替える
		if (pivotRow != pivotColumn) {

			for (int column = 0; column < 8; ++column) {
				std::swap(
					augmentedMatrix[pivotColumn][column],
					augmentedMatrix[pivotRow][column]
				);
			}
		}

		const float pivot =
			augmentedMatrix[pivotColumn][pivotColumn];

		// ピボットを1にする
		for (int column = 0; column < 8; ++column) {
			augmentedMatrix[pivotColumn][column] /= pivot;
		}

		// ピボット以外の同じ列を0にする
		for (int row = 0; row < 4; ++row) {

			if (row == pivotColumn) {
				continue;
			}

			const float factor =
				augmentedMatrix[row][pivotColumn];

			for (int column = 0; column < 8; ++column) {
				augmentedMatrix[row][column] -=
					factor *
					augmentedMatrix[pivotColumn][column];
			}
		}
	}

	Matrix4x4 result{};

	// 右側の4x4部分を取り出す
	for (int row = 0; row < 4; ++row) {
		for (int column = 0; column < 4; ++column) {
			result.m[row][column] =
				augmentedMatrix[row][column + 4];
		}
	}

	return result;
}

// ==============================
// 透視投影行列
// ==============================
Matrix4x4 Matrix::MakePerspectiveFovMatrix(
	float fovY,
	float aspectRatio,
	float nearClip,
	float farClip
) {

	Matrix4x4 result{};

	const float f =
		1.0f / std::tan(fovY / 2.0f);

	result.m[0][0] = f / aspectRatio;
	result.m[1][1] = f;

	result.m[2][2] =
		farClip / (farClip - nearClip);

	result.m[2][3] = 1.0f;

	result.m[3][2] =
		(-nearClip * farClip) /
		(farClip - nearClip);

	return result;
}

// ==============================
// 正射影行列
// ==============================
Matrix4x4 Matrix::MakeOrthographicMatrix(
	float left,
	float top,
	float right,
	float bottom,
	float nearClip,
	float farClip
) {

	Matrix4x4 result{};

	result.m[0][0] =
		2.0f / (right - left);

	result.m[1][1] =
		2.0f / (top - bottom);

	result.m[2][2] =
		1.0f / (farClip - nearClip);

	result.m[3][0] =
		(left + right) / (left - right);

	result.m[3][1] =
		(top + bottom) / (bottom - top);

	result.m[3][2] =
		nearClip / (nearClip - farClip);

	result.m[3][3] = 1.0f;

	return result;
}

// ==============================
// ビューポート変換行列
// ==============================
Matrix4x4 Matrix::MakeViewportMatrix(
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

	result.m[3][0] =
		left + width / 2.0f;

	result.m[3][1] =
		top + height / 2.0f;

	result.m[3][2] = minDepth;
	result.m[3][3] = 1.0f;

	return result;
}

// ==============================
// ビュー行列
// ==============================
Matrix4x4 Matrix::MakeViewMatrix(
	const Vector3& rotate,
	const Vector3& translate
) {

	const Matrix4x4 cameraMatrix =
		MakeAffineMatrix(
			{ 1.0f, 1.0f, 1.0f },
			rotate,
			translate
		);

	return Inverse(cameraMatrix);
}

// ==============================
// ベクトル変換
// ==============================
Vector3 Matrix::Transform(
	const Vector3& vector,
	const Matrix4x4& matrix
) {

	Vector3 result{};

	result.x =
		vector.x * matrix.m[0][0] +
		vector.y * matrix.m[1][0] +
		vector.z * matrix.m[2][0] +
		matrix.m[3][0];

	result.y =
		vector.x * matrix.m[0][1] +
		vector.y * matrix.m[1][1] +
		vector.z * matrix.m[2][1] +
		matrix.m[3][1];

	result.z =
		vector.x * matrix.m[0][2] +
		vector.y * matrix.m[1][2] +
		vector.z * matrix.m[2][2] +
		matrix.m[3][2];

	const float w =
		vector.x * matrix.m[0][3] +
		vector.y * matrix.m[1][3] +
		vector.z * matrix.m[2][3] +
		matrix.m[3][3];

	assert(std::abs(w) > 0.000001f);

	result.x /= w;
	result.y /= w;
	result.z /= w;

	return result;
}