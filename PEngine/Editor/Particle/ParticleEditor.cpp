
#include "ParticleEditor.h"
#include "ParticleManager.h"

#ifdef USE_IMGUI
#include "externals/imgui/imgui.h"
#endif

// ========================================
// 初期化
// ========================================

void ParticleEditor::Initialize(
    ParticleManager* particleManager
) {
    particleManager_ = particleManager;

    if (particleManager_ == nullptr) {
        return;
    }

    // ========================================
    // Fade Out設定を同期
    // ========================================

    // ParticleManagerが保持している設定を取得
    fadeOutEnabled_ =
        particleManager_->IsFadeOutEnabled();

    // ========================================
    // Grid初期設定
    // ========================================
    particleManager_->SetGridSettings(
        static_cast<uint32_t>(particleCountX_),
        static_cast<uint32_t>(particleCountY_),
        static_cast<uint32_t>(particleCountZ_),
        particleSpacing_,
        particleScale_
    );


    // ========================================
    // Emitter設定を取得
    // ========================================

    const ParticleManager::EmitSettings settings =
        particleManager_->GetEmitSettings();

    // 開始色を取得
    startColor_[0] = settings.startColor.x;
    startColor_[1] = settings.startColor.y;
    startColor_[2] = settings.startColor.z;

    // 終了色を取得
    endColor_[0] = settings.endColor.x;
    endColor_[1] = settings.endColor.y;
    endColor_[2] = settings.endColor.z;

    // ========================================
    // Size Over Lifetime
    // ========================================
    startSize_ = settings.startSize;
    endSize_ = settings.endSize;

    // 初速度の最小値
    emitVelocityMin_[0] = settings.velocityMin.x;
    emitVelocityMin_[1] = settings.velocityMin.y;
    emitVelocityMin_[2] = settings.velocityMin.z;

    // 初速度の最大値
    emitVelocityMax_[0] = settings.velocityMax.x;
    emitVelocityMax_[1] = settings.velocityMax.y;
    emitVelocityMax_[2] = settings.velocityMax.z;

    // 重力
    emitGravity_ = settings.gravity;

    // 寿命
    emitLifeTimeMin_ = settings.lifeTimeMin;
    emitLifeTimeMax_ = settings.lifeTimeMax;



    // ParticleManagerの現在の設定を取得
    drawParticle_ = particleManager_->IsVisible();

    billboardType_ = static_cast<int>(
        particleManager_->GetBillboardType()
        );
}

