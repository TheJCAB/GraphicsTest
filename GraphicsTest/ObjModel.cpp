// GraphicsTest.cpp : Defines the entry point for the application.
//

#include "ObjModel.h"

#include <map>
#include <set>
#include <string_view>
#include <filesystem>

std::map<std::string, ObjMaterial> LoadObjMaterials(std::filesystem::path filePath)
{
    std::map<std::string, ObjMaterial> materials;

    ObjMaterial* material = nullptr;

    FILE* const f = fopen(filePath.string().c_str(), "r");
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
        if (line.starts_with("newmtl "))
        {
            auto const name = line.substr(line.find_first_not_of(" \t", 7));

            material = &materials[std::string(name)];
        }
        else if (line.starts_with("Ka "))
        {
            sscanf_s(line.data(), "Ka %f %f %f", &material->AmbientColor.x, &material->AmbientColor.y, &material->AmbientColor.z);
        }
        else if (line.starts_with("Kd "))
        {
            sscanf_s(line.data(), "Kd %f %f %f", &material->DiffuseColor.x, &material->DiffuseColor.y, &material->DiffuseColor.z);
        }
        else if (line.starts_with("Ks "))
        {
            sscanf_s(line.data(), "Ks %f %f %f", &material->SpecularColor.x, &material->SpecularColor.y, &material->SpecularColor.z);
        }
        else if (line.starts_with("Ns "))
        {
            sscanf_s(line.data(), "Ns %f", &material->SpecularExponent);
        }
        else if (line.starts_with("d "))
        {
            sscanf_s(line.data(), "d %f", &material->Opacity);
        }
        else if (line.starts_with("Tr "))
        {
            sscanf_s(line.data(), "Tr %f", &material->Opacity);
            material->Opacity = 1.0f - material->Opacity;
        }
        else if (line.starts_with("illum "))
        {
            sscanf_s(line.data(), "illum %u", &material->Illum);
        }
        else if (line.starts_with("map_Kd "))
        {
            auto path = filePath;
            path.replace_filename(line.substr(line.find_first_not_of(" \t", 7)));
            material->DiffuseTexture = path;
        }
    }

    return materials;
}

