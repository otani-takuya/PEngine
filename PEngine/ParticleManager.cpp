
#include "ParticleManager.h"

#include <cassert>
#include <algorithm>
#include <dxcapi.h>
#include <Windows.h>
#include <cmath>
#include <random>

// ==========================================
// UploadHeapのBufferResource生成
// ==========================================
namespace {

    Microsoft::WRL::ComPtr<ID3D12Resource>
        CreateParticleBufferResource(
            ID3D12Device* device,
            size_t sizeInBytes
        ) {
        D3D12_HEAP_PROPERTIES heapProperties{};
        heapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;

        D3D12_RESOURCE_DESC resourceDesc{};
        resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        resourceDesc.Width = sizeInBytes;
        resourceDesc.Height = 1;
        resourceDesc.DepthOrArraySize = 1;
        resourceDesc.MipLevels = 1;
        resourceDesc.SampleDesc.Count = 1;
        resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        Microsoft::WRL::ComPtr<ID3D12Resource> resource;

        HRESULT hr = device->CreateCommittedResource(
            &heapProperties,
            D3D12_HEAP_FLAG_NONE,
            &resourceDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&resource)
        );

        if (FAILED(hr)) {
            return nullptr;
        }

        return resource;
    }

} // namespace

// ==========================================
// 初期化
// ==========================================
bool ParticleManager::Initialize(
    const InitializeInfo& info
) {
    if (isInitialized_) {
        return false;
    }

    if (!info.device ||
        !info.srvHeap ||
        info.srvDescriptorSize == 0 ||
        info.maxInstanceCount == 0) {
        return false;
    }

    if (!info.dxcUtils ||
        !info.dxcCompiler ||
        !info.includeHandler) {
        return false;
    }

    dxcUtils_ = info.dxcUtils;
    dxcCompiler_ = info.dxcCompiler;
    includeHandler_ = info.includeHandler;

    vertexShaderPath_ = info.vertexShaderPath;
    pixelShaderPath_ = info.pixelShaderPath;

    if (info.srvHeap->GetDesc().Type !=
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV ||
        !(info.srvHeap->GetDesc().Flags &
            D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE) ||
        info.instancingSrvIndex >=
        info.srvHeap->GetDesc().NumDescriptors) {
        return false;
    }

    device_ = info.device;
    srvHeap_ = info.srvHeap;

    srvDescriptorSize_ = info.srvDescriptorSize;
    instancingSrvIndex_ = info.instancingSrvIndex;

    maxInstanceCount_ = info.maxInstanceCount;
    instanceCount_ = maxInstanceCount_;


    // ==========================================
    // Particle Transform配列の初期化
    // ==========================================

    // 最大インスタンス数分の要素を確保
    transforms_.resize(maxInstanceCount_);

    // 各ParticleのTransformを初期化
    for (uint32_t index = 0; index < maxInstanceCount_; ++index) {

        transforms_[index].scale = {
            0.5f, 0.5f, 0.5f
        };

        transforms_[index].rotate = {
            0.0f, 0.0f, 0.0f
        };

        transforms_[index].translate = {
            0.0f, 0.0f, 0.0f
        };
    }



    if (!CreateInstancingResource()) {
        Finalize();
        return false;
    }

    if (!CreateMaterialResource()) {
        Finalize();
        return false;
    }

    // RootSignature生成
    if (!CreateRootSignature()) {
        Finalize();
        return false;
    }

    // PSO生成
    if (!CreatePipelineState()) {
        Finalize();
        return false;
    }

    particles_.clear();
    particleMode_ = ParticleMode::kGrid;
    isInitialized_ = true;
    return true;
}


// ==========================================
// Spherical Billboard行列生成
// ==========================================
Matrix4x4 ParticleManager::CreateSphericalBillboardMatrix(
    const Matrix4x4& viewMatrix
) const {

    // ======================================
    // View行列の逆行列を取得
    // ======================================

    Matrix4x4 billboardMatrix =
        Matrix::Inverse(viewMatrix);

    // ======================================
    // 平行移動成分を取り除く
    // ======================================

    // 行ベクトル方式なので4行目が平行移動
    billboardMatrix.m[3][0] = 0.0f;
    billboardMatrix.m[3][1] = 0.0f;
    billboardMatrix.m[3][2] = 0.0f;

    // 同次座標成分を明示的に設定
    billboardMatrix.m[0][3] = 0.0f;
    billboardMatrix.m[1][3] = 0.0f;
    billboardMatrix.m[2][3] = 0.0f;
    billboardMatrix.m[3][3] = 1.0f;

    return billboardMatrix;
}



