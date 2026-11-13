
#pragma once

#include "Matrix.h"
#include <cstdint>

// ========================================
// Particle用Material
// ========================================
// Particle.PS.hlslのcbufferと一致させる
struct ParticleMaterial {
    Vector4 color;
    int32_t lightingType;
    float padding[3];
    Matrix4x4 uvTransform;
};

// ========================================
// Particle用Instancingデータ
// ========================================
// Particle.VS.hlslのStructuredBufferと一致させる
struct ParticleTransformationMatrix {

    // World View Projection行列
    Matrix4x4 WVP;

    // World行列
    Matrix4x4 World;

    // Particleごとの色（RGBA）
    // w成分が透明度
    Vector4 Color;
};

// C++とHLSLのデータサイズの不一致を防ぐ
static_assert(
    sizeof(ParticleTransformationMatrix) == 144,
    "ParticleTransformationMatrix size mismatch"
    );
