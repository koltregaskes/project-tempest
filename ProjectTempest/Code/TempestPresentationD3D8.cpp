/*
** Project Tempest Direct3D 8 presentation pass
** Copyright 2026 Project Tempest contributors
** GPL-3.0-or-later
*/

#include "TempestPresentationD3D8.h"
#include "TempestPresentationShader.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include <d3dx8core.h>

#include "ww3d.h"

namespace Tempest::Presentation {
namespace {

struct ColourVertex {
    float x;
    float y;
    float z;
    float rhw;
    D3DCOLOR colour;
};

struct TextureVertex {
    float x;
    float y;
    float z;
    float rhw;
    float u;
    float v;
};

constexpr DWORD ColourVertexFormat = D3DFVF_XYZRHW | D3DFVF_DIFFUSE;
constexpr DWORD TextureVertexFormat = D3DFVF_XYZRHW | D3DFVF_TEX1;

int ScalePixels(float value, float scale)
{
    return std::max(1, static_cast<int>(std::lround(value * scale)));
}

int NextPowerOfTwo(int value)
{
    int result = 1;
    while (result < value && result <= (1 << 14)) {
        result <<= 1;
    }
    return result;
}

void ReleaseFont(ID3DXFont *&font, HFONT &handle)
{
    if (font) {
        font->Release();
        font = nullptr;
    }
    if (handle) {
        DeleteObject(handle);
        handle = nullptr;
    }
}

} // namespace

bool Renderer::Initialise(int width, int height)
{
    if (m_initialised) {
        return true;
    }
    m_width = std::max(1, width);
    m_height = std::max(1, height);
    m_initialised = true;
    DX8Wrapper::SetCleanupHook(this);
    if (!EnsureRenderTarget() || !EnsureShader()) {
        Shutdown();
        return false;
    }
    return true;
}

void Renderer::Shutdown()
{
    if (m_targetBound) {
        RestoreBackBuffer();
    }
    if (m_initialised) {
        DX8Wrapper::SetCleanupHook(nullptr);
    }
    ReleaseResources();
    m_initialised = false;
    m_width = 0;
    m_height = 0;
    m_fontScale = 0;
}

void Renderer::Resize(int width, int height)
{
    width = std::max(1, width);
    height = std::max(1, height);
    if (width == m_width && height == m_height) {
        return;
    }
    if (m_targetBound) {
        RestoreBackBuffer();
    }
    m_width = width;
    m_height = height;
    ReleaseRenderTarget();
}

bool Renderer::BindCombinedTarget(int width, int height)
{
    Resize(width, height);
    if (!m_initialised || !EnsureRenderTarget() || !EnsureShader()) {
        return false;
    }
    IDirect3DSurface8 *surface = nullptr;
    if (FAILED(m_renderTarget->GetSurfaceLevel(0, &surface)) || !surface) {
        return false;
    }
    DX8Wrapper::Set_Render_Target(surface, m_depthTarget);
    surface->Release();
    D3DVIEWPORT8 viewport { 0, 0, static_cast<DWORD>(m_width), static_cast<DWORD>(m_height), 0.0F, 1.0F };
    DX8Wrapper::Set_Viewport(&viewport);
    m_targetBound = true;
    return true;
}

void Renderer::RestoreBackBuffer()
{
    if (m_targetBound) {
        DX8Wrapper::Set_Render_Target(static_cast<IDirect3DSurface8 *>(nullptr));
        m_targetBound = false;
    }
}

bool Renderer::CompositeAndPresent(const Accessibility::Settings &settings)
{
    if (!m_initialised || !m_targetBound || !m_renderTarget || !EnsureShader()) {
        RestoreBackBuffer();
        return false;
    }
    RestoreBackBuffer();
    if (WW3D::Begin_Render(true, false, Vector3(0.0F, 0.0F, 0.0F)) != WW3D_ERROR_OK) {
        return false;
    }

    const Accessibility::PresentationParameters parameters =
        Accessibility::BuildPresentationParameters(settings);
    const float constants[7][4] = {
        { parameters.preparationScale, parameters.preparationScale, parameters.preparationScale, 1.0F },
        { parameters.preparationOffset, parameters.preparationOffset, parameters.preparationOffset, 0.0F },
        { parameters.greenTransform.red, parameters.greenTransform.green, parameters.greenTransform.blue, 0.0F },
        { parameters.blueTransform.red, parameters.blueTransform.green, parameters.blueTransform.blue, 0.0F },
        { parameters.strength, parameters.strength, parameters.strength, parameters.strength },
        { parameters.outputScale, parameters.outputScale, parameters.outputScale, 1.0F },
        { parameters.outputOffset, parameters.outputOffset, parameters.outputOffset, 0.0F },
    };

    IDirect3DDevice8 *device = DX8Wrapper::_Get_D3D_Device8();
    D3DVIEWPORT8 viewport { 0, 0, static_cast<DWORD>(m_width), static_cast<DWORD>(m_height), 0.0F, 1.0F };
    DX8Wrapper::Set_Viewport(&viewport);
    device->SetRenderState(D3DRS_ZENABLE, FALSE);
    device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    device->SetRenderState(D3DRS_LIGHTING, FALSE);
    device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    device->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    device->SetTextureStageState(0, D3DTSS_ADDRESSU, D3DTADDRESS_CLAMP);
    device->SetTextureStageState(0, D3DTSS_ADDRESSV, D3DTADDRESS_CLAMP);
    device->SetTextureStageState(0, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);
    device->SetTextureStageState(0, D3DTSS_MINFILTER, D3DTEXF_LINEAR);
    device->SetTextureStageState(0, D3DTSS_MIPFILTER, D3DTEXF_NONE);
    device->SetTexture(0, m_renderTarget);
    device->SetVertexShader(TextureVertexFormat);
    device->SetPixelShader(m_shader);
    device->SetPixelShaderConstant(0, constants, 7);

    const float right = static_cast<float>(m_width) - 0.5F;
    const float bottom = static_cast<float>(m_height) - 0.5F;
    const float maximumU = static_cast<float>(m_width) / static_cast<float>(m_textureWidth);
    const float maximumV = static_cast<float>(m_height) / static_cast<float>(m_textureHeight);
    const TextureVertex vertices[4] = {
        { right, bottom, 0.0F, 1.0F, maximumU, maximumV },
        { right, -0.5F, 0.0F, 1.0F, maximumU, 0.0F },
        { -0.5F, bottom, 0.0F, 1.0F, 0.0F, maximumV },
        { -0.5F, -0.5F, 0.0F, 1.0F, 0.0F, 0.0F },
    };
    const HRESULT drawResult = device->DrawPrimitiveUP(
        D3DPT_TRIANGLESTRIP,
        2,
        vertices,
        sizeof(TextureVertex));

    device->SetPixelShader(0);
    device->SetTexture(0, nullptr);
    DX8Wrapper::Invalidate_Cached_Render_States();
    WW3D::End_Render(true);
    return SUCCEEDED(drawResult);
}

bool Renderer::EnsureFonts(float scale)
{
    const int scaleKey = static_cast<int>(std::lround(scale * 100.0F));
    if (scaleKey == m_fontScale && m_smallFont && m_bodyFont && m_titleFont) {
        return true;
    }

    ReleaseFonts();
    m_smallFontHandle = CreateFontA(
        -ScalePixels(13.0F, scale), 0, 0, 0, FW_MEDIUM, FALSE, FALSE, FALSE,
        ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
        DEFAULT_PITCH | FF_SWISS, "Segoe UI");
    m_bodyFontHandle = CreateFontA(
        -ScalePixels(17.0F, scale), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
        DEFAULT_PITCH | FF_SWISS, "Segoe UI");
    m_titleFontHandle = CreateFontA(
        -ScalePixels(28.0F, scale), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
        DEFAULT_PITCH | FF_SWISS, "Segoe UI");
    if (!m_smallFontHandle || !m_bodyFontHandle || !m_titleFontHandle) {
        ReleaseFonts();
        return false;
    }

    IDirect3DDevice8 *device = DX8Wrapper::_Get_D3D_Device8();
    if (FAILED(D3DXCreateFont(device, m_smallFontHandle, &m_smallFont)) ||
        FAILED(D3DXCreateFont(device, m_bodyFontHandle, &m_bodyFont)) ||
        FAILED(D3DXCreateFont(device, m_titleFontHandle, &m_titleFont))) {
        ReleaseFonts();
        return false;
    }
    m_fontScale = scaleKey;
    return true;
}

void Renderer::DrawSolidRect(const RECT &rect, D3DCOLOR colour)
{
    if (rect.right <= rect.left || rect.bottom <= rect.top) {
        return;
    }
    IDirect3DDevice8 *device = DX8Wrapper::_Get_D3D_Device8();
    device->SetRenderState(D3DRS_ZENABLE, FALSE);
    device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    device->SetRenderState(D3DRS_LIGHTING, FALSE);
    device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    device->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    device->SetTexture(0, nullptr);
    device->SetPixelShader(0);
    device->SetVertexShader(ColourVertexFormat);
    const float left = static_cast<float>(rect.left) - 0.5F;
    const float top = static_cast<float>(rect.top) - 0.5F;
    const float right = static_cast<float>(rect.right) - 0.5F;
    const float bottom = static_cast<float>(rect.bottom) - 0.5F;
    const ColourVertex vertices[4] = {
        { right, bottom, 0.0F, 1.0F, colour },
        { right, top, 0.0F, 1.0F, colour },
        { left, bottom, 0.0F, 1.0F, colour },
        { left, top, 0.0F, 1.0F, colour },
    };
    device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, vertices, sizeof(ColourVertex));
    DX8Wrapper::Invalidate_Cached_Render_States();
}

