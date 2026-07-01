#pragma once

// ==============================
// 構造体
// ==============================

// Vector2
struct Vector2 {
	float x;
	float y;
};

// Vector3
struct Vector3 {
	float x;
	float y;
	float z;
};

// Vector4
struct Vector4 {
	float x;
	float y;
	float z;
	float w;
};

// 4x4行列
struct Matrix4x4 {
	float m[4][4];
};

// 3x3行列
struct Matrix3x3 {
	float m[3][3];
};

// ==============================
// 行列関数
// ==============================

// 単位行列
Matrix4x4 MakeIdentity4x4();

// 平行移動行列
Matrix4x4 MakeTranslateMatrix(const Vector3& translate);

// 拡大縮小行列
Matrix4x4 MakeScaleMatrix(const Vector3& scale);

// X軸回転行列
Matrix4x4 MakeRotateXMatrix(float radian);

// Y軸回転行列
Matrix4x4 MakeRotateYMatrix(float radian);

// Z軸回転行列
Matrix4x4 MakeRotateZMatrix(float radian);

// アフィン変換行列
Matrix4x4 MakeAffineMatrix(
	const Vector3& scale,
	const Vector3& rotate,
	const Vector3& translate
);

// 行列積
Matrix4x4 Multiply(const Matrix4x4& m1, const Matrix4x4& m2);

// 逆行列
Matrix4x4 Inverse(const Matrix4x4& matrix);

// 転置行列
Matrix4x4 Transpose(const Matrix4x4& matrix);

// 透視投影行列
Matrix4x4 MakePerspectiveFovMatrix(
	float fovY,
	float aspectRatio,
	float nearClip,
	float farClip
);

// 正射影行列
Matrix4x4 MakeOrthographicMatrix(
	float left,
	float top,
	float right,
	float bottom,
	float nearClip,
	float farClip
);

// ビューポート変換行列
Matrix4x4 MakeViewportMatrix(
	float left,
	float top,
	float width,
	float height,
	float minDepth,
	float maxDepth
);

// ベクトル変換
Vector3 VectorTransform(const Vector3& vector, const Matrix4x4& matrix);

// ==============================
// 行列関数
// ==============================

// 単位行列
Matrix4x4 MakeIdentity4x4();

// 3x3単位行列
Matrix3x3 MakeIdentity3x3();