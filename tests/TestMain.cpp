// Developer test: dumps what the importer sees and writes the MIDI files.
#include "../Source/GpxImporter.h"
using namespace tabmidi;

int main (int argc, char** argv)
{
    if (argc >= 2 && String (argv[1]) == "--tunings")
    {
        auto before = TuningStore::load().size();
        Tuning t; t.name = String::fromUTF8 ("Drop C (тест)"); t.custom = true; for (int p : { 36, 43, 48, 53, 57, 62 }) t.pitches.add (p);
        auto list = TuningStore::load(); list.push_back (t); TuningStore::save (list);
        auto back = TuningStore::forStrings (6);
        std::printf ("tunings before=%d after=%d, last: %s [%d notes, first=%d]\n", (int) before, (int) back.size(),
                     back.back().name.toRawUTF8(), back.back().pitches.size(), back.back().pitches[0]);
        TuningStore::file().deleteFile();
        return 0;
    }
    if (argc < 2) return 1;
    String err;
    auto gp = GpFile::load (File (argv[1]), err);
    if (gp == nullptr) { std::printf ("ERROR: %s\n", err.toRawUTF8()); return 2; }
    std::printf ("title: %s, tracks in file: %d, editable guitar tracks: %d\n", gp->title.toRawUTF8(), gp->trackCount(), (int) gp->tracks.size());

    ImportResult whole;
    gp->importSongOnly (whole);
    std::printf ("tempo/time map: %d tempo events, %d time signatures\n", (int) whole.song.tempos.size(), (int) whole.song.sigs.size());
    for (size_t i = 0; i < whole.song.tracks.size(); ++i)
    {
        auto& t = whole.song.tracks[i];
        int lo = 127, hi = 0;
        for (auto& n : t.notes) { lo = jmin (lo, n.pitch); hi = jmax (hi, n.pitch); }
        std::printf ("  [%d] %-22s notes=%-5d %s%s\n", (int) i, t.name.toRawUTF8(), (int) t.notes.size(),
                     t.notes.empty() ? "" : ("pitch " + String (lo) + ".." + String (hi)).toRawUTF8(), t.drums ? "  (drums)" : "");
        if (argc > 2 && ! t.notes.empty())
        {
            File out (String (argv[2]) + "_" + String ((int) i) + ".mid");
            out.deleteFile();
            auto os = out.createOutputStream();
            buildMidiFile (t.notes, whole.song.tempos, whole.song.sigs, t.name, t.drums ? 10 : 1).writeTo (*os, 1);
        }
    }
    for (int i = 0; i < (int) gp->tracks.size(); ++i)
    {
        ImportResult r;
        gp->importTrack (i, r);
        int cols = 0;
        for (auto& c : r.score.cells) { int k = 0; for (int s = 0; s < 9; ++s) if (c.fret[s] >= 0) ++k; if (k > 1) ++cols; }
        std::printf ("  editor track %d: %s, %d strings, %d bars (editor %d), chord columns %d\n", i, gp->tracks[(size_t) i].name.toRawUTF8(),
                     gp->tracks[(size_t) i].tuning.size(), r.totalBars, r.score.numBars(), cols);
    }
    return 0;
}
