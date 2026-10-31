#include "PluginEditor.h"

using namespace pl;
using namespace pl::ui;

namespace
{
    const char* shortLabel (const juce::String& id)
    {
        static const std::map<juce::String, const char*> labels {
            { ID::boomSub, "Sub" }, { ID::boomTune, "Tune" }, { ID::boomDecay, "Decay" }, { ID::boomNoise, "Noise" },
            { ID::boomTone, "Noise Tone" }, { ID::boomNDecay, "Noise Decay" }, { ID::boomSens, "Sensitivity" },
            { ID::shiftPitch, "Pitch" }, { ID::shiftFormant, "Formant" }, { ID::shiftFreq, "Freq Shift" },
            { ID::spaceType, "Type" }, { ID::spaceSize, "Size" }, { ID::spaceDecay, "Decay" }, { ID::spacePredelay, "Pre-Delay" },
            { ID::spaceDamp, "Damping" }, { ID::spaceWidth, "Width" },
            { ID::compMode, "Mode" }, { ID::compSpeed, "Speed" }, { ID::compMix, "Mix" },
            { ID::satMode, "Mode" }, { ID::satDrive, "Drive" }, { ID::satTone, "Tone" },
            { ID::expMode, "Mode" }, { ID::expDiv, "Grid" }, { ID::expSize, "Size" }, { ID::expChaos, "Chaos" }, { ID::expPitch, "Pitch" },
            { ID::moreChar, "Character" }, { ID::moreCeil, "Ceiling" },
        };
        auto it = labels.find (id);
        return it != labels.end() ? it->second : "";
    }

    const char* moduleDescription (int m)
    {
        switch (m)
        {
            case kBoom:         return "Adds a pitched sub thump to kicks and a filtered noise burst to snares, hats and percussion.";
            case kShift:        return "Phase-vocoder pitch and formant shifting plus frequency shifting for tonal transformations.";
            case kSpace:        return "Feedback-delay-network reverb for depth, width and room character. Gated mode for 80s snares.";
            case kCompress:     return "Six compressor characters: glue, punch, smash, pump, opto and limit.";
            case kSaturate:     return "Oversampled saturation and distortion: tape, tube, transistor, fuzz, folding and bit crushing.";
            case kExperimental: return "Tempo-synced granular cloud, stutter, reverse, tape stop, scatter and ring modulation.";
            case kMore:         return "Waveshaping and clipping for extra loudness and density. Character morphs soft to hard.";
            default:            return "";
        }
    }
}

// ============================================================================
ModulePanel::ModulePanel (PunchLabAudioProcessor& p, int m)
    : proc (p), moduleId (m),
      colour (juce::Colour (getModuleInfos()[(size_t) m].colour)),
      title (getModuleInfos()[(size_t) m].name),
      description (moduleDescription (m))
{
    auto& s = proc.apvts;
    for (auto* id : getModuleInfos()[(size_t) m].controls)
    {
        std::unique_ptr<juce::Component> c;
        if (dynamic_cast<juce::AudioParameterChoice*> (s.getParameter (id)) != nullptr)
            c = std::make_unique<ChoiceBox> (s, id, shortLabel (id));
        else
            c = std::make_unique<Knob> (s, id, shortLabel (id), colour);
        addAndMakeVisible (*c);
        controls.push_back (std::move (c));
    }

    if (auto* tp = dynamic_cast<juce::RangedAudioParameter*> (s.getParameter (moduleParam (m, "target"))))
    {
        target = std::make_unique<SegmentedChoice> (*tp, colour);
        target->setTooltip ("Process the whole signal, only the transients or only the sustain");
        addAndMakeVisible (*target);
    }
    splitLen  = std::make_unique<Knob> (s, ID::splitTime, "Length", Colours::textDim);
    splitSens = std::make_unique<Knob> (s, ID::splitSens, "Sensitivity", Colours::textDim);
    splitLen->setTooltip ("Global: how long a detected transient lasts");
    splitSens->setTooltip ("Global: how easily transients are detected");
    addAndMakeVisible (*splitLen);
    addAndMakeVisible (*splitSens);

    focusToggle = std::make_unique<ParamToggle> (s, moduleParam (m, "focus"), "Focus filter", colour);
    focusToggle->setTooltip ("Limit this module to a frequency band");
    addAndMakeVisible (*focusToggle);
    focusLo = std::make_unique<Knob> (s, moduleParam (m, "focus_lo"), "Low", colour);
    focusHi = std::make_unique<Knob> (s, moduleParam (m, "focus_hi"), "High", colour);
    addAndMakeVisible (*focusLo);
    addAndMakeVisible (*focusHi);
    focusDisplay = std::make_unique<FocusDisplay> (s.getRawParameterValue (moduleParam (m, "focus")),
                                                   s.getRawParameterValue (moduleParam (m, "focus_lo")),
                                                   s.getRawParameterValue (moduleParam (m, "focus_hi")), colour);
    addAndMakeVisible (*focusDisplay);

    if (m == kCompress)
    {
        grMeter = std::make_unique<GainReductionMeter>();
        addAndMakeVisible (*grMeter);
    }
}