// ==========================================
// Particle更新
// ==========================================
void ParticleManager::Update(
    const Matrix4x4& viewMatrix,
    const Matrix4x4& projectionMatrix,
    float deltaTime
) {

    // ======================================
    // 初期化確認
    // ======================================

    if (!isInitialized_ || !instancingData_) {
        return;
    }

    // Emitterモードでは個別のParticleを移動・描画する
    if (particleMode_ == ParticleMode::kEmitter) {
        UpdateEmitter(viewMatrix, projectionMatrix, deltaTime);
        return;
    }

    // Transform配列のサイズ確認
    if (transforms_.size() < maxInstanceCount_) {
        assert(false && "Particle Transform配列が不足しています");
        return;
    }

    // ======================================
    // ViewProjection行列
    // ======================================

    Matrix4x4 viewProjectionMatrix =
        Matrix::Multiply(
            viewMatrix,
            projectionMatrix
        );

    // ==========================================
    // Billboard行列の選択
    // ==========================================
    Matrix4x4 billboardMatrix =
        Matrix::MakeIdentity4x4();

    switch (billboardType_) {

    case BillboardType::kSpherical:
        billboardMatrix =
            CreateSphericalBillboardMatrix(viewMatrix);
        break;

    case BillboardType::kCylindrical:
        billboardMatrix =
            CreateCylindricalBillboardMatrix(viewMatrix);
        break;

    case BillboardType::kNone:
    default:
        break;
    }


    // ======================================
    // 格子配置の中心位置
    // ======================================

    const float offsetX =
        (particleCountX_ - 1) * particleSpacing_ * 0.5f;

    const float offsetY =
        (particleCountY_ - 1) * particleSpacing_ * 0.5f;

    const float offsetZ =
        (particleCountZ_ - 1) * particleSpacing_ * 0.5f;

    // ======================================
    // Particle配置・行列更新
    // ======================================

    uint32_t index = 0;

    for (uint32_t z = 0; z < particleCountZ_; ++z) {

        for (uint32_t y = 0; y < particleCountY_; ++y) {

            for (uint32_t x = 0; x < particleCountX_; ++x) {

                // 最大数チェック
                if (index >= maxInstanceCount_) {
                    instanceCount_ = index;
                    return;
                }

                // Transform配列チェック
                if (index >= transforms_.size()) {
                    assert(false && "Particle配列の範囲外アクセス");
                    instanceCount_ = index;
                    return;
                }

                // ==================================
                // Transform設定
                // ==================================

                ParticleTransform& transform =
                    transforms_[index];

                transform.scale = {
                    particleScale_,
                    particleScale_,
                    particleScale_
                };

                transform.rotate = {
                    0.0f,
                    0.0f,
                    0.0f
                };

                transform.translate = {
                    static_cast<float>(x) * particleSpacing_ - offsetX,
                    static_cast<float>(y) * particleSpacing_ - offsetY,
                    static_cast<float>(z) * particleSpacing_ - offsetZ
                };

                // ==================================
                // World行列
                // ==================================

                Matrix4x4 worldMatrix{};

                if (billboardType_ != BillboardType::kNone) {

                    // Scale行列
                    Matrix4x4 scaleMatrix =
                        Matrix::MakeScaleMatrix(
                            transform.scale
                        );

                    // Translation行列
                    Matrix4x4 translateMatrix =
                        Matrix::MakeTranslateMatrix(
                            transform.translate
                        );

                    // S × Billboard × T
                    worldMatrix =
                        Matrix::Multiply(
                            Matrix::Multiply(
                                scaleMatrix,
                                billboardMatrix
                            ),
                            translateMatrix
                        );

                }
                else {

                    // 従来のParticle描画
                    worldMatrix =
                        Matrix::MakeAffineMatrix(
                            transform.scale,
                            transform.rotate,
                            transform.translate
                        );
                }

                // ==================================
                // WVP行列
                // ==================================

                Matrix4x4 wvpMatrix =
                    Matrix::Multiply(
                        worldMatrix,
                        viewProjectionMatrix
                    );

                // ==================================
                // GPU用データに書き込む
                // ==================================

                instancingData_[index].WVP = wvpMatrix;
                instancingData_[index].World = worldMatrix;

                ++index;
            }
        }
    }

    // ======================================
    // 描画するParticle数
    // ======================================

    instanceCount_ = index;
}

