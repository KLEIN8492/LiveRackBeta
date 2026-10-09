#include "PluginCategory.h"

namespace lr
{
const char* displayName (PluginCategory c)
{
    switch (c)
    {
        case PluginCategory::eq:           return "EQ / Filter";
        case PluginCategory::compressor:   return "Compressor";
        case PluginCategory::limiter:      return "Limiter";
        case PluginCategory::gate:         return "Gate / Expander";
        case PluginCategory::deEsser:      return "De-esser";
        case PluginCategory::multiband:    return "Multiband / Dyn EQ";
        case PluginCategory::channelStrip: return "Channel strip";
        case PluginCategory::reverb:       return "Reverb";
        case PluginCategory::delay:        return "Delay / Echo";
        case PluginCategory::modulation:   return "Modulation";
        case PluginCategory::pitch:        return "Pitch / Tune";
        case PluginCategory::saturation:   return "Saturation";
        case PluginCategory::ampSim:       return "Amp / Cab";
        case PluginCategory::feedback:     return "Feedback";
        case PluginCategory::restoration:  return "Restoration / Noise";
        case PluginCategory::multiFx:      return "Multi-FX / Suite";
        case PluginCategory::utility:      return "Utility / Meter";
        case PluginCategory::instrument:   return "Instrument";
        case PluginCategory::other:        return "Other";
        case PluginCategory::count:        break;
    }
    return "Other";
}

namespace
{
bool hasAny (const juce::String& text, std::initializer_list<const char*> words)
{
    for (auto* w : words)
        if (text.contains (w))
            return true;
    return false;
}

// Whole-word-ish match for short tokens like "c6", "la-2a", "l2".
bool hasToken (const juce::String& text, std::initializer_list<const char*> tokens)
{
    auto words = juce::StringArray::fromTokens (text, " _/|()[],.", "");
    for (auto* t : tokens)
        if (words.contains (t))
            return true;
    return false;
}
} // namespace

PluginCategory classify (const juce::PluginDescription& d)
{
    using C = PluginCategory;
    const auto name = d.name.toLowerCase();
    const auto cat  = d.category.toLowerCase();

    if (d.isInstrument || (cat.startsWith ("instrument") && ! cat.contains ("fx")))
        return C::instrument;

    // Rules run from most to least specific. Watch for words hiding inside
    // others: "Valhalla"/"Marshall" contain "hall", "Preamp" contains "amp",
    // "Limited" contains "limit", "Linear Phase EQ" contains "phase".

    if (hasAny (name, { "feedback", "fdbk", "howl" }))                   return C::feedback;

    // Products whose names say nothing about what they do.
    if (hasAny (name, { "silencer" }))                                   return C::gate;
    if (hasAny (name, { "kickstart", "duck" }))                          return C::compressor;
    if (hasAny (name, { "comeback kid" }))                               return C::delay;
    if (hasAny (name, { "auto-key", "manipulator" }))                    return C::pitch;
    if (hasAny (name, { "goyo" }))                                       return C::restoration;
    if (hasAny (name, { "rift", "ruina", "fresh air" }))                 return C::saturation;
    if (hasAny (name, { "monstr" }))                                     return C::multiband;
    if (hasToken (name, { "ssq" }))                                      return C::eq;
    if (hasAny (name, { "soundid", "vision 4x", "afx2daw", "trigg", "console recall" })) return C::utility;
    if (hasAny (name, { "mic collection", "mic modeler", "townsend", "sphere" }))       return C::eq; // mic modelling
    if (hasAny (name, { "nectar", "landr fx", "avox", "mixbox", "tr5 suite", "tr5 one", "thermal", "god particle",
                        "neutone", "i wish", "voyager", "c-suite", "multi-fx", "multifx" }))
        return C::multiFx;

    // Repair tools and meters name the thing they act on ("De-reverb", "Guitar De-noise").
    if (hasAny (name, { "de-ess", "deess", "de ess", "sibilan", "pro-ds", "rdeesser" }))      return C::deEsser;
    if (hasAny (name, { "denoise", "de-noise", "noise reduc", "declick", "de-click", "dereverb", "de-reverb",
                        "dehum", "de-hum", "declip", "de-clip", "de-plosive", "breath", "mouth de", "voice de",
                        "clarity", "isolat", "extract dialogue", "dialogue", "repair", "restor", "debleed",
                        "de-bleed", "voicegate", "rx " }))
        return C::restoration;
    if (hasAny (name, { "meter", "analy", "scope", "spectr", "tuner" }))
        return C::utility;

    // Dynamics.
    if (hasAny (name, { "channel strip", "channelstrip", "mixhub", "omni channel", "vocal strip", " channel" })
        || hasToken (name, { "e-channel", "g-channel" }) || cat.contains ("channel strip"))
        return C::channelStrip;
    if (hasAny (name, { "multiband", "multi-band", "multi band", "dynamic eq", "dyn eq", "pro-mb", "soothe",
                        "resonance", "trackspacer", "maxxbass", "silk", "unmask" })
        || hasToken (name, { "c4", "c6", "f6", "linmb", "mv2" }))
        return C::multiband;
    if (hasAny (name, { "limiter", "maximi", "loudness", "clipper", "pro-l", "ozone" })
        || hasToken (name, { "l1", "l2", "l3", "l3-16", "l3-ll", "limit" }))
        return C::limiter;
    if (hasAny (name, { "gate", "expander", "pro-g" }))                  return C::gate;
    if (hasAny (name, { "comp", "1176", "la-2", "la-3a", "la2a", "cla-2a", "cla-3a", "cla-76", "fairchild", "dbx",
                        "distressor", "optical", "opto", "leveler", "leveling", "pro-c", "rvox", "rcomp",
                        "vocal rider", "kramer pie", "glue", "vari-mu", "variable mu", "varimu", "manley vari",
                        "tube-tech cl", "puncher", "dynamics", "black 76", "white 2a", "mpressor", "dyna-mu" })
        || hasToken (name, { "vca", "fet", "76" }))
        return C::compressor;

    // Tone.
    if (hasAny (name, { "pultec", "pro-q", "puigtec", "equaliz", "parametric", "graphic eq", "tilt", "q10",
                        "volcano", "simplon", "curve bender", "linear phase eq" })
        || hasToken (name, { "eq", "eqp-1a", "meq-5", "hlf-3c", "micro", "filter" }))
        return C::eq;
    if (hasAny (name, { "amplitube", "tonex", "amp room", "guitar amp", "bass amp", "marshall", "fender", "mesa",
                        "ampeg", "engl", "plexi", "silverface", "showtime", "tweed", "bluesbreaker", "archon",
                        "dallas", "th-u", "cabinet", "friedman", "suhr", "diezel", "bogner", "orange",
                        "dumble", "matchless", "vox ac", "soldano", "peavey", "5150", "jcm" })
        || hasToken (name, { "amp", "cab", "prs" }))
        return C::ampSim;

    // Time and modulation.
    if (hasAny (name, { "chorus", "flang", "phaser", "tremolo", "vibrato", "rotary", "leslie", "ensemble",
                        "dimension", "microshift", "modulat", "panman", "vibe", "doubler" }))
        return C::modulation;
    if (hasAny (name, { "delay", "echo", "timeless", "galaxy", "echoboy", "freqecho", "slapback", "ubermod",
                        "repeater", "sdd-3000", "dmx" }))
        return C::delay;
    if (hasAny (name, { "verb", "plate", "room", "chamber", "spring", "ambien", "shimmer", "supermassive",
                        "pro-r", "emt", "lexicon", "cathedral", "bx 20", "ocean way studios" })
        || hasToken (name, { "hall" }))
        return C::reverb;
    if (hasAny (name, { "tune", "pitch", "harmon", "alterboy", "vocalign", "melodyne", "octav", "formant",
                        "vocalsynth", "voice synth", "deja vu" }))
        return C::pitch;
    if (hasAny (name, { "satur", "distort", "drive", "tube", "tape", "decapitator", "saturn", "crush", "fuzz",
                        "warm", "heat", "exciter", "aural", "harmonics", "culture", "radiator", "transformer",
                        "studer", "ampex", "inflator" }))
        return C::saturation;
    if (hasAny (name, { "gain", "trim", "utility", "imager", "shuffler", "stereo imag", "align", "invert",
                        "polarity", "test", "oscillator", "panner", "balance" }))
        return C::utility;

    // ---- fall back to the format's own category -----------------------------
    if (cat.contains ("vocals"))                          return C::multiFx;
    if (cat.contains ("eq") || cat.contains ("filter"))   return C::eq;
    if (cat.contains ("reverb"))                          return C::reverb;
    if (cat.contains ("delay"))                           return C::delay;
    if (cat.contains ("modulation"))                      return C::modulation;
    if (cat.contains ("pitch"))                           return C::pitch;
    if (cat.contains ("guitar"))                          return C::ampSim;
    if (cat.contains ("distortion"))                      return C::saturation;
    if (cat.contains ("restoration"))                     return C::restoration;
    if (cat.contains ("dynamics"))                        return C::compressor;
    if (cat.contains ("mastering"))                       return C::limiter;
    if (cat.contains ("analyzer") || cat.contains ("tools") || cat.contains ("spatial")) return C::utility;
    if (cat.contains ("microphone"))                      return C::eq;   // mic modelling = tonal

    return C::other;
}

juce::String categoryKey (PluginCategory c)
{
    return juce::String ((int) c);
}

PluginCategory categoryFromKey (const juce::String& key)
{
    const int v = key.getIntValue();
    return key.containsOnly ("0123456789") && key.isNotEmpty() && v >= 0 && v < (int) PluginCategory::count
               ? (PluginCategory) v : PluginCategory::other;
}

} // namespace lr
