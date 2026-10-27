#include "PluginEditor.h"

namespace tabmidi
{
static const int kDurations[] = { 16, 8, 4, 2, 1 };          // whole, half, quarter, eighth, sixteenth (in cells)
static const char* kDurLabels[] = { "1", "1/2", "1/4", "1/8", "1/16" };
static const int kMainW = 1040, kPanelW = 490, kWideW = kMainW + kPanelW + 16, kMinH = 724;
static const char* kDurNames[]  = { "ЦЕЛАЯ", "ПОЛОВИНА", "ЧЕТВЕРТЬ", "ВОСЬМАЯ", "ШЕСТНАДЦАТАЯ" };

TabMidiEditor::TabMidiEditor (TabMidiProcessor& p)
    : AudioProcessorEditor (&p), proc (p), canvas (p), fretboard (p, canvas), chordPanel (p)
{
    setLookAndFeel (&lnf);
    setResizable (true, true);                                      // the layout follows the window size
    setResizeLimits (kWideW, kMinH, 3200, 1800);
    setSize (jlimit (kWideW, 3200, p.editorW > 0 ? p.editorW : kWideW),
             jlimit (kMinH, 1800, p.editorH > 0 ? p.editorH : kMinH));

    for (auto* b : { &openBtn, &makeBtn, &saveBtn, &presetBtn }) addAndMakeVisible (b);
    for (auto* b : { &copyBtn, &pasteBtn, &delBarBtn, &undoBtn, &modeBtn }) addAndMakeVisible (b);
    addAndMakeVisible (chordPanel);
    copyBtn.onClick   = [this] { canvas.copyBars();   canvas.grabKeyboardFocus(); };
    pasteBtn.onClick  = [this] { canvas.pasteBars();  canvas.grabKeyboardFocus(); };
    delBarBtn.onClick = [this] { canvas.deleteBars(); canvas.grabKeyboardFocus(); };
    undoBtn.onClick   = [this] { canvas.undo();       canvas.grabKeyboardFocus(); };
    chordPanel.onToggle = [this] (int root, int type)
    {
        if (chordPanel.isSelected (root, type)) { chordPanel.clearSelection(); fretboard.clearChord(); }
        else { chordPanel.setSelection (root, type); fretboard.setChord (root, chordTypes()[(size_t) type]); }
    };
    modeBtn.onClick = [this]
    {
        { const ScopedLock sl (proc.lock); proc.exportSeparate = ! proc.exportSeparate; }
        refreshAll();
        if (dragHandle.ready()) makeMidi();                          // rebuild the files in the new mode
    };
    makeBtn.setColour (TextButton::buttonColourId, col::accent);
    makeBtn.setColour (TextButton::textColourOffId, col::bg);
    openBtn.onClick = [this]
    {
        chooser = std::make_unique<FileChooser> ("Выберите файл Guitar Pro"_ru, File::getSpecialLocation (File::userDocumentsDirectory), "*.gpx;*.gp");
        chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles,
                              [this] (const FileChooser& fc) { if (fc.getResult().existsAsFile()) loadFile (fc.getResult()); });
    };
    makeBtn.onClick = [this] { makeMidi(); };
    saveBtn.onClick = [this] { saveMidiAs(); };
    presetBtn.onClick = [this] { showPresetMenu(); };

    addAndMakeVisible (dragHandle);
    addChildComponent (trackBox);
    trackBox.onChange = [this] { importTrack (trackBox.getSelectedId() - 1, true); };

    for (int v : { 4, 8, 16 })
    {
        auto* b = barBtns.add (new TextButton (String (v)));
        b->setClickingTogglesState (true);
        b->setRadioGroupId (101);
        b->onClick = [this, v] { setBars (v); };
        addAndMakeVisible (b);
    }
    for (int v : { 6, 7, 8, 9 })
    {
        auto* b = strBtns.add (new TextButton (String (v)));
        b->setClickingTogglesState (true);
        b->setRadioGroupId (102);
        b->onClick = [this, v] { setStrings (v); };
        addAndMakeVisible (b);
    }
    for (int i = 0; i < kMaxStrings; ++i)
    {
        auto* b = chipBtns.add (new TextButton());
        b->onClick = [this, b] { openTuningEditor (b); };
        addChildComponent (b);
    }

