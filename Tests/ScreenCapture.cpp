// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "ScreenCapture.h"

#if defined (_WIN32)
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #include <windows.h>
#endif

namespace encore::test::screen
{

#if defined (_WIN32)

std::vector<std::uint32_t> capture (int x, int y, int width, int height, std::string& error)
{
    std::vector<std::uint32_t> pixels;

    if (width <= 0 || height <= 0)
    {
        error = "empty area";
        return pixels;
    }

    HDC screen = GetDC (nullptr);
    HDC memory = CreateCompatibleDC (screen);
    HBITMAP bitmap = CreateCompatibleBitmap (screen, width, height);
    HGDIOBJ old = SelectObject (memory, bitmap);

    // CAPTUREBLT: include layered (semi-transparent) windows such as menus.
    if (BitBlt (memory, 0, 0, width, height, screen, x, y, SRCCOPY | CAPTUREBLT))
    {
        BITMAPINFO info {};
        info.bmiHeader.biSize = sizeof (info.bmiHeader);
        info.bmiHeader.biWidth = width;
        info.bmiHeader.biHeight = -height;   // top-down
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;

        pixels.resize ((size_t) width * (size_t) height);
        SelectObject (memory, old);

        if (GetDIBits (memory, bitmap, 0, (UINT) height, pixels.data(), &info, DIB_RGB_COLORS) != height)
        {
            pixels.clear();
            error = "GetDIBits failed, error " + std::to_string (GetLastError());
        }

        old = nullptr;
    }
    else
    {
        error = "BitBlt failed, error " + std::to_string (GetLastError());
    }

    if (old != nullptr)
        SelectObject (memory, old);

    DeleteObject (bitmap);
    DeleteDC (memory);
    ReleaseDC (nullptr, screen);

    for (auto& p : pixels)
        p |= 0xFF000000u;   // the screen is opaque

    return pixels;
}

#else

std::vector<std::uint32_t> capture (int, int, int, int, std::string& error)
{
    error = "screen capture is Windows only";
    return {};
}

#endif

} // namespace encore::test::screen
