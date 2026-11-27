# Encore — a small change to JUCE's Windows window code: touch messages go to
# Encore before JUCE handles them (the two-finger right click, decision 0063).
#
# Encore used to hook its window from outside (SetWindowSubclass) for this, and
# Chrome's Safe Browsing flagged that build as dangerous (20 Nov 2026). Inside
# JUCE's own window procedure it's no new kind of code. The edit is made on the
# fetched JUCE source each configure and only once (a marker line), so a cached
# or reused copy is fine. A local JUCE checkout (ENCORE_JUCE_DIR) is never
# edited. When the code doesn't match (another JUCE version), it warns and the
# app builds without the two-finger tap.
#
# Sets ENCORE_JUCE_TOUCH_FILTER to ON when the patched code is in place.

set(ENCORE_JUCE_TOUCH_FILTER OFF)
set(_file "${juce_SOURCE_DIR}/modules/juce_gui_basics/native/juce_Windowing_windows.cpp")
set(_marker "// Encore patch (cmake/PatchJuceTouch.cmake)")

if(NOT EXISTS "${_file}")
    message(WARNING "Encore: ${_file} not found; no two-finger right click")
    return()
endif()

file(READ "${_file}" _text)
string(FIND "${_text}" "${_marker}" _patched)

if(_patched GREATER -1)
    set(ENCORE_JUCE_TOUCH_FILTER ON)
    return()
endif()

if(ENCORE_JUCE_DIR)
    message(STATUS "Encore: local JUCE checkout, left unedited; no two-finger right click")
    return()
endif()

set(_ok TRUE)

function(_encore_replace before after)
    string(FIND "${_text}" "${before}" _at)

    if(_at EQUAL -1)
        set(_ok FALSE PARENT_SCOPE)
    else()
        string(REPLACE "${before}" "${after}" _new "${_text}")
        set(_text "${_new}" PARENT_SCOPE)
    endif()
endfunction()

# 1. The filter, at namespace scope before the window class.
_encore_replace("class HWNDComponentPeer final : public ComponentPeer\n"
"${_marker}: touch messages go to Encore first.
using WindowsTouchFilter = bool (*) (Component& window, int phase, unsigned int pointerId, Point<float> physicalScreenPosition);
WindowsTouchFilter windowsTouchFilter = nullptr;

class HWNDComponentPeer final : public ComponentPeer\n")

# 2. A member that asks it, for touch pointers only.
_encore_replace("    bool handlePointerInput (WPARAM wParam, LPARAM lParam, const bool isDown, const bool isUp)\n"
"    // Encore patch: phase 0 down, 1 move, 2 up; true = Encore took the message.
    bool encoreTouchFilter (int phase, WPARAM wParam, LPARAM lParam)
    {
        if (windowsTouchFilter == nullptr || getPointerType (wParam) != MouseInputSource::InputSourceType::touch)
            return false;

        return windowsTouchFilter (component, phase, (unsigned int) GET_POINTERID_WPARAM (wParam),
                                   { (float) GET_X_LPARAM (lParam), (float) GET_Y_LPARAM (lParam) });
    }

    bool handlePointerInput (WPARAM wParam, LPARAM lParam, const bool isDown, const bool isUp)\n")

# 3. Asked first for each pointer message.
foreach(_case IN ITEMS "UPDATE;false, false;1" "DOWN;true, false;0" "UP;false, true;2")
    list(GET _case 0 _name)
    list(GET _case 1 _args)
    list(GET _case 2 _phase)
    _encore_replace("            case WM_POINTER${_name}:\n                if (canUseMultiTouch() && handlePointerInput (wParam, lParam, ${_args}))"
"            case WM_POINTER${_name}:\n                if (encoreTouchFilter (${_phase}, wParam, lParam))   // Encore patch\n                    return 0;\n\n                if (canUseMultiTouch() && handlePointerInput (wParam, lParam, ${_args}))")
endforeach()

if(NOT _ok)
    message(WARNING "Encore: JUCE's window code isn't what cmake/PatchJuceTouch.cmake expects "
                    "(JUCE ${ENCORE_JUCE_VERSION}); building without the two-finger right click")
    return()
endif()

file(WRITE "${_file}" "${_text}")
set(ENCORE_JUCE_TOUCH_FILTER ON)
message(STATUS "Encore: JUCE patched for the two-finger right click")