    for (int i = 0; i < 12; ++i) rootBox.addItem (noteName (i), i + 1);
    { const ScopedLock sl (proc.lock); ScaleStore::mergeIntoBank(); }       // keys saved earlier join the list
    rootBox.onChange = [this] { { const ScopedLock sl (proc.lock); proc.keyRoot = rootBox.getSelectedId() - 1; } chordPanel.clearSelection(); fretboard.clearChord(); repaint(); };
    scaleBox.onChange = [this]
    {
        const int id = scaleBox.getSelectedId();
        if (id == 1000 || id == 1001) { refreshScaleBox(); openScaleEditor (id == 1001); return; }
        { const ScopedLock sl (proc.lock); proc.keyScale = id >= 101 ? builtInScaleCount() + (id - 101) : id - 1; }
        chordPanel.clearSelection(); fretboard.clearChord(); repaint();
    };
    for (int i = 0; i < 5; ++i)
    {
        auto* b = durBtns.add (new TextButton (kDurLabels[i]));
        b->setClickingTogglesState (true);
        b->setRadioGroupId (103);
        b->onClick = [this, i]
        {
            { const ScopedLock sl (proc.lock); proc.duration = kDurations[i]; }
            canvas.applyDurationToSelection();
            repaint();
        };
        addAndMakeVisible (b);
    }
    for (int v : { 4, 8, 16 })                                      // bars visible at once
    {
        auto* b = viewBtns.add (new TextButton (String (v)));
        b->setClickingTogglesState (true);
        b->setRadioGroupId (104);
        b->onClick = [this, v]
        {
            canvas.pushUndo();
            {
                const ScopedLock sl (proc.lock);
                proc.viewBars = v;
                if (proc.score.numBars() < v) { proc.score.setBarCount (v); proc.markEdited(); }   // an empty staff grows to the shown length
            }
            refreshAll();
            viewport.setViewPosition (jmax (0, (int) (TabCanvas::x0 + (float) canvas.selCell * canvas.cellW) - 80), 0);
            canvas.repaint();
        };
        addAndMakeVisible (b);
    }
    for (auto* c : { &rootBox, &scaleBox }) addAndMakeVisible (c);

    viewport.setViewedComponent (&canvas, false);
    viewport.setScrollBarsShown (false, true);
    viewport.setScrollBarThickness (10);
    addAndMakeVisible (viewport);
    addAndMakeVisible (fretboard);

    canvas.onEdited = [this]
    {
        fretboard.repaint();
        updateTabButtons();
        if (dragHandle.ready()) { dragHandle.clearFiles(); midiStatus = "Таб изменён: нажмите MAKE MIDI"_ru; repaint(); }
    };
    canvas.onSelection = [this]
    {
        fretboard.repaint();
        updateTabButtons();
    };
    canvas.onStructure = [this] { refreshAll(); };
    canvas.onInfo = [this] (const String& m) { midiStatus = m; repaint(); };
    fretboard.onPick = [this] (int s, int f) { canvas.toggleNote (s, f); canvas.grabKeyboardFocus(); };

    resized();
    refreshAll();
}

TabMidiEditor::~TabMidiEditor() { setLookAndFeel (nullptr); }

//==============================================================================
bool TabMidiEditor::isInterestedInFileDrag (const StringArray& files)
{
    for (auto& f : files)
        if (File (f).hasFileExtension ("gpx;gp")) return true;
    return false;
}

void TabMidiEditor::filesDropped (const StringArray& files, int, int)
{
    dragOver = false;
    for (auto& f : files)
        if (File (f).hasFileExtension ("gpx;gp")) { loadFile (File (f)); return; }
    repaint();
}