void ModulePanel::refresh()
{
    focusDisplay->refresh();
    const bool f = proc.apvts.getRawParameterValue (moduleParam (moduleId, "focus"))->load() > 0.5f;
    focusLo->setEnabled (f);
    focusHi->setEnabled (f);
    if (grMeter != nullptr) grMeter->setValue (proc.getCompressorGainReduction());
}

void ModulePanel::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (Colours::panel);
    g.fillRoundedRectangle (r, 8.0f);
    g.setColour (colour.withAlpha (0.8f));
    g.fillRoundedRectangle (r.getX(), r.getY() + 14.0f, 4.0f, 34.0f, 2.0f);

    g.setColour (colour);
    g.setFont (font (21.0f, true));
    g.drawText (title, 18, 10, 400, 26, juce::Justification::centredLeft);
    g.setColour (Colours::textDim);
    g.setFont (font (13.0f));
    g.drawText (description, 18, 36, 600, 18, juce::Justification::centredLeft);

    // правая колонка
    g.setColour (Colours::line);
    g.drawVerticalLine (636, 14.0f, (float) getHeight() - 14.0f);
    g.setColour (Colours::text);
    g.setFont (font (13.0f, true));
    g.drawText ("Target", 652, 10, 200, 18, juce::Justification::centredLeft);
}

void ModulePanel::resized()
{
    int x = 14;
    for (auto& c : controls)
    {
        const bool isChoice = dynamic_cast<ChoiceBox*> (c.get()) != nullptr;
        const int w = isChoice ? 118 : 86;
        c->setBounds (x, 68, w, isChoice ? 60 : 108);
        x += w + (isChoice ? 6 : 0);
    }
    if (grMeter != nullptr) grMeter->setBounds (18, 196, 420, 26);

    target->setBounds (652, 32, 372, 28);
    splitLen->setBounds (648, 74, 82, 96);
    splitSens->setBounds (736, 74, 82, 96);
    focusToggle->setBounds (832, 76, 192, 26);
    focusDisplay->setBounds (832, 110, 192, 40);
    focusLo->setBounds (840, 156, 86, 90);
    focusHi->setBounds (934, 156, 86, 90);
}

