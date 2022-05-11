// GraphicsTest.cpp : Defines the entry point for the application.
//

#include "JcabD3D.h"

#include <shlobj.h>

#include <array>
#include <span>
#include <vector>
#include <map>
#include <filesystem>

// Shared with the shader.
#define MAX_MODEL_DEFINITIONS     100
#define MAX_INSTANCE_DEFINITIONS 1000


XMMATRIX View;
XMMATRIX Projection;
XMMATRIX InverseProjection;
XMMATRIX ViewProj;


struct VertexPosition
{
    XMFLOAT3 position;
};

struct VertexAttributes
{
    XMFLOAT3 normal;
    uint32_t color;
};

struct TriangleParameters
{
    uint32_t color;
    uint32_t patch;
};


struct Model
{
    enum class Id : uint32_t { Invalid = 0 };

    uint32_t                 TriangleCount            = 0;
    ComPtr<ID3D12Resource>   TriangleIndices          {};
    D3D12_INDEX_BUFFER_VIEW  TriangleIndicesView      {};
    ComPtr<ID3D12Resource>   TriangleParametersBuffer {};

    uint32_t                 VertexCount              = 0;
    ComPtr<ID3D12Resource>   VertexPositionsBuffer    {};
    D3D12_VERTEX_BUFFER_VIEW VertexPositionsBufferView{};
    ComPtr<ID3D12Resource>   VertexAttributesBuffer   {};
};

using TriangleIndices = std::array<uint32_t, 3>;

struct ModelVerticesDefinition
{
    UINT                                   Count;
    std::function<void(VertexPosition*  )> PositionsFiller;
    std::function<void(VertexAttributes*)> AttributesFiller;

    ModelVerticesDefinition() = default;
    ModelVerticesDefinition(std::span<VertexPosition const> positions, std::span<VertexAttributes const> attributes)
        : Count           { static_cast<UINT>(positions.size()) }
        , PositionsFiller { [positions ](VertexPosition*   p){ memcpy(p, positions .data(), positions .size() * sizeof(positions [0])); } }
        , AttributesFiller{ [attributes](VertexAttributes* p){ memcpy(p, attributes.data(), attributes.size() * sizeof(attributes[0])); } }
    {
        assert(Count = positions .size()); // No slicing of the Count.
        assert(Count = attributes.size()); // And both spans must line up.
    }
};

struct ModelTrianglesDefinition
{
    UINT                                     Count;
    std::function<void(TriangleIndices*   )> IndicesFiller;
    std::function<void(TriangleParameters*)> ParametersFiller;

    ModelTrianglesDefinition() = default;
    ModelTrianglesDefinition(std::span<TriangleIndices const> indices, std::span<TriangleParameters const> parameters)
        : Count           { static_cast<UINT>(indices.size()) }
        , IndicesFiller   { [indices   ](TriangleIndices*    p){ memcpy(p, indices   .data(), indices   .size() * sizeof(indices   [0])); } }
        , ParametersFiller{ [parameters](TriangleParameters* p){ memcpy(p, parameters.data(), parameters.size() * sizeof(parameters[0])); } }
    {
        assert(Count = indices   .size()); // No slicing of the Count.
        assert(Count = parameters.size()); // And both spans must line up.
    }
};

Model CreateModel(AssetUploader& uploader, RenderDevice& renderDevice, ModelTrianglesDefinition const& triangles, ModelVerticesDefinition const& vertices)
{
    Model model{};

    model.TriangleCount = triangles.Count;
    model.VertexCount   = vertices .Count;

    auto const indicesSize = sizeof(TriangleIndices) * triangles.Count;
    model.TriangleIndices = uploader.CreateStaticBuffer(indicesSize, D3D12_RESOURCE_STATE_INDEX_BUFFER,
        [&](void* p)
        {
            triangles.IndicesFiller(static_cast<TriangleIndices*>(p));
        }
    );

    model.TriangleIndicesView.BufferLocation = model.TriangleIndices->GetGPUVirtualAddress();
    model.TriangleIndicesView.Format         = DXGI_FORMAT_R32_UINT;
    model.TriangleIndicesView.SizeInBytes    = indicesSize;

    model.VertexPositionsBuffer = uploader.CreateStaticBuffer(vertices.Count * sizeof(VertexPosition), D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER,
        [&](void* p)
        {
            vertices.PositionsFiller(static_cast<VertexPosition*>(p));
        }
    );

    model.VertexPositionsBufferView.BufferLocation = model.VertexPositionsBuffer->GetGPUVirtualAddress();
    model.VertexPositionsBufferView.StrideInBytes  = sizeof(VertexPosition);
    model.VertexPositionsBufferView.SizeInBytes    = sizeof(VertexPosition) * vertices.Count;

    model.VertexAttributesBuffer = uploader.CreateStaticBuffer(sizeof(VertexAttributes) * vertices.Count, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        [&](void* p)
        {
            vertices.AttributesFiller(static_cast<VertexAttributes*>(p));
        }
    );

    model.TriangleParametersBuffer = uploader.CreateStaticBuffer(sizeof(TriangleParameters) * triangles.Count, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        [&](void* p)
        {
            triangles.ParametersFiller(static_cast<TriangleParameters*>(p));
        }
    );

    return model;
}

struct ModelManager
{
    UINT                                     ModelCount = 0;
    std::array<Model, MAX_MODEL_DEFINITIONS> ModelList{};

    UINT                                     FirstSrv = 0;

    ModelManager(RenderDevice&);
    Model::Id AllocateNewModel(RenderDevice&, Model&&);

    UINT GetTriangleIndicesDescriptorTable   () const { return FirstSrv + MAX_MODEL_DEFINITIONS * 0; }
    UINT GetTriangleParametersDescriptorTable() const { return FirstSrv + MAX_MODEL_DEFINITIONS * 1; }
    UINT GetVertexPositionsDescriptorTable   () const { return FirstSrv + MAX_MODEL_DEFINITIONS * 2; }
    UINT GetVertexAttributesDescriptorTable  () const { return FirstSrv + MAX_MODEL_DEFINITIONS * 3; }

    uint32_t GetModelIndex(Model::Id const id) const
    {
        assert(id != Model::Id::Invalid);
        auto const index = static_cast<uint32_t>(id) - 1;
        assert(index < ModelList.size());
        assert(index < ModelCount);
        return index;
    }

    Model&       GetModel(Model::Id id)       { return ModelList[GetModelIndex(id)]; }
    Model const& GetModel(Model::Id id) const { return ModelList[GetModelIndex(id)]; }

};

ModelManager::ModelManager(RenderDevice& renderDevice)
{
    FirstSrv = renderDevice.srvDescriptorHeap.AllocateDescriptors(MAX_MODEL_DEFINITIONS * 4);
}

