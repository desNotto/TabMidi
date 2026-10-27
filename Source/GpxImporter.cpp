#include "GpxImporter.h"

namespace tabmidi
{
//==============================================================================
// GPX container: "BCFZ" = LZ-style bit stream wrapping a "BCFS" sector file system.
namespace
{
struct BitReader
{
    const uint8* d; size_t n, pos; int cur = 0, used = 8; bool eof = false;
    BitReader (const uint8* data, size_t size, size_t start) : d (data), n (size), pos (start) {}

    int bit()
    {
        if (used == 8)
        {
            if (pos >= n) { eof = true; return 0; }
            cur = d[pos++]; used = 0;
        }
        return (cur >> (7 - used++)) & 1;
    }
    int bits (int k)      { int r = 0; for (int i = 0; i < k; ++i) r = (r << 1) | bit(); return r; }
    int bitsRev (int k)   { int r = 0; for (int i = 0; i < k; ++i) r |= bit() << i; return r; }
};

int32 readI32 (const uint8* p) { return (int32) ((uint32) p[0] | ((uint32) p[1] << 8) | ((uint32) p[2] << 16) | ((uint32) p[3] << 24)); }

bool bcfzDecompress (const uint8* d, size_t n, std::vector<uint8>& out)
{
    if (n < 8) return false;
    const int32 expected = readI32 (d + 4);
    if (expected <= 0 || expected > 256 * 1024 * 1024) return false;
    out.clear();
    out.reserve ((size_t) expected);
    BitReader r (d, n, 8);

    while ((int32) out.size() < expected && ! r.eof)
    {
        if (r.bit() == 1)
        {
            const int w = r.bits (4);
            const int offset = r.bitsRev (w);
            const int size = r.bitsRev (w);
            if (offset <= 0 || (size_t) offset > out.size()) return false;
            const size_t src = out.size() - (size_t) offset;
            const int count = jmin (offset, size);
            for (int i = 0; i < count; ++i) out.push_back (out[src + (size_t) i]);
        }
        else
        {
            const int size = r.bitsRev (2);
            for (int i = 0; i < size && ! r.eof; ++i) out.push_back ((uint8) r.bits (8));
        }
    }
    return out.size() >= 8;
}

bool readGpxContainer (const MemoryBlock& file, MemoryBlock& gpif)
{
    const auto* d = (const uint8*) file.getData();
    const size_t n = file.getSize();
    if (n < 8) return false;

    std::vector<uint8> fs;
    if (std::memcmp (d, "BCFZ", 4) == 0)      { if (! bcfzDecompress (d, n, fs)) return false; }
    else if (std::memcmp (d, "BCFS", 4) == 0) fs.assign (d, d + n);
    else return false;

    const uint8* p = fs.data() + 4;                  // sectors are counted after the 4-byte header
    const size_t len = fs.size() - 4, S = 0x1000;

    for (size_t off = S; off + S + 3 < len; off += S)
    {
        if (readI32 (p + off) != 2) continue;
        char name[128] = {};
        std::memcpy (name, p + off + 4, 127);
        if (String (name) != "score.gpif") continue;

        const int32 size = readI32 (p + off + 0x8c);
        const uint8* blocks = p + off + 0x94;
        for (int i = 0; i < 1000 && (size_t) (0x94 + 4 * (i + 1)) < S; ++i)
        {
            const int32 id = readI32 (blocks + 4 * i);
            if (id == 0) break;
            const size_t start = (size_t) id * S;
            if (start >= len) break;
            gpif.append (p + start, jmin (S, len - start));
        }
        if (size > 0 && (size_t) size < gpif.getSize()) gpif.setSize ((size_t) size);
        return gpif.getSize() > 0;
    }
    return false;
}

std::vector<int> toInts (const XmlElement* e)
{
    std::vector<int> v;
    if (e == nullptr) return v;
    for (auto& s : StringArray::fromTokens (e->getAllSubText().trim(), " \t\r\n", ""))
        v.push_back (s.getIntValue());
    return v;
}

const XmlElement* findProperty (const XmlElement* e, const String& name)
{
    if (e == nullptr) return nullptr;
    for (auto* c : e->getChildIterator())
    {
        if (c->hasTagName ("Property") && c->getStringAttribute ("name") == name) return c;
        if (auto* r = findProperty (c, name)) return r;
    }
    return nullptr;
}

const XmlElement* findElement (const XmlElement* e, const String& tag)
{
    if (e == nullptr) return nullptr;
    for (auto* c : e->getChildIterator())
    {
        if (c->hasTagName (tag)) return c;
        if (auto* r = findElement (c, tag)) return r;
    }
    return nullptr;
}

/** Guitar Pro 6 drum notes are (element, variation); this is the usual General MIDI drum note for each. */
int gp6DrumNote (int element, int variation)
{
    static const int t[17][3] = {
        { 36, 36, 36 }, { 38, 37, 38 }, { 56, 56, 56 }, { 56, 56, 56 }, { 56, 56, 56 }, { 43, 43, 43 },
        { 45, 45, 45 }, { 47, 47, 47 }, { 48, 48, 48 }, { 50, 50, 50 }, { 42, 46, 46 }, { 44, 44, 44 },
        { 57, 57, 57 }, { 49, 49, 49 }, { 55, 55, 55 }, { 51, 59, 53 }, { 52, 52, 52 } };
    if (element < 0 || element > 16) return 38;
    return t[element][jlimit (0, 2, variation)];
}

int dynamicVelocity (const XmlElement* d)
{
    if (d == nullptr) return 92;
    const String s = d->getAllSubText().trim();
    if (s == "PPP") return 20;   if (s == "PP") return 34;   if (s == "P") return 50;
    if (s == "MP") return 66;    if (s == "MF") return 82;   if (s == "F") return 98;
    if (s == "FF") return 112;   if (s == "FFF") return 126;
    return 92;
}
} // namespace

//==============================================================================
std::unique_ptr<GpFile> GpFile::load (const File& f, String& error)
{
    MemoryBlock mb;
    if (! f.loadFileAsData (mb) || mb.getSize() < 8) { error = "Не удалось прочитать файл"_ru; return nullptr; }

    String xml;
    const auto* d = (const uint8*) mb.getData();

    if (d[0] == 'P' && d[1] == 'K')            // Guitar Pro 7/8 (.gp) is a zip archive
    {
        ZipFile zip (f);
        const int idx = zip.getIndexOfFileName ("Content/score.gpif");
        if (idx < 0) { error = "В архиве .gp нет score.gpif"_ru; return nullptr; }
        std::unique_ptr<InputStream> in (zip.createStreamForEntry (idx));
        if (in == nullptr) { error = "Не удалось распаковать .gp"_ru; return nullptr; }
        xml = in->readEntireStreamAsString();
    }
    else
    {
        MemoryBlock gpif;
        if (! readGpxContainer (mb, gpif))
        {
            error = "Это не файл Guitar Pro 6/7/8 (.gpx / .gp). Файлы Guitar Pro 5 и старше не поддерживаются"_ru;
            return nullptr;
        }
        xml = String::fromUTF8 ((const char*) gpif.getData(), (int) gpif.getSize());
    }

    auto g = std::make_unique<GpFile>();
    if (! g->parse (xml, error)) return nullptr;
    return g;
}

bool GpFile::parse (const String& xml, String& error)
{
    root = XmlDocument::parse (xml);
    if (root == nullptr || ! root->hasTagName ("GPIF")) { error = "Не удалось разобрать содержимое файла"_ru; return false; }

    auto fillMap = [this] (const char* tag, std::map<int, const XmlElement*>& m)
    {
        if (auto* sect = root->getChildByName (tag))
            for (auto* e : sect->getChildIterator())
                m[e->getIntAttribute ("id", -1)] = e;
    };
    fillMap ("Bars", bars); fillMap ("Voices", voices); fillMap ("Beats", beats);
    fillMap ("Notes", notes); fillMap ("Rhythms", rhythms);

    if (auto* sc = root->getChildByName ("Score"))
        if (auto* t = sc->getChildByName ("Title")) title = t->getAllSubText().trim();

    if (auto* mbs = root->getChildByName ("MasterBars"))
        for (auto* e : mbs->getChildIterator())
        {
            if (! e->hasTagName ("MasterBar")) continue;
            MasterBar m;
            if (auto* t = e->getChildByName ("Time"))
            {
                auto parts = StringArray::fromTokens (t->getAllSubText().trim(), "/", "");
                if (parts.size() == 2) { m.num = jmax (1, parts[0].getIntValue()); m.den = jmax (1, parts[1].getIntValue()); }
            }
            m.barIds = toInts (e->getChildByName ("Bars"));
            if (auto* r = e->getChildByName ("Repeat"))
            {
                m.repStart = r->getStringAttribute ("start") == "true";
                m.repEnd   = r->getStringAttribute ("end") == "true";
                m.repCount = jmax (2, r->getIntAttribute ("count", 2));
            }
            m.alt = toInts (e->getChildByName ("AlternateEndings"));
            masterBars.push_back (m);
        }
    if (masterBars.empty()) { error = "В файле нет тактов"_ru; return false; }

    if (auto* mt = root->getChildByName ("MasterTrack"))
        if (auto* au = mt->getChildByName ("Automations"))
            for (auto* a : au->getChildIterator())
            {
                auto* ty = a->getChildByName ("Type");
                if (ty == nullptr || ty->getAllSubText().trim() != "Tempo") continue;
                Auto x;
                x.bar = a->getChildByName ("Bar") ? a->getChildByName ("Bar")->getAllSubText().getIntValue() : 0;
                x.pos = a->getChildByName ("Position") ? a->getChildByName ("Position")->getAllSubText().getDoubleValue() : 0.0;
                auto vals = StringArray::fromTokens (a->getChildByName ("Value") ? a->getChildByName ("Value")->getAllSubText().trim() : String(), " ", "");
                x.bpm = vals.size() > 0 ? vals[0].getDoubleValue() : 120.0;
                if (x.bpm > 0) tempoAutos.push_back (x);
            }

    if (auto* ts = root->getChildByName ("Tracks"))
    {
        int ordinal = 0;
        for (auto* t : ts->getChildIterator())
        {
            if (! t->hasTagName ("Track")) continue;
            Track tr;
            tr.ordinal = ordinal++;
            if (auto* n = t->getChildByName ("Name")) tr.name = n->getAllSubText().trim();
            if (tr.name.isEmpty()) tr.name = "Track " + String (tr.ordinal + 1);
            if (auto* tp = findProperty (t, "Tuning"))
                for (int v : toInts (tp->getChildByName ("Pitches"))) tr.tuning.add (v);
            if (auto* cp = findProperty (t, "CapoFret"))
                if (auto* fr = cp->getChildByName ("Fret")) tr.capo = fr->getAllSubText().getIntValue();
            if (auto* ins = findElement (t, "Instrument"))
                if (ins->getStringAttribute ("ref").startsWith ("drm")) tr.drums = true;
            if (auto* iset = findElement (t, "InstrumentSet"))
            {
                if (auto* ty = iset->getChildByName ("Type"))
                    if (ty->getAllSubText().trim() == "drumKit") tr.drums = true;
                if (auto* els = iset->getChildByName ("Elements"))
                    for (auto* el : els->getChildIterator())
                        if (auto* arts = el->getChildByName ("Articulations"))
                            for (auto* ar : arts->getChildIterator())
                                tr.drumOut.push_back (ar->getChildByName ("OutputMidiNumber") ? ar->getChildByName ("OutputMidiNumber")->getAllSubText().getIntValue() : 38);
            }
            allTracks.push_back (tr);
            if (! tr.drums && tr.tuning.size() >= 6 && tr.tuning.size() <= kMaxStrings)
                tracks.push_back (tr);
        }
    }
    if (allTracks.empty()) { error = "В файле нет дорожек"_ru; return false; }
    return true;
}

int GpFile::rhythmTicks (int id) const
{
    auto it = rhythms.find (id);
    if (it == rhythms.end()) return kPPQ;
    auto* r = it->second;
    const String nv = r->getChildByName ("NoteValue") ? r->getChildByName ("NoteValue")->getAllSubText().trim() : String ("Quarter");
    double t = nv == "Whole" ? 1920 : nv == "Half" ? 960 : nv == "Quarter" ? 480 : nv == "Eighth" ? 240
             : nv == "16th" ? 120 : nv == "32nd" ? 60 : nv == "64th" ? 30 : nv == "128th" ? 15 : 480;
    if (auto* dot = r->getChildByName ("AugmentationDot"))
    {
        const int c = dot->getIntAttribute ("count", 1);
        double add = t / 2, total = t;
        for (int i = 0; i < c; ++i) { total += add; add /= 2; }
        t = total;
    }
    if (auto* pt = r->getChildByName ("PrimaryTuplet"))
    {
        const int num = pt->getIntAttribute ("num", 1), den = pt->getIntAttribute ("den", 1);
        if (num > 0) t = t * den / num;
    }
    return jmax (1, roundToInt (t));
}

/** Play order (repeats / alternate endings expanded), bar lengths, time signatures and tempo changes. */
void GpFile::ensureTimeline() const
{
    if (timelineBuilt) return;
    timelineBuilt = true;

    std::map<int, int> passes;
    int start = 0, i = 0, guard = 0;
    while (i < (int) masterBars.size() && guard++ < 5000 && order.size() < 600)
    {
        const MasterBar& m = masterBars[(size_t) i];
        if (m.repStart) start = i;
        const int play = passes[start] + 1;
        if (! m.alt.empty() && std::find (m.alt.begin(), m.alt.end(), play) == m.alt.end()) { ++i; continue; }
        order.push_back (i);
        if (m.repEnd)
        {
            int& p = passes[start];
            if (p + 1 < m.repCount) { ++p; i = start; continue; }
            p = 0;
        }
        ++i;
    }

    int T = 0;
    for (int mi : order)
    {
        const MasterBar& m = masterBars[(size_t) mi];
        const int barLen = jmax (1, m.num * 4 * kPPQ / m.den);
        barTicks.push_back (barLen);
        if (sigs.empty() || sigs.back().num != m.num || sigs.back().den != m.den) sigs.push_back ({ T, m.num, m.den });
        for (auto& a : tempoAutos)
            if (a.bar == mi) tempos.push_back ({ T + roundToInt (a.pos * barLen), a.bpm });
        T += barLen;
    }
}

const std::vector<GpFile::Imp>& GpFile::notesOf (const Track& tr) const
{
    auto cached = cache.find (tr.ordinal);
    if (cached != cache.end()) return cached->second;

    ensureTimeline();
    std::vector<Imp> imps;
    const int ns = tr.tuning.size();
    int T = 0;

    for (size_t k = 0; k < order.size(); ++k)
    {
        const MasterBar& m = masterBars[(size_t) order[k]];
        if (tr.ordinal < (int) m.barIds.size())
        {
            auto bit = bars.find (m.barIds[(size_t) tr.ordinal]);
            if (bit != bars.end())
                for (int vid : toInts (bit->second->getChildByName ("Voices")))
                {
                    auto vit = voices.find (vid);
                    if (vid < 0 || vit == voices.end()) continue;
                    int t = 0;
                    for (int bid : toInts (vit->second->getChildByName ("Beats")))
                    {
                        auto beit = beats.find (bid);
                        if (beit == beats.end()) continue;
                        const XmlElement* beat = beit->second;
                        if (beat->getChildByName ("GraceNotes") != nullptr) continue;

                        auto* rr = beat->getChildByName ("Rhythm");
                        const int dur = rhythmTicks (rr ? rr->getIntAttribute ("ref", 0) : 0);
                        const int vel = dynamicVelocity (beat->getChildByName ("Dynamic"));

                        for (int nid : toInts (beat->getChildByName ("Notes")))
                        {
                            auto nit = notes.find (nid);
                            if (nit == notes.end()) continue;
                            int str = -1, fret = -1, midi = -1, element = -1, variation = 0, tone = -1, octave = -1;
                            if (auto* props = nit->second->getChildByName ("Properties"))
                                for (auto* p : props->getChildIterator())
                                {
                                    const String nm = p->getStringAttribute ("name");
                                    auto num = [p] (const char* child) { auto* c = p->getChildByName (child); return c ? c->getAllSubText().getIntValue() : -1; };
                                    if (nm == "String")         str = num ("String");
                                    else if (nm == "Fret")      fret = num ("Fret");
                                    else if (nm == "Midi")      midi = num ("Number");
                                    else if (nm == "Element")   element = num ("Element");
                                    else if (nm == "Variation") variation = jmax (0, num ("Variation"));
                                    else if (nm == "Tone")      tone = num ("Step");
                                    else if (nm == "Octave")    octave = num ("Number");
                                }
                            const int artic = nit->second->getChildByName ("InstrumentArticulation")
                                                ? nit->second->getChildByName ("InstrumentArticulation")->getAllSubText().getIntValue() : -1;

                            Imp imp { T + t, dur, vel, -1, -1, -1 };
                            if (tr.drums)
                            {
                                if (artic >= 0 && artic < (int) tr.drumOut.size()) imp.midi = tr.drumOut[(size_t) artic];
                                else if (element >= 0) imp.midi = gp6DrumNote (element, variation);
                                else if (midi >= 0) imp.midi = midi;
                                else continue;
                            }
                            else if (str >= 0 && fret >= 0 && ns > 0)
                            {
                                if (str >= ns) continue;
                                imp.str = str; imp.fret = fret;
                            }
                            else if (midi >= 0)                imp.midi = midi;
                            else if (tone >= 0 && octave >= 0) imp.midi = octave * 12 + tone;
                            else if (element >= 0)             imp.midi = gp6DrumNote (element, variation);
                            else continue;

                            bool tied = false;
                            if (auto* tie = nit->second->getChildByName ("Tie"))
                                if (tie->getStringAttribute ("destination") == "true")
                                    for (int q = (int) imps.size() - 1; q >= 0 && q >= (int) imps.size() - 64; --q)
                                        if (imps[(size_t) q].str == imp.str && imps[(size_t) q].fret == imp.fret && imps[(size_t) q].midi == imp.midi
                                            && std::abs (imps[(size_t) q].start + imps[(size_t) q].len - (T + t)) <= 8)
                                        {
                                            imps[(size_t) q].len = T + t + dur - imps[(size_t) q].start;
                                            tied = true;
                                            break;
                                        }
                            if (! tied) imps.push_back (imp);
                        }
                        t += dur;
                    }
                }
        }
        T += barTicks[k];
    }
    return cache.emplace (tr.ordinal, std::move (imps)).first->second;
}

void GpFile::fillSong (SongData& song) const
{
    ensureTimeline();
    song.tracks.clear();
    for (auto& tr : allTracks)
    {
        ExportTrack et;
        et.name = tr.name;
        et.drums = tr.drums;
        for (auto& n : notesOf (tr))
        {
            const int pitch = pitchOf (tr, n);
            if (pitch >= 0 && pitch <= 127) et.notes.push_back ({ n.start, n.len, pitch, n.vel });
        }
        song.tracks.push_back (std::move (et));
    }
    song.tempos = tempos;
    song.sigs = sigs;
}

int GpFile::noteCount (int listIndex) const
{
    if (listIndex < 0 || listIndex >= (int) tracks.size()) return 0;
    return (int) notesOf (tracks[(size_t) listIndex]).size();
}

bool GpFile::importSongOnly (ImportResult& out) const
{
    fillSong (out.song);
    out.hasEditorTrack = false;
    out.title = title;
    out.totalBars = (int) order.size();
    return true;
}

bool GpFile::importTrack (int listIndex, ImportResult& out) const
{
    if (listIndex < 0 || listIndex >= (int) tracks.size()) return false;
    const Track& tr = tracks[(size_t) listIndex];
    const int ns = tr.tuning.size();
    fillSong (out.song);
    const auto& imps = notesOf (tr);

    Array<int> tun;
    for (int v : tr.tuning) tun.add (v + tr.capo);
    TabScore& sc = out.score;
    sc.reset (1, ns, tun, "Из файла"_ru);
    sc.tempo = tempos.empty() ? 120 : roundToInt (tempos.front().bpm);
    out.totalBars = (int) order.size();
    out.truncated = out.totalBars > kMaxBars;
    const int nb = jmin (kMaxBars, out.totalBars);
    sc.barCells.clear();
    int total = 0;
    for (int b = 0; b < nb; ++b)
    {
        const int c = jmax (1, roundToInt (barTicks[(size_t) b] / (double) kCellTicks));
        sc.barCells.push_back (c);
        total += c;
    }
    if (sc.barCells.empty()) { sc.barCells.push_back (16); total = 16; }
    sc.cells.assign ((size_t) total, Cell());
    for (auto& n : imps)
    {
        if (n.str < 0) continue;
        const int c = roundToInt (n.start / (double) kCellTicks);
        if (c < 0 || c >= total) continue;
        sc.cells[(size_t) c].fret[n.str] = n.fret;
        sc.cells[(size_t) c].len[n.str] = jmax (1, roundToInt (n.len / (double) kCellTicks));
    }
    out.hasEditorTrack = true;
    out.editorTrack = tr.ordinal;
    out.title = title.isNotEmpty() ? title : tr.name;
    return true;
}
} // namespace tabmidi
