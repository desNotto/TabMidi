#pragma once
#include "PluginProcessor.h"
#include <array>

namespace tabmidi
{
namespace col
{
inline const Colour bg      { 0xff16181d };
inline const Colour panel   { 0xff20242b };
inline const Colour line    { 0xff2f353f };
inline const Colour text    { 0xffe7eaf0 };
inline const Colour muted   { 0xff9aa3b2 };
inline const Colour accent  { 0xfff5a524 };
inline const Colour btn     { 0xff2a303a };
inline const Colour btnLine { 0xff3a4250 };
inline const Colour dot     { 0xff3a4a63 };
inline const Colour deg     { 0xffffcf7a };
inline const Colour strLine { 0xff6b7586 };
inline const Colour chordTone { 0xff2f6fd0 };
}

inline Font fnt  (float h, bool bold = false) { return Font (FontOptions (h, bold ? Font::bold : Font::plain)); }
inline Font mono (float h, bool bold = false) { return Font (FontOptions (Font::getDefaultMonospacedFontName(), h, bold ? Font::bold : Font::plain)); }

struct TabLnF : LookAndFeel_V4
{
    TabLnF()
    {
        setColour (ResizableWindow::backgroundColourId, col::bg);
        setColour (TextButton::buttonColourId, col::btn);
        setColour (TextButton::buttonOnColourId, col::accent);
        setColour (TextButton::textColourOffId, col::text);
        setColour (TextButton::textColourOnId, col::bg);
        setColour (ComboBox::backgroundColourId, col::btn);
        setColour (ComboBox::outlineColourId, col::btnLine);
        setColour (ComboBox::textColourId, col::text);
        setColour (ComboBox::arrowColourId, col::muted);
        setColour (PopupMenu::backgroundColourId, col::panel);
        setColour (PopupMenu::textColourId, col::text);
        setColour (PopupMenu::highlightedBackgroundColourId, col::accent);
        setColour (PopupMenu::highlightedTextColourId, col::bg);
        setColour (Label::textColourId, col::text);
        setColour (TextEditor::backgroundColourId, col::bg);
        setColour (TextEditor::textColourId, col::text);
        setColour (TextEditor::outlineColourId, col::btnLine);
        setColour (TextEditor::focusedOutlineColourId, col::accent);
        setColour (ScrollBar::thumbColourId, col::btnLine);
        setColour (BubbleComponent::backgroundColourId, col::panel);
        setColour (BubbleComponent::outlineColourId, col::btnLine);
    }
};

//==============================================================================
/** The tab staff: grid of 16th-note cells, one row per string (highest string on top).
    Click a bar number to select the whole bar (Shift = range): Delete / Ctrl+C / Ctrl+X / Ctrl+V work on it. Ctrl+Z undoes. */
class TabCanvas : public Component
{
public:
    static constexpr float x0 = 44.0f, top = 32.0f;

    explicit TabCanvas (TabMidiProcessor& p) : proc (p) { setWantsKeyboardFocus (true); }

    std::function<void()> onEdited;                  // notes changed
    std::function<void()> onSelection;               // selection / clipboard / undo state changed
    std::function<void()> onStructure;               // bar count may have changed (undo)
    std::function<void (const String&)> onInfo;      // short status message
    int selCell = 0, selStr = 0;
    float cellW = 14.7f;                             // 4 bars exactly fill the window; longer scores scroll

    static float rowSp (int n) { return n <= 6 ? 18.0f : n == 7 ? 17.0f : n == 8 ? 15.0f : 14.0f; }

    bool hasBarSelection() const { return barA >= 0; }
    bool hasClipboard() const    { return ! clipBars.empty(); }
    bool canUndo() const         { return ! undoStack.empty(); }

    void refreshSize()
    {
        const ScopedLock sl (proc.lock);
        const int n = proc.score.numStrings, total = proc.score.totalCells();
        selCell = jlimit (0, jmax (0, total - 1), selCell);
        selStr = jlimit (0, n - 1, selStr);
        if (barA >= proc.score.numBars() || barB >= proc.score.numBars()) barA = barB = -1;
        float viewW = 990.0f;
        if (auto* v = findParentComponentOfClass<Viewport>()) viewW = (float) v->getMaximumVisibleWidth();
        const int shown = jmin (proc.viewBars, proc.score.numBars());          // bars visible at once
        int cellsShown = 0;
        for (int b = 0; b < shown; ++b) cellsShown += proc.score.barCells[(size_t) b];
        const float fit = (viewW - x0 - 12.0f) / (float) jmax (1, cellsShown);
        cellW = jlimit (2.5f, 26.0f, std::floor (fit * 10.0f) / 10.0f);
        setSize ((int) (x0 + (float) total * cellW + 12.0f), (int) (top + (float) (n - 1) * rowSp (n) + 14.0f));
    }

    void select (int cell, int str)
    {
        {
            const ScopedLock sl (proc.lock);
            selCell = jlimit (0, jmax (0, proc.score.totalCells() - 1), cell);
            selStr = jlimit (0, proc.score.numStrings - 1, str);
            barA = barB = -1;
        }
        ensureVisible();
        repaint();
        if (onSelection) onSelection();
    }

    void selectBar (int bar, bool extend)
    {
        {
            const ScopedLock sl (proc.lock);
            bar = jlimit (0, proc.score.numBars() - 1, bar);
            if (extend && barA >= 0) barB = bar; else barA = barB = bar;
            selCell = proc.score.barStart (jmin (barA, barB));
        }
        ensureVisible();
        repaint();
        if (onSelection) onSelection();
    }

    /** (string, fret) of every note in the selected column. */
    std::vector<std::pair<int, int>> selectedNotes() const
    {
        const ScopedLock sl (proc.lock);
        std::vector<std::pair<int, int>> r;
        if (barA < 0 && selCell < proc.score.totalCells())
            for (int s = 0; s < proc.score.numStrings; ++s)
                if (proc.score.cells[(size_t) selCell].fret[s] >= 0)
                    r.push_back ({ s, proc.score.cells[(size_t) selCell].fret[s] });
        return r;
    }

    /** Length (in cells) of the notes in the selected column, 0 when it is empty. */
    int selectedLength() const
    {
        const ScopedLock sl (proc.lock);
        if (selCell < proc.score.totalCells())
            for (int s = 0; s < proc.score.numStrings; ++s)
                if (proc.score.cells[(size_t) selCell].fret[s] >= 0)
                    return proc.score.cells[(size_t) selCell].len[s];
        return 0;
    }

    //==========================================================================
    void pushUndo()
    {
        const ScopedLock sl (proc.lock);
        undoStack.push_back ({ proc.score.cells, proc.score.barCells });
        if (undoStack.size() > 40) undoStack.erase (undoStack.begin());
    }

    void clearHistory() { undoStack.clear(); barA = barB = -1; }