Model::Id ModelManager::AllocateNewModel(RenderDevice& renderDevice, Model&& model)
{
    assert(ModelCount < ModelList.size());
    auto const index = ModelCount;
    auto const id = Model::Id{ index + 1 };
    ++ModelCount;

    // Initialize the SRVs.

    renderDevice.UploadSrvDescriptor(
        GetTriangleIndicesDescriptorTable() + index,
        model.TriangleIndices,
        {
            .Format                  = DXGI_FORMAT_R32G32B32_UINT,
            .ViewDimension           = D3D12_SRV_DIMENSION_BUFFER,
            .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
            .Buffer                  { .NumElements = model.TriangleCount },
        }
    );
    renderDevice.UploadSrvDescriptor(
        GetTriangleParametersDescriptorTable() + index,
        model.TriangleParametersBuffer,
        {
            .Format                  = DXGI_FORMAT_UNKNOWN,
            .ViewDimension           = D3D12_SRV_DIMENSION_BUFFER,
            .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
            .Buffer                  { .NumElements = model.TriangleCount, .StructureByteStride = sizeof(TriangleParameters) },
        }
    );
    renderDevice.UploadSrvDescriptor(
        GetVertexPositionsDescriptorTable() + index,
        model.VertexPositionsBuffer,
        {
            .Format                  = DXGI_FORMAT_UNKNOWN,
            .ViewDimension           = D3D12_SRV_DIMENSION_BUFFER,
            .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
            .Buffer                  { .NumElements = model.VertexCount, .StructureByteStride = sizeof(VertexPosition) },
        }
    );
    renderDevice.UploadSrvDescriptor(
        GetVertexAttributesDescriptorTable() + index,
        model.VertexAttributesBuffer,
        {
            .Format                  = DXGI_FORMAT_UNKNOWN,
            .ViewDimension           = D3D12_SRV_DIMENSION_BUFFER,
            .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
            .Buffer                  { .NumElements = model.VertexCount, .StructureByteStride = sizeof(VertexAttributes) },
        }
    );
    ModelList[index] = std::move(model);
    return id;
}


struct InstanceAttributes
{
    XMFLOAT4X4 View;
    XMFLOAT4X4 Projection;
    uint32_t   ModelIndex;
    uint32_t   Padding[3];
};

struct Instance
{
    enum Id : uint32_t { Invalid = 0 };

    Model::Id ModelId = Model::Id::Invalid;

    uint16_t Yaw   = 0;
    uint16_t Pitch = 0;
    uint16_t Roll  = 0;
};

struct InstanceManager
{
    UINT                                           InstanceCount = 0;
    std::array<Instance, MAX_INSTANCE_DEFINITIONS> InstanceList{};

    struct AttributesBuffer
    {
        ComPtr<ID3D12Resource>                                    Buffer   {};
        std::array<InstanceAttributes, MAX_INSTANCE_DEFINITIONS>* pAttributes; // Permanently mapped for CPU access.
        UINT                                                      SrvIndex = 0;
    };
    std::array<AttributesBuffer, FrameCount> AttributesBufferList{};

    InstanceManager(RenderDevice&);
    Instance::Id AllocateNewInstance(RenderDevice&, ModelManager const&, Instance&&);

    uint32_t GetInstanceIndex(Instance::Id const id) const
    {
        assert(id != Instance::Id::Invalid);
        auto const index = static_cast<uint32_t>(id) - 1;
        assert(index < MAX_INSTANCE_DEFINITIONS);
        assert(index < InstanceCount);
        return index;
    }

    Instance&       GetInstance(Instance::Id id)       { return InstanceList[GetInstanceIndex(id)]; }
    Instance const& GetInstance(Instance::Id id) const { return InstanceList[GetInstanceIndex(id)]; }

    AttributesBuffer&       GetCurrentFrameInstanceAttributes(RenderDevice const& renderDevice)       { return AttributesBufferList[renderDevice.frameIndex]; }
    AttributesBuffer const& GetCurrentFrameInstanceAttributes(RenderDevice const& renderDevice) const { return AttributesBufferList[renderDevice.frameIndex]; }

    InstanceAttributes& GetCurrentFrameInstanceAttributes(RenderDevice const& renderDevice, Instance::Id const id) { return (*AttributesBufferList[renderDevice.frameIndex].pAttributes)[GetInstanceIndex(id)]; }

    struct iterator
    {
        uint32_t Index;

        iterator& operator++() { ++Index; return *this; }
        iterator operator++(int) { auto const result = *this; ++Index; return result; }

        Instance::Id operator*() { return static_cast<Instance::Id>(Index + 1); }

        friend auto operator!=(iterator const a, iterator const b) { return a.Index != b.Index; }
    };

    iterator begin() const { return iterator{ 0 }; }
    iterator end  () const { return iterator{ InstanceCount }; }
};

InstanceManager::InstanceManager(RenderDevice& renderDevice)
{
    for (auto& buffer : AttributesBufferList)
    {
        buffer.Buffer = CreateUploadBuffer(renderDevice.device, sizeof(*buffer.pAttributes));
        D3D12_RANGE const readRange{ .Begin = 0, .End = 0 }; // We do not intend to read from this resource on the CPU.
        ThrowIfFailed(buffer.Buffer->Map(0, &readRange, reinterpret_cast<void**>(&buffer.pAttributes)));
        buffer.SrvIndex = renderDevice.AllocateSrvDescriptor(buffer.Buffer,
            {
                .Format                  = DXGI_FORMAT_UNKNOWN,
                .ViewDimension           = D3D12_SRV_DIMENSION_BUFFER,
                .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
                .Buffer                  { .NumElements = MAX_INSTANCE_DEFINITIONS, .StructureByteStride = sizeof(InstanceAttributes) },
            }
        );
    }
}

Instance::Id InstanceManager::AllocateNewInstance(RenderDevice& renderDevice, ModelManager const& modelManager, Instance&& instance)
{
    assert(InstanceCount < InstanceList.size());
    auto const index = InstanceCount;
    auto const id = Instance::Id{ index + 1 };
    ++InstanceCount;

    for (auto& buffer : AttributesBufferList)
    {
        (*buffer.pAttributes)[GetInstanceIndex(id)].ModelIndex = modelManager.GetModelIndex(instance.ModelId);
    }

    InstanceList[index] = std::move(instance);
    return id;
}

void UpdateModelInstance(RenderDevice const& renderDevice, InstanceManager& instanceManager, Instance::Id const instanceId)
{
    auto& instance = instanceManager.GetInstance(instanceId);

    instance.Yaw   += 0x50;
    instance.Pitch += 0x77;
    instance.Roll  += 0x23;

    XMMATRIX const modelMatrix = XMMatrixRotationRollPitchYaw(instance.Pitch * (2.0f * XM_PI / 65536.0f), instance.Yaw * (2.0f * XM_PI / 65536.0f), instance.Roll * (2.0f * XM_PI / 65536.0));
    XMMATRIX const translation = XMMatrixTranslation(4, 0, 0);

    auto& attributes = instanceManager.GetCurrentFrameInstanceAttributes(renderDevice, instanceId);
    XMStoreFloat4x4(&attributes.View      , XMMatrixTranspose(modelMatrix * translation * View    ));
    XMStoreFloat4x4(&attributes.Projection, XMMatrixTranspose(modelMatrix * translation * ViewProj));
};

void UpdateStaticModelInstance(RenderDevice const& renderDevice, InstanceManager& instanceManager, Instance::Id const instanceId)
{
    auto& instance = instanceManager.GetInstance(instanceId);

    instance.Yaw += 50;

    XMMATRIX const modelMatrix = XMMatrixRotationRollPitchYaw(instance.Pitch * (2.0f * XM_PI / 65536.0f), instance.Yaw * (2.0f * XM_PI / 65536.0f), instance.Roll * (2.0f * XM_PI / 65536.0));

    XMMATRIX const scale = XMMatrixScaling(100, 100, 100);

    auto& attributes = instanceManager.GetCurrentFrameInstanceAttributes(renderDevice, instanceId);
    XMStoreFloat4x4(&attributes.View      , XMMatrixTranspose(scale * modelMatrix * View    ));
    XMStoreFloat4x4(&attributes.Projection, XMMatrixTranspose(scale * modelMatrix * ViewProj));
};