// ==========================================
// Cylindrical Billboard行列生成
// ==========================================
Matrix4x4 ParticleManager::CreateCylindricalBillboardMatrix(
    const Matrix4x4& viewMatrix
) const {

    // View行列からカメラのWorld行列を取得
    const Matrix4x4 cameraWorld =
        Matrix::Inverse(viewMatrix);

    // カメラの前方向をXZ平面に投影する
    const float forwardX = cameraWorld.m[2][0];
    const float forwardZ = cameraWorld.m[2][2];

    // 水平方向の長さを求める
    const float length = std::sqrt(
        forwardX * forwardX +
        forwardZ * forwardZ
    );

    // 真上・真下を向いている場合は
    // 水平方向が求められないため単位行列を返す
    if (length < 0.0001f) {
        return Matrix::MakeIdentity4x4();
    }

    // 水平方向を正規化
    const float zX = forwardX / length;
    const float zZ = forwardZ / length;

    // Y軸を固定した回転行列を作成
    Matrix4x4 billboardMatrix =
        Matrix::MakeIdentity4x4();

    // X軸方向
    billboardMatrix.m[0][0] = zZ;
    billboardMatrix.m[0][1] = 0.0f;
    billboardMatrix.m[0][2] = -zX;

    // Y軸方向（固定）
    billboardMatrix.m[1][0] = 0.0f;
    billboardMatrix.m[1][1] = 1.0f;
    billboardMatrix.m[1][2] = 0.0f;

    // Z軸方向
    billboardMatrix.m[2][0] = zX;
    billboardMatrix.m[2][1] = 0.0f;
    billboardMatrix.m[2][2] = zZ;

    return billboardMatrix;
}

// ==========================================
// Particle生成設定の更新
// ==========================================
void ParticleManager::SetEmitSettings(const EmitSettings& settings) {
    emitSettings_ = settings;
    auto fixRange = [](float& minimum, float& maximum) {
        if (minimum > maximum) {
            std::swap(minimum, maximum);
        }
        };
    fixRange(emitSettings_.velocityMin.x, emitSettings_.velocityMax.x);
    fixRange(emitSettings_.velocityMin.y, emitSettings_.velocityMax.y);
    fixRange(emitSettings_.velocityMin.z, emitSettings_.velocityMax.z);
    emitSettings_.lifeTimeMin = (std::max)(emitSettings_.lifeTimeMin, 0.01f);
    emitSettings_.lifeTimeMax = (std::max)(emitSettings_.lifeTimeMax, 0.01f);
    fixRange(emitSettings_.lifeTimeMin, emitSettings_.lifeTimeMax);
}

// ==========================================
// Particle生成（GPUリソースは使い回す）
// ==========================================
void ParticleManager::Emit(const Vector3& position, uint32_t count) {
    if (!isInitialized_ || count == 0 || particles_.size() >= maxInstanceCount_) {
        return;
    }

    const size_t available = static_cast<size_t>(maxInstanceCount_) - particles_.size();
    const uint32_t emitCount = static_cast<uint32_t>((std::min)(static_cast<size_t>(count), available));
    particles_.reserve(particles_.size() + emitCount);

    static std::mt19937 randomEngine(std::random_device{}());
    std::uniform_real_distribution<float> velocityX(emitSettings_.velocityMin.x, emitSettings_.velocityMax.x);
    std::uniform_real_distribution<float> velocityY(emitSettings_.velocityMin.y, emitSettings_.velocityMax.y);
    std::uniform_real_distribution<float> velocityZ(emitSettings_.velocityMin.z, emitSettings_.velocityMax.z);
    std::uniform_real_distribution<float> lifeTime(emitSettings_.lifeTimeMin, emitSettings_.lifeTimeMax);

    for (uint32_t i = 0; i < emitCount; ++i) {
        Particle particle{};
        particle.transform.translate = position;
        particle.transform.scale = { particleScale_, particleScale_, particleScale_ };
        particle.velocity = { velocityX(randomEngine), velocityY(randomEngine), velocityZ(randomEngine) };
        particle.acceleration = { 0.0f, emitSettings_.gravity, 0.0f };
        particle.currentTime = 0.0f;
        particle.lifeTime = lifeTime(randomEngine);
        particles_.push_back(particle);
    }
}

