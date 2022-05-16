// GraphicsTest.cpp : Defines the entry point for the application.
//

#include "JcabD3D.h"
#include "Model.h"
#include "ObjModel.h"
#include "Pipeline.h"

#include <shlobj.h>

#include <array>
#include <span>
#include <vector>
#include <map>
#include <filesystem>










Model CreateCubeModel(uint32_t materialIndex, AssetUploader& uploader, RenderDevice& renderDevice)
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
        { {  1,  0,  0 }, { 0, 0 }, 0xFF0000FFu },
        { {  1,  0,  0 }, { 0, 0 }, 0xFF0000FFu },
        { {  1,  0,  0 }, { 0, 0 }, 0xFF0000FFu },
        { {  1,  0,  0 }, { 0, 0 }, 0xFF0000FFu },
        { { -1,  0,  0 }, { 0, 0 }, 0xFF00FF00u },
        { { -1,  0,  0 }, { 0, 0 }, 0xFF00FF00u },
        { { -1,  0,  0 }, { 0, 0 }, 0xFF00FF00u },
        { { -1,  0,  0 }, { 0, 0 }, 0xFF00FF00u },
        { {  0,  0,  1 }, { 0, 0 }, 0xFFFF0000u },
        { {  0,  0,  1 }, { 0, 0 }, 0xFFFF0000u },
        { {  0,  0,  1 }, { 0, 0 }, 0xFFFF0000u },
        { {  0,  0,  1 }, { 0, 0 }, 0xFFFF0000u },
        { {  0,  0, -1 }, { 0, 0 }, 0xFF00FFFFu },
        { {  0,  0, -1 }, { 0, 0 }, 0xFF00FFFFu },
        { {  0,  0, -1 }, { 0, 0 }, 0xFF00FFFFu },
        { {  0,  0, -1 }, { 0, 0 }, 0xFF00FFFFu },
        { {  0,  1,  0 }, { 0, 0 }, 0xFFFF00FFu },
        { {  0,  1,  0 }, { 0, 0 }, 0xFFFF00FFu },
        { {  0,  1,  0 }, { 0, 0 }, 0xFFFF00FFu },
        { {  0,  1,  0 }, { 0, 0 }, 0xFFFF00FFu },
        { {  0, -1,  0 }, { 0, 0 }, 0xFFFFFF00u },
        { {  0, -1,  0 }, { 0, 0 }, 0xFFFFFF00u },
        { {  0, -1,  0 }, { 0, 0 }, 0xFFFFFF00u },
        { {  0, -1,  0 }, { 0, 0 }, 0xFFFFFF00u },
    };

    TriangleParameters const triangleParams[] =
    {
        { 0xFF0000FFu, 0, materialIndex },
        { 0xFF0000FFu, 0, materialIndex },
        { 0xFF00FF00u, 1, materialIndex },
        { 0xFF00FF00u, 1, materialIndex },
        { 0xFFFF0000u, 2, materialIndex },
        { 0xFFFF0000u, 2, materialIndex },
        { 0xFF00FFFFu, 3, materialIndex },
        { 0xFF00FFFFu, 3, materialIndex },
        { 0xFFFF00FFu, 4, materialIndex },
        { 0xFFFF00FFu, 4, materialIndex },
        { 0xFFFFFF00u, 5, materialIndex },
        { 0xFFFFFF00u, 5, materialIndex },
    };

    return CreateModel(uploader, renderDevice, { indices, triangleParams }, { triangleVertices, vertexAttributes });
}



struct Assets
{
    std::shared_ptr<RenderPipeline>  pipeline;

    std::shared_ptr<TextureManager > textureManager;
    std::shared_ptr<MaterialManager> materialManager;
    std::shared_ptr<ModelManager   > modelManager;
    std::shared_ptr<InstanceManager> instanceManager;

    Material::Id DefaultMaterialId = Material::Id::Invalid;

    Model::Id                        cube;
    Instance::Id                     cubeInstances[2];
    std::vector<Instance::Id>        objModelInstances;
};

