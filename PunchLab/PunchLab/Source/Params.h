#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <vector>

namespace pl
{
    // Порядок индексов модулей (идентификатор модуля, НЕ позиция в цепочке)
    enum ModuleId
    {
        kBoom = 0,
        kShift,
        kSpace,
        kCompress,
        kSaturate,
        kExperimental,
        kMore,
        kNumModules
    };

    struct ModuleInfo
    {
        const char* prefix;   // префикс ID параметров: boom_on, boom_amount ...
        const char* name;     // отображаемое имя
        juce::uint32 colour;  // ARGB
        std::vector<const char*> controls; // специфичные для модуля параметры (для GUI)
    };

    const std::array<ModuleInfo, kNumModules>& getModuleInfos();

    // Общие для каждого модуля суффиксы
    juce::String moduleParam (int module, const char* suffix); // "boom" + "_" + suffix

    enum TargetMode { kTargetFull = 0, kTargetTransient, kTargetSustain };

    namespace ID
    {
        // Глобальные
        inline constexpr const char* magnitude  = "magnitude";
        inline constexpr const char* mix        = "mix";
        inline constexpr const char* output     = "output";
        inline constexpr const char* bypass     = "bypass";
        inline constexpr const char* loudMatch  = "loudmatch";
        inline constexpr const char* splitTime  = "split_time";
        inline constexpr const char* splitSens  = "split_sens";

        // Мастер-секция
        inline constexpr const char* mLow       = "m_low";
        inline constexpr const char* mMid       = "m_mid";
        inline constexpr const char* mMidFreq   = "m_midfreq";
        inline constexpr const char* mHigh      = "m_high";
        inline constexpr const char* mAir       = "m_air";
        inline constexpr const char* mAttack    = "m_attack";
        inline constexpr const char* mSustain   = "m_sustain";
        inline constexpr const char* mClip      = "m_clip";

        // Boom Shack
        inline constexpr const char* boomSub    = "boom_sub";
        inline constexpr const char* boomTune   = "boom_tune";
        inline constexpr const char* boomDecay  = "boom_decay";
        inline constexpr const char* boomNoise  = "boom_noise";
        inline constexpr const char* boomTone   = "boom_tone";
        inline constexpr const char* boomNDecay = "boom_ndecay";
        inline constexpr const char* boomSens   = "boom_sens";

        // Shift
        inline constexpr const char* shiftPitch   = "shift_pitch";
        inline constexpr const char* shiftFormant = "shift_formant";
        inline constexpr const char* shiftFreq    = "shift_freq";

        // Space
        inline constexpr const char* spaceType     = "space_type";
        inline constexpr const char* spaceSize     = "space_size";
        inline constexpr const char* spaceDecay    = "space_decay";
        inline constexpr const char* spacePredelay = "space_predelay";
        inline constexpr const char* spaceDamp     = "space_damp";
        inline constexpr const char* spaceWidth    = "space_width";

        // Compress
        inline constexpr const char* compMode  = "comp_mode";
        inline constexpr const char* compSpeed = "comp_speed";
        inline constexpr const char* compMix   = "comp_mix";

        // Saturate
        inline constexpr const char* satMode  = "sat_mode";
        inline constexpr const char* satDrive = "sat_drive";
        inline constexpr const char* satTone  = "sat_tone";

        // Experimental
        inline constexpr const char* expMode  = "exp_mode";
        inline constexpr const char* expDiv   = "exp_div";
        inline constexpr const char* expSize  = "exp_size";
        inline constexpr const char* expChaos = "exp_chaos";
        inline constexpr const char* expPitch = "exp_pitch";

        // More
        inline constexpr const char* moreChar = "more_char";
        inline constexpr const char* moreCeil = "more_ceil";
    }

    // Списки вариантов для choice-параметров
    juce::StringArray targetChoices();
    juce::StringArray spaceTypeChoices();
    juce::StringArray compModeChoices();
    juce::StringArray satModeChoices();
    juce::StringArray expModeChoices();
    juce::StringArray expDivChoices();
    double expDivisionInBeats (int index);

    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // true, если параметр принадлежит модулю (для блокировки модулей при смене пресета)
    int moduleOfParameter (const juce::String& paramId); // -1 = глобальный/мастер
}
