#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "GpxImporter.h"

namespace tabmidi
{
class TabMidiProcessor : public AudioProcessor
{
public:
    TabMidiProcessor();

    // The plugin never makes sound or live MIDI: it is a workbench that produces .mid files.
    const String getName() const override { return "TabMidi"; }
    void prepareToPlay (double, int) override {}
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& l) const override { return l.getMainOutputChannelSet() == AudioChannelSet::stereo(); }
    void processBlock (AudioBuffer<float>& b, MidiBuffer& m) override { b.clear(); m.clear(); }

    bool hasEditor() const override { return true; }
    AudioProcessorEditor* createEditor() override;
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const String getProgramName (int) override { return {}; }
    void changeProgramName (int, const String&) override {}

    void getStateInformation (MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    //==========================================================================
    CriticalSection lock;          // guards everything below (GUI thread vs. host state calls)
    TabScore score;
    int keyRoot = 4;               // E
    int keyScale = 1;              // Minor
    int viewBars = 4;              // how many bars the tab shows at once (4 / 8 / 16)
    int editorW = 0, editorH = 0;  // last window size, 0 = default
    bool exportSeparate = true;    // MAKE MIDI: one file per track (true) or one file with all tracks
    int duration = 2;              // default length of a newly typed note, in cells (2 = 1/8)

    void markEdited()              { exactValid = false; }
    bool hasExactImport() const    { return exactValid; }
    /** Takes a freshly read Guitar Pro file (sameSong = false) or just switches the track shown in the editor (sameSong = true). */
    void setImported (ImportResult&& r, const String& name, bool sameSong);
    int exportTrackCount() const;  // tracks of the imported song that have notes
    /** Writes the .mid files for MAKE MIDI into the temp folder: one per track, or a single file with every track. */
    std::vector<File> makeMidiFiles (bool separate);

private:
    void commitEditedTrack();      // copies the edited tab back into the imported song
    SongData song;                 // the imported Guitar Pro file, all tracks
    int activeTrack = -1;          // index into song.tracks that is shown in the editor
    bool exactValid = false;       // the editor track has not been edited since the import
    String sourceName;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TabMidiProcessor)
};
} // namespace tabmidi