ObjModel LoadObjModel(char const* pFilename)
{
    ObjModel model;
    
    std::filesystem::path materialFile{pFilename};
    materialFile.replace_extension(".mtl");

    std::set<std::filesystem::path> materialFiles{ materialFile };

    std::map<std::string, uint32_t> materialMap;
    for (auto&& [ name, material ] : LoadObjMaterials(materialFile))
    {
        auto& index = materialMap[name];
        if (index == 0)
        {
            model.Materials.push_back(material);
            index = static_cast<uint32_t>(model.Materials.size());

        }
    }

    uint32_t currentMaterial = 0;

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
        if (line.starts_with("v "))
        {
            XMFLOAT3 pos;
            sscanf_s(line.data(), "v %f %f %f", &pos.x, &pos.y, &pos.z);
            model.Positions.push_back(pos);
        }
        else if (line.starts_with("vn "))
        {
            XMFLOAT3 norm;
            sscanf_s(line.data(), "vn %f %f %f", &norm.x, &norm.y, &norm.z);
            model.Normals.push_back(norm);
        }
        else if (line.starts_with("vt "))
        {
            XMFLOAT2 uv;
            sscanf_s(line.data(), "vt %f %f", &uv.x, &uv.y);
            model.UVs.push_back(uv);
        }
        else if (line.starts_with("mtllib "))
        {
            auto path = materialFile;
            path.replace_filename(line.substr(line.find_first_not_of(" \t", 7)));
            if (materialFiles.find(path) == materialFiles.end())
            {
                materialFiles.insert(path);
                for (auto&& [ name, material ] : LoadObjMaterials(path))
                {
                    auto& index = materialMap[name];
                    if (index == 0)
                    {
                        model.Materials.push_back(material);
                        index = static_cast<uint32_t>(model.Materials.size());

                    }
                }
            }
        }
        else if (line.starts_with("usemtl "))
        {
            auto const materialIt = materialMap.find(std::string(line.substr(line.find_first_not_of(" \t", 7))));
            if (materialIt != materialMap.end())
            {
                currentMaterial = materialIt->second;
            }
        }
        else if (line.starts_with("f "))
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
                triangle.Indices[0] = {p0 - 1, t0 - 1, n0 - 1};
                triangle.Indices[1] = {p1 - 1, t1 - 1, n1 - 1};
                triangle.Indices[2] = {p2 - 1, t2 - 1, n2 - 1};
                triangle.Material = currentMaterial - 1;
                model.Triangles.push_back(triangle);

            }
            if (matches >= 12)
            {
                ObjModel::Triangle triangle{};
                triangle.Indices[0] = {p0 - 1, t0 - 1, n0 - 1};
                triangle.Indices[1] = {p2 - 1, t2 - 1, n2 - 1};
                triangle.Indices[2] = {p3 - 1, t3 - 1, n3 - 1};
                triangle.Material = currentMaterial - 1;
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
        auto& [ p0, t0, n0 ] = triangle.Indices[0];
        auto& [ p1, t1, n1 ] = triangle.Indices[1];
        auto& [ p2, t2, n2 ] = triangle.Indices[2];

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



std::vector<Model> UploadObjModel(ObjModel const& model, TextureManager& textureManager, MaterialManager& materialManager, AssetUploader& uploader, RenderDevice& renderDevice)
{
    std::vector<Model> result;

    std::map<std::tuple<uint32_t, uint32_t, uint32_t>, uint32_t> vertices;

    std::map<uint32_t, Material::Id> materials;

    std::vector<VertexPosition  > vertexPositions;
    std::vector<VertexAttributes> vertexAttributes;

    std::vector<TriangleIndices   > triangleIndices;
    std::vector<TriangleParameters> triangleParameters;

    uint32_t vcount = 0;

    for (auto const& triangle : model.Triangles)
    {
        auto& materialId = materials[triangle.Material];
        if (materialId == Material::Id::Invalid)
        {
            auto& objMaterial = model.Materials[triangle.Material];
            materialId = materialManager.AllocateNewMaterial(renderDevice, uploader, textureManager,
                Material{
                    .AmbientColor           = objMaterial.AmbientColor,
                    .DiffuseColor           = objMaterial.DiffuseColor,
                    .SpecularColor          = objMaterial.SpecularColor,
                    .SpecularExponent       = objMaterial.SpecularExponent,
                    .DiffuseTexturePathName = objMaterial.DiffuseTexture.value_or("").string(),
                }
            );
        }
        auto const materialIndex = materialManager.GetMaterialIndex(materialId);

        auto const [ p0, t0, n0 ] = triangle.Indices[0];
        auto const [ p1, t1, n1 ] = triangle.Indices[1];
        auto const [ p2, t2, n2 ] = triangle.Indices[2];

        auto& v0 = vertices[std::tuple{p0, t0, n0}];
        auto& v1 = vertices[std::tuple{p1, t1, n1}];
        auto& v2 = vertices[std::tuple{p2, t2, n2}];
        if (v0 == 0) { v0 = ++vcount; vertexPositions.emplace_back(model.Positions[p0]); vertexAttributes.emplace_back(model.Normals[n0], model.UVs[t0], 0xFFFFFFFFu); }
        if (v1 == 0) { v1 = ++vcount; vertexPositions.emplace_back(model.Positions[p1]); vertexAttributes.emplace_back(model.Normals[n1], model.UVs[t1], 0xFFFFFFFFu); }
        if (v2 == 0) { v2 = ++vcount; vertexPositions.emplace_back(model.Positions[p2]); vertexAttributes.emplace_back(model.Normals[n2], model.UVs[t2], 0xFFFFFFFFu); }
        triangleIndices   .emplace_back(TriangleIndices{v0 - 1, v2 - 1, v1 - 1});
        triangleParameters.emplace_back(0xFFFF'FFFFu, static_cast<uint32_t>(triangleParameters.size()), materialIndex); // TODO: patch

        if (triangleIndices.size() >= (0x100000 - 2))
        {
            result.push_back(CreateModel(uploader, renderDevice, { triangleIndices, triangleParameters }, { vertexPositions, vertexAttributes }));

            vertices.clear();

            vertexPositions.clear();
            vertexAttributes.clear();

            triangleIndices.clear();
            triangleParameters.clear();

            vcount = 0;
        }
    }

    if (!triangleIndices.empty())
    {
        result.push_back(CreateModel(uploader, renderDevice, { triangleIndices, triangleParameters }, { vertexPositions, vertexAttributes }));
    }

    return result;
}
