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
/** Chord shapes of the "chords in key" helper panel. */
struct ChordType { const char* name; const char* longName; std::vector<int> iv; };

inline const std::vector<ChordType>& chordTypes()
{
    static const std::vector<ChordType> t = {
        { "maj", "maj", { 0, 4, 7 } },              { "min", "min", { 0, 3, 7 } },
        { "sus2", "sus2", { 0, 2, 7 } },            { "sus4", "sus4", { 0, 5, 7 } },
        { "7", "7", { 0, 4, 7, 10 } },              { "maj7", "maj7", { 0, 4, 7, 11 } },
        { "m7", "min7", { 0, 3, 7, 10 } },          { "7b5", "7b5", { 0, 4, 6, 10 } },
        { "7#5", "7#5", { 0, 4, 8, 10 } },          { "m7b5", "min7b5", { 0, 3, 6, 10 } },
        { "minmaj7", "minmaj7", { 0, 3, 7, 11 } },  { "7sus4", "7sus4", { 0, 5, 7, 10 } },
        { "6", "6", { 0, 4, 7, 9 } },               { "min6", "min6", { 0, 3, 7, 9 } },
        { "6add9", "6/9", { 0, 2, 4, 7, 9 } },      { "9", "9", { 0, 2, 4, 7, 10 } },
        { "maj9", "maj9", { 0, 2, 4, 7, 11 } },     { "m9", "min9", { 0, 2, 3, 7, 10 } },
        { "minmaj9", "minmaj9", { 0, 2, 3, 7, 11 } }, { "add9", "add9", { 0, 2, 4, 7 } },
        { "m add9", "min add9", { 0, 2, 3, 7 } },   { "11", "11", { 0, 2, 4, 5, 7, 10 } } };
    return t;
}

/** True when every note of the chord belongs to the key. */
inline bool chordInScale (int rootPc, const ChordType& c, int keyRoot, const Scale& sc)
{
    for (int i : c.iv)
        if (degreeOf (rootPc + i, keyRoot, sc) == 0) return false;
    return true;
}

inline String flatSign()  { return String::fromUTF8 ("\xe2\x99\xad"); }     // ♭
inline String sharpSign() { return String::fromUTF8 ("\xe2\x99\xaf"); }     // ♯
inline String asciiAccidentals (const String& s) { return s.replace (flatSign(), "b").replace (sharpSign(), "#"); }

/** Name of a chord tone by its distance from the root. The other notes of the chord decide what it is
    (9 or 2, 11 or 4, ♯5 or ♭13 ...). */
inline String degreeLabel (int rel, const std::vector<int>& iv)
{
    auto has = [&iv] (int v) { return std::find (iv.begin(), iv.end(), v) != iv.end(); };
    const bool third = has (3) || has (4), fifth = has (7), seventh = has (10) || has (11);
    const String f = flatSign(), sh = sharpSign();
    switch (((rel % 12) + 12) % 12)
    {
        case 0:  return "1";
        case 1:  return third ? f + "9" : f + "2";
        case 2:  return (third || seventh) ? "9" : "2";
        case 3:  return has (4) ? sh + "9" : f + "3";
        case 4:  return "3";
        case 5:  return third ? "11" : "4";
        case 6:  return fifth ? sh + "11" : f + "5";
        case 7:  return "5";
        case 8:  return fifth ? f + "13" : sh + "5";
        case 9:  return (has (3) && has (6)) ? f + f + "7" : (seventh ? "13" : "6");
        case 10: return f + "7";
        default: return "7";
    }
}

/** Same, with plain "b" and "#" (used on the narrow fretboard markers). */
inline String chordDegreeLabel (int rel, const ChordType& c) { return asciiAccidentals (degreeLabel (rel, c.iv)); }

//==============================================================================
/** Every chord shape the naming engine knows. rank: how plain the name is (lower is preferred). */
struct ChordTemplate { const char* sfx; const char* longSfx; std::vector<int> iv; int rank; };

