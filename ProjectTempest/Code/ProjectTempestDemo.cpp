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
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>
#include <vector>

#include "assetmgr.h"
#include "camera.h"
#include "light.h"
#include "line3d.h"
#include "matrix3d.h"
#include "rendobj.h"
#include "scene.h"
#include "TempestInterface.h"
#include "TempestSimulation.h"
#include "vector3.h"
#include "ww3d.h"
#include "wwmath.h"

HINSTANCE ApplicationHInstance = nullptr;
HWND ApplicationHWnd = nullptr;
const char *gAppPrefix = "pt_";

namespace {

constexpr int kInitialWidth = 1280;
constexpr int kInitialHeight = 720;
constexpr float kArenaExtent = 18.0F;
constexpr float kSimulationToWorld = 0.001F;
constexpr std::int32_t kScreenPickRadius = 2200;
constexpr double kSimulationTickSeconds = 1.0 / static_cast<double>(Tempest::TicksPerSecond);

bool g_keys[256] = {};
bool g_mouseButtons[6] = {};
bool g_rendererReady = false;
bool g_applicationActive = true;
std::uint32_t g_selectedUnitId = 0;
Tempest::Point g_pointerPoint;
POINT g_pointerClient = {};
double g_simulationAccumulator = 0.0;
float g_cameraCenterX = 0.0F;
float g_cameraCenterY = 0.0F;
char g_feedback[160] = "Select a Courier and restore the first substation.";
DWORD g_feedbackUntil = 0;

WW3DAssetManager *g_assetManager = nullptr;
SimpleSceneClass *g_scene = nullptr;
CameraClass *g_camera = nullptr;
LightClass *g_keyLight = nullptr;

Tempest::Simulation g_simulation;
Tempest::Ui::InterfaceState g_interface;
std::filesystem::path g_settingsPath;

HFONT g_hudFont = nullptr;
HFONT g_hudSmallFont = nullptr;
HFONT g_hudTitleFont = nullptr;
int g_hudFontScale = 0;

struct UnitVisual {
    std::uint32_t id = 0;
    bool damaged = false;
    RenderObjClass *object = nullptr;
};

struct CrossVisual {
    std::uint32_t id = 0;
    Line3DClass *horizontal = nullptr;
    Line3DClass *vertical = nullptr;
};

struct BuildingModelVisual {
    std::uint32_t id = 0;
    RenderObjClass *object = nullptr;
};

std::vector<UnitVisual> g_unitVisuals;
std::vector<CrossVisual> g_nodeVisuals;
std::vector<CrossVisual> g_buildingVisuals;
std::vector<BuildingModelVisual> g_buildingModelVisuals;
std::vector<Line3DClass *> g_gridLines;
Line3DClass *g_selectionHorizontal = nullptr;
Line3DClass *g_selectionVertical = nullptr;

void UpdateCameraTransform();
void UpdateCameraProjection(int width, int height);
void DrawInterface();

std::int64_t DistanceSquared(Tempest::Point left, Tempest::Point right)
{
    const std::int64_t dx = static_cast<std::int64_t>(left.x) - right.x;
    const std::int64_t dy = static_cast<std::int64_t>(left.y) - right.y;
    return (dx * dx) + (dy * dy);
}

Tempest::Command MakeCommand(
    Tempest::CommandKind kind,
    std::uint32_t actorId = 0,
    std::uint32_t targetId = 0,
    Tempest::Point point = {})
{
    Tempest::Command command {};
    command.executeTick = g_simulation.GetState().tick;
    command.kind = kind;
    command.actorId = actorId;
    command.targetId = targetId;
    command.point = point;
    return command;
}

const Tempest::Unit *FindUnit(std::uint32_t id)
{
    for (const Tempest::Unit &unit : g_simulation.GetState().units) {
        if (unit.id == id && unit.alive) {
            return &unit;
        }
    }
    return nullptr;
}

const Tempest::Building *FindBuilding(std::uint32_t id)
{
    for (const Tempest::Building &building : g_simulation.GetState().buildings) {
        if (building.id == id && building.hitPoints > 0) {
            return &building;
        }
    }
    return nullptr;
}

Tempest::Point ScreenToSimulationPoint(int mouseX, int mouseY)
{
    if (!g_camera) {
        return {};
    }
    RECT client = {};
    GetClientRect(ApplicationHWnd, &client);
    const float width = static_cast<float>(std::max(1L, client.right - client.left));
    const float height = static_cast<float>(std::max(1L, client.bottom - client.top));
    const Vector2 viewPoint(
        ((static_cast<float>(mouseX) / width) * 2.0F) - 1.0F,
        1.0F - ((static_cast<float>(mouseY) / height) * 2.0F));
    Vector3 pointOnViewPlane;
    g_camera->Un_Project(pointOnViewPlane, viewPoint);
    const Vector3 cameraPosition = g_camera->Get_Position();
    const Vector3 rayDirection = pointOnViewPlane - cameraPosition;
    if (std::abs(rayDirection.Z) < 0.0001F) {
        return {};
    }
    const float distanceToArena = -cameraPosition.Z / rayDirection.Z;
    const float worldX = std::clamp(
        cameraPosition.X + (rayDirection.X * distanceToArena),
        -kArenaExtent,
        kArenaExtent);
    const float worldY = std::clamp(
        cameraPosition.Y + (rayDirection.Y * distanceToArena),
        -kArenaExtent,
        kArenaExtent);
    return {
        static_cast<std::int32_t>(worldX / kSimulationToWorld),
        static_cast<std::int32_t>(worldY / kSimulationToWorld),
    };
}

void SetFeedback(const char *message)
{
    std::snprintf(g_feedback, sizeof(g_feedback), "%s", message);
    g_feedbackUntil = timeGetTime() + 2600;
}

void RemoveRenderObject(RenderObjClass *&object)
{
    if (object) {
        object->Remove();
        REF_PTR_RELEASE(object);
    }
}

void RemoveLine(Line3DClass *&line)
{
    if (line) {
        line->Remove();
        REF_PTR_RELEASE(line);
    }
}

void UpdateWindowTitle()
{
    char title[160];
    std::snprintf(
        title,
        sizeof(title),
        "Project Tempest - Substation 9 | %s",
        Tempest::Ui::InterfaceState::ScreenName(g_interface.GetScreen()));
    SetWindowTextA(ApplicationHWnd, title);
}

void ResetPrototype()
{
    g_simulation.Reset();
    g_simulationAccumulator = 0.0;
    g_cameraCenterX = 0.0F;
    g_cameraCenterY = 0.0F;
    UpdateCameraTransform();
    g_selectedUnitId = 0;
    for (const Tempest::Unit &unit : g_simulation.GetState().units) {
        if (unit.alive && unit.faction == Tempest::Faction::Freegrid && unit.kind == Tempest::UnitKind::Courier) {
            g_selectedUnitId = unit.id;
            g_pointerPoint = unit.position;
            break;
        }
    }
    UpdateWindowTitle();
}

void SelectFromScreen(int mouseX, int mouseY)
{
    g_pointerPoint = ScreenToSimulationPoint(mouseX, mouseY);
    g_selectedUnitId = 0;
    std::int64_t closestDistance = static_cast<std::int64_t>(kScreenPickRadius) * kScreenPickRadius;
    for (const Tempest::Unit &unit : g_simulation.GetState().units) {
        if (!unit.alive || unit.faction != Tempest::Faction::Freegrid || unit.kind != Tempest::UnitKind::Courier) {
            continue;
        }
        const std::int64_t distance = DistanceSquared(unit.position, g_pointerPoint);
        if (distance <= closestDistance) {
            closestDistance = distance;
            g_selectedUnitId = unit.id;
        }
    }
    SetFeedback(g_selectedUnitId != 0 ? "Courier selected." : "No Freegrid unit at cursor.");
    UpdateWindowTitle();
}

void IssueContextOrder(int mouseX, int mouseY)
{
    g_pointerPoint = ScreenToSimulationPoint(mouseX, mouseY);
    const Tempest::Unit *selected = FindUnit(g_selectedUnitId);
    if (!selected || selected->faction != Tempest::Faction::Freegrid || g_simulation.GetState().paused) {
        return;
    }

    std::uint32_t targetId = 0;
    std::int64_t closestDistance = static_cast<std::int64_t>(kScreenPickRadius) * kScreenPickRadius;
    for (const Tempest::Unit &unit : g_simulation.GetState().units) {
        if (!unit.alive || unit.faction != Tempest::Faction::Chorus) {
            continue;
        }
        const std::int64_t distance = DistanceSquared(unit.position, g_pointerPoint);
        if (distance <= closestDistance) {
            closestDistance = distance;
            targetId = unit.id;
        }
    }
    for (const Tempest::Building &building : g_simulation.GetState().buildings) {
        if (building.hitPoints <= 0 || building.faction != Tempest::Faction::Chorus) {
            continue;
        }
        const std::int64_t distance = DistanceSquared(building.position, g_pointerPoint);
        if (distance <= closestDistance) {
            closestDistance = distance;
            targetId = building.id;
        }
    }
    if (targetId != 0) {
        g_simulation.Submit(MakeCommand(Tempest::CommandKind::Attack, selected->id, targetId));
        SetFeedback("Attack order acknowledged.");
        return;
    }

    targetId = 0;
    closestDistance = static_cast<std::int64_t>(kScreenPickRadius) * kScreenPickRadius;
    for (const Tempest::ControlNode &node : g_simulation.GetState().nodes) {
        const std::int64_t distance = DistanceSquared(node.position, g_pointerPoint);
        if (distance <= closestDistance) {
            closestDistance = distance;
            targetId = node.id;
        }
    }
    if (targetId != 0) {
        g_simulation.Submit(MakeCommand(Tempest::CommandKind::Capture, selected->id, targetId));
        SetFeedback("Grid-link capture order acknowledged.");
    } else {
        g_simulation.Submit(MakeCommand(Tempest::CommandKind::Move, selected->id, 0, g_pointerPoint));
        SetFeedback("Move order acknowledged.");
    }
}

void BuildRelayAtNearestOwnedNode()
{
    const Tempest::Unit *selected = FindUnit(g_selectedUnitId);
    if (!selected || g_simulation.GetState().paused) {
        return;
    }
    std::uint32_t targetId = 0;
    std::int64_t closestDistance = std::numeric_limits<std::int64_t>::max();
    for (const Tempest::ControlNode &node : g_simulation.GetState().nodes) {
        if (node.owner != Tempest::Faction::Freegrid) {
            continue;
        }
        const std::int64_t distance = DistanceSquared(node.position, selected->position);
        if (distance < closestDistance) {
            closestDistance = distance;
            targetId = node.id;
        }
    }
    if (targetId != 0) {
        g_simulation.Submit(MakeCommand(Tempest::CommandKind::BuildRelay, 0, targetId));
        SetFeedback("Relay construction requested at the nearest owned node.");
    }
}

void ProduceCourier()
{
    for (const Tempest::Building &building : g_simulation.GetState().buildings) {
        if (building.hitPoints > 0 && building.complete &&
            building.faction == Tempest::Faction::Freegrid &&
            building.kind == Tempest::BuildingKind::Workshop) {
            g_simulation.Submit(MakeCommand(Tempest::CommandKind::ProduceCourier, building.id));
            SetFeedback("Courier production queued.");
            return;
        }
    }
}

bool SaveInterfaceConfiguration()
{
    if (g_settingsPath.empty()) {
        return false;
    }
    std::error_code error;
    std::filesystem::create_directories(g_settingsPath.parent_path(), error);
    if (error) {
        return false;
    }

    std::filesystem::path temporaryPath = g_settingsPath;
    temporaryPath += ".tmp";
    bool writeSucceeded = false;
    {
        std::ofstream output(temporaryPath, std::ios::binary | std::ios::trunc);
        const std::string configuration = g_interface.SerializeConfiguration();
        output.write(configuration.data(), static_cast<std::streamsize>(configuration.size()));
        output.flush();
        writeSucceeded = static_cast<bool>(output);
    }
    if (!writeSucceeded) {
        DeleteFileW(temporaryPath.c_str());
        return false;
    }
    if (!MoveFileExW(
            temporaryPath.c_str(),
            g_settingsPath.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temporaryPath.c_str());
        return false;
    }
    return true;
}

enum class ConfigurationLoadResult {
    NotFound,
    Loaded,
    Rejected,
};

ConfigurationLoadResult LoadInterfaceConfiguration()
{
    wchar_t localAppData[MAX_PATH] = {};
    const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return ConfigurationLoadResult::Rejected;
    }
    g_settingsPath = std::filesystem::path(localAppData) / L"ProjectTempest" / L"settings.ini";
    std::error_code error;
    if (!std::filesystem::exists(g_settingsPath, error)) {
        return error ? ConfigurationLoadResult::Rejected : ConfigurationLoadResult::NotFound;
    }

