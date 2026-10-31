#include "Params.h"

namespace pl
{
using APF  = juce::AudioParameterFloat;
using APC  = juce::AudioParameterChoice;
using APB  = juce::AudioParameterBool;
using NR   = juce::NormalisableRange<float>;
using Attr = juce::AudioParameterFloatAttributes;

const std::array<ModuleInfo, kNumModules>& getModuleInfos()
{
    static const std::array<ModuleInfo, kNumModules> infos {{
        { "boom",  "Boom Shack",   0xffe8893a, { ID::boomSub, ID::boomTune, ID::boomDecay, ID::boomNoise, ID::boomTone, ID::boomNDecay, ID::boomSens } },
        { "shift", "Shift",        0xff9b7be0, { ID::shiftPitch, ID::shiftFormant, ID::shiftFreq } },
        { "space", "Space",        0xff4fa3d9, { ID::spaceType, ID::spaceSize, ID::spaceDecay, ID::spacePredelay, ID::spaceDamp, ID::spaceWidth } },
        { "comp",  "Compress",     0xff5cc28a, { ID::compMode, ID::compSpeed, ID::compMix } },
        { "sat",   "Saturate",     0xffe0534f, { ID::satMode, ID::satDrive, ID::satTone } },
        { "exp",   "Experimental", 0xffd65db1, { ID::expMode, ID::expDiv, ID::expSize, ID::expChaos, ID::expPitch } },
        { "more",  "More",         0xffe6c547, { ID::moreChar, ID::moreCeil } },
    }};
    return infos;
}

juce::String moduleParam (int module, const char* suffix)
{
    return juce::String (getModuleInfos()[(size_t) module].prefix) + "_" + suffix;
}

juce::StringArray targetChoices()     { return { "Full", "Transient", "Sustain" }; }
juce::StringArray spaceTypeChoices()  { return { "Room", "Plate", "Hall", "Gated", "Ambience" }; }
juce::StringArray compModeChoices()   { return { "Glue", "Punch", "Smash", "Pump", "Opto", "Limit" }; }
juce::StringArray satModeChoices()    { return { "Tape", "Tube", "Transistor", "Fuzz", "Fold", "Crush" }; }
juce::StringArray expModeChoices()    { return { "Granular", "Stutter", "Reverse", "Tape Stop", "Scatter", "Ring Mod" }; }
juce::StringArray expDivChoices()     { return { "1/2", "1/4", "1/8", "1/16", "1/32", "1/4T", "1/8T", "1/16T" }; }

double expDivisionInBeats (int index)
{
    static const double d[] = { 2.0, 1.0, 0.5, 0.25, 0.125, 2.0 / 3.0, 1.0 / 3.0, 1.0 / 6.0 };
    return d[juce::jlimit (0, 7, index)];
}

// ---------- форматирование значений -------------------------------------------
static juce::String fmtPercent (float v, int)  { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; }
static juce::String fmtDb (float v, int)       { return (v > 0.0f ? "+" : "") + juce::String (v, 1) + " dB"; }
static juce::String fmtMs (float v, int)       { return v < 10.0f ? juce::String (v, 1) + " ms" : juce::String (juce::roundToInt (v)) + " ms"; }
static juce::String fmtSec (float v, int)      { return juce::String (v, 2) + " s"; }
static juce::String fmtSemi (float v, int)     { return (v > 0.0f ? "+" : "") + juce::String (v, 1) + " st"; }
static juce::String fmtBipolar (float v, int)  { return (v > 0.0f ? "+" : "") + juce::String (juce::roundToInt (v * 100.0f)) + " %"; }
static juce::String fmtX (float v, int)        { return "x" + juce::String (v, 2); }
static juce::String fmtHz (float v, int)
{
    if (std::abs (v) >= 1000.0f) return juce::String (v / 1000.0f, 2) + " kHz";
    return juce::String (juce::roundToInt (v)) + " Hz";
}
static juce::String fmtHzSigned (float v, int)  { return (v > 0.0f ? "+" : "") + fmtHz (v, 0); }

static NR freqRange (float lo, float hi)
{
    NR r (lo, hi);
    r.setSkewForCentre (std::sqrt (lo * hi));
    return r;
}

static NR skewed (float lo, float hi, float centre)
{
    NR r (lo, hi);
    r.setSkewForCentre (centre);
    return r;
}

// Симметричный диапазон с «лупой» около нуля (для частотного сдвига)
static NR symmetricSkew (float maxAbs)
{
    return NR (-maxAbs, maxAbs,
        [maxAbs] (float, float, float n)   { float x = 2.0f * n - 1.0f; return maxAbs * x * std::abs (x); },
        [maxAbs] (float, float, float v)   { float x = v / maxAbs; float s = std::sqrt (std::abs (x)); return 0.5f * (1.0f + (x < 0 ? -s : s)); },
        nullptr);
}

using Layout = juce::AudioProcessorValueTreeState::ParameterLayout;

static void addFloat (Layout& l, const juce::String& id, const juce::String& name, NR range, float def,
                      juce::String (*fmt)(float, int), const juce::String& label = {})
{
    l.add (std::make_unique<APF> (juce::ParameterID { id, 1 }, name, range, def,
                                  Attr().withStringFromValueFunction (fmt).withLabel (label)));
}

static void addChoice (Layout& l, const juce::String& id, const juce::String& name, const juce::StringArray& c, int def)
{
    l.add (std::make_unique<APC> (juce::ParameterID { id, 1 }, name, c, def));
}

static void addBool (Layout& l, const juce::String& id, const juce::String& name, bool def)
{
    l.add (std::make_unique<APB> (juce::ParameterID { id, 1 }, name, def));
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    Layout l;

    // ---------------- Глобальные ----------------
    addFloat (l, ID::magnitude, "Magnitude", NR (0.0f, 1.0f), 1.0f, fmtPercent);
    addFloat (l, ID::mix,       "Mix",       NR (0.0f, 1.0f), 1.0f, fmtPercent);
    addFloat (l, ID::output,    "Output",    NR (-24.0f, 12.0f), 0.0f, fmtDb, "dB");
    addBool  (l, ID::bypass,    "Bypass", false);
    addBool  (l, ID::loudMatch, "Loudness Match", false);
    addFloat (l, ID::splitTime, "Target Length", skewed (5.0f, 250.0f, 40.0f), 40.0f, fmtMs, "ms");
    addFloat (l, ID::splitSens, "Target Sensitivity", NR (0.0f, 1.0f), 0.5f, fmtPercent);

    // ---------------- Мастер ----------------
    addFloat (l, ID::mLow,     "Master Low",      NR (-12.0f, 12.0f), 0.0f, fmtDb, "dB");
    addFloat (l, ID::mMid,     "Master Mid",      NR (-12.0f, 12.0f), 0.0f, fmtDb, "dB");
    addFloat (l, ID::mMidFreq, "Master Mid Freq", freqRange (150.0f, 8000.0f), 1000.0f, fmtHz, "Hz");
    addFloat (l, ID::mHigh,    "Master High",     NR (-12.0f, 12.0f), 0.0f, fmtDb, "dB");
    addFloat (l, ID::mAir,     "Master Air",      NR (-6.0f, 12.0f), 0.0f, fmtDb, "dB");
    addFloat (l, ID::mAttack,  "Master Attack",   NR (-1.0f, 1.0f), 0.0f, fmtBipolar);
    addFloat (l, ID::mSustain, "Master Sustain",  NR (-1.0f, 1.0f), 0.0f, fmtBipolar);
    addFloat (l, ID::mClip,    "Master Clip",     NR (0.0f, 1.0f), 0.0f, fmtPercent);

    // ---------------- Общие параметры модулей ----------------
    const auto& infos = getModuleInfos();
    for (int m = 0; m < kNumModules; ++m)
    {
        const juce::String n = infos[(size_t) m].name;
        const bool defaultOn = false;
        addBool   (l, moduleParam (m, "on"),       n + " On", defaultOn);
        addFloat  (l, moduleParam (m, "amount"),   n + " Amount", NR (0.0f, 1.0f), 0.5f, fmtPercent);
        addChoice (l, moduleParam (m, "target"),   n + " Target", targetChoices(), kTargetFull);
        addBool   (l, moduleParam (m, "focus"),    n + " Focus", false);
        addFloat  (l, moduleParam (m, "focus_lo"), n + " Focus Low",  freqRange (20.0f, 20000.0f), 20.0f, fmtHz, "Hz");
        addFloat  (l, moduleParam (m, "focus_hi"), n + " Focus High", freqRange (20.0f, 20000.0f), 20000.0f, fmtHz, "Hz");
    }

    // ---------------- Boom Shack ----------------
    addFloat (l, ID::boomSub,    "Boom Sub",        NR (0.0f, 1.0f), 0.7f, fmtPercent);
    addFloat (l, ID::boomTune,   "Boom Tune",       freqRange (30.0f, 150.0f), 55.0f, fmtHz, "Hz");
    addFloat (l, ID::boomDecay,  "Boom Decay",      skewed (30.0f, 1500.0f, 250.0f), 280.0f, fmtMs, "ms");
    addFloat (l, ID::boomNoise,  "Boom Noise",      NR (0.0f, 1.0f), 0.3f, fmtPercent);
    addFloat (l, ID::boomTone,   "Boom Noise Tone", freqRange (300.0f, 16000.0f), 4500.0f, fmtHz, "Hz");
    addFloat (l, ID::boomNDecay, "Boom Noise Decay",skewed (10.0f, 600.0f, 90.0f), 120.0f, fmtMs, "ms");
    addFloat (l, ID::boomSens,   "Boom Sensitivity",NR (0.0f, 1.0f), 0.6f, fmtPercent);

    // ---------------- Shift ----------------
    addFloat (l, ID::shiftPitch,   "Shift Pitch",   NR (-24.0f, 24.0f, 0.01f), 0.0f, fmtSemi, "st");
    addFloat (l, ID::shiftFormant, "Shift Formant", NR (-12.0f, 12.0f, 0.01f), 0.0f, fmtSemi, "st");
    addFloat (l, ID::shiftFreq,    "Shift Freq",    symmetricSkew (1000.0f), 0.0f, fmtHzSigned, "Hz");

    // ---------------- Space ----------------
    addChoice (l, ID::spaceType,     "Space Type", spaceTypeChoices(), 0);
    addFloat  (l, ID::spaceSize,     "Space Size",      NR (0.0f, 1.0f), 0.5f, fmtPercent);
    addFloat  (l, ID::spaceDecay,    "Space Decay",     skewed (0.1f, 10.0f, 1.2f), 1.2f, fmtSec, "s");
    addFloat  (l, ID::spacePredelay, "Space Pre-Delay", skewed (0.0f, 200.0f, 30.0f), 10.0f, fmtMs, "ms");
    addFloat  (l, ID::spaceDamp,     "Space Damping",   NR (0.0f, 1.0f), 0.4f, fmtPercent);
    addFloat  (l, ID::spaceWidth,    "Space Width",     NR (0.0f, 1.5f), 1.0f, fmtPercent);

    // ---------------- Compress ----------------
    addChoice (l, ID::compMode,  "Comp Mode",  compModeChoices(), 0);
    addFloat  (l, ID::compSpeed, "Comp Speed", skewed (0.25f, 4.0f, 1.0f), 1.0f, fmtX);
    addFloat  (l, ID::compMix,   "Comp Mix",   NR (0.0f, 1.0f), 1.0f, fmtPercent);

    // ---------------- Saturate ----------------
    addChoice (l, ID::satMode,  "Sat Mode",  satModeChoices(), 0);
    addFloat  (l, ID::satDrive, "Sat Drive", NR (0.0f, 1.0f), 0.4f, fmtPercent);
    addFloat  (l, ID::satTone,  "Sat Tone",  NR (-1.0f, 1.0f), 0.0f, fmtBipolar);

    // ---------------- Experimental ----------------
    addChoice (l, ID::expMode,  "Exp Mode",     expModeChoices(), 0);
    addChoice (l, ID::expDiv,   "Exp Division", expDivChoices(), 3);
    addFloat  (l, ID::expSize,  "Exp Size",     NR (0.0f, 1.0f), 0.5f, fmtPercent);
    addFloat  (l, ID::expChaos, "Exp Chaos",    NR (0.0f, 1.0f), 0.3f, fmtPercent);
    addFloat  (l, ID::expPitch, "Exp Pitch",    NR (-12.0f, 12.0f, 0.01f), 0.0f, fmtSemi, "st");

    // ---------------- More ----------------
    addFloat (l, ID::moreChar, "More Character", NR (0.0f, 1.0f), 0.4f, fmtPercent);
    addFloat (l, ID::moreCeil, "More Ceiling",   NR (-12.0f, 0.0f), -0.3f, fmtDb, "dB");

    return l;
}

int moduleOfParameter (const juce::String& paramId)
{
    const auto& infos = getModuleInfos();
    for (int m = 0; m < kNumModules; ++m)
        if (paramId.startsWith (juce::String (infos[(size_t) m].prefix) + "_"))
            return m;
    return -1;
}
} // namespace pl