inline const std::vector<ChordTemplate>& chordTemplates()
{
    static const std::vector<ChordTemplate> t = {
        // triads
        { "", "maj", { 0, 4, 7 }, 0 },          { "m", "min", { 0, 3, 7 }, 0 },
        { "dim", "dim", { 0, 3, 6 }, 0 },       { "aug", "aug", { 0, 4, 8 }, 0 },
        { "sus2", "sus2", { 0, 2, 7 }, 0 },     { "sus4", "sus4", { 0, 5, 7 }, 0 },
        // sixths and sevenths
        { "6", "6", { 0, 4, 7, 9 }, 1 },        { "m6", "min6", { 0, 3, 7, 9 }, 1 },
        { "7", "7", { 0, 4, 7, 10 }, 1 },       { "maj7", "maj7", { 0, 4, 7, 11 }, 1 },
        { "m7", "min7", { 0, 3, 7, 10 }, 1 },   { "m(maj7)", "minmaj7", { 0, 3, 7, 11 }, 1 },
        { "dim7", "dim7", { 0, 3, 6, 9 }, 1 },  { "m7b5", "min7b5", { 0, 3, 6, 10 }, 1 },
        { "7sus4", "7sus4", { 0, 5, 7, 10 }, 1 }, { "7sus2", "7sus2", { 0, 2, 7, 10 }, 2 },
        { "maj7sus4", "maj7sus4", { 0, 5, 7, 11 }, 2 }, { "maj7sus2", "maj7sus2", { 0, 2, 7, 11 }, 2 },
        { "7b5", "7b5", { 0, 4, 6, 10 }, 2 },   { "7#5", "7#5", { 0, 4, 8, 10 }, 2 },
        { "maj7#5", "maj7#5", { 0, 4, 8, 11 }, 2 }, { "maj7b5", "maj7b5", { 0, 4, 6, 11 }, 2 },
        { "dim(maj7)", "dim maj7", { 0, 3, 6, 11 }, 3 },
        // added notes
        { "add9", "add9", { 0, 2, 4, 7 }, 2 },  { "m add9", "min add9", { 0, 2, 3, 7 }, 2 },
        { "add11", "add11", { 0, 4, 5, 7 }, 3 }, { "m add11", "min add11", { 0, 3, 5, 7 }, 3 },
        { "6/9", "6/9", { 0, 2, 4, 7, 9 }, 2 }, { "m6/9", "min6/9", { 0, 2, 3, 7, 9 }, 2 },
        // ninths
        { "9", "9", { 0, 2, 4, 7, 10 }, 2 },    { "maj9", "maj9", { 0, 2, 4, 7, 11 }, 2 },
        { "m9", "min9", { 0, 2, 3, 7, 10 }, 2 }, { "m(maj9)", "minmaj9", { 0, 2, 3, 7, 11 }, 3 },
        { "9sus4", "9sus4", { 0, 2, 5, 7, 10 }, 3 }, { "7b9", "7b9", { 0, 1, 4, 7, 10 }, 2 },
        { "7#9", "7#9", { 0, 3, 4, 7, 10 }, 2 }, { "9b5", "9b5", { 0, 2, 4, 6, 10 }, 3 },
        { "9#5", "9#5", { 0, 2, 4, 8, 10 }, 3 }, { "m9b5", "min9b5", { 0, 2, 3, 6, 10 }, 3 },
        { "7#11", "7#11", { 0, 4, 6, 7, 10 }, 3 }, { "maj7#11", "maj7#11", { 0, 4, 6, 7, 11 }, 3 },
        { "7b13", "7b13", { 0, 4, 7, 8, 10 }, 3 },
        { "7b9#5", "7b9#5", { 0, 1, 4, 8, 10 }, 3 }, { "7#9#5", "7#9#5", { 0, 3, 4, 8, 10 }, 3 },
        { "7b9b5", "7b9b5", { 0, 1, 4, 6, 10 }, 3 }, { "7#9b5", "7#9b5", { 0, 3, 4, 6, 10 }, 3 },
        // elevenths and thirteenths
        { "11", "11", { 0, 2, 4, 5, 7, 10 }, 3 }, { "m11", "min11", { 0, 2, 3, 5, 7, 10 }, 3 },
        { "maj11", "maj11", { 0, 2, 4, 5, 7, 11 }, 4 },
        { "13", "13", { 0, 2, 4, 7, 9, 10 }, 3 }, { "m13", "min13", { 0, 2, 3, 7, 9, 10 }, 3 },
        { "maj13", "maj13", { 0, 2, 4, 7, 9, 11 }, 3 },
        { "13(no9)", "13 no9", { 0, 4, 7, 9, 10 }, 3 }, { "m13(no9)", "min13 no9", { 0, 3, 7, 9, 10 }, 3 },
        { "maj13(no9)", "maj13 no9", { 0, 4, 7, 9, 11 }, 3 },
        // voicings without the fifth (very common on guitar)
        { "7(no5)", "7 no5", { 0, 4, 10 }, 4 },       { "maj7(no5)", "maj7 no5", { 0, 4, 11 }, 4 },
        { "m7(no5)", "min7 no5", { 0, 3, 10 }, 4 },   { "m(maj7)(no5)", "minmaj7 no5", { 0, 3, 11 }, 4 },
        { "9(no5)", "9 no5", { 0, 2, 4, 10 }, 4 },    { "maj9(no5)", "maj9 no5", { 0, 2, 4, 11 }, 4 },
        { "m9(no5)", "min9 no5", { 0, 2, 3, 10 }, 4 }, { "6(no5)", "6 no5", { 0, 4, 9 }, 4 },
        { "m6(no5)", "min6 no5", { 0, 3, 9 }, 4 },    { "add9(no5)", "add9 no5", { 0, 2, 4 }, 4 },
        { "m add9(no5)", "min add9 no5", { 0, 2, 3 }, 4 }, { "7sus4(no5)", "7sus4 no5", { 0, 5, 10 }, 4 },
        { "7b9(no5)", "7b9 no5", { 0, 1, 4, 10 }, 4 }, { "7#9(no5)", "7#9 no5", { 0, 3, 4, 10 }, 4 },
        { "13(no5)", "13 no5", { 0, 4, 9, 10 }, 4 } };
    return t;
}

