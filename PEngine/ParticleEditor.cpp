
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

    const ParticleManager::EmitSettings& settings =
        particleManager_->GetEmitSettings();

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

    // 非表示または未初期化なら終了
    if (!isVisible_ || particleManager_ == nullptr) {
        return;
    }

    // Editorウィンドウ

    if (ImGui::Begin("Particle Editor", &isVisible_)) {

        ImGui::Text("PEngine Particle System");
        ImGui::Separator();

        // ========================================
        // Particleの表示設定
        // ========================================
        ImGui::SeparatorText("Display Settings");

        if (ImGui::Checkbox(
            "Draw Particle",
            &drawParticle_
        )) {
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

                // Gridの設定変更を検出
                bool changed = false;

                ImGui::SeparatorText("Grid Count");

                changed |= ImGui::SliderInt(
                    "Count X",
                    &particleCountX_,
                    1, 10
                );

                changed |= ImGui::SliderInt(
                    "Count Y",
                    &particleCountY_,
                    1, 10
                );

                changed |= ImGui::SliderInt(
                    "Count Z",
                    &particleCountZ_,
                    1, 10
                );

                ImGui::SeparatorText("Transform");

                changed |= ImGui::DragFloat(
                    "Spacing",
                    &particleSpacing_,
                    0.01f, 0.1f, 5.0f
                );

                changed |= ImGui::DragFloat(
                    "Scale",
                    &particleScale_,
                    0.01f, 0.01f, 3.0f
                );

                // 変更があったときだけ反映
                if (changed) {

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

                // 設定が変更されたか
                bool changed = false;

                // ========================================
                // 生成設定
                // ========================================
                ImGui::SeparatorText("Emission");

                ImGui::SliderInt(
                    "Emit Count",
                    &emitCount_,
                    1,
                    100
                );

                ImGui::DragFloat3(
                    "Emit Position",
                    emitPosition_,
                    0.1f
                );

                // ========================================
                // 初速度
                // ========================================
                ImGui::SeparatorText("Velocity");

                changed |= ImGui::DragFloat3(
                    "Velocity Min",
                    emitVelocityMin_,
                    0.1f
                );

                changed |= ImGui::DragFloat3(
                    "Velocity Max",
                    emitVelocityMax_,
                    0.1f
                );

                // ========================================
                // 重力
                // ========================================
                ImGui::SeparatorText("Acceleration");

                changed |= ImGui::DragFloat(
                    "Gravity",
                    &emitGravity_,
                    0.1f,
                    -30.0f,
                    30.0f
                );

                // ========================================
                // 寿命
                // ========================================
                ImGui::SeparatorText("Life Time");

                changed |= ImGui::DragFloat(
                    "Life Time Min",
                    &emitLifeTimeMin_,
                    0.1f,
                    0.01f,
                    10.0f
                );

                changed |= ImGui::DragFloat(
                    "Life Time Max",
                    &emitLifeTimeMax_,
                    0.1f,
                    0.01f,
                    10.0f
                );

                // ========================================
                // 入力値の補正
                // ========================================

                // 各軸で最小速度が最大速度を
                // 超えないように補正
                for (int i = 0; i < 3; ++i) {
                    if (emitVelocityMin_[i] >
                        emitVelocityMax_[i]) {

                        emitVelocityMax_[i] =
                            emitVelocityMin_[i];

                        changed = true;
                    }
                }

                // 寿命の下限を保証
                if (emitLifeTimeMin_ < 0.01f) {
                    emitLifeTimeMin_ = 0.01f;
                    changed = true;
                }

                if (emitLifeTimeMax_ < emitLifeTimeMin_) {
                    emitLifeTimeMax_ = emitLifeTimeMin_;
                    changed = true;
                }


                // ========================================
                // ParticleManagerへ設定を反映
                // ========================================
                if (changed) {

                    // 現在の設定をコピー
                    ParticleManager::EmitSettings settings =
                        particleManager_->GetEmitSettings();

                    // 初速度
                    settings.velocityMin = {
                        emitVelocityMin_[0],
                        emitVelocityMin_[1],
                        emitVelocityMin_[2]
                    };

                    settings.velocityMax = {
                        emitVelocityMax_[0],
                        emitVelocityMax_[1],
                        emitVelocityMax_[2]
                    };

                    // 重力
                    settings.gravity = emitGravity_;

                    // 寿命
                    settings.lifeTimeMin = emitLifeTimeMin_;
                    settings.lifeTimeMax = emitLifeTimeMax_;

                    // 変更を反映
                    particleManager_->SetEmitSettings(settings);
                }


                // ========================================
                // Particle生成・削除
                // ========================================
                ImGui::SeparatorText("Controls");

                if (ImGui::Button("Emit Particles")) {

                    // 現在の設定でParticleを生成
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
        // Billboard設定
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
        // Particle情報
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