    void undo()
    {
        {
            const ScopedLock sl (proc.lock);
            if (undoStack.empty()) return;
            proc.score.cells = undoStack.back().cells;
            proc.score.barCells = undoStack.back().barCells;
            undoStack.pop_back();
            proc.markEdited();
            barA = barB = -1;
        }
        refreshSize();
        repaint();
        if (onStructure) onStructure();
        if (onEdited) onEdited();
        if (onSelection) onSelection();
        info ("Отменено"_ru);
    }

    void putNote (int str, int fret)
    {
        pushUndo();
        {
            const ScopedLock sl (proc.lock);
            auto& c = proc.score.cells[(size_t) selCell];
            if (c.fret[str] < 0)
            {
                int len = proc.duration;                       // a new note in a chord joins the chord's length
                for (int s = 0; s < proc.score.numStrings; ++s)
                    if (s != str && c.fret[s] >= 0) { len = c.len[s]; break; }
                c.len[str] = len;
            }
            c.fret[str] = fret;
            selStr = str;
            barA = barB = -1;
            proc.markEdited();
        }
        edited();
    }

    /** Fretboard click: adds the note, or removes it when the same note is already in the selected column. */
    void toggleNote (int str, int fret)
    {
        bool present;
        { const ScopedLock sl (proc.lock); present = proc.score.cells[(size_t) selCell].fret[str] == fret; selStr = str; }
        if (! present) { putNote (str, fret); return; }
        pushUndo();
        {
            const ScopedLock sl (proc.lock);
            proc.score.cells[(size_t) selCell].fret[str] = -1;
            barA = barB = -1;
            proc.markEdited();
        }
        edited();
    }

    void clearNote()
    {
        pushUndo();
        {
            const ScopedLock sl (proc.lock);
            proc.score.cells[(size_t) selCell].fret[selStr] = -1;
            proc.markEdited();
        }
        edited();
    }

    /** Sets the length of every note in the selected column (like a beat in Guitar Pro). */
    void applyDurationToSelection()
    {
        if (selectedLength() == 0) return;
        pushUndo();
        {
            const ScopedLock sl (proc.lock);
            auto& c = proc.score.cells[(size_t) selCell];
            for (int s = 0; s < proc.score.numStrings; ++s)
                if (c.fret[s] >= 0) c.len[s] = proc.duration;
            proc.markEdited();
        }
        edited();
    }

    //==========================================================================
    bool copyBars()
    {
        int n;
        {
            const ScopedLock sl (proc.lock);
            if (barA < 0) return false;
            clipCells.clear(); clipBars.clear();
            for (int b = jmin (barA, barB); b <= jmax (barA, barB); ++b)
            {
                const int st = proc.score.barStart (b), cnt = proc.score.barCells[(size_t) b];
                clipBars.push_back (cnt);
                clipCells.insert (clipCells.end(), proc.score.cells.begin() + st, proc.score.cells.begin() + st + cnt);
            }
            n = (int) clipBars.size();
        }
        if (onSelection) onSelection();
        info ("Скопировано тактов: "_ru + String (n));
        return true;
    }

    void deleteBars()
    {
        if (barA < 0) return;
        pushUndo();
        int n;
        {
            const ScopedLock sl (proc.lock);
            for (int b = jmin (barA, barB); b <= jmax (barA, barB); ++b)
            {
                const int st = proc.score.barStart (b);
                for (int j = 0; j < proc.score.barCells[(size_t) b]; ++j) proc.score.cells[(size_t) (st + j)] = Cell();
            }
            n = jmax (barA, barB) - jmin (barA, barB) + 1;
            proc.markEdited();
        }
        edited();
        info ("Очищено тактов: "_ru + String (n));
    }

    void pasteBars()
    {
        if (clipBars.empty()) return;
        pushUndo();
        int pasted = 0;
        {
            const ScopedLock sl (proc.lock);
            const int first = barA >= 0 ? jmin (barA, barB) : proc.score.barOfCell (selCell);
            size_t src = 0;
            for (size_t i = 0; i < clipBars.size(); ++i)
            {
                const int b = first + (int) i;
                const int sn = clipBars[i];
                if (b < proc.score.numBars())
                {
                    const int dst = proc.score.barStart (b), dn = proc.score.barCells[(size_t) b];
                    for (int j = 0; j < dn; ++j)
                        proc.score.cells[(size_t) (dst + j)] = j < sn ? clipCells[src + (size_t) j] : Cell();
                    ++pasted;
                }
                src += (size_t) sn;
            }
            barA = first; barB = first + jmax (0, pasted - 1);
            selCell = proc.score.barStart (first);
            proc.markEdited();
        }
        edited();
        info (pasted == (int) clipBars.size() ? "Вставлено тактов: "_ru + String (pasted)
                                              : "Вставлено тактов: "_ru + String (pasted) + " из " + String ((int) clipBars.size()) + " (дальше нет места)"_ru);
    }

