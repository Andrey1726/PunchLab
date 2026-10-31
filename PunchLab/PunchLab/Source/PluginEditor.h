#pragma once

#include "PluginProcessor.h"
#include "gui/Widgets.h"

// Панель настроек одного модуля (показывается для выбранной плитки)
class ModulePanel : public juce::Component
{
public:
    ModulePanel (PunchLabAudioProcessor& p, int moduleId);
    void paint (juce::Graphics&) override;
    void resized() override;
    void refresh();

private:
    PunchLabAudioProcessor& proc;
    int moduleId;
    juce::Colour colour;
    juce::String title, description;
    std::vector<std::unique_ptr<juce::Component>> controls;
    std::unique_ptr<pl::ui::SegmentedChoice> target;
    std::unique_ptr<pl::ui::Knob> splitLen, splitSens;
    std::unique_ptr<pl::ui::ParamToggle> focusToggle;
    std::unique_ptr<pl::ui::Knob> focusLo, focusHi;
    std::unique_ptr<pl::ui::FocusDisplay> focusDisplay;
    std::unique_ptr<pl::ui::GainReductionMeter> grMeter;
};

class PunchLabEditor : public juce::AudioProcessorEditor,
                       public juce::DragAndDropContainer,
                       private juce::Timer
{
public:
    explicit PunchLabEditor (PunchLabAudioProcessor&);
    ~PunchLabEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int designW = 1080, designH = 720;

private:
    // Корневой контейнер фиксированного «дизайн-размера», масштабируется целиком
    struct Root : juce::Component
    {
        std::function<void (juce::Graphics&)> painter;
        void paint (juce::Graphics& g) override { if (painter) painter (g); }
    };

    void timerCallback() override;
    void layoutChain();
    void selectModule (int moduleId);
    void showPresetMenu();
    void savePresetDialog();
    void deletePresetDialog();
    void updatePresetLabel();
    void paintRoot (juce::Graphics&);

    PunchLabAudioProcessor& proc;
    pl::ui::PunchLookAndFeel lnf;
    Root root;
    juce::TooltipWindow tooltips { this, 600 };

    // header
    juce::TextButton prevButton { "<" }, nextButton { ">" }, presetButton, saveButton { "Save" }, deleteButton { "Delete" };
    std::unique_ptr<pl::ui::ParamToggle> loudButton, bypassButton;
    std::unique_ptr<pl::ui::Knob> magnitude;

    // chain
    std::array<std::unique_ptr<pl::ui::ModuleTile>, pl::kNumModules> tiles;
    std::array<std::unique_ptr<ModulePanel>, pl::kNumModules> panels;
    int selected = pl::kBoom;
    int lastOrderVersion = -1;

    // master
    std::vector<std::unique_ptr<pl::ui::Knob>> masterKnobs;
    pl::ui::LevelMeter inMeter { "In" }, outMeter { "Out" };
    juce::Label lmLabel;

    std::unique_ptr<juce::AlertWindow> dialog;
    juce::String lastPresetName;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PunchLabEditor)
};