// ==========================================
// 生成したParticleを削除
// ==========================================
void ParticleManager::ClearParticles() {
    particles_.clear();
    if (particleMode_ == ParticleMode::kEmitter) {
        instanceCount_ = 0;
    }
}

// ==========================================
// Emitterモードの更新
// ==========================================
void ParticleManager::UpdateEmitter(
    const Matrix4x4& viewMatrix,
    const Matrix4x4& projectionMatrix,
    float deltaTime
) {
    // 負の経過時間は無効として扱う
    deltaTime = (std::max)(deltaTime, 0.0f);
    const Matrix4x4 viewProjection = Matrix::Multiply(viewMatrix, projectionMatrix);

    Matrix4x4 billboard = Matrix::MakeIdentity4x4();
    switch (billboardType_) {
    case BillboardType::kSpherical:
        billboard = CreateSphericalBillboardMatrix(viewMatrix);
        break;
    case BillboardType::kCylindrical:
        billboard = CreateCylindricalBillboardMatrix(viewMatrix);
        break;
    case BillboardType::kNone:
    default:
        break;
    }

    // 寿命を進め、期限切れのParticleを削除する
    for (Particle& particle : particles_) {
        particle.currentTime += deltaTime;
    }
    particles_.erase(
        std::remove_if(particles_.begin(), particles_.end(),
            [](const Particle& particle) {
                return particle.currentTime >= particle.lifeTime;
            }),
        particles_.end()
    );

    uint32_t index = 0;
    for (Particle& particle : particles_) {
        if (index >= maxInstanceCount_) {
            break;
        }

        // 半陰的オイラー法：速度を更新してから位置を更新
        particle.velocity.x += particle.acceleration.x * deltaTime;
        particle.velocity.y += particle.acceleration.y * deltaTime;
        particle.velocity.z += particle.acceleration.z * deltaTime;

        particle.transform.translate.x += particle.velocity.x * deltaTime;
        particle.transform.translate.y += particle.velocity.y * deltaTime;
        particle.transform.translate.z += particle.velocity.z * deltaTime;

        // 寿命の残りに応じて表示スケールだけを小さくする
        const float lifeRatio = (std::clamp)(particle.currentTime / particle.lifeTime, 0.0f, 1.0f);
        const float remainingScale = 1.0f - lifeRatio;
        const Vector3 drawScale = {
            particle.transform.scale.x * remainingScale,
            particle.transform.scale.y * remainingScale,
            particle.transform.scale.z * remainingScale
        };

        Matrix4x4 world{};
        if (billboardType_ == BillboardType::kNone) {
            world = Matrix::MakeAffineMatrix(drawScale, particle.transform.rotate, particle.transform.translate);
        }
        else {
            const Matrix4x4 scale = Matrix::MakeScaleMatrix(drawScale);
            const Matrix4x4 translate = Matrix::MakeTranslateMatrix(particle.transform.translate);
            world = Matrix::Multiply(Matrix::Multiply(scale, billboard), translate);
        }

        instancingData_[index].World = world;
        instancingData_[index].WVP = Matrix::Multiply(world, viewProjection);
        ++index;
    }
    instanceCount_ = index;
}

