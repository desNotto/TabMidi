#pragma once
#include <juce_core/juce_core.h>
#include <algorithm>
#include <set>
#include <vector>

namespace tabmidi
{
using namespace juce;

/** UTF-8 string literal: "Привет"_ru. JUCE reads plain char literals as ASCII, so every non-ASCII literal needs this. */
inline String operator""_ru (const char* s, size_t n) { return String::fromUTF8 (s, (int) n); }

inline const char* noteName (int midiOrPc)
{
    static const char* n[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return n[((midiOrPc % 12) + 12) % 12];
}

inline String pitchName (int midi) { return String (noteName (midi)) + String (midi / 12 - 1); }

//==============================================================================
struct Scale
{
    String name;
    std::vector<int> iv;        // semitones above the tonic, ascending, starting with 0
    bool custom = false;        // made by the user
};

/** All keys: the built-in ones first, then the user's own (see ScaleStore). Touch it only under TabMidiProcessor::lock. */
inline std::vector<Scale>& scaleBank()
{
    static std::vector<Scale> s = {
        { "Major",         { 0, 2, 4, 5, 7, 9, 11 }, false },
        { "Minor",         { 0, 2, 3, 5, 7, 8, 10 }, false },
        { "Dorian",        { 0, 2, 3, 5, 7, 9, 10 }, false },
        { "Phrygian",      { 0, 1, 3, 5, 7, 8, 10 }, false },
        { "Lydian",        { 0, 2, 4, 6, 7, 9, 11 }, false },
        { "Mixolydian",    { 0, 2, 4, 5, 7, 9, 10 }, false },
        { "Locrian",       { 0, 1, 3, 5, 6, 8, 10 }, false },
        { "Harmonic min",  { 0, 2, 3, 5, 7, 8, 11 }, false },
        { "Melodic min",   { 0, 2, 3, 5, 7, 9, 11 }, false },
        { "Major pent.",   { 0, 2, 4, 7, 9 }, false },
        { "Minor pent.",   { 0, 3, 5, 7, 10 }, false } };
    return s;
}

inline const std::vector<Scale>& scales() { return scaleBank(); }
inline int builtInScaleCount() { return 11; }
inline const Scale& scaleAt (int i) { auto& b = scaleBank(); return b[(size_t) jlimit (0, (int) b.size() - 1, i)]; }

inline std::vector<Scale> customScales()
{
    auto& b = scaleBank();
    return std::vector<Scale> (b.begin() + builtInScaleCount(), b.end());
}

inline void setCustomScales (const std::vector<Scale>& list)
{
    auto& b = scaleBank();
    b.resize ((size_t) builtInScaleCount());
    for (auto s : list) { s.custom = true; b.push_back (s); }
}

inline int indexOfScale (const String& name)
{
    auto& b = scaleBank();
    for (size_t i = 0; i < b.size(); ++i) if (b[i].name == name) return (int) i;
    return -1;
}

/** Finds a key by name and notes, or adds it (used when a project carries a key this computer does not know). */
inline int findOrAddScale (const String& name, const std::vector<int>& iv)
{
    auto& b = scaleBank();
    for (size_t i = 0; i < b.size(); ++i) if (b[i].name == name && b[i].iv == iv) return (int) i;
    for (size_t i = 0; i < b.size(); ++i) if (b[i].iv == iv) return (int) i;
    Scale s; s.iv = iv; s.custom = true;
    s.name = indexOfScale (name) >= 0 ? name + " *" : name;
    b.push_back (s);
    return (int) b.size() - 1;
}

/** 1-based degree of a pitch inside the scale, or 0 when the pitch is not in the scale. */
inline int degreeOf (int midi, int rootPc, const Scale& sc)
{
    const int rel = (((midi - rootPc) % 12) + 12) % 12;
    for (size_t i = 0; i < sc.iv.size(); ++i)
        if (sc.iv[i] == rel)
            return (int) i + 1;
    return 0;
}

//==============================================================================
struct Tuning
{
    String name;
    Array<int> pitches;      // lowest string first (MIDI note numbers)
    bool custom = false;
};

inline std::vector<Tuning> builtInTunings (int n)
{
    std::vector<Tuning> t;
    auto add = [&t] (const char* nm, std::initializer_list<int> p)
    {
        Tuning x; x.name = nm;
        for (int v : p) x.pitches.add (v);
        t.push_back (x);
    };

    switch (n)
    {
        case 6:
            add ("Standard",       { 40, 45, 50, 55, 59, 64 });
            add ("Drop D",         { 38, 45, 50, 55, 59, 64 });
            add ("Half-step down", { 39, 44, 49, 54, 58, 63 });
            add ("D Standard",     { 38, 43, 48, 53, 57, 62 });
            add ("Drop C",         { 36, 43, 48, 53, 57, 62 });
            add ("DADGAD",         { 38, 45, 50, 55, 57, 62 });
            add ("Open G",         { 38, 43, 50, 55, 59, 62 });
            add ("Open D",         { 38, 45, 50, 54, 57, 62 });
            break;
        case 7:
            add ("Standard",   { 35, 40, 45, 50, 55, 59, 64 });
            add ("Drop A",     { 33, 40, 45, 50, 55, 59, 64 });
            add ("A Standard", { 33, 38, 43, 48, 52, 57, 62 });
            break;
        case 8:
            add ("Standard", { 30, 35, 40, 45, 50, 55, 59, 64 });
            add ("Drop E",   { 28, 35, 40, 45, 50, 55, 59, 64 });
            break;
        default:
            add ("Standard", { 25, 30, 35, 40, 45, 50, 55, 59, 64 });
            add ("Drop B",   { 23, 30, 35, 40, 45, 50, 55, 59, 64 });
            break;
    }
    return t;
}

/** User tunings, kept as JSON in the per-user application data folder. */
struct TuningStore
{
    static File file()
    {
        return File::getSpecialLocation (File::userApplicationDataDirectory)
                   .getChildFile ("TabMidi").getChildFile ("tunings.json");
    }

    static std::vector<Tuning> load()
    {
        std::vector<Tuning> out;
        auto v = JSON::parse (file());
        if (auto* a = v.getArray())
            for (auto& item : *a)
            {
                Tuning t; t.custom = true;
                t.name = item.getProperty ("name", "").toString();
                const var pv = item.getProperty ("pitches", {});
                if (auto* p = pv.getArray())
                    for (auto& x : *p) t.pitches.add ((int) x);
                if (t.name.isNotEmpty() && t.pitches.size() >= 6 && t.pitches.size() <= 9)
                    out.push_back (t);
            }
        return out;
    }

    static void save (const std::vector<Tuning>& list)
    {
        Array<var> arr;
        for (auto& t : list)
        {
            auto* o = new DynamicObject();
            o->setProperty ("name", t.name);
            Array<var> p;
            for (int v : t.pitches) p.add (v);
            o->setProperty ("pitches", p);
            arr.add (var (o));
        }
        auto f = file();
        f.getParentDirectory().createDirectory();
        f.replaceWithText (JSON::toString (var (arr)));
    }

    static std::vector<Tuning> forStrings (int n)
    {
        std::vector<Tuning> all;
        for (auto& t : load()) if (t.pitches.size() == n) all.push_back (t);
        return all;
    }
};


/** The user's own keys, kept as JSON next to the saved tunings. */
struct ScaleStore
{
    static File file()
    {
        return File::getSpecialLocation (File::userApplicationDataDirectory)
                   .getChildFile ("TabMidi").getChildFile ("scales.json");
    }

    static std::vector<Scale> load()
    {
        std::vector<Scale> out;
        auto v = JSON::parse (file());
        if (auto* a = v.getArray())
            for (auto& item : *a)
            {
                Scale s; s.custom = true;
                s.name = item.getProperty ("name", "").toString();
                const var iv = item.getProperty ("intervals", {});
                if (auto* p = iv.getArray())
                    for (auto& x : *p) s.iv.push_back (jlimit (0, 11, (int) x));
                if (s.name.isNotEmpty() && s.iv.size() >= 3 && s.iv.size() <= 8 && s.iv.front() == 0)
                    out.push_back (s);
            }
        return out;
    }

    static void save (const std::vector<Scale>& list)
    {
        Array<var> arr;
        for (auto& sc : list)
        {
            auto* o = new DynamicObject();
            o->setProperty ("name", sc.name);
            Array<var> iv;
            for (int v : sc.iv) iv.add (v);
            o->setProperty ("intervals", iv);
            arr.add (var (o));
        }
        auto f = file();
        f.getParentDirectory().createDirectory();
        f.replaceWithText (JSON::toString (var (arr)));
    }

    /** Adds the saved keys to the bank (keys that came with a project stay). Existing indices do not change. */
    static void mergeIntoBank()
    {
        auto have = customScales();
        for (auto& s : load())
        {
            bool exists = false;
            for (auto& h : have) if (h.name == s.name) exists = true;
            if (! exists) have.push_back (s);
        }
        setCustomScales (have);
    }
};

//==============================================================================
/** Names a chord from the sounding MIDI pitches ("Am", "C/E", "G7"...). Empty when unknown. */
inline String chordName (std::vector<int> p)
{
    if (p.size() < 2) return {};
    std::sort (p.begin(), p.end());
    const int bass = ((p.front() % 12) + 12) % 12;
    std::set<int> pcs;
    for (int v : p) pcs.insert (((v % 12) + 12) % 12);
    if (pcs.size() < 2) return {};

    struct T { const char* sfx; std::vector<int> iv; };
    static const std::vector<T> ts = {
        { "", { 0, 4, 7 } },       { "m", { 0, 3, 7 } },       { "5", { 0, 7 } },
        { "7", { 0, 4, 7, 10 } },  { "maj7", { 0, 4, 7, 11 } }, { "m7", { 0, 3, 7, 10 } },
        { "6", { 0, 4, 7, 9 } },   { "m6", { 0, 3, 7, 9 } },    { "sus4", { 0, 5, 7 } },
        { "sus2", { 0, 2, 7 } },   { "dim", { 0, 3, 6 } },      { "aug", { 0, 4, 8 } },
        { "dim7", { 0, 3, 6, 9 } },{ "m7b5", { 0, 3, 6, 10 } }, { "add9", { 0, 2, 4, 7 } },
        { "madd9", { 0, 2, 3, 7 } },{ "9", { 0, 2, 4, 7, 10 } },{ "m9", { 0, 2, 3, 7, 10 } },
        { "maj9", { 0, 2, 4, 7, 11 } }, { "7sus4", { 0, 5, 7, 10 } } };

    std::vector<int> roots { bass };
    for (int pc : pcs) if (pc != bass) roots.push_back (pc);

    for (int root : roots)
    {
        std::set<int> rel;
        for (int pc : pcs) rel.insert ((pc - root + 12) % 12);
        for (auto& t : ts)
            if (std::set<int> (t.iv.begin(), t.iv.end()) == rel)
            {
                String s = String (noteName (root)) + t.sfx;
                if (root != bass) s << "/" << noteName (bass);
                return s;
            }
    }
    return {};
}

//==============================================================================
/** Chord shapes for the "chords in key" helper panel (intervals in semitones from the root). */
struct ChordType { const char* name; std::vector<int> iv; };

inline const std::vector<ChordType>& chordTypes()
{
    static const std::vector<ChordType> t = {
        { "maj", { 0, 4, 7 } },        { "min", { 0, 3, 7 } },        { "sus2", { 0, 2, 7 } },
        { "sus4", { 0, 5, 7 } },       { "7", { 0, 4, 7, 10 } },      { "maj7", { 0, 4, 7, 11 } },
        { "m7", { 0, 3, 7, 10 } },     { "7b5", { 0, 4, 6, 10 } },    { "7#5", { 0, 4, 8, 10 } },
        { "m7b5", { 0, 3, 6, 10 } },   { "minmaj7", { 0, 3, 7, 11 } },{ "7sus4", { 0, 5, 7, 10 } },
        { "6", { 0, 4, 7, 9 } },       { "min6", { 0, 3, 7, 9 } },    { "6add9", { 0, 2, 4, 7, 9 } },
        { "9", { 0, 2, 4, 7, 10 } },   { "maj9", { 0, 2, 4, 7, 11 } },{ "m9", { 0, 2, 3, 7, 10 } },
        { "minmaj9", { 0, 2, 3, 7, 11 } }, { "add9", { 0, 2, 4, 7 } },{ "m add9", { 0, 2, 3, 7 } },
        { "11", { 0, 2, 4, 5, 7, 10 } } };
    return t;
}

/** True when every note of the chord belongs to the key. */
inline bool chordInScale (int rootPc, const ChordType& c, int keyRoot, const Scale& sc)
{
    for (int i : c.iv)
        if (degreeOf (rootPc + i, keyRoot, sc) == 0) return false;
    return true;
}

/** Name of a chord tone by its distance from the chord root: 1, b3, 3, 5, b7, 9 ... */
inline String chordDegreeLabel (int rel, const ChordType& c)
{
    const bool third = std::find (c.iv.begin(), c.iv.end(), 3) != c.iv.end()
                    || std::find (c.iv.begin(), c.iv.end(), 4) != c.iv.end();
    switch (rel)
    {
        case 0:  return "1";
        case 2:  return third ? "9" : "2";
        case 3:  return "b3";
        case 4:  return "3";
        case 5:  return third ? "11" : "4";
        case 6:  return "b5";
        case 7:  return "5";
        case 8:  return "#5";
        case 9:  return "6";
        case 10: return "b7";
        case 11: return "7";
        default: return {};
    }
}
} // namespace tabmidi