void Renderer::DrawPanel(const RECT &rect, D3DCOLOR fill, D3DCOLOR border)
{
    DrawSolidRect(rect, fill);
    DrawSolidRect({ rect.left, rect.top, rect.right, rect.top + 1 }, border);
    DrawSolidRect({ rect.left, rect.bottom - 1, rect.right, rect.bottom }, border);
    DrawSolidRect({ rect.left, rect.top + 1, rect.left + 1, rect.bottom - 1 }, border);
    DrawSolidRect({ rect.right - 1, rect.top + 1, rect.right, rect.bottom - 1 }, border);
}

void Renderer::DrawLabel(
    FontStyle style,
    D3DCOLOR colour,
    const RECT &rect,
    const char *text,
    UINT flags)
{
    ID3DXFont *font = FontForStyle(style);
    if (!font || !text || !*text || FAILED(font->Begin())) {
        return;
    }
    RECT drawRect = rect;
    font->DrawTextA(text, -1, &drawRect, flags | DT_NOPREFIX, colour);
    font->End();
    DX8Wrapper::Invalidate_Cached_Render_States();
}

void Renderer::ReleaseResources()
{
    RestoreBackBuffer();
    ReleaseFonts();
    ReleaseShader();
    ReleaseRenderTarget();
}

void Renderer::ReAcquireResources()
{
    if (!m_initialised) {
        return;
    }
    EnsureRenderTarget();
    EnsureShader();
}