// ==========================================
// Particle Instancing描画
// ==========================================
void ParticleManager::Draw(
    ID3D12GraphicsCommandList* commandList,
    const D3D12_VERTEX_BUFFER_VIEW& vertexBufferView,
    UINT vertexCount,
    D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandle
) {
    // ======================================
    // 描画可能か確認
    // ======================================

    if (!isInitialized_ ||
        !isVisible_ ||
        !commandList ||
        !rootSignature_ ||
        !pipelineState_ ||
        !materialResource_ ||
        !instancingResource_ ||
        !srvHeap_ ||
        vertexCount == 0 ||
        instanceCount_ == 0) {
        return;
    }

    // ======================================
    // DescriptorHeap
    // ======================================

    ID3D12DescriptorHeap* descriptorHeaps[] = {
        srvHeap_
    };

    commandList->SetDescriptorHeaps(
        1,
        descriptorHeaps
    );

    // ======================================
    // RootSignature / PSO
    // ======================================

    commandList->SetGraphicsRootSignature(
        rootSignature_.Get()
    );

    commandList->SetPipelineState(
        pipelineState_.Get()
    );

    // ======================================
    // PrimitiveTopology
    // ======================================

    commandList->IASetPrimitiveTopology(
        D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST
    );

    // ======================================
    // VertexBuffer
    // ======================================

    commandList->IASetVertexBuffers(
        0,
        1,
        &vertexBufferView
    );

    // IndexBufferは使用しない
    commandList->IASetIndexBuffer(nullptr);

    // ======================================
    // RootParameter[0]
    // Material CBV : b0 / PixelShader
    // ======================================

    commandList->SetGraphicsRootConstantBufferView(
        0,
        materialResource_->GetGPUVirtualAddress()
    );

    // ======================================
    // RootParameter[1]
    // Instancing SRV : t0 / VertexShader
    // ======================================

    commandList->SetGraphicsRootDescriptorTable(
        1,
        instancingSrvHandleGPU_
    );

    // ======================================
    // RootParameter[2]
    // Texture SRV : t0 / PixelShader
    // ======================================

    commandList->SetGraphicsRootDescriptorTable(
        2,
        textureSrvHandle
    );

    // ======================================
    // Instancing描画
    // ======================================

    commandList->DrawInstanced(
        vertexCount,
        instanceCount_,
        0,
        0
    );
}



// ==========================================
// Instancing用StructuredBuffer生成
// ==========================================
bool ParticleManager::CreateInstancingResource() {

    const size_t bufferSize =
        sizeof(ParticleTransformationMatrix) *
        maxInstanceCount_;

    instancingResource_ =
        CreateParticleBufferResource(
            device_,
            bufferSize
        );

    if (!instancingResource_) {
        return false;
    }

    HRESULT hr = instancingResource_->Map(
        0,
        nullptr,
        reinterpret_cast<void**>(&instancingData_)
    );

    if (FAILED(hr) || !instancingData_) {
        return false;
    }

    // SRV Handle
    instancingSrvHandleCPU_ =
        srvHeap_->GetCPUDescriptorHandleForHeapStart();

    instancingSrvHandleCPU_.ptr +=
        static_cast<SIZE_T>(srvDescriptorSize_) *
        instancingSrvIndex_;

    instancingSrvHandleGPU_ =
        srvHeap_->GetGPUDescriptorHandleForHeapStart();

    instancingSrvHandleGPU_.ptr +=
        static_cast<UINT64>(srvDescriptorSize_) *
        instancingSrvIndex_;

    // StructuredBuffer用SRV
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};

    srvDesc.Format = DXGI_FORMAT_UNKNOWN;
    srvDesc.Shader4ComponentMapping =
        D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

    srvDesc.ViewDimension =
        D3D12_SRV_DIMENSION_BUFFER;

    srvDesc.Buffer.FirstElement = 0;
    srvDesc.Buffer.NumElements = maxInstanceCount_;

    srvDesc.Buffer.StructureByteStride =
        sizeof(ParticleTransformationMatrix);

    srvDesc.Buffer.Flags =
        D3D12_BUFFER_SRV_FLAG_NONE;

    device_->CreateShaderResourceView(
        instancingResource_.Get(),
        &srvDesc,
        instancingSrvHandleCPU_
    );

    return true;
}

// ==========================================
// Material用Resource生成
// ==========================================
bool ParticleManager::CreateMaterialResource() {

    materialResource_ =
        CreateParticleBufferResource(
            device_,
            sizeof(ParticleMaterial)
        );

    if (!materialResource_) {
        return false;
    }

    HRESULT hr = materialResource_->Map(
        0,
        nullptr,
        reinterpret_cast<void**>(&materialData_)
    );

    if (FAILED(hr) || !materialData_) {
        return false;
    }

    // テクスチャの色をそのまま表示
    materialData_->color = {
        1.0f, 1.0f, 1.0f, 1.0f
    };

    // ライティングなし
    materialData_->lightingType = 0;

    // UV変換行列は単位行列
    materialData_->uvTransform =
        Matrix::MakeIdentity4x4();

    return true;
}

