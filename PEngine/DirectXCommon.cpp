
#include "DirectXCommon.h"

#include <array>
#include <cassert>
#include <format>
#include <string>

#include "WinApp.h"

// ==============================
// 初期化
// ==============================
bool DirectXCommon::Initialize(
    WinApp* winApp,
    std::ostream& logStream) {

    // WinAppの有効性確認
    if (winApp == nullptr) {
        return false;
    }

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

    // ウィンドウ情報を取得
    HWND hwnd = winApp->GetHwnd();

    constexpr UINT width = WinApp::kClientWidth;
    constexpr UINT height = WinApp::kClientHeight;

    // SwapChain生成
    if (!InitializeSwapChain(hwnd, width, height)) {
        return false;
    }

    // RTV生成
    if (!InitializeRTV()) {
        return false;
    }

    // DSV・DepthBuffer生成
    if (!InitializeDSV(width, height)) {
        return false;
    }

    // DSV・DepthBuffer生成
    if (!InitializeDSV(width, height)) {
        return false;
    }

    // ==============================
    // Fence初期化
    // ==============================
    if (!InitializeFence()) {
        return false;
    }

    return true;

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
// SwapChain生成
// ==============================
bool DirectXCommon::InitializeSwapChain(
    HWND hwnd,
    UINT width,
    UINT height) {

    if (!factory_ || !commandQueue_ || !hwnd ||
        width == 0 || height == 0) {
        return false;
    }

    // SwapChainの設定
    DXGI_SWAP_CHAIN_DESC1 swapChainDesc{};

    swapChainDesc.Width = width;
    swapChainDesc.Height = height;
    swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDesc.SampleDesc.Count = 1;
    swapChainDesc.BufferUsage =
        DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.BufferCount = kBackBufferCount;
    swapChainDesc.SwapEffect =
        DXGI_SWAP_EFFECT_FLIP_DISCARD;

    // 一度IDXGISwapChain1として生成
    Microsoft::WRL::ComPtr<IDXGISwapChain1>
        swapChain1;

    HRESULT hr = factory_->CreateSwapChainForHwnd(
        commandQueue_.Get(),
        hwnd,
        &swapChainDesc,
        nullptr,
        nullptr,
        &swapChain1
    );

    if (FAILED(hr)) {
        return false;
    }

    // IDXGISwapChain4へ変換
    hr = swapChain1.As(&swapChain_);

    if (FAILED(hr)) {
        return false;
    }

    return true;
}

// ==============================
// RTV生成
// ==============================
bool DirectXCommon::InitializeRTV() {

    if (!device_ || !swapChain_) {
        return false;
    }

    // RTV用DescriptorHeap設定
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};

    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    heapDesc.NumDescriptors = kBackBufferCount;
    heapDesc.Flags =
        D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

    // DescriptorHeap生成
    HRESULT hr = device_->CreateDescriptorHeap(
        &heapDesc,
        IID_PPV_ARGS(&rtvHeap_)
    );

    if (FAILED(hr)) {
        return false;
    }

    // RTVのDescriptorSize取得
    descriptorSizeRTV_ =
        device_->GetDescriptorHandleIncrementSize(
            D3D12_DESCRIPTOR_HEAP_TYPE_RTV
        );

    // RTVの設定
    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};

    rtvDesc.Format =
        DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;

    rtvDesc.ViewDimension =
        D3D12_RTV_DIMENSION_TEXTURE2D;

    // RTVヒープ先頭のCPUハンドル
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle =
        rtvHeap_->GetCPUDescriptorHandleForHeapStart();

    // バックバッファ2枚分のRTVを生成
    for (UINT i = 0; i < kBackBufferCount; ++i) {

        // SwapChainからバックバッファ取得
        hr = swapChain_->GetBuffer(
            i,
            IID_PPV_ARGS(&swapChainResources_[i])
        );

        if (FAILED(hr)) {
            return false;
        }

        // RTVを作成
        device_->CreateRenderTargetView(
            swapChainResources_[i].Get(),
            &rtvDesc,
            rtvHandle
        );

        // 次のDescriptorへ移動
        rtvHandle.ptr += descriptorSizeRTV_;
    }

    return true;
}

