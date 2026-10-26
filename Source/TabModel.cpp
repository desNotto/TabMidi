#include "TabModel.h"

namespace tabmidi
{
MidiFile buildMidiFile (std::vector<NoteEv> notes, const std::vector<TempoEv>& tempos,
                        const std::vector<SigEv>& sigs, const String& trackName)
{
    MidiFile mf;
    mf.setTicksPerQuarterNote (kPPQ);

    MidiMessageSequence conductor, track;
    bool tempoAtZero = false;
    for (auto& t : tempos)
    {
        if (t.tick == 0) tempoAtZero = true;
        auto m = MidiMessage::tempoMetaEvent (roundToInt (60000000.0 / jmax (20.0, t.bpm)));
        m.setTimeStamp (t.tick);
        conductor.addEvent (m);
    }
    if (! tempoAtZero)
    {
        auto m = MidiMessage::tempoMetaEvent (500000);
        m.setTimeStamp (0);
        conductor.addEvent (m);
    }
    for (auto& s : sigs)
    {
        auto m = MidiMessage::timeSignatureMetaEvent (s.num, s.den);
        m.setTimeStamp (s.tick);
        conductor.addEvent (m);
    }

    struct Ev { double t; int order; MidiMessage m; };
    std::vector<Ev> evs;
    for (auto& n : notes)
    {
        if (n.len <= 0) continue;
        const int pitch = jlimit (0, 127, n.pitch);
        evs.push_back ({ (double) n.start, 1, MidiMessage::noteOn (1, pitch, (uint8) jlimit (1, 127, n.vel)) });
        evs.push_back ({ (double) (n.start + n.len), 0, MidiMessage::noteOff (1, pitch) });
    }
    std::stable_sort (evs.begin(), evs.end(), [] (const Ev& a, const Ev& b)
                      { return a.t < b.t || (a.t == b.t && a.order < b.order); });

    auto nm = MidiMessage::textMetaEvent (3, trackName);
    nm.setTimeStamp (0);
    track.addEvent (nm);
    for (auto& e : evs)
    {
        e.m.setTimeStamp (e.t);
        track.addEvent (e.m);
    }

    mf.addTrack (conductor);
    mf.addTrack (track);
    return mf;
}

//==============================================================================
void TabScore::reset (int bars, int strings, const Array<int>& tun, const String& tunName)
{
    numStrings = jlimit (6, kMaxStrings, strings);
    tuning = tun;
    tuningName = tunName;
    barCells.assign ((size_t) jlimit (1, kMaxBars, bars), 16);
    cells.assign ((size_t) (barCells.size() * 16), Cell());
}

void TabScore::setBarCount (int n)
{
    n = jlimit (1, kMaxBars, n);
    if (n < numBars())
    {
        int total = 0;
        for (int i = 0; i < n; ++i) total += barCells[(size_t) i];
        barCells.resize ((size_t) n);
        cells.resize ((size_t) total);
    }
    while (numBars() < n)
    {
        barCells.push_back (16);
        cells.resize (cells.size() + 16);
    }
}

void TabScore::setTuning (const Array<int>& tun, const String& name)
{
    tuning = tun;
    tuningName = name;
    numStrings = jlimit (6, kMaxStrings, tun.size());
}

int TabScore::barStart (int bar) const
{
    int c = 0;
    for (int i = 0; i < bar && i < numBars(); ++i) c += barCells[(size_t) i];
    return c;
}

int TabScore::barOfCell (int cell) const
{
    int c = 0;
    for (int i = 0; i < numBars(); ++i)
    {
        c += barCells[(size_t) i];
        if (cell < c) return i;
    }
    return jmax (0, numBars() - 1);
}

std::vector<int> TabScore::pitchesAt (int cell) const
{
    std::vector<int> p;
    if (cell < 0 || cell >= totalCells()) return p;
    for (int s = 0; s < numStrings; ++s)
        if (cells[(size_t) cell].fret[s] >= 0)
            p.push_back (pitchOf (s, cells[(size_t) cell].fret[s]));
    return p;
}

MidiFile TabScore::toMidiFile() const
{
    std::vector<NoteEv> notes;
    for (int c = 0; c < totalCells(); ++c)
        for (int s = 0; s < numStrings; ++s)
            if (cells[(size_t) c].fret[s] >= 0)
                notes.push_back ({ c * kCellTicks, jmax (1, cells[(size_t) c].len[s]) * kCellTicks,
                                   pitchOf (s, cells[(size_t) c].fret[s]), 100 });

    std::vector<SigEv> sigs;
    int tick = 0;
    for (int cellsInBar : barCells)
    {
        // all editor bars are expressed in 16ths; 16 -> 4/4, 12 -> 3/4, other sizes -> n/16
        int num = cellsInBar / 4, den = 4;                  // 16 -> 4/4, 12 -> 3/4, 8 -> 2/4 ...
        if (cellsInBar % 4 != 0) { num = cellsInBar; den = 16; }
        if (sigs.empty() || sigs.back().num != num || sigs.back().den != den)
            sigs.push_back ({ tick, num, den });
        tick += cellsInBar * kCellTicks;
    }
    return buildMidiFile (notes, { { 0, (double) tempo } }, sigs, "Tab");
}

static String intsToString (const int* v, int n)
{
    StringArray a;
    for (int i = 0; i < n; ++i) a.add (String (v[i]));
    return a.joinIntoString (" ");
}

std::unique_ptr<XmlElement> TabScore::toXml() const
{
    auto e = std::make_unique<XmlElement> ("TABSCORE");
    e->setAttribute ("strings", numStrings);
    e->setAttribute ("tuning", intsToString (tuning.getRawDataPointer(), tuning.size()));
    e->setAttribute ("tname", tuningName);
    e->setAttribute ("tempo", tempo);
    e->setAttribute ("bars", intsToString (barCells.data(), (int) barCells.size()));
    for (int c = 0; c < totalCells(); ++c)
        if (! cells[(size_t) c].empty())
        {
            auto* ce = e->createNewChildElement ("C");
            ce->setAttribute ("i", c);
            ce->setAttribute ("f", intsToString (cells[(size_t) c].fret, kMaxStrings));
            ce->setAttribute ("l", intsToString (cells[(size_t) c].len, kMaxStrings));
        }
    return e;
}

bool TabScore::fromXml (const XmlElement& e)
{
    if (! e.hasTagName ("TABSCORE")) return false;
    Array<int> tun;
    for (auto& s : StringArray::fromTokens (e.getStringAttribute ("tuning"), " ", "")) tun.add (s.getIntValue());
    if (tun.size() < 6 || tun.size() > kMaxStrings) return false;

    std::vector<int> bc;
    for (auto& s : StringArray::fromTokens (e.getStringAttribute ("bars"), " ", ""))
        bc.push_back (jlimit (1, 64, s.getIntValue()));
    if (bc.empty() || (int) bc.size() > kMaxBars) return false;

    numStrings = tun.size();
    tuning = tun;
    tuningName = e.getStringAttribute ("tname", "Standard");
    tempo = e.getIntAttribute ("tempo", 120);
    barCells = bc;
    int total = 0;
    for (int v : bc) total += v;
    cells.assign ((size_t) total, Cell());

    for (auto* ce : e.getChildIterator())
    {
        const int i = ce->getIntAttribute ("i", -1);
        if (i < 0 || i >= total) continue;
        auto f = StringArray::fromTokens (ce->getStringAttribute ("f"), " ", "");
        auto l = StringArray::fromTokens (ce->getStringAttribute ("l"), " ", "");
        for (int s = 0; s < kMaxStrings; ++s)
        {
            if (s < f.size()) cells[(size_t) i].fret[s] = f[s].getIntValue();
            if (s < l.size()) cells[(size_t) i].len[s] = jmax (1, l[s].getIntValue());
        }
    }
    return true;
}
} // namespace tabmidi
