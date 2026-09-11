#pragma once
#include <windows.h>
#include <d3d12.h>
#include <wrl/client.h>
#include <vector>
namespace fh6 {
struct ArtworkUploadResources {
    Microsoft::WRL::ComPtr<ID3D12Resource> upload;
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> allocator;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> commands;
    Microsoft::WRL::ComPtr<ID3D12Fence> fence;
    Microsoft::WRL::ComPtr<ID3D12Device> device;
    std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> targets;
    HANDLE event = nullptr;
    ~ArtworkUploadResources() { if (event) CloseHandle(event); }
    // Background worker only. Never release in-flight resources on a timeout.
    void wait_for_completion() const noexcept {
        const bool event_ok = SUCCEEDED(fence->SetEventOnCompletion(1, event));
        while (fence->GetCompletedValue() < 1 && SUCCEEDED(device->GetDeviceRemovedReason())) {
            if (event_ok) WaitForSingleObject(event, 100);
            else Sleep(10);
        }
    }
};
}
