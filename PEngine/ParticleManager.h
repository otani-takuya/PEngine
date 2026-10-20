#pragma once

#include "ParticleData.h"

#include <Windows.h>
#include <d3d12.h>
#include <dxcapi.h>
#include <wrl.h>

#include <cstdint>
#include <string>
#include <vector>

// Particleの描画・GPUリソース・Billboardを管理するクラス
class ParticleManager {
public:
    // Billboardの種類
    enum class BillboardType {
        kNone = 0,      // Billboardを使用しない
        kSpherical,     // カメラの回転に合わせて全方向へ回転
        kCylindrical    // Y軸固定
    };

    // 格子表示とEmitter表示を切り替える
    enum class ParticleMode { kGrid = 0, kEmitter };

    // 1つのParticleのTransform
    struct ParticleTransform {
        Vector3 scale = { 1.0f, 1.0f, 1.0f };
        Vector3 rotate = { 0.0f, 0.0f, 0.0f };
        Vector3 translate = { 0.0f, 0.0f, 0.0f };
    };

    // 個別のParticleデータ（STEP 10-4で寿命による削除を追加予定）
    struct Particle {
        ParticleTransform transform;
        Vector3 velocity = { 0.0f, 0.0f, 0.0f };
        Vector3 acceleration = { 0.0f, 0.0f, 0.0f };
        Vector4 color = { 1.0f, 1.0f, 1.0f, 1.0f };
        float currentTime = 0.0f;
        float lifeTime = 1.0f;
    };

    // Particleの生成パラメータ（ImGuiに依存しない）
    struct EmitSettings {
        Vector3 velocityMin = { -3.0f, 2.0f, -3.0f };
        Vector3 velocityMax = { 3.0f, 5.0f, 3.0f };
        float gravity = -9.8f;
        float lifeTimeMin = 0.5f;
        float lifeTimeMax = 2.0f;
    };

    // エンジン側から渡す初期化情報（ポインタは借用）
    struct InitializeInfo {
        ID3D12Device* device = nullptr;
        ID3D12DescriptorHeap* srvHeap = nullptr;
        UINT srvDescriptorSize = 0;
        UINT instancingSrvIndex = 0;
        uint32_t maxInstanceCount = 1000;

        // DXC関連
        IDxcUtils* dxcUtils = nullptr;
        IDxcCompiler3* dxcCompiler = nullptr;
        IDxcIncludeHandler* includeHandler = nullptr;

        // Shaderファイル
        std::wstring vertexShaderPath = L"Particle.VS.hlsl";
        std::wstring pixelShaderPath = L"Particle.PS.hlsl";
    };

    ParticleManager() = default;
    ~ParticleManager() = default;
    ParticleManager(const ParticleManager&) = delete;
    ParticleManager& operator=(const ParticleManager&) = delete;

    // 初期化・更新・描画・終了
    bool Initialize(const InitializeInfo& info);
    void Update(const Matrix4x4& viewMatrix, const Matrix4x4& projectionMatrix, float deltaTime);
    void Draw(
        ID3D12GraphicsCommandList* commandList,
        const D3D12_VERTEX_BUFFER_VIEW& vertexBufferView,
        UINT vertexCount,
        D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandle
    );
    void Finalize();

    // インスタンス数
    void SetInstanceCount(uint32_t count);
    uint32_t GetInstanceCount() const { return instanceCount_; }
    uint32_t GetMaxInstanceCount() const { return maxInstanceCount_; }

    // 表示ON/OFF
    void SetVisible(bool visible) { isVisible_ = visible; }
    bool IsVisible() const { return isVisible_; }

    // Particleの格子配置
    void SetGridSettings(
        uint32_t countX,
        uint32_t countY,
        uint32_t countZ,
        float spacing,
        float scale
    );

    // Emitterモードの操作
    void SetParticleMode(ParticleMode mode) { particleMode_ = mode; }
    ParticleMode GetParticleMode() const { return particleMode_; }
    void SetEmitSettings(const EmitSettings& settings);
    const EmitSettings& GetEmitSettings() const { return emitSettings_; }
    void Emit(const Vector3& position, uint32_t count);
    void ClearParticles();
    uint32_t GetParticleCount() const { return static_cast<uint32_t>(particles_.size()); }

    // Billboard設定
    void SetBillboardType(BillboardType type) { billboardType_ = type; }
    BillboardType GetBillboardType() const { return billboardType_; }

private:
    // GPUリソース・描画パイプライン生成
    bool CreateInstancingResource();
    bool CreateMaterialResource();
    bool CreateRootSignature();
    bool CreatePipelineState();

    // Shaderコンパイル
    Microsoft::WRL::ComPtr<IDxcBlob> CompileParticleShader(
        const std::wstring& filePath,
        const wchar_t* profile
    );

    // Emitterモードの行列更新と移動
    void UpdateEmitter(const Matrix4x4& viewMatrix, const Matrix4x4& projectionMatrix, float deltaTime);

    // Spherical Billboard行列生成
    Matrix4x4 CreateSphericalBillboardMatrix(const Matrix4x4& viewMatrix) const;
    Matrix4x4 CreateCylindricalBillboardMatrix(const Matrix4x4& viewMatrix) const;

private:
    // DirectX12 DeviceとSRV Heapは所有しない
    ID3D12Device* device_ = nullptr;
    ID3D12DescriptorHeap* srvHeap_ = nullptr;
    UINT srvDescriptorSize_ = 0;
    UINT instancingSrvIndex_ = 0;

    // DXC関連は借用する
    IDxcUtils* dxcUtils_ = nullptr;
    IDxcCompiler3* dxcCompiler_ = nullptr;
    IDxcIncludeHandler* includeHandler_ = nullptr;
    std::wstring vertexShaderPath_;
    std::wstring pixelShaderPath_;

    // Particle専用RootSignature・PSO
    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState_;

    // Material・Instancing用Resource
    Microsoft::WRL::ComPtr<ID3D12Resource> materialResource_;
    Microsoft::WRL::ComPtr<ID3D12Resource> instancingResource_;

    // MapしたResourceのCPU側ポインタ
    ParticleMaterial* materialData_ = nullptr;
    ParticleTransformationMatrix* instancingData_ = nullptr;

    // Instancing SRV
    D3D12_CPU_DESCRIPTOR_HANDLE instancingSrvHandleCPU_{};
    D3D12_GPU_DESCRIPTOR_HANDLE instancingSrvHandleGPU_{};

    // インスタンス数と描画状態
    uint32_t maxInstanceCount_ = 10;
    uint32_t instanceCount_ = 10;
    bool isVisible_ = true;
    bool isInitialized_ = false;

    // ParticleごとのTransform
    std::vector<ParticleTransform> transforms_;

    // 生成されたParticleと現在の表示モード
    ParticleMode particleMode_ = ParticleMode::kGrid;
    std::vector<Particle> particles_;
    EmitSettings emitSettings_{};

    // 格子配置の設定
    uint32_t particleCountX_ = 10;
    uint32_t particleCountY_ = 10;
    uint32_t particleCountZ_ = 10;
    float particleSpacing_ = 1.0f;
    float particleScale_ = 0.5f;

    // Billboardの状態
    BillboardType billboardType_ = BillboardType::kNone;
};
