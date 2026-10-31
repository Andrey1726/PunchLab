#include "PresetManager.h"
#include <map>

namespace pl
{
ModuleOrder defaultModuleOrder()
{
    // Boom → Shift → Saturate → Compress → Experimental → Space → More
    return { kBoom, kShift, kSaturate, kCompress, kExperimental, kSpace, kMore };
}

bool isValidOrder (const ModuleOrder& o)
{
    std::array<bool, kNumModules> seen {};
    for (int m : o)
    {
        if (m < 0 || m >= kNumModules || seen[(size_t) m]) return false;
        seen[(size_t) m] = true;
    }
    return true;
}

juce::String orderToString (const ModuleOrder& o)
{
    juce::StringArray s;
    for (int m : o) s.add (juce::String (m));
    return s.joinIntoString (",");
}

ModuleOrder orderFromString (const juce::String& str)
{
    juce::StringArray s;
    s.addTokens (str, ",", "");
    ModuleOrder o = defaultModuleOrder();
    if (s.size() != kNumModules) return o;
    ModuleOrder r {};
    for (int i = 0; i < kNumModules; ++i) r[(size_t) i] = s[i].getIntValue();
    return isValidOrder (r) ? r : o;
}

// ============================================================================
PresetManager::PresetManager (juce::AudioProcessorValueTreeState& s,
                              std::function<ModuleOrder()> g,
                              std::function<void (const ModuleOrder&)> st)
    : apvts (s), getOrderFn (std::move (g)), setOrderFn (std::move (st))
{
    for (auto& l : locks) l.store (false);
    refresh();
}

juce::File PresetManager::getUserPresetDirectory()
{
    auto dir = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                   .getChildFile ("PunchLab").getChildFile ("Presets");
    if (! dir.exists()) dir.createDirectory();
    return dir;
}

bool PresetManager::isExcludedFromPresets (const juce::String& id)
{
    return id == ID::bypass || id == ID::loudMatch;
}

void PresetManager::refresh()
{
    entries.clear();
    const auto& fp = getFactoryPresets();
    for (int i = 0; i < (int) fp.size(); ++i)
        entries.push_back ({ fp[(size_t) i].name, fp[(size_t) i].category, true, i, {} });

    auto files = getUserPresetDirectory().findChildFiles (juce::File::findFiles, false, "*.plpreset");
    files.sort();
    for (auto& f : files)
        entries.push_back ({ f.getFileNameWithoutExtension(), "User", false, -1, f });
}

void PresetManager::applyValues (const std::map<juce::String, float>& values, const ModuleOrder* order)
{
    for (auto* p : apvts.processor.getParameters())
    {
        auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p);
        if (rp == nullptr) continue;
        const auto id = rp->getParameterID();
        if (isExcludedFromPresets (id)) continue;

        const int mod = moduleOfParameter (id);
        if (mod >= 0 && locks[(size_t) mod].load()) continue;

        float norm = rp->getDefaultValue();
        auto it = values.find (id);
        if (it != values.end()) norm = rp->convertTo0to1 (it->second);
        rp->beginChangeGesture();
        rp->setValueNotifyingHost (norm);
        rp->endChangeGesture();
    }

    // Порядок: заблокированные модули остаются на своих местах,
    // остальные места заполняются в порядке пресета
    const ModuleOrder current = getOrderFn();
    const ModuleOrder wanted  = order != nullptr ? *order : defaultModuleOrder();
    ModuleOrder result {};
    std::array<bool, kNumModules> placed {};
    std::array<bool, kNumModules> slotTaken {};
    for (int s = 0; s < kNumModules; ++s)
    {
        const int m = current[(size_t) s];
        if (locks[(size_t) m].load()) { result[(size_t) s] = m; placed[(size_t) m] = true; slotTaken[(size_t) s] = true; }
    }
    int slot = 0;
    for (int m : wanted)
    {
        if (placed[(size_t) m]) continue;
        while (slot < kNumModules && slotTaken[(size_t) slot]) ++slot;
        if (slot >= kNumModules) break;
        result[(size_t) slot] = m; slotTaken[(size_t) slot] = true; placed[(size_t) m] = true;
    }
    if (isValidOrder (result)) setOrderFn (result);
}

void PresetManager::load (int index)
{
    if (index < 0 || index >= (int) entries.size()) return;
    const auto& e = entries[(size_t) index];

    std::map<juce::String, float> values;
    ModuleOrder order = defaultModuleOrder();

    if (e.isFactory)
    {
        const auto& fp = getFactoryPresets()[(size_t) e.factoryIndex];
        for (auto& v : fp.values) values[v.first] = v.second;
        order = fp.order;
    }
    else
    {
        auto xml = juce::XmlDocument::parse (e.file);
        if (xml == nullptr || ! xml->hasTagName ("PunchLabPreset")) return;
        for (auto* p : xml->getChildWithTagNameIterator ("Param"))
            values[p->getStringAttribute ("id")] = (float) p->getDoubleAttribute ("value");
        order = orderFromString (xml->getStringAttribute ("order"));
    }

    applyValues (values, &order);
    currentIndex = index;
    currentName = e.name;
}

void PresetManager::loadNext()
{
    if (entries.empty()) return;
    load ((currentIndex + 1) % (int) entries.size());
}

void PresetManager::loadPrevious()
{
    if (entries.empty()) return;
    load ((currentIndex - 1 + (int) entries.size()) % (int) entries.size());
}

bool PresetManager::saveUserPreset (const juce::String& rawName)
{
    const auto name = juce::File::createLegalFileName (rawName.trim());
    if (name.isEmpty()) return false;

    juce::XmlElement xml ("PunchLabPreset");
    xml.setAttribute ("name", name);
    xml.setAttribute ("version", 1);
    xml.setAttribute ("order", orderToString (getOrderFn()));
    for (auto* p : apvts.processor.getParameters())
    {
        auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p);
        if (rp == nullptr || isExcludedFromPresets (rp->getParameterID())) continue;
        auto* e = xml.createNewChildElement ("Param");
        e->setAttribute ("id", rp->getParameterID());
        e->setAttribute ("value", (double) rp->convertFrom0to1 (rp->getValue()));
    }

    auto file = getUserPresetDirectory().getChildFile (name + ".plpreset");
    if (! xml.writeTo (file)) return false;

    refresh();
    for (int i = 0; i < (int) entries.size(); ++i)
        if (! entries[(size_t) i].isFactory && entries[(size_t) i].file == file) { currentIndex = i; break; }
    currentName = name;
    return true;
}

bool PresetManager::deleteUserPreset (int index)
{
    if (index < 0 || index >= (int) entries.size() || entries[(size_t) index].isFactory) return false;
    const bool ok = entries[(size_t) index].file.deleteFile();
    refresh();
    currentIndex = juce::jlimit (0, (int) entries.size() - 1, index - 1);
    return ok;
}
} // namespace pl
