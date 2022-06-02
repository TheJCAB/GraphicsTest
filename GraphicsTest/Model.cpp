// GraphicsTest.cpp : Defines the entry point for the application.
//

#include "Model.h"

#include "Model_VisibilitySolidVSShader.h"
#include "Model_VisibilitySolidPSShader.h"
#include "Model_VisibilityAlphaTestVSShader.h"
#include "Model_VisibilityAlphaTestPSShader.h"

#include <gdiplus.h>

uint8_t SaturateByte(int value)
{
    return
            value <   0 ?   0 :
            value > 255 ? 255 :
                          static_cast<uint8_t>(value);
}

TextureManager::TextureManager(RenderDevice& renderDevice)
{
    FirstSrv = renderDevice.srvDescriptorHeap.AllocateDescriptors(MAX_TEXTURES);
}

std::unique_ptr<Gdiplus::Bitmap> LoadGdiplusImage(std::string_view const pathName)
{
    struct GdiplusStartup
    {
        ULONG_PTR gdiplusToken = UINTPTR_MAX;
        Gdiplus::GdiplusStartupInput gdiplusStartupInput;

        GdiplusStartup()
        {
            Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);
        }
    };
    static GdiplusStartup gdiplusStartup{};

    // Warning! Sooper-cheesy char to wchar_t conversion!
    return std::make_unique<Gdiplus::Bitmap>(std::wstring{pathName.begin(), pathName.end()}.c_str());
}

Texture::Id TextureManager::AllocateNewTexture(RenderDevice& renderDevice, AssetUploader& assetUploader, std::string_view const pathName, DXGI_FORMAT const format)
{
    if (auto const id = FindTexture(pathName))
    {
        return id;
    }

    auto bitmap = LoadGdiplusImage(pathName);
    Gdiplus::SizeF size;
    bitmap->GetPhysicalDimension(&size);
    auto const width       = bitmap->GetWidth();
    auto const height      = bitmap->GetHeight();
    auto const pixelFormat = bitmap->GetPixelFormat();
    Gdiplus::BitmapData bitmapData{};
    Gdiplus::Rect const dimensions{ 0, 0, static_cast<INT>(width), static_cast<INT>(height) };
    bitmap->LockBits(&dimensions, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &bitmapData);

    assert(TextureCount < TextureList.size());
    auto const index = TextureCount;
    auto const id = Texture::Id{ index + 1 };
    ++TextureCount;

    TextureList[index].Path = pathName;
    TextureList[index].Resource = assetUploader.CreateStaticTexture(width, height, format, 0, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE,
        [&](uint32_t x, uint32_t y, uint32_t w, void* p)
        {
            auto const pSrc = reinterpret_cast<uint32_t const*>(reinterpret_cast<uintptr_t>(bitmapData.Scan0) + (height - 1 - y) * bitmapData.Stride + x * 4);
            for (uint32_t i = 0; i < w; ++i)
            {
                reinterpret_cast<uint32_t*>(p)[i] = ((pSrc[i] >> 16) & 0xFF) | ((pSrc[i] & 0xFF) << 16) | (pSrc[i] & 0xFF00FF00);
            }
        }
    );
    TextureList[index].HasAlpha = (pixelFormat & PixelFormatAlpha) != 0;

    bitmap->UnlockBits(&bitmapData);

    renderDevice.UploadSrvDescriptor(FirstSrv + index, TextureList[index].Resource);

    return id;
}

