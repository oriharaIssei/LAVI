#include "PreviewTextureUtil.h"

#include "Engine.h"
#include "directX12/DxCommand.h"
#include "directX12/DxDevice.h"
#include "directX12/ResourceStateTracker.h"

#include <algorithm>
#include <cstring>

namespace {

void TransitionPreviewTexture(
    OriGine::DxCommand* dxCmd,
    PreviewTexture& preview,
    D3D12_RESOURCE_STATES stateAfter) {
    if (preview.state == stateAfter) return;

    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource = preview.texture.Get();
    barrier.Transition.StateBefore = preview.state;
    barrier.Transition.StateAfter = stateAfter;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

    dxCmd->ResourceDirectBarrier(preview.texture, barrier);
    preview.state = stateAfter;
}

} // namespace

void PreviewTexture::Release(OriGine::DxSrvHeap* srvHeap) {
    if (srvDescriptor.GetGpuHandle().ptr != 0) {
        srvHeap->ReleaseDescriptor(srvDescriptor);
        srvDescriptor = OriGine::DxSrvDescriptor(0);
    }
    if (texture) {
        OriGine::ResourceStateTracker::UnregisterResource(texture.Get());
    }
    uploadBuffer.Reset();
    texture.Reset();
    width = 0;
    height = 0;
    state = D3D12_RESOURCE_STATE_COMMON;
}

bool CreatePreviewTexture(PreviewTexture& preview, uint32_t w, uint32_t h) {
    auto* engine = OriGine::Engine::GetInstance();
    auto* device = engine->GetDxDevice()->device_.Get();
    auto* srvHeap = engine->GetSrvHeap();

    preview.Release(srvHeap);
    preview.width = w;
    preview.height = h;

    D3D12_RESOURCE_DESC texDesc{};
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Width = w;
    texDesc.Height = h;
    texDesc.DepthOrArraySize = 1;
    texDesc.MipLevels = 1;
    texDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    texDesc.SampleDesc.Count = 1;
    texDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    D3D12_HEAP_PROPERTIES defaultHeap{};
    defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;

    HRESULT hr = device->CreateCommittedResource(
        &defaultHeap, D3D12_HEAP_FLAG_NONE,
        &texDesc, D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr, IID_PPV_ARGS(&preview.texture));
    if (FAILED(hr)) return false;

    OriGine::ResourceStateTracker::RegisterResource(preview.texture.Get(), D3D12_RESOURCE_STATE_COPY_DEST);
    preview.state = D3D12_RESOURCE_STATE_COPY_DEST;

    UINT64 uploadSize = 0;
    device->GetCopyableFootprints(&texDesc, 0, 1, 0, nullptr, nullptr, nullptr, &uploadSize);

    D3D12_RESOURCE_DESC bufDesc{};
    bufDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufDesc.Width = uploadSize;
    bufDesc.Height = 1;
    bufDesc.DepthOrArraySize = 1;
    bufDesc.MipLevels = 1;
    bufDesc.Format = DXGI_FORMAT_UNKNOWN;
    bufDesc.SampleDesc.Count = 1;
    bufDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    D3D12_HEAP_PROPERTIES uploadHeap{};
    uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;

    hr = device->CreateCommittedResource(
        &uploadHeap, D3D12_HEAP_FLAG_NONE,
        &bufDesc, D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr, IID_PPV_ARGS(&preview.uploadBuffer));
    if (FAILED(hr)) return false;

    preview.srvDescriptor = srvHeap->AllocateDescriptor();

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MipLevels = 1;
    device->CreateShaderResourceView(preview.texture.Get(), &srvDesc, preview.srvDescriptor.GetCpuHandle());

    return true;
}

void UploadPreviewFrame(PreviewTexture& preview, const uint8_t* data, uint32_t dataSize, uint32_t w, uint32_t h) {
    if (!preview.texture || w != preview.width || h != preview.height) {
        if (!CreatePreviewTexture(preview, w, h)) return;
    }

    auto* engine = OriGine::Engine::GetInstance();
    auto* device = engine->GetDxDevice()->device_.Get();
    auto* dxCmd = engine->GetDxCommand();
    auto* cmdList = dxCmd->GetCommandList().Get();

    D3D12_RESOURCE_DESC texDesc = preview.texture->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    UINT numRows = 0;
    UINT64 rowSizeInBytes = 0;
    device->GetCopyableFootprints(&texDesc, 0, 1, 0, &footprint, &numRows, &rowSizeInBytes, nullptr);

    uint8_t* mapped = nullptr;
    D3D12_RANGE readRange{0, 0};
    if (FAILED(preview.uploadBuffer->Map(0, &readRange, reinterpret_cast<void**>(&mapped)))) return;

    uint32_t srcRowPitch = w * 4;
    uint32_t availableRows = srcRowPitch > 0 ? dataSize / srcRowPitch : 0;
    uint32_t rowsToCopy = (std::min)(static_cast<uint32_t>(numRows), availableRows);
    for (uint32_t row = 0; row < rowsToCopy; ++row) {
        memcpy(mapped + row * footprint.Footprint.RowPitch, data + row * srcRowPitch, srcRowPitch);
    }
    preview.uploadBuffer->Unmap(0, nullptr);

    TransitionPreviewTexture(dxCmd, preview, D3D12_RESOURCE_STATE_COPY_DEST);

    D3D12_TEXTURE_COPY_LOCATION dst{};
    dst.pResource = preview.texture.Get();
    dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst.SubresourceIndex = 0;

    D3D12_TEXTURE_COPY_LOCATION src{};
    src.pResource = preview.uploadBuffer.Get();
    src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src.PlacedFootprint = footprint;

    cmdList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

    TransitionPreviewTexture(dxCmd, preview, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
}