/** Interval between two notes, simple (0..12) or compound (13..24). */
struct IntervalName { String shortName, longName; };

inline IntervalName intervalName (int semis)
{
    static const char* sh[25] = { "ч1", "м2", "б2", "м3", "б3", "ч4", "тритон", "ч5", "м6", "б6", "м7", "б7", "ч8",
                                  "м9", "б9", "м10", "б10", "ч11", "ув.11", "ч12", "м13", "б13", "м14", "б14", "2 окт." };
    static const char* lg[25] = { "Унисон", "Малая секунда", "Большая секунда", "Малая терция", "Большая терция",
                                  "Чистая кварта", "Тритон", "Чистая квинта", "Малая секста", "Большая секста",
                                  "Малая септима", "Большая септима", "Октава", "Малая нона", "Большая нона",
                                  "Малая децима", "Большая децима", "Чистая ундецима", "Увеличенная ундецима",
                                  "Чистая дуодецима", "Малая терцдецима", "Большая терцдецима", "Малая квартодецима",
                                  "Большая квартодецима", "Две октавы" };
    const int i = jlimit (0, 24, semis);
    return { String::fromUTF8 (sh[i]), String::fromUTF8 (lg[i]) };
}

/** What the engine tells about a set of sounding notes. */
struct ChordAnalysis
{
    int noteCount = 0;                    // different pitch classes
    bool isInterval = false;              // exactly two notes
    bool known = false;                   // a name was found
    String shortName, longName;           // "Em7" / "Emin7", "м3" / "Малая терция"
    std::vector<String> alternatives;     // other readings: "G6/E", "обращение: б6"
    String inversion;                     // "1-я инверсия", empty in root position
    std::vector<String> degrees;          // 1, ♭3, 5, ♭7 ...
    std::vector<String> noteNames;        // E G B D
};

namespace chorddetail
{
    struct Candidate { int root; int tpl; int score; };

    inline std::vector<Candidate> find (const std::set<int>& pcs, int bassPc)
    {
        std::vector<Candidate> out;
        const auto& ts = chordTemplates();
        for (int root : pcs)
        {
            std::set<int> rel;
            for (int pc : pcs) rel.insert ((pc - root + 12) % 12);
            for (size_t i = 0; i < ts.size(); ++i)
                if (std::set<int> (ts[i].iv.begin(), ts[i].iv.end()) == rel)
                    out.push_back ({ root, (int) i, ts[i].rank * 10 + (root == bassPc ? 0 : 25) });
        }
        std::stable_sort (out.begin(), out.end(), [] (const Candidate& a, const Candidate& b) { return a.score < b.score; });
        return out;
    }