Texture::Id TextureManager::AllocateNewElevationTexture(RenderDevice& renderDevice, AssetUploader& assetUploader, std::string_view const pathName, DXGI_FORMAT const format)
{
    if (auto const id = FindTexture(pathName))
    {
        return id;
    }

    auto bitmap = LoadGdiplusImage(pathName);
    Gdiplus::SizeF size;
    bitmap->GetPhysicalDimension(&size);
    auto const width       = bitmap->GetWidth();
    auto const height      = bitmap->GetHeight();
    auto const pixelFormat = bitmap->GetPixelFormat();
    Gdiplus::BitmapData bitmapData{};
    Gdiplus::Rect const dimensions{ 0, 0, static_cast<INT>(width), static_cast<INT>(height) };
    bitmap->LockBits(&dimensions, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &bitmapData);
    using BgraPixel = std::array<uint8_t, 4>;
    auto bitmapAddress = reinterpret_cast<uintptr_t>(bitmapData.Scan0);
    std::vector<BgraPixel> bumpPixels;
    bumpPixels.resize(bitmapData.Width * bitmapData.Height);
    for(uint32_t y = 0; y < bitmapData.Height; ++y)
    {
        auto const previousScan = reinterpret_cast<BgraPixel const*>(bitmapAddress + ((y + height - 1) % height) * bitmapData.Stride);
        auto const currentScan  = reinterpret_cast<BgraPixel const*>(bitmapAddress +   y                         * bitmapData.Stride);
        auto const nextScan     = reinterpret_cast<BgraPixel const*>(bitmapAddress + ((y +          1) % height) * bitmapData.Stride);
        auto const destScan     = bumpPixels.data() + (height - 1 - y) * width;

        for(uint32_t x = 0; x < bitmapData.Width; ++x)
        {
            auto const previousIndex = (x + width - 1) % width;
            auto const currentIndex  =  x                     ;
            auto const nextIndex     = (x +         1) % width;
            int const bx = currentScan[nextIndex][1] - currentScan[previousIndex][1];
            int const by = previousScan[currentIndex][1] - previousScan[currentIndex][1];
            destScan[currentIndex][2] = SaturateByte((bx * 2) + 128);
            destScan[currentIndex][1] = SaturateByte((by * 2) + 128);
            destScan[currentIndex][0] = static_cast<uint8_t>(255);
        }
    }

    assert(TextureCount < TextureList.size());
    auto const index = TextureCount;
    auto const id = Texture::Id{ index + 1 };
    ++TextureCount;

    TextureList[index].Path = pathName;
    TextureList[index].Resource = assetUploader.CreateStaticTexture(width, height, format, 0, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE,
        [&](uint32_t x, uint32_t y, uint32_t w, void* p)
        {
            auto const pSrc = bumpPixels.data() + y * width + x;
            for (uint32_t i = 0; i < w; ++i)
            {
                reinterpret_cast<uint32_t*>(p)[i] = static_cast<uint32_t>(pSrc[i][2] | (pSrc[i][1] << 8) | (pSrc[i][0] << 16) | (pSrc[i][3] << 24));
            }
        }
    );
    TextureList[index].HasAlpha = false;

    bitmap->UnlockBits(&bitmapData);

    renderDevice.UploadSrvDescriptor(FirstSrv + index, TextureList[index].Resource);

    return id;
}

