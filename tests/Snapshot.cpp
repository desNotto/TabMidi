// Developer tool: renders the plugin window to PNG files without a DAW.
#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"
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
        snap (*ed, out + "/2_imported.png");
        proc.makeMidiFile();
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
                     b.score.tuningName.toRawUTF8(), b.keyRoot, b.keyScale, b.score.cells[0].fret[0], b.makeMidiFile().getFileName().toRawUTF8());
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
