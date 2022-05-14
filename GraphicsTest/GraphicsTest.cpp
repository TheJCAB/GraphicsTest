// GraphicsTest.cpp : Defines the entry point for the application.
//

#include "JcabD3D.h"
#include "Model.h"
#include "Pipeline.h"

#include <shlobj.h>

#include <array>
#include <span>
#include <vector>
#include <map>
#include <filesystem>










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

    static constexpr IndexSet NullIndexSet{ UINT32_MAX, UINT32_MAX };

    struct Edge
    {
        std::array<IndexSet, 2> Indices     { NullIndexSet, NullIndexSet };
        std::array<uint32_t, 2> Triangles   { UINT32_MAX, UINT32_MAX };
        uint32_t                InverseEdge = UINT32_MAX;
        XMFLOAT3                Normal      {};
    };
    struct Triangle
    {
        std::array<IndexSet, 3> Indices           { NullIndexSet, NullIndexSet, NullIndexSet };           // Required. UINT32_MAX indicates nullness.
        std::array<uint32_t, 3> Edges             { UINT32_MAX, UINT32_MAX, UINT32_MAX };
        std::array<uint32_t, 3> AdjacentTriangles { UINT32_MAX, UINT32_MAX, UINT32_MAX };
        XMFLOAT3                Normal            {};
    };

    std::vector<XMFLOAT3> Positions;    // Required.
    std::vector<Triangle> Triangles;    // Required.

    std::vector<XMFLOAT3> Normals;      // Optional.

    std::vector<Edge>     Edges;        // Computed.
};


ObjModel LoadObjModel(char const* pFilename, AssetUploader& uploader, RenderDevice& renderDevice)
{
    ObjModel model;
    
    FILE* const f = fopen(pFilename, "r");
    if (!f)
    {
        return {};
    }
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
                n0 = 0;
                n1 = 0;
                n2 = 0;
                n3 = 0;
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
                ObjModel::Triangle triangle{};
                triangle.Indices[0] = {p0 - 1, n0 - 1};
                triangle.Indices[1] = {p1 - 1, n1 - 1};
                triangle.Indices[2] = {p2 - 1, n2 - 1};
                model.Triangles.push_back(triangle);

            }
            if (matches >= 12)
            {
                ObjModel::Triangle triangle{};
                triangle.Indices[0] = {p0 - 1, n0 - 1};
                triangle.Indices[1] = {p2 - 1, n2 - 1};
                triangle.Indices[2] = {p3 - 1, n3 - 1};
                model.Triangles.push_back(triangle);
            }
        }
    }

    return model;
}

void RecenterObjModelPositions(ObjModel& model, float scale)
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
        XMStoreFloat3(&pos, (XMLoadFloat3(&pos) - center) * scale);
    }
}

void AddObjEdgesAndAdjacency(ObjModel& model)
{
    // Note: the two values are offset by 1 (so that the default of zero means invalid).
    std::map<std::tuple<ObjModel::IndexSet, ObjModel::IndexSet>, uint32_t> edgeMap;

    uint32_t tindex = 0;
    for (auto& triangle : model.Triangles)
    {
        auto& [ v0, v1, v2 ] = triangle.Indices;

        auto& e0 = edgeMap[{v0, v1}];
        auto& e1 = edgeMap[{v1, v2}];
        auto& e2 = edgeMap[{v2, v0}];

        if (e0 == 0)
        {
            ObjModel::Edge edge{};
            edge.Indices      = {v0, v1};
            edge.Triangles[0] = tindex;
            model.Edges.push_back(edge);
            e0 = static_cast<uint32_t>(model.Edges.size());
        }

        if (e1 == 0)
        {
            ObjModel::Edge edge{};
            edge.Indices      = {v1, v2};
            edge.Triangles[0] = tindex;
            model.Edges.push_back(edge);
            e1 = static_cast<uint32_t>(model.Edges.size());
        }

        if (e2 == 0)
        {
            ObjModel::Edge edge{};
            edge.Indices      = {v2, v0};
            edge.Triangles[0] = tindex;
            model.Edges.push_back(edge);
            e2 = static_cast<uint32_t>(model.Edges.size());
        }
        triangle.Edges = { e0 - 1, e1 - 1, e2 - 1 };
        ++tindex;
    }

    tindex = 0;
    for (auto& triangle : model.Triangles)
    {
        auto& [ v0, v1, v2 ] = triangle.Indices;

        auto ie0 = edgeMap[{v1, v0}];
        auto ie1 = edgeMap[{v2, v1}];
        auto ie2 = edgeMap[{v0, v2}];
        if (ie0 != 0)
        {
            auto& iedge = model.Edges[ie0 - 1];
            iedge.InverseEdge = triangle.Edges[0];
            iedge.Triangles[1] = tindex;
            auto& edge = model.Edges[iedge.InverseEdge];
            edge.InverseEdge = ie0 - 1;
            edge.Triangles[1] = iedge.Triangles[0];
        }
        if (ie1 != 0)
        {
            auto& iedge = model.Edges[ie1 - 1];
            iedge.InverseEdge = triangle.Edges[1];
            iedge.Triangles[1] = tindex;
            auto& edge = model.Edges[iedge.InverseEdge];
            edge.InverseEdge = ie1 - 1;
            edge.Triangles[1] = iedge.Triangles[0];
        }
        if (ie2 != 0)
        {
            auto& iedge = model.Edges[ie2 - 1];
            iedge.InverseEdge = triangle.Edges[2];
            iedge.Triangles[1] = tindex;
            auto& edge = model.Edges[iedge.InverseEdge];
            edge.InverseEdge = ie2 - 1;
            edge.Triangles[1] = iedge.Triangles[0];
        }
        ++tindex;
    }
}

