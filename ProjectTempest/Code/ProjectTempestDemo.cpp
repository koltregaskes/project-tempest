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
#include <limits>
#include <vector>

#include "assetmgr.h"
#include "camera.h"
#include "light.h"
#include "line3d.h"
#include "matrix3d.h"
#include "rendobj.h"
#include "scene.h"
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
bool g_rendererReady = false;
std::uint32_t g_selectedUnitId = 0;
Tempest::Point g_pointerPoint;
double g_simulationAccumulator = 0.0;

WW3DAssetManager *g_assetManager = nullptr;
SimpleSceneClass *g_scene = nullptr;
CameraClass *g_camera = nullptr;
LightClass *g_keyLight = nullptr;

Tempest::Simulation g_simulation;

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
    Tempest::Command command;
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
    RECT client = {};
    GetClientRect(ApplicationHWnd, &client);
    const float width = static_cast<float>(std::max(1L, client.right - client.left));
    const float height = static_cast<float>(std::max(1L, client.bottom - client.top));
    const float worldX = std::clamp(
        ((static_cast<float>(mouseX) / width) - 0.5F) * (kArenaExtent * 2.0F),
        -kArenaExtent,
        kArenaExtent);
    const float worldY = std::clamp(
        (0.5F - (static_cast<float>(mouseY) / height)) * (kArenaExtent * 2.0F),
        -kArenaExtent,
        kArenaExtent);
    return {
        static_cast<std::int32_t>(worldX / kSimulationToWorld),
        static_cast<std::int32_t>(worldY / kSimulationToWorld),
    };
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
    const Tempest::MatchState &match = g_simulation.GetState();
    const Tempest::Unit *selected = FindUnit(g_selectedUnitId);
    const int capturedNodes = static_cast<int>(std::count_if(
        match.nodes.begin(), match.nodes.end(), [](const Tempest::ControlNode &node) {
            return node.owner == Tempest::Faction::Freegrid;
        }));
    const char *outcome = match.outcome == Tempest::MatchOutcome::Victory
        ? "VICTORY"
        : (match.outcome == Tempest::MatchOutcome::Defeat ? "DEFEAT" : (match.paused ? "PAUSED" : "ACTIVE"));
    const int selectedHitPoints = selected ? selected->hitPoints : 0;
    const int selectedMaximumHitPoints = selected ? selected->maximumHitPoints : 0;
    char title[512];
    std::snprintf(
        title,
        sizeof(title),
        "Project Tempest: Substation 9 | %s T%llu | Credits %d  Power %d  Nodes %d/3 | Selected #%u HP %d/%d | LMB select  RMB order  B relay  U courier  F pulse  Space pause  R restart",
        outcome,
        static_cast<unsigned long long>(match.tick),
        match.freegridCredits,
        match.freegridPower,
        capturedNodes,
        g_selectedUnitId,
        selectedHitPoints,
        selectedMaximumHitPoints);
    SetWindowTextA(ApplicationHWnd, title);
}

void ResetPrototype()
{
    g_simulation.Reset();
    g_simulationAccumulator = 0.0;
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
    } else {
        g_simulation.Submit(MakeCommand(Tempest::CommandKind::Move, selected->id, 0, g_pointerPoint));
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
    }
}