void TabMidiEditor::loadFile (const File& f)
{
    String err;
    auto g = GpFile::load (f, err);
    if (g == nullptr)
    {
        dropError = true; dropTitle = f.getFileName(); dropInfo = err;
        repaint();
        return;
    }
    dropError = false;
    gp = std::move (g);
    currentName = f.getFileNameWithoutExtension();

    // the track with the most notes opens in the editor first; the others stay available in the list
    int best = -1, bestCount = -1;
    trackBox.clear (dontSendNotification);
    for (int i = 0; i < (int) gp->tracks.size(); ++i)
    {
        const int count = gp->noteCount (i);
        trackBox.addItem (gp->tracks[(size_t) i].name + " · "_ru + String (gp->tracks[(size_t) i].tuning.size()) + " str", i + 1);
        if (count > bestCount) { bestCount = count; best = i; }
    }
    trackBox.setVisible (gp->tracks.size() > 1);
    if (best >= 0)
    {
        trackBox.setSelectedId (best + 1, dontSendNotification);
        importTrack (best, false);
    }
    else                                                     // no 6-9 string guitar track: the whole song still goes to MIDI
    {
        ImportResult r;
        gp->importSongOnly (r);
        const int bars = r.totalBars;
        proc.setImported (std::move (r), currentName, false);
        canvas.clearHistory();
        dropTitle = currentName;
        dropInfo = String (bars) + " тактов · "_ru + String (proc.exportTrackCount()) + " дорожек в MIDI (в редактор не подходят)"_ru;
        midiStatus = {};
        dragHandle.clearFiles();
        refreshAll();
    }
}

void TabMidiEditor::importTrack (int index, bool sameSong)
{
    if (gp == nullptr) return;
    ImportResult r;
    if (! gp->importTrack (index, r)) return;

    const int bars = r.totalBars, strings = r.score.numStrings;
    const bool truncated = r.truncated;
    proc.setImported (std::move (r), currentName, sameSong);
    canvas.clearHistory();
    dropTitle = currentName;
    dropInfo = String (bars) + " тактов"_ru + (truncated ? " (в редакторе "_ru + String (kMaxBars) + ")"_ru : String())
             + " · "_ru + String (proc.exportTrackCount()) + " дорожек в MIDI"_ru;
    juce::ignoreUnused (strings);
    midiStatus = {};
    dragHandle.clearFiles();
    canvas.select (0, 0);
    refreshAll();
}

void TabMidiEditor::makeMidi()
{
    bool separate;
    { const ScopedLock sl (proc.lock); separate = proc.exportSeparate; }
    const auto files = proc.makeMidiFiles (separate);
    if (! files.empty())
    {
        dragHandle.setFiles (files);
        midiStatus = files.size() > 1 ? "Готово: "_ru + String ((int) files.size()) + " MIDI-файлов, тяните в DAW"_ru
                                      : files.front().getFileName() + " готов, тяните в DAW"_ru;
    }
    else midiStatus = "Не удалось записать MIDI-файл"_ru;
    repaint();
}

void TabMidiEditor::saveMidiAs()
{
    bool separate;
    { const ScopedLock sl (proc.lock); separate = proc.exportSeparate; }
    const auto files = proc.makeMidiFiles (separate);
    if (files.empty()) return;
    dragHandle.setFiles (files);
    midiStatus = "Готово, выберите куда сохранить"_ru;
    repaint();

    const auto docs = File::getSpecialLocation (File::userDocumentsDirectory);
    if (files.size() == 1)
    {
        const File src = files.front();
        chooser = std::make_unique<FileChooser> ("Сохранить MIDI"_ru, docs.getChildFile (src.getFileName()), "*.mid");
        chooser->launchAsync (FileBrowserComponent::saveMode | FileBrowserComponent::canSelectFiles | FileBrowserComponent::warnAboutOverwriting,
                              [src] (const FileChooser& fc)
                              {
                                  auto dest = fc.getResult();
                                  if (dest != File()) src.copyFileTo (dest.withFileExtension ("mid"));
                              });
    }
    else                                                     // several files: pick a folder for all of them
    {
        chooser = std::make_unique<FileChooser> ("Папка для MIDI-файлов"_ru, docs);
        chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectDirectories,
                              [files] (const FileChooser& fc)
                              {
                                  const auto dir = fc.getResult();
                                  if (dir != File() && dir.isDirectory())
                                      for (auto& f : files) f.copyFileTo (dir.getChildFile (f.getFileName()));
                              });
    }
}

//==============================================================================
void TabMidiEditor::setBars (int n)
{
    canvas.pushUndo();
    { const ScopedLock sl (proc.lock); proc.score.setBarCount (n); proc.viewBars = jmin (proc.viewBars, n); proc.markEdited(); }
    canvas.onEdited();
    refreshAll();
}