// ==========================================
// インスタンス数変更
// ==========================================

void ParticleManager::SetInstanceCount(
    uint32_t count
) {
    instanceCount_ = (std::min)(
        count,
        maxInstanceCount_
        );
}


// ==========================================
// 終了処理
// ==========================================
void ParticleManager::Finalize() {

    if (instancingResource_ && instancingData_) {
        instancingResource_->Unmap(0, nullptr);
    }

    if (materialResource_ && materialData_) {
        materialResource_->Unmap(0, nullptr);
    }

    instancingData_ = nullptr;
    materialData_ = nullptr;

    instancingResource_.Reset();
    materialResource_.Reset();

    rootSignature_.Reset();
    pipelineState_.Reset();

    instancingSrvHandleCPU_ = {};
    instancingSrvHandleGPU_ = {};

    device_ = nullptr;
    srvHeap_ = nullptr;

    srvDescriptorSize_ = 0;
    instancingSrvIndex_ = 0;

    instanceCount_ = 0;
    isInitialized_ = false;

    transforms_.clear();
    particles_.clear();
    particleMode_ = ParticleMode::kGrid;

    dxcUtils_ = nullptr;
    dxcCompiler_ = nullptr;
    includeHandler_ = nullptr;

    vertexShaderPath_.clear();
    pixelShaderPath_.clear();

}


void ParticleManager::SetGridSettings(
    uint32_t countX,
    uint32_t countY,
    uint32_t countZ,
    float spacing,
    float scale
) {
    // 0個を指定した場合も安全に処理できるようにする
    particleCountX_ = countX;
    particleCountY_ = countY;
    particleCountZ_ = countZ;

    particleSpacing_ = spacing;
    particleScale_ = scale;
}


bool ParticleManager::CreateRootSignature() {

    // ==========================================
    // Instancing SRV : t0 / VertexShader
    // ==========================================

    D3D12_DESCRIPTOR_RANGE instancingRange{};

    instancingRange.RangeType =
        D3D12_DESCRIPTOR_RANGE_TYPE_SRV;

    instancingRange.NumDescriptors = 1;
    instancingRange.BaseShaderRegister = 0;

    instancingRange.OffsetInDescriptorsFromTableStart =
        D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    // ==========================================
    // Texture SRV : t0 / PixelShader
    // ==========================================

    D3D12_DESCRIPTOR_RANGE textureRange{};

    textureRange.RangeType =
        D3D12_DESCRIPTOR_RANGE_TYPE_SRV;

    textureRange.NumDescriptors = 1;
    textureRange.BaseShaderRegister = 0;

    textureRange.OffsetInDescriptorsFromTableStart =
        D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    // ==========================================
    // RootParameter
    // ==========================================

    D3D12_ROOT_PARAMETER rootParameters[3]{};

    // [0] Material CBV : b0 / PS
    rootParameters[0].ParameterType =
        D3D12_ROOT_PARAMETER_TYPE_CBV;

    rootParameters[0].ShaderVisibility =
        D3D12_SHADER_VISIBILITY_PIXEL;

    rootParameters[0].Descriptor.ShaderRegister = 0;

    // [1] Instancing SRV : t0 / VS
    rootParameters[1].ParameterType =
        D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;

    rootParameters[1].ShaderVisibility =
        D3D12_SHADER_VISIBILITY_VERTEX;

    rootParameters[1].DescriptorTable.pDescriptorRanges =
        &instancingRange;

    rootParameters[1].DescriptorTable.NumDescriptorRanges = 1;

    // [2] Texture SRV : t0 / PS
    rootParameters[2].ParameterType =
        D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;

    rootParameters[2].ShaderVisibility =
        D3D12_SHADER_VISIBILITY_PIXEL;

    rootParameters[2].DescriptorTable.pDescriptorRanges =
        &textureRange;

    rootParameters[2].DescriptorTable.NumDescriptorRanges = 1;

    // ==========================================
    // StaticSampler
    // ==========================================

    D3D12_STATIC_SAMPLER_DESC sampler{};

    sampler.Filter =
        D3D12_FILTER_MIN_MAG_MIP_LINEAR;

    sampler.AddressU =
        D3D12_TEXTURE_ADDRESS_MODE_WRAP;

    sampler.AddressV =
        D3D12_TEXTURE_ADDRESS_MODE_WRAP;

    sampler.AddressW =
        D3D12_TEXTURE_ADDRESS_MODE_WRAP;

    sampler.ComparisonFunc =
        D3D12_COMPARISON_FUNC_NEVER;

    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderRegister = 0;

    sampler.ShaderVisibility =
        D3D12_SHADER_VISIBILITY_PIXEL;

    // ==========================================
    // RootSignature
    // ==========================================

    D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc{};

    rootSignatureDesc.Flags =
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    rootSignatureDesc.pParameters = rootParameters;
    rootSignatureDesc.NumParameters = 3;

    rootSignatureDesc.pStaticSamplers = &sampler;
    rootSignatureDesc.NumStaticSamplers = 1;

    Microsoft::WRL::ComPtr<ID3DBlob> signatureBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;

    HRESULT hr = D3D12SerializeRootSignature(
        &rootSignatureDesc,
        D3D_ROOT_SIGNATURE_VERSION_1,
        &signatureBlob,
        &errorBlob
    );

    if (FAILED(hr)) {
        if (errorBlob) {
            OutputDebugStringA(
                static_cast<const char*>(
                    errorBlob->GetBufferPointer()
                    )
            );
        }
        return false;
    }

    hr = device_->CreateRootSignature(
        0,
        signatureBlob->GetBufferPointer(),
        signatureBlob->GetBufferSize(),
        IID_PPV_ARGS(&rootSignature_)
    );

    return SUCCEEDED(hr);
}