Texture::Id TextureManager::AllocateNewHdrMap(RenderDevice& renderDevice, AssetUploader& assetUploader, std::string_view pathName)
{
    if (auto const id = FindTexture(pathName))
    {
        return id;
    }

    auto const f = fopen(std::string{pathName.begin(), pathName.end()}.c_str(), "rb");
    if (f == nullptr)
    {
        return Texture::Id::Invalid;
    }

    bool isValid = false;
    uint32_t width = 0;
    uint32_t height = 0;

    while (!feof(f))
    {
        std::array<char, 1024> buffer;
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
        if (line == "#?RADIANCE")
        {
            isValid = true;
        }
        if (line.starts_with("-Y "))
        {
            sscanf_s(line.data(), "-Y %u +X %u", &height, &width);
            break;
        }
    }

    auto&& convertColor = [](int r, int g, int b, int e) -> std::array<float, 3>
        {
            if (e == 0)
            {
                return { 0.0f, 0.0f, 0.0f };
            }
            else
            {
                auto const mul = ldexp(1.0, e - (128 + 8));
                return {
                    static_cast<float>((r + 0.5) * mul),
                    static_cast<float>((g + 0.5) * mul),
                    static_cast<float>((b + 0.5) * mul),
                };
            }
        };

    std::vector<std::array<float, 3>> pixels;
    pixels.resize(width * height);

    for (uint32_t y = 0; y < height; ++y)
    {
        auto const p = pixels.data() + y * width;
        int shift = 0;
        for (uint32_t x = 0; x < width;)
        {
            auto const r = fgetc(f);
            auto const g = fgetc(f);
            auto const b = fgetc(f);
            auto const e = fgetc(f);
            if (x == 0 && r == 2 && g == 2 && (b & 128) == 0)
            {
                // More complex conversion.
                assert(static_cast<uint32_t>((b << 8) + e) == width);
                std::vector<int> components[4];
                for (auto& component : components)
                {
                    x = 0;
                    component.resize(width);
                    while (x < width)
                    {
                        auto const code = fgetc(f);
                        if (code > 128)
                        {
                            auto const value = fgetc(f);
                            auto const run = x + (code & 127);
                            for (; x < run; ++x)
                            {
                                component[x] = value;
                            }
                        }
                        else
                        {
                            auto const run = x + code;
                            for (; x < run; ++x)
                            {
                                component[x] = fgetc(f);
                            }
                        }
                    }
                }
                for (x = 0; x < width; ++x)
                {
                    p[x] = convertColor(components[0][x], components[1][x], components[2][x], components[3][x]);
                }
            }
            else if (r == 1 && g == 1 && b == 1)
            {
                assert(x > 0 || y > 0);
                auto const run = x + e << shift;
                for (; x < run; ++x)
                {
                    p[x] = p[static_cast<int>(x) - 1];
                }
                shift += 8;
            }
            else
            {
                shift = 0;
                p[x] = convertColor(r, g, b, e);
                ++x;
            }
        }
    }

    assert(TextureCount < TextureList.size());
    auto const index = TextureCount;
    auto const id = Texture::Id{ index + 1 };
    ++TextureCount;

    TextureList[index].Path = pathName;
    TextureList[index].Resource = assetUploader.CreateStaticTexture(width, height, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE,
        [&](uint32_t x, uint32_t y, uint32_t w, void* p)
        {
            auto const pDest = reinterpret_cast<std::array<float, 4>*>(p);
            auto const pSrc = pixels.data() + (height - 1 - y) * width + x;
            for (uint32_t i = 0; i < w; ++i)
            {
                pDest[i][0] = pSrc[i][0];
                pDest[i][1] = pSrc[i][1];
                pDest[i][2] = pSrc[i][2];
                pDest[i][3] = 1.0f;
            }
        }
    );
    TextureList[index].HasAlpha = false;

    renderDevice.UploadSrvDescriptor(FirstSrv + index, TextureList[index].Resource);

    return id;
}

Texture::Id TextureManager::FindTexture(std::string_view pathName)
{
    std::filesystem::path path{ pathName };
    for (uint32_t i = 0; i < TextureCount; ++i)
    {
        if (TextureList[i].Path == path)
        {
            return static_cast<Texture::Id>(i + 1);
        }
    }
    return Texture::Id::Invalid;
}


MaterialManager::MaterialManager(RenderDevice& renderDevice)
{
    MaterialList = ConstantBuffer<MaterialParameters, MAX_MATERIAL_DEFINITIONS>::Create(renderDevice.device);
    Srv = renderDevice.AllocateSrvDescriptor(MaterialList.pResource,
        {
            .Format                  = DXGI_FORMAT_UNKNOWN,
            .ViewDimension           = D3D12_SRV_DIMENSION_BUFFER,
            .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
            .Buffer                  { .NumElements = MAX_MATERIAL_DEFINITIONS, .StructureByteStride = sizeof(MaterialParameters) },
        }
    );
}

