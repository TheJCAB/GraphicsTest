// GraphicsTest.cpp : Defines the entry point for the application.
//
#pragma once

#if HLSL

#include "HLSL.h"

#else // !HLSL

#include "JcabD3D.h"

#include <shlobj.h>

#include <string_view>
#include <string>
#include <array>
#include <span>
#include <vector>
#include <map>
#include <filesystem>

#endif // !HLSL



// Define some types.

#if HLSL

typedef uint TextureId;

#else // !HLSL

enum TextureId : uint32_t { Invalid = 0 };

#endif // !HLSL

// Shared with the shader.
#define MAX_MODEL_DEFINITIONS     100
#define MAX_INSTANCE_DEFINITIONS 1000
#define MAX_MATERIAL_DEFINITIONS 1000
#define MAX_TEXTURES             1000

#define MATERIAL_FLAG_ALPHATEST 0x00000001

struct MaterialParameters
{
    XMFLOAT3  AmbientColor;
    XMFLOAT3  DiffuseColor;
    XMFLOAT3  SpecularColor;
    float     SpecularExponent;
    TextureId DiffuseTextureId;
    uint32_t  Flags;
};

typedef XMFLOAT3 VertexPosition;

struct VertexAttributes
{
    XMFLOAT3 Normal;
    XMFLOAT2 UV;
    uint32_t Color;
};

struct TriangleParameters
{
    uint32_t Color;
    uint32_t Patch;
    uint32_t MaterialIndex;
};

typedef XMUINT3 TriangleIndices;

struct InstanceAttributes
{
    XMFLOAT4X4 View;
    XMFLOAT4X4 Projection;

    XMFLOAT4X4 InverseViewAndLastFrameView;

    uint32_t   ModelIndex;
    uint32_t   Padding[3];
};


#if HLSL

StructuredBuffer<TriangleIndices>     indices         [MAX_MODEL_DEFINITIONS] : register(t1, space1);
StructuredBuffer<TriangleParameters>  triangleParams  [MAX_MODEL_DEFINITIONS] : register(t1, space2);
StructuredBuffer<VertexPosition>      vertexPositions [MAX_MODEL_DEFINITIONS] : register(t1, space3);
StructuredBuffer<VertexAttributes>    vertexAttributes[MAX_MODEL_DEFINITIONS] : register(t1, space4);
Texture2D<float4>                     textures        [MAX_TEXTURES         ] : register(t1, space5);
StructuredBuffer<MaterialParameters>  materials                               : register(t0, space5);

StructuredBuffer<InstanceAttributes> instanceList : register(t0, space0);

sampler defaultSampler : register(s0, space0);

#else // !HLSL

inline bool g_animate = false;

inline XMMATRIX View;
inline XMMATRIX InverseView;
inline XMMATRIX Projection;
inline XMMATRIX InverseProjection;
inline XMMATRIX ViewProj;


struct Texture
{
    using Id = TextureId;

    std::filesystem::path  Path;
    ComPtr<ID3D12Resource> Resource;
    bool                   HasAlpha = false;
};

struct TextureManager
{
    uint32_t                          TextureCount{};
    std::array<Texture, MAX_TEXTURES> TextureList{};
    UINT                              FirstSrv = 0;

    TextureManager(RenderDevice&);
    Texture::Id AllocateNewTexture(RenderDevice&, AssetUploader&, std::string_view pathName);

    UINT GetTexturesDescriptorTable() const { return FirstSrv; }

    Texture::Id FindTexture(std::string_view path);

    uint32_t GetTextureIndex(Texture::Id const id) const
    {
        assert(id != Texture::Id::Invalid);
        auto const index = static_cast<uint32_t>(id) - 1;
        assert(index < TextureList.size());
        assert(index < TextureCount);
        return index;
    }

    Texture&       GetTexture(Texture::Id id)       { return TextureList[GetTextureIndex(id)]; }
    Texture const& GetTexture(Texture::Id id) const { return TextureList[GetTextureIndex(id)]; }
};