void TabMidiEditor::setStrings (int n)
{
    {
        const ScopedLock sl (proc.lock);
        if (proc.score.numStrings == n) return;
        const auto t = builtInTunings (n)[0];
        proc.score.setTuning (t.pitches, t.name);
        proc.markEdited();
    }
    canvas.onEdited();
    refreshAll();
}

void TabMidiEditor::showPresetMenu()
{
    int n;
    { const ScopedLock sl (proc.lock); n = proc.score.numStrings; }
    auto list = builtInTunings (n);
    const int nb = (int) list.size();
    for (auto& t : TuningStore::forStrings (n)) list.push_back (t);

    PopupMenu m;
    for (int i = 0; i < (int) list.size(); ++i)
    {
        if (i == nb) { m.addSeparator(); m.addSectionHeader ("Мои строи"_ru); }
        StringArray a;
        for (int p : list[(size_t) i].pitches) a.add (noteName (p));
        m.addItem (i + 1, list[(size_t) i].name + "    " + a.joinIntoString (" "));
    }
    m.addSeparator();
    m.addItem (1000, "Изменить струны…"_ru);

    Component::SafePointer<TabMidiEditor> safe (this);
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (&presetBtn), [safe, list] (int r)
    {
        if (safe == nullptr || r == 0) return;
        if (r == 1000) { safe->openTuningEditor (&safe->presetBtn); return; }
        const auto& t = list[(size_t) r - 1];
        {
            const ScopedLock sl (safe->proc.lock);
            safe->proc.score.setTuning (t.pitches, t.name);
            safe->proc.markEdited();
        }
        safe->canvas.onEdited();
        safe->refreshAll();
    });
}

void TabMidiEditor::openTuningEditor (Component* from)
{
    Component::SafePointer<TabMidiEditor> safe (this);
    auto content = std::make_unique<TuningEditor> (proc, [safe]
    {
        if (safe == nullptr) return;
        safe->canvas.onEdited();
        safe->refreshAll();
    });
    CallOutBox::launchAsynchronously (std::move (content), from->getScreenBounds(), nullptr);
}

void TabMidiEditor::updateTabButtons()
{
    copyBtn.setEnabled (canvas.hasBarSelection());
    delBarBtn.setEnabled (canvas.hasBarSelection());
    pasteBtn.setEnabled (canvas.hasClipboard());
    undoBtn.setEnabled (canvas.canUndo());
}


void TabMidiEditor::refreshScaleBox()
{
    int cur;
    std::vector<Scale> bank;
    { const ScopedLock sl (proc.lock); cur = proc.keyScale; bank = scales(); }
    const int nb = builtInScaleCount();
    scaleBox.clear (dontSendNotification);
    for (int i = 0; i < nb; ++i) scaleBox.addItem (bank[(size_t) i].name, i + 1);
    if ((int) bank.size() > nb)
    {
        scaleBox.addSeparator();
        scaleBox.addSectionHeading ("Мои тональности"_ru);
        for (size_t i = (size_t) nb; i < bank.size(); ++i) scaleBox.addItem (bank[i].name, 101 + (int) i - nb);
    }
    scaleBox.addSeparator();
    scaleBox.addItem ("+ Новая тональность…"_ru, 1000);
    scaleBox.addItem ("Изменить выбранную…"_ru, 1001);
    scaleBox.setItemEnabled (1001, cur >= nb);
    scaleBox.setSelectedId (cur >= nb ? 101 + cur - nb : cur + 1, dontSendNotification);
}

void TabMidiEditor::openScaleEditor (bool editCurrent)
{
    Component::SafePointer<TabMidiEditor> safe (this);
    auto content = std::make_unique<ScaleEditor> (proc, editCurrent, [safe]
    {
        if (safe == nullptr) return;
        safe->chordPanel.clearSelection();
        safe->fretboard.clearChord();
        safe->refreshAll();
    });
    CallOutBox::launchAsynchronously (std::move (content), scaleBox.getScreenBounds(), nullptr);
}

