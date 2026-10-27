// Developer tool: renders the plugin window to PNG files without a DAW.
#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"
#include <set>
using namespace tabmidi;

static void snap (Component& c, const String& path)
{
    auto img = c.createComponentSnapshot (c.getLocalBounds(), true, 1.0f);
    File f (path); f.deleteFile();
    if (auto os = f.createOutputStream()) PNGImageFormat().writeImageToStream (img, *os);
}

static TabCanvas* findCanvas (Component& c)
{
    for (auto* ch : c.getChildren())
        if (auto* v = dynamic_cast<Viewport*> (ch))
            return dynamic_cast<TabCanvas*> (v->getViewedComponent());
    return nullptr;
}

static ChordPanel* findPanel (Component& c)
{
    for (auto* ch : c.getChildren())
        if (auto* p = dynamic_cast<ChordPanel*> (ch)) return p;
    return nullptr;
}

#define CHECK(cond) do { std::printf ("%s  %s\n", (cond) ? "ok  " : "FAIL", #cond); } while (0)

static void behaviourTests()
{
    TabMidiProcessor p;
    TabCanvas cv (p);
    String lastInfo;
    cv.onInfo = [&lastInfo] (const String& m) { lastInfo = m; };
    auto fret = [&p] (int cell, int s) { return p.score.cells[(size_t) cell].fret[s]; };

    // fretboard click: first adds, second removes
    cv.select (5, 3);
    cv.toggleNote (3, 2);   CHECK (fret (5, 3) == 2);
    cv.toggleNote (3, 2);   CHECK (fret (5, 3) == -1);
    cv.toggleNote (3, 2);   cv.toggleNote (3, 4);   CHECK (fret (5, 3) == 4);      // another fret on the same string replaces

    // bars: copy / paste / delete / undo (4 bars of 16 cells)
    p.score.cells[0].fret[0] = 3; p.score.cells[7].fret[2] = 5; p.score.cells[7].len[2] = 4;
    cv.selectBar (0, false);
    CHECK (cv.hasBarSelection());
    CHECK (cv.copyBars());
    cv.selectBar (2, false);
    cv.pasteBars();
    CHECK (fret (32, 0) == 3 && fret (39, 2) == 5 && p.score.cells[39].len[2] == 4);
    cv.selectBar (0, false);
    cv.deleteBars();
    CHECK (fret (0, 0) == -1 && fret (7, 2) == -1 && fret (32, 0) == 3);
    cv.undo();
    CHECK (fret (0, 0) == 3 && fret (7, 2) == 5);
    cv.undo();
    CHECK (fret (32, 0) == -1);                                                    // the paste is undone too
    cv.selectBar (0, false); cv.selectBar (1, true); cv.copyBars();                // two bars
    cv.selectBar (3, false); cv.pasteBars();                                       // only one fits
    std::printf ("      paste message: %s\n", lastInfo.toRawUTF8());
    CHECK (fret (48, 0) == 3);

    // keyboard shortcuts
    cv.selectBar (1, false);
    CHECK (cv.keyPressed (KeyPress ('C', ModifierKeys (ModifierKeys::ctrlModifier), 0)));
    cv.selectBar (2, false);
    CHECK (cv.keyPressed (KeyPress ('V', ModifierKeys (ModifierKeys::ctrlModifier), 0)));
    CHECK (cv.keyPressed (KeyPress (KeyPress::deleteKey)));
    CHECK (cv.keyPressed (KeyPress ('Z', ModifierKeys (ModifierKeys::ctrlModifier), 0)));
    std::printf ("      last message: %s\n", lastInfo.toRawUTF8());

    // chord table: which chords of E minor are "in key" (compare with the reference chart)
    const auto& sc = scaleAt (1);
    String inKey;
    for (int r : { 0, 1, 2, 3 })
        for (int c = 0; c < 7; ++c)
        {
            const int root = (4 + sc.iv[(size_t) c]) % 12;
            if (chordInScale (root, chordTypes()[(size_t) r], 4, sc)) inKey << noteName (root) << chordTypes()[(size_t) r].name << " ";
        }
    std::printf ("      E minor, maj/min/sus2/sus4 in key: %s\n", inKey.toRawUTF8());
    const auto& d11 = chordTypes()[21];
    CHECK (chordInScale (2, d11, 4, sc));                                          // D11 is highlighted in the reference
    CHECK (! chordInScale (4, chordTypes()[0], 4, sc));                            // Emaj is not
}