void AddObjModelMissingNormals(ObjModel& model)
{
    for (auto& triangle : model.Triangles)
    {
        auto& [ p0, n0 ] = triangle.Indices[0];
        auto& [ p1, n1 ] = triangle.Indices[1];
        auto& [ p2, n2 ] = triangle.Indices[2];

        auto xp0 = XMLoadFloat3(&model.Positions[p0]);
        auto xp1 = XMLoadFloat3(&model.Positions[p1]);
        auto xp2 = XMLoadFloat3(&model.Positions[p2]);
        XMStoreFloat3(&triangle.Normal, XMVector3Normalize(XMVector3Cross(xp1 - xp0, xp2 - xp0)));

        //if (n0 == UINT32_MAX && n1 == UINT32_MAX && n2 == UINT32_MAX)
        //{
        //}
        //else
        //{
        //    XMVECTOR normal{};
        //    if (n0 != UINT32_MAX) normal += XMLoadFloat3(&model.Normals[n0]);
        //    if (n1 != UINT32_MAX) normal += XMLoadFloat3(&model.Normals[n1]);
        //    if (n2 != UINT32_MAX) normal += XMLoadFloat3(&model.Normals[n2]);
        //    XMStoreFloat3(&triangle.Normal, XMVector3Normalize(normal));
        //}
        if (n0 == UINT32_MAX || n1 == UINT32_MAX || n2 == UINT32_MAX)
        {
            auto const n = static_cast<uint32_t>(model.Normals.size());
            if (n0 == UINT32_MAX) n0 = n;
            if (n1 == UINT32_MAX) n1 = n;
            if (n2 == UINT32_MAX) n2 = n;
            model.Normals.push_back(triangle.Normal);
        }
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

    uint32_t vcount = 0;

    for (auto const& triangle : model.Triangles)
    {
        auto const [ p0, n0 ] = triangle.Indices[0];
        auto const [ p1, n1 ] = triangle.Indices[1];
        auto const [ p2, n2 ] = triangle.Indices[2];

        auto& v0 = vertices[std::tuple{p0, n0}];
        auto& v1 = vertices[std::tuple{p1, n1}];
        auto& v2 = vertices[std::tuple{p2, n2}];
        if (v0 == 0) { v0 = ++vcount; vertexPositions.emplace_back(model.Positions[p0]); vertexAttributes.emplace_back(model.Normals[n0], 0xFFFFFFFFu); }
        if (v1 == 0) { v1 = ++vcount; vertexPositions.emplace_back(model.Positions[p1]); vertexAttributes.emplace_back(model.Normals[n1], 0xFFFFFFFFu); }
        if (v2 == 0) { v2 = ++vcount; vertexPositions.emplace_back(model.Positions[p2]); vertexAttributes.emplace_back(model.Normals[n2], 0xFFFFFFFFu); }
        indices.emplace_back(TriangleIndices{v0 - 1, v2 - 1, v1 - 1});
        triangleParams.emplace_back(0xFFFF'FFFFu);

        if (indices.size() >= ((1 << D3D12_WHQL_DRAWINDEXED_INDEX_COUNT_2_TO_EXP) / 3) - 2)
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
    std::shared_ptr<RenderPipeline>  pipeline;

    std::shared_ptr<ModelManager   > modelManager;
    std::shared_ptr<InstanceManager> instanceManager;

    Model::Id                        cube;
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

    assets.pipeline         = std::make_shared<RenderPipeline> (renderDevice);
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

    {
        auto objModel = LoadObjModel(R"(X:\TEMP\MaryRoseTankard_100kMesh.obj)", uploader, renderDevice);
        RecenterObjModelPositions(objModel, 50);
        AddObjEdgesAndAdjacency  (objModel);
        AddObjModelMissingNormals(objModel);

        for (auto& model : UploadObjModel(objModel, uploader, renderDevice))
        {
            auto const modelId = assets.modelManager->AllocateNewModel(renderDevice, std::move(model));
            assets.objModelInstances.push_back(assets.instanceManager->AllocateNewInstance(renderDevice, *assets.modelManager, Instance{modelId, XMVectorSet(-8, 0, 0, 0)}));
        }
    }

    {
        auto objModel = LoadObjModel(R"(C:\Users\jcab\Downloads\ameise\source\Ameise_C\Ameise_C.obj)", uploader, renderDevice);
        RecenterObjModelPositions(objModel, 1);
        AddObjEdgesAndAdjacency  (objModel);
        AddObjModelMissingNormals(objModel);

        for (auto& model : UploadObjModel(objModel, uploader, renderDevice))
        {
            auto const modelId = assets.modelManager->AllocateNewModel(renderDevice, std::move(model));
            assets.objModelInstances.push_back(assets.instanceManager->AllocateNewInstance(renderDevice, *assets.modelManager, Instance{modelId}));
        }
    }

    //FILE* const f = fopen(, "r");

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

    assets.pipeline->Render(renderDevice, *assets.modelManager, *assets.instanceManager);

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
        switch (static_cast<UINT8>(wParam))
        {
        case VK_SPACE:
            g_animate = !g_animate;
            break;
        }
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
