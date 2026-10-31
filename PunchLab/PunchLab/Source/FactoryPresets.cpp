#include "PresetManager.h"

namespace pl
{
// Значения указываются в реальных единицах параметров. Всё, что не указано,
// принимает значение по умолчанию (модули по умолчанию выключены).
const std::vector<FactoryPreset>& getFactoryPresets()
{
    static const std::vector<FactoryPreset> presets = []
    {
        const ModuleOrder def = defaultModuleOrder();
        const ModuleOrder spaceFirst { kBoom, kSpace, kShift, kSaturate, kCompress, kExperimental, kMore };
        const ModuleOrder compFirst  { kCompress, kBoom, kSaturate, kShift, kExperimental, kSpace, kMore };
        const ModuleOrder satLast    { kBoom, kShift, kCompress, kExperimental, kSpace, kSaturate, kMore };
        const ModuleOrder expFirst   { kExperimental, kBoom, kShift, kSaturate, kCompress, kSpace, kMore };

        std::vector<FactoryPreset> p;
        auto add = [&p] (const char* name, const char* cat, std::vector<std::pair<juce::String, float>> v, ModuleOrder o)
        {
            p.push_back ({ name, cat, std::move (v), o });
        };

        // ------------------------------------------------------------ Init
        add ("Init", "Basic", {}, def);

        // ------------------------------------------------------------ Kick
        add ("Kick - Sub Boom", "Kick", {
            { "boom_on", 1 }, { "boom_amount", 0.75f }, { "boom_sub", 0.9f }, { "boom_noise", 0.0f },
            { "boom_tune", 48 }, { "boom_decay", 420 }, { "boom_sens", 0.6f },
            { "comp_on", 1 }, { "comp_amount", 0.35f }, { "comp_mode", 1 },
            { "m_low", 2 }, { "m_clip", 0.3f } }, def);

        add ("Kick - Punch & Click", "Kick", {
            { "comp_on", 1 }, { "comp_amount", 0.6f }, { "comp_mode", 1 }, { "comp_speed", 0.8f },
            { "sat_on", 1 }, { "sat_amount", 0.5f }, { "sat_mode", 2 }, { "sat_drive", 0.45f }, { "sat_target", 1 },
            { "m_attack", 0.5f }, { "m_mid", -2 }, { "m_midfreq", 350 }, { "m_high", 2.5f } }, compFirst);

        add ("Kick - 808 Tail", "Kick", {
            { "boom_on", 1 }, { "boom_amount", 0.85f }, { "boom_sub", 1 }, { "boom_noise", 0 }, { "boom_tune", 42 }, { "boom_decay", 1100 },
            { "sat_on", 1 }, { "sat_amount", 0.6f }, { "sat_mode", 0 }, { "sat_drive", 0.5f }, { "sat_target", 2 },
            { "more_on", 1 }, { "more_amount", 0.35f }, { "more_char", 0.3f },
            { "m_sustain", 0.3f } }, satLast);

        add ("Kick - Tight Dry", "Kick", {
            { "comp_on", 1 }, { "comp_amount", 0.45f }, { "comp_mode", 5 },
            { "m_sustain", -0.6f }, { "m_attack", 0.3f }, { "m_low", 1.5f }, { "m_mid", -3 }, { "m_midfreq", 420 } }, def);

        // ------------------------------------------------------------ Snare
        add ("Snare - Crack", "Snare", {
            { "boom_on", 1 }, { "boom_amount", 0.6f }, { "boom_sub", 0 }, { "boom_noise", 0.8f }, { "boom_tone", 5500 }, { "boom_ndecay", 140 },
            { "comp_on", 1 }, { "comp_amount", 0.5f }, { "comp_mode", 1 },
            { "space_on", 1 }, { "space_amount", 0.25f }, { "space_type", 0 }, { "space_size", 0.35f }, { "space_decay", 0.6f },
            { "m_attack", 0.4f }, { "m_high", 2 } }, def);

        add ("Snare - Gated 80s", "Snare", {
            { "space_on", 1 }, { "space_amount", 0.7f }, { "space_type", 3 }, { "space_size", 0.7f }, { "space_decay", 1.4f }, { "space_predelay", 5 },
            { "comp_on", 1 }, { "comp_amount", 0.55f }, { "comp_mode", 2 }, { "comp_mix", 0.6f },
            { "boom_on", 1 }, { "boom_amount", 0.4f }, { "boom_sub", 0 }, { "boom_noise", 0.6f }, { "boom_tone", 3500 },
            { "m_mid", 2 }, { "m_midfreq", 220 } }, spaceFirst);

        add ("Snare - Fat Body", "Snare", {
            { "shift_on", 1 }, { "shift_amount", 0.4f }, { "shift_pitch", -3 }, { "shift_formant", 2 },
            { "sat_on", 1 }, { "sat_amount", 0.55f }, { "sat_mode", 1 }, { "sat_drive", 0.5f },
            { "comp_on", 1 }, { "comp_amount", 0.45f }, { "comp_mode", 4 },
            { "m_low", 2 }, { "m_mid", 1.5f }, { "m_midfreq", 250 } }, def);

        add ("Snare - Trashy Room", "Snare", {
            { "sat_on", 1 }, { "sat_amount", 0.7f }, { "sat_mode", 3 }, { "sat_drive", 0.55f }, { "sat_tone", 0.2f },
            { "space_on", 1 }, { "space_amount", 0.45f }, { "space_type", 0 }, { "space_size", 0.5f }, { "space_decay", 0.9f }, { "space_damp", 0.2f },
            { "comp_on", 1 }, { "comp_amount", 0.7f }, { "comp_mode", 2 }, { "comp_mix", 0.5f } }, def);

        // ------------------------------------------------------------ Hats & Perc
        add ("Hats - Sizzle", "Hats & Perc", {
            { "boom_on", 1 }, { "boom_amount", 0.4f }, { "boom_sub", 0 }, { "boom_noise", 0.7f }, { "boom_tone", 11000 }, { "boom_ndecay", 60 }, { "boom_sens", 0.8f },
            { "sat_on", 1 }, { "sat_amount", 0.35f }, { "sat_mode", 0 }, { "sat_drive", 0.35f }, { "sat_tone", 0.4f },
            { "m_air", 4 } }, def);

        add ("Hats - Metallic Shift", "Hats & Perc", {
            { "shift_on", 1 }, { "shift_amount", 0.5f }, { "shift_freq", 180 },
            { "space_on", 1 }, { "space_amount", 0.25f }, { "space_type", 1 }, { "space_size", 0.4f }, { "space_decay", 1.0f },
            { "m_low", -4 } }, def);

        add ("Perc - Wide Plate", "Hats & Perc", {
            { "space_on", 1 }, { "space_amount", 0.5f }, { "space_type", 1 }, { "space_size", 0.6f }, { "space_decay", 1.8f }, { "space_width", 1.4f },
            { "space_target", 1 }, { "comp_on", 1 }, { "comp_amount", 0.3f }, { "comp_mode", 0 } }, def);

        add ("Perc - Formant Talk", "Hats & Perc", {
            { "shift_on", 1 }, { "shift_amount", 0.8f }, { "shift_pitch", 0 }, { "shift_formant", -7 },
            { "sat_on", 1 }, { "sat_amount", 0.4f }, { "sat_mode", 1 }, { "sat_drive", 0.35f } }, def);

        // ------------------------------------------------------------ Bus
        add ("Bus - Glue", "Drum Bus", {
            { "comp_on", 1 }, { "comp_amount", 0.4f }, { "comp_mode", 0 },
            { "sat_on", 1 }, { "sat_amount", 0.35f }, { "sat_mode", 0 }, { "sat_drive", 0.3f },
            { "m_low", 1 }, { "m_air", 1.5f }, { "m_clip", 0.2f } }, def);

        add ("Bus - Parallel Smash", "Drum Bus", {
            { "comp_on", 1 }, { "comp_amount", 0.8f }, { "comp_mode", 2 }, { "comp_mix", 0.45f },
            { "sat_on", 1 }, { "sat_amount", 0.3f }, { "sat_mode", 2 }, { "sat_drive", 0.4f },
            { "m_attack", 0.2f } }, compFirst);

        add ("Bus - Tape Warmth", "Drum Bus", {
            { "sat_on", 1 }, { "sat_amount", 0.65f }, { "sat_mode", 0 }, { "sat_drive", 0.5f }, { "sat_tone", -0.15f },
            { "comp_on", 1 }, { "comp_amount", 0.3f }, { "comp_mode", 4 },
            { "m_low", 1.5f }, { "m_high", -1 } }, def);

        add ("Bus - Room Mic Glue", "Drum Bus", {
            { "space_on", 1 }, { "space_amount", 0.35f }, { "space_type", 0 }, { "space_size", 0.45f }, { "space_decay", 0.8f },
            { "comp_on", 1 }, { "comp_amount", 0.55f }, { "comp_mode", 0 },
            { "more_on", 1 }, { "more_amount", 0.25f } }, spaceFirst);

        add ("Bus - Loud & Dense", "Drum Bus", {
            { "comp_on", 1 }, { "comp_amount", 0.45f }, { "comp_mode", 0 },
            { "sat_on", 1 }, { "sat_amount", 0.4f }, { "sat_mode", 1 }, { "sat_drive", 0.35f },
            { "more_on", 1 }, { "more_amount", 0.6f }, { "more_char", 0.55f }, { "more_ceil", -0.5f },
            { "m_clip", 0.35f } }, def);

        add ("Bus - Pumping House", "Drum Bus", {
            { "comp_on", 1 }, { "comp_amount", 0.75f }, { "comp_mode", 3 }, { "comp_speed", 1.2f },
            { "space_on", 1 }, { "space_amount", 0.3f }, { "space_type", 2 }, { "space_decay", 2.0f },
            { "m_low", 1 } }, spaceFirst);

        add ("Bus - Transient Focus Verb", "Drum Bus", {
            { "space_on", 1 }, { "space_amount", 0.55f }, { "space_type", 2 }, { "space_size", 0.7f }, { "space_decay", 2.5f },
            { "space_target", 1 }, { "space_focus", 1 }, { "space_focus_lo", 400 }, { "space_focus_hi", 9000 } }, def);

        // ------------------------------------------------------------ Loops
        add ("Loop - Lo-Fi Crush", "Loops", {
            { "sat_on", 1 }, { "sat_amount", 0.6f }, { "sat_mode", 5 }, { "sat_drive", 0.55f }, { "sat_tone", -0.3f },
            { "comp_on", 1 }, { "comp_amount", 0.55f }, { "comp_mode", 4 },
            { "m_high", -4 }, { "m_low", 2 } }, def);

        add ("Loop - Dusty Vinyl Room", "Loops", {
            { "sat_on", 1 }, { "sat_amount", 0.5f }, { "sat_mode", 0 }, { "sat_drive", 0.6f }, { "sat_tone", -0.5f },
            { "space_on", 1 }, { "space_amount", 0.3f }, { "space_type", 4 }, { "space_size", 0.6f },
            { "m_air", -4 } }, def);

        add ("Loop - Pitched Down Grit", "Loops", {
            { "shift_on", 1 }, { "shift_amount", 1.0f }, { "shift_pitch", -5 }, { "shift_formant", 0 },
            { "sat_on", 1 }, { "sat_amount", 0.5f }, { "sat_mode", 2 }, { "sat_drive", 0.4f },
            { "comp_on", 1 }, { "comp_amount", 0.4f }, { "comp_mode", 0 } }, def);

        // ------------------------------------------------------------ Experimental
        add ("FX - Granular Cloud", "Experimental", {
            { "exp_on", 1 }, { "exp_amount", 0.6f }, { "exp_mode", 0 }, { "exp_size", 0.55f }, { "exp_chaos", 0.6f }, { "exp_pitch", 0 },
            { "space_on", 1 }, { "space_amount", 0.4f }, { "space_type", 2 }, { "space_decay", 3 } }, def);

        add ("FX - Stutter Fills", "Experimental", {
            { "exp_on", 1 }, { "exp_amount", 1.0f }, { "exp_mode", 1 }, { "exp_div", 2 }, { "exp_size", 0.5f }, { "exp_chaos", 0.6f } }, expFirst);

        add ("FX - Reverse Swells", "Experimental", {
            { "exp_on", 1 }, { "exp_amount", 0.8f }, { "exp_mode", 2 }, { "exp_div", 1 }, { "exp_size", 1.0f }, { "exp_chaos", 0.5f },
            { "space_on", 1 }, { "space_amount", 0.35f }, { "space_type", 2 } }, expFirst);

        add ("FX - Tape Stop Drops", "Experimental", {
            { "exp_on", 1 }, { "exp_amount", 1.0f }, { "exp_mode", 3 }, { "exp_div", 0 }, { "exp_size", 0.35f }, { "exp_chaos", 0.5f } }, expFirst);

        add ("FX - Scatter Break", "Experimental", {
            { "exp_on", 1 }, { "exp_amount", 1.0f }, { "exp_mode", 4 }, { "exp_div", 3 }, { "exp_size", 0.6f }, { "exp_chaos", 0.45f },
            { "comp_on", 1 }, { "comp_amount", 0.4f }, { "comp_mode", 0 } }, expFirst);

        add ("FX - Ring Clang", "Experimental", {
            { "exp_on", 1 }, { "exp_amount", 0.45f }, { "exp_mode", 5 }, { "exp_size", 0.55f }, { "exp_chaos", 0.3f },
            { "exp_target", 1 }, { "space_on", 1 }, { "space_amount", 0.3f }, { "space_type", 1 } }, def);

        add ("FX - Octave Ghosts", "Experimental", {
            { "shift_on", 1 }, { "shift_amount", 0.45f }, { "shift_pitch", 12 }, { "shift_target", 2 },
            { "space_on", 1 }, { "space_amount", 0.5f }, { "space_type", 2 }, { "space_decay", 4 }, { "space_width", 1.5f } }, def);

        // ------------------------------------------------------------ Extreme
        add ("Extreme - Fold & Smash", "Extreme", {
            { "sat_on", 1 }, { "sat_amount", 0.8f }, { "sat_mode", 4 }, { "sat_drive", 0.6f },
            { "comp_on", 1 }, { "comp_amount", 0.85f }, { "comp_mode", 2 },
            { "more_on", 1 }, { "more_amount", 0.5f }, { "more_char", 0.8f } }, def);

        add ("Extreme - Fuzz Box", "Extreme", {
            { "sat_on", 1 }, { "sat_amount", 0.9f }, { "sat_mode", 3 }, { "sat_drive", 0.8f }, { "sat_tone", -0.3f },
            { "boom_on", 1 }, { "boom_amount", 0.5f }, { "boom_sub", 0.6f }, { "boom_noise", 0.3f },
            { "m_clip", 0.5f } }, satLast);

        add ("Extreme - Everything On", "Extreme", {
            { "boom_on", 1 }, { "shift_on", 1 }, { "shift_amount", 0.25f }, { "shift_pitch", -2 },
            { "space_on", 1 }, { "space_amount", 0.3f }, { "comp_on", 1 }, { "comp_amount", 0.5f },
            { "sat_on", 1 }, { "sat_amount", 0.5f }, { "exp_on", 1 }, { "exp_amount", 0.25f }, { "exp_mode", 0 },
            { "more_on", 1 }, { "more_amount", 0.4f }, { "magnitude", 0.8f } }, def);

        return p;
    }();
    return presets;
}
} // namespace pl
