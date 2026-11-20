// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "TouchInjection.h"

#if defined (_WIN32)
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #if ! defined (_WIN32_WINNT) || _WIN32_WINNT < 0x0602
  #undef _WIN32_WINNT
  #define _WIN32_WINNT 0x0A00
 #endif
 #include <windows.h>
#endif

namespace encore::test::touch
{

#if defined (_WIN32)

bool initialise (int maxContacts, std::string& error)
{
    if (InitializeTouchInjection ((UINT32) maxContacts, TOUCH_FEEDBACK_NONE))
        return true;

    error = "InitializeTouchInjection failed, error " + std::to_string (GetLastError());
    return false;
}

bool inject (const std::vector<Contact>& frame, std::string& error)
{
    std::vector<POINTER_TOUCH_INFO> infos (frame.size());

    for (size_t i = 0; i < frame.size(); ++i)
    {
        const auto& c = frame[i];
        auto& t = infos[i];
        ZeroMemory (&t, sizeof (t));
        t.pointerInfo.pointerType = PT_TOUCH;
        t.pointerInfo.pointerId = (UINT32) c.id;
        t.pointerInfo.ptPixelLocation = { c.x, c.y };
        t.pointerInfo.pointerFlags = c.phase == Contact::Phase::down ? (POINTER_FLAG_DOWN | POINTER_FLAG_INRANGE | POINTER_FLAG_INCONTACT)
                                   : c.phase == Contact::Phase::move ? (POINTER_FLAG_UPDATE | POINTER_FLAG_INRANGE | POINTER_FLAG_INCONTACT)
                                                                     : POINTER_FLAG_UP;
        t.touchFlags = TOUCH_FLAG_NONE;
        t.touchMask = TOUCH_MASK_CONTACTAREA | TOUCH_MASK_ORIENTATION | TOUCH_MASK_PRESSURE;
        t.rcContact = { c.x - 2, c.y - 2, c.x + 2, c.y + 2 };
        t.orientation = 90;
        t.pressure = 32000;
    }

    if (InjectTouchInput ((UINT32) infos.size(), infos.data()))
        return true;

    error = "InjectTouchInput failed, error " + std::to_string (GetLastError());
    return false;
}

#else

bool initialise (int, std::string& error)                 { error = "touch injection is Windows only"; return false; }
bool inject (const std::vector<Contact>&, std::string& error) { error = "touch injection is Windows only"; return false; }

#endif

} // namespace encore::test::touch
