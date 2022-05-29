// GraphicsTest.cpp : Defines the entry point for the application.
//
#pragma once

#include "JcabD3D.h"
#include "Model.h"

#include <array>
#include <span>
#include <vector>
#include <optional>
#include <filesystem>

struct ObjMaterial
{
    uint32_t                             Illum            = 0;
    XMFLOAT3                             AmbientColor     { 0, 0, 0 };
    XMFLOAT3                             DiffuseColor     { 1, 1, 1 };
    XMFLOAT3                             SpecularColor    { 0, 0, 0 };
    float                                SpecularExponent = 1;
    float                                Opacity          = 1;
    std::optional<std::filesystem::path> DiffuseTexture   {};
    std::optional<std::filesystem::path> BumpTexture      {};
    std::optional<std::filesystem::path> SpecularTexture  {};
};

struct ObjModel
{
    using IndexSet = std::tuple<uint32_t, uint32_t, uint32_t>;

    static constexpr IndexSet NullIndexSet{ UINT32_MAX, UINT32_MAX, UINT32_MAX };

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
        uint32_t                Material          = 0;
    };

    std::vector<XMFLOAT3> Positions;    // Required.
    std::vector<Triangle> Triangles;    // Required.

    std::vector<XMFLOAT3> Normals;      // Optional.
    std::vector<XMFLOAT2> UVs;          // Optional.

    std::vector<Edge>     Edges;        // Computed.

    std::vector<ObjMaterial> Materials;
};


ObjModel LoadObjModel(char const* pFilename);

void RecenterObjModelPositions(ObjModel&, float scale);

void AddObjEdgesAndAdjacency(ObjModel&);

void AddObjModelMissingNormals(ObjModel&);

std::vector<Model> UploadObjModel(ObjModel const&, TextureManager&, MaterialManager&, AssetUploader&, RenderDevice&);