Assets LoadAssets(RenderDevice& renderDevice)
{
    Assets assets{};

    AssetUploader uploader{ renderDevice };

    assets.pipeline         = std::make_shared<RenderPipeline> (renderDevice);
    assets.textureManager   = std::make_shared<TextureManager> (renderDevice);
    assets.materialManager  = std::make_shared<MaterialManager>(renderDevice);
    assets.modelManager     = std::make_shared<ModelManager>   (renderDevice);
    assets.instanceManager  = std::make_shared<InstanceManager>(renderDevice);

    assets.DefaultMaterialId = assets.materialManager->AllocateNewMaterial(renderDevice, uploader, *assets.textureManager,
        Material{
            .AmbientColor = { 0, 0, 0 },
            .DiffuseColor = { 1, 1, 1 },
        }
    );

    assets.cube             = assets.modelManager->AllocateNewModel(renderDevice, CreateCubeModel(assets.materialManager->GetMaterialIndex(assets.DefaultMaterialId), uploader, renderDevice));
    assets.cubeInstances[0] = assets.instanceManager->AllocateNewInstance(renderDevice, *assets.modelManager, Instance{assets.cube});
    assets.cubeInstances[1] = assets.instanceManager->AllocateNewInstance(renderDevice, *assets.modelManager, Instance{assets.cube});
    
    {
        auto& instance = assets.instanceManager->GetInstance(assets.cubeInstances[1]);

        instance.Yaw   = static_cast<uint16_t>(rand());
        instance.Pitch = static_cast<uint16_t>(rand());
        instance.Roll  = static_cast<uint16_t>(rand());
    }

    //{
    //    auto objModel = LoadObjModel(R"(X:\TEMP\mary-rose-a-wooden-tankard\source\maryRoseTankard.zip\MaryRoseTankard_100kMesh.obj)");
    //    RecenterObjModelPositions(objModel, 50);
    //    AddObjEdgesAndAdjacency  (objModel);
    //    AddObjModelMissingNormals(objModel);
    //
    //    for (auto& model : UploadObjModel(objModel, *assets.textureManager, *assets.materialManager, uploader, renderDevice))
    //    {
    //        auto const modelId = assets.modelManager->AllocateNewModel(renderDevice, std::move(model));
    //        assets.objModelInstances.push_back(assets.instanceManager->AllocateNewInstance(renderDevice, *assets.modelManager, Instance{modelId, 1.0f, XMVectorSet(-8, 0, 0, 0)}));
    //    }
    //}

    //{
    //    auto objModel = LoadObjModel(R"(C:\Users\jcab\Downloads\ameise\source\Ameise_C\Ameise_C.obj)");
    //    RecenterObjModelPositions(objModel, 1);
    //    AddObjEdgesAndAdjacency  (objModel);
    //    AddObjModelMissingNormals(objModel);
    //
    //    for (auto& model : UploadObjModel(objModel, *assets.textureManager, *assets.materialManager, uploader, renderDevice))
    //    {
    //        auto const modelId = assets.modelManager->AllocateNewModel(renderDevice, std::move(model));
    //        assets.objModelInstances.push_back(assets.instanceManager->AllocateNewInstance(renderDevice, *assets.modelManager, Instance{modelId}));
    //    }
    //}

    {
        auto objModel = LoadObjModel(R"(C:\Users\jcab\Downloads\San_Miguel\san-miguel-low-poly.obj)");
        RecenterObjModelPositions(objModel, 1);
        AddObjEdgesAndAdjacency  (objModel);
        AddObjModelMissingNormals(objModel);
    
        for (auto& model : UploadObjModel(objModel, *assets.textureManager, *assets.materialManager, uploader, renderDevice))
        {
            auto const modelId = assets.modelManager->AllocateNewModel(renderDevice, std::move(model));
            assets.objModelInstances.push_back(assets.instanceManager->AllocateNewInstance(renderDevice, *assets.modelManager, Instance{modelId, 1.0f, XMVectorSet(0, 0, 4, 6)}));
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

    assets.pipeline->Render(renderDevice, *assets.textureManager, *assets.materialManager, *assets.modelManager, *assets.instanceManager);

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
