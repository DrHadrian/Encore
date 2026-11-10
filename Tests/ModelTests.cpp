// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include <juce_core/juce_core.h>

#include "model/ShowFolder.h"
#include "model/ShowJson.h"

namespace encore::test
{

/** A show that uses every field: sections, cues, a unique patch, files, params. */
inline ShowModel makeRichShow()
{
    auto show = makeDefaultShow ("Phantom - Pit");
    show.showTemplate = ShowTemplate::musical;
    show.patches.push_back (duplicatePatch (show.patches[0], "Lead"));
    show.patches[1].spillover = Spillover::fullChain;
    show.patches[1].colour = 0xffe5484d;
    show.patches[1].fadeInMs = 5.0f;
    show.patches[1].ringOutMs = 500.0f;
    show.patches[1].mainLane().blocks[0].params["input"] = 6.5f;
    show.patches[1].mainLane().blocks[0].file = "models/lead amp.nam";
    show.patches[1].mainLane().blocks[2].enabled = false;
    show.patches[1].mainLane().blocks[2].stomp = 3;
    show.patches[1].mainLane().blocks[0].expression["input"] = { 2, -6.0f, 12.0f };
    show.ownMidiMappings = true;
    show.midiBindings = makeDefaultMidiBindings();
    show.midiBindings.push_back ({ MidiAction::stomp, 3, { MidiTrigger::Kind::note, 10, 36 } });

    auto act = RunItem::makeSection ("Act 1", "act");

    auto cue = RunItem::makeCue ("42.5", "Think of Me", {});
    cue.barMark = "bar 112 / C";
    cue.note = "watch conductor, swell on fermata";
    cue.patchId = show.patches[1].id;
    cue.samePatch = SamePatchBehaviour::reset;
    cue.programChange = 7;
    cue.midiOut = { { MidiOutMessage::Kind::programChange, 1, 5, 0 }, { MidiOutMessage::Kind::controlChange, 2, 20, 127 } };
    act.children.push_back (cue);

    RunItem cutCue = cue;
    cutCue.id = newId();
    cutCue.number = "43";
    cutCue.cut = true;
    cutCue.patchId.clear();
    cutCue.uniquePatch = duplicatePatch (show.patches[0], "Banjo");
    act.children.push_back (cutCue);

    show.runningOrder.children.push_back (act);
    return show;
}

class ShowJsonTests final : public juce::UnitTest
{
public:
    ShowJsonTests() : juce::UnitTest ("Show file format", "Encore") {}

    void runTest() override
    {
        beginTest ("Everything survives a save/load round trip exactly");
        {
            const auto show = makeRichShow();
            juce::String error;
            const auto loaded = json::fromJson (json::toJson (show), error);
            expect (loaded.has_value(), error);
            expect (loaded.has_value() && *loaded == show);
        }

        beginTest ("Damaged, foreign and too-new files are refused with a clear message");
        {
            juce::String error;
            expect (! json::fromJson ("{ not json", error).has_value());
            expect (error.contains ("damaged"));

            expect (! json::fromJson (R"({ "hello": 1 })", error).has_value());
            expect (error.contains ("isn't an Encore show"));

            expect (! json::fromJson (R"({ "format": "encore-show", "schemaVersion": 999 })", error).has_value());
            expect (error.contains ("newer version"));
        }

        beginTest ("Missing fields fall back to defaults (hand-edited files stay loadable)");
        {
            juce::String error;
            const auto show = json::fromJson (R"({ "format": "encore-show", "schemaVersion": 1,
                                                  "patches": [ { "name": "Bare" } ] })", error);
            expect (show.has_value(), error);

            if (show)
            {
                expectEquals ((int) show->patches.size(), 1);
                expect (show->patches[0].id.isNotEmpty(), "gets an ID");
                expectEquals ((int) show->patches[0].lanes.size(), 1, "always has a lane");
                expect (show->runningOrder.kind == RunItem::Kind::section);
            }
        }
    }
};

class ShowFolderTests final : public juce::UnitTest
{
public:
    ShowFolderTests() : juce::UnitTest ("Show folders", "Encore") {}