void TabMidiEditor::refreshAll()
{
    int n, bars, root, scale, dur;
    String tname;
    {
        const ScopedLock sl (proc.lock);
        n = proc.score.numStrings; bars = proc.score.numBars(); tname = proc.score.tuningName;
        root = proc.keyRoot; scale = proc.keyScale; dur = proc.duration;
        for (int i = 0; i < kMaxStrings; ++i)
        {
            chipBtns[i]->setVisible (i < n);
            if (i < n) chipBtns[i]->setButtonText (noteName (proc.score.tuning[i]));
        }
    }
    int k = 0;
    for (int v : { 4, 8, 16 }) barBtns[k++]->setToggleState (bars == v, dontSendNotification);
    k = 0;
    for (int v : { 6, 7, 8, 9 }) strBtns[k++]->setToggleState (n == v, dontSendNotification);
    presetBtn.setButtonText (tname + " " + String::fromUTF8 ("\xe2\x96\xbe"));
    rootBox.setSelectedId (root + 1, dontSendNotification);
    scaleBox.setSelectedId (scale + 1, dontSendNotification);
    for (int i = 0; i < 5; ++i) durBtns[i]->setToggleState (kDurations[i] == dur, dontSendNotification);
    { int k2 = 0; for (int v : { 4, 8, 16 }) viewBtns[k2++]->setToggleState (proc.viewBars == v, dontSendNotification); }
    modeBtn.setButtonText (proc.exportSeparate ? "Раздельно"_ru : "Одним файлом"_ru);
    refreshScaleBox();
    chordPanel.repaint();
    canvas.refreshSize();
    canvas.repaint();
    fretboard.repaint();
    updateTabButtons();
    repaint();
}

//==============================================================================
void TabMidiEditor::resized()
{
    const int pad = 16, gap = 12;
    const int W = getWidth(), H = getHeight();
    { const ScopedLock sl (proc.lock); proc.editorW = W; proc.editorH = H; }
    const int mainW = W - kPanelW - 16;                // main area is [0, mainW], the chord panel sits to its right
    const int right = mainW - pad;

    rDrag = { right - 210, pad, 210, 72 };
    makeBtn.setBounds (rDrag.getX() - gap - 180, pad, 180, 72);
    rDrop = { pad, pad, makeBtn.getX() - gap - pad, 72 };
    dragHandle.setBounds (rDrag.getX() + 8, rDrag.getY() + 7, rDrag.getWidth() - 16, 36);
    saveBtn.setBounds (rDrag.getX() + 8, rDrag.getY() + 47, (rDrag.getWidth() - 22) / 2, 20);
    modeBtn.setBounds (saveBtn.getRight() + 6, rDrag.getY() + 47, (rDrag.getWidth() - 22) / 2, 20);
    openBtn.setBounds (rDrop.getRight() - 12 - 100, rDrop.getY() + 22, 100, 28);
    trackBox.setBounds (openBtn.getX() - 8 - 190, rDrop.getY() + 22, 190, 28);

    rControls = { pad, 100, mainW - 2 * pad, 72 };
    rTab = { pad, 184, mainW - 2 * pad, 216 };
    rFret = { pad, 412, mainW - 2 * pad, jmax (296, H - 412 - pad) };   // extra window height goes to the fretboard

    const int cy = rControls.getY() + 30;
    int x = rControls.getX() + 16;
    xBars = x;
    for (auto* b : barBtns) { b->setBounds (x, cy, 36, 32); x += 40; }
    x += 16; xStrings = x;
    for (auto* b : strBtns) { b->setBounds (x, cy, 36, 32); x += 40; }
    x += 16; xTuning = x;
    presetBtn.setBounds (x, cy, 112, 32);
    x += 118;
    for (auto* b : chipBtns) { b->setBounds (x, cy, 34, 32); x += 36; }
    xDur = rControls.getRight() - 16 - 214;
    {
        const int w[] = { 34, 40, 40, 40, 48 };                                 // "1/16" needs the widest button
        int dx = xDur;
        for (int i = 0; i < durBtns.size(); ++i) { durBtns[i]->setBounds (dx, cy, w[i], 32); dx += w[i] + 3; }
    }
    rootBox.setBounds (rFret.getX() + 14, rFret.getY() + 6, 62, 28);          // key picker sits above the fretboard
    scaleBox.setBounds (rFret.getX() + 82, rFret.getY() + 6, 170, 28);

    xView = rTab.getX() + 150;
    for (int i = 0; i < viewBtns.size(); ++i) viewBtns[i]->setBounds (xView + 104 + i * 38, rTab.getY() + 7, 34, 24);
    {
        const int bw[] = { 100, 84, 100, 88 };
        Button* arr[] = { &copyBtn, &pasteBtn, &delBarBtn, &undoBtn };
        int bx = rTab.getRight() - 14;
        for (int i = 3; i >= 0; --i) { bx -= bw[i]; arr[i]->setBounds (bx, rTab.getY() + 7, bw[i], 24); bx -= 6; }
    }
    chordPanel.setBounds (mainW, pad, kPanelW, H - 2 * pad);

    viewport.setBounds (rTab.reduced (8, 0).withTrimmedTop (36).withTrimmedBottom (8));
    fretboard.setBounds (rFret.reduced (10, 0).withTrimmedTop (38).withTrimmedBottom (8));
    canvas.refreshSize();                                                       // bar width depends on the window width
}

