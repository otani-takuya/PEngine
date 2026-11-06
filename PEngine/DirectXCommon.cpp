
#include "DirectXCommon.h"

#include <array>
#include <cassert>
#include <format>
#include <string>

// ==============================
// 初期化
// ==============================
bool DirectXCommon::Initialize(std::ostream& logStream) {

    // DXGIファクトリ生成
    if (!InitializeDXGIFactory()) {
        return false;
    }

    // GPUアダプタ選択
    if (!SelectAdapter(logStream)) {
        return false;
    }

    // D3D12デバイス生成
    if (!InitializeDevice(logStream)) {
        return false;
    }

    // Command関連生成
    if (!InitializeCommand()) {
        return false;
    }

    return true;
}

// ==============================
// DXGIファクトリ生成
// ==============================
bool DirectXCommon::InitializeDXGIFactory() {

    HRESULT hr = CreateDXGIFactory(
        IID_PPV_ARGS(&factory_)
    );

    assert(SUCCEEDED(hr));

    return SUCCEEDED(hr);
}

// ==============================
// GPUアダプタ選択
// ==============================
bool DirectXCommon::SelectAdapter(
    std::ostream& logStream) {

    // 高性能GPUを優先して取得
    for (UINT i = 0;; ++i) {

        adapter_.Reset();

        HRESULT hr =
            factory_->EnumAdapterByGpuPreference(
                i,
                DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                IID_PPV_ARGS(&adapter_)
            );

        // アダプタがなくなったら終了
        if (hr == DXGI_ERROR_NOT_FOUND) {
            break;
        }

        if (FAILED(hr)) {
            return false;
        }

        DXGI_ADAPTER_DESC3 desc{};
        hr = adapter_->GetDesc3(&desc);

        if (FAILED(hr)) {
            return false;
        }

        // ソフトウェアアダプタは除外
        if (desc.Flags & DXGI_ADAPTER_FLAG3_SOFTWARE) {
            continue;
        }

        // 使用するGPUをログへ出力
        logStream << "Use Adapter : ";

        // GPU名はワイド文字列なので、
        // 既存の文字列変換関数への依存を避けて
        // この段階ではGPU名の出力を省略する
        logStream << "Hardware Adapter Selected\n";

        return true;
    }

    adapter_.Reset();

    return false;
}

// ==============================
// D3D12デバイス生成
// ==============================
bool DirectXCommon::InitializeDevice(
    std::ostream& logStream) {

    // 既存のPEngineと同じ順番
    constexpr std::array featureLevels = {
        D3D_FEATURE_LEVEL_12_2,
        D3D_FEATURE_LEVEL_12_1,
        D3D_FEATURE_LEVEL_12_0
    };

    constexpr std::array featureLevelStrings = {
        "12.2",
        "12.1",
        "12.0"
    };

    for (size_t i = 0; i < featureLevels.size(); ++i) {

        device_.Reset();

        HRESULT hr = D3D12CreateDevice(
            adapter_.Get(),
            featureLevels[i],
            IID_PPV_ARGS(&device_)
        );

        if (SUCCEEDED(hr)) {

            logStream << std::format(
                "Feature Level {} supported\n",
                featureLevelStrings[i]
            );

            return true;
        }
    }

    return false;
}

// ==============================
// Command関連の生成
// ==============================
bool DirectXCommon::InitializeCommand() {

    HRESULT hr = S_OK;

    // ==============================
    // CommandQueue
    // ==============================

    D3D12_COMMAND_QUEUE_DESC queueDesc{};

    // DIRECTタイプを使用
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

    hr = device_->CreateCommandQueue(
        &queueDesc,
        IID_PPV_ARGS(&commandQueue_)
    );

    if (FAILED(hr)) {
        return false;
    }

    // ==============================
    // CommandAllocator
    // ==============================

    hr = device_->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        IID_PPV_ARGS(&commandAllocator_)
    );

    if (FAILED(hr)) {
        return false;
    }

    // ==============================
    // CommandList
    // ==============================

    hr = device_->CreateCommandList(
        0,
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        commandAllocator_.Get(),
        nullptr,
        IID_PPV_ARGS(&commandList_)
    );

    if (FAILED(hr)) {
        return false;
    }

    // 既存のmain.cppと同じく、
    // 初期状態ではCommandListを開いたままにする

    return true;
}

// ==============================
// 終了処理
// ==============================
void DirectXCommon::Finalize() {
    WaitForGPU();
    if (fenceEvent_) { CloseHandle(fenceEvent_); fenceEvent_ = nullptr; }
    fence_.Reset();
    for (auto& buffer : backBuffers_) { buffer.Reset(); }
    dsvHeap_.Reset(); rtvHeap_.Reset(); swapChain_.Reset();
    frameQueue_ = nullptr; frameAllocator_ = nullptr; frameList_ = nullptr;
    isDrawing_ = false;


    // 参照を明示的に解放
    commandList_.Reset();
    commandAllocator_.Reset();
    commandQueue_.Reset();

    device_.Reset();
    adapter_.Reset();
    factory_.Reset();
}


