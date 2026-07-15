/*
** Project Tempest
** Copyright 2026 Project Tempest contributors
**
** This program is free software: you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation, either version 3 of the License, or
** (at your option) any later version.
*/

#include <windows.h>
#include <windowsx.h>
#include <mmsystem.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>

#include "assetmgr.h"
#include "camera.h"
#include "light.h"
#include "matrix3d.h"
#include "rendobj.h"
#include "scene.h"
#include "vector3.h"
#include "ww3d.h"
#include "wwmath.h"

HINSTANCE ApplicationHInstance = nullptr;
HWND ApplicationHWnd = nullptr;
const char *gAppPrefix = "pt_";

namespace {

constexpr int kInitialWidth = 1280;
constexpr int kInitialHeight = 720;
constexpr float kMoveSpeed = 9.0F;
constexpr float kArenaExtent = 18.0F;
constexpr float kObjectiveX = 12.0F;
constexpr float kObjectiveY = 10.0F;

bool g_keys[256] = {};
bool g_rendererReady = false;
bool g_selected = true;
bool g_hasMoveTarget = false;
bool g_objectiveComplete = false;
float g_unitX = -10.0F;
float g_unitY = -8.0F;
float g_heading = 0.0F;
float g_targetX = g_unitX;
float g_targetY = g_unitY;

WW3DAssetManager *g_assetManager = nullptr;
SimpleSceneClass *g_scene = nullptr;
CameraClass *g_camera = nullptr;
LightClass *g_keyLight = nullptr;
RenderObjClass *g_courier = nullptr;

void UpdateWindowTitle()
{
    char title[512];
    const char *state = g_objectiveComplete
        ? "UPLINK SECURED - prototype objective complete"
        : (g_selected ? "Courier selected" : "Click to select Courier");
    std::snprintf(
        title,
        sizeof(title),
        "Project Tempest | %s | Courier %.1f, %.1f | Right-click: move  WASD: nudge  R: restart  Esc: quit",
        state,
        g_unitX,
        g_unitY);
    SetWindowTextA(ApplicationHWnd, title);
}

void ResetPrototype()
{
    g_selected = true;
    g_hasMoveTarget = false;
    g_objectiveComplete = false;
    g_unitX = -10.0F;
    g_unitY = -8.0F;
    g_heading = 0.0F;
    g_targetX = g_unitX;
    g_targetY = g_unitY;
    UpdateWindowTitle();
}

void SetMoveTargetFromScreen(int mouseX, int mouseY)
{
    RECT client = {};
    GetClientRect(ApplicationHWnd, &client);
    const float width = static_cast<float>(std::max(1L, client.right - client.left));
    const float height = static_cast<float>(std::max(1L, client.bottom - client.top));

    // The prototype arena is a flat plane viewed through a fixed RTS camera.
    // This mapping is deliberately simple until terrain ray-casting lands.
    g_targetX = ((static_cast<float>(mouseX) / width) - 0.5F) * (kArenaExtent * 2.0F);
    g_targetY = (0.5F - (static_cast<float>(mouseY) / height)) * (kArenaExtent * 2.0F);
    g_targetX = std::clamp(g_targetX, -kArenaExtent, kArenaExtent);
    g_targetY = std::clamp(g_targetY, -kArenaExtent, kArenaExtent);
    g_hasMoveTarget = true;
    g_selected = true;
    UpdateWindowTitle();
}

LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message) {
        case WM_KEYDOWN:
            if (wParam < 256) {
                g_keys[wParam] = true;
            }
            if (wParam == VK_ESCAPE) {
                DestroyWindow(window);
            } else if (wParam == 'R') {
                ResetPrototype();
            }
            return 0;

        case WM_KEYUP:
            if (wParam < 256) {
                g_keys[wParam] = false;
            }
            return 0;

        case WM_LBUTTONDOWN:
            g_selected = true;
            UpdateWindowTitle();
            return 0;

        case WM_RBUTTONDOWN:
            SetMoveTargetFromScreen(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            return 0;

        case WM_SIZE:
            if (g_rendererReady && wParam != SIZE_MINIMIZED) {
                const int width = std::max(1, static_cast<int>(LOWORD(lParam)));
                const int height = std::max(1, static_cast<int>(HIWORD(lParam)));
                WW3D::Set_Device_Resolution(width, height, 24, true);
            }
            return 0;

        case WM_ERASEBKGND:
            return 1;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProc(window, message, wParam, lParam);
}

bool CreateMainWindow(HINSTANCE instance, int commandShow)
{
    WNDCLASSEXA windowClass = {};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    windowClass.lpfnWndProc = WindowProcedure;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
    windowClass.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    windowClass.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    windowClass.lpszClassName = "ProjectTempestDemoWindow";

    if (!RegisterClassExA(&windowClass)) {
        return false;
    }

    RECT windowRect = { 0, 0, kInitialWidth, kInitialHeight };
    AdjustWindowRect(&windowRect, WS_OVERLAPPEDWINDOW, FALSE);
    ApplicationHWnd = CreateWindowExA(
        0,
        windowClass.lpszClassName,
        "Project Tempest",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        windowRect.right - windowRect.left,
        windowRect.bottom - windowRect.top,
        nullptr,
        nullptr,
        instance,
        nullptr);
    if (!ApplicationHWnd) {
        return false;
    }

    ShowWindow(ApplicationHWnd, commandShow);
    UpdateWindow(ApplicationHWnd);
    return true;
}

bool InitialiseRenderer()
{
    WWMath::Init();
    if (WW3D::Init(ApplicationHWnd) != WW3D_ERROR_OK) {
        return false;
    }
    if (WW3D::Get_Render_Device_Count() <= 0) {
        return false;
    }

    RECT client = {};
    GetClientRect(ApplicationHWnd, &client);
    if (WW3D::Set_Render_Device(
            0,
            std::max(1L, client.right - client.left),
            std::max(1L, client.bottom - client.top),
            24,
            true) != WW3D_ERROR_OK) {
        return false;
    }
    g_rendererReady = true;

    g_assetManager = new WW3DAssetManager;
    if (!g_assetManager || !g_assetManager->Load_3D_Assets("courier.w3d")) {
        return false;
    }

    g_courier = g_assetManager->Create_Render_Obj("courier");
    if (!g_courier) {
        return false;
    }

    g_scene = new SimpleSceneClass;
    g_scene->Set_Ambient_Light(Vector3(0.30F, 0.34F, 0.38F));
    g_scene->Set_Fog_Enable(false);
    g_scene->Add_Render_Object(g_courier);

    g_keyLight = new LightClass;
    g_keyLight->Set_Position(Vector3(-16.0F, -24.0F, 30.0F));
    g_keyLight->Set_Intensity(1.0F);
    g_keyLight->Set_Force_Visible(true);
    g_keyLight->Set_Flag(LightClass::NEAR_ATTENUATION, false);
    g_keyLight->Set_Far_Attenuation_Range(1000.0F, 1000.0F);
    g_keyLight->Set_Ambient(Vector3(0.0F, 0.0F, 0.0F));
    g_keyLight->Set_Diffuse(Vector3(0.85F, 0.93F, 1.0F));
    g_keyLight->Set_Specular(Vector3(0.45F, 0.75F, 1.0F));
    g_scene->Add_Render_Object(g_keyLight);

    g_camera = new CameraClass;
    Matrix3D cameraTransform;
    cameraTransform.Look_At(
        Vector3(0.0F, -38.0F, 31.0F),
        Vector3(0.0F, 0.0F, 0.0F),
        0.0F);
    g_camera->Set_Transform(cameraTransform);
    g_camera->Set_View_Plane(Vector2(-1.0F, -0.5625F), Vector2(1.0F, 0.5625F));
    g_camera->Set_Clip_Planes(1.0F, 250.0F);

    ResetPrototype();
    return true;
}

void UpdatePrototype(float deltaSeconds)
{
    float inputX = 0.0F;
    float inputY = 0.0F;
    if (g_selected) {
        inputX += (g_keys['D'] || g_keys[VK_RIGHT]) ? 1.0F : 0.0F;
        inputX -= (g_keys['A'] || g_keys[VK_LEFT]) ? 1.0F : 0.0F;
        inputY += (g_keys['W'] || g_keys[VK_UP]) ? 1.0F : 0.0F;
        inputY -= (g_keys['S'] || g_keys[VK_DOWN]) ? 1.0F : 0.0F;
    }

    if (inputX != 0.0F || inputY != 0.0F) {
        const float length = std::sqrt((inputX * inputX) + (inputY * inputY));
        inputX /= length;
        inputY /= length;
        g_unitX += inputX * kMoveSpeed * deltaSeconds;
        g_unitY += inputY * kMoveSpeed * deltaSeconds;
        g_heading = std::atan2(inputY, inputX);
        g_hasMoveTarget = false;
    } else if (g_hasMoveTarget) {
        const float deltaX = g_targetX - g_unitX;
        const float deltaY = g_targetY - g_unitY;
        const float distance = std::sqrt((deltaX * deltaX) + (deltaY * deltaY));
        if (distance < 0.15F) {
            g_unitX = g_targetX;
            g_unitY = g_targetY;
            g_hasMoveTarget = false;
        } else {
            const float step = std::min(distance, kMoveSpeed * deltaSeconds);
            g_unitX += (deltaX / distance) * step;
            g_unitY += (deltaY / distance) * step;
            g_heading = std::atan2(deltaY, deltaX);
        }
    }

    g_unitX = std::clamp(g_unitX, -kArenaExtent, kArenaExtent);
    g_unitY = std::clamp(g_unitY, -kArenaExtent, kArenaExtent);

    Matrix3D unitTransform(1);
    unitTransform.Rotate_Z(g_heading);
    unitTransform.Set_Translation(Vector3(g_unitX, g_unitY, 0.0F));
    g_courier->Set_Transform(unitTransform);

    const float objectiveDeltaX = g_unitX - kObjectiveX;
    const float objectiveDeltaY = g_unitY - kObjectiveY;
    if (!g_objectiveComplete &&
        ((objectiveDeltaX * objectiveDeltaX) + (objectiveDeltaY * objectiveDeltaY)) < 4.0F) {
        g_objectiveComplete = true;
        g_hasMoveTarget = false;
        MessageBeep(MB_OK);
    }

    static DWORD lastTitleUpdate = 0;
    const DWORD now = timeGetTime();
    if ((now - lastTitleUpdate) > 120) {
        lastTitleUpdate = now;
        UpdateWindowTitle();
    }
}

void RenderPrototype(float deltaSeconds)
{
    WW3D::Update_Logic_Frame_Time(static_cast<int>(deltaSeconds * 1000.0F));
    WW3D::Sync(true);
    if (WW3D::Begin_Render(true, true, Vector3(0.015F, 0.025F, 0.040F)) == WW3D_ERROR_OK) {
        WW3D::Render(g_scene, g_camera, false, false);
        WW3D::End_Render();
    }
}

void ShutdownRenderer()
{
    if (g_courier) {
        g_courier->Remove();
    }
    if (g_keyLight) {
        g_keyLight->Remove();
    }
    REF_PTR_RELEASE(g_courier);
    REF_PTR_RELEASE(g_keyLight);
    REF_PTR_RELEASE(g_camera);
    REF_PTR_RELEASE(g_scene);

    if (g_assetManager) {
        g_assetManager->Free_Assets();
        delete g_assetManager;
        g_assetManager = nullptr;
    }

    if (g_rendererReady) {
        WW3D::Shutdown();
        g_rendererReady = false;
    }
    WWMath::Shutdown();
}

} // namespace

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int commandShow)
{
    ApplicationHInstance = instance;

    char executablePath[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, executablePath, MAX_PATH);
    char *separator = std::strrchr(executablePath, '\\');
    if (separator) {
        *separator = '\0';
        SetCurrentDirectoryA(executablePath);
    }

    if (!CreateMainWindow(instance, commandShow)) {
        MessageBoxA(nullptr, "Unable to create the Project Tempest window.", "Project Tempest", MB_ICONERROR);
        return 1;
    }

    if (!InitialiseRenderer()) {
        MessageBoxA(
            ApplicationHWnd,
            "The renderer or courier.w3d could not be initialised. Run the compatibility preparation script and verify the demo package.",
            "Project Tempest startup failed",
            MB_ICONERROR);
        ShutdownRenderer();
        return 2;
    }

    LARGE_INTEGER frequency = {};
    LARGE_INTEGER previous = {};
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&previous);

    MSG message = {};
    bool running = true;
    while (running) {
        while (PeekMessage(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT) {
                running = false;
                break;
            }
            TranslateMessage(&message);
            DispatchMessage(&message);
        }
        if (!running) {
            break;
        }

        LARGE_INTEGER current = {};
        QueryPerformanceCounter(&current);
        const float deltaSeconds = std::min(
            0.05F,
            static_cast<float>(current.QuadPart - previous.QuadPart) /
                static_cast<float>(frequency.QuadPart));
        previous = current;

        UpdatePrototype(deltaSeconds);
        RenderPrototype(deltaSeconds);
        Sleep(1);
    }

    ShutdownRenderer();
    return 0;
}