Material::Id MaterialManager::AllocateNewMaterial(RenderDevice& renderDevice, AssetUploader& assetUploader, TextureManager& textureManager, Material&& material)
{
    assert(MaterialCount < MaterialList.Get().size());
    auto const index = MaterialCount;
    auto const id = Material::Id{ index + 1 };
    ++MaterialCount;

    MaterialParameters& params = MaterialList[index];
    params.AmbientColor     = material.AmbientColor;
    params.DiffuseColor     = material.DiffuseColor;
    params.SpecularColor    = material.SpecularColor;
    params.SpecularExponent = material.SpecularExponent;
    if (!material.DiffuseTexturePathName.empty())
    {
        params.DiffuseTextureId = textureManager.AllocateNewTexture(renderDevice, assetUploader, material.DiffuseTexturePathName);
        if (textureManager.GetTexture(params.DiffuseTextureId).HasAlpha)
        {
            params.Flags |= MATERIAL_FLAG_ALPHATEST;
        }
    }
    else
    {
        params.DiffuseTextureId = Texture::Id::Invalid;
    }
    if (!material.BumpTexturePathName.empty())
    {
        params.BumpTextureId =
            material.BumpTextureIsElevation
                ? textureManager.AllocateNewElevationTexture(renderDevice, assetUploader, material.BumpTexturePathName, DXGI_FORMAT_R8G8B8A8_UNORM)
                : textureManager.AllocateNewTexture(renderDevice, assetUploader, material.BumpTexturePathName, DXGI_FORMAT_R8G8B8A8_UNORM);
        if (textureManager.GetTexture(params.BumpTextureId).HasAlpha)
        {
            // ?
        }
    }
    else
    {
        params.BumpTextureId = Texture::Id::Invalid;
    }
    if (!material.SpecularTexturePathName.empty())
    {
        params.SpecularTextureId = textureManager.AllocateNewTexture(renderDevice, assetUploader, material.SpecularTexturePathName, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);
        if (textureManager.GetTexture(params.SpecularTextureId).HasAlpha)
        {
            // ?
        }
    }
    else
    {
        params.SpecularTextureId = Texture::Id::Invalid;
    }

    return id;
}



Model CreateModel(AssetUploader& uploader, RenderDevice& renderDevice, ModelTrianglesDefinition const& triangles, ModelVerticesDefinition const& vertices, bool const usesAlphaTest)
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

    model.VertexAttributesBufferView.BufferLocation = model.VertexAttributesBuffer->GetGPUVirtualAddress();
    model.VertexAttributesBufferView.StrideInBytes  = sizeof(VertexAttributes);
    model.VertexAttributesBufferView.SizeInBytes    = sizeof(VertexAttributes) * vertices.Count;

    model.TriangleParametersBuffer = uploader.CreateStaticBuffer(sizeof(TriangleParameters) * triangles.Count, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        [&](void* p)
        {
            triangles.ParametersFiller(static_cast<TriangleParameters*>(p));
        }
    );

    model.UsesAlphaTest = usesAlphaTest;

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

    auto const view        = modelMatrix * translation * View;
    auto const projection  = modelMatrix * translation * ViewProj;
    auto const inverseView = XMMatrixInverse(nullptr, view);

    auto& attributes = instanceManager.GetCurrentFrameInstanceAttributes(renderDevice, instanceId);
    XMStoreFloat4x4(&attributes.View      , XMMatrixTranspose(view      ));
    XMStoreFloat4x4(&attributes.Projection, XMMatrixTranspose(projection));
    XMStoreFloat4x4(&attributes.InverseViewAndLastFrameView, XMMatrixTranspose(inverseView * instance.View));

    instance.View = view;
}

void UpdateStaticModelInstance(RenderDevice const& renderDevice, InstanceManager& instanceManager, Instance::Id const instanceId)
{
    auto& instance = instanceManager.GetInstance(instanceId);

    if (g_animate)
    {
        instance.Yaw += 500;
    }

    XMMATRIX const modelMatrix = XMMatrixRotationRollPitchYaw(instance.Pitch * (2.0f * XM_PI / 65536.0f), instance.Yaw * (2.0f * XM_PI / 65536.0f), instance.Roll * (2.0f * XM_PI / 65536.0));

    XMMATRIX const scaling = XMMatrixScaling(instance.Scale, instance.Scale, instance.Scale);
    XMMATRIX const translation = XMMatrixTranslationFromVector(instance.Pos);

    auto const view        = scaling * modelMatrix * translation * View;
    auto const projection  = scaling * modelMatrix * translation * ViewProj;
    auto const inverseView = XMMatrixInverse(nullptr, view);

    auto& attributes = instanceManager.GetCurrentFrameInstanceAttributes(renderDevice, instanceId);
    XMStoreFloat4x4(&attributes.View      , XMMatrixTranspose(view      ));
    XMStoreFloat4x4(&attributes.Projection, XMMatrixTranspose(projection));
    XMStoreFloat4x4(&attributes.InverseViewAndLastFrameView, XMMatrixTranspose(inverseView * instance.View));

    instance.View = view;
}

