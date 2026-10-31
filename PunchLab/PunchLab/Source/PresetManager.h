#pragma once

#include "Params.h"
#include <functional>
#include <map>

namespace pl
{
using ModuleOrder = std::array<int, kNumModules>;

ModuleOrder defaultModuleOrder();
bool isValidOrder (const ModuleOrder& o);
juce::String orderToString (const ModuleOrder& o);
ModuleOrder orderFromString (const juce::String& s);

struct FactoryPreset
{
    juce::String name, category;
    std::vector<std::pair<juce::String, float>> values;  // реальные значения параметров
    ModuleOrder order;
};
const std::vector<FactoryPreset>& getFactoryPresets();

class PresetManager
{
public:
    struct Entry
    {
        juce::String name, category;
        bool isFactory = true;
        int factoryIndex = -1;
        juce::File file;
    };

    PresetManager (juce::AudioProcessorValueTreeState& state,
                   std::function<ModuleOrder()> getOrder,
                   std::function<void (const ModuleOrder&)> setOrder);

    void refresh();
    const std::vector<Entry>& getEntries() const { return entries; }

    void load (int entryIndex);
    void loadNext();
    void loadPrevious();
    bool saveUserPreset (const juce::String& name);
    bool deleteUserPreset (int entryIndex);

    int getCurrentIndex() const          { return currentIndex; }
    juce::String getCurrentName() const  { return currentName; }
    void setCurrentName (const juce::String& n, int idx) { currentName = n; currentIndex = idx; }

    static juce::File getUserPresetDirectory();

    // Блокировка модулей: параметры и позиция заблокированного модуля
    // не меняются при загрузке пресета
    std::array<std::atomic<bool>, kNumModules> locks {};

    static bool isExcludedFromPresets (const juce::String& id);

private:
    void applyValues (const std::map<juce::String, float>& values, const ModuleOrder* order);

    juce::AudioProcessorValueTreeState& apvts;
    std::function<ModuleOrder()> getOrderFn;
    std::function<void (const ModuleOrder&)> setOrderFn;
    std::vector<Entry> entries;
    int currentIndex = 0;
    juce::String currentName { "Init" };
};
} // namespace pl