static void click (TextButton* b) { if (b != nullptr && b->onClick) b->onClick(); else std::printf ("      (no button or handler)\n"); }

static TextButton* findButton (Component& c, const String& text, int skip = 0)
{
    for (auto* ch : c.getChildren())
        if (auto* b = dynamic_cast<TextButton*> (ch))
            if (b->getButtonText() == text && skip-- == 0) return b;
    return nullptr;
}

static void gpExportAndGridTests (const String& gpPath)
{
    // ---- 1. every track of a Guitar Pro file reaches MIDI
    String err;
    auto gp = GpFile::load (File (gpPath), err);
    CHECK (gp != nullptr);
    if (gp != nullptr)
    {
        TabMidiProcessor p;
        TabCanvas cv (p);
        ImportResult r;
        const int editable = gp->noteCount (0) > 0 ? 0 : -1;
        if (editable >= 0) gp->importTrack (0, r); else gp->importSongOnly (r);
        std::printf ("      file has %d tracks, %d of them can open in the editor\n", gp->trackCount(), (int) gp->tracks.size());
        p.setImported (std::move (r), "song", false);
        const int withNotes = p.exportTrackCount();
        CHECK (withNotes >= 2);

        const auto separate = p.makeMidiFiles (true);
        CHECK ((int) separate.size() == withNotes);
        const auto combined = p.makeMidiFiles (false);
        CHECK (combined.size() == 1);
        MidiFile mf;
        { FileInputStream in (combined.front()); CHECK (mf.readFrom (in)); }
        std::printf ("      separate files: %d, combined file has %d MIDI tracks (1 conductor + %d)\n", (int) separate.size(), mf.getNumTracks(), mf.getNumTracks() - 1);
        CHECK (mf.getNumTracks() == withNotes + 1);
        std::set<int> channels;
        for (int t = 1; t < mf.getNumTracks(); ++t)
            for (int i = 0; i < mf.getTrack (t)->getNumEvents(); ++i)
                if (mf.getTrack (t)->getEventPointer (i)->message.isNoteOn()) { channels.insert (mf.getTrack (t)->getEventPointer (i)->message.getChannel()); break; }
        CHECK ((int) channels.size() == withNotes);                               // each track has its own channel

        // an edit of the editor track goes into its file only
        const auto before = separate.front().getSize();
        cv.select (0, 0); cv.putNote (0, 7);
        const auto afterEdit = p.makeMidiFiles (true);
        CHECK (afterEdit.size() == separate.size());
        std::printf ("      first file before/after an edit in the editor: %d / %d bytes\n", (int) before, (int) afterEdit.front().getSize());
    }

    // ---- 2. the grid follows the note length: a whole note only at the start of a bar
    {
        TabMidiProcessor p;
        p.score.reset (4, 6, builtInTunings (6)[0].pitches, "Standard");
        TabCanvas cv (p);
        cv.setBounds (0, 0, 1000, 200);
        cv.refreshSize();
        const float x0 = TabCanvas::x0, w = cv.cellW;
        p.duration = 16;                                              // whole note
        cv.mouseDown (MouseEvent (Desktop::getInstance().getMainMouseSource(), Point<float> (x0 + 8.5f * w, 60.0f), ModifierKeys(), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &cv, &cv, Time::getCurrentTime(), Point<float> (x0 + 8.5f * w, 60.0f), Time::getCurrentTime(), 1, false));
        CHECK (cv.selCell == 0);                                      // clicked in the middle of bar 1, lands on its start
        cv.putNote (2, 5);
        CHECK (p.score.cells[0].len[2] == 16);
        cv.keyPressed (KeyPress (KeyPress::rightKey));
        CHECK (cv.selCell == 16);                                     // next slot = next bar
        p.duration = 8;                                               // half note
        cv.applyDurationToSelection();
        cv.select (11, 2);
        cv.snapSelectionToGrid();
        CHECK (cv.selCell == 8);
        cv.putNote (2, 3);
        CHECK (p.score.cells[8].len[2] == 8);
        p.duration = 16;
        cv.select (24, 2); cv.snapSelectionToGrid();
        CHECK (cv.selCell == 16);
        // a note that is longer than the room left in the bar is cut at the bar line
        p.duration = 8; cv.select (14, 3); cv.putNote (3, 4);
        p.duration = 8;
        CHECK (p.score.cells[14].len[3] == 2);
    }

    // ---- 3. drag selection and Shift+arrows select cells, Delete clears them
    {
        TabMidiProcessor p;
        p.score.reset (4, 6, builtInTunings (6)[0].pitches, "Standard");
        p.duration = 1;
        for (int i = 0; i < 20; ++i) p.score.cells[(size_t) i].fret[1] = 3;
        TabCanvas cv (p);
        cv.setBounds (0, 0, 1000, 200);
        cv.refreshSize();
        cv.select (2, 1);
        for (int i = 0; i < 5; ++i) cv.keyPressed (KeyPress (KeyPress::rightKey, ModifierKeys (ModifierKeys::shiftModifier), 0));
        CHECK (cv.hasBarSelection());
        cv.keyPressed (KeyPress (KeyPress::deleteKey));
        CHECK (p.score.cells[2].fret[1] == -1 && p.score.cells[7].fret[1] == -1 && p.score.cells[8].fret[1] == 3 && p.score.cells[1].fret[1] == 3);
        cv.undo();
        CHECK (p.score.cells[4].fret[1] == 3);
    }

    // ---- 4. a fresh plugin: 8 strings, Drop E, E minor, 4 bars; "visible bars" grows an empty staff
    {
        TabMidiProcessor p;
        CHECK (p.score.numStrings == 8 && p.score.tuningName == "Drop E" && p.score.numBars() == 4);
        CHECK (p.keyRoot == 4 && scaleAt (p.keyScale).name == "Minor" && p.viewBars == 4);
        std::unique_ptr<AudioProcessorEditor> ed (p.createEditor());
        TextButton* b16 = nullptr;
        for (auto* ch : ed->getChildren()) if (auto* b = dynamic_cast<TextButton*> (ch)) if (b->getButtonText() == "16" && b->getRadioGroupId() == 104) b16 = b;
        CHECK (b16 != nullptr);
        if (b16 != nullptr) { b16->onClick(); }
        CHECK (p.viewBars == 16 && p.score.numBars() == 16);
    }
}