// ============================================================================
PunchLabEditor::PunchLabEditor (PunchLabAudioProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);
    root.painter = [this] (juce::Graphics& g) { paintRoot (g); };
    addAndMakeVisible (root);
    auto& s = proc.apvts;

    // ---------------- header ----------------
    for (auto* b : { &prevButton, &nextButton, &presetButton, &saveButton, &deleteButton })
        root.addAndMakeVisible (*b);
    prevButton.onClick   = [this] { proc.getPresetManager().loadPrevious(); updatePresetLabel(); };
    nextButton.onClick   = [this] { proc.getPresetManager().loadNext(); updatePresetLabel(); };
    presetButton.onClick = [this] { showPresetMenu(); };
    saveButton.onClick   = [this] { savePresetDialog(); };
    deleteButton.onClick = [this] { deletePresetDialog(); };
    presetButton.setTooltip ("Browse presets");

    loudButton   = std::make_unique<ParamToggle> (s, ID::loudMatch, "Loudness Match", Colours::brass);
    bypassButton = std::make_unique<ParamToggle> (s, ID::bypass, "Bypass", juce::Colour (0xffe0534f));
    loudButton->setTooltip ("Automatically match the output loudness to the input for honest A/B comparison");
    root.addAndMakeVisible (*loudButton);
    root.addAndMakeVisible (*bypassButton);

    magnitude = std::make_unique<Knob> (s, ID::magnitude, "Magnitude", Colours::brass, true);
    magnitude->setTooltip ("Scales the intensity of every module and the master section");
    root.addAndMakeVisible (*magnitude);

    // ---------------- chain ----------------
    for (int m = 0; m < kNumModules; ++m)
    {
        tiles[(size_t) m] = std::make_unique<ModuleTile> (s, m);
        auto& t = *tiles[(size_t) m];
        t.onSelect = [this] (int id) { selectModule (id); };
        t.onDrop = [this] (int src, int dst)
        {
            const auto order = proc.getModuleOrder();
            const int slot = (int) (std::find (order.begin(), order.end(), dst) - order.begin());
            proc.moveModuleToSlot (src, slot);
            selectModule (src);
        };
        t.lockButton.setToggleState (proc.getPresetManager().locks[(size_t) m].load(), juce::dontSendNotification);
        t.lockButton.onClick = [this, m] { proc.getPresetManager().locks[(size_t) m].store (tiles[(size_t) m]->lockButton.getToggleState()); };
        root.addAndMakeVisible (t);

        panels[(size_t) m] = std::make_unique<ModulePanel> (proc, m);
        root.addChildComponent (*panels[(size_t) m]);
    }

    // ---------------- master ----------------
    struct M { const char* id; const char* label; juce::Colour c; };
    const juce::Colour eqc (0xff9fb3c8), dyn (0xff5cc28a), clp (0xffe0534f);
    const M defs[] = {
        { ID::mLow, "Low", eqc }, { ID::mMid, "Mid", eqc }, { ID::mMidFreq, "Mid Freq", eqc }, { ID::mHigh, "High", eqc }, { ID::mAir, "Air", eqc },
        { ID::mAttack, "Attack", dyn }, { ID::mSustain, "Sustain", dyn },
        { ID::mClip, "Clip", clp },
        { ID::mix, "Mix", Colours::brass }, { ID::output, "Output", Colours::brass } };
    for (auto& d : defs)
    {
        masterKnobs.push_back (std::make_unique<Knob> (s, d.id, d.label, d.c));
        root.addAndMakeVisible (*masterKnobs.back());
    }
    root.addAndMakeVisible (inMeter);
    root.addAndMakeVisible (outMeter);
    lmLabel.setFont (font (12.5f));
    lmLabel.setColour (juce::Label::textColourId, Colours::textDim);
    lmLabel.setJustificationType (juce::Justification::centredRight);
    root.addAndMakeVisible (lmLabel);

    selectModule (proc.getModuleOrder()[0]);
    updatePresetLabel();

    setResizable (true, true);
    setResizeLimits (designW * 6 / 10, designH * 6 / 10, designW * 16 / 10, designH * 16 / 10);
    if (auto* c = getConstrainer()) c->setFixedAspectRatio ((double) designW / (double) designH);
    setSize (designW, designH);
    startTimerHz (30);
}

