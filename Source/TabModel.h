#pragma once
#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include "Theory.h"

namespace tabmidi
{
using namespace juce;

constexpr int kMaxStrings = 9;
constexpr int kPPQ        = 480;
constexpr int kCellTicks  = 120;   // one cell of the editor grid = a 16th note
constexpr int kMaxBars    = 64;

/** One grid column. String index 0 is the LOWEST string. fret < 0 means "no note". */
struct Cell
{
    int fret[kMaxStrings];
    int len[kMaxStrings];     // length in cells

    Cell() { for (int i = 0; i < kMaxStrings; ++i) { fret[i] = -1; len[i] = 1; } }
    bool empty() const { for (int f : fret) if (f >= 0) return false; return true; }
};

struct NoteEv  { int start, len, pitch, vel; };
struct TempoEv { int tick; double bpm; };
struct SigEv   { int tick, num, den; };

MidiFile buildMidiFile (std::vector<NoteEv> notes, const std::vector<TempoEv>& tempos,
                        const std::vector<SigEv>& sigs, const String& trackName);

struct TabScore
{
    int numStrings = 6;
    Array<int> tuning;                // lowest string first
    String tuningName = "Standard";
    int tempo = 120;
    std::vector<int> barCells;        // cells per bar (16 for 4/4)
    std::vector<Cell> cells;          // all bars, concatenated

    TabScore() { reset (4, 6, builtInTunings (6)[0].pitches, "Standard"); }

    void reset (int bars, int strings, const Array<int>& tun, const String& tunName);
    void setBarCount (int n);
    void setTuning (const Array<int>& tun, const String& name);

    int numBars() const { return (int) barCells.size(); }
    int totalCells() const { return (int) cells.size(); }
    int barStart (int bar) const;
    int barOfCell (int cell) const;
    int pitchOf (int stringIdx, int fret) const { return tuning[stringIdx] + fret; }
    std::vector<int> pitchesAt (int cell) const;

    MidiFile toMidiFile() const;
    std::unique_ptr<XmlElement> toXml() const;
    bool fromXml (const XmlElement& e);
};
} // namespace tabmidi