    void runTest() override
    {
        const auto temp = juce::File::getSpecialLocation (juce::File::tempDirectory)
                              .getChildFile ("encore-tests-" + juce::String (juce::Random::getSystemRandom().nextInt()));
        temp.createDirectory();

        // Some files outside the show, as if picked from the user's downloads.
        const auto downloads = temp.getChildFile ("Downloads");
        downloads.createDirectory();
        const auto amp = downloads.getChildFile ("plexi.nam");
        amp.replaceWithText ("amp capture A");
        const auto otherAmp = downloads.getChildFile ("other").getChildFile ("plexi.nam");
        otherAmp.getParentDirectory().createDirectory();
        otherAmp.replaceWithText ("a different capture, same name");
        const auto cab = downloads.getChildFile ("4x12 V30.wav");
        cab.replaceWithText ("cab ir");

        beginTest ("Create, then open from the folder or from show.json");
        {
            auto show = makeRichShow();
            show.patches[1].mainLane().blocks[0].file.clear();
            const auto folder = ShowFolder::suggestFolder (temp, show.name);
            expect (folder.getFileName() == "Phantom - Pit.encore");
            expect (ShowFolder::create (folder, show).wasOk());

            for (auto f : { folder, folder.getChildFile ("show.json") })
            {
                ShowFolder opened;
                juce::String error;
                const auto loaded = ShowFolder::open (f, opened, error);
                expect (loaded.has_value() && *loaded == show, error);
                expect (opened.getFolder() == folder);
            }
        }

        beginTest ("Saving copies outside files into the show; the folder works anywhere");
        {
            auto show = makeDefaultShow ("Portable");
            show.patches[0].mainLane().blocks[0].file = amp.getFullPathName();
            show.patches[0].mainLane().blocks[1].file = cab.getFullPathName();

            const auto folder = ShowFolder::suggestFolder (temp, show.name);
            expect (ShowFolder::create (folder, show).wasOk());
            expectEquals (show.patches[0].mainLane().blocks[0].file, juce::String ("models/plexi.nam"));
            expectEquals (show.patches[0].mainLane().blocks[1].file, juce::String ("irs/4x12 V30.wav"));

            // "Copy to a USB stick": move the whole folder, delete the originals.
            const auto moved = temp.getChildFile ("USB").getChildFile ("Portable.encore");
            moved.getParentDirectory().createDirectory();
            expect (folder.moveFileTo (moved));

            ShowFolder opened;
            juce::String error;
            const auto loaded = ShowFolder::open (moved, opened, error);
            expect (loaded.has_value(), error);

            if (loaded)
            {
                const auto ampFile = opened.resolve (loaded->patches[0].mainLane().blocks[0].file);
                expect (ampFile.existsAsFile() && ampFile.loadFileAsString() == "amp capture A");
            }
        }

        beginTest ("Same file shared, same name but different file kept apart");
        {
            const ShowFolder folder (temp.getChildFile ("Assets.encore"));
            juce::String error;
            expectEquals (folder.importAsset (amp, "models", error), juce::String ("models/plexi.nam"));
            expectEquals (folder.importAsset (amp, "models", error), juce::String ("models/plexi.nam"), "identical: reused");
            expectEquals (folder.importAsset (otherAmp, "models", error), juce::String ("models/plexi (2).nam"));

            const auto inside = folder.getFolder().getChildFile ("models/plexi.nam");
            expectEquals (folder.importAsset (inside, "models", error), juce::String ("models/plexi.nam"), "already inside");

            expect (folder.importAsset (downloads.getChildFile ("gone.nam"), "models", error).isEmpty());
            expect (error.contains ("Missing"));
        }

        beginTest ("Every save keeps a backup; only the newest 50 are kept");
        {
            auto show = makeDefaultShow ("Backups");
            const auto folderFile = ShowFolder::suggestFolder (temp, show.name);
            expect (ShowFolder::create (folderFile, show).wasOk());
            ShowFolder folder (folderFile);

            for (int i = 0; i < 55; ++i)
            {
                show.patches[0].name = "Version " + juce::String (i);
                expect (folder.save (show, true).wasOk());
            }

            const auto backups = folder.listBackups();
            expectEquals (backups.size(), ShowFolder::maxBackups);

            // The newest backup holds the version before the last save.
            juce::String error;
            const auto newest = json::fromJson (backups[0].loadFileAsString(), error);
            expect (newest.has_value() && newest->patches[0].name == "Version 53");
        }

        beginTest ("No temporary files are left behind after saving");
        {
            const auto leftovers = temp.findChildFiles (juce::File::findFiles, true, "*.tmp");
            expectEquals (leftovers.size(), 0);
        }

        temp.deleteRecursively();
    }
};

class MidiMappingTests final : public juce::UnitTest
{
public:
    MidiMappingTests() : juce::UnitTest ("MIDI mapping text and format", "Encore") {}

    void runTest() override
    {
        beginTest ("Cue MIDI out: text round trip, and clear errors");
        std::vector<MidiOutMessage> parsed;
        juce::String error;
        expect (parseMidiOut ("PC 1 5\n cc 2 20 127 \n\n", parsed, error), error);
        expectEquals ((int) parsed.size(), 2);
        expect (parsed[1].kind == MidiOutMessage::Kind::controlChange && parsed[1].value == 127);

        std::vector<MidiOutMessage> again;
        expect (parseMidiOut (formatMidiOut (parsed), again, error) && again == parsed);

        expect (! parseMidiOut ("PC 17 5", parsed, error), "channel out of range");
        expect (error.contains ("PC 17 5"));
        expect (! parseMidiOut ("hello", parsed, error));

        beginTest ("Bindings survive JSON; unknown actions from newer versions are skipped");
        const auto defaults = makeDefaultMidiBindings();
        expect (bindingsFromVar (bindingsToVar (defaults)) == defaults);

        juce::var parsedJson;
        juce::JSON::parse (R"([{ "action": "teleport", "kind": "cc", "number": 3 }, { "action": "mute", "kind": "note", "number": 60 }])", parsedJson);
        const auto read = bindingsFromVar (parsedJson);
        expectEquals ((int) read.size(), 1);
        expect (read[0].action == MidiAction::mute && read[0].trigger.kind == MidiTrigger::Kind::note);
    }
};

static ShowJsonTests showJsonTests;
static MidiMappingTests midiMappingTests;
static ShowFolderTests showFolderTests;

} // namespace encore::test