static void scaleAndTuningTests()
{
    ScaleStore::file().deleteFile();
    TabMidiProcessor p;
    int changes = 0;

    {   // create a key from the current one (A minor): add the major 7th -> harmonic minor shape
        ScaleEditor ed (p, false, [&changes] { ++changes; });
        findButton (ed, "7")->setToggleState (true, dontSendNotification);
        click (findButton (ed, "7"));                                      // the click handler reads the toggle state
        for (auto* ch : ed.getChildren())
            if (auto* te = dynamic_cast<TextEditor*> (ch)) te->setText ("Test key", dontSendNotification);
        click (findButton (ed, String::fromUTF8 ("Сохранить")));
        const int idx = indexOfScale ("Test key");
        CHECK (idx >= builtInScaleCount() && p.keyScale == idx);
        CHECK (scaleAt (idx).iv.size() >= 7 && changes == 1);
        CHECK (ScaleStore::load().size() == 1);
    }
    {   // edit it: rename and drop a note
        ScaleEditor ed (p, true, [&changes] { ++changes; });
        for (auto* ch : ed.getChildren())
            if (auto* te = dynamic_cast<TextEditor*> (ch)) te->setText ("Test key 2", dontSendNotification);
        click (findButton (ed, String::fromUTF8 ("Сохранить")));
        CHECK (indexOfScale ("Test key") < 0 && indexOfScale ("Test key 2") >= 0);
        CHECK (customScales().size() == 1 && ScaleStore::load().size() == 1);
    }
    {   // a project that carries the key restores it, even when this computer does not know it
        MemoryBlock mb; p.getStateInformation (mb);
        const auto iv = scaleAt (p.keyScale).iv;
        setCustomScales ({});                                                      // pretend a different computer
        TabMidiProcessor q; q.setStateInformation (mb.getData(), (int) mb.getSize());
        CHECK (scaleAt (q.keyScale).name == "Test key 2" && scaleAt (q.keyScale).iv == iv);
        setCustomScales (ScaleStore::load());
        p.keyScale = indexOfScale ("Test key 2");
    }
    {   // delete it
        ScaleEditor ed (p, true, [&changes] { ++changes; });
        click (findButton (ed, String::fromUTF8 ("Удалить")));
        CHECK (customScales().empty() && ScaleStore::load().empty());
        CHECK (scaleAt (p.keyScale).name == "Minor");
    }

    {   // whole-tuning buttons move every string by a semitone
        TuningEditor te (p, nullptr);
        const auto before = p.score.tuning;
        click (findButton (te, "+", 0));                                   // first "+" is the whole-tuning one
        bool allUp = true; for (int i = 0; i < before.size(); ++i) allUp = allUp && p.score.tuning[i] == before[i] + 1;
        CHECK (allUp);
        click (findButton (te, String::fromUTF8 ("\xe2\x88\x92"), 0));
        click (findButton (te, String::fromUTF8 ("\xe2\x88\x92"), 0));
        bool allDown = true; for (int i = 0; i < before.size(); ++i) allDown = allDown && p.score.tuning[i] == before[i] - 1;
        CHECK (allDown);
    }
}