Microsoft::WRL::ComPtr<IDxcBlob>
ParticleManager::CompileParticleShader(
    const std::wstring& filePath,
    const wchar_t* profile
) {
    Microsoft::WRL::ComPtr<IDxcBlobEncoding> source;

    HRESULT hr = dxcUtils_->LoadFile(
        filePath.c_str(),
        nullptr,
        &source
    );

    if (FAILED(hr)) {
        return nullptr;
    }

    DxcBuffer sourceBuffer{};
    sourceBuffer.Ptr = source->GetBufferPointer();
    sourceBuffer.Size = source->GetBufferSize();
    sourceBuffer.Encoding = DXC_CP_UTF8;

    LPCWSTR arguments[] = {
        filePath.c_str(),
        L"-E", L"main",
        L"-T", profile,
        L"-Zi",
        L"-Qembed_debug",
        L"-Od",
        L"-Zpr"
    };

    Microsoft::WRL::ComPtr<IDxcResult> result;

    hr = dxcCompiler_->Compile(
        &sourceBuffer,
        arguments,
        _countof(arguments),
        includeHandler_,
        IID_PPV_ARGS(&result)
    );

    if (FAILED(hr)) {
        return nullptr;
    }

    Microsoft::WRL::ComPtr<IDxcBlobUtf8> errors;

    result->GetOutput(
        DXC_OUT_ERRORS,
        IID_PPV_ARGS(&errors),
        nullptr
    );

    if (errors && errors->GetStringLength() > 0) {
        OutputDebugStringA(errors->GetStringPointer());
    }

    HRESULT status = E_FAIL;
    hr = result->GetStatus(&status);

    if (FAILED(hr) || FAILED(status)) {
        return nullptr;
    }

    Microsoft::WRL::ComPtr<IDxcBlob> shaderBlob;

    hr = result->GetOutput(
        DXC_OUT_OBJECT,
        IID_PPV_ARGS(&shaderBlob),
        nullptr
    );

    if (FAILED(hr)) {
        return nullptr;
    }

    return shaderBlob;
}


