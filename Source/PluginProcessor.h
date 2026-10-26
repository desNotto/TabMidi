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
    int keyRoot = 9;               // A
    int keyScale = 1;              // Minor
    bool showChords = true;        // chord helper panel on the right
    int duration = 2;              // default length of a newly typed note, in cells (2 = 1/8)

    void markEdited()              { exactValid = false; }
    bool hasExactImport() const    { return exactValid; }
    void setImported (ImportResult&& r, const String& name);
    File makeMidiFile();           // writes a .mid into the temp folder and returns it

private:
    MidiFile exact;
    bool exactValid = false;
    String sourceName;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TabMidiProcessor)
};
} // namespace tabmidi
