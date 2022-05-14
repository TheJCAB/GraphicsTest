// GraphicsTest.cpp : Defines the entry point for the application.
//
#pragma once

#include "JcabD3D.h"

#include <shlobj.h>

#include <array>
#include <span>
#include <vector>
#include <map>
#include <filesystem>

inline bool g_animate = false;

// Shared with the shader.
#define MAX_MODEL_DEFINITIONS     100
#define MAX_INSTANCE_DEFINITIONS 1000


inline XMMATRIX View;
inline XMMATRIX Projection;
inline XMMATRIX InverseProjection;
inline XMMATRIX ViewProj;


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

Model CreateModel(AssetUploader& uploader, RenderDevice& renderDevice, ModelTrianglesDefinition const& triangles, ModelVerticesDefinition const& vertices);

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

    XMVECTOR Pos{};

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

void UpdateModelInstance(RenderDevice const& renderDevice, InstanceManager& instanceManager, Instance::Id const instanceId);
void UpdateStaticModelInstance(RenderDevice const& renderDevice, InstanceManager& instanceManager, Instance::Id const instanceId);