bool Renderer::EnsureRenderTarget()
{
    if (m_renderTarget) {
        return true;
    }
    IDirect3DDevice8 *device = DX8Wrapper::_Get_D3D_Device8();
    IDirect3DSurface8 *backBuffer = nullptr;
    if (FAILED(device->GetBackBuffer(0, D3DBACKBUFFER_TYPE_MONO, &backBuffer)) || !backBuffer) {
        return false;
    }
    D3DSURFACE_DESC description = {};
    const HRESULT descriptionResult = backBuffer->GetDesc(&description);
    backBuffer->Release();
    if (FAILED(descriptionResult)) {
        return false;
    }

    m_textureWidth = m_width;
    m_textureHeight = m_height;
    HRESULT textureResult = device->CreateTexture(
        m_textureWidth,
        m_textureHeight,
        1,
        D3DUSAGE_RENDERTARGET,
        description.Format,
        D3DPOOL_DEFAULT,
        &m_renderTarget);
    if (FAILED(textureResult)) {
        m_textureWidth = NextPowerOfTwo(m_width);
        m_textureHeight = NextPowerOfTwo(m_height);
        textureResult = device->CreateTexture(
            m_textureWidth,
            m_textureHeight,
            1,
            D3DUSAGE_RENDERTARGET,
            description.Format,
            D3DPOOL_DEFAULT,
            &m_renderTarget);
    }
    if (FAILED(textureResult) || !m_renderTarget) {
        ReleaseRenderTarget();
        return false;
    }

    D3DFORMAT defaultDepthFormat = D3DFMT_UNKNOWN;
    IDirect3DSurface8 *defaultDepth = nullptr;
    if (SUCCEEDED(device->GetDepthStencilSurface(&defaultDepth)) && defaultDepth) {
        D3DSURFACE_DESC depthDescription = {};
        if (SUCCEEDED(defaultDepth->GetDesc(&depthDescription))) {
            defaultDepthFormat = depthDescription.Format;
        }
        defaultDepth->Release();
    }
    const D3DFORMAT depthFormats[] = {
        defaultDepthFormat,
        D3DFMT_D24S8,
        D3DFMT_D24X8,
        D3DFMT_D16,
    };
    IDirect3DSurface8 *renderSurface = nullptr;
    IDirect3DSurface8 *restoreRenderSurface = nullptr;
    IDirect3DSurface8 *restoreDepthSurface = nullptr;
    if (FAILED(m_renderTarget->GetSurfaceLevel(0, &renderSurface)) || !renderSurface ||
        FAILED(device->GetRenderTarget(&restoreRenderSurface)) || !restoreRenderSurface ||
        FAILED(device->GetDepthStencilSurface(&restoreDepthSurface)) || !restoreDepthSurface) {
        if (renderSurface) {
            renderSurface->Release();
        }
        if (restoreRenderSurface) {
            restoreRenderSurface->Release();
        }
        if (restoreDepthSurface) {
            restoreDepthSurface->Release();
        }
        ReleaseRenderTarget();
        return false;
    }
    for (const D3DFORMAT format : depthFormats) {
        if (format == D3DFMT_UNKNOWN) {
            continue;
        }
        IDirect3DSurface8 *candidateDepth = nullptr;
        if (SUCCEEDED(device->CreateDepthStencilSurface(
                m_textureWidth,
                m_textureHeight,
                format,
                D3DMULTISAMPLE_NONE,
                &candidateDepth))) {
            const HRESULT bindResult = device->SetRenderTarget(renderSurface, candidateDepth);
            const HRESULT restoreResult = device->SetRenderTarget(restoreRenderSurface, restoreDepthSurface);
            if (SUCCEEDED(bindResult) && SUCCEEDED(restoreResult)) {
                m_depthTarget = candidateDepth;
                break;
            }
            candidateDepth->Release();
        }
    }
    renderSurface->Release();
    restoreRenderSurface->Release();
    restoreDepthSurface->Release();
    DX8Wrapper::Invalidate_Cached_Render_States();
    if (!m_depthTarget) {
        ReleaseRenderTarget();
        return false;
    }
    return true;
}

