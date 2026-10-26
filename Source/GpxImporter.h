#pragma once
#include <map>
#include "TabModel.h"

namespace tabmidi
{
struct ImportResult
{
    TabScore score;        // quantised to the 16th grid, editable
    MidiFile exact;        // exact timing straight from the file (tuplets etc.)
    String title;
    int totalBars = 0;     // bars after expanding repeats
    bool truncated = false;
};

/** Reads Guitar Pro 6 (.gpx) and Guitar Pro 7/8 (.gp) files. Only fretted tracks with 6-9 strings are offered. */
class GpFile
{
public:
    struct Track { int ordinal; String name; Array<int> tuning; int capo; };

    static std::unique_ptr<GpFile> load (const File& file, String& error);
    bool importTrack (int trackListIndex, ImportResult& out) const;

    std::vector<Track> tracks;
    String title;

private:
    struct MasterBar { int num = 4, den = 4; std::vector<int> barIds; bool repStart = false, repEnd = false; int repCount = 2; std::vector<int> alt; };
    struct Auto { int bar; double pos; double bpm; };

    bool parse (const String& xml, String& error);
    int rhythmTicks (int id) const;

    std::unique_ptr<XmlElement> root;
    std::map<int, const XmlElement*> bars, voices, beats, notes, rhythms;
    std::vector<MasterBar> masterBars;
    std::vector<Auto> tempoAutos;
};
} // namespace tabmidi