struct VisibilityPass
{
    ComPtr<ID3D12RootSignature> RootSignature;
    ComPtr<ID3D12PipelineState> PipelineState;
    IntermediateRenderTarget    VisibilityBuffer;
    IntermediateDepthBuffer     DepthBuffer;

    VisibilityPass(RenderDevice& renderDevice);

    void Render(RenderDevice const&, ModelManager const&, InstanceManager const&) const;
};

VisibilityPass::VisibilityPass(RenderDevice& renderDevice)
{
    // Create the rendering root signature.
    RootSignature = renderDevice.CreateRootSignature<
        Root::Constants<D3D12_SHADER_VISIBILITY_ALL   ,                0,    0, 1>, // Constant buffer
        Root::Table    <D3D12_SHADER_VISIBILITY_VERTEX, Root::SrvRange<0, 1, 0>>,   // Instance buffer
        Root::Flags    <D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT>
    >();

    // Create the pipeline state, which includes compiling and loading shaders.
    {
        // Define the vertex input layout.
        D3D12_INPUT_ELEMENT_DESC const inputElementDescs[] =
        {
            { .SemanticName = "SV_Position", .SemanticIndex = 0, .Format = DXGI_FORMAT_R32G32B32_FLOAT, .InputSlot = 0, .AlignedByteOffset =  0, .InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, .InstanceDataStepRate = 0 },
        };

        PipelineState = renderDevice.Create(Pipeline::State
            {
                .RootSignature         { RootSignature },
                .TopologyType          = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
                .InputLayout           { inputElementDescs, _countof(inputElementDescs) },
                .VS                    = Compile(Shader{ L"model.hlsl", "VisibilityVS", "vs_5_1" }),
                .PS                    = Compile(Shader{ L"model.hlsl", "VisibilityPS", "ps_5_1" }),
                .DepthStencilState     = DefaultDepthStencilDesc,
                .Multisampling         = Pipeline::Multisampling<8>,
                .RTVFormats            { DXGI_FORMAT_R32_UINT, },
                .DSVFormat             = DXGI_FORMAT_D32_FLOAT,
            }
        );
    }

    VisibilityBuffer = CreateIntermediateRenderTarget(renderDevice, 1280, 720, DXGI_FORMAT_R32_UINT, {}, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_FLAG_NONE, 8);
    DepthBuffer      = CreateIntermediateDepthBuffer (renderDevice, 1280, 720, 0, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, 8);
}