bool Renderer::EnsureShader()
{
    if (m_shader != 0) {
        return true;
    }
    D3DCAPS8 caps = {};
    if (FAILED(DX8Wrapper::_Get_D3D_Device8()->GetDeviceCaps(&caps)) ||
        caps.PixelShaderVersion < D3DPS_VERSION(1, 4)) {
        OutputDebugStringA("Project Tempest requires pixel shader 1.4 for the exact accessibility presentation pass.\n");
        return false;
    }
    ID3DXBuffer *compiledShader = nullptr;
    ID3DXBuffer *errors = nullptr;
    const HRESULT assembleResult = D3DXAssembleShader(
        ShaderSource,
        std::strlen(ShaderSource),
        0,
        nullptr,
        &compiledShader,
        &errors);
    if (errors) {
        OutputDebugStringA(static_cast<const char *>(errors->GetBufferPointer()));
        errors->Release();
    }
    if (FAILED(assembleResult) || !compiledShader) {
        return false;
    }
    const HRESULT createResult = DX8Wrapper::_Get_D3D_Device8()->CreatePixelShader(
        static_cast<const DWORD *>(compiledShader->GetBufferPointer()),
        &m_shader);
    compiledShader->Release();
    return SUCCEEDED(createResult);
}

void Renderer::ReleaseRenderTarget()
{
    if (m_renderTarget) {
        m_renderTarget->Release();
        m_renderTarget = nullptr;
    }
    if (m_depthTarget) {
        m_depthTarget->Release();
        m_depthTarget = nullptr;
    }
    m_textureWidth = 0;
    m_textureHeight = 0;
}

void Renderer::ReleaseShader()
{
    if (m_shader != 0 && DX8Wrapper::_Get_D3D_Device8()) {
        DX8Wrapper::_Get_D3D_Device8()->DeletePixelShader(m_shader);
    }
    m_shader = 0;
}

void Renderer::ReleaseFonts()
{
    ReleaseFont(m_smallFont, m_smallFontHandle);
    ReleaseFont(m_bodyFont, m_bodyFontHandle);
    ReleaseFont(m_titleFont, m_titleFontHandle);
    m_fontScale = 0;
}

ID3DXFont *Renderer::FontForStyle(FontStyle style) const
{
    switch (style) {
        case FontStyle::Small: return m_smallFont;
        case FontStyle::Body: return m_bodyFont;
        case FontStyle::Title: return m_titleFont;
    }
    return m_bodyFont;
}

} // namespace Tempest::Presentation