int main (int argc, char** argv)
{
    ScopedJuceInitialiser_GUI init;
    behaviourTests();
    scaleAndTuningTests();
    gpExportAndGridTests (argc > 2 ? argv[2] : "");
    String out = argc > 1 ? argv[1] : "/tmp/snap";
    File (out).createDirectory();

    {   // 1. empty state
        TabMidiProcessor proc;
        std::unique_ptr<AudioProcessorEditor> ed (proc.createEditor());
        snap (*ed, out + "/1_empty.png");
    }
    if (argc > 2)
    {   // 2. imported file
        TabMidiProcessor proc;
        std::unique_ptr<AudioProcessorEditor> ed (proc.createEditor());
        dynamic_cast<TabMidiEditor*> (ed.get())->filesDropped (StringArray (argv[2]), 0, 0);
        click (findButton (*ed, "MAKE MIDI"));                                     // the drag area now holds every track
        snap (*ed, out + "/2_imported.png");
    }
    {   // 13. the grid follows the note length: whole note selected
        TabMidiProcessor p;
        p.duration = 16;
        auto put = [&p] (int cell, std::initializer_list<std::pair<int, int>> notes, int len)
        {
            for (auto& n : notes) { p.score.cells[(size_t) cell].fret[n.first] = n.second; p.score.cells[(size_t) cell].len[n.first] = len; }
        };
        put (0,  { { 2, 0 }, { 3, 2 }, { 4, 2 }, { 5, 0 }, { 6, 0 }, { 7, 0 } }, 16);           // Em, whole note
        put (16, { { 3, 0 }, { 4, 2 }, { 5, 2 }, { 6, 1 }, { 7, 0 } }, 8);                      // Am, half note
        put (24, { { 3, 2 }, { 4, 2 }, { 5, 0 }, { 6, 0 }, { 7, 0 } }, 8);                      // second half
        put (32, { { 1, 0 } }, 4); put (36, { { 1, 3 } }, 4); put (40, { { 1, 5 } }, 4); put (44, { { 1, 3 } }, 4);   // quarters
        std::unique_ptr<AudioProcessorEditor> ed (p.createEditor());
        if (auto* cv = findCanvas (*ed)) cv->select (48, 3);                       // empty bar 4, cursor spans the whole bar
        snap (*ed, out + "/13_whole_note_grid.png");
        if (auto* cv = findCanvas (*ed)) { cv->select (16, 3); cv->mouseDown (MouseEvent (Desktop::getInstance().getMainMouseSource(), Point<float> (TabCanvas::x0 + 4.0f * cv->cellW, 60.0f), ModifierKeys(), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, cv, cv, Time::getCurrentTime(), Point<float> (TabCanvas::x0 + 4.0f * cv->cellW, 60.0f), Time::getCurrentTime(), 1, false)); }
    }
    {   // 7. the view from the latest request: 8 strings, Drop E, E minor, a bar selected, chord Gmaj7 from the helper panel
        TabMidiProcessor p;
        p.score.reset (4, 8, builtInTunings (8)[1].pitches, "Drop E");
        p.keyRoot = 4; p.keyScale = 1; p.duration = 4;
        auto put = [&p] (int cell, std::initializer_list<std::pair<int, int>> notes, int len)
        {
            for (auto& n : notes) { p.score.cells[(size_t) cell].fret[n.first] = n.second; p.score.cells[(size_t) cell].len[n.first] = len; }
        };
        put (0,  { { 2, 0 }, { 3, 2 }, { 4, 2 }, { 5, 0 }, { 6, 0 }, { 7, 0 } }, 4);       // Em
        put (4,  { { 2, 3 }, { 3, 2 }, { 4, 0 }, { 5, 0 }, { 6, 0 }, { 7, 3 } }, 4);       // G
        put (8,  { { 3, 0 }, { 4, 2 }, { 5, 2 }, { 6, 1 }, { 7, 0 } }, 4);                 // Am
        put (12, { { 4, 0 }, { 5, 2 }, { 6, 3 }, { 7, 2 } }, 4);                           // D
        put (16, { { 0, 0 } }, 2); put (18, { { 0, 0 } }, 2); put (20, { { 1, 0 } }, 2); put (22, { { 0, 3 } }, 2);
        std::unique_ptr<AudioProcessorEditor> ed (p.createEditor());
        if (auto* cv = findCanvas (*ed)) cv->selectBar (0, false);
        snap (*ed, out + "/7_chords_bar.png");
        if (auto* pn = findPanel (*ed)) pn->pick (7, 5);                               // Gmaj7
        snap (*ed, out + "/8_chord_mode.png");
        if (auto* pn = findPanel (*ed)) pn->pick (7, 5);                               // second click: back to the key
        if (auto* cv = findCanvas (*ed)) cv->select (8, 5);
        snap (*ed, out + "/9_back_to_key.png");
    }
    {   // 10. bigger window, 16 bars shown at once, a custom key, 24-fret neck
        ScaleStore::file().deleteFile();
        TabMidiProcessor p;
        p.score.reset (16, 7, builtInTunings (7)[0].pitches, "Standard");
        p.keyRoot = 9; p.viewBars = 16; p.duration = 2;
        {
            const ScopedLock sl (p.lock);
            Scale sc; sc.name = String::fromUTF8 ("Моя пентатоника"); sc.iv = { 0, 3, 5, 6, 7, 10 }; sc.custom = true;     // blues scale
            setCustomScales ({ sc });
            p.keyScale = indexOfScale (sc.name);
        }
        for (int b = 0; b < 16; ++b)
            for (int i = 0; i < 16; i += 2)
                if ((b + i) % 3 != 1) { auto& c = p.score.cells[(size_t) (b * 16 + i)]; c.fret[b % 4] = (i / 2 + b) % 9; c.len[b % 4] = 2; }
        std::unique_ptr<AudioProcessorEditor> ed (p.createEditor());
        ed->setSize (1800, 1000);
        snap (*ed, out + "/10_big_16bars.png");
        ed->setSize (1546, 724);
        snap (*ed, out + "/11_min_size.png");
        ScaleStore::file().deleteFile();
    }
    {   // 5. 9-string C major with four chords, the Am chord selected (outline on the fretboard)
        TabMidiProcessor p;
        p.score.reset (4, 9, builtInTunings (9)[0].pitches, "Standard");
        p.keyRoot = 0; p.keyScale = 0; p.duration = 4;
        auto put = [&p] (int cell, std::initializer_list<std::pair<int, int>> notes, int len)
        {
            for (auto& n : notes) { p.score.cells[(size_t) cell].fret[n.first] = n.second; p.score.cells[(size_t) cell].len[n.first] = len; }
        };
        put (0,  { { 4, 3 }, { 5, 2 }, { 6, 0 }, { 7, 1 }, { 8, 0 } }, 4);                 // C
        put (4,  { { 3, 3 }, { 4, 2 }, { 5, 0 }, { 6, 0 }, { 7, 0 }, { 8, 3 } }, 4);       // G
        put (8,  { { 4, 0 }, { 5, 2 }, { 6, 2 }, { 7, 1 }, { 8, 0 } }, 4);                 // Am
        put (12, { { 3, 1 }, { 4, 3 }, { 5, 3 }, { 6, 2 }, { 7, 1 }, { 8, 1 } }, 4);       // F
        put (16, { { 0, 0 } }, 2); put (18, { { 0, 0 } }, 2); put (20, { { 1, 0 } }, 2); put (22, { { 1, 2 } }, 2);
        put (32, { { 3, 0 }, { 4, 2 }, { 5, 0 }, { 6, 1 }, { 7, 0 }, { 8, 0 } }, 8);       // E7 (G# is outside the key)
        std::unique_ptr<AudioProcessorEditor> ed (p.createEditor());
        if (auto* cv = findCanvas (*ed)) cv->select (8, 5);
        snap (*ed, out + "/5_current.png");
        if (auto* cv = findCanvas (*ed)) cv->select (32, 6);
        snap (*ed, out + "/6_outside_key.png");
    }
    {   // 3. hand-written 7-string tab in D Dorian, state round-trip through getStateInformation
        TabMidiProcessor a;
        a.score.reset (8, 7, builtInTunings (7)[1].pitches, "Drop A");
        a.keyRoot = 2; a.keyScale = 2;
        for (int i = 0; i < 16; i += 2) { a.score.cells[(size_t) i].fret[0] = 0; a.score.cells[(size_t) i].len[0] = 2; }
        a.score.cells[4].fret[3] = 2; a.score.cells[4].fret[2] = 2; a.score.cells[4].fret[1] = 0;
        a.score.cells[16].fret[4] = 3; a.score.cells[16].fret[5] = 3; a.score.cells[16].fret[6] = 1; a.score.cells[16].fret[1] = 1; a.score.cells[16].fret[2] = 3; a.score.cells[16].fret[3] = 3;
        MemoryBlock mb; a.getStateInformation (mb);
        TabMidiProcessor b; b.setStateInformation (mb.getData(), (int) mb.getSize());
        std::printf ("state roundtrip: strings=%d bars=%d tuning=%s key=%d/%d notes[0]=%d midi=%s\n", b.score.numStrings, b.score.numBars(),
                     b.score.tuningName.toRawUTF8(), b.keyRoot, b.keyScale, b.score.cells[0].fret[0], b.makeMidiFiles (true).front().getFileName().toRawUTF8());
        std::unique_ptr<AudioProcessorEditor> ed (b.createEditor());
        snap (*ed, out + "/3_dorian_7str.png");
        TuningEditor te (b, nullptr);
        snap (te, out + "/4_tuning.png");
        b.keyRoot = 4; b.keyScale = 1;
        ScaleEditor se (b, false, nullptr);
        snap (se, out + "/12_scale_editor.png");
    }
    return 0;
}
