#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <functional>

namespace pl::ui
{
namespace Colours
{
    const juce::Colour bg        { 0xff1d242d };
    const juce::Colour panel     { 0xff262f3a };
    const juce::Colour raised    { 0xff313c4a };
    const juce::Colour line      { 0xff3c4858 };
    const juce::Colour text      { 0xffe3e7ec };
    const juce::Colour textDim   { 0xff8794a5 };
    const juce::Colour brass     { 0xffc9a24b };
}

juce::Font font (float size, bool bold = false);

// ----------------------------------------------------------------------------
class PunchLookAndFeel : public juce::LookAndFeel_V4
{
public:
    PunchLookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool over, bool down) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override { return font (13.0f); }
    juce::Font getPopupMenuFont() override               { return font (14.0f); }
    juce::Font getTextButtonFont (juce::TextButton&, int) override { return font (13.0f, true); }
    juce::Label* createSliderTextBox (juce::Slider&) override;
    void drawLabel (juce::Graphics&, juce::Label&) override;
};

// ----------------------------------------------------------------------------
// Ручка с подписью, привязанная к параметру
class Knob : public juce::Component
{
public:
    Knob (juce::AudioProcessorValueTreeState& s, const juce::String& paramId, const juce::String& label,
          juce::Colour colour, bool large = false);
    void resized() override;
    void setTooltip (const juce::String& t) { slider.setTooltip (t); }
    juce::Slider slider;
private:
    juce::Label name;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    bool isLarge;
};

// Выпадающий список для choice-параметров
class ChoiceBox : public juce::Component
{
public:
    ChoiceBox (juce::AudioProcessorValueTreeState& s, const juce::String& paramId, const juce::String& label);
    void resized() override;
private:
    juce::Label name;
    juce::ComboBox box;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attachment;
};

// Сегментированный переключатель (Target)
class SegmentedChoice : public juce::Component, public juce::SettableTooltipClient
{
public:
    SegmentedChoice (juce::RangedAudioParameter& p, juce::Colour c);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
private:
    juce::StringArray items;
    juce::Colour colour;
    int current = 0;
    juce::ParameterAttachment attachment;
};

// Кнопка-переключатель с иконкой (питание / замок)
class IconToggle : public juce::Button
{
public:
    enum Icon { Power, Lock };
    IconToggle (Icon i, juce::Colour c) : juce::Button ({}), icon (i), colour (c) { setClickingTogglesState (true); }
    void paintButton (juce::Graphics&, bool over, bool down) override;
    void setColour (juce::Colour c) { colour = c; repaint(); }
private:
    Icon icon;
    juce::Colour colour;
};

// Текстовый переключатель, связанный с bool-параметром
class ParamToggle : public juce::TextButton
{
public:
    ParamToggle (juce::AudioProcessorValueTreeState& s, const juce::String& paramId, const juce::String& text, juce::Colour onColour);
private:
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;
};

// Индикатор уровня
class LevelMeter : public juce::Component
{
public:
    explicit LevelMeter (juce::String label) : name (std::move (label)) {}
    void setLevel (float linear);
    void paint (juce::Graphics&) override;
private:
    juce::String name;
    float levelDb = -100.0f, peakDb = -100.0f;
    int peakHold = 0;
};

// Визуализация полосы Focus на логарифмической оси
class FocusDisplay : public juce::Component
{
public:
    FocusDisplay (std::atomic<float>* on, std::atomic<float>* lo, std::atomic<float>* hi, juce::Colour c)
        : pOn (on), pLo (lo), pHi (hi), colour (c) {}
    void paint (juce::Graphics&) override;
    void refresh();
private:
    std::atomic<float>* pOn; std::atomic<float>* pLo; std::atomic<float>* pHi;
    juce::Colour colour;
    float lastLo = -1, lastHi = -1; bool lastOn = false;
};

// Горизонтальный индикатор подавления усиления
class GainReductionMeter : public juce::Component
{
public:
    void setValue (float db) { if (std::abs (db - value) > 0.05f) { value = db; repaint(); } }
    void paint (juce::Graphics&) override;
private:
    float value = 0.0f;
};

// ----------------------------------------------------------------------------
// Плитка модуля в цепочке (перетаскиваемая)
class ModuleTile : public juce::Component, public juce::DragAndDropTarget
{
public:
    ModuleTile (juce::AudioProcessorValueTreeState& s, int moduleId);

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    bool isInterestedInDragSource (const SourceDetails&) override { return true; }
    void itemDragEnter (const SourceDetails&) override { dragHover = true; repaint(); }
    void itemDragExit (const SourceDetails&) override  { dragHover = false; repaint(); }
    void itemDropped (const SourceDetails& d) override;

    void setSlotIndex (int s)   { slot = s; repaint(); }
    void setSelected (bool s)   { if (selected != s) { selected = s; repaint(); } }
    void refresh();

    std::function<void (int)> onSelect;
    std::function<void (int moduleId, int targetModuleId)> onDrop;
    IconToggle lockButton;

private:
    int moduleId, slot = 0;
    juce::Colour colour;
    juce::String title;
    bool selected = false, dragHover = false, dragging = false;
    IconToggle powerButton;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> powerAttachment;
    juce::Slider amount;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> amountAttachment;
    std::atomic<float>* pOn; std::atomic<float>* pTarget; std::atomic<float>* pFocus;
    int lastTarget = -1; bool lastFocus = false, lastOn = false;
};
} // namespace pl::ui