// ==============================
// SwapChain / RTV / DSV / Fenceを所有する
// ==============================
bool DirectXCommon::InitializeFrameResources(IDXGIFactory7* factory, ID3D12Device* device,
    ID3D12CommandQueue* queue, ID3D12CommandAllocator* allocator,
    ID3D12GraphicsCommandList* list, HWND hwnd, UINT width, UINT height,
    ID3D12Resource* depthStencil) {
    if (!factory || !device || !queue || !allocator || !list || !hwnd || !depthStencil) return false;
    frameQueue_ = queue; frameAllocator_ = allocator; frameList_ = list;
    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width = width; desc.Height = height;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = kBackBufferCount;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    Microsoft::WRL::ComPtr<IDXGISwapChain1> baseSwapChain;
    if (FAILED(factory->CreateSwapChainForHwnd(queue, hwnd, &desc, nullptr, nullptr, &baseSwapChain))) return false;
    if (FAILED(baseSwapChain.As(&swapChain_))) return false;

    D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    heapDesc.NumDescriptors = kBackBufferCount;
    if (FAILED(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&rtvHeap_)))) return false;
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    heapDesc.NumDescriptors = 1;
    if (FAILED(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&dsvHeap_)))) return false;
    rtvDescriptorSize_ = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
    rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    for (UINT i = 0; i < kBackBufferCount; ++i) {
        if (FAILED(swapChain_->GetBuffer(i, IID_PPV_ARGS(&backBuffers_[i])))) return false;
        auto handle = rtvHeap_->GetCPUDescriptorHandleForHeapStart();
        handle.ptr += static_cast<SIZE_T>(i) * rtvDescriptorSize_;
        device->CreateRenderTargetView(backBuffers_[i].Get(), &rtvDesc, handle);
    }
    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
    dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    dsvHandle_ = dsvHeap_->GetCPUDescriptorHandleForHeapStart();
    device->CreateDepthStencilView(depthStencil, &dsvDesc, dsvHandle_);
    if (FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_)))) return false;
    fenceEvent_ = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    return fenceEvent_ != nullptr;
}

bool DirectXCommon::WaitForGPU() {
    if (!fence_) return true;
    if (!fenceEvent_) return false;
    if (fence_->GetCompletedValue() >= fenceValue_) return true;
    if (FAILED(fence_->SetEventOnCompletion(fenceValue_, fenceEvent_))) return false;
    return WaitForSingleObject(fenceEvent_, INFINITE) == WAIT_OBJECT_0;
}

bool DirectXCommon::SignalAndWait() {
    if (!frameQueue_ || !fence_) return false;
    const uint64_t next = fenceValue_ + 1;
    if (FAILED(frameQueue_->Signal(fence_.Get(), next))) return false;
    fenceValue_ = next;
    return WaitForGPU();
}

bool DirectXCommon::PreDraw() {
    if (isDrawing_ || !swapChain_ || !frameAllocator_ || !frameList_ || !WaitForGPU()) return false;
    if (FAILED(frameAllocator_->Reset())) return false;
    if (FAILED(frameList_->Reset(frameAllocator_, nullptr))) return false;
    frameBackBufferIndex_ = swapChain_->GetCurrentBackBufferIndex();
    if (frameBackBufferIndex_ >= kBackBufferCount) return false;
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = backBuffers_[frameBackBufferIndex_].Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    frameList_->ResourceBarrier(1, &barrier);
    auto rtv = rtvHeap_->GetCPUDescriptorHandleForHeapStart();
    rtv.ptr += static_cast<SIZE_T>(frameBackBufferIndex_) * rtvDescriptorSize_;
    frameList_->OMSetRenderTargets(1, &rtv, FALSE, &dsvHandle_);
    frameList_->ClearDepthStencilView(dsvHandle_, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
    constexpr float clearColor[] = { 0.1f, 0.25f, 0.5f, 1.0f };
    frameList_->ClearRenderTargetView(rtv, clearColor, 0, nullptr);
    isDrawing_ = true;
    return true;
}

bool DirectXCommon::PostDraw() {
    if (!isDrawing_ || !frameList_ || !frameQueue_ || !swapChain_) return false;
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = backBuffers_[frameBackBufferIndex_].Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    frameList_->ResourceBarrier(1, &barrier);
    if (FAILED(frameList_->Close())) return false;
    ID3D12CommandList* lists[] = { frameList_ };
    frameQueue_->ExecuteCommandLists(1, lists);
    HRESULT presentResult = swapChain_->Present(1, 0);
    const uint64_t next = fenceValue_ + 1;
    HRESULT signalResult = frameQueue_->Signal(fence_.Get(), next);
    if (SUCCEEDED(signalResult)) fenceValue_ = next;
    isDrawing_ = false;
    return SUCCEEDED(presentResult) && SUCCEEDED(signalResult);
}