VisibilityPass::VisibilityPass(RenderDevice& renderDevice, uint32_t msaa)
{
    // Create the rendering root signature.
    RootSignature = renderDevice.Create(
        Root::Signature<
            Root::Constants<D3D12_SHADER_VISIBILITY_ALL,                    0, 1                       , 1 >, // Instance constant
            Root::Table    <D3D12_SHADER_VISIBILITY_ALL, Root::SrvRange    <0, 1                       , 0>>, // Instance buffer
            Root::Table    <D3D12_SHADER_VISIBILITY_ALL, Root::SrvRange    <1, MAX_MODEL_DEFINITIONS   , 2>>, // Triangle paramters
            Root::Table    <D3D12_SHADER_VISIBILITY_ALL, Root::SrvRange    <1, MAX_TEXTURES            , 5>>, // Textures
            Root::Srv      <D3D12_SHADER_VISIBILITY_ALL,                    0                          , 5 >, // Materials
            Root::Table    <D3D12_SHADER_VISIBILITY_ALL, Root::SamplerRange<0, 2                       , 0>>,
            Root::Flags    <D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT>
        >{}
    );

    // Create the pipeline state, which includes compiling and loading shaders.
    SolidPipelineState = renderDevice.Create(Pipeline::State
        {
            .RootSignature         { RootSignature },
            .TopologyType          = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
            .InputLayout           = Vertex::InputLayout<Vertex::Input<Semantic::SV_Position, DXGI_FORMAT_R32G32B32_FLOAT, 0>>{},
            .VS                    = std::span{Model_VisibilitySolidVSShader},
            .PS                    = std::span{Model_VisibilitySolidPSShader},
            .DepthStencilState     = DefaultDepthStencilDesc,
            .Multisampling         = Pipeline::MultisamplingState{ msaa },
            .RTVFormats            { DXGI_FORMAT_R32_UINT, },
            .DSVFormat             = DXGI_FORMAT_D32_FLOAT,
        }
    );

    // Create the pipeline state, which includes compiling and loading shaders.
    AlphaTestPipelineState = renderDevice.Create(Pipeline::State
        {
            .RootSignature         { RootSignature },
            .TopologyType          = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
            .InputLayout           = Vertex::InputLayout<
                                        Vertex::Input<Semantic::SV_Position, DXGI_FORMAT_R32G32B32_FLOAT, 0                             , 0>,
                                        Vertex::Input<Semantic::UV         , DXGI_FORMAT_R32G32_FLOAT   , offsetof(VertexAttributes, UV), 1>
                                    >{},
            .VS                    = std::span{Model_VisibilityAlphaTestVSShader},
            .PS                    = std::span{Model_VisibilityAlphaTestPSShader},
            .DepthStencilState     = DefaultDepthStencilDesc,
            .Multisampling         = Pipeline::MultisamplingState{ msaa },
            .RTVFormats            { DXGI_FORMAT_R32_UINT, },
            .DSVFormat             = DXGI_FORMAT_D32_FLOAT,
        }
    );
}