    //==========================================================================
    void paint (Graphics& g) override
    {
        const ScopedLock sl (proc.lock);
        const auto& sc = proc.score;
        const int n = sc.numStrings;
        const float sp = rowSp (n);
        auto rowY = [&] (int s) { return top + (float) (n - 1 - s) * sp; };
        const float yTop = top, yBot = rowY (0), xEnd = x0 + (float) sc.totalCells() * cellW;

        // selected bars
        if (barA >= 0)
        {
            const int lo = jmin (barA, barB), hi = jmax (barA, barB);
            const float xa = x0 + (float) sc.barStart (lo) * cellW;
            const float xb = x0 + (float) (sc.barStart (hi) + sc.barCells[(size_t) hi]) * cellW;
            g.setColour (col::accent.withAlpha (0.10f));
            g.fillRoundedRectangle (xa, 1.0f, xb - xa, yBot + 12.0f, 5.0f);
            g.setColour (col::accent.withAlpha (0.6f));
            g.drawRoundedRectangle (xa + 0.5f, 1.5f, xb - xa - 1.0f, yBot + 11.0f, 5.0f, 1.2f);
        }
        else
        {
            g.setColour (col::accent.withAlpha (0.12f));
            g.fillRoundedRectangle (x0 + (float) selCell * cellW + 1.0f, yTop - 9.0f, jmax (2.0f, cellW - 2.0f), yBot - yTop + 18.0f, 4.0f);
            g.setColour (col::accent);
            g.drawRoundedRectangle (x0 + (float) selCell * cellW + 1.5f, rowY (selStr) - sp * 0.5f, jmax (1.5f, cellW - 3.0f), sp, 3.0f, 1.2f);
        }

        // strings
        g.setColour (col::strLine.withAlpha (0.55f));
        for (int s = 0; s < n; ++s) g.drawHorizontalLine (roundToInt (rowY (s)), x0, xEnd);

        // bars and beats
        int c = 0;
        for (int b = 0; b < sc.numBars(); ++b)
        {
            const float x = x0 + (float) c * cellW;
            for (int k = 4; k < sc.barCells[(size_t) b]; k += 4)
            {
                g.setColour (col::line);
                g.drawVerticalLine (roundToInt (x + (float) k * cellW), yTop - 4.0f, yBot + 4.0f);
            }
            g.setColour (col::muted);
            g.drawVerticalLine (roundToInt (x), yTop, yBot);
            const bool isSel = barA >= 0 && b >= jmin (barA, barB) && b <= jmax (barA, barB);
            const float tw = b >= 9 ? 22.0f : 16.0f;
            g.setColour (isSel ? col::accent : col::btn);
            g.fillRoundedRectangle (x + 3.0f, 2.0f, tw, 11.0f, 3.0f);
            g.setColour (isSel ? col::bg : col::muted);
            g.setFont (fnt (10.0f, true));
            g.drawText (String (b + 1), Rectangle<float> (x + 3.0f, 2.0f, tw, 11.0f), Justification::centred, false);
            c += sc.barCells[(size_t) b];
        }
        g.setColour (col::muted);
        g.drawVerticalLine (roundToInt (xEnd) - 1, yTop, yBot);

        g.setFont (fnt (12.0f, true));
        g.setColour (col::muted);
        const float mid = (yTop + yBot) * 0.5f;
        g.drawText ("T", 10, roundToInt (mid) - 20, 20, 14, Justification::centred, false);
        g.drawText ("A", 10, roundToInt (mid) - 6, 20, 14, Justification::centred, false);
        g.drawText ("B", 10, roundToInt (mid) + 8, 20, 14, Justification::centred, false);

        // notes (digits shrink when many bars are shown at once)
        const float fs = jlimit (8.0f, 11.5f, cellW * 0.8f + 2.0f);
        float lastChordRight = -1000.0f;
        for (int i = 0; i < sc.totalCells(); ++i)
        {
            const auto& cell = sc.cells[(size_t) i];
            const float cx = x0 + ((float) i + 0.5f) * cellW;
            int count = 0;
            for (int s = 0; s < n; ++s)
            {
                if (cell.fret[s] < 0) continue;
                ++count;
                const float y = rowY (s);
                g.setColour (col::accent.withAlpha (0.45f));
                g.drawHorizontalLine (roundToInt (y), cx, x0 + (float) (i + cell.len[s]) * cellW - 3.0f);
                const String t (cell.fret[s]);
                const float w = (float) t.length() * fs * 0.62f + 2.0f;
                g.setColour (col::panel);
                g.fillRect (cx - w * 0.5f, y - fs * 0.6f, w, fs * 1.2f);
                g.setColour (col::text);
                g.setFont (mono (fs, true));
                g.drawText (t, Rectangle<float> (cx - 12.0f, y - 7.0f, 24.0f, 14.0f), Justification::centred, false);
            }
            if (count > 1)
            {
                const String nm = chordName (sc.pitchesAt (i));
                const float tw = (float) nm.length() * 7.4f + 8.0f;
                if (nm.isNotEmpty() && cx - tw * 0.5f >= lastChordRight)     // labels that would collide are skipped
                {
                    g.setColour (col::accent);
                    g.setFont (fnt (13.0f, true));
                    g.drawText (nm, Rectangle<float> (cx - 45.0f, 15.0f, 90.0f, 16.0f), Justification::centred, false);
                    lastChordRight = cx + tw * 0.5f;
                }
            }
        }
    }

    void mouseDown (const MouseEvent& e) override
    {
        grabKeyboardFocus();
        if (e.position.y < 14.0f)                                   // strip with the bar numbers
        {
            int bar;
            {
                const ScopedLock sl (proc.lock);
                const int c = jlimit (0, proc.score.totalCells() - 1, (int) std::floor ((e.position.x - x0) / cellW));
                bar = proc.score.barOfCell (c);
            }
            selectBar (bar, e.mods.isShiftDown());
            return;
        }
        {
            const ScopedLock sl (proc.lock);
            const int n = proc.score.numStrings;
            const int c = (int) std::floor ((e.position.x - x0) / cellW);
            const int r = roundToInt ((e.position.y - top) / rowSp (n));
            selCell = jlimit (0, proc.score.totalCells() - 1, c);
            selStr = jlimit (0, n - 1, n - 1 - jlimit (0, n - 1, r));
            barA = barB = -1;
        }
        if (e.mods.isPopupMenu()) clearNote();
        repaint();
        if (onSelection) onSelection();
    }

    bool keyPressed (const KeyPress& k) override
    {
        const juce_wchar ch = k.getTextCharacter();
        const int code = k.getKeyCode();
        if (k.getModifiers().isCommandDown())
        {
            if (code == 'C') { copyBars(); return true; }
            if (code == 'X') { if (copyBars()) deleteBars(); return true; }
            if (code == 'V') { pasteBars(); return true; }
            if (code == 'Z') { undo(); return true; }
            return false;
        }
        if (ch >= '0' && ch <= '9') { typeDigit (ch - '0'); return true; }
        if (code == KeyPress::leftKey)  { move (-1, 0); return true; }
        if (code == KeyPress::rightKey) { move (1, 0); return true; }
        if (code == KeyPress::upKey)    { move (0, 1); return true; }
        if (code == KeyPress::downKey)  { move (0, -1); return true; }
        if (code == KeyPress::returnKey || code == KeyPress::spaceKey) { move (jmax (1, selectedLength() > 0 ? selectedLength() : proc.duration), 0); return true; }
        if (code == KeyPress::deleteKey || code == KeyPress::backspaceKey) { if (barA >= 0) deleteBars(); else clearNote(); return true; }
        if (code == KeyPress::homeKey) { select (0, selStr); return true; }
        return false;
    }

    void mouseWheelMove (const MouseEvent&, const MouseWheelDetails& w) override
    {
        if (auto* v = findParentComponentOfClass<Viewport>())
        {
            const float d = std::abs (w.deltaX) > std::abs (w.deltaY) ? w.deltaX : w.deltaY;
            v->setViewPosition (jmax (0, v->getViewPositionX() - roundToInt (d * 500.0f)), 0);
        }
    }

private:
    struct UndoState { std::vector<Cell> cells; std::vector<int> barCells; };

    void info (const String& m) { if (onInfo) onInfo (m); }
    void edited()
    {
        repaint();
        if (onEdited) onEdited();
        if (onSelection) onSelection();
    }
    void move (int dc, int ds) { select (selCell + dc, selStr + ds); }

