#include "fh6/gpu_upload_completion.hpp"
#include "fh6/single_flight_worker.hpp"
#include <chrono>
#include <cstdio>
#include <memory>
#include <cstring>
using Microsoft::WRL::ComPtr;
void check(HRESULT hr) { if (FAILED(hr)) { std::printf("D3D12 failure: 0x%08lx\n", (unsigned long)hr); std::exit(2); } }
int main() {
    ComPtr<ID3D12Device> device;
    check(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)));
    ComPtr<ID3D12CommandQueue> queue;
    D3D12_COMMAND_QUEUE_DESC desc{};
    check(device->CreateCommandQueue(&desc, IID_PPV_ARGS(&queue)));
    auto resources = std::make_shared<fh6::ArtworkUploadResources>();
    resources->device = device;
    check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&resources->allocator)));
    check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, resources->allocator.Get(), nullptr, IID_PPV_ARGS(&resources->commands)));
    check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&resources->fence)));
    resources->event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!resources->event) return 2;
    D3D12_RESOURCE_DESC buffer{};
    buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    buffer.Width = 4096; buffer.Height = 1; buffer.DepthOrArraySize = 1;
    buffer.MipLevels = 1; buffer.SampleDesc.Count = 1; buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    D3D12_HEAP_PROPERTIES heap{}; heap.Type = D3D12_HEAP_TYPE_UPLOAD;
    check(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buffer,
          D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&resources->upload)));
    unsigned char* mapped;
    check(resources->upload->Map(0, nullptr, (void**)&mapped));
    std::memset(mapped, 0xA5, 4096); resources->upload->Unmap(0, nullptr);
    heap.Type = D3D12_HEAP_TYPE_READBACK;
    ComPtr<ID3D12Resource> readback;
    check(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buffer,
          D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback)));
    resources->targets.push_back(readback);
    resources->commands->CopyBufferRegion(readback.Get(), 0, resources->upload.Get(), 0, 4096);
    check(resources->commands->Close());
    ComPtr<ID3D12Fence> gate;
    check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&gate)));
    check(queue->Wait(gate.Get(), 1)); // GPU cannot complete until CPU opens the gate
    ID3D12CommandList* commands[] = {resources->commands.Get()};
    queue->ExecuteCommandLists(1, commands);
    check(queue->Signal(resources->fence.Get(), 1));
    fh6::SingleFlightWorker worker;
    if (!worker.try_reserve()) return 3;
    std::weak_ptr<fh6::ArtworkUploadResources> lifetime = resources;
    auto start = std::chrono::steady_clock::now();
    worker.dispatch([resources] { resources->wait_for_completion(); });
    resources.reset();
    auto submit_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    if (!worker.busy() || lifetime.expired()) return 4;
    check(gate->Signal(1));
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (worker.busy() && std::chrono::steady_clock::now() < deadline) Sleep(1);
    if (worker.busy() || !lifetime.expired()) std::exit(5);
    check(readback->Map(0, nullptr, (void**)&mapped));
    for (int i = 0; i < 4096; ++i) if (mapped[i] != 0xA5) return 6;
    readback->Unmap(0, nullptr);
    std::printf("PASS: real GPU copy, retained until fence, correct readback; completion dispatch %.3f ms\n", submit_ms);
}
