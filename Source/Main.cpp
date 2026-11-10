// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include <juce_gui_basics/juce_gui_basics.h>

#include "app/AppSettings.h"
#include "app/EventLog.h"
#include "app/MainComponent.h"
#include "engine/core/Realtime.h"

namespace encore
{

/** Scrolls the main component vertically when the window is shorter than its content. */
class ScrollingContent final : public juce::Viewport
{
public:
    explicit ScrollingContent (AppSettings& settings)
        : content (std::make_unique<MainComponent> (settings))
    {
        setViewedComponent (content.get(), false);
        setScrollBarsShown (true, false);
        setScrollBarThickness (12);

        content->onViewChanged = [this]
        {
            // The stage view fills the window exactly; the edit view scrolls.
            setScrollBarsShown (! content->isShowingStage(), false);
            setViewPosition (0, 0);
            resized();
        };
    }

    void resized() override
    {
        Viewport::resized();
        const auto width = getMaximumVisibleWidth();
        content->setSize (width, juce::jmax (getMaximumVisibleHeight(), content->getRequiredHeight (width)));
    }

private:
    std::unique_ptr<MainComponent> content;
};

class MainWindow final : public juce::DocumentWindow
{
public:
    MainWindow (const juce::String& name, AppSettings& settings)
        : DocumentWindow (name, juce::Colour (0xff121417), DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar (true);
        setContentOwned (new ScrollingContent (settings), false);
        setResizable (true, true);
        setResizeLimits (640, 480, 8192, 8192);

        // Fit the screen we open on (laptops are often 768 px tall).
        auto screen = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()->userBounds.toNearestInt();
        centreWithSize (juce::jmin (1100, screen.getWidth() - 40), juce::jmin (900, screen.getHeight() - 40));
        setVisible (true);
    }

    void closeButtonPressed() override
    {
        juce::JUCEApplication::getInstance()->systemRequestedQuit();
    }

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
};

class EncoreApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }

    // Two copies would fight over the same ASIO driver, which most drivers
    // handle badly. A second launch just brings the first one to the front.
    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise (const juce::String&) override
    {
        // Debug builds: count any allocation made on the audio thread.
        rt::installAllocationTripwire();

        settings = std::make_unique<AppSettings>();
        eventlog::start (settings->getFolder().getChildFile ("Logs"), getApplicationVersion());

        mainWindow = std::make_unique<MainWindow> (getApplicationName(), *settings);
    }

    void shutdown() override
    {
        mainWindow = nullptr;   // closes the audio device first
        settings->save();
        settings = nullptr;
        eventlog::stop();
    }

    void systemRequestedQuit() override { quit(); }

    void anotherInstanceStarted (const juce::String&) override
    {
        if (mainWindow != nullptr)
            mainWindow->toFront (true);
    }

private:
    std::unique_ptr<AppSettings> settings;
    std::unique_ptr<MainWindow> mainWindow;
};

} // namespace encore

START_JUCE_APPLICATION (encore::EncoreApplication)