void VisibilityPass::Render(RenderDevice const& renderDevice, ModelManager const& modelManager, InstanceManager const& instanceManager) const
{
    renderDevice.commandList->SetGraphicsRootSignature(RootSignature.Get());
    renderDevice.commandList->SetPipelineState        (PipelineState.Get());

    ID3D12DescriptorHeap* const heaps[]{ renderDevice.srvDescriptorHeap };
    renderDevice.commandList->SetDescriptorHeaps(1, heaps);

    TransitionResource(renderDevice.commandList, VisibilityBuffer.Resource, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
    TransitionResource(renderDevice.commandList, DepthBuffer     .Resource, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);

    auto const rtvHandle = renderDevice.rtvDescriptorHeap.GetCpuDescriptorHandle(VisibilityBuffer.RtvDescriptorIndex);
    auto const dsvHandle = renderDevice.dsvDescriptorHeap.GetCpuDescriptorHandle(DepthBuffer     .DsvDescriptorIndex);
    renderDevice.commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);

    D3D12_VIEWPORT const viewport   { 0, 0, static_cast<float>(VisibilityBuffer.Width), static_cast<float>(VisibilityBuffer.Height), D3D12_MIN_DEPTH, D3D12_MAX_DEPTH };
    D3D12_RECT     const scissorRect{ 0, 0, static_cast<LONG >(VisibilityBuffer.Width), static_cast<LONG >(VisibilityBuffer.Height) };

    renderDevice.commandList->RSSetViewports    (1, &viewport);
    renderDevice.commandList->RSSetScissorRects (1, &scissorRect);

    renderDevice.commandList->ClearRenderTargetView(rtvHandle, VisibilityBuffer.ClearColor.data(), 0, nullptr);
    renderDevice.commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, DepthBuffer.ClearValue, 0, 0, nullptr);
    renderDevice.commandList->SetGraphicsRootDescriptorTable(1, renderDevice.srvDescriptorHeap.GetGpuDescriptorHandle(instanceManager.GetCurrentFrameInstanceAttributes(renderDevice).SrvIndex));

    for (auto instanceId : instanceManager)
    {
        auto& instance = instanceManager.GetInstance(instanceId);
        if (instance.ModelId == Model::Id::Invalid)
        {
            continue;
        }
        auto& model = modelManager.GetModel(instance.ModelId);
        renderDevice.commandList->IASetPrimitiveTopology      (D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        renderDevice.commandList->IASetIndexBuffer            (&model.TriangleIndicesView);
        renderDevice.commandList->IASetVertexBuffers          (0, 1, &model.VertexPositionsBufferView);
        renderDevice.commandList->SetGraphicsRoot32BitConstant(0, instanceManager.GetInstanceIndex(instanceId), 0);
        renderDevice.commandList->DrawIndexedInstanced        (model.TriangleCount * 3, 1, 0, 0, 0);
    }

    TransitionResource(renderDevice.commandList, VisibilityBuffer.Resource, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    TransitionResource(renderDevice.commandList, DepthBuffer     .Resource, D3D12_RESOURCE_STATE_DEPTH_WRITE  , D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
}



template < typename T >
struct CpuAccessibleBuffer
{
    ComPtr<ID3D12Resource> Buffer{};
    T*                     pData = nullptr;

    CpuAccessibleBuffer() = default;

    explicit CpuAccessibleBuffer(RenderDevice& renderDevice);

    explicit operator bool() const { return pData != nullptr; }
    operator ComRef<ID3D12Resource>() const { return Buffer; }
    operator T*  () const { return pData; }
    T& operator* () const { return *pData; }
    T* operator->() const { return pData; }
};

template < typename T >
CpuAccessibleBuffer<T>::CpuAccessibleBuffer(RenderDevice& renderDevice)
    : Buffer{ CreateUploadBuffer(renderDevice.device, sizeof(*pData)) }
{
    D3D12_RANGE const readRange{ .Begin = 0, .End = 0 }; // We do not intend to read from this resource on the CPU.
    ThrowIfFailed(Buffer->Map(0, &readRange, reinterpret_cast<void**>(&pData)));
}

struct ComputableGatherPass
{
    ComPtr<ID3D12RootSignature> RootSignature;
    ComPtr<ID3D12PipelineState> PipelineState;

    struct ComputableSample
    {
        uint32_t ScreenPos;  // 32 bits
        uint32_t Visibility; // 32 bits instance + tri
        uint32_t Weights;    // UNORM  4 * 5 = 20 bits compressed
        float    Depth;      // As is, 32 bits
    };
    ComPtr<ID3D12Resource> ComputableSampleBuffer;
    UINT                   ComputableSampleBufferUav;

    ComputableGatherPass(RenderDevice& renderDevice);

    void Render(RenderDevice const&, VisibilityPass const&, ModelManager const&, InstanceManager const&) const;
};

ComputableGatherPass::ComputableGatherPass(RenderDevice& renderDevice)
{
    // Create the rendering root signature.
    RootSignature = renderDevice.CreateRootSignature<
        //Root::Cbv<  D3D12_SHADER_VISIBILITY_ALL,                0,                        0 >,
        //Root::Table<D3D12_SHADER_VISIBILITY_ALL, Root::SrvRange<0, 1,                     0>>,
        Root::Table<D3D12_SHADER_VISIBILITY_ALL, Root::SrvRange<1, 1,                     0>>,
        Root::Table<D3D12_SHADER_VISIBILITY_ALL, Root::SrvRange<2, 1,                     0>>,
        Root::Table<D3D12_SHADER_VISIBILITY_ALL, Root::UavRange<1, 1,                     0>>,
        //Root::Table<D3D12_SHADER_VISIBILITY_ALL, Root::SrvRange<1, MAX_MODEL_DEFINITIONS, 1>>,
        //Root::Table<D3D12_SHADER_VISIBILITY_ALL, Root::SrvRange<1, MAX_MODEL_DEFINITIONS, 2>>,
        //Root::Table<D3D12_SHADER_VISIBILITY_ALL, Root::SrvRange<1, MAX_MODEL_DEFINITIONS, 3>>,
        //Root::Table<D3D12_SHADER_VISIBILITY_ALL, Root::SrvRange<1, MAX_MODEL_DEFINITIONS, 4>>,
        Root::Flags<D3D12_ROOT_SIGNATURE_FLAG_NONE>
    >();

    // Create the pipeline state, which includes compiling and loading shaders.
    PipelineState = renderDevice.Create(Compute::State
        {
            .RootSignature{ RootSignature },
            .CS           = Compile(Shader{ L"model.hlsl", "ComputableGatherPass", "cs_5_1" }),
        }
    );

    ComputableSampleBuffer = CreateGpuBuffer(renderDevice.device, 1280 * 720 * sizeof(ComputableSample));
    ComputableSampleBufferUav = renderDevice.AllocateUavDescriptor(ComputableSampleBuffer,
        {
            .Format        = DXGI_FORMAT_UNKNOWN,
            .ViewDimension = D3D12_UAV_DIMENSION_BUFFER,
            .Buffer{
                .NumElements = 1280 * 720,
                .StructureByteStride = sizeof(ComputableSample),
            },
        }
    );
}

void ComputableGatherPass::Render(RenderDevice const& renderDevice, VisibilityPass const& visibilityPass, ModelManager const& modelManager, InstanceManager const& instanceManager) const
{
    renderDevice.commandList->SetComputeRootSignature(RootSignature.Get());
    renderDevice.commandList->SetPipelineState       (PipelineState.Get());

    ID3D12DescriptorHeap* const heaps[]{ renderDevice.srvDescriptorHeap };
    renderDevice.commandList->SetDescriptorHeaps(1, heaps);

    renderDevice.commandList->SetComputeRootDescriptorTable   (0, renderDevice.srvDescriptorHeap.GetGpuDescriptorHandle(visibilityPass.VisibilityBuffer.SrvDescriptorIndex));
    renderDevice.commandList->SetComputeRootDescriptorTable   (1, renderDevice.srvDescriptorHeap.GetGpuDescriptorHandle(visibilityPass.DepthBuffer     .SrvDescriptorIndex));
    renderDevice.commandList->SetComputeRootDescriptorTable   (2, renderDevice.srvDescriptorHeap.GetGpuDescriptorHandle(ComputableSampleBufferUav));

    renderDevice.commandList->Dispatch(1280 / 32, 720 / 16, 1);
}







struct ColorPass
{
    ComPtr<ID3D12RootSignature> RootSignature;
    ComPtr<ID3D12PipelineState> PipelineState;

    IntermediateRenderTarget ColorBuffer;
    UINT                     ColorBufferUav = 0;

    struct SceneConstants
    {
        XMFLOAT4X4 InverseProjection;
    };
    CpuAccessibleBuffer<SceneConstants> SceneConstantsBuffer[FrameCount];

    ColorPass(RenderDevice& renderDevice);

    void Render(RenderDevice const&, VisibilityPass const&, ModelManager const&, InstanceManager const&) const;
};

ColorPass::ColorPass(RenderDevice& renderDevice)
{
    // Create the rendering root signature.
    RootSignature = renderDevice.CreateRootSignature<
        Root::Cbv<  D3D12_SHADER_VISIBILITY_ALL,                0,                        0 >,
        Root::Table<D3D12_SHADER_VISIBILITY_ALL, Root::SrvRange<0, 1,                     0>>,
        Root::Table<D3D12_SHADER_VISIBILITY_ALL, Root::SrvRange<1, 1,                     0>>,
        Root::Table<D3D12_SHADER_VISIBILITY_ALL, Root::SrvRange<2, 1,                     0>>,
        Root::Table<D3D12_SHADER_VISIBILITY_ALL, Root::UavRange<0, 1,                     0>>,
        Root::Table<D3D12_SHADER_VISIBILITY_ALL, Root::SrvRange<1, MAX_MODEL_DEFINITIONS, 1>>,
        Root::Table<D3D12_SHADER_VISIBILITY_ALL, Root::SrvRange<1, MAX_MODEL_DEFINITIONS, 2>>,
        Root::Table<D3D12_SHADER_VISIBILITY_ALL, Root::SrvRange<1, MAX_MODEL_DEFINITIONS, 3>>,
        Root::Table<D3D12_SHADER_VISIBILITY_ALL, Root::SrvRange<1, MAX_MODEL_DEFINITIONS, 4>>,
        Root::Flags<D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT>
    >();

    // Create the pipeline state, which includes compiling and loading shaders.
    PipelineState = renderDevice.Create(Compute::State
        {
            .RootSignature{ RootSignature },
            .CS           = Compile(Shader{ L"model.hlsl", "ColorPass", "cs_5_1" }),
        }
    );

    ColorBuffer = CreateIntermediateRenderTarget(renderDevice, 1280, 720, DXGI_FORMAT_R8G8B8A8_UNORM, {}, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    ColorBufferUav = renderDevice.AllocateUavDescriptor(ColorBuffer.Resource,
        {
            .Format        = DXGI_FORMAT_R8G8B8A8_UNORM,
            .ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D,
        }
    );

    for (auto& buffer : SceneConstantsBuffer)
    {
        buffer = CpuAccessibleBuffer<SceneConstants>{renderDevice};
    }
}

void ColorPass::Render(RenderDevice const& renderDevice, VisibilityPass const& visibilityPass, ModelManager const& modelManager, InstanceManager const& instanceManager) const
{
    TransitionResource(renderDevice.commandList, ColorBuffer.Resource, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    auto& sceneConstants = SceneConstantsBuffer[renderDevice.frameIndex];
    XMStoreFloat4x4(&sceneConstants->InverseProjection, XMMatrixTranspose(InverseProjection));

    renderDevice.commandList->SetComputeRootSignature(RootSignature.Get());
    renderDevice.commandList->SetPipelineState       (PipelineState.Get());

    ID3D12DescriptorHeap* const heaps[]{ renderDevice.srvDescriptorHeap };
    renderDevice.commandList->SetDescriptorHeaps(1, heaps);

    renderDevice.commandList->SetComputeRootConstantBufferView(0, SceneConstantsBuffer[renderDevice.frameIndex].Buffer->GetGPUVirtualAddress());
    renderDevice.commandList->SetComputeRootDescriptorTable   (1, renderDevice.srvDescriptorHeap.GetGpuDescriptorHandle(instanceManager.GetCurrentFrameInstanceAttributes(renderDevice).SrvIndex));
    renderDevice.commandList->SetComputeRootDescriptorTable   (2, renderDevice.srvDescriptorHeap.GetGpuDescriptorHandle(visibilityPass.VisibilityBuffer.SrvDescriptorIndex));
    renderDevice.commandList->SetComputeRootDescriptorTable   (3, renderDevice.srvDescriptorHeap.GetGpuDescriptorHandle(visibilityPass.DepthBuffer     .SrvDescriptorIndex));
    renderDevice.commandList->SetComputeRootDescriptorTable   (4, renderDevice.srvDescriptorHeap.GetGpuDescriptorHandle(ColorBufferUav));

    renderDevice.commandList->SetComputeRootDescriptorTable(5, renderDevice.srvDescriptorHeap.GetGpuDescriptorHandle(modelManager.GetTriangleIndicesDescriptorTable   ()));
    renderDevice.commandList->SetComputeRootDescriptorTable(6, renderDevice.srvDescriptorHeap.GetGpuDescriptorHandle(modelManager.GetTriangleParametersDescriptorTable()));
    renderDevice.commandList->SetComputeRootDescriptorTable(7, renderDevice.srvDescriptorHeap.GetGpuDescriptorHandle(modelManager.GetVertexPositionsDescriptorTable   ()));
    renderDevice.commandList->SetComputeRootDescriptorTable(8, renderDevice.srvDescriptorHeap.GetGpuDescriptorHandle(modelManager.GetVertexAttributesDescriptorTable  ()));

    renderDevice.commandList->Dispatch(1280 / 64, 720 / 16, 1);
}






struct PostprocessPass
{
    ComPtr<ID3D12RootSignature> RootSignature;
    ComPtr<ID3D12PipelineState> PipelineState;

    PostprocessPass(RenderDevice& renderDevice);

    void Render(RenderDevice const&, VisibilityPass const&, ColorPass const&) const;
};

PostprocessPass::PostprocessPass(RenderDevice& renderDevice)
{
    // Create the rendering root signature.
    RootSignature = renderDevice.CreateRootSignature<
        Root::Table<D3D12_SHADER_VISIBILITY_ALL, Root::SrvRange<1, 1, 0>>,
        Root::Table<D3D12_SHADER_VISIBILITY_ALL, Root::UavRange<0, 1, 0>>
    >();

    // Create the pipeline state, which includes compiling and loading shaders.
    PipelineState = renderDevice.Create(Compute::State
        {
            .RootSignature{ RootSignature },
            .CS           = Compile(Shader{ L"model.hlsl", "Postprocess", "cs_5_1" }),
        }
    );
}

void PostprocessPass::Render(RenderDevice const& renderDevice, VisibilityPass const& visibilityPass, ColorPass const& colorPass) const
{
    //TransitionResource(renderDevice.commandList, colorPass.ColorBuffer.Resource, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    renderDevice.commandList->SetComputeRootSignature(RootSignature.Get());
    renderDevice.commandList->SetPipelineState       (PipelineState.Get());

    ID3D12DescriptorHeap* const heaps[]{ renderDevice.srvDescriptorHeap };
    renderDevice.commandList->SetDescriptorHeaps(1, heaps);

    renderDevice.commandList->SetComputeRootDescriptorTable(0, renderDevice.srvDescriptorHeap.GetGpuDescriptorHandle(visibilityPass.VisibilityBuffer.SrvDescriptorIndex));
    renderDevice.commandList->SetComputeRootDescriptorTable(1, renderDevice.srvDescriptorHeap.GetGpuDescriptorHandle(colorPass.ColorBufferUav));

    renderDevice.commandList->Dispatch(1280/8, 720/8, 1);
}







struct ResolvePass
{
    ComPtr<ID3D12RootSignature> RootSignature;
    ComPtr<ID3D12PipelineState> PipelineState;

    ResolvePass(RenderDevice& renderDevice);

    void Render(RenderDevice const&, ColorPass const&) const;
};

ResolvePass::ResolvePass(RenderDevice& renderDevice)
{
    // Create the rendering root signature.
    RootSignature = renderDevice.CreateRootSignature<
        Root::Table<D3D12_SHADER_VISIBILITY_ALL, Root::SrvRange<2, 1, 0>>
    >();

    // Create the pipeline state, which includes compiling and loading shaders.
    PipelineState = renderDevice.Create(Pipeline::State
        {
            .RootSignature         { RootSignature },
            .TopologyType          = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
            .VS                    = Compile(Shader{ L"model.hlsl", "ResolveVS", "vs_5_1" }),
            .PS                    = Compile(Shader{ L"model.hlsl", "ResolvePS", "ps_5_1" }),
            .DepthStencilState     = Pipeline::NoDepthStencil,
            .Multisampling         = Pipeline::NoMultisampling,
            .RTVFormats            { DXGI_FORMAT_R8G8B8A8_UNORM, },
        }
    );
}

void ResolvePass::Render(RenderDevice const& renderDevice, ColorPass const& colorPass) const
{
    TransitionResource(renderDevice.commandList, colorPass.ColorBuffer.Resource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

    renderDevice.commandList->SetGraphicsRootSignature(RootSignature.Get());
    renderDevice.commandList->SetPipelineState        (PipelineState.Get());

    ID3D12DescriptorHeap* const heaps[]{ renderDevice.srvDescriptorHeap };
    renderDevice.commandList->SetDescriptorHeaps(1, heaps);

    renderDevice.commandList->SetGraphicsRootDescriptorTable(0, renderDevice.srvDescriptorHeap.GetGpuDescriptorHandle(colorPass.ColorBuffer.SrvDescriptorIndex));

    renderDevice.commandList->DrawInstanced(3, 1, 0, 0);
}








Model CreateCubeModel(AssetUploader& uploader, RenderDevice& renderDevice)
{
    static constexpr TriangleIndices indices[] =
    {
        {  0,  1,  2 },
        {  2,  1,  3 },

        {  4,  5,  6 },
        {  6,  5,  7 },

        {  8,  9, 10 },
        { 10,  9, 11 },

        { 12, 13, 14 },
        { 14, 13, 15 },

        { 16, 17, 18 },
        { 18, 17, 19 },

        { 20, 21, 22 },
        { 22, 21, 23 },
    };

    static constexpr VertexPosition triangleVertices[] =
    {
        { {  1.0f,  1.0f,  1.0f } },
        { {  1.0f,  1.0f, -1.0f } },
        { {  1.0f, -1.0f,  1.0f } },
        { {  1.0f, -1.0f, -1.0f } },

        { { -1.0f,  1.0f,  1.0f } },
        { { -1.0f, -1.0f,  1.0f } },
        { { -1.0f,  1.0f, -1.0f } },
        { { -1.0f, -1.0f, -1.0f } },

        { {  1.0f,  1.0f,  1.0f } },
        { {  1.0f, -1.0f,  1.0f } },
        { { -1.0f,  1.0f,  1.0f } },
        { { -1.0f, -1.0f,  1.0f } },

        { {  1.0f,  1.0f, -1.0f } },
        { { -1.0f,  1.0f, -1.0f } },
        { {  1.0f, -1.0f, -1.0f } },
        { { -1.0f, -1.0f, -1.0f } },

        { {  1.0f,  1.0f,  1.0f } },
        { { -1.0f,  1.0f,  1.0f } },
        { {  1.0f,  1.0f, -1.0f } },
        { { -1.0f,  1.0f, -1.0f } },

        { {  1.0f, -1.0f,  1.0f } },
        { {  1.0f, -1.0f, -1.0f } },
        { { -1.0f, -1.0f,  1.0f } },
        { { -1.0f, -1.0f, -1.0f } },
    };

    static constexpr VertexAttributes vertexAttributes[] =
    {
        { {  1,  0,  0 }, 0xFF0000FFu },
        { {  1,  0,  0 }, 0xFF0000FFu },
        { {  1,  0,  0 }, 0xFF0000FFu },
        { {  1,  0,  0 }, 0xFF0000FFu },
        { { -1,  0,  0 }, 0xFF00FF00u },
        { { -1,  0,  0 }, 0xFF00FF00u },
        { { -1,  0,  0 }, 0xFF00FF00u },
        { { -1,  0,  0 }, 0xFF00FF00u },
        { {  0,  0,  1 }, 0xFFFF0000u },
        { {  0,  0,  1 }, 0xFFFF0000u },
        { {  0,  0,  1 }, 0xFFFF0000u },
        { {  0,  0,  1 }, 0xFFFF0000u },
        { {  0,  0, -1 }, 0xFF00FFFFu },
        { {  0,  0, -1 }, 0xFF00FFFFu },
        { {  0,  0, -1 }, 0xFF00FFFFu },
        { {  0,  0, -1 }, 0xFF00FFFFu },
        { {  0,  1,  0 }, 0xFFFF00FFu },
        { {  0,  1,  0 }, 0xFFFF00FFu },
        { {  0,  1,  0 }, 0xFFFF00FFu },
        { {  0,  1,  0 }, 0xFFFF00FFu },
        { {  0, -1,  0 }, 0xFFFFFF00u },
        { {  0, -1,  0 }, 0xFFFFFF00u },
        { {  0, -1,  0 }, 0xFFFFFF00u },
        { {  0, -1,  0 }, 0xFFFFFF00u },
    };

    static constexpr TriangleParameters triangleParams[] =
    {
        { 0xFF0000FFu },
        { 0xFF0000FFu },
        { 0xFF00FF00u },
        { 0xFF00FF00u },
        { 0xFFFF0000u },
        { 0xFFFF0000u },
        { 0xFF00FFFFu },
        { 0xFF00FFFFu },
        { 0xFFFF00FFu },
        { 0xFFFF00FFu },
        { 0xFFFFFF00u },
        { 0xFFFFFF00u },
    };

    return CreateModel(uploader, renderDevice, { indices, triangleParams }, { triangleVertices, vertexAttributes });
}

struct ObjModel
{
    using IndexSet = std::tuple<uint32_t, uint32_t>;
    using Triangle = std::tuple<IndexSet, IndexSet, IndexSet>;

    std::vector<XMFLOAT3> Positions;
    std::vector<XMFLOAT3> Normals;
    std::vector<Triangle> Triangles;
};


ObjModel LoadObjModel(AssetUploader& uploader, RenderDevice& renderDevice)
{
    ObjModel model;
    
    auto const f = fopen(R"(X:\TEMP\MaryRoseTankard_100kMesh.obj)", "r");
    //auto const f = fopen(R"(C:\Users\jcab\Downloads\ameise\source\Ameise_C\Ameise_C.obj)", "r");
    while (!feof(f))
    {
        std::array<char, 2048> buffer;
        buffer[0] = '\0';
        fgets(buffer.data(), buffer.size(), f);
        std::string_view line{ buffer.data() };
        if (line.empty())
        {
            continue;
        }
        if (line.back() == '\n')
        {
            line = line.substr(0, line.size() - 1);
        }
        if (line[0] == 'v' && line[1] == ' ')
        {
            XMFLOAT3 pos;
            sscanf_s(line.data(), "v %f %f %f", &pos.x, &pos.y, &pos.z);
            model.Positions.push_back(pos);
        }
        else if (line[0] == 'v' && line[1] == 'n' && line[2] == ' ')
        {
            XMFLOAT3 norm;
            sscanf_s(line.data(), "vn %f %f %f", &norm.x, &norm.y, &norm.z);
            model.Normals.push_back(norm);
        }
        else if (line[0] == 'f' && line[1] == ' ')
        {
            uint32_t p0;
            uint32_t p1;
            uint32_t p2;
            uint32_t p3;
            uint32_t t0;
            uint32_t t1;
            uint32_t t2;
            uint32_t t3;
            uint32_t n0;
            uint32_t n1;
            uint32_t n2;
            uint32_t n3;
            auto matches = sscanf(line.data(), "f %u/%u/%u %u/%u/%u %u/%u/%u %u/%u/%u", &p0, &t0, &n0, &p1, &t1, &n1, &p2, &t2, &n2, &p3, &t3, &n3);
            if (matches == 2)
            {
                matches = sscanf(line.data(), "f %u/%u %u/%u %u/%u %u/%u", &p0, &t0, &p1, &t1, &p2, &t2, &p3, &t3);
                n0 = UINT32_MAX;
                n1 = UINT32_MAX;
                n2 = UINT32_MAX;
                n3 = UINT32_MAX;
                if (matches == 6)
                {
                    matches = 9;
                }
                else if (matches == 8)
                {
                    matches = 12;
                }
                else
                {
                    matches = 0;
                }
            }
            if (matches >= 9)
            {
                model.Triangles.emplace_back(ObjModel::IndexSet{p0, n0}, ObjModel::IndexSet{p2, n2}, ObjModel::IndexSet{p1, n1});

            }
            if (matches >= 12)
            {
                model.Triangles.emplace_back(ObjModel::IndexSet{p0, n0}, ObjModel::IndexSet{p3, n3}, ObjModel::IndexSet{p2, n2});
            }
        }
    }

    return model;
}

void AddObjModelMissingNormals(ObjModel& model)
{
    for (auto& [ v0, v1, v2 ] : model.Triangles)
    {
        auto& [ p0, n0 ] = v0;
        auto& [ p1, n1 ] = v1;
        auto& [ p2, n2 ] = v2;

        if (n0 == UINT32_MAX || n1 == UINT32_MAX || n2 == UINT32_MAX)
        {
            uint32_t n = model.Normals.size() + 1;
            auto xp0 = XMLoadFloat3(&model.Positions[p0 - 1]);
            auto xp1 = XMLoadFloat3(&model.Positions[p1 - 1]);
            auto xp2 = XMLoadFloat3(&model.Positions[p2 - 1]);
            XMFLOAT3 norm;
            XMStoreFloat3(&norm, XMVector3Normalize(XMVector3Cross(xp2 - xp0, xp1 - xp0)));
            model.Normals.push_back(norm);
            if (n0 == UINT32_MAX) n0 = n;
            if (n1 == UINT32_MAX) n1 = n;
            if (n2 == UINT32_MAX) n2 = n;
        }
    }
}

void RecenterObjModelPositions(ObjModel& model)
{
    XMFLOAT3 vmin{  1e20f,  1e20f,  1e20f };
    XMFLOAT3 vmax{ -1e20f, -1e20f, -1e20f };
    for (auto pos : model.Positions)
    {
        vmax.x = XMMax(vmax.x, pos.x);
        vmax.y = XMMax(vmax.y, pos.y);
        vmax.z = XMMax(vmax.z, pos.z);
        vmin.x = XMMin(vmin.x, pos.x);
        vmin.y = XMMin(vmin.y, pos.y);
        vmin.z = XMMin(vmin.z, pos.z);
    }

    XMVECTOR center = (XMLoadFloat3(&vmin) + XMLoadFloat3(&vmax)) / 2;

    for (auto& pos : model.Positions)
    {
        XMStoreFloat3(&pos, XMLoadFloat3(&pos) - center);
    }
}



std::vector<Model> UploadObjModel(ObjModel const& model, AssetUploader& uploader, RenderDevice& renderDevice)
{
    std::vector<Model> result;

    std::map<std::tuple<uint32_t, uint32_t>, uint32_t> vertices;

    std::vector<VertexPosition>   vertexPositions;
    std::vector<VertexAttributes> vertexAttributes;

    std::vector<TriangleIndices> indices;
    std::vector<TriangleParameters> triangleParams;

    using Edge = std::tuple<uint32_t, uint32_t>;

    std::map<Edge, uint32_t> edgeTriangleMap;

    uint32_t vcount = 0;

    for (auto const [ v0, v1, v2 ] : model.Triangles)
    {
        auto const [ p0, n0 ] = v0;
        auto const [ p1, n1 ] = v1;
        auto const [ p2, n2 ] = v2;

        auto&& addTriangle =
            [&](uint32_t p0, uint32_t p1, uint32_t p2, uint32_t n0, uint32_t n1, uint32_t n2)
            {
                auto& v0 = vertices[std::tuple{p0, n0}];
                auto& v1 = vertices[std::tuple{p1, n1}];
                auto& v2 = vertices[std::tuple{p2, n2}];
                if (v0 == 0) { v0 = ++vcount; vertexPositions.emplace_back(model.Positions[p0 - 1]); vertexAttributes.emplace_back(model.Normals[n0 - 1], 0xFFFFFFFFu); }
                if (v1 == 0) { v1 = ++vcount; vertexPositions.emplace_back(model.Positions[p1 - 1]); vertexAttributes.emplace_back(model.Normals[n1 - 1], 0xFFFFFFFFu); }
                if (v2 == 0) { v2 = ++vcount; vertexPositions.emplace_back(model.Positions[p2 - 1]); vertexAttributes.emplace_back(model.Normals[n2 - 1], 0xFFFFFFFFu); }
                indices.emplace_back(TriangleIndices{v0 - 1, v1 - 1, v2 - 1});
                triangleParams.emplace_back(0xFFFF'FFFFu);
            };


        addTriangle(p0, p1, p2, n0, n1, n2);

        if (indices.size() >= (0x100000 / 12) - 2)
        {
            result.push_back(CreateModel(uploader, renderDevice, { indices, triangleParams }, { vertexPositions, vertexAttributes }));

            vertices.clear();

            vertexPositions.clear();
            vertexAttributes.clear();

            indices.clear();
            triangleParams.clear();

            vcount = 0;
        }
    }

    if (!indices.empty())
    {
        result.push_back(CreateModel(uploader, renderDevice, { indices, triangleParams }, { vertexPositions, vertexAttributes }));
    }

    return result;
}



struct Assets
{
    std::shared_ptr<VisibilityPass>  visibilityPass;
    std::shared_ptr<ComputableGatherPass> computableGatherPass;
    std::shared_ptr<ColorPass>       colorPass;
    std::shared_ptr<PostprocessPass> postprocessPass;
    std::shared_ptr<ResolvePass>     resolvePass;

    std::shared_ptr<ModelManager   > modelManager;
    std::shared_ptr<InstanceManager> instanceManager;

    Model::Id                        cube;
    Model::Id                        objModel;
    Instance::Id                     cubeInstances[2];
    std::vector<Instance::Id>        objModelInstances;
};

Assets LoadAssets(RenderDevice& renderDevice)
{
    Assets assets{};

    AssetUploader uploader{ renderDevice };

    //{
    //    assets.objectTargetSampleWrapDescriptorIndex = renderDevice.samplerDescriptorHeap.AllocateDescriptors(1);
    //    auto handle = renderDevice.samplerDescriptorHeap.GetCpuDescriptorHandle(assets.objectTargetSampleWrapDescriptorIndex);
    //
    //    D3D12_SAMPLER_DESC const wrapSamplerDesc
    //    {
    //        .Filter         = D3D12_FILTER_MIN_MAG_MIP_LINEAR,
    //        .AddressU       = D3D12_TEXTURE_ADDRESS_MODE_WRAP,
    //        .AddressV       = D3D12_TEXTURE_ADDRESS_MODE_WRAP,
    //        .AddressW       = D3D12_TEXTURE_ADDRESS_MODE_WRAP,
    //        .MipLODBias     = 0.0f,
    //        .MaxAnisotropy  = 1,
    //        .ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS,
    //        .BorderColor    {},
    //        .MinLOD         = 0,
    //        .MaxLOD         = D3D12_FLOAT32_MAX,
    //    };
    //    renderDevice.device->CreateSampler(&wrapSamplerDesc, handle);
    //}

    assets.visibilityPass   = std::make_shared<VisibilityPass> (renderDevice);
    assets.computableGatherPass = std::make_shared<ComputableGatherPass>      (renderDevice);
    assets.colorPass        = std::make_shared<ColorPass>      (renderDevice);
    assets.postprocessPass  = std::make_shared<PostprocessPass>(renderDevice);
    assets.resolvePass      = std::make_shared<ResolvePass>    (renderDevice);
    assets.modelManager     = std::make_shared<ModelManager>   (renderDevice);
    assets.instanceManager  = std::make_shared<InstanceManager>(renderDevice);
    assets.cube             = assets.modelManager->AllocateNewModel(renderDevice, CreateCubeModel(uploader, renderDevice));
    assets.cubeInstances[0] = assets.instanceManager->AllocateNewInstance(renderDevice, *assets.modelManager, Instance{assets.cube});
    assets.cubeInstances[1] = assets.instanceManager->AllocateNewInstance(renderDevice, *assets.modelManager, Instance{assets.cube});
    
    {
        auto& instance = assets.instanceManager->GetInstance(assets.cubeInstances[1]);

        instance.Yaw   = static_cast<uint16_t>(rand());
        instance.Pitch = static_cast<uint16_t>(rand());
        instance.Roll  = static_cast<uint16_t>(rand());
    }

    auto objModel = LoadObjModel(uploader, renderDevice);
    RecenterObjModelPositions(objModel);
    AddObjModelMissingNormals(objModel);

    for (auto& model : UploadObjModel(objModel, uploader, renderDevice))
    {
        assets.objModel = assets.modelManager->AllocateNewModel(renderDevice, std::move(model));
        assets.objModelInstances.push_back(assets.instanceManager->AllocateNewInstance(renderDevice, *assets.modelManager, Instance{assets.objModel}));
    }

    return assets;
}

void RenderFrame(RenderDevice& renderDevice, Assets const& assets)
{
    auto const& commandAllocator = renderDevice.commandAllocators[renderDevice.frameIndex];

    // Command list allocators can only be reset when the associated 
    // command lists have finished execution on the GPU; apps should use 
    // fences to determine GPU execution progress.
    ThrowIfFailed(commandAllocator->Reset());

    // However, when ExecuteCommandList() is called on a particular command 
    // list, that command list can then be reset at any time and must be before 
    // re-recording.
    ThrowIfFailed(renderDevice.commandList->Reset(commandAllocator.Get(), nullptr));

    assets.visibilityPass->Render(renderDevice, *assets.modelManager, *assets.instanceManager);

    assets.computableGatherPass->Render(renderDevice, *assets.visibilityPass, *assets.modelManager, *assets.instanceManager);

    assets.colorPass->Render(renderDevice, *assets.visibilityPass, *assets.modelManager, *assets.instanceManager);

    assets.postprocessPass->Render(renderDevice, *assets.visibilityPass, *assets.colorPass);

    renderDevice.StartFrame();

    assets.resolvePass->Render(renderDevice, *assets.colorPass);

    renderDevice.PresentFrame();
}

XMMATRIX XMMatrixPerspectiveFovInfiniteRH(float FovAngleY, float AspectRatio, float NearZ) noexcept
{
    assert(NearZ > 0.f);
    assert(!XMScalarNearEqual(FovAngleY, 0.0f, 0.00001f * 2.0f));
    assert(!XMScalarNearEqual(AspectRatio, 0.0f, 0.00001f));

    float    SinFov;
    float    CosFov;
    XMScalarSinCos(&SinFov, &CosFov, 0.5f * FovAngleY);

    float Height = CosFov / SinFov;
    float Width = Height / AspectRatio;

    XMFLOAT4X4 M;
    M.m[0][0] = Width;
    M.m[0][1] = 0.0f;
    M.m[0][2] = 0.0f;
    M.m[0][3] = 0.0f;

    M.m[1][0] = 0.0f;
    M.m[1][1] = Height;
    M.m[1][2] = 0.0f;
    M.m[1][3] = 0.0f;

    M.m[2][0] = 0.0f;
    M.m[2][1] = 0.0f;
    M.m[2][2] = 0.0f;
    M.m[2][3] = -1.0f;

    M.m[3][0] = 0.0f;
    M.m[3][1] = 0.0f;
    M.m[3][2] = NearZ;
    M.m[3][3] = 0.0f;
    return XMLoadFloat4x4(&M);
}

XMMATRIX XMMatrixInversePerspectiveFovInfiniteRH(float FovAngleY, float AspectRatio, float NearZ) noexcept
{
    assert(NearZ > 0.f);
    assert(!XMScalarNearEqual(FovAngleY, 0.0f, 0.00001f * 2.0f));
    assert(!XMScalarNearEqual(AspectRatio, 0.0f, 0.00001f));

    float    SinFov;
    float    CosFov;
    XMScalarSinCos(&SinFov, &CosFov, 0.5f * FovAngleY);

    float Height = CosFov / SinFov;
    float Width = Height / AspectRatio;

    XMFLOAT4X4 M;
    M.m[0][0] = Width;
    M.m[0][1] = 0.0f;
    M.m[0][2] = 0.0f;
    M.m[0][3] = 0.0f;

    M.m[1][0] = 0.0f;
    M.m[1][1] = Height;
    M.m[1][2] = 0.0f;
    M.m[1][3] = 0.0f;

    M.m[2][0] = 0.0f;
    M.m[2][1] = 0.0f;
    M.m[2][2] = 0.0f;
    M.m[2][3] = -1.0f;

    M.m[3][0] = 0.0f;
    M.m[3][1] = 0.0f;
    M.m[3][2] = NearZ;
    M.m[3][3] = 0.0f;
    return XMLoadFloat4x4(&M);
}

// Prepare to render the next frame.
void AnimateAssets(RenderDevice const& renderDevice, Assets const& assets)
{
    XMFLOAT3 const eye{ 0, 10, -10 };
    XMFLOAT3 const at { 0, 0,  0 };
    XMFLOAT3 const up { 0, 1,  0 };

    View = XMMatrixLookAtRH(XMLoadFloat3(&eye), XMLoadFloat3(&at), XMLoadFloat3(&up));

    float const fovAngleY = 90.0f;
    float const fovRadiansY = fovAngleY * XM_PI / 180.0f;
    Projection = XMMatrixPerspectiveFovInfiniteRH(fovRadiansY, 1280.0 / 720.0, 0.0001f);

    InverseProjection = XMMatrixInverse(nullptr, Projection);

    ViewProj = View * Projection;

    UpdateModelInstance(renderDevice, *assets.instanceManager, assets.cubeInstances[0]);
    UpdateModelInstance(renderDevice, *assets.instanceManager, assets.cubeInstances[1]);
    for (auto instanceId : assets.objModelInstances)
    {
        UpdateStaticModelInstance(renderDevice, *assets.instanceManager, instanceId);
    }
}

RenderDevice* pRenderDevice;
Assets const* pAssets      ;

// Main message handler for the sample.
LRESULT CALLBACK WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_CREATE:
        {
            // Save the DXSample* passed in to CreateWindow.
            LPCREATESTRUCT pCreateStruct = reinterpret_cast<LPCREATESTRUCT>(lParam);
            SetWindowLongPtr(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pCreateStruct->lpCreateParams));
        }
        return 0;

    case WM_KEYDOWN:
        // OnKeyDown(static_cast<UINT8>(wParam));
        return 0;

    case WM_KEYUP:
        // OnKeyUp(static_cast<UINT8>(wParam));
        return 0;

    case WM_PAINT:
        // Record all the commands we need to render the scene into the command list.
        RenderFrame(*pRenderDevice, *pAssets);

        AnimateAssets(*pRenderDevice, *pAssets);

        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }

    // Handle any messages the switch statement didn't.
    return DefWindowProc(hWnd, message, wParam, lParam);
}

static std::wstring GetLatestWinPixGpuCapturerPath_Cpp17()
{
    LPWSTR programFilesPath = nullptr;
    SHGetKnownFolderPath(FOLDERID_ProgramFiles, KF_FLAG_DEFAULT, NULL, &programFilesPath);

    std::filesystem::path pixInstallationPath = programFilesPath;
    pixInstallationPath /= "Microsoft PIX";

    std::wstring newestVersionFound;

    for (auto const& directory_entry : std::filesystem::directory_iterator(pixInstallationPath))
    {
        if (directory_entry.is_directory())
        {
            if (newestVersionFound.empty() || newestVersionFound < directory_entry.path().filename().c_str())
            {
                newestVersionFound = directory_entry.path().filename().c_str();
            }
        }
    }

    if (newestVersionFound.empty())
    {
        // TODO: Error, no PIX installation found
    }

    return pixInstallationPath / newestVersionFound / L"WinPixGpuCapturer.dll";
}

_Use_decl_annotations_
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow)
{
    LoadLibraryW(GetLatestWinPixGpuCapturerPath_Cpp17().c_str());

    // Initialize the window class.
    WNDCLASSEX windowClass{
        .cbSize        = sizeof(WNDCLASSEX),
        .style         = CS_HREDRAW | CS_VREDRAW,
        .lpfnWndProc   = WindowProc,
        .hInstance     = hInstance,
        .hCursor       = LoadCursor(NULL, IDC_ARROW),
        .lpszClassName = L"GraphicsTestClass",
    };
    RegisterClassEx(&windowClass);

    RECT windowRect{ 0, 0, 1280, 720 };
    AdjustWindowRect(&windowRect, WS_OVERLAPPEDWINDOW, FALSE);

    // Create the window and store a handle to it.
    auto const hwnd = CreateWindow(
        windowClass.lpszClassName,
        L"Graphics test",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        windowRect.right - windowRect.left,
        windowRect.bottom - windowRect.top,
        nullptr,        // We have no parent window.
        nullptr,        // We aren't using menus.
        hInstance,
        nullptr         // lParam
    );

    auto       renderDevice = RenderDevice::Create(hwnd);
    auto const assets       = LoadAssets(renderDevice);

    pRenderDevice = &renderDevice;
    pAssets       = &assets      ;

    ShowWindow(hwnd, nCmdShow);

    // Main sample loop.
    MSG msg = {};
    while (msg.message != WM_QUIT)
    {
        // Process any messages in the queue.
        if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        else if (GetMessage(&msg, NULL, 0, 0))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    renderDevice.WaitForGpuIdle();

    return 0;
}
