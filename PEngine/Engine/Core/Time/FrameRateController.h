#pragma once

#include <chrono>
#include <thread>

// ========================================
// フレームレート制御（60FPS上限）
// ========================================
// CPU側でフレーム間隔を調整する。
// 描画のPresent(1, 0)による垂直同期と併用できる。
// ※処理が重い場合に60FPSを保証するものではない。
class FrameRateController {
public:
    static constexpr int kTargetFPS = 60;
    static constexpr float kFixedDeltaTime = 1.0f / kTargetFPS;

    // 1フレームの計測開始
    void BeginFrame() {
        frameStart_ = Clock::now();
    }

    // 目標フレーム時間まで待機する
    void EndFrame() {
        const auto frameDuration =
            std::chrono::duration<double>(1.0 / kTargetFPS);
        const auto deadline = frameStart_ +
            std::chrono::duration_cast<Clock::duration>(frameDuration);

        // sleep_untilでCPU使用率を抑えつつ待機
        const auto now = Clock::now();
        if (now < deadline) {
            std::this_thread::sleep_until(deadline);
        }

        // 実際に経過したフレーム時間を記録
        const auto end = Clock::now();
        measuredDeltaTime_ =
            std::chrono::duration<float>(end - frameStart_).count();
    }

    // 固定更新用の経過時間
    float GetFixedDeltaTime() const {
        return kFixedDeltaTime;
    }

    // 実測経過時間（デバッグや可変更新用）
    float GetMeasuredDeltaTime() const {
        return measuredDeltaTime_;
    }

private:
    using Clock = std::chrono::steady_clock;
    Clock::time_point frameStart_ = Clock::now();
    float measuredDeltaTime_ = kFixedDeltaTime;
};