    inline String title (const Candidate& c, int bassPc, bool longForm)
    {
        const auto& t = chordTemplates()[(size_t) c.tpl];
        String s = String (noteName (c.root)) + (longForm ? t.longSfx : t.sfx);
        if (c.root != bassPc) s << "/" << noteName (bassPc);
        return s;
    }
}

/** Names a set of MIDI pitches. Two notes give an interval (third, fifth ...), three or more a chord,
    with the lowest note as bass: Em7, C/E, Am7/G ... Other readings of the same notes go to `alternatives`. */
inline ChordAnalysis analysePitches (std::vector<int> midi)
{
    ChordAnalysis a;
    if (midi.size() < 2) return a;
    std::sort (midi.begin(), midi.end());
    const int bassPc = ((midi.front() % 12) + 12) % 12;
    std::set<int> pcs;
    for (int v : midi) pcs.insert (((v % 12) + 12) % 12);
    a.noteCount = (int) pcs.size();
    if (pcs.size() < 2) return a;                                   // the same note in several octaves

    if (pcs.size() == 2)                                            // an interval
    {
        const int upperPc = *pcs.begin() == bassPc ? *pcs.rbegin() : *pcs.begin();
        const int simple = (upperPc - bassPc + 12) % 12;
        const int d = midi.size() == 2 ? midi[1] - midi[0] : simple;
        const auto nm = intervalName (d);
        a.isInterval = a.known = true;
        a.shortName = nm.shortName;
        a.longName = nm.longName;
        a.degrees = { "1", degreeLabel (simple, { 0, simple }) };
        a.noteNames = { noteName (bassPc), noteName (upperPc) };
        if (simple == 7) a.alternatives.push_back (String (noteName (bassPc)) + "5");
        if (simple == 5) a.alternatives.push_back (String (noteName (upperPc)) + "5/" + noteName (bassPc));
        a.alternatives.push_back ("обращение: "_ru + intervalName (12 - simple).shortName);
        return a;
    }

    const auto cands = chorddetail::find (pcs, bassPc);
    if (cands.empty())                                              // a cluster the table does not know
    {
        std::set<int> rel;
        for (int pc : pcs) rel.insert ((pc - bassPc + 12) % 12);
        const std::vector<int> iv (rel.begin(), rel.end());
        a.longName = "Набор нот"_ru;
        for (int v : iv) { a.degrees.push_back (degreeLabel (v, iv)); a.noteNames.push_back (noteName (bassPc + v)); }
        return a;
    }

    const auto& best = cands.front();
    const auto& t = chordTemplates()[(size_t) best.tpl];
    a.known = true;
    a.shortName = chorddetail::title (best, bassPc, false);
    a.longName = chorddetail::title (best, bassPc, true);
    for (int v : t.iv) { a.degrees.push_back (degreeLabel (v, t.iv)); a.noteNames.push_back (noteName (best.root + v)); }

    if (best.root != bassPc)
    {
        const int rel = (bassPc - best.root + 12) % 12;
        a.inversion = (rel == 3 || rel == 4) ? "1-я инверсия"_ru
                    : (rel == 6 || rel == 7 || rel == 8) ? "2-я инверсия"_ru
                    : (rel == 9 || rel == 10 || rel == 11) ? "3-я инверсия"_ru
                    : "бас: "_ru + degreeLabel (rel, t.iv);
    }
    for (size_t i = 1; i < cands.size() && a.alternatives.size() < 3; ++i)
    {
        const String nm = chorddetail::title (cands[i], bassPc, true);
        if (nm != a.longName && std::find (a.alternatives.begin(), a.alternatives.end(), nm) == a.alternatives.end())
            a.alternatives.push_back (nm);
    }
    return a;
}