void ProduceCourier()
{
    for (const Tempest::Building &building : g_simulation.GetState().buildings) {
        if (building.hitPoints > 0 && building.complete && building.faction == Tempest::Faction::Freegrid) {
            g_simulation.Submit(MakeCommand(Tempest::CommandKind::ProduceCourier, building.id));
            return;
        }
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
            if (wParam == VK_ESCAPE) {
                DestroyWindow(window);
            } else if (wParam == 'R') {
                ResetPrototype();
            } else if (wParam == VK_SPACE) {
                g_simulation.Submit(MakeCommand(Tempest::CommandKind::TogglePause));
            } else if (wParam == 'B') {
                BuildRelayAtNearestOwnedNode();
            } else if (wParam == 'U') {
                ProduceCourier();
            } else if (wParam == 'F' && g_selectedUnitId != 0) {
                g_simulation.Submit(MakeCommand(
                    Tempest::CommandKind::ArcPulse,
                    g_selectedUnitId,
                    0,
                    g_pointerPoint));
            }
            return 0;

        case WM_KEYUP:
            if (wParam < 256) {
                g_keys[wParam] = false;
            }
            return 0;

        case WM_LBUTTONDOWN:
            SelectFromScreen(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            return 0;

        case WM_RBUTTONDOWN:
            IssueContextOrder(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            return 0;

        case WM_MOUSEMOVE:
            g_pointerPoint = ScreenToSimulationPoint(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
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

Line3DClass *AddLine(const Vector3 &start, const Vector3 &end, float width, const Vector3 &color)
{
    Line3DClass *line = new Line3DClass(start, end, width, color.X, color.Y, color.Z, 1.0F);
    g_scene->Add_Render_Object(line);
    return line;
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

void UpdateCrossVisual(CrossVisual &visual, Tempest::Point point, float extent, const Vector3 &color)
{
    const float x = static_cast<float>(point.x) * kSimulationToWorld;
    const float y = static_cast<float>(point.y) * kSimulationToWorld;
    visual.horizontal->Reset(Vector3(x - extent, y, 0.06F), Vector3(x + extent, y, 0.06F));
    visual.vertical->Reset(Vector3(x, y - extent, 0.06F), Vector3(x, y + extent, 0.06F));
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
        UpdateCrossVisual(*visual, node.position, 1.4F, color);
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
        UpdateCrossVisual(*visual, building.position, extent, color);
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
    Matrix3D cameraTransform;
    cameraTransform.Look_At(
        Vector3(0.0F, -38.0F, 31.0F),
        Vector3(0.0F, 0.0F, 0.0F),
        0.0F);
    g_camera->Set_Transform(cameraTransform);
    g_camera->Set_View_Plane(Vector2(-1.0F, -0.5625F), Vector2(1.0F, 0.5625F));
    g_camera->Set_Clip_Planes(1.0F, 250.0F);

    CreateArenaGrid();
    ResetPrototype();
    SyncMarkerVisuals();
    return SyncBuildingModelVisuals() && SyncUnitVisuals();
}

void UpdatePrototype(float deltaSeconds)
{
    g_simulationAccumulator = std::min(0.25, g_simulationAccumulator + static_cast<double>(deltaSeconds));
    while (g_simulationAccumulator >= kSimulationTickSeconds) {
        const Tempest::Unit *selected = FindUnit(g_selectedUnitId);
        if (selected && !g_simulation.GetState().paused &&
            g_simulation.GetState().outcome == Tempest::MatchOutcome::InProgress) {
            std::int32_t inputX = 0;
            std::int32_t inputY = 0;
            inputX += (g_keys['D'] || g_keys[VK_RIGHT]) ? 1 : 0;
            inputX -= (g_keys['A'] || g_keys[VK_LEFT]) ? 1 : 0;
            inputY += (g_keys['W'] || g_keys[VK_UP]) ? 1 : 0;
            inputY -= (g_keys['S'] || g_keys[VK_DOWN]) ? 1 : 0;
            if (inputX != 0 || inputY != 0) {
                const std::int32_t step = inputX != 0 && inputY != 0 ? 640 : 900;
                const Tempest::Point target {
                    std::clamp(selected->position.x + (inputX * step), -18000, 18000),
                    std::clamp(selected->position.y + (inputY * step), -18000, 18000),
                };
                g_simulation.Submit(MakeCommand(Tempest::CommandKind::Move, selected->id, 0, target));
            }
        }
        g_simulation.Step();
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
            "The renderer or a required Project Tempest W3D could not be initialised. Verify the demo package.",
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