PunchLabEditor::~PunchLabEditor()
{
    stopTimer();
    if (dialog != nullptr) dialog->setLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

void PunchLabEditor::paint (juce::Graphics& g)
{
    g.fillAll (Colours::bg);
}

void PunchLabEditor::resized()
{
    const float scale = (float) getWidth() / (float) designW;
    root.setBounds (0, 0, designW, designH);
    root.setTransform (juce::AffineTransform::scale (scale));

    // header
    const int px = 300;
    prevButton.setBounds (px, 30, 30, 30);
    presetButton.setBounds (px + 36, 30, 300, 30);
    nextButton.setBounds (px + 342, 30, 30, 30);
    saveButton.setBounds (px + 386, 30, 62, 30);
    deleteButton.setBounds (px + 454, 30, 66, 30);
    loudButton->setBounds (842, 16, 124, 28);
    bypassButton->setBounds (842, 50, 124, 28);
    magnitude->setBounds (978, 2, 96, 92);

    layoutChain();
    for (auto& p : panels) p->setBounds (20, 254, designW - 40, 252);

    // master
    int x = 36;
    const int y = 546;
    for (size_t i = 0; i < masterKnobs.size(); ++i)
    {
        if (i == 5 || i == 7 || i == 8) x += 26;
        masterKnobs[i]->setBounds (x, y, 84, 108);
        x += 84;
    }
    inMeter.setBounds (968, 534, 26, 160);
    outMeter.setBounds (1010, 534, 26, 160);
    lmLabel.setBounds (700, 680, 250, 20);
}

void PunchLabEditor::layoutChain()
{
    const auto order = proc.getModuleOrder();
    const int tileW = 134, gap = 16, total = tileW * kNumModules + gap * (kNumModules - 1);
    const int x0 = (designW - total) / 2;
    for (int slot = 0; slot < kNumModules; ++slot)
    {
        auto& t = *tiles[(size_t) order[(size_t) slot]];
        t.setBounds (x0 + slot * (tileW + gap), 104, tileW, 138);
        t.setSlotIndex (slot);
    }
    lastOrderVersion = proc.getOrderVersion();
    root.repaint();
}

void PunchLabEditor::paintRoot (juce::Graphics& g)
{
    g.fillAll (Colours::bg);

    // header
    g.setColour (Colours::panel.darker (0.15f));
    g.fillRect (0, 0, designW, 96);
    g.setColour (Colours::line);
    g.drawHorizontalLine (96, 0.0f, (float) designW);
    g.setColour (Colours::brass);
    g.setFont (font (30.0f, true));
    g.drawText ("PunchLab", 24, 18, 240, 34, juce::Justification::centredLeft);
    g.setColour (Colours::textDim);
    g.setFont (font (13.0f));
    g.drawText ("Drum & percussion shaper", 26, 52, 240, 18, juce::Justification::centredLeft);

    // стрелки сигнального потока между плитками
    const int tileW = 134, gap = 16, total = tileW * kNumModules + gap * (kNumModules - 1);
    const int x0 = (designW - total) / 2;
    g.setColour (Colours::textDim.withAlpha (0.7f));
    for (int s = 0; s < kNumModules - 1; ++s)
    {
        const float cx = (float) (x0 + (s + 1) * tileW + s * gap) + gap * 0.5f;
        juce::Path a;
        a.addTriangle (cx - 3.5f, 166.0f, cx - 3.5f, 180.0f, cx + 4.5f, 173.0f);
        g.fillPath (a);
    }
    g.setFont (font (11.5f));
    g.drawText ("Drag modules in the chain to change the processing order", 300, 66, 520, 16, juce::Justification::centred);

    // master
    auto mr = juce::Rectangle<float> (20.0f, 518.0f, (float) designW - 40.0f, 190.0f);
    g.setColour (Colours::panel);
    g.fillRoundedRectangle (mr, 8.0f);
    g.setColour (Colours::text);
    g.setFont (font (15.0f, true));
    g.drawText ("Master", 36, 524, 200, 20, juce::Justification::centredLeft);
    g.setColour (Colours::textDim);
    g.setFont (font (11.5f));
    g.drawText ("Equaliser", 36, 656, 420, 16, juce::Justification::centred);
    g.drawText ("Transients", 36 + 420 + 26, 656, 168, 16, juce::Justification::centred);
    g.drawText ("Clipper", 36 + 588 + 52, 656, 84, 16, juce::Justification::centred);
    g.drawText ("Output", 36 + 672 + 78, 656, 168, 16, juce::Justification::centred);
}

void PunchLabEditor::selectModule (int id)
{
    selected = id;
    for (int m = 0; m < kNumModules; ++m)
    {
        tiles[(size_t) m]->setSelected (m == id);
        panels[(size_t) m]->setVisible (m == id);
    }
}

void PunchLabEditor::timerCallback()
{
    if (proc.getOrderVersion() != lastOrderVersion) layoutChain();
    auto& pm = proc.getPresetManager();
    for (int m = 0; m < kNumModules; ++m)
    {
        tiles[(size_t) m]->refresh();
        const bool locked = pm.locks[(size_t) m].load();
        if (tiles[(size_t) m]->lockButton.getToggleState() != locked)
            tiles[(size_t) m]->lockButton.setToggleState (locked, juce::dontSendNotification);
    }
    panels[(size_t) selected]->refresh();
    inMeter.setLevel (proc.getInputLevel());
    outMeter.setLevel (proc.getOutputLevel());

    const bool lm = proc.apvts.getRawParameterValue (ID::loudMatch)->load() > 0.5f;
    const float lmDb = proc.getLoudnessMatchGain();
    lmLabel.setText (lm ? "Loudness match " + juce::String (lmDb >= 0.0f ? "+" : "") + juce::String (lmDb, 1) + " dB" : juce::String(),
                     juce::dontSendNotification);

    if (pm.getCurrentName() != lastPresetName) updatePresetLabel();
}

void PunchLabEditor::updatePresetLabel()
{
    auto& pm = proc.getPresetManager();
    lastPresetName = pm.getCurrentName();
    presetButton.setButtonText (lastPresetName);
    const auto& e = pm.getEntries();
    const int idx = pm.getCurrentIndex();
    deleteButton.setEnabled (idx >= 0 && idx < (int) e.size() && ! e[(size_t) idx].isFactory);
}

void PunchLabEditor::showPresetMenu()
{
    auto& pm = proc.getPresetManager();
    pm.refresh();
    const auto& entries = pm.getEntries();

    juce::PopupMenu menu;
    std::map<juce::String, juce::PopupMenu> cats;
    juce::StringArray catOrder;
    juce::PopupMenu user;
    for (int i = 0; i < (int) entries.size(); ++i)
    {
        const auto& e = entries[(size_t) i];
        const bool ticked = i == pm.getCurrentIndex();
        if (e.isFactory)
        {
            if (! catOrder.contains (e.category)) catOrder.add (e.category);
            cats[e.category].addItem (i + 1, e.name, true, ticked);
        }
        else user.addItem (i + 1, e.name, true, ticked);
    }
    for (auto& c : catOrder) menu.addSubMenu (c, cats[c]);
    menu.addSeparator();
    if (user.getNumItems() > 0) menu.addSubMenu ("User", user);
    else menu.addItem (-1, "No user presets yet", false);
    menu.addSeparator();
    menu.addItem (10000, "Open preset folder");

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&presetButton),
        [this] (int result)
        {
            if (result == 10000) { PresetManager::getUserPresetDirectory().startAsProcess(); return; }
            if (result > 0) { proc.getPresetManager().load (result - 1); updatePresetLabel(); }
        });
}