// ==============================
// DSV・DepthBuffer生成
// ==============================
bool DirectXCommon::InitializeDSV(
    UINT width,
    UINT height) {

    if (!device_ || width == 0 || height == 0) {
        return false;
    }

    // ==============================
    // DepthBufferの設定
    // ==============================

    D3D12_RESOURCE_DESC resourceDesc{};

    resourceDesc.Dimension =
        D3D12_RESOURCE_DIMENSION_TEXTURE2D;

    resourceDesc.Width = width;
    resourceDesc.Height = height;
    resourceDesc.DepthOrArraySize = 1;
    resourceDesc.MipLevels = 1;

    resourceDesc.Format =
        DXGI_FORMAT_D24_UNORM_S8_UINT;

    resourceDesc.SampleDesc.Count = 1;

    resourceDesc.Layout =
        D3D12_TEXTURE_LAYOUT_UNKNOWN;

    resourceDesc.Flags =
        D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    // GPUメモリに配置
    D3D12_HEAP_PROPERTIES heapProperties{};

    heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;

    // 深度値の初期設定
    D3D12_CLEAR_VALUE clearValue{};

    clearValue.Format =
        DXGI_FORMAT_D24_UNORM_S8_UINT;

    clearValue.DepthStencil.Depth = 1.0f;
    clearValue.DepthStencil.Stencil = 0;

    // DepthBuffer生成
    HRESULT hr = device_->CreateCommittedResource(
        &heapProperties,
        D3D12_HEAP_FLAG_NONE,
        &resourceDesc,
        D3D12_RESOURCE_STATE_DEPTH_WRITE,
        &clearValue,
        IID_PPV_ARGS(&depthStencilResource_)
    );

    if (FAILED(hr)) {
        return false;
    }

    // ==============================
    // DSV用DescriptorHeap生成
    // ==============================

    D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};

    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    heapDesc.NumDescriptors = 1;
    heapDesc.Flags =
        D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

    hr = device_->CreateDescriptorHeap(
        &heapDesc,
        IID_PPV_ARGS(&dsvHeap_)
    );

    if (FAILED(hr)) {
        return false;
    }

    // DSVのDescriptorSize取得
    descriptorSizeDSV_ =
        device_->GetDescriptorHandleIncrementSize(
            D3D12_DESCRIPTOR_HEAP_TYPE_DSV
        );

    // ==============================
    // DSV生成
    // ==============================

    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};

    dsvDesc.Format =
        DXGI_FORMAT_D24_UNORM_S8_UINT;

    dsvDesc.ViewDimension =
        D3D12_DSV_DIMENSION_TEXTURE2D;

    // DepthBufferとDSVを関連付け
    device_->CreateDepthStencilView(
        depthStencilResource_.Get(),
        &dsvDesc,
        dsvHeap_->GetCPUDescriptorHandleForHeapStart()
    );

    return true;
}


// ==============================
// Fence生成
// ==============================
bool DirectXCommon::InitializeFence() {

    if (!device_) {
        return false;
    }

    // Fenceの初期値
    fenceValue_ = 0;

    // Fence生成
    HRESULT hr = device_->CreateFence(
        fenceValue_,
        D3D12_FENCE_FLAG_NONE,
        IID_PPV_ARGS(&fence_)
    );

    if (FAILED(hr)) {
        return false;
    }

    // GPU処理完了通知用イベント生成
    fenceEvent_ = CreateEvent(
        nullptr,
        FALSE,
        FALSE,
        nullptr
    );

    if (fenceEvent_ == nullptr) {
        return false;
    }

    return true;
}


// ==============================
// Fenceシグナル送信
// ==============================
bool DirectXCommon::SignalFence() {

    if (!commandQueue_ || !fence_) {
        return false;
    }

    // 次のFence値を設定
    const uint64_t nextFenceValue =
        fenceValue_ + 1;

    // GPUのコマンドキューにシグナルを登録
    HRESULT hr = commandQueue_->Signal(
        fence_.Get(),
        nextFenceValue
    );

    if (FAILED(hr)) {
        return false;
    }

    // シグナル登録成功後に値を更新
    fenceValue_ = nextFenceValue;

    return true;
}

// ==============================
// GPU処理完了待機
// ==============================
bool DirectXCommon::WaitForGPU() {

    if (!fence_ || !fenceEvent_) {
        return false;
    }

    // GPUが指定Fence値まで処理済みなら待たない
    if (fence_->GetCompletedValue() >= fenceValue_) {
        return true;
    }

    // GPU処理完了時にイベントを発生させる
    HRESULT hr = fence_->SetEventOnCompletion(
        fenceValue_,
        fenceEvent_
    );

    if (FAILED(hr)) {
        return false;
    }

    // GPUの完了を待機
    DWORD waitResult = WaitForSingleObject(
        fenceEvent_,
        INFINITE
    );

    return waitResult == WAIT_OBJECT_0;
}

// ==============================
// コマンド実行・GPU完了待機
// ==============================
bool DirectXCommon::ExecuteCommandListAndWait() {

    if (!commandList_ || !commandQueue_) {
        return false;
    }

    // 記録中のCommandListを閉じる
    HRESULT hr = commandList_->Close();

    if (FAILED(hr)) {
        return false;
    }

    // CommandListをGPUへ送信
    ID3D12CommandList* commandLists[] = {
        commandList_.Get()
    };

    commandQueue_->ExecuteCommandLists(
        1,
        commandLists
    );

    // GPUへFenceシグナル送信
    if (!SignalFence()) {
        return false;
    }

    // GPUの完了を待機
    return WaitForGPU();
}

// ==============================
// 終了処理
// ==============================
void DirectXCommon::Finalize() {

    // ==============================
    // GPU完了待機
    // ==============================

    // 最後に送信したコマンドの完了を待つ
    if (fence_ && fenceEvent_) {
        const bool result = WaitForGPU();
        assert(result);

        // GPU待機失敗時はリソースを安全に解放できない
        if (!result) {
            return;
        }
    }

    // ==============================
    // Fence関連の解放
    // ==============================

    fence_.Reset();

    if (fenceEvent_ != nullptr) {
        CloseHandle(fenceEvent_);
        fenceEvent_ = nullptr;
    }

    fenceValue_ = 0;

    // ==============================
    // DepthBuffer・DescriptorHeap
    // ==============================

    depthStencilResource_.Reset();

    dsvHeap_.Reset();
    rtvHeap_.Reset();

    // バックバッファ解放
    for (UINT i = 0; i < kBackBufferCount; ++i) {
        swapChainResources_[i].Reset();
    }

    swapChain_.Reset();

    // ==============================
    // Command関連
    // ==============================

    commandList_.Reset();
    commandAllocator_.Reset();
    commandQueue_.Reset();

    // ==============================
    // Device・DXGI
    // ==============================

    device_.Reset();
    adapter_.Reset();
    factory_.Reset();
}