void TabMidiEditor::paint (Graphics& g)
{
    g.fillAll (col::bg);

    auto panel = [&g] (Rectangle<int> r, Colour border = col::line)
    {
        g.setColour (col::panel);
        g.fillRoundedRectangle (r.toFloat(), 10.0f);
        g.setColour (border);
        g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 10.0f, 1.0f);
    };
    auto label = [&g] (const String& t, int x, int y, int w = 300, Colour c = col::muted)
    {
        g.setColour (c);
        g.setFont (fnt (11.0f, true));
        g.drawText (t, x, y, w, 14, Justification::left, false);
    };

    // file drop area
    panel (rDrop, dragOver ? col::accent : col::line);
    {
        Path p, dashed;
        p.addRoundedRectangle (rDrop.toFloat().reduced (1.5f), 10.0f);
        const float dl[] = { 5.0f, 4.0f };
        PathStrokeType (1.3f).createDashedStroke (dashed, p, dl, 2);
        g.setColour (dragOver ? col::accent : col::btnLine);
        g.fillPath (dashed);
    }
    const int tw = rDrop.getWidth() - 32 - 116 - (trackBox.isVisible() ? 198 : 0);
    g.setColour (col::text);
    g.setFont (fnt (14.0f, true));
    g.drawText (dropTitle, rDrop.getX() + 16, rDrop.getY() + 14, tw, 20, Justification::left, true);
    g.setColour (dropError ? Colour (0xffff7b72) : col::muted);
    g.setFont (fnt (12.0f));
    g.drawText (dropInfo, rDrop.getX() + 16, rDrop.getY() + 38, tw, 18, Justification::left, true);

    panel (rDrag, dragHandle.ready() ? col::accent : col::line);
    panel (rControls);

    label ("ТАКТЫ"_ru, xBars, rControls.getY() + 10);
    label ("СТРУНЫ"_ru, xStrings, rControls.getY() + 10);
    label ("СТРОЙ · НОТЫ СТРУН СНИЗУ ВВЕРХ"_ru, xTuning, rControls.getY() + 10);
    {
        int di = 2;
        { const ScopedLock sl (proc.lock); for (int i = 0; i < 5; ++i) if (kDurations[i] == proc.duration) di = i; }
        label ("ДЛИТЕЛЬНОСТЬ · "_ru + String::fromUTF8 (kDurNames[di]), xDur, rControls.getY() + 10, 220);
    }

    // tab panel
    panel (rTab);
    int bars, root, scale;
    { const ScopedLock sl (proc.lock); bars = proc.score.numBars(); root = proc.keyRoot; scale = proc.keyScale; }
    label ("ТАБ · "_ru + String (bars) + " " + (bars % 10 == 1 && bars != 11 ? "ТАКТ"_ru : (bars % 10 >= 2 && bars % 10 <= 4 && (bars < 10 || bars > 20)) ? "ТАКТА"_ru : "ТАКТОВ"_ru),
           rTab.getX() + 14, rTab.getY() + 12, 160);
    label ("ВИДНО ТАКТОВ"_ru, xView, rTab.getY() + 12, 100);
    g.setFont (fnt (12.0f));
    g.setColour (midiStatus.isNotEmpty() ? col::accent : col::muted);
    g.drawText (midiStatus.isNotEmpty() ? midiStatus : String ("Клик по номеру такта: выделить такт"_ru),
                xView + 104 + 3 * 38 + 10, rTab.getY() + 9, copyBtn.getX() - 8 - (xView + 104 + 3 * 38 + 10), 20, Justification::left, true);

    // fretboard panel
    panel (rFret);
}
} // namespace tabmidi
