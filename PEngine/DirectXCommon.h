
#pragma once

#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl.h>

#include <cassert>
#include <ostream>
#include <cstdint>

class WinApp;

// ==============================
// DirectX12共通管理クラス
// ==============================
class DirectXCommon {
public:

    // ==============================
    // 初期化・終了
    // ==============================

    // DXGI、Device、Command関連を初期化
    bool Initialize(
        WinApp* winApp,
        std::ostream& logStream
    );

    // DirectX12の基本オブジェクトを解放
    void Finalize();

    // ==============================
    // Getter
    // ==============================

    IDXGIFactory7* GetFactory() const {
        return factory_.Get();
    }

    IDXGIAdapter4* GetAdapter() const {
        return adapter_.Get();
    }

    ID3D12Device* GetDevice() const {
        return device_.Get();
    }

    ID3D12CommandQueue* GetCommandQueue() const {
        return commandQueue_.Get();
    }

    ID3D12CommandAllocator* GetCommandAllocator() const {
        return commandAllocator_.Get();
    }

    ID3D12GraphicsCommandList* GetCommandList() const {
        return commandList_.Get();
    }

    UINT GetDescriptorSizeRTV() const {
        return descriptorSizeRTV_;
    }

    UINT GetDescriptorSizeDSV() const {
        return descriptorSizeDSV_;
    }

    // ==============================
    // SwapChain関連のGetter
    // ==============================

    // SwapChainを取得
    IDXGISwapChain4* GetSwapChain() const {
        return swapChain_.Get();
    }

    // RTVヒープを取得
    ID3D12DescriptorHeap* GetRTVHeap() const {
        return rtvHeap_.Get();
    }

    // DSVヒープを取得
    ID3D12DescriptorHeap* GetDSVHeap() const {
        return dsvHeap_.Get();
    }

    // バックバッファを取得
    ID3D12Resource* GetBackBuffer(UINT index) const {

        if (index >= kBackBufferCount) {
            return nullptr;
        }

        return swapChainResources_[index].Get();
    }

    // DepthBufferを取得
    ID3D12Resource* GetDepthStencilResource() const {
        return depthStencilResource_.Get();
    }

    // RTVのDescriptorSizeを取得
    UINT GetRTVDescriptorSize() const {
        return descriptorSizeRTV_;
    }

    // DSVのDescriptorSizeを取得
    UINT GetDSVDescriptorSize() const {
        return descriptorSizeDSV_;
    }

    // バックバッファ数を取得
    UINT GetBackBufferCount() const {
        return kBackBufferCount;
    }

    // ==============================
    // RTVハンドル取得
    // ==============================
    D3D12_CPU_DESCRIPTOR_HANDLE GetRTVHandle(
        UINT index) const {

        D3D12_CPU_DESCRIPTOR_HANDLE handle{};

        if (!rtvHeap_ || index >= kBackBufferCount) {
            return handle;
        }

        handle =
            rtvHeap_->GetCPUDescriptorHandleForHeapStart();

        handle.ptr +=
            static_cast<SIZE_T>(index) *
            descriptorSizeRTV_;

        return handle;
    }

    // ==============================
    // DSVハンドル取得
    // ==============================
    D3D12_CPU_DESCRIPTOR_HANDLE GetDSVHandle() const {

        if (!dsvHeap_) {
            return {};
        }

        return dsvHeap_->
            GetCPUDescriptorHandleForHeapStart();
    }

    // ==============================
    // GPU同期
    // ==============================

    // GPUへFenceシグナルを送信
    bool SignalFence();

    // 最後に送信したFence値までGPUの完了を待つ
    bool WaitForGPU();

    // コマンドを実行してGPUの完了を待つ
    bool ExecuteCommandListAndWait();


private:

    // ==============================
    // 内部初期化処理
    // ==============================

    bool InitializeDXGIFactory();
    bool SelectAdapter(std::ostream& logStream);
    bool InitializeDevice(std::ostream& logStream);
    bool InitializeCommand();

    // ==============================
    // DirectX12オブジェクト
    // ==============================

    Microsoft::WRL::ComPtr<IDXGIFactory7> factory_;
    Microsoft::WRL::ComPtr<IDXGIAdapter4> adapter_;
    Microsoft::WRL::ComPtr<ID3D12Device> device_;

    Microsoft::WRL::ComPtr<ID3D12CommandQueue> commandQueue_;
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> commandAllocator_;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> commandList_;

    // ==============================
    // SwapChain
    // ==============================

    // スワップチェーン
    Microsoft::WRL::ComPtr<IDXGISwapChain4> swapChain_;

    // バックバッファ（ダブルバッファリング）
    static constexpr UINT kBackBufferCount = 2;

    Microsoft::WRL::ComPtr<ID3D12Resource>
        swapChainResources_[kBackBufferCount];

    // ==============================
    // DescriptorHeap
    // ==============================

    // RTV用DescriptorHeap
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvHeap_;

    // DSV用DescriptorHeap
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> dsvHeap_;

    // ==============================
    // DepthBuffer
    // ==============================

    Microsoft::WRL::ComPtr<ID3D12Resource> depthStencilResource_;

    // ==============================
    // DescriptorSize
    // ==============================

    UINT descriptorSizeRTV_ = 0;
    UINT descriptorSizeDSV_ = 0;

    // SwapChain生成
    bool InitializeSwapChain(HWND hwnd, UINT width, UINT height);

    // RTV生成
    bool InitializeRTV();

    // DSV・DepthBuffer生成
    bool InitializeDSV(UINT width, UINT height);

    // ==============================
    // Fence・GPU同期
    // ==============================

    // GPUの処理完了を管理するFence
    Microsoft::WRL::ComPtr<ID3D12Fence> fence_;

    // GPUへ通知するFence値
    uint64_t fenceValue_ = 0;

    // GPU完了待機用イベント
    HANDLE fenceEvent_ = nullptr;

    // Fenceとイベントを生成
    bool InitializeFence();
};