struct Material
{
    enum Id : uint32_t { Invalid = 0 };

    XMFLOAT3    AmbientColor;
    XMFLOAT3    DiffuseColor;
    XMFLOAT3    SpecularColor;
    float       SpecularExponent;
    std::string DiffuseTexturePathName;
};

struct MaterialManager
{
    UINT                                                         MaterialCount = 0;
    ConstantBuffer<MaterialParameters, MAX_MATERIAL_DEFINITIONS> MaterialList  {};
    UINT                                                         Srv = 0;

    MaterialManager(RenderDevice&);
    Material::Id AllocateNewMaterial(RenderDevice&, AssetUploader&, TextureManager&, Material&&);

    UINT GetMaterialsSrv() const { return Srv; }

    uint32_t GetMaterialIndex(Material::Id const id) const
    {
        assert(id != Material::Id::Invalid);
        auto const index = static_cast<uint32_t>(id) - 1;
        assert(index < MaterialList.Get().size());
        assert(index < MaterialCount);
        return index;
    }

    //Material&       GetMaterial(Material::Id id)       { return MaterialList[GetMaterialIndex(id)]; }
    //Material const& GetMaterial(Material::Id id) const { return MaterialList[GetMaterialIndex(id)]; }
};




struct Model
{
    enum class Id : uint32_t { Invalid = 0 };

    uint32_t                 TriangleCount            = 0;
    ComPtr<ID3D12Resource>   TriangleIndices          {};
    D3D12_INDEX_BUFFER_VIEW  TriangleIndicesView      {};
    ComPtr<ID3D12Resource>   TriangleParametersBuffer {};

    uint32_t                 VertexCount               = 0;
    ComPtr<ID3D12Resource>   VertexPositionsBuffer     {};
    D3D12_VERTEX_BUFFER_VIEW VertexPositionsBufferView {};
    ComPtr<ID3D12Resource>   VertexAttributesBuffer    {};
    D3D12_VERTEX_BUFFER_VIEW VertexAttributesBufferView{};

    bool UsesAlphaTest = false;
};

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

Model CreateModel(AssetUploader&, RenderDevice&, ModelTrianglesDefinition const&, ModelVerticesDefinition const&, bool usesAlphaTest);

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


struct Instance
{
    enum Id : uint32_t { Invalid = 0 };

    Model::Id ModelId = Model::Id::Invalid;

    float Scale = 1.0f;
    XMVECTOR Pos{};

    uint16_t Yaw   = 0;
    uint16_t Pitch = 0;
    uint16_t Roll  = 0;

    XMMATRIX View;
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

struct SceneGraphAssets
{
    ::TextureManager  TextureManager ;
    ::MaterialManager MaterialManager;
    ::ModelManager    ModelManager   ;
    ::InstanceManager InstanceManager;

    SceneGraphAssets(RenderDevice& renderDevice)
        : TextureManager { renderDevice }
        , MaterialManager{ renderDevice }
        , ModelManager   { renderDevice }
        , InstanceManager{ renderDevice }
    {}
};

struct VisibilityPass
{
    ComPtr<ID3D12RootSignature> RootSignature;
    ComPtr<ID3D12PipelineState> SolidPipelineState;
    ComPtr<ID3D12PipelineState> AlphaTestPipelineState;

    VisibilityPass(RenderDevice&, uint32_t msaa);

    void Render(RenderDevice const&, SceneGraphAssets const&, XMFLOAT2 const& normalizedPixelOffset, IntermediateRenderTarget const&, IntermediateDepthBuffer const&) const;
};

struct RootSignatureIndices
{
    uint32_t RootConstants;
    uint32_t InstanceArray;
    uint32_t TriangleParametersArray;
    uint32_t TextureArray;
    uint32_t MaterialsBuffer;
    uint32_t Sampler;
};

void SetupVisibilityRootConstants(ComRef<ID3D12GraphicsCommandList>, RootSignatureIndices const&, SceneGraphAssets const&);

#endif // !HLSL
