#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace tabmidi
{
TabMidiProcessor::TabMidiProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", AudioChannelSet::stereo(), true))
{
}

AudioProcessorEditor* TabMidiProcessor::createEditor() { return new TabMidiEditor (*this); }

void TabMidiProcessor::commitEditedTrack()
{
    if (activeTrack >= 0 && activeTrack < (int) song.tracks.size() && ! exactValid)
        song.tracks[(size_t) activeTrack].notes = score.toNoteEvents();
    exactValid = true;
}

void TabMidiProcessor::setImported (ImportResult&& r, const String& name, bool sameSong)
{
    const ScopedLock sl (lock);
    if (sameSong) commitEditedTrack();                       // keep edits made to the track we are leaving
    else { song = std::move (r.song); song.name = name; activeTrack = -1; }

    if (r.hasEditorTrack)
    {
        score = std::move (r.score);
        activeTrack = r.editorTrack;
        exactValid = true;
    }
    else if (! sameSong) exactValid = false;
    sourceName = name;
}

int TabMidiProcessor::exportTrackCount() const
{
    const ScopedLock sl (const_cast<CriticalSection&> (lock));
    int n = 0;
    for (auto& t : song.tracks) if (! t.notes.empty()) ++n;
    return n;
}

std::vector<File> TabMidiProcessor::makeMidiFiles (bool separate)
{
    const ScopedLock sl (lock);
    auto dir = File::getSpecialLocation (File::tempDirectory).getChildFile ("TabMidi").getChildFile ("export");
    dir.createDirectory();
    for (auto& old : dir.findChildFiles (File::findFiles, false, "*.mid")) old.deleteFile();   // only the latest export stays

    std::vector<File> out;
    auto write = [&] (const String& fileName, const MidiFile& mf)
    {
        auto f = dir.getChildFile (File::createLegalFileName (fileName) + ".mid");
        if (auto os = f.createOutputStream())
        {
            mf.writeTo (*os, 1);
            os->flush();
            out.push_back (f);
        }
    };

    const String base = sourceName.isNotEmpty() ? sourceName : String ("TabMidi riff");
    if (song.tracks.empty())                                 // a hand-written tab: one file
    {
        write (base, score.toMidiFile());
        return out;
    }

    auto notesOf = [this] (int i) { return (i == activeTrack && ! exactValid) ? score.toNoteEvents() : song.tracks[(size_t) i].notes; };

    if (separate)
    {
        int n = 0;
        for (int i = 0; i < (int) song.tracks.size(); ++i)
        {
            auto notes = notesOf (i);
            if (notes.empty()) continue;
            ++n;
            const auto& tr = song.tracks[(size_t) i];
            write (base + " - " + String (n).paddedLeft ('0', 2) + " " + tr.name,
                   buildMidiFile (std::move (notes), song.tempos, song.sigs, tr.name, tr.drums ? 10 : 1));
        }
    }
    else
    {
        std::vector<MidiTrackSpec> specs;
        int channel = 1;
        for (int i = 0; i < (int) song.tracks.size(); ++i)
        {
            auto notes = notesOf (i);
            if (notes.empty()) continue;
            const auto& tr = song.tracks[(size_t) i];
            int ch = 10;
            if (! tr.drums) { if (channel == 10) ++channel; ch = channel; channel = channel >= 16 ? 1 : channel + 1; }
            specs.push_back ({ tr.name, std::move (notes), ch });
        }
        write (base + " (все дорожки)"_ru, buildMidiFileMulti (specs, song.tempos, song.sigs));
    }
    return out;
}

void TabMidiProcessor::getStateInformation (MemoryBlock& dest)
{
    const ScopedLock sl (lock);
    XmlElement root ("TABMIDI");
    root.setAttribute ("root", keyRoot);
    root.setAttribute ("scale", keyScale);
    {
        const auto& sc = scaleAt (keyScale);
        StringArray ivs;
        for (int v : sc.iv) ivs.add (String (v));
        root.setAttribute ("scaleName", sc.name);
        root.setAttribute ("scaleIv", ivs.joinIntoString (" "));
    }
    root.setAttribute ("view", viewBars);
    root.setAttribute ("ew", editorW);
    root.setAttribute ("eh", editorH);
    root.setAttribute ("dur", duration);
    root.setAttribute ("src", sourceName);
    root.addChildElement (score.toXml().release());
    copyXmlToBinary (root, dest);
}

void TabMidiProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName ("TABMIDI"))
        {
            const ScopedLock sl (lock);
            keyRoot  = jlimit (0, 11, xml->getIntAttribute ("root", 4));
            keyScale = jlimit (0, (int) scales().size() - 1, xml->getIntAttribute ("scale", 1));
            if (xml->getStringAttribute ("scaleName").isNotEmpty())
            {
                std::vector<int> iv;
                for (auto& t : StringArray::fromTokens (xml->getStringAttribute ("scaleIv"), " ", "")) iv.push_back (jlimit (0, 11, t.getIntValue()));
                if (iv.size() >= 3 && iv.front() == 0) keyScale = findOrAddScale (xml->getStringAttribute ("scaleName"), iv);
            }
            viewBars = xml->getIntAttribute ("view", 4) == 16 ? 16 : xml->getIntAttribute ("view", 4) == 8 ? 8 : 4;
            editorW = xml->getIntAttribute ("ew", 0);
            editorH = xml->getIntAttribute ("eh", 0);
            duration = jlimit (1, 16, xml->getIntAttribute ("dur", 2));
            sourceName = xml->getStringAttribute ("src");
            if (auto* sc = xml->getChildByName ("TABSCORE"))
                score.fromXml (*sc);
            exactValid = false;
        }
}
} // namespace tabmidi

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new tabmidi::TabMidiProcessor(); }