void VisibilityPass::Render(RenderDevice const& renderDevice, SceneGraphAssets const& sceneGraphAssets, XMFLOAT2 const& pixelOffset, IntermediateRenderTarget const& visibilityBuffer, IntermediateDepthBuffer const& depthBuffer) const
{
    renderDevice.commandList->SetGraphicsRootSignature(RootSignature.Get());
    renderDevice.commandList->SetPipelineState        (SolidPipelineState.Get());

    ID3D12DescriptorHeap* const heaps[]{ renderDevice.srvDescriptorHeap, renderDevice.samplerDescriptorHeap };
    renderDevice.commandList->SetDescriptorHeaps(2, heaps);

    auto const rtvHandle = renderDevice.rtvDescriptorHeap.GetCpuDescriptorHandle(visibilityBuffer.RtvDescriptorIndex);
    auto const dsvHandle = renderDevice.dsvDescriptorHeap.GetCpuDescriptorHandle(depthBuffer     .DsvDescriptorIndex);
    renderDevice.commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);

    D3D12_VIEWPORT const viewport
    {
        .TopLeftX = 0.5f + pixelOffset.x,
        .TopLeftY = 0.5f + pixelOffset.y,
        .Width    = 0.5f + pixelOffset.x + static_cast<float>(visibilityBuffer.Width),
        .Height   = 0.5f + pixelOffset.y + static_cast<float>(visibilityBuffer.Height),
        .MinDepth = D3D12_MIN_DEPTH,
        .MaxDepth = D3D12_MAX_DEPTH,
    };
    D3D12_RECT const scissorRect
    {
        .left   = 0,
        .top    = 0,
        .right  = static_cast<LONG >(depthBuffer.Width ),
        .bottom = static_cast<LONG >(depthBuffer.Height),
    };

    renderDevice.commandList->RSSetViewports    (1, &viewport);
    renderDevice.commandList->RSSetScissorRects (1, &scissorRect);

    renderDevice.commandList->ClearRenderTargetView(rtvHandle, visibilityBuffer.ClearColor.data(), 0, nullptr);
    renderDevice.commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, depthBuffer.ClearValue, 0, 0, nullptr);
    renderDevice.commandList->SetGraphicsRootDescriptorTable   (1, renderDevice.srvDescriptorHeap.GetGpuDescriptorHandle(sceneGraphAssets.InstanceManager.GetCurrentFrameInstanceAttributes(renderDevice).SrvIndex));
    renderDevice.commandList->SetGraphicsRootDescriptorTable   (2, renderDevice.srvDescriptorHeap.GetGpuDescriptorHandle(sceneGraphAssets.ModelManager   .GetTriangleParametersDescriptorTable()));
    renderDevice.commandList->SetGraphicsRootDescriptorTable   (3, renderDevice.srvDescriptorHeap.GetGpuDescriptorHandle(sceneGraphAssets.TextureManager .GetTexturesDescriptorTable          ()));
    renderDevice.commandList->SetGraphicsRootShaderResourceView(4, sceneGraphAssets.MaterialManager.MaterialList.pResource->GetGPUVirtualAddress());
    renderDevice.commandList->SetGraphicsRootDescriptorTable   (5, renderDevice.samplerDescriptorHeap.GetGpuDescriptorHandle(0)); // TODO Samplers

    for (auto instanceId : sceneGraphAssets.InstanceManager)
    {
        auto& instance = sceneGraphAssets.InstanceManager.GetInstance(instanceId);
        if (instance.ModelId == Model::Id::Invalid)
        {
            continue;
        }
        auto& model = sceneGraphAssets.ModelManager.GetModel(instance.ModelId);
        if (!model.UsesAlphaTest)
        {
            renderDevice.commandList->IASetPrimitiveTopology      (D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            renderDevice.commandList->IASetIndexBuffer            (&model.TriangleIndicesView);
            renderDevice.commandList->IASetVertexBuffers          (0, 1, &model.VertexPositionsBufferView);
            renderDevice.commandList->SetGraphicsRoot32BitConstant(0, sceneGraphAssets.InstanceManager.GetInstanceIndex(instanceId), 0);
            renderDevice.commandList->DrawIndexedInstanced        (model.TriangleCount * 3, 1, 0, 0, 0);
        }
    }

    renderDevice.commandList->SetPipelineState(AlphaTestPipelineState.Get());

    for (auto instanceId : sceneGraphAssets.InstanceManager)
    {
        auto& instance = sceneGraphAssets.InstanceManager.GetInstance(instanceId);
        if (instance.ModelId == Model::Id::Invalid)
        {
            continue;
        }
        auto& model = sceneGraphAssets.ModelManager.GetModel(instance.ModelId);
        if (model.UsesAlphaTest)
        {
            renderDevice.commandList->IASetPrimitiveTopology      (D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            renderDevice.commandList->IASetIndexBuffer            (&model.TriangleIndicesView);
            renderDevice.commandList->IASetVertexBuffers          (0, 1, &model.VertexPositionsBufferView);
            renderDevice.commandList->IASetVertexBuffers          (1, 1, &model.VertexAttributesBufferView);
            renderDevice.commandList->SetGraphicsRoot32BitConstant(0, sceneGraphAssets.InstanceManager.GetInstanceIndex(instanceId), 0);
            renderDevice.commandList->DrawIndexedInstanced        (model.TriangleCount * 3, 1, 0, 0, 0);
        }
    }
}
