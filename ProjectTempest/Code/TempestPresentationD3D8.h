/*
** Project Tempest Direct3D 8 presentation pass
** Copyright 2026 Project Tempest contributors
** GPL-3.0-or-later
*/

#pragma once

#include <windows.h>

#include "dx8wrapper.h"
#include "TempestAccessibility.h"

struct ID3DXFont;

namespace Tempest::Presentation {

enum class FontStyle {
    Small,
    Body,
    Title,
};

class Renderer final : public DX8_CleanupHook {
public:
    bool Initialise(int width, int height);
    void Shutdown();
    void Resize(int width, int height);

    bool BindCombinedTarget(int width, int height);
    void RestoreBackBuffer();
    bool CompositeAndPresent(const Accessibility::Settings &settings);

    bool EnsureFonts(float scale);
    void DrawSolidRect(const RECT &rect, D3DCOLOR colour);
    void DrawPanel(const RECT &rect, D3DCOLOR fill, D3DCOLOR border);
    void DrawLabel(
        FontStyle style,
        D3DCOLOR colour,
        const RECT &rect,
        const char *text,
        UINT flags = DT_LEFT | DT_TOP | DT_SINGLELINE);

    void ReleaseResources() override;
    void ReAcquireResources() override;

private:
    bool EnsureRenderTarget();
    bool EnsureShader();
    void ReleaseRenderTarget();
    void ReleaseShader();
    void ReleaseFonts();
    ID3DXFont *FontForStyle(FontStyle style) const;

    bool m_initialised = false;
    bool m_targetBound = false;
    int m_width = 0;
    int m_height = 0;
    int m_textureWidth = 0;
    int m_textureHeight = 0;
    int m_fontScale = 0;
    IDirect3DTexture8 *m_renderTarget = nullptr;
    IDirect3DSurface8 *m_depthTarget = nullptr;
    DWORD m_shader = 0;
    HFONT m_smallFontHandle = nullptr;
    HFONT m_bodyFontHandle = nullptr;
    HFONT m_titleFontHandle = nullptr;
    ID3DXFont *m_smallFont = nullptr;
    ID3DXFont *m_bodyFont = nullptr;
    ID3DXFont *m_titleFont = nullptr;
};

} // namespace Tempest::Presentation