    void typeDigit (int d)
    {
        const uint32 now = Time::getMillisecondCounter();
        int fret = d;
        {
            const ScopedLock sl (proc.lock);
            const int cur = proc.score.cells[(size_t) selCell].fret[selStr];
            if (now - lastDigitMs < 900 && lastCell == selCell && lastStr == selStr && cur >= 0 && cur * 10 + d <= 36)
                fret = cur * 10 + d;
        }
        putNote (selStr, fret);
        lastDigitMs = now; lastCell = selCell; lastStr = selStr;
    }

    void ensureVisible()
    {
        if (auto* v = findParentComponentOfClass<Viewport>())
        {
            const int x = roundToInt (x0 + (float) selCell * cellW);
            const int vx = v->getViewPositionX(), vw = v->getViewWidth();
            if (x < vx + 40)             v->setViewPosition (jmax (0, x - 60), 0);
            else if (x > vx + vw - 80)   v->setViewPosition (x - vw + 120, 0);
        }
    }

    TabMidiProcessor& proc;
    std::vector<UndoState> undoStack;
    int barA = -1, barB = -1;                          // selected bars, -1 = none
    static inline std::vector<Cell> clipCells;         // shared between plugin instances
    static inline std::vector<int> clipBars;
    uint32 lastDigitMs = 0;
    int lastCell = -1, lastStr = -1;
};

//==============================================================================
/** Virtual fretboard with 24 frets. Fret widths shrink like on a real guitar. Shows the notes of the key
    (name + scale degree), or, while a chord from the helper panel is active, the notes of that chord
    (name + chord degree). Open strings always show their note, notes of the selected tab column get a white outline. */
class FretboardView : public Component
{
public:
    static constexpr int kFrets = 24;
    std::function<void (int stringIdx, int fret)> onPick;

    FretboardView (TabMidiProcessor& p, const TabCanvas& c) : proc (p), canvas (c) {}

    void setChord (int rootPc, const ChordType& t) { chordRoot = rootPc; chordType = &t; repaint(); }
    void clearChord() { chordRoot = -1; chordType = nullptr; repaint(); }

    void paint (Graphics& g) override
    {
        const ScopedLock sl (proc.lock);
        const auto& sc = proc.score;
        const int n = sc.numStrings;
        const auto geo = geometry (n);
        const float yTop = geo.y0, yBot = geo.y0 + (float) n * geo.sp;

        g.setColour (col::btnLine);
        for (int f = 1; f <= kFrets; ++f) g.drawVerticalLine (roundToInt (fretX (geo, f)), yTop, yBot);
        g.setColour (col::muted);
        g.fillRect (geo.nutX - 2.0f, yTop, 4.0f, yBot - yTop);

        for (int s = 0; s < n; ++s)
        {
            g.setColour (col::strLine);
            const float th = 1.0f + 1.6f * (1.0f - (float) s / (float) (n - 1));
            g.fillRect (geo.nutX, geo.yOf (s, n) - th * 0.5f, (float) getWidth() - geo.nutX - 6.0f, th);
        }

        g.setFont (fnt (10.5f));
        g.setColour (col::muted);
        for (int f : { 3, 5, 7, 9, 12, 15, 17, 19, 21, 24 })
        {
            const float cx = (fretX (geo, f - 1) + fretX (geo, f)) * 0.5f;
            g.drawText (String (f), Rectangle<float> (cx - 12.0f, yBot + 3.0f, 24.0f, 14.0f), Justification::centred, false);
        }

        const bool chordMode = chordType != nullptr;
        forEachPill (n, [&] (int s, int f, const Marker& m, Rectangle<float> r, bool selected)
                     { drawPill (g, r, m, sc.pitchOf (s, f), chordMode, selected); });
    }

    void mouseDown (const MouseEvent& e) override
    {
        int hitS = -1, hitF = -1;
        {
            const ScopedLock sl (proc.lock);
            forEachPill (proc.score.numStrings, [&] (int s, int f, const Marker&, Rectangle<float> r, bool)
                         { if (r.contains (e.position)) { hitS = s; hitF = f; } });
        }
        if (hitS >= 0 && onPick) onPick (hitS, hitF);
    }

private:
    struct Marker { int kind = 0; String deg; };       // kind: 0 outside, 1 root, 2 other note

    struct Geo
    {
        float nutX, span, sp, y0;
        float yOf (int s, int n) const { return y0 + (float) (n - 1 - s) * sp + sp * 0.5f; }
    };

    Geo geometry (int n) const
    {
        Geo g;
        g.nutX = 56.0f;
        g.span = (float) getWidth() - g.nutX - 8.0f;
        g.sp = jmin (46.0f, ((float) getHeight() - 22.0f) / (float) n);
        g.y0 = 2.0f;
        return g;
    }

    /** Distance of fret f from the nut: every fret is 2^(-1/12) shorter than the previous one, like on a real neck. */
    static float fretX (const Geo& g, int f)
    {
        static const double q = std::pow (2.0, -1.0 / 12.0);
        static const double norm = 1.0 - std::pow (q, (double) kFrets);
        return g.nutX + (float) ((double) g.span * (1.0 - std::pow (q, (double) f)) / norm);
    }

    Rectangle<float> pillRect (const Geo& g, int n, int s, int f) const
    {
        const float cy = g.yOf (s, n);
        if (f == 0)
        {
            const float h = jmin (24.0f, g.sp - 3.0f);
            return { g.nutX - 53.0f, cy - h * 0.5f, 46.0f, h };
        }
        const float xa = fretX (g, f - 1), xb = fretX (g, f);
        const float pw = jlimit (17.0f, 46.0f, xb - xa - 3.0f);
        const float h = pw >= 40.0f ? jmin (24.0f, g.sp - 3.0f) : jmin (28.0f, g.sp - 2.0f);
        return { (xa + xb) * 0.5f - pw * 0.5f, cy - h * 0.5f, pw, h };
    }

