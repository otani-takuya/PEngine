
#pragma once

#include "Matrix.h"
#include <cstdint>

// Particle用Material
// Particle.PS.hlsl のcbufferと一致させる
struct ParticleMaterial {
    Vector4 color;
    int32_t lightingType;
    float padding[3];
    Matrix4x4 uvTransform;
};

// Particle.VS.hlslのStructuredBufferと一致させる
struct ParticleTransformationMatrix {
    Matrix4x4 WVP;
    Matrix4x4 World;
};