// ========================================
// Editorの描画
// ========================================
void ParticleEditor::Draw() {

#ifdef USE_IMGUI

    // ========================================
    // 初期化・表示状態の確認
    // ========================================
    if (!isVisible_ || particleManager_ == nullptr) {
        return;
    }

    // ImGui::End()はBegin()の戻り値に関係なく呼ぶ
    const bool isWindowOpen =
        ImGui::Begin("Particle Editor", &isVisible_);

    if (isWindowOpen) {

        ImGui::TextUnformatted("PEngine Particle System");
        ImGui::Separator();

        // ========================================
        // Display Settings
        // ========================================
        ImGui::SeparatorText("Display Settings");

        if (ImGui::Checkbox("Draw Particle", &drawParticle_)) {
            particleManager_->SetVisible(drawParticle_);
        }

        // ========================================
        // Particle Mode
        // ========================================
        ImGui::SeparatorText("Particle Mode");

        const char* modeNames[] = {
            "Grid",
            "Emitter"
        };

        int currentMode = static_cast<int>(
            particleManager_->GetParticleMode()
            );

        if (ImGui::Combo(
            "Mode",
            &currentMode,
            modeNames,
            IM_ARRAYSIZE(modeNames)
        )) {
            particleManager_->SetParticleMode(
                static_cast<ParticleManager::ParticleMode>(
                    currentMode
                    )
            );
        }

        // ========================================
        // Grid Settings
        // ========================================
        if (particleManager_->GetParticleMode() ==
            ParticleManager::ParticleMode::kGrid) {

            if (ImGui::CollapsingHeader(
                "Grid Settings",
                ImGuiTreeNodeFlags_DefaultOpen
            )) {

                bool gridChanged = false;

                ImGui::SeparatorText("Grid Count");

                gridChanged |= ImGui::SliderInt(
                    "Count X",
                    &particleCountX_,
                    1, 10
                );

                gridChanged |= ImGui::SliderInt(
                    "Count Y",
                    &particleCountY_,
                    1, 10
                );

                gridChanged |= ImGui::SliderInt(
                    "Count Z",
                    &particleCountZ_,
                    1, 10
                );

                ImGui::SeparatorText("Transform");

                gridChanged |= ImGui::DragFloat(
                    "Spacing",
                    &particleSpacing_,
                    0.01f, 0.1f, 5.0f
                );

                gridChanged |= ImGui::DragFloat(
                    "Scale",
                    &particleScale_,
                    0.01f, 0.01f, 3.0f
                );

                // 変更時のみ設定を反映
                if (gridChanged) {
                    particleManager_->SetGridSettings(
                        static_cast<uint32_t>(particleCountX_),
                        static_cast<uint32_t>(particleCountY_),
                        static_cast<uint32_t>(particleCountZ_),
                        particleSpacing_,
                        particleScale_
                    );
                }
            }
        }

        // ========================================
        // Emitter Settings
        // ========================================
        if (particleManager_->GetParticleMode() ==
            ParticleManager::ParticleMode::kEmitter) {

            if (ImGui::CollapsingHeader(
                "Emitter Settings",
                ImGuiTreeNodeFlags_DefaultOpen
            )) {

                // 現在の設定を取得
                // この変数は関数内で宣言する
                ParticleManager::EmitSettings emitSettings =
                    particleManager_->GetEmitSettings();

                // Emitter設定の変更フラグ
                bool emitterChanged = false;

                // ========================================
                // Emission
                // ========================================
                ImGui::SeparatorText("Emission");

                ImGui::SliderInt(
                    "Emit Count",
                    &emitCount_,
                    1, 100
                );

                ImGui::DragFloat3(
                    "Emit Position",
                    emitPosition_,
                    0.1f
                );

                // ========================================
                // Velocity
                // ========================================
                ImGui::SeparatorText("Velocity");

                emitterChanged |= ImGui::DragFloat3(
                    "Velocity Min",
                    emitVelocityMin_,
                    0.1f
                );

                emitterChanged |= ImGui::DragFloat3(
                    "Velocity Max",
                    emitVelocityMax_,
                    0.1f
                );

                // 最小値が最大値を超えないように補正
                for (int i = 0; i < 3; ++i) {
                    if (emitVelocityMin_[i] > emitVelocityMax_[i]) {
                        emitVelocityMax_[i] = emitVelocityMin_[i];
                        emitterChanged = true;
                    }
                }

                // ========================================
                // Acceleration
                // ========================================
                ImGui::SeparatorText("Acceleration");

                emitterChanged |= ImGui::DragFloat(
                    "Gravity",
                    &emitGravity_,
                    0.1f,
                    -30.0f,
                    30.0f
                );

                // ========================================
                // Life Time
                // ========================================
                ImGui::SeparatorText("Life Time");

                emitterChanged |= ImGui::DragFloat(
                    "Life Time Min",
                    &emitLifeTimeMin_,
                    0.1f,
                    0.01f,
                    10.0f
                );

                emitterChanged |= ImGui::DragFloat(
                    "Life Time Max",
                    &emitLifeTimeMax_,
                    0.1f,
                    0.01f,
                    10.0f
                );

                // 寿命の範囲を補正
                if (emitLifeTimeMin_ < 0.01f) {
                    emitLifeTimeMin_ = 0.01f;
                    emitterChanged = true;
                }

                if (emitLifeTimeMax_ < emitLifeTimeMin_) {
                    emitLifeTimeMax_ = emitLifeTimeMin_;
                    emitterChanged = true;
                }

                // ========================================
                // Appearance
                // ========================================
                ImGui::SeparatorText("Appearance");

                if (ImGui::Checkbox(
                    "Enable Fade Out",
                    &fadeOutEnabled_
                )) {
                    particleManager_->SetFadeOutEnabled(
                        fadeOutEnabled_
                    );
                }

                // ========================================
                // Color Over Lifetime
                // ========================================
                ImGui::SeparatorText("Color Over Lifetime");

                emitterChanged |= ImGui::ColorEdit3(
                    "Start Color",
                    startColor_
                );

                emitterChanged |= ImGui::ColorEdit3(
                    "End Color",
                    endColor_
                );

                // ========================================
                // Size Over Lifetime
                // ========================================
                ImGui::SeparatorText("Size Over Lifetime");

                emitterChanged |= ImGui::DragFloat(
                    "Start Size",
                    &startSize_,
                    0.01f,
                    0.0f,
                    5.0f,
                    "%.2f"
                );

                emitterChanged |= ImGui::DragFloat(
                    "End Size",
                    &endSize_,
                    0.01f,
                    0.0f,
                    5.0f,
                    "%.2f"
                );

                // ========================================
                // Random Start Size
                // ========================================
                ImGui::SeparatorText("Random Start Size");

                emitterChanged |= ImGui::Checkbox(
                    "Enable Random Size",
                    &emitSettings.randomSizeEnabled
                );

                if (emitSettings.randomSizeEnabled) {

                    emitterChanged |= ImGui::DragFloat(
                        "Random Size Min",
                        &emitSettings.randomSizeMin,
                        0.01f,
                        0.0f,
                        5.0f,
                        "%.2f"
                    );

                    emitterChanged |= ImGui::DragFloat(
                        "Random Size Max",
                        &emitSettings.randomSizeMax,
                        0.01f,
                        0.0f,
                        5.0f,
                        "%.2f"
                    );

                    // 負のサイズ倍率を防止
                    if (emitSettings.randomSizeMin < 0.0f) {
                        emitSettings.randomSizeMin = 0.0f;
                        emitterChanged = true;
                    }

                    // 最小値が最大値を超えた場合は補正
                    if (emitSettings.randomSizeMax <
                        emitSettings.randomSizeMin) {

                        emitSettings.randomSizeMax =
                            emitSettings.randomSizeMin;

                        emitterChanged = true;
                    }
                }

                // ========================================
                // Rotation Over Lifetime
                // ========================================
                ImGui::SeparatorText("Rotation Over Lifetime");

                // 回転の有効・無効
                emitterChanged |= ImGui::Checkbox(
                    "Enable Rotation",
                    &emitSettings.rotationEnabled
                );

                // 回転が有効な場合のみ設定を表示
                if (emitSettings.rotationEnabled) {

                    // ========================================
                    // Initial Rotation
                    // ========================================
                    ImGui::TextUnformatted("Initial Rotation");

                    // 初期回転角度の最小値
                    emitterChanged |= ImGui::SliderAngle(
                        "Rotation Min",
                        &emitSettings.rotationMin,
                        -360.0f,
                        360.0f
                    );

                    // 初期回転角度の最大値
                    emitterChanged |= ImGui::SliderAngle(
                        "Rotation Max",
                        &emitSettings.rotationMax,
                        -360.0f,
                        360.0f
                    );

                    // ========================================
                    // Rotation Speed
                    // ========================================
                    ImGui::TextUnformatted("Rotation Speed (rad/s)");

                    // 回転速度の最小値
                    emitterChanged |= ImGui::DragFloat(
                        "Rotation Speed Min",
                        &emitSettings.rotationSpeedMin,
                        0.1f,
                        -15.0f,
                        15.0f,
                        "%.2f"
                    );

                    // 回転速度の最大値
                    emitterChanged |= ImGui::DragFloat(
                        "Rotation Speed Max",
                        &emitSettings.rotationSpeedMax,
                        0.1f,
                        -15.0f,
                        15.0f,
                        "%.2f"
                    );

                    // ========================================
                    // 入力値の補正
                    // ========================================

                    // 初期角度の範囲を補正
                    if (emitSettings.rotationMin >
                        emitSettings.rotationMax) {

                        emitSettings.rotationMax =
                            emitSettings.rotationMin;

                        emitterChanged = true;
                    }

                    // 回転速度の範囲を補正
                    if (emitSettings.rotationSpeedMin >
                        emitSettings.rotationSpeedMax) {

                        emitSettings.rotationSpeedMax =
                            emitSettings.rotationSpeedMin;

                        emitterChanged = true;
                    }

                    // ========================================
                    // 説明
                    // ========================================
                    ImGui::TextDisabled(
                        "Each particle rotates at a random speed."
                    );
                }


                // ========================================
                // ParticleManagerへ設定を反映
                // ========================================
                if (emitterChanged) {

                    // Velocity
                    emitSettings.velocityMin = {
                        emitVelocityMin_[0],
                        emitVelocityMin_[1],
                        emitVelocityMin_[2]
                    };

                    emitSettings.velocityMax = {
                        emitVelocityMax_[0],
                        emitVelocityMax_[1],
                        emitVelocityMax_[2]
                    };

                    // Gravity
                    emitSettings.gravity = emitGravity_;

                    // Life Time
                    emitSettings.lifeTimeMin = emitLifeTimeMin_;
                    emitSettings.lifeTimeMax = emitLifeTimeMax_;

                    // Color Over Lifetime
                    emitSettings.startColor.x = startColor_[0];
                    emitSettings.startColor.y = startColor_[1];
                    emitSettings.startColor.z = startColor_[2];

                    emitSettings.endColor.x = endColor_[0];
                    emitSettings.endColor.y = endColor_[1];
                    emitSettings.endColor.z = endColor_[2];

                    // Size Over Lifetime
                    emitSettings.startSize = startSize_;
                    emitSettings.endSize = endSize_;

                    // Random Start Sizeは
                    // emitSettingsに直接変更済み

                    particleManager_->SetEmitSettings(
                        emitSettings
                    );
                }

                // ========================================
                // Fade Outの説明
                // ========================================
                if (fadeOutEnabled_) {
                    ImGui::TextDisabled(
                        "Particles fade out over their lifetime."
                    );
                }
                else {
                    ImGui::TextDisabled(
                        "Particles remain opaque until they expire."
                    );
                }

                // ========================================
                // Controls
                // ========================================
                ImGui::SeparatorText("Controls");

                if (ImGui::Button("Emit Particles")) {

                    particleManager_->Emit(
                        {
                            emitPosition_[0],
                            emitPosition_[1],
                            emitPosition_[2]
                        },
                        static_cast<uint32_t>(emitCount_)
                    );
                }

                ImGui::SameLine();

                if (ImGui::Button("Clear Particles")) {
                    particleManager_->ClearParticles();
                }
            }
        }

        // ========================================
        // Billboard
        // ========================================
        ImGui::SeparatorText("Billboard");

        const char* billboardNames[] = {
            "None",
            "Spherical",
            "Cylindrical"
        };

        if (ImGui::Combo(
            "Billboard Type",
            &billboardType_,
            billboardNames,
            IM_ARRAYSIZE(billboardNames)
        )) {
            particleManager_->SetBillboardType(
                static_cast<ParticleManager::BillboardType>(
                    billboardType_
                    )
            );
        }

        // ========================================
        // Particle Info
        // ========================================
        ImGui::SeparatorText("Particle Info");

        ImGui::Text(
            "Alive Particles : %u",
            particleManager_->GetParticleCount()
        );

        ImGui::Text(
            "Draw Instances : %u",
            particleManager_->GetInstanceCount()
        );

        ImGui::Text(
            "Max Instances : %u",
            particleManager_->GetMaxInstanceCount()
        );
    }

    // Begin()と必ず対になる
    ImGui::End();

#endif
}

// ========================================
// 表示設定
// ========================================
void ParticleEditor::SetVisible(bool visible) {
    isVisible_ = visible;
}

// ========================================
// 表示状態の取得
// ========================================
bool ParticleEditor::IsVisible() const {
    return isVisible_;
}
