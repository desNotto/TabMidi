#pragma once
#include <map>
#include "TabModel.h"

namespace tabmidi
{
/** One track of the song as MIDI notes (ticks at 480 per quarter). */
struct ExportTrack
{
    String name;
    std::vector<NoteEv> notes;
    bool drums = false;
};

/** Every track of a Guitar Pro file plus its tempo and time signature map. */
struct SongData
{
    String name;
    std::vector<ExportTrack> tracks;
    std::vector<TempoEv> tempos;
    std::vector<SigEv> sigs;
};

struct ImportResult
{
    SongData song;                 // all tracks, for MAKE MIDI
    bool hasEditorTrack = false;
    int editorTrack = -1;          // index into song.tracks of the track that was loaded into the editor
    TabScore score;                // that track quantised to the 16th grid, editable
    String title;
    int totalBars = 0;             // bars after expanding repeats
    bool truncated = false;
};

/** Reads Guitar Pro 6 (.gpx) and Guitar Pro 7/8 (.gp) files. Every track goes to MIDI; guitar tracks with
    6-9 strings can also be opened in the tab editor. */
class GpFile
{
public:
    struct Track
    {
        int ordinal = 0;
        String name;
        Array<int> tuning;             // lowest string first, empty for instruments without strings
        int capo = 0;
        bool drums = false;
        std::vector<int> drumOut;      // GP7 drum kit: MIDI note of every articulation
    };

    static std::unique_ptr<GpFile> load (const File& file, String& error);

    /** Whole song for MAKE MIDI + one guitar track (index into `tracks`) for the editor. */
    bool importTrack (int trackListIndex, ImportResult& out) const;
    /** Whole song only (a file without a 6-9 string guitar track). */
    bool importSongOnly (ImportResult& out) const;
    int noteCount (int trackListIndex) const;

    std::vector<Track> tracks;         // tracks that can be shown in the editor
    int trackCount() const { return (int) allTracks.size(); }
    String title;

private:
    struct MasterBar { int num = 4, den = 4; std::vector<int> barIds; bool repStart = false, repEnd = false; int repCount = 2; std::vector<int> alt; };
    struct Auto { int bar; double pos; double bpm; };
    struct Imp { int start, len, vel, str, fret, midi; };

    bool parse (const String& xml, String& error);
    int rhythmTicks (int id) const;
    void ensureTimeline() const;
    const std::vector<Imp>& notesOf (const Track& tr) const;
    void fillSong (SongData& song) const;
    static int pitchOf (const Track& tr, const Imp& n) { return n.str >= 0 ? tr.tuning[n.str] + tr.capo + n.fret : n.midi; }

    std::unique_ptr<XmlElement> root;
    std::map<int, const XmlElement*> bars, voices, beats, notes, rhythms;
    std::vector<MasterBar> masterBars;
    std::vector<Auto> tempoAutos;
    std::vector<Track> allTracks;

    mutable bool timelineBuilt = false;
    mutable std::vector<int> order, barTicks;                 // played bars (repeats expanded), their length in ticks
    mutable std::vector<SigEv> sigs;
    mutable std::vector<TempoEv> tempos;
    mutable std::map<int, std::vector<Imp>> cache;            // by track ordinal
};
} // namespace tabmidi