void PunchLabEditor::savePresetDialog()
{
    dialog = std::make_unique<juce::AlertWindow> ("Save preset", "Preset name:", juce::MessageBoxIconType::NoIcon, this);
    dialog->setLookAndFeel (&lnf);
    dialog->addTextEditor ("name", proc.getPresetManager().getCurrentName());
    dialog->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    dialog->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    // SafePointer создаётся здесь, вне лямбды: MSVC иначе подставляет вместо
    // PunchLabEditor* указатель на саму лямбду.
    juce::Component::SafePointer<PunchLabEditor> safe (this);
    dialog->enterModalState (true, juce::ModalCallbackFunction::create ([safe] (int r)
    {
        if (safe == nullptr) return;
        if (r == 1 && safe->dialog != nullptr)
        {
            const auto name = safe->dialog->getTextEditorContents ("name");
            safe->proc.getPresetManager().saveUserPreset (name);
            safe->updatePresetLabel();
        }
        juce::MessageManager::callAsync ([safe]
        {
            if (safe != nullptr) safe->dialog.reset();
        });
    }), false);
}

void PunchLabEditor::deletePresetDialog()
{
    auto& pm = proc.getPresetManager();
    const int idx = pm.getCurrentIndex();
    const auto& e = pm.getEntries();
    if (idx < 0 || idx >= (int) e.size() || e[(size_t) idx].isFactory) return;

    dialog = std::make_unique<juce::AlertWindow> ("Delete preset", "Delete \"" + e[(size_t) idx].name + "\"?",
                                                  juce::MessageBoxIconType::WarningIcon, this);
    dialog->setLookAndFeel (&lnf);
    dialog->addButton ("Delete", 1, juce::KeyPress (juce::KeyPress::returnKey));
    dialog->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    juce::Component::SafePointer<PunchLabEditor> safe (this);
    dialog->enterModalState (true, juce::ModalCallbackFunction::create ([safe, idx] (int r)
    {
        if (safe == nullptr) return;
        if (r == 1)
        {
            auto& manager = safe->proc.getPresetManager();
            manager.deleteUserPreset (idx);
            manager.load (manager.getCurrentIndex());
            safe->updatePresetLabel();
        }
        juce::MessageManager::callAsync ([safe]
        {
            if (safe != nullptr) safe->dialog.reset();
        });
    }), false);
}