bool ParticleManager::CreatePipelineState() {

    // ==========================================
    // Shader
    // ==========================================

    auto vertexShader = CompileParticleShader(
        vertexShaderPath_,
        L"vs_6_0"
    );

    auto pixelShader = CompileParticleShader(
        pixelShaderPath_,
        L"ps_6_0"
    );

    if (!vertexShader || !pixelShader) {
        return false;
    }

    // ==========================================
    // InputLayout
    // ==========================================

    D3D12_INPUT_ELEMENT_DESC inputElements[3]{};

    inputElements[0].SemanticName = "POSITION";
    inputElements[0].SemanticIndex = 0;
    inputElements[0].Format =
        DXGI_FORMAT_R32G32B32A32_FLOAT;
    inputElements[0].InputSlot = 0;
    inputElements[0].AlignedByteOffset =
        D3D12_APPEND_ALIGNED_ELEMENT;
    inputElements[0].InputSlotClass =
        D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;

    inputElements[1].SemanticName = "TEXCOORD";
    inputElements[1].SemanticIndex = 0;
    inputElements[1].Format =
        DXGI_FORMAT_R32G32_FLOAT;
    inputElements[1].InputSlot = 0;
    inputElements[1].AlignedByteOffset =
        D3D12_APPEND_ALIGNED_ELEMENT;
    inputElements[1].InputSlotClass =
        D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;

    inputElements[2].SemanticName = "NORMAL";
    inputElements[2].SemanticIndex = 0;
    inputElements[2].Format =
        DXGI_FORMAT_R32G32B32_FLOAT;
    inputElements[2].InputSlot = 0;
    inputElements[2].AlignedByteOffset =
        D3D12_APPEND_ALIGNED_ELEMENT;
    inputElements[2].InputSlotClass =
        D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;

    // ==========================================
    // BlendState
    // ==========================================

    D3D12_BLEND_DESC blendDesc{};

    auto& target = blendDesc.RenderTarget[0];

    target.RenderTargetWriteMask =
        D3D12_COLOR_WRITE_ENABLE_ALL;

    target.BlendEnable = TRUE;

    target.SrcBlend = D3D12_BLEND_SRC_ALPHA;
    target.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    target.BlendOp = D3D12_BLEND_OP_ADD;

    target.SrcBlendAlpha = D3D12_BLEND_ONE;
    target.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
    target.BlendOpAlpha = D3D12_BLEND_OP_ADD;

    // ==========================================
    // RasterizerState
    // ==========================================

    D3D12_RASTERIZER_DESC rasterizerDesc{};

    rasterizerDesc.FillMode =
        D3D12_FILL_MODE_SOLID;

    rasterizerDesc.CullMode =
        D3D12_CULL_MODE_NONE;

    rasterizerDesc.DepthClipEnable = TRUE;

    // ==========================================
    // DepthStencilState
    // ==========================================

    D3D12_DEPTH_STENCIL_DESC depthDesc{};

    depthDesc.DepthEnable = TRUE;

    depthDesc.DepthWriteMask =
        D3D12_DEPTH_WRITE_MASK_ZERO;

    depthDesc.DepthFunc =
        D3D12_COMPARISON_FUNC_LESS_EQUAL;

    depthDesc.StencilEnable = FALSE;

    // ==========================================
    // GraphicsPipelineState
    // ==========================================

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};

    psoDesc.pRootSignature = rootSignature_.Get();

    psoDesc.InputLayout = {
        inputElements,
        _countof(inputElements)
    };

    psoDesc.VS = {
        vertexShader->GetBufferPointer(),
        vertexShader->GetBufferSize()
    };

    psoDesc.PS = {
        pixelShader->GetBufferPointer(),
        pixelShader->GetBufferSize()
    };

    psoDesc.BlendState = blendDesc;
    psoDesc.RasterizerState = rasterizerDesc;
    psoDesc.DepthStencilState = depthDesc;

    psoDesc.NumRenderTargets = 1;

    psoDesc.RTVFormats[0] =
        DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;

    psoDesc.DSVFormat =
        DXGI_FORMAT_D24_UNORM_S8_UINT;

    psoDesc.PrimitiveTopologyType =
        D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

    psoDesc.SampleDesc.Count = 1;
    psoDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;

    HRESULT hr = device_->CreateGraphicsPipelineState(
        &psoDesc,
        IID_PPV_ARGS(&pipelineState_)
    );

    return SUCCEEDED(hr);
}