    std::ifstream input(g_settingsPath, std::ios::binary);
    if (!input) {
        return ConfigurationLoadResult::Rejected;
    }
    const std::string content((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    if (input.bad() || !g_interface.LoadConfiguration(content)) {
        return ConfigurationLoadResult::Rejected;
    }
    return ConfigurationLoadResult::Loaded;
}

void HandleInterfaceEvent(HWND window, const Tempest::Ui::InputEvent &event)
{
    if (event.intent == Tempest::Ui::Intent::ExitRequested) {
        DestroyWindow(window);
    } else if (event.intent == Tempest::Ui::Intent::BeginMatch) {
        ResetPrototype();
        SetFeedback("Link established. Capture a substation, build a Relay, then break the Chorus Spire.");
    } else if (event.intent == Tempest::Ui::Intent::RestartMatch) {
        ResetPrototype();
        SetFeedback("Substation 9 restarted.");
    } else if (event.intent == Tempest::Ui::Intent::SettingsChanged ||
               event.intent == Tempest::Ui::Intent::BindingChanged) {
        SetFeedback(SaveInterfaceConfiguration()
                ? "Settings saved to your local profile."
                : "Setting changed, but the local profile could not be saved.");
    } else if (event.intent == Tempest::Ui::Intent::BindingRejected) {
        SetFeedback("That input is reserved or already assigned.");
    } else if (event.intent == Tempest::Ui::Intent::GameplayAction) {
        switch (event.action) {
            case Tempest::Ui::Action::BuildRelay:
                BuildRelayAtNearestOwnedNode();
                break;
            case Tempest::Ui::Action::ProduceCourier:
                ProduceCourier();
                break;
            case Tempest::Ui::Action::ArcPulse:
                if (g_selectedUnitId != 0) {
                    g_simulation.Submit(MakeCommand(
                        Tempest::CommandKind::ArcPulse,
                        g_selectedUnitId,
                        0,
                        g_pointerPoint));
                    SetFeedback("Arc Pulse requested at cursor.");
                }
                break;
            case Tempest::Ui::Action::PrimarySelect:
                SelectFromScreen(g_pointerClient.x, g_pointerClient.y);
                break;
            case Tempest::Ui::Action::ContextCommand:
                IssueContextOrder(g_pointerClient.x, g_pointerClient.y);
                break;
            default:
                break;
        }
    }
    UpdateWindowTitle();
}

void HandleMouseButtonDown(HWND window, Tempest::Ui::MouseButton button, int x, int y)
{
    g_pointerClient = { x, y };
    g_pointerPoint = ScreenToSimulationPoint(x, y);
    const std::size_t index = static_cast<std::size_t>(button);
    if (index < std::size(g_mouseButtons)) {
        g_mouseButtons[index] = true;
    }
    HandleInterfaceEvent(window, g_interface.HandleMouseButton(button));
}

void HandleMouseButtonUp(Tempest::Ui::MouseButton button)
{
    const std::size_t index = static_cast<std::size_t>(button);
    if (index < std::size(g_mouseButtons)) {
        g_mouseButtons[index] = false;
    }
}

LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message) {
        case WM_KEYDOWN:
            if (wParam < 256) {
                g_keys[wParam] = true;
            }
            if ((lParam & (1L << 30)) != 0) {
                return 0;
            }
            {
                const Tempest::Ui::InputEvent event = g_interface.HandleKey(static_cast<std::uint16_t>(wParam));
                HandleInterfaceEvent(window, event);
            }
            return 0;

        case WM_KEYUP:
            if (wParam < 256) {
                g_keys[wParam] = false;
            }
            return 0;

        case WM_LBUTTONDOWN:
            HandleMouseButtonDown(
                window,
                Tempest::Ui::MouseButton::Left,
                GET_X_LPARAM(lParam),
                GET_Y_LPARAM(lParam));
            return 0;

        case WM_RBUTTONDOWN:
            HandleMouseButtonDown(
                window,
                Tempest::Ui::MouseButton::Right,
                GET_X_LPARAM(lParam),
                GET_Y_LPARAM(lParam));
            return 0;

        case WM_MBUTTONDOWN:
            HandleMouseButtonDown(
                window,
                Tempest::Ui::MouseButton::Middle,
                GET_X_LPARAM(lParam),
                GET_Y_LPARAM(lParam));
            return 0;

        case WM_XBUTTONDOWN:
            HandleMouseButtonDown(
                window,
                GET_XBUTTON_WPARAM(wParam) == XBUTTON1
                    ? Tempest::Ui::MouseButton::Extra1
                    : Tempest::Ui::MouseButton::Extra2,
                GET_X_LPARAM(lParam),
                GET_Y_LPARAM(lParam));
            return TRUE;

        case WM_LBUTTONUP:
            HandleMouseButtonUp(Tempest::Ui::MouseButton::Left);
            return 0;

        case WM_RBUTTONUP:
            HandleMouseButtonUp(Tempest::Ui::MouseButton::Right);
            return 0;

        case WM_MBUTTONUP:
            HandleMouseButtonUp(Tempest::Ui::MouseButton::Middle);
            return 0;

        case WM_XBUTTONUP:
            HandleMouseButtonUp(
                GET_XBUTTON_WPARAM(wParam) == XBUTTON1
                    ? Tempest::Ui::MouseButton::Extra1
                    : Tempest::Ui::MouseButton::Extra2);
            return TRUE;

        case WM_MOUSEMOVE:
            g_pointerClient = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            g_pointerPoint = ScreenToSimulationPoint(g_pointerClient.x, g_pointerClient.y);
            return 0;

        case WM_ACTIVATEAPP:
            g_applicationActive = wParam != 0;
            if (!wParam) {
                std::fill_n(g_keys, 256, false);
                std::fill(std::begin(g_mouseButtons), std::end(g_mouseButtons), false);
                if (g_interface.GetScreen() == Tempest::Ui::Screen::Playing) {
                    g_interface.HandleKey(Tempest::Ui::KeyEscape);
                    SetFeedback("Paused because the game window lost focus.");
                    UpdateWindowTitle();
                }
            }
            return 0;

        case WM_SIZE:
            if (g_rendererReady && wParam != SIZE_MINIMIZED) {
                const int width = std::max(1, static_cast<int>(LOWORD(lParam)));
                const int height = std::max(1, static_cast<int>(HIWORD(lParam)));
                WW3D::Set_Device_Resolution(width, height, 24, true);
                UpdateCameraProjection(width, height);
            }
            return 0;

        case WM_PAINT:
            {
                PAINTSTRUCT paint = {};
                BeginPaint(window, &paint);
                EndPaint(window, &paint);
            }
            return 0;

        case WM_CLOSE:
            DestroyWindow(window);
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

Line3DClass *AddLine(const Vector3 &start, const Vector3 &end, float width, const Vector3 &color)
{
    Line3DClass *line = new Line3DClass(start, end, width, color.X, color.Y, color.Z, 1.0F);
    g_scene->Add_Render_Object(line);
    return line;
}

void UpdateCameraTransform()
{
    if (!g_camera) {
        return;
    }
    Matrix3D cameraTransform;
    cameraTransform.Look_At(
        Vector3(g_cameraCenterX, g_cameraCenterY - 38.0F, 31.0F),
        Vector3(g_cameraCenterX, g_cameraCenterY, 0.0F),
        0.0F);
    g_camera->Set_Transform(cameraTransform);
}

void UpdateCameraProjection(int width, int height)
{
    if (!g_camera) {
        return;
    }
    const float aspect = static_cast<float>(std::max(1, width)) / static_cast<float>(std::max(1, height));
    constexpr float halfHeight = 0.5625F;
    const float halfWidth = halfHeight * aspect;
    g_camera->Set_View_Plane(Vector2(-halfWidth, -halfHeight), Vector2(halfWidth, halfHeight));
}

void UpdateCameraPan(float deltaSeconds)
{
    if (!g_applicationActive || !g_interface.AllowsGameplayInput()) {
        return;
    }
    float directionX = 0.0F;
    float directionY = 0.0F;
    directionX += g_interface.IsActionPressed(
        Tempest::Ui::Action::MoveRight,
        g_keys,
        std::size(g_keys),
        g_mouseButtons,
        std::size(g_mouseButtons)) ? 1.0F : 0.0F;
    directionX -= g_interface.IsActionPressed(
        Tempest::Ui::Action::MoveLeft,
        g_keys,
        std::size(g_keys),
        g_mouseButtons,
        std::size(g_mouseButtons)) ? 1.0F : 0.0F;
    directionY += g_interface.IsActionPressed(
        Tempest::Ui::Action::MoveUp,
        g_keys,
        std::size(g_keys),
        g_mouseButtons,
        std::size(g_mouseButtons)) ? 1.0F : 0.0F;
    directionY -= g_interface.IsActionPressed(
        Tempest::Ui::Action::MoveDown,
        g_keys,
        std::size(g_keys),
        g_mouseButtons,
        std::size(g_mouseButtons)) ? 1.0F : 0.0F;

    RECT client = {};
    GetClientRect(ApplicationHWnd, &client);
    if (g_interface.GetSettings().edgeScroll && !g_interface.GetSettings().reducedMotion &&
        client.right > 0 && client.bottom > 0) {
        const int clientWidth = static_cast<int>(client.right);
        const int clientHeight = static_cast<int>(client.bottom);
        const int edge = std::max(8, std::min(clientWidth, clientHeight) / 60);
        directionX -= g_pointerClient.x >= 0 && g_pointerClient.x <= edge ? 1.0F : 0.0F;
        directionX += g_pointerClient.x >= client.right - edge && g_pointerClient.x <= client.right ? 1.0F : 0.0F;
        directionY += g_pointerClient.y >= 0 && g_pointerClient.y <= edge ? 1.0F : 0.0F;
        directionY -= g_pointerClient.y >= client.bottom - edge && g_pointerClient.y <= client.bottom ? 1.0F : 0.0F;
    }
    if (directionX == 0.0F && directionY == 0.0F) {
        return;
    }
    const float normalizer = directionX != 0.0F && directionY != 0.0F ? 0.7071067F : 1.0F;
    const float speed = 9.0F *
        (static_cast<float>(g_interface.GetSettings().cameraSpeedPercent) / 100.0F) * deltaSeconds * normalizer;
    g_cameraCenterX = std::clamp(g_cameraCenterX + (directionX * speed), -9.0F, 9.0F);
    g_cameraCenterY = std::clamp(g_cameraCenterY + (directionY * speed), -9.0F, 9.0F);
    UpdateCameraTransform();
    g_pointerPoint = ScreenToSimulationPoint(g_pointerClient.x, g_pointerClient.y);
}

void CreateArenaGrid()
{
    const Vector3 gridColor(0.035F, 0.16F, 0.23F);
    for (int coordinate = -18; coordinate <= 18; coordinate += 3) {
        const float position = static_cast<float>(coordinate);
        g_gridLines.push_back(AddLine(
            Vector3(-kArenaExtent, position, -0.08F),
            Vector3(kArenaExtent, position, -0.08F),
            0.035F,
            gridColor));
        g_gridLines.push_back(AddLine(
            Vector3(position, -kArenaExtent, -0.08F),
            Vector3(position, kArenaExtent, -0.08F),
            0.035F,
            gridColor));
    }

    g_selectionHorizontal = AddLine(Vector3(), Vector3(), 0.14F, Vector3(0.85F, 0.98F, 1.0F));
    g_selectionVertical = AddLine(Vector3(), Vector3(), 0.14F, Vector3(0.85F, 0.98F, 1.0F));
    g_selectionHorizontal->Set_Hidden(1);
    g_selectionVertical->Set_Hidden(1);
}

CrossVisual *EnsureCrossVisual(std::vector<CrossVisual> &visuals, std::uint32_t id)
{
    const auto found = std::find_if(visuals.begin(), visuals.end(), [id](const CrossVisual &visual) {
        return visual.id == id;
    });
    if (found != visuals.end()) {
        return &*found;
    }

    CrossVisual visual;
    visual.id = id;
    visual.horizontal = AddLine(Vector3(), Vector3(), 0.12F, Vector3(1.0F, 1.0F, 1.0F));
    visual.vertical = AddLine(Vector3(), Vector3(), 0.12F, Vector3(1.0F, 1.0F, 1.0F));
    visuals.push_back(visual);
    return &visuals.back();
}

void UpdateCrossVisual(
    CrossVisual &visual,
    Tempest::Point point,
    float extent,
    const Vector3 &color,
    Tempest::Faction owner)
{
    const float x = static_cast<float>(point.x) * kSimulationToWorld;
    const float y = static_cast<float>(point.y) * kSimulationToWorld;
    const bool chorusShape = g_interface.GetSettings().colourIndependentCues && owner == Tempest::Faction::Chorus;
    if (chorusShape) {
        visual.horizontal->Reset(
            Vector3(x - extent, y - extent, 0.06F),
            Vector3(x + extent, y + extent, 0.06F));
        visual.vertical->Reset(
            Vector3(x - extent, y + extent, 0.06F),
            Vector3(x + extent, y - extent, 0.06F));
    } else {
        visual.horizontal->Reset(Vector3(x - extent, y, 0.06F), Vector3(x + extent, y, 0.06F));
        visual.vertical->Reset(Vector3(x, y - extent, 0.06F), Vector3(x, y + extent, 0.06F));
    }
    visual.horizontal->Re_Color(color.X, color.Y, color.Z);
    visual.vertical->Re_Color(color.X, color.Y, color.Z);
}

void SyncMarkerVisuals()
{
    const Vector3 neutralColor(1.0F, 0.62F, 0.10F);
    const Vector3 freegridColor(0.08F, 0.86F, 1.0F);
    const Vector3 chorusColor(1.0F, 0.12F, 0.38F);

    for (const Tempest::ControlNode &node : g_simulation.GetState().nodes) {
        CrossVisual *visual = EnsureCrossVisual(g_nodeVisuals, node.id);
        const Vector3 &color = node.owner == Tempest::Faction::Freegrid
            ? freegridColor
            : (node.owner == Tempest::Faction::Chorus ? chorusColor : neutralColor);
        UpdateCrossVisual(*visual, node.position, 1.4F, color, node.owner);
    }

    for (auto visual = g_buildingVisuals.begin(); visual != g_buildingVisuals.end();) {
        if (FindBuilding(visual->id)) {
            ++visual;
        } else {
            RemoveLine(visual->horizontal);
            RemoveLine(visual->vertical);
            visual = g_buildingVisuals.erase(visual);
        }
    }
    for (const Tempest::Building &building : g_simulation.GetState().buildings) {
        if (building.hitPoints <= 0) {
            continue;
        }
        CrossVisual *visual = EnsureCrossVisual(g_buildingVisuals, building.id);
        const Vector3 &color = building.faction == Tempest::Faction::Chorus ? chorusColor : freegridColor;
        const float extent = building.kind == Tempest::BuildingKind::ChorusCore
            ? 3.2F
            : (building.kind == Tempest::BuildingKind::Workshop ? 2.7F : 2.1F);
        UpdateCrossVisual(*visual, building.position, extent, color, building.faction);
    }
}

bool SyncBuildingModelVisuals()
{
    for (auto visual = g_buildingModelVisuals.begin(); visual != g_buildingModelVisuals.end();) {
        const Tempest::Building *building = FindBuilding(visual->id);
        if (building && building->kind == Tempest::BuildingKind::Relay) {
            ++visual;
        } else {
            RemoveRenderObject(visual->object);
            visual = g_buildingModelVisuals.erase(visual);
        }
    }

    for (const Tempest::Building &building : g_simulation.GetState().buildings) {
        if (building.hitPoints <= 0 || building.kind != Tempest::BuildingKind::Relay) {
            continue;
        }
        auto found = std::find_if(
            g_buildingModelVisuals.begin(),
            g_buildingModelVisuals.end(),
            [&building](const BuildingModelVisual &visual) { return visual.id == building.id; });
        if (found == g_buildingModelVisuals.end()) {
            BuildingModelVisual visual;
            visual.id = building.id;
            visual.object = g_assetManager->Create_Render_Obj("relay");
            if (!visual.object) {
                return false;
            }
            g_scene->Add_Render_Object(visual.object);
            g_buildingModelVisuals.push_back(visual);
            found = g_buildingModelVisuals.end() - 1;
        }

        Matrix3D transform(1);
        transform.Set_Translation(Vector3(
            static_cast<float>(building.position.x) * kSimulationToWorld,
            static_cast<float>(building.position.y) * kSimulationToWorld,
            0.0F));
        found->object->Set_Transform(transform);
        found->object->Set_ObjectColor(0x16D9FF);
    }
    return true;
}

bool SyncUnitVisuals()
{
    for (auto visual = g_unitVisuals.begin(); visual != g_unitVisuals.end();) {
        if (FindUnit(visual->id)) {
            ++visual;
        } else {
            RemoveRenderObject(visual->object);
            visual = g_unitVisuals.erase(visual);
        }
    }

    for (const Tempest::Unit &unit : g_simulation.GetState().units) {
        if (!unit.alive) {
            continue;
        }
        const bool isDrone = unit.kind == Tempest::UnitKind::ChorusDrone;
        const bool damaged = !isDrone && unit.hitPoints * 2 <= unit.maximumHitPoints;
        const char *modelName = isDrone ? "drone" : (damaged ? "courierd" : "courier");
        auto found = std::find_if(g_unitVisuals.begin(), g_unitVisuals.end(), [&unit](const UnitVisual &visual) {
            return visual.id == unit.id;
        });
        if (found == g_unitVisuals.end()) {
            UnitVisual visual;
            visual.id = unit.id;
            visual.damaged = damaged;
            visual.object = g_assetManager->Create_Render_Obj(modelName);
            if (!visual.object) {
                return false;
            }
            g_scene->Add_Render_Object(visual.object);
            g_unitVisuals.push_back(visual);
            found = g_unitVisuals.end() - 1;
        } else if (found->damaged != damaged) {
            RenderObjClass *replacement = g_assetManager->Create_Render_Obj(modelName);
            if (!replacement) {
                return false;
            }
            g_scene->Add_Render_Object(replacement);
            RemoveRenderObject(found->object);
            found->object = replacement;
            found->damaged = damaged;
        }

        Tempest::Point facingPoint = unit.destination;
        if (unit.order == Tempest::OrderKind::Attack) {
            if (const Tempest::Unit *targetUnit = FindUnit(unit.targetId)) {
                facingPoint = targetUnit->position;
            } else if (const Tempest::Building *targetBuilding = FindBuilding(unit.targetId)) {
                facingPoint = targetBuilding->position;
            }
        }
        const float heading = std::atan2(
            static_cast<float>(facingPoint.y - unit.position.y),
            static_cast<float>(facingPoint.x - unit.position.x));
        Matrix3D transform(1);
        transform.Rotate_Z(heading);
        transform.Scale(isDrone ? 0.72F : 1.0F);
        transform.Set_Translation(Vector3(
            static_cast<float>(unit.position.x) * kSimulationToWorld,
            static_cast<float>(unit.position.y) * kSimulationToWorld,
            0.0F));
        found->object->Set_Transform(transform);
        found->object->Set_ObjectColor(unit.faction == Tempest::Faction::Chorus ? 0xFF1F5A : 0x16D9FF);
    }

    const Tempest::Unit *selected = FindUnit(g_selectedUnitId);
    if (!selected || selected->faction != Tempest::Faction::Freegrid) {
        g_selectedUnitId = 0;
        for (const Tempest::Unit &unit : g_simulation.GetState().units) {
            if (unit.alive && unit.faction == Tempest::Faction::Freegrid) {
                g_selectedUnitId = unit.id;
                selected = &unit;
                break;
            }
        }
    }
    if (selected) {
        const float x = static_cast<float>(selected->position.x) * kSimulationToWorld;
        const float y = static_cast<float>(selected->position.y) * kSimulationToWorld;
        g_selectionHorizontal->Reset(Vector3(x - 1.7F, y, 0.03F), Vector3(x + 1.7F, y, 0.03F));
        g_selectionVertical->Reset(Vector3(x, y - 1.7F, 0.03F), Vector3(x, y + 1.7F, 0.03F));
        g_selectionHorizontal->Set_Hidden(0);
        g_selectionVertical->Set_Hidden(0);
    } else {
        g_selectionHorizontal->Set_Hidden(1);
        g_selectionVertical->Set_Hidden(1);
    }
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
    if (!g_assetManager || !g_assetManager->Load_3D_Assets("courier.w3d") ||
        !g_assetManager->Load_3D_Assets("courierd.w3d") ||
        !g_assetManager->Load_3D_Assets("drone.w3d") ||
        !g_assetManager->Load_3D_Assets("relay.w3d")) {
        return false;
    }

    g_scene = new SimpleSceneClass;
    g_scene->Set_Ambient_Light(Vector3(0.30F, 0.34F, 0.38F));
    g_scene->Set_Fog_Enable(false);

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
    UpdateCameraTransform();
    UpdateCameraProjection(
        std::max(1L, client.right - client.left),
        std::max(1L, client.bottom - client.top));
    g_camera->Set_Clip_Planes(1.0F, 250.0F);

    CreateArenaGrid();
    ResetPrototype();
    SyncMarkerVisuals();
    return SyncBuildingModelVisuals() && SyncUnitVisuals();
}

int ScalePixels(float value, float scale)
{
    return std::max(1, static_cast<int>(std::lround(value * scale)));
}

float GetInterfaceScale(const RECT &client)
{
    const float widthScale = static_cast<float>(std::max(1L, client.right - client.left)) /
        static_cast<float>(kInitialWidth);
    const float heightScale = static_cast<float>(std::max(1L, client.bottom - client.top)) /
        static_cast<float>(kInitialHeight);
    const float userScale = static_cast<float>(g_interface.GetSettings().uiScalePercent) / 100.0F;
    return std::clamp(std::min(widthScale, heightScale) * userScale, 0.72F, 3.25F);
}

void EnsureHudFonts(float scale)
{
    const int scaleKey = static_cast<int>(std::lround(scale * 100.0F));
    if (scaleKey == g_hudFontScale && g_hudFont && g_hudSmallFont && g_hudTitleFont) {
        return;
    }
    if (g_hudFont) {
        DeleteObject(g_hudFont);
    }
    if (g_hudSmallFont) {
        DeleteObject(g_hudSmallFont);
    }
    if (g_hudTitleFont) {
        DeleteObject(g_hudTitleFont);
    }
    g_hudSmallFont = CreateFontA(
        -ScalePixels(13.0F, scale), 0, 0, 0, FW_MEDIUM, FALSE, FALSE, FALSE,
        ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
        DEFAULT_PITCH | FF_SWISS, "Segoe UI");
    g_hudFont = CreateFontA(
        -ScalePixels(17.0F, scale), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
        DEFAULT_PITCH | FF_SWISS, "Segoe UI");
    g_hudTitleFont = CreateFontA(
        -ScalePixels(28.0F, scale), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
        DEFAULT_PITCH | FF_SWISS, "Segoe UI");
    g_hudFontScale = scaleKey;
}

void DrawPanel(HDC device, const RECT &rect, COLORREF fill, COLORREF border)
{
    HBRUSH fillBrush = CreateSolidBrush(fill);
    HBRUSH borderBrush = CreateSolidBrush(border);
    FillRect(device, &rect, fillBrush);
    FrameRect(device, &rect, borderBrush);
    DeleteObject(borderBrush);
    DeleteObject(fillBrush);
}

void DrawLabel(
    HDC device,
    HFONT font,
    COLORREF color,
    const RECT &rect,
    const char *text,
    UINT flags = DT_LEFT | DT_TOP | DT_SINGLELINE)
{
    SelectObject(device, font);
    SetTextColor(device, color);
    RECT drawRect = rect;
    DrawTextA(device, text, -1, &drawRect, flags | DT_NOPREFIX);
}

void FormatBindingName(Tempest::Ui::InputBinding binding, char *buffer, std::size_t bufferSize)
{
    if (binding.device == Tempest::Ui::InputDevice::Mouse) {
        const char *mouseName = nullptr;
        switch (static_cast<Tempest::Ui::MouseButton>(binding.code)) {
            case Tempest::Ui::MouseButton::Left: mouseName = "MOUSE LEFT"; break;
            case Tempest::Ui::MouseButton::Right: mouseName = "MOUSE RIGHT"; break;
            case Tempest::Ui::MouseButton::Middle: mouseName = "MOUSE MIDDLE"; break;
            case Tempest::Ui::MouseButton::Extra1: mouseName = "MOUSE 4"; break;
            case Tempest::Ui::MouseButton::Extra2: mouseName = "MOUSE 5"; break;
        }
        std::snprintf(buffer, bufferSize, "%s", mouseName ? mouseName : "MOUSE ?");
        return;
    }

    const std::uint16_t key = binding.code;
    const char *special = nullptr;
    switch (key) {
        case Tempest::Ui::KeySpace: special = "SPACE"; break;
        case Tempest::Ui::KeyLeft: special = "LEFT"; break;
        case Tempest::Ui::KeyRight: special = "RIGHT"; break;
        case Tempest::Ui::KeyUp: special = "UP"; break;
        case Tempest::Ui::KeyDown: special = "DOWN"; break;
        default: break;
    }
    if (special) {
        std::snprintf(buffer, bufferSize, "%s", special);
    } else if ((key >= '0' && key <= '9') || (key >= 'A' && key <= 'Z')) {
        std::snprintf(buffer, bufferSize, "%c", static_cast<char>(key));
    } else {
        std::snprintf(buffer, bufferSize, "VK-%u", static_cast<unsigned int>(key));
    }
}

void DrawHud(HDC device, const RECT &client, float scale)
{
    const Tempest::MatchState &match = g_simulation.GetState();
    const Tempest::Unit *selected = FindUnit(g_selectedUnitId);
    const int margin = ScalePixels(14.0F, scale);
    const int topHeight = ScalePixels(66.0F, scale);
    const int bottomHeight = ScalePixels(58.0F, scale);
    const COLORREF panel = RGB(7, 17, 26);
    const COLORREF border = RGB(28, 187, 217);
    const COLORREF primary = RGB(225, 244, 247);
    const COLORREF secondary = RGB(141, 181, 190);
    const COLORREF accent = RGB(31, 218, 242);

    RECT top = { margin, margin, client.right - margin, margin + topHeight };
    DrawPanel(device, top, panel, border);
    const int freegridNodes = static_cast<int>(std::count_if(
        match.nodes.begin(), match.nodes.end(), [](const Tempest::ControlNode &node) {
            return node.owner == Tempest::Faction::Freegrid;
        }));
    const int chorusNodes = static_cast<int>(std::count_if(
        match.nodes.begin(), match.nodes.end(), [](const Tempest::ControlNode &node) {
            return node.owner == Tempest::Faction::Chorus;
        }));
    char status[384];
    std::snprintf(
        status,
        sizeof(status),
        "SUBSTATION 9    SALVAGE %d    GRID CHARGE %d%%    RELAYS [F] %d / [C] %d / 3    %02llu:%02llu",
        match.freegridCredits,
        match.freegridPower,
        freegridNodes,
        chorusNodes,
        static_cast<unsigned long long>(match.tick / Tempest::TicksPerSecond / 60),
        static_cast<unsigned long long>(match.tick / Tempest::TicksPerSecond % 60));
    RECT statusRect = {
        top.left + ScalePixels(18.0F, scale),
        top.top + ScalePixels(11.0F, scale),
        top.right - ScalePixels(18.0F, scale),
        top.bottom,
    };
    DrawLabel(device, g_hudFont, accent, statusRect, status);

    char detail[384];
    if (selected) {
        std::snprintf(
            detail,
            sizeof(detail),
            "SELECTED  COURIER #%u  INTEGRITY %d/%d    OBJECTIVE  Secure relays, then destroy the Chorus Spire [C].",
            selected->id,
            selected->hitPoints,
            selected->maximumHitPoints);
    } else {
        std::snprintf(
            detail,
            sizeof(detail),
            "SELECTED  NONE    OBJECTIVE  Secure relays, then destroy the Chorus Spire [C].");
    }
    RECT detailRect = statusRect;
    detailRect.top += ScalePixels(29.0F, scale);
    DrawLabel(device, g_hudSmallFont, primary, detailRect, detail);

    RECT bottom = {
        margin,
        client.bottom - margin - bottomHeight,
        client.right - margin,
        client.bottom - margin,
    };
    DrawPanel(device, bottom, panel, border);
    char buildKey[24];
    char produceKey[24];
    char pulseKey[24];
    char pauseKey[24];
    char settingsKey[24];
    char selectKey[24];
    char contextKey[24];
    FormatBindingName(g_interface.BindingFor(Tempest::Ui::Action::PrimarySelect), selectKey, sizeof(selectKey));
    FormatBindingName(g_interface.BindingFor(Tempest::Ui::Action::ContextCommand), contextKey, sizeof(contextKey));
    FormatBindingName(g_interface.BindingFor(Tempest::Ui::Action::BuildRelay), buildKey, sizeof(buildKey));
    FormatBindingName(g_interface.BindingFor(Tempest::Ui::Action::ProduceCourier), produceKey, sizeof(produceKey));
    FormatBindingName(g_interface.BindingFor(Tempest::Ui::Action::ArcPulse), pulseKey, sizeof(pulseKey));
    FormatBindingName(g_interface.BindingFor(Tempest::Ui::Action::Pause), pauseKey, sizeof(pauseKey));
    FormatBindingName(g_interface.BindingFor(Tempest::Ui::Action::OpenSettings), settingsKey, sizeof(settingsKey));
    char commands[512];
    std::snprintf(
        commands,
        sizeof(commands),
        "[%s] SELECT   [%s] MOVE / CAPTURE / ATTACK   [%s] RELAY   [%s] COURIER   [%s] ARC PULSE   [%s] PAUSE   [%s] SETTINGS",
        selectKey,
        contextKey,
        buildKey,
        produceKey,
        pulseKey,
        pauseKey,
        settingsKey);
    RECT commandRect = {
        bottom.left + ScalePixels(14.0F, scale),
        bottom.top + ScalePixels(9.0F, scale),
        bottom.right - ScalePixels(14.0F, scale),
        bottom.bottom,
    };
    DrawLabel(device, g_hudSmallFont, primary, commandRect, commands);
    if (g_feedbackUntil == 0 || static_cast<LONG>(g_feedbackUntil - timeGetTime()) > 0) {
        commandRect.top += ScalePixels(25.0F, scale);
        DrawLabel(device, g_hudSmallFont, secondary, commandRect, g_feedback);
    }
}

void DrawSettingsOverlay(HDC device, const RECT &client, float scale)
{
    const int width = std::min(
        static_cast<int>(client.right) - ScalePixels(24.0F, scale),
        ScalePixels(940.0F, scale));
    const int height = std::min(
        static_cast<int>(client.bottom) - ScalePixels(24.0F, scale),
        ScalePixels(610.0F, scale));
    RECT panel = {
        (client.right - width) / 2,
        (client.bottom - height) / 2,
        (client.right + width) / 2,
        (client.bottom + height) / 2,
    };
    DrawPanel(device, panel, RGB(6, 13, 21), RGB(41, 209, 231));
    const int padding = ScalePixels(24.0F, scale);
    RECT title = { panel.left + padding, panel.top + padding, panel.right - padding, panel.top + ScalePixels(66.0F, scale) };
    DrawLabel(device, g_hudTitleFont, RGB(222, 248, 250), title, "SETTINGS / ACCESSIBILITY");
    RECT hint = title;
    hint.top += ScalePixels(38.0F, scale);
    DrawLabel(
        device,
        g_hudSmallFont,
        RGB(135, 180, 188),
        hint,
        "UP/DOWN select   LEFT/RIGHT adjust   ENTER rebind   ESC return to pause");

    const char *settingNames[Tempest::Ui::InterfaceState::AdjustableSettingCount] = {
        "Camera speed",
        "UI scale",
        "Master volume",
        "Music volume",
        "Effects volume",
        "Edge scroll",
        "Reduced motion",
        "Reduced flashes",
        "Colour-independent cues",
    };
    const Tempest::Ui::Settings &settings = g_interface.GetSettings();
    const int selectedRow = g_interface.GetSelectedSettingsRow();
    const int rowHeight = ScalePixels(31.0F, scale);
    const int contentTop = panel.top + ScalePixels(100.0F, scale);
    const int gutter = ScalePixels(20.0F, scale);
    const int columnWidth = (panel.right - panel.left - (padding * 2) - gutter) / 2;
    for (int row = 0; row < Tempest::Ui::InterfaceState::AdjustableSettingCount; ++row) {
        RECT rowRect = {
            panel.left + padding,
            contentTop + (row * rowHeight),
            panel.left + padding + columnWidth,
            contentTop + ((row + 1) * rowHeight) - ScalePixels(3.0F, scale),
        };
        if (row == selectedRow) {
            HBRUSH selected = CreateSolidBrush(RGB(20, 77, 91));
            FillRect(device, &rowRect, selected);
            DeleteObject(selected);
        }
        char value[96];
        switch (row) {
            case 0: std::snprintf(value, sizeof(value), "%d%%", settings.cameraSpeedPercent); break;
            case 1: std::snprintf(value, sizeof(value), "%d%%", settings.uiScalePercent); break;
            case 2: std::snprintf(value, sizeof(value), "%d%%", settings.masterVolume); break;
            case 3: std::snprintf(value, sizeof(value), "%d%%", settings.musicVolume); break;
            case 4: std::snprintf(value, sizeof(value), "%d%%", settings.effectsVolume); break;
            case 5: std::snprintf(value, sizeof(value), "%s", settings.edgeScroll ? "ON" : "OFF"); break;
            case 6: std::snprintf(value, sizeof(value), "%s", settings.reducedMotion ? "ON" : "OFF"); break;
            case 7: std::snprintf(value, sizeof(value), "%s", settings.reducedFlashes ? "ON" : "OFF"); break;
            case 8: std::snprintf(value, sizeof(value), "%s", settings.colourIndependentCues ? "ON" : "OFF"); break;
            default: value[0] = '\0'; break;
        }
        char line[180];
        std::snprintf(line, sizeof(line), "%s    %s", settingNames[row], value);
        rowRect.left += ScalePixels(8.0F, scale);
        DrawLabel(device, g_hudSmallFont, RGB(223, 242, 244), rowRect, line, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }

    for (int actionRow = 0; actionRow < Tempest::Ui::InterfaceState::RemappableActionCount; ++actionRow) {
        const int absoluteRow = Tempest::Ui::InterfaceState::AdjustableSettingCount + actionRow;
        const Tempest::Ui::Action action = g_interface.ActionForSettingsRow(absoluteRow);
        RECT rowRect = {
            panel.left + padding + columnWidth + gutter,
            contentTop + (actionRow * rowHeight),
            panel.right - padding,
            contentTop + ((actionRow + 1) * rowHeight) - ScalePixels(3.0F, scale),
        };
        if (absoluteRow == selectedRow) {
            HBRUSH selected = CreateSolidBrush(RGB(20, 77, 91));
            FillRect(device, &rowRect, selected);
            DeleteObject(selected);
        }
        char keyName[24];
        FormatBindingName(g_interface.BindingFor(action), keyName, sizeof(keyName));
        char line[180];
        std::snprintf(line, sizeof(line), "%s    [%s]", Tempest::Ui::InterfaceState::ActionName(action), keyName);
        rowRect.left += ScalePixels(8.0F, scale);
        DrawLabel(device, g_hudSmallFont, RGB(223, 242, 244), rowRect, line, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }
    if (g_interface.IsCapturingBinding()) {
        RECT capture = { panel.left + padding, panel.bottom - ScalePixels(55.0F, scale), panel.right - padding, panel.bottom };
        DrawLabel(device, g_hudFont, RGB(255, 184, 67), capture, "PRESS A NEW KEY OR MOUSE BUTTON - ESC CANCELS", DT_CENTER | DT_TOP | DT_SINGLELINE);
    } else {
        RECT note = { panel.left + padding, panel.bottom - ScalePixels(45.0F, scale), panel.right - padding, panel.bottom };
        DrawLabel(
            device,
            g_hudSmallFont,
            RGB(128, 164, 173),
            note,
            "Changes save atomically to the local profile; original music/effects playback remains an M5 content task.",
            DT_CENTER | DT_TOP | DT_SINGLELINE);
    }
}

void DrawModalOverlay(HDC device, const RECT &client, float scale)
{
    const Tempest::Ui::Screen screen = g_interface.GetScreen();
    if (screen == Tempest::Ui::Screen::Playing) {
        return;
    }
    if (screen == Tempest::Ui::Screen::Settings) {
        DrawSettingsOverlay(device, client, scale);
        return;
    }
    const int width = std::min(
        static_cast<int>(client.right) - ScalePixels(30.0F, scale),
        ScalePixels(760.0F, scale));
    const int height = std::min(
        static_cast<int>(client.bottom) - ScalePixels(30.0F, scale),
        ScalePixels(410.0F, scale));
    RECT panel = {
        (client.right - width) / 2,
        (client.bottom - height) / 2,
        (client.right + width) / 2,
        (client.bottom + height) / 2,
    };
    DrawPanel(device, panel, RGB(5, 12, 20), RGB(40, 210, 232));
    const int padding = ScalePixels(30.0F, scale);
    RECT title = { panel.left + padding, panel.top + padding, panel.right - padding, panel.top + ScalePixels(85.0F, scale) };
    RECT body = { panel.left + padding, panel.top + ScalePixels(98.0F, scale), panel.right - padding, panel.bottom - padding };
    if (screen == Tempest::Ui::Screen::Briefing) {
        DrawLabel(device, g_hudTitleFont, RGB(226, 249, 250), title, "BLACK CURRENT / SUBSTATION 9");
        char settingsKey[24];
        FormatBindingName(g_interface.BindingFor(Tempest::Ui::Action::OpenSettings), settingsKey, sizeof(settingsKey));
        char briefing[640];
        std::snprintf(
            briefing,
            sizeof(briefing),
            "2089. Chorus has colonised the basin's abandoned control grid.\n\n"
            "SELECT a Freegrid Courier. CAPTURE substations to earn salvage and grid charge. "
            "BUILD Relays, PRODUCE Couriers, then destroy the Chorus Spire [C]. "
            "If your Relay Core [F] falls, the district is lost.\n\n"
            "ENTER  establish link and begin     [%s]  settings     ESC  exit",
            settingsKey);
        DrawLabel(
            device,
            g_hudFont,
            RGB(195, 224, 228),
            body,
            briefing,
            DT_LEFT | DT_TOP | DT_WORDBREAK);
    } else if (screen == Tempest::Ui::Screen::Pause) {
        DrawLabel(device, g_hudTitleFont, RGB(226, 249, 250), title, "NETWORK PAUSED");
        char pauseKey[24];
        char settingsKey[24];
        char restartKey[24];
        FormatBindingName(g_interface.BindingFor(Tempest::Ui::Action::Pause), pauseKey, sizeof(pauseKey));
        FormatBindingName(g_interface.BindingFor(Tempest::Ui::Action::OpenSettings), settingsKey, sizeof(settingsKey));
        FormatBindingName(g_interface.BindingFor(Tempest::Ui::Action::Restart), restartKey, sizeof(restartKey));
        char copy[320];
        std::snprintf(
            copy,
            sizeof(copy),
            "Simulation time is stopped.\n\n[%s] or ESC  resume\n[%s]  settings / controls\n[%s]  restart Substation 9",
            pauseKey,
            settingsKey,
            restartKey);
        DrawLabel(device, g_hudFont, RGB(195, 224, 228), body, copy, DT_LEFT | DT_TOP | DT_WORDBREAK);
    } else if (screen == Tempest::Ui::Screen::Result) {
        const Tempest::MatchState &match = g_simulation.GetState();
        const bool victory = match.outcome == Tempest::MatchOutcome::Victory;
        DrawLabel(
            device,
            g_hudTitleFont,
            victory ? RGB(51, 231, 209) : RGB(255, 79, 106),
            title,
            victory ? "GRID RESTORED" : "DISTRICT LOST");
        const int freegridNodes = static_cast<int>(std::count_if(
            match.nodes.begin(), match.nodes.end(), [](const Tempest::ControlNode &node) {
                return node.owner == Tempest::Faction::Freegrid;
            }));
        char restartKey[24];
        char settingsKey[24];
        FormatBindingName(g_interface.BindingFor(Tempest::Ui::Action::Restart), restartKey, sizeof(restartKey));
        FormatBindingName(g_interface.BindingFor(Tempest::Ui::Action::OpenSettings), settingsKey, sizeof(settingsKey));
        char copy[512];
        std::snprintf(
            copy,
            sizeof(copy),
            "%s\n\nDecisive state: %d of 3 substations linked, %d salvage banked, %d%% grid charge.\n\n[%s]  restart without returning to desktop\n[%s]  settings\nESC  exit",
            victory
                ? "You severed the Chorus Spire and returned the grid to Freegrid control."
                : "The Relay Core fell before the Chorus signal could be isolated.",
            freegridNodes,
            match.freegridCredits,
            match.freegridPower,
            restartKey,
            settingsKey);
        DrawLabel(device, g_hudFont, RGB(195, 224, 228), body, copy, DT_LEFT | DT_TOP | DT_WORDBREAK);
    }
}

void DrawInterface()
{
    if (!ApplicationHWnd) {
        return;
    }
    RECT client = {};
    GetClientRect(ApplicationHWnd, &client);
    if (client.right <= 0 || client.bottom <= 0) {
        return;
    }
    const float scale = GetInterfaceScale(client);
    EnsureHudFonts(scale);
    HDC device = GetDC(ApplicationHWnd);
    if (!device) {
        return;
    }
    SetBkMode(device, TRANSPARENT);
    DrawHud(device, client, scale);
    DrawModalOverlay(device, client, scale);
    ReleaseDC(ApplicationHWnd, device);
}

void DrawLoadingScreen()
{
    RECT client = {};
    GetClientRect(ApplicationHWnd, &client);
    HDC device = GetDC(ApplicationHWnd);
    if (!device) {
        return;
    }
    HBRUSH background = CreateSolidBrush(RGB(4, 11, 18));
    FillRect(device, &client, background);
    DeleteObject(background);
    const float scale = GetInterfaceScale(client);
    EnsureHudFonts(scale);
    SetBkMode(device, TRANSPARENT);
    RECT title = { 0, client.bottom / 2 - ScalePixels(60.0F, scale), client.right, client.bottom };
    DrawLabel(device, g_hudTitleFont, RGB(38, 216, 237), title, "PROJECT TEMPEST", DT_CENTER | DT_TOP | DT_SINGLELINE);
    title.top += ScalePixels(48.0F, scale);
    DrawLabel(device, g_hudFont, RGB(164, 201, 208), title, "Linking original Substation 9 content...", DT_CENTER | DT_TOP | DT_SINGLELINE);
    ReleaseDC(ApplicationHWnd, device);
}

void UpdatePrototype(float deltaSeconds)
{
    UpdateCameraPan(deltaSeconds);
    if (g_interface.AdvancesSimulation()) {
        g_simulationAccumulator = std::min(0.25, g_simulationAccumulator + static_cast<double>(deltaSeconds));
    } else {
        g_simulationAccumulator = 0.0;
    }
    while (g_interface.AdvancesSimulation() && g_simulationAccumulator >= kSimulationTickSeconds) {
        g_simulation.Step();
        g_interface.SyncOutcome(g_simulation.GetState().outcome);
        g_simulationAccumulator -= kSimulationTickSeconds;
    }

    SyncMarkerVisuals();
    if (!SyncBuildingModelVisuals() || !SyncUnitVisuals()) {
        PostQuitMessage(3);
    }

    static DWORD lastTitleUpdate = 0;
    const DWORD now = timeGetTime();
    if ((now - lastTitleUpdate) > 150) {
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
    DrawInterface();
}

void ShutdownRenderer()
{
    for (UnitVisual &visual : g_unitVisuals) {
        RemoveRenderObject(visual.object);
    }
    g_unitVisuals.clear();
    for (CrossVisual &visual : g_nodeVisuals) {
        RemoveLine(visual.horizontal);
        RemoveLine(visual.vertical);
    }
    g_nodeVisuals.clear();
    for (CrossVisual &visual : g_buildingVisuals) {
        RemoveLine(visual.horizontal);
        RemoveLine(visual.vertical);
    }
    g_buildingVisuals.clear();
    for (BuildingModelVisual &visual : g_buildingModelVisuals) {
        RemoveRenderObject(visual.object);
    }
    g_buildingModelVisuals.clear();
    for (Line3DClass *&line : g_gridLines) {
        RemoveLine(line);
    }
    g_gridLines.clear();
    RemoveLine(g_selectionHorizontal);
    RemoveLine(g_selectionVertical);

    if (g_hudFont) {
        DeleteObject(g_hudFont);
        g_hudFont = nullptr;
    }
    if (g_hudSmallFont) {
        DeleteObject(g_hudSmallFont);
        g_hudSmallFont = nullptr;
    }
    if (g_hudTitleFont) {
        DeleteObject(g_hudTitleFont);
        g_hudTitleFont = nullptr;
    }
    g_hudFontScale = 0;

    if (g_keyLight) {
        g_keyLight->Remove();
    }
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
    g_interface.ResetForBoot();

    char executablePath[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, executablePath, MAX_PATH);
    char *separator = std::strrchr(executablePath, '\\');
    if (separator) {
        *separator = '\0';
        SetCurrentDirectoryA(executablePath);
    }
    const ConfigurationLoadResult configurationLoad = LoadInterfaceConfiguration();

    if (!CreateMainWindow(instance, commandShow)) {
        MessageBoxA(nullptr, "Unable to create the Project Tempest window.", "Project Tempest", MB_ICONERROR);
        return 1;
    }

    RECT client = {};
    GetClientRect(ApplicationHWnd, &client);
    g_pointerClient = { client.right / 2, client.bottom / 2 };
    DrawLoadingScreen();

    if (!InitialiseRenderer()) {
        MessageBoxA(
            ApplicationHWnd,
            "The renderer or a required Project Tempest W3D could not be initialised. Verify the demo package.",
            "Project Tempest startup failed",
            MB_ICONERROR);
        ShutdownRenderer();
        return 2;
    }
    if (configurationLoad == ConfigurationLoadResult::Loaded) {
        SetFeedback("Local settings restored. Press ENTER to establish the Freegrid link.");
    } else if (configurationLoad == ConfigurationLoadResult::Rejected) {
        SetFeedback("Local settings were invalid or unreadable; safe defaults are active.");
    } else {
        SetFeedback("Press ENTER to establish the Freegrid link.");
    }

    LARGE_INTEGER frequency = {};
    LARGE_INTEGER previous = {};
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&previous);

    MSG message = {};
    bool running = true;
    int exitCode = 0;
    while (running) {
        while (PeekMessage(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT) {
                exitCode = static_cast<int>(message.wParam);
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
    return exitCode;
}