    void drawPill (Graphics& g, Rectangle<float> r, const Marker& m, int midi, bool chordMode, bool selected) const
    {
        auto outline = [&g] (Rectangle<float> b)
        {
            const auto o = b.expanded (2.5f);
            g.setColour (Colours::white);
            g.drawRoundedRectangle (o, jmin (o.getWidth(), o.getHeight()) * 0.5f, 2.0f);
        };

        if (m.kind == 0)                                       // outside the key: open string or a note from the tab
        {
            const auto small = r.withSizeKeepingCentre (jmin (32.0f, r.getWidth()), r.getHeight());
            const float rad = jmin (small.getWidth(), small.getHeight()) * 0.5f;
            g.setColour (col::panel);
            g.fillRoundedRectangle (small, rad);
            g.setColour (col::btnLine);
            g.drawRoundedRectangle (small.reduced (0.5f), rad, 1.2f);
            g.setFont (fnt (jlimit (9.0f, 12.0f, small.getWidth() * 0.45f), true));
            g.setColour (col::muted);
            g.drawText (noteName (midi), small, Justification::centred, false);
            if (selected) outline (small);
            return;
        }

        const bool isRoot = m.kind == 1;
        g.setColour (isRoot ? col::accent : (chordMode ? col::chordTone : col::dot));
        g.fillRoundedRectangle (r, jmin (r.getWidth(), r.getHeight()) * 0.5f);
        const Colour noteCol = isRoot ? col::bg : Colours::white;
        const Colour degCol = isRoot ? col::bg : (chordMode ? Colour (0xffd6e6ff) : col::deg);

        if (r.getWidth() >= 40.0f)                             // wide fret: "E 5" side by side
        {
            g.setFont (fnt (12.0f, true));
            g.setColour (noteCol);
            g.drawText (noteName (midi), r.withWidth (27.0f).withTrimmedLeft (3.0f), Justification::centred, false);
            g.setFont (fnt (11.0f, true));
            g.setColour (degCol);
            g.drawText (m.deg, r.withTrimmedLeft (25.0f).withTrimmedRight (2.0f), Justification::centred, false);
        }
        else                                                   // narrow fret: note above, degree below
        {
            const float fs = jlimit (9.0f, 12.0f, r.getWidth() * 0.5f);
            g.setFont (fnt (fs, true));
            g.setColour (noteCol);
            g.drawText (noteName (midi), Rectangle<float> (r.getX(), r.getY() + 1.0f, r.getWidth(), r.getHeight() * 0.5f), Justification::centred, false);
            g.setFont (fnt (fs - 1.0f, true));
            g.setColour (degCol);
            g.drawText (m.deg, Rectangle<float> (r.getX(), r.getCentreY() - 1.0f, r.getWidth(), r.getHeight() * 0.5f), Justification::centred, false);
        }
        if (selected) outline (r);
    }

    Marker markerFor (int midi) const
    {
        Marker m;
        if (chordType != nullptr)
        {
            const int rel = (((midi - chordRoot) % 12) + 12) % 12;
            if (std::find (chordType->iv.begin(), chordType->iv.end(), rel) != chordType->iv.end())
            {
                m.kind = rel == 0 ? 1 : 2;
                m.deg = chordDegreeLabel (rel, *chordType);
            }
        }
        else
        {
            const int d = degreeOf (midi, proc.keyRoot, scaleAt (proc.keyScale));
            if (d > 0) { m.kind = d == 1 ? 1 : 2; m.deg = String (d); }
        }
        return m;
    }

    /** Visits every marker: key or chord notes, every open string, and notes of the selected tab column. */
    template <typename Fn>
    void forEachPill (int n, Fn fn) const
    {
        const auto geo = geometry (n);
        const auto sel = canvas.selectedNotes();
        for (int s = 0; s < n; ++s)
            for (int f = 0; f <= kFrets; ++f)
            {
                const Marker m = markerFor (proc.score.pitchOf (s, f));
                const bool selected = std::find (sel.begin(), sel.end(), std::make_pair (s, f)) != sel.end();
                if (m.kind == 0 && f != 0 && ! selected) continue;
                fn (s, f, m, pillRect (geo, n, s, f), selected);
            }
    }

    TabMidiProcessor& proc;
    const TabCanvas& canvas;
    int chordRoot = -1;
    const ChordType* chordType = nullptr;
};

//==============================================================================
/** "Chords in key" helper: a column per note of the key, a row per chord type; chords made only of
    notes of the key are highlighted. Click a chord to show it on the fretboard, click again to go back. */
class ChordPanel : public Component
{
public:
    static constexpr int kRows = 22;
    std::function<void (int rootPc, int typeIdx)> onToggle;

    explicit ChordPanel (TabMidiProcessor& p) : proc (p) {}

    void setSelection (int r, int t) { selRoot = r; selType = t; repaint(); }
    void clearSelection()            { selRoot = -1; selType = -1; repaint(); }
    bool isSelected (int r, int t) const { return r == selRoot && t == selType; }
    void pick (int r, int t)         { if (onToggle) onToggle (r, t); }

    void paint (Graphics& g) override
    {
        g.setColour (col::panel);
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 10.0f);
        g.setColour (col::line);
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 10.0f, 1.0f);

        int keyRoot;
        Scale sc;
        { const ScopedLock sl (proc.lock); keyRoot = proc.keyRoot; sc = scaleAt (proc.keyScale); }
        cols = (int) sc.iv.size();

        g.setColour (col::muted);
        g.setFont (fnt (11.0f, true));
        g.drawText ("АККОРДЫ ТОНАЛЬНОСТИ · "_ru + noteName (keyRoot) + " " + sc.name, 14, 10, getWidth() - 28, 14, Justification::left, false);
        g.setFont (fnt (11.0f));
        g.drawText ("Клик по аккорду: его ноты на грифе. Ещё раз: назад к тональности."_ru, 14, 26, getWidth() - 28, 14, Justification::left, false);

        const auto& types = chordTypes();
        for (int c = 0; c < (int) sc.iv.size(); ++c)
        {
            g.setColour (col::text);
            g.setFont (fnt (13.0f, true));
            g.drawText (noteName (keyRoot + sc.iv[(size_t) c]), cellRect (c, -1), Justification::centred, false);
            for (int r = 0; r < (int) types.size(); ++r)
            {
                const int rootPc = ((keyRoot + sc.iv[(size_t) c]) % 12 + 12) % 12;
                const bool inKey = chordInScale (rootPc, types[(size_t) r], keyRoot, sc);
                const bool sel = isSelected (rootPc, r);
                const auto rc = cellRect (c, r);
                g.setColour (sel ? col::accent : (inKey ? Colour (0xff2c4f87) : col::btn));
                g.fillRoundedRectangle (rc, 4.0f);
                if (sel) { g.setColour (Colours::white); g.drawRoundedRectangle (rc.reduced (0.5f), 4.0f, 1.5f); }
                const String name = String (noteName (rootPc)) + types[(size_t) r].name;
                g.setFont (fnt (name.length() <= 5 ? 12.0f : name.length() <= 7 ? 11.0f : 10.0f, inKey || sel));
                g.setColour (sel ? col::bg : (inKey ? Colours::white : col::muted));
                g.drawText (name, rc, Justification::centred, false);
            }
        }
    }

    void mouseDown (const MouseEvent& e) override
    {
        int keyRoot;
        Scale sc;
        { const ScopedLock sl (proc.lock); keyRoot = proc.keyRoot; sc = scaleAt (proc.keyScale); }
        cols = (int) sc.iv.size();
        for (int c = 0; c < (int) sc.iv.size(); ++c)
            for (int r = 0; r < kRows; ++r)
                if (cellRect (c, r).contains (e.position))
                {
                    pick (((keyRoot + sc.iv[(size_t) c]) % 12 + 12) % 12, r);
                    return;
                }
    }

