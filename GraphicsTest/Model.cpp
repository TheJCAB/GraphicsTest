// GraphicsTest.cpp : Defines the entry point for the application.
//

#include "Model.h"

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

    if (g_animate)
    {
        instance.Yaw   += 0x50;
        instance.Pitch += 0x77;
        instance.Roll  += 0x23;
    }

    XMMATRIX const modelMatrix = XMMatrixRotationRollPitchYaw(instance.Pitch * (2.0f * XM_PI / 65536.0f), instance.Yaw * (2.0f * XM_PI / 65536.0f), instance.Roll * (2.0f * XM_PI / 65536.0));
    XMMATRIX const translation = XMMatrixTranslation(2, 0, 0);

    auto& attributes = instanceManager.GetCurrentFrameInstanceAttributes(renderDevice, instanceId);
    XMStoreFloat4x4(&attributes.View      , XMMatrixTranspose(modelMatrix * translation * View    ));
    XMStoreFloat4x4(&attributes.Projection, XMMatrixTranspose(modelMatrix * translation * ViewProj));
};

void UpdateStaticModelInstance(RenderDevice const& renderDevice, InstanceManager& instanceManager, Instance::Id const instanceId)
{
    auto& instance = instanceManager.GetInstance(instanceId);

    if (g_animate)
    {
        instance.Yaw += 5;
    }

    XMMATRIX const modelMatrix = XMMatrixRotationRollPitchYaw(instance.Pitch * (2.0f * XM_PI / 65536.0f), instance.Yaw * (2.0f * XM_PI / 65536.0f), instance.Roll * (2.0f * XM_PI / 65536.0));

    XMMATRIX const translation = XMMatrixTranslationFromVector(instance.Pos);

    auto& attributes = instanceManager.GetCurrentFrameInstanceAttributes(renderDevice, instanceId);
    XMStoreFloat4x4(&attributes.View      , XMMatrixTranspose(modelMatrix * translation * View    ));
    XMStoreFloat4x4(&attributes.Projection, XMMatrixTranspose(modelMatrix * translation * ViewProj));
};
