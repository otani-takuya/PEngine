
#pragma once

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl.h>

#include <ostream>
#include <cstdint>

// ==============================
// DirectX12共通管理クラス
// ==============================
class DirectXCommon {
public:

    // ==============================
    // 初期化・終了
    // ==============================

    // DXGI、Device、Command関連を初期化
    bool Initialize(std::ostream& logStream);

    // DirectX12の基本オブジェクトを解放
    void Finalize();

    // フレーム用リソースを生成。Device/Factory/Queue/Listの所有権は移さない。
    bool InitializeFrameResources(IDXGIFactory7* factory, ID3D12Device* device,
        ID3D12CommandQueue* queue, ID3D12CommandAllocator* allocator,
        ID3D12GraphicsCommandList* list, HWND hwnd, UINT width, UINT height,
        ID3D12Resource* depthStencil);
    bool PreDraw();
    bool PostDraw();
    bool WaitForGPU();
    bool SignalAndWait();
    IDXGISwapChain4* GetSwapChain() const { return swapChain_.Get(); }
    UINT GetBackBufferCount() const { return kBackBufferCount; }
    DXGI_FORMAT GetRTVFormat() const { return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB; }

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

private:

    // ==============================
    // 内部初期化処理
    // ==============================

    bool InitializeDXGIFactory();
    bool SelectAdapter(std::ostream& logStream);
    bool InitializeDevice(std::ostream& logStream);
    bool InitializeCommand();

    static constexpr UINT kBackBufferCount = 2;
    Microsoft::WRL::ComPtr<IDXGISwapChain4> swapChain_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvHeap_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> dsvHeap_;
    Microsoft::WRL::ComPtr<ID3D12Resource> backBuffers_[kBackBufferCount];
    UINT rtvDescriptorSize_ = 0;
    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle_{};
    Microsoft::WRL::ComPtr<ID3D12Fence> fence_;
    HANDLE fenceEvent_ = nullptr;
    uint64_t fenceValue_ = 0;
    // 外部所有
    ID3D12CommandQueue* frameQueue_ = nullptr;
    ID3D12CommandAllocator* frameAllocator_ = nullptr;
    ID3D12GraphicsCommandList* frameList_ = nullptr;
    UINT frameBackBufferIndex_ = 0;
    bool isDrawing_ = false;

    // ==============================
    // DirectX12オブジェクト
    // ==============================

    Microsoft::WRL::ComPtr<IDXGIFactory7> factory_;
    Microsoft::WRL::ComPtr<IDXGIAdapter4> adapter_;
    Microsoft::WRL::ComPtr<ID3D12Device> device_;

    Microsoft::WRL::ComPtr<ID3D12CommandQueue> commandQueue_;
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> commandAllocator_;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> commandList_;
};