private:
    Rectangle<float> cellRect (int col, int row) const      // row -1 = column header
    {
        const float colW = ((float) getWidth() - 20.0f) / (float) jmax (7, cols);
        const float rowH = ((float) getHeight() - 70.0f - 10.0f) / (float) kRows;
        const float y = row < 0 ? 46.0f : 70.0f + (float) row * rowH;
        return { 10.0f + (float) col * colW + 2.0f, y + 1.0f, colW - 4.0f, (row < 0 ? 20.0f : rowH) - 2.0f };
    }

    TabMidiProcessor& proc;
    int selRoot = -1, selType = -1;
    int cols = 7;
};

//==============================================================================
/** Pop-up with a note stepper for every string, a stepper for the whole tuning, presets and saving of custom tunings. */
class TuningEditor : public Component
{
public:
    static constexpr int kRow0 = 96;

    TuningEditor (TabMidiProcessor& p, std::function<void()> changed) : proc (p), onChanged (std::move (changed))
    {
        {
            const ScopedLock sl (proc.lock);
            pitches = proc.score.tuning;
            name = proc.score.tuningName;
        }
        n = pitches.size();

        allMinus.setButtonText (String::fromUTF8 ("\xe2\x88\x92"));
        allPlus.setButtonText ("+");
        allMinus.onClick = [this] { shiftAll (-1); };
        allPlus.onClick  = [this] { shiftAll (+1); };
        addAndMakeVisible (allMinus);
        addAndMakeVisible (allPlus);

        for (int i = 0; i < n; ++i)
        {
            const int s = n - 1 - i;                     // row 0 = highest string, like the tab
            auto* m = minus.add (new TextButton (String::fromUTF8 ("\xe2\x88\x92")));
            auto* pl = plus.add (new TextButton ("+"));
            auto* nl = noteLbl.add (new Label());
            auto* sh = shiftLbl.add (new Label());
            nl->setJustificationType (Justification::centred);
            nl->setFont (mono (16.0f, true));
            sh->setJustificationType (Justification::centredRight);
            sh->setFont (fnt (12.0f, true));
            sh->setColour (Label::textColourId, col::accent);
            m->onClick  = [this, s] { step (s, -1); };
            pl->onClick = [this, s] { step (s, +1); };
            for (Component* c : std::initializer_list<Component*> { m, pl, nl, sh }) addAndMakeVisible (c);
        }

        presets.onChange = [this]
        {
            const int idx = presets.getSelectedId() - 1;
            if (idx >= 0 && idx < (int) all.size()) { pitches = all[(size_t) idx].pitches; name = all[(size_t) idx].name; nameEd.setText (name, dontSendNotification); apply(); }
        };
        nameEd.setText (name, dontSendNotification);
        nameEd.onTextChange = [this] { name = nameEd.getText(); apply (false); };
        resetBtn.onClick = [this] { auto t = builtInTunings (n)[0]; pitches = t.pitches; name = t.name; nameEd.setText (name, dontSendNotification); apply(); };
        saveBtn.onClick = [this] { saveCustom(); };
        delBtn.onClick = [this] { deleteCustom(); };
        for (Component* c : std::initializer_list<Component*> { &presets, &nameEd, &resetBtn, &saveBtn, &delBtn }) addAndMakeVisible (c);
        saveBtn.setColour (TextButton::buttonColourId, col::accent);
        saveBtn.setColour (TextButton::textColourOffId, col::bg);

        rebuildPresets();
        refreshLabels();
        setSize (380, kRow0 + n * 32 + 96);
    }

    void paint (Graphics& g) override
    {
        g.fillAll (col::panel);
        g.setColour (col::muted);
        g.setFont (fnt (11.0f, true));
        g.drawText ("СТРОЙ · "_ru + String (n) + " СТРУН"_ru, 14, 8, 200, 14, Justification::left, false);

        g.setFont (fnt (12.0f, true));
        g.setColour (col::text);
        g.drawText ("Весь строй"_ru, 14, 58, 80, 28, Justification::left, false);
        g.setFont (fnt (12.0f));
        g.setColour (col::muted);
        g.drawText ("все струны"_ru, 130, 58, 70, 28, Justification::centred, false);
        g.drawText ("на полутон"_ru, 252, 58, 108, 28, Justification::centredRight, false);
        g.setColour (col::line);
        g.fillRect (14, kRow0 - 6, getWidth() - 28, 1);

        g.setColour (col::muted);
        for (int i = 0; i < n; ++i)
            g.drawText ("Струна "_ru + String (i + 1), 14, kRow0 + i * 32, 70, 28, Justification::left, false);
        g.setFont (fnt (11.0f));
        g.drawText ("Шаг — полутон, справа сдвиг от стандартного строя"_ru, 14, kRow0 + n * 32 + 4, 352, 14, Justification::left, false);
    }

    void resized() override
    {
        presets.setBounds (14, 24, getWidth() - 28, 26);
        allMinus.setBounds (96, 58, 30, 28);
        allPlus.setBounds (204, 58, 30, 28);
        for (int i = 0; i < n; ++i)
        {
            const int y = kRow0 + 2 + i * 32;
            minus[i]->setBounds (96, y, 30, 26);
            noteLbl[i]->setBounds (130, y, 70, 26);
            plus[i]->setBounds (204, y, 30, 26);
            shiftLbl[i]->setBounds (260, y, 100, 26);
        }
        const int y = kRow0 + n * 32 + 24;
        nameEd.setBounds (14, y, getWidth() - 28, 26);
        resetBtn.setBounds (14, y + 32, 80, 26);
        delBtn.setBounds (100, y + 32, 80, 26);
        saveBtn.setBounds (getWidth() - 14 - 170, y + 32, 170, 26);
    }

private:
    void step (int s, int d) { pitches.set (s, jlimit (12, 100, pitches[s] + d)); apply(); }

    void shiftAll (int d)
    {
        for (int v : pitches) if (v + d < 12 || v + d > 100) return;
        for (int i = 0; i < n; ++i) pitches.set (i, pitches[i] + d);
        apply();
    }

    void apply (bool refresh = true)
    {
        {
            const ScopedLock sl (proc.lock);
            proc.score.setTuning (pitches, name);
            proc.markEdited();
        }
        if (refresh) refreshLabels();
        if (onChanged) onChanged();
    }

    void refreshLabels()
    {
        const auto std0 = builtInTunings (n)[0].pitches;
        for (int i = 0; i < n; ++i)
        {
            const int s = n - 1 - i;
            noteLbl[i]->setText (pitchName (pitches[s]), dontSendNotification);
            const int d = pitches[s] - std0[s];
            shiftLbl[i]->setText (d == 0 ? String ("0") : (d > 0 ? "+" : String::fromUTF8 ("\xe2\x88\x92")) + String (std::abs (d)), dontSendNotification);
        }
    }

