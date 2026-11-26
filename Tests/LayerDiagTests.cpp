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
        using P = juce::ComponentPeer;
        const int variants[] { P::windowIsTemporary, P::windowIgnoresKeyPresses, P::windowIgnoresMouseClicks, 0,
                               P::windowIsTemporary | P::windowIgnoresKeyPresses, P::windowIgnoresMouseClicks | P::windowIgnoresKeyPresses,
                               flags, flags };
        juce::OwnedArray<Shade> shades;
        for (int i = 0; i < 8; ++i)
        {
            auto* s = shades.add (new Shade());
            s->setBounds (130 + i * 90, 150, 60, 60);
            s->setVisible (true);
            s->addToDesktop (variants[i]);
            if (i == 7)
                if (auto* peer = s->getPeer())
                    peer->setCurrentRenderingEngine (0);   // GDI
        }
        mm.runDispatchLoopUntil (800);
        juce::String r;
        for (auto* s : shades)
        {
            const auto p = juce::Desktop::getInstance().getDisplays().logicalToPhysical (s->getScreenBounds().getCentre());
            std::string err;
            const auto px = screen::capture (p.x, p.y, 1, 1, err);
            r << shades.indexOf (s) << ": " << juce::String::toHexString ((int) (px.empty() ? 0 : px[0])) << " paints " << s->paints << " [" << screen::describeWindow (s->getWindowHandle()) << "] ";
        }
        expect (false, "LAYERDIAG " + r);
        for (auto* s : shades) s->removeFromDesktop();
        backdrop.removeFromDesktop();
    }
};
static LayerDiagTests layerDiagTests;
}
