#include <juce_gui_basics/juce_gui_basics.h>
#include "ScreenCapture.h"
#include "UiSnapshot.h"
namespace encore::test {
class LayerDiagTests final : public juce::UnitTest
{
public:
    LayerDiagTests() : juce::UnitTest ("Layer diag", "Encore") {}
    struct Shade final : juce::Component
    {
        Shade() { setInterceptsMouseClicks (false, false); }
        void paint (juce::Graphics& g) override { g.fillAll (juce::Colour (0x80000000)); ++paints; }
        int paints = 0;
    };
    void runTest() override
    {
        beginTest ("which layered windows show");
        if ((juce::SystemStats::getOperatingSystemType() & juce::SystemStats::Windows) == 0) return;
        auto& mm = *juce::MessageManager::getInstance();
        struct Backdrop final : juce::Component { Backdrop() { setOpaque (true); } void paint (juce::Graphics& g) override { g.fillAll (juce::Colour (0xFFFF00FF)); } } backdrop;
        backdrop.setBounds (100, 100, 800, 500); backdrop.addToDesktop (0); backdrop.setVisible (true); backdrop.toFront (true);
        mm.runDispatchLoopUntil (300);
        const auto flags = juce::ComponentPeer::windowIgnoresMouseClicks | juce::ComponentPeer::windowIsTemporary | juce::ComponentPeer::windowIgnoresKeyPresses;
        Shade a, b, c, d, e;
        // a: visible first, no association
        a.setBounds (150, 150, 60, 60); a.setVisible (true); a.addToDesktop (flags);
        // b: hidden first, shown later
        b.setBounds (250, 150, 60, 60); b.addToDesktop (flags); b.setVisible (true);
        // c: visible first, associated with backdrop
        c.setVisible (true); c.setSize (1, 1); c.addToDesktop (juce::DesktopWindowOptions().withStyleFlags (flags).withAssociatedComponent (&backdrop)); c.setBounds (350, 150, 60, 60);
        // d: like c, always on top
        d.setVisible (true); d.setSize (1, 1); d.setAlwaysOnTop (true); d.addToDesktop (juce::DesktopWindowOptions().withStyleFlags (flags).withAssociatedComponent (&backdrop)); d.setBounds (450, 150, 60, 60);
        // e: like a, toBehind backdrop? no: plain, setOpaque false explicitly, without temporary flag
        e.setOpaque (false); e.setBounds (550, 150, 60, 60); e.setVisible (true); e.addToDesktop (juce::ComponentPeer::windowIgnoresMouseClicks);
        mm.runDispatchLoopUntil (800);
        juce::String r;
        for (auto* s : { &a, &b, &c, &d, &e })
        {
            const auto p = juce::Desktop::getInstance().getDisplays().logicalToPhysical (s->getScreenBounds().getCentre());
            std::string err;
            const auto px = screen::capture (p.x, p.y, 1, 1, err);
            r << juce::String::toHexString ((int) (px.empty() ? 0 : px[0])) << " paints " << s->paints << " [" << screen::describeWindow (s->getWindowHandle()) << "] ";
        }
        expect (false, "LAYERDIAG " + r);
        for (auto* s : { &a, &b, &c, &d, &e }) s->removeFromDesktop();
        backdrop.removeFromDesktop();
    }
};
static LayerDiagTests layerDiagTests;
}