    void rebuildPresets()
    {
        all = builtInTunings (n);
        const auto custom = TuningStore::forStrings (n);
        const int nb = (int) all.size();
        for (auto& t : custom) all.push_back (t);
        presets.clear (dontSendNotification);
        presets.addSectionHeading ("Готовые"_ru);
        for (int i = 0; i < nb; ++i) presets.addItem (all[(size_t) i].name + "   " + notes (all[(size_t) i]), i + 1);
        if (! custom.empty())
        {
            presets.addSectionHeading ("Мои строи"_ru);
            for (size_t i = 0; i < custom.size(); ++i) presets.addItem (custom[i].name + "   " + notes (custom[i]), nb + (int) i + 1);
        }
        presets.setTextWhenNothingSelected ("Выбрать готовый строй…"_ru);
    }

    static String notes (const Tuning& t)
    {
        StringArray a;
        for (int p : t.pitches) a.add (noteName (p));
        return a.joinIntoString (" ");
    }

    void saveCustom()
    {
        if (name.trim().isEmpty()) return;
        auto list = TuningStore::load();
        bool replaced = false;
        for (auto& t : list)
            if (t.name == name && t.pitches.size() == n) { t.pitches = pitches; replaced = true; }
        if (! replaced) { Tuning t; t.name = name; t.pitches = pitches; t.custom = true; list.push_back (t); }
        TuningStore::save (list);
        rebuildPresets();
    }

    void deleteCustom()
    {
        auto list = TuningStore::load();
        list.erase (std::remove_if (list.begin(), list.end(), [this] (const Tuning& t) { return t.name == name && t.pitches.size() == n; }), list.end());
        TuningStore::save (list);
        rebuildPresets();
    }

    TabMidiProcessor& proc;
    std::function<void()> onChanged;
    Array<int> pitches;
    String name;
    int n = 6;
    std::vector<Tuning> all;
    ComboBox presets;
    TextEditor nameEd;
    TextButton allMinus, allPlus;
    TextButton resetBtn { "Сбросить"_ru }, saveBtn { String::fromUTF8 ("\xe2\x98\x85 Сохранить строй") }, delBtn { "Удалить"_ru };
    OwnedArray<TextButton> minus, plus;
    OwnedArray<Label> noteLbl, shiftLbl;
};

//==============================================================================
/** Pop-up for the user's own keys: pick the notes above the tonic, name it, save / change / delete. */
class ScaleEditor : public Component
{
public:
    ScaleEditor (TabMidiProcessor& p, bool editCurrent, std::function<void()> changed) : proc (p), onChanged (std::move (changed))
    {
        Scale cur;
        {
            const ScopedLock sl (proc.lock);
            cur = scaleAt (proc.keyScale);
            keyRoot = proc.keyRoot;
        }
        if (editCurrent && cur.custom) { originalName = cur.name; nameEd.setText (cur.name, dontSendNotification); }
        else nameEd.setText ("Моя тональность"_ru, dontSendNotification);
        for (int i : cur.iv) on[(size_t) i] = true;
        on[0] = true;

        static const char* labels[12] = { "1", "b2", "2", "b3", "3", "4", "b5", "5", "b6", "6", "b7", "7" };
        for (int i = 0; i < 12; ++i)
        {
            auto* b = toggles.add (new TextButton (labels[i]));
            b->setClickingTogglesState (true);
            b->setColour (TextButton::buttonOnColourId, col::accent);        // the pop-up has its own look and feel, set the colours here
            b->setColour (TextButton::textColourOnId, col::bg);
            b->setToggleState (on[(size_t) i], dontSendNotification);
            if (i == 0) b->setEnabled (false);
            b->onClick = [this, i] { toggle (i); };
            addAndMakeVisible (b);
        }

        const auto& bank = scales();
        for (size_t i = 0; i < bank.size(); ++i) basis.addItem (bank[i].name, (int) i + 1);
        basis.setTextWhenNothingSelected ("Взять за основу готовую…"_ru);
        basis.onChange = [this]
        {
            const int idx = basis.getSelectedId() - 1;
            if (idx < 0) return;
            Scale s;
            { const ScopedLock sl (proc.lock); s = scaleAt (idx); }
            on.fill (false);
            for (int i : s.iv) on[(size_t) i] = true;
            for (int i = 0; i < 12; ++i) toggles[i]->setToggleState (on[(size_t) i], dontSendNotification);
            repaint();
        };
        delBtn.onClick  = [this] { remove(); };
        saveBtn.onClick = [this] { save(); };
        saveBtn.setColour (TextButton::buttonColourId, col::accent);
        saveBtn.setColour (TextButton::textColourOffId, col::bg);
        delBtn.setEnabled (originalName.isNotEmpty());
        for (Component* c : std::initializer_list<Component*> { &basis, &nameEd, &saveBtn, &delBtn }) addAndMakeVisible (c);
        setSize (420, 268);
    }

    void paint (Graphics& g) override
    {
        g.fillAll (col::panel);
        g.setColour (col::muted);
        g.setFont (fnt (11.0f, true));
        g.drawText (originalName.isNotEmpty() ? "ИЗМЕНИТЬ ТОНАЛЬНОСТЬ"_ru : "НОВАЯ ТОНАЛЬНОСТЬ"_ru, 14, 8, 300, 14, Justification::left, false);
        g.drawText ("ЗВУКИ НАД ТОНИКОЙ (ПОЛУТОНОВ ОТ ТОНИКИ)"_ru, 14, 94, 392, 14, Justification::left, false);

        StringArray notesList;
        int count = 0;
        for (int i = 0; i < 12; ++i)
            if (on[(size_t) i]) { notesList.add (noteName (keyRoot + i)); ++count; }
        g.setFont (fnt (12.0f));
        g.setColour (count >= 3 && count <= 8 ? col::text : Colour (0xffff7b72));
        g.drawText (count < 3 ? "Нужно хотя бы 3 ноты"_ru : count > 8 ? "Не больше 8 нот"_ru
                                 : "В тональности "_ru + noteName (keyRoot) + ": " + notesList.joinIntoString (" "),
                    14, 192, 392, 18, Justification::left, true);
    }

    void resized() override
    {
        basis.setBounds (14, 24, getWidth() - 28, 26);
        nameEd.setBounds (14, 58, getWidth() - 28, 26);
        for (int i = 0; i < 12; ++i)
            toggles[i]->setBounds (14 + (i % 6) * 66, 114 + (i / 6) * 38, 60, 32);
        delBtn.setBounds (14, 224, 100, 28);
        saveBtn.setBounds (getWidth() - 14 - 190, 224, 190, 28);
    }

private:
    int countOn() const { int c = 0; for (bool b : on) if (b) ++c; return c; }

