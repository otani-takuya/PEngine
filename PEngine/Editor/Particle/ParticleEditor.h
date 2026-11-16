
#pragma once

// ParticleManagerの前方宣言
class ParticleManager;

// ========================================
// Particle Editor
// パーティクルの編集機能を管理する
// ========================================
class ParticleEditor {
public:
    ParticleEditor() = default;
    ~ParticleEditor() = default;

    // コピー禁止
    ParticleEditor(const ParticleEditor&) = delete;
    ParticleEditor& operator=(const ParticleEditor&) = delete;

    // 初期化
    void Initialize(ParticleManager* particleManager);

    // ImGuiの描画
    void Draw();

    // Editorの表示切り替え
    void SetVisible(bool visible);
    bool IsVisible() const;

private:
    // ParticleManagerは所有しない
    ParticleManager* particleManager_ = nullptr;

    // Editorの表示状態
    bool isVisible_ = true;


    // ========================================
    // Particle描画設定
    // ========================================

    // Particleの表示状態
    bool drawParticle_ = true;

    // Billboardの種類
    // 0: None
    // 1: Spherical
    // 2: Cylindrical
    int billboardType_ = 1;

    // ========================================
    // Grid設定
    // ========================================

    // 各軸の配置数
    int particleCountX_ = 10;
    int particleCountY_ = 10;
    int particleCountZ_ = 10;

    // 配置間隔
    float particleSpacing_ = 1.0f;

    // Particleの拡大率
    float particleScale_ = 0.5f;


    // ========================================
    // Emitter設定
    // ========================================

    // 1回のParticle生成数
    int emitCount_ = 24;

    // Particle生成位置
    float emitPosition_[3] = {
        0.0f, 0.0f, 0.0f
    };

    // 初速度の最小値
    float emitVelocityMin_[3] = {
        -5.0f, 2.0f, -5.0f
    };

    // 初速度の最大値
    float emitVelocityMax_[3] = {
        5.0f, 7.0f, 5.0f
    };

    // 重力
    float emitGravity_ = -9.8f;

    // 寿命の最小値・最大値
    float emitLifeTimeMin_ = 0.8f;
    float emitLifeTimeMax_ = 1.3f;


    // ========================================
    // Fade Out設定
    // ========================================

    // Editor上で管理するFade Outの状態
    bool fadeOutEnabled_ = true;


    // ========================================
    // Color Over Lifetime設定
    // ========================================

    // Particle生成時の開始色（RGB）
    float startColor_[3] = {
        0.64f, 0.55f, 0.44f
    };

    // Particle寿命終了時の色（RGB）
    float endColor_[3] = {
        0.33f, 0.29f, 0.26f
    };


    // ========================================
    // Size Over Lifetime設定
    // ========================================

    // Particle生成時の開始サイズ倍率
    float startSize_ = 1.0f;

    // Particle寿命終了時のサイズ倍率
    float endSize_ = 0.0f;


};