/** The chord picked in the helper panel: its name, degrees, notes and other names of the same notes. */
inline ChordAnalysis analyseChordType (int rootPc, const ChordType& ct)
{
    ChordAnalysis a;
    std::set<int> pcs;
    for (int v : ct.iv) pcs.insert (((rootPc + v) % 12 + 12) % 12);
    a.noteCount = (int) pcs.size();
    a.known = true;
    a.shortName = a.longName = String (noteName (rootPc)) + ct.longName;
    for (int v : ct.iv) { a.degrees.push_back (degreeLabel (v, ct.iv)); a.noteNames.push_back (noteName (rootPc + v)); }

    const std::set<int> own (ct.iv.begin(), ct.iv.end());
    for (auto& c : chorddetail::find (pcs, ((rootPc % 12) + 12) % 12))
    {
        const auto& t = chordTemplates()[(size_t) c.tpl];
        if (c.root == ((rootPc % 12) + 12) % 12 && std::set<int> (t.iv.begin(), t.iv.end()) == own) continue;
        const String nm = chorddetail::title (c, ((rootPc % 12) + 12) % 12, true);
        if (std::find (a.alternatives.begin(), a.alternatives.end(), nm) == a.alternatives.end()) a.alternatives.push_back (nm);
        if (a.alternatives.size() >= 3) break;
    }
    return a;
}

/** Short name for the label above a tab column ("Em7", "C/E", "м3"); empty when unknown. */
inline String chordName (const std::vector<int>& midi) { return analysePitches (midi).shortName; }

//==============================================================================
/** A setup the user can recall: tuning (and with it the number of strings) plus the key. */
struct Preset
{
    String name;
    String tuningName;
    Array<int> tuning;                   // lowest string first
    int keyRoot = 0;
    String scaleName;
    std::vector<int> scaleIv;
    bool factory = false;

    bool sameSetup (const Array<int>& tun, int root, const std::vector<int>& iv) const
    {
        return tuning == tun && keyRoot == root && scaleIv == iv;
    }
};

inline std::vector<Preset> factoryPresets()
{
    auto make = [] (const char* name, int strings, size_t tuningIdx, int root)
    {
        Preset p;
        p.name = name;
        p.factory = true;
        const auto t = builtInTunings (strings)[tuningIdx];
        p.tuning = t.pitches;
        p.tuningName = t.name;
        p.keyRoot = root;
        const auto& minor = scaleBank()[1];
        p.scaleName = minor.name;
        p.scaleIv = minor.iv;
        return p;
    };
    return { make ("8 Strings - Drop E - E minor", 8, 1, 4), make ("9 Strings - Drop B - B minor", 9, 1, 11) };
}

/** The user's own presets, kept as JSON next to the saved tunings and keys. */
struct PresetStore
{
    static File file()
    {
        return File::getSpecialLocation (File::userApplicationDataDirectory)
                   .getChildFile ("TabMidi").getChildFile ("presets.json");
    }

    static std::vector<Preset> load()
    {
        std::vector<Preset> out;
        auto v = JSON::parse (file());
        if (auto* a = v.getArray())
            for (auto& item : *a)
            {
                Preset p;
                p.name = item.getProperty ("name", "").toString();
                p.tuningName = item.getProperty ("tuningName", "").toString();
                p.keyRoot = jlimit (0, 11, (int) item.getProperty ("keyRoot", 0));
                p.scaleName = item.getProperty ("scaleName", "").toString();
                const var pitches = item.getProperty ("pitches", {}), iv = item.getProperty ("intervals", {});
                if (auto* pa = pitches.getArray()) for (auto& x : *pa) p.tuning.add ((int) x);
                if (auto* ia = iv.getArray()) for (auto& x : *ia) p.scaleIv.push_back (jlimit (0, 11, (int) x));
                if (p.name.isNotEmpty() && p.tuning.size() >= 6 && p.tuning.size() <= 9 && p.scaleIv.size() >= 3)
                    out.push_back (p);
            }
        return out;
    }

    static void save (const std::vector<Preset>& list)
    {
        Array<var> arr;
        for (auto& p : list)
        {
            auto* o = new DynamicObject();
            o->setProperty ("name", p.name);
            o->setProperty ("tuningName", p.tuningName);
            o->setProperty ("keyRoot", p.keyRoot);
            o->setProperty ("scaleName", p.scaleName);
            Array<var> pitches, iv;
            for (int v : p.tuning) pitches.add (v);
            for (int v : p.scaleIv) iv.add (v);
            o->setProperty ("pitches", pitches);
            o->setProperty ("intervals", iv);
            arr.add (var (o));
        }
        auto f = file();
        f.getParentDirectory().createDirectory();
        f.replaceWithText (JSON::toString (var (arr)));
    }
};

} // namespace tabmidi
