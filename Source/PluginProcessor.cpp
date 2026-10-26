#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace tabmidi
{
TabMidiProcessor::TabMidiProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", AudioChannelSet::stereo(), true))
{
}

AudioProcessorEditor* TabMidiProcessor::createEditor() { return new TabMidiEditor (*this); }

void TabMidiProcessor::setImported (ImportResult&& r, const String& name)
{
    const ScopedLock sl (lock);
    score = std::move (r.score);
    exact = std::move (r.exact);
    exactValid = true;
    sourceName = name;
}

File TabMidiProcessor::makeMidiFile()
{
    const ScopedLock sl (lock);
    MidiFile mf = exactValid ? exact : score.toMidiFile();

    auto dir = File::getSpecialLocation (File::tempDirectory).getChildFile ("TabMidi");
    dir.createDirectory();
    const String base = sourceName.isNotEmpty() ? File::createLegalFileName (sourceName)
                                                : String ("TabMidi riff");
    auto f = dir.getNonexistentChildFile (base, ".mid", false);
    if (auto os = f.createOutputStream())
    {
        mf.writeTo (*os, 1);
        os->flush();
        return f;
    }
    return {};
}

void TabMidiProcessor::getStateInformation (MemoryBlock& dest)
{
    const ScopedLock sl (lock);
    XmlElement root ("TABMIDI");
    root.setAttribute ("root", keyRoot);
    root.setAttribute ("scale", keyScale);
    root.setAttribute ("dur", duration);
    root.setAttribute ("chords", showChords ? 1 : 0);
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
            keyRoot  = jlimit (0, 11, xml->getIntAttribute ("root", 9));
            keyScale = jlimit (0, (int) scales().size() - 1, xml->getIntAttribute ("scale", 1));
            duration = jlimit (1, 16, xml->getIntAttribute ("dur", 2));
            showChords = xml->getIntAttribute ("chords", 1) != 0;
            sourceName = xml->getStringAttribute ("src");
            if (auto* sc = xml->getChildByName ("TABSCORE"))
                score.fromXml (*sc);
            exactValid = false;
        }
}
} // namespace tabmidi

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new tabmidi::TabMidiProcessor(); }
