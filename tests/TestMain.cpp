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
    std::printf ("title: %s, tracks: %d\n", gp->title.toRawUTF8(), (int) gp->tracks.size());
    for (size_t i = 0; i < gp->tracks.size(); ++i)
    {
        ImportResult r;
        gp->importTrack ((int) i, r);
        int notes = 0, chords = 0;
        for (auto& c : r.score.cells) { int k = 0; for (int s = 0; s < 9; ++s) if (c.fret[s] >= 0) ++k; notes += k; if (k > 1) ++chords; }
        std::printf ("  [%d] %s strings=%d capo=%d bars=%d (editor %d) notes=%d chordCols=%d tempo=%d\n", (int) i,
                     gp->tracks[i].name.toRawUTF8(), gp->tracks[i].tuning.size(), gp->tracks[i].capo,
                     r.totalBars, r.score.numBars(), notes, chords, r.score.tempo);
        if (argc > 2)
        {
            File out (String (argv[2]) + "_" + String ((int) i) + "_exact.mid");
            out.deleteFile(); { auto os = out.createOutputStream(); r.exact.writeTo (*os, 1); }
            File out2 (String (argv[2]) + "_" + String ((int) i) + "_quant.mid");
            out2.deleteFile(); { auto os = out2.createOutputStream(); r.score.toMidiFile().writeTo (*os, 1); }
            if (i == 0)
            {
                for (int c = 0; c < jmin (r.score.totalCells(), 64); ++c)
                {
                    auto p = r.score.pitchesAt (c);
                    if (! p.empty()) std::printf ("   cell %2d: %s  %s\n", c, chordName (p).toRawUTF8(), String (p.size()).toRawUTF8());
                }
            }
        }
    }
    return 0;
}