    void toggle (int i)
    {
        if (i == 0) { toggles[0]->setToggleState (true, dontSendNotification); return; }
        const bool want = toggles[i]->getToggleState();
        if (want && countOn() >= 8) { toggles[i]->setToggleState (false, dontSendNotification); repaint(); return; }
        on[(size_t) i] = want;
        repaint();
    }

    void save()
    {
        std::vector<int> iv;
        for (int i = 0; i < 12; ++i) if (on[(size_t) i]) iv.push_back (i);
        if (iv.size() < 3 || iv.size() > 8) return;

        String nm = nameEd.getText().trim();
        if (nm.isEmpty()) nm = "Моя тональность"_ru;
        for (int i = 0; i < builtInScaleCount(); ++i)
            if (scaleAt (i).name.equalsIgnoreCase (nm)) nm << " (моя)"_ru;

        {
            const ScopedLock sl (proc.lock);
            auto list = customScales();
            bool done = false;
            for (auto& c : list)
                if (originalName.isNotEmpty() && c.name == originalName) { c.name = nm; c.iv = iv; done = true; break; }
            if (! done)
                for (auto& c : list) if (c.name == nm) { c.iv = iv; done = true; break; }
            if (! done) { Scale s; s.name = nm; s.iv = iv; s.custom = true; list.push_back (s); }

            std::vector<Scale> unique;                     // two keys never share a name
            for (auto& c : list)
            {
                bool dup = false;
                for (auto& u : unique) if (u.name == c.name) dup = true;
                if (! dup) unique.push_back (c);
            }
            setCustomScales (unique);
            ScaleStore::save (unique);
            proc.keyScale = jmax (0, indexOfScale (nm));
        }
        originalName = nm;
        nameEd.setText (nm, dontSendNotification);
        delBtn.setEnabled (true);
        repaint();
        if (onChanged) onChanged();
    }

    void remove()
    {
        if (originalName.isEmpty()) return;
        {
            const ScopedLock sl (proc.lock);
            const String current = scaleAt (proc.keyScale).name;
            auto list = customScales();
            list.erase (std::remove_if (list.begin(), list.end(), [this] (const Scale& s) { return s.name == originalName; }), list.end());
            setCustomScales (list);
            ScaleStore::save (list);
            proc.keyScale = jmax (0, indexOfScale (current == originalName ? String ("Minor") : current));
        }
        if (onChanged) onChanged();
        if (auto* cb = findParentComponentOfClass<CallOutBox>()) cb->dismiss();
    }

    TabMidiProcessor& proc;
    std::function<void()> onChanged;
    int keyRoot = 0;
    String originalName;                       // empty = a new key
    std::array<bool, 12> on {};
    ComboBox basis;
    TextEditor nameEd;
    TextButton saveBtn { "Сохранить"_ru }, delBtn { "Удалить"_ru };
    OwnedArray<TextButton> toggles;
};

//==============================================================================
/** The "drag me into the DAW" area. Starts an OS-level file drag with the generated .mid. */
class DragHandle : public Component
{
public:
    DragHandle() { setMouseCursor (MouseCursor::DraggingHandCursor); }

    void setFile (const File& f) { file = f; repaint(); }
    bool ready() const { return file.existsAsFile(); }

    void paint (Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (ready() ? col::accent.withAlpha (0.12f) : col::bg.withAlpha (0.4f));
        g.fillRoundedRectangle (r, 8.0f);
        g.setColour (ready() ? col::accent : col::btnLine);
        g.drawRoundedRectangle (r, 8.0f, 1.4f);
        // grip dots
        g.setColour (ready() ? col::accent : col::muted);
        for (int i = 0; i < 6; ++i)
            g.fillEllipse (14.0f + (float) (i % 2) * 6.0f, r.getCentreY() - 8.0f + (float) (i / 2) * 6.0f, 3.0f, 3.0f);
        g.setColour (ready() ? col::text : col::muted);
        g.setFont (fnt (13.0f, true));
        g.drawText (ready() ? "Перетащить MIDI в DAW"_ru : "Сначала MAKE MIDI"_ru, getLocalBounds().withTrimmedLeft (36), Justification::centredLeft, true);
    }

    void mouseDrag (const MouseEvent& e) override
    {
        if (! ready() || dragging || e.getDistanceFromDragStart() < 6) return;
        dragging = true;
        DragAndDropContainer::performExternalDragDropOfFiles (StringArray (file.getFullPathName()), false, this, [this] { dragging = false; });
        dragging = false;
    }

private:
    File file;
    bool dragging = false;
};

//==============================================================================
class TabMidiEditor : public AudioProcessorEditor, public FileDragAndDropTarget
{
public:
    explicit TabMidiEditor (TabMidiProcessor&);
    ~TabMidiEditor() override;

    void paint (Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag (const StringArray& files) override;
    void filesDropped (const StringArray& files, int x, int y) override;
    void fileDragEnter (const StringArray&, int, int) override { dragOver = true; repaint(); }
    void fileDragExit (const StringArray&) override { dragOver = false; repaint(); }

private:
    void loadFile (const File& f);
    void importTrack (int index);
    void makeMidi();
    void saveMidiAs();
    void setBars (int n);
    void setStrings (int n);
    void showPresetMenu();
    void openTuningEditor (Component* from);
    void refreshAll();
    void updateTabButtons();
    void refreshScaleBox();
    void openScaleEditor (bool editCurrent);
    int minWidth() const;
    void updateChordsButton (bool resizeWindow);

    TabMidiProcessor& proc;
    TabLnF lnf;
    std::unique_ptr<GpFile> gp;
    std::unique_ptr<FileChooser> chooser;

    String dropTitle { "Перетащите .gpx сюда"_ru }, dropInfo { "или нажмите «Открыть…». Поддерживаются Guitar Pro 6/7/8 (.gpx, .gp)"_ru };
    String midiStatus, currentName;
    bool dropError = false, dragOver = false;

    TextButton copyBtn { "Копировать"_ru }, pasteBtn { "Вставить"_ru }, delBarBtn { "Удалить такт"_ru }, undoBtn { "Отменить"_ru }, chordsBtn;
    ChordPanel chordPanel;
    TextButton openBtn { "Открыть…"_ru }, makeBtn { "MAKE MIDI" }, saveBtn { "Сохранить .mid…"_ru }, presetBtn { "Standard" };
    ComboBox trackBox, rootBox, scaleBox;
    OwnedArray<TextButton> barBtns, strBtns, chipBtns, durBtns, viewBtns;
    DragHandle dragHandle;
    TabCanvas canvas;
    Viewport viewport;
    FretboardView fretboard;

    Rectangle<int> rDrop, rDrag, rControls, rTab, rFret;
    int xBars = 0, xStrings = 0, xTuning = 0, xDur = 0, xView = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TabMidiEditor)
};
} // namespace tabmidi
