#pragma once

#include <wrl.h>
#include <d3d12.h>

#include "directX12/DxDescriptor.h"

struct PreviewTexture {
    Microsoft::WRL::ComPtr<ID3D12Resource> texture;
    Microsoft::WRL::ComPtr<ID3D12Resource> uploadBuffer;
    OriGine::DxSrvDescriptor srvDescriptor{0};
    uint32_t width = 0;
    uint32_t height = 0;
    D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;

    void Release(OriGine::DxSrvHeap* srvHeap);
};

bool CreatePreviewTexture(PreviewTexture& preview, uint32_t width, uint32_t height);
void UploadPreviewFrame(PreviewTexture& preview, const uint8_t* data, uint32_t dataSize, uint32_t width, uint32_t height);
