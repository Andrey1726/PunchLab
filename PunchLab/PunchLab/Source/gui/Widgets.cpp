#include "Widgets.h"
#include "../Params.h"

namespace pl::ui
{
juce::Font font (float size, bool bold)
{
    return juce::Font (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain));
}

// ============================================================================
PunchLookAndFeel::PunchLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, Colours::bg);
    setColour (juce::Label::textColourId, Colours::text);
    setColour (juce::Slider::textBoxTextColourId, Colours::text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxHighlightColourId, Colours::brass.withAlpha (0.4f));
    setColour (juce::ComboBox::backgroundColourId, Colours::raised);
    setColour (juce::ComboBox::textColourId, Colours::text);
    setColour (juce::ComboBox::outlineColourId, Colours::line);
    setColour (juce::ComboBox::arrowColourId, Colours::textDim);
    setColour (juce::PopupMenu::backgroundColourId, Colours::panel);
    setColour (juce::PopupMenu::textColourId, Colours::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, Colours::raised);
    setColour (juce::PopupMenu::highlightedTextColourId, Colours::brass);
    setColour (juce::TextButton::buttonColourId, Colours::raised);
    setColour (juce::TextButton::buttonOnColourId, Colours::brass);
    setColour (juce::TextButton::textColourOffId, Colours::text);
    setColour (juce::TextButton::textColourOnId, Colours::bg);
    setColour (juce::AlertWindow::backgroundColourId, Colours::panel);
    setColour (juce::AlertWindow::textColourId, Colours::text);
    setColour (juce::AlertWindow::outlineColourId, Colours::line);
    setColour (juce::TextEditor::backgroundColourId, Colours::bg);
    setColour (juce::TextEditor::textColourId, Colours::text);
    setColour (juce::TextEditor::outlineColourId, Colours::line);
    setColour (juce::TextEditor::focusedOutlineColourId, Colours::brass);
}

void PunchLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                         float a0, float a1, juce::Slider& s)
{
    const auto bounds = juce::Rectangle<int> (x, y, w, h).toFloat().reduced (3.0f);
    const float r = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto c = bounds.getCentre();
    const float track = juce::jmax (2.5f, r * 0.13f);
    const auto fill = s.findColour (juce::Slider::rotarySliderFillColourId);
    const bool bipolar = (bool) s.getProperties().getWithDefault ("bipolar", false);
    const float angle = a0 + pos * (a1 - a0);

    juce::Path bgArc;
    bgArc.addCentredArc (c.x, c.y, r - track * 0.5f, r - track * 0.5f, 0.0f, a0, a1, true);
    g.setColour (Colours::bg.darker (0.3f));
    g.strokePath (bgArc, juce::PathStrokeType (track, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const float from = bipolar ? (a0 + a1) * 0.5f : a0;
    if (std::abs (angle - from) > 0.001f)
    {
        juce::Path arc;
        arc.addCentredArc (c.x, c.y, r - track * 0.5f, r - track * 0.5f, 0.0f, juce::jmin (from, angle), juce::jmax (from, angle), true);
        g.setColour (s.isEnabled() ? fill : Colours::textDim.withAlpha (0.35f));
        g.strokePath (arc, juce::PathStrokeType (track, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    const float kr = r - track - 3.0f;
    g.setGradientFill (juce::ColourGradient (Colours::raised.brighter (0.15f), c.x, c.y - kr,
                                             Colours::raised.darker (0.35f), c.x, c.y + kr, false));
    g.fillEllipse (c.x - kr, c.y - kr, kr * 2.0f, kr * 2.0f);
    g.setColour (Colours::line);
    g.drawEllipse (c.x - kr, c.y - kr, kr * 2.0f, kr * 2.0f, 1.0f);

    juce::Path p;
    p.addRoundedRectangle (-1.5f, -kr + 3.0f, 3.0f, kr * 0.5f, 1.5f);
    p.applyTransform (juce::AffineTransform::rotation (angle).translated (c.x, c.y));
    g.setColour (Colours::text);
    g.fillPath (p);
}

void PunchLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    const bool on = b.getToggleState();
    auto base = on ? b.findColour (juce::TextButton::buttonOnColourId) : b.findColour (juce::TextButton::buttonColourId);
    if (over) base = base.brighter (0.08f);
    if (down) base = base.darker (0.1f);
    g.setColour (base);
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (on ? base.brighter (0.2f) : Colours::line);
    g.drawRoundedRectangle (r, 4.0f, 1.0f);
}

void PunchLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    g.setFont (font (13.0f, true));
    g.setColour (b.getToggleState() ? b.findColour (juce::TextButton::textColourOnId) : b.findColour (juce::TextButton::textColourOffId));
    g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (4, 0), juce::Justification::centred, 1);
}

void PunchLookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    auto r = juce::Rectangle<int> (0, 0, w, h).toFloat().reduced (0.5f);
    g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (box.findColour (juce::ComboBox::outlineColourId));
    g.drawRoundedRectangle (r, 4.0f, 1.0f);
    juce::Path arrow;
    const float ax = (float) w - 14.0f, ay = (float) h * 0.5f;
    arrow.addTriangle (ax - 4.0f, ay - 2.0f, ax + 4.0f, ay - 2.0f, ax, ay + 3.0f);
    g.setColour (box.findColour (juce::ComboBox::arrowColourId));
    g.fillPath (arrow);
}

juce::Label* PunchLookAndFeel::createSliderTextBox (juce::Slider& s)
{
    auto* l = LookAndFeel_V4::createSliderTextBox (s);
    l->setFont (font (11.5f));
    l->setColour (juce::Label::textColourId, Colours::textDim);
    l->setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    l->setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
    return l;
}

void PunchLookAndFeel::drawLabel (juce::Graphics& g, juce::Label& l)
{
    // Поле значения у ручек: только текст, без рамки и фона
    if (dynamic_cast<juce::Slider*> (l.getParentComponent()) != nullptr)
    {
        if (! l.isBeingEdited())
        {
            g.setColour (l.getParentComponent()->isEnabled() ? Colours::textDim : Colours::textDim.withAlpha (0.5f));
            g.setFont (font (11.5f));
            g.drawFittedText (l.getText(), l.getLocalBounds(), juce::Justification::centred, 1);
        }
        return;
    }
    LookAndFeel_V4::drawLabel (g, l);
}

// ============================================================================
Knob::Knob (juce::AudioProcessorValueTreeState& s, const juce::String& id, const juce::String& label, juce::Colour colour, bool large)
    : isLarge (large)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 76, 16);
    slider.setColour (juce::Slider::rotarySliderFillColourId, colour);
    slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    if (auto* p = dynamic_cast<juce::RangedAudioParameter*> (s.getParameter (id)))
    {
        const auto& range = p->getNormalisableRange();
        if (std::abs (range.start + range.end) < 1.0e-4f && range.start < 0.0f)
            slider.getProperties().set ("bipolar", true);
        slider.setDoubleClickReturnValue (true, range.convertFrom0to1 (p->getDefaultValue()));
    }
    addAndMakeVisible (slider);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (s, id, slider);

    name.setText (label, juce::dontSendNotification);
    name.setFont (font (large ? 14.0f : 12.5f, true));
    name.setJustificationType (juce::Justification::centred);
    name.setColour (juce::Label::textColourId, Colours::text);
    name.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (name);
}

void Knob::resized()
{
    auto r = getLocalBounds();
    name.setBounds (r.removeFromTop (isLarge ? 18 : 16));
    slider.setBounds (r);
}

// ============================================================================
ChoiceBox::ChoiceBox (juce::AudioProcessorValueTreeState& s, const juce::String& id, const juce::String& label)
{
    if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (s.getParameter (id)))
        box.addItemList (p->choices, 1);
    addAndMakeVisible (box);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (s, id, box);
    name.setText (label, juce::dontSendNotification);
    name.setFont (font (12.5f, true));
    name.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (name);
}

void ChoiceBox::resized()
{
    auto r = getLocalBounds();
    name.setBounds (r.removeFromTop (16));
    r.removeFromTop (8);
    box.setBounds (r.removeFromTop (26));
}

// ============================================================================
SegmentedChoice::SegmentedChoice (juce::RangedAudioParameter& p, juce::Colour c)
    : colour (c),
      attachment (p, [this] (float v) { current = juce::roundToInt (v); repaint(); }, nullptr)
{
    if (auto* ch = dynamic_cast<juce::AudioParameterChoice*> (&p)) items = ch->choices;
    attachment.sendInitialUpdate();
}

void SegmentedChoice::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (Colours::bg);
    g.fillRoundedRectangle (r, 5.0f);
    const float w = r.getWidth() / (float) juce::jmax (1, items.size());
    for (int i = 0; i < items.size(); ++i)
    {
        auto seg = juce::Rectangle<float> (r.getX() + w * (float) i, r.getY(), w, r.getHeight()).reduced (2.0f);
        if (i == current)
        {
            g.setColour (colour);
            g.fillRoundedRectangle (seg, 4.0f);
        }
        g.setColour (i == current ? Colours::bg : Colours::textDim);
        g.setFont (font (12.5f, true));
        g.drawText (items[i], seg, juce::Justification::centred);
    }
    g.setColour (Colours::line);
    g.drawRoundedRectangle (r, 5.0f, 1.0f);
}

void SegmentedChoice::mouseDown (const juce::MouseEvent& e)
{
    if (items.isEmpty()) return;
    const int idx = juce::jlimit (0, items.size() - 1, (int) ((float) e.x / (float) getWidth() * (float) items.size()));
    attachment.setValueAsCompleteGesture ((float) idx);
}

// ============================================================================
void IconToggle::paintButton (juce::Graphics& g, bool over, bool)
{
    auto r = getLocalBounds().toFloat().reduced (2.0f);
    const bool on = getToggleState();
    const auto col = on ? colour : Colours::textDim.withAlpha (over ? 0.9f : 0.6f);
    g.setColour (col);
    if (icon == Power)
    {
        const auto c = r.getCentre();
        const float rad = juce::jmin (r.getWidth(), r.getHeight()) * 0.38f;
        juce::Path p;
        p.addCentredArc (c.x, c.y, rad, rad, 0.0f, 0.65f, juce::MathConstants<float>::twoPi - 0.65f, true);
        g.strokePath (p, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.drawLine (c.x, c.y - rad - 1.5f, c.x, c.y - rad * 0.15f, 1.8f);
    }
    else
    {
        const float s = juce::jmin (r.getWidth(), r.getHeight());
        auto body = juce::Rectangle<float> (s * 0.62f, s * 0.46f).withCentre ({ r.getCentreX(), r.getCentreY() + s * 0.14f });
        if (on) g.fillRoundedRectangle (body, 1.5f); else g.drawRoundedRectangle (body, 1.5f, 1.4f);
        juce::Path shackle;
        const float sw = body.getWidth() * 0.62f, top = body.getY() - s * 0.26f;
        const float left = body.getCentreX() - sw * 0.5f;
        shackle.startNewSubPath (left, body.getY());
        shackle.lineTo (left, top + sw * 0.5f);
        shackle.addArc (left, top, sw, sw, -juce::MathConstants<float>::halfPi, juce::MathConstants<float>::halfPi);
        if (on) shackle.lineTo (left + sw, body.getY());
        else    shackle.lineTo (left + sw, top + sw * 0.5f + 1.0f);
        g.strokePath (shackle, juce::PathStrokeType (1.4f));
    }
}

ParamToggle::ParamToggle (juce::AudioProcessorValueTreeState& s, const juce::String& id, const juce::String& text, juce::Colour onColour)
    : juce::TextButton (text)
{
    setClickingTogglesState (true);
    setColour (juce::TextButton::buttonOnColourId, onColour);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (s, id, *this);
}

// ============================================================================
void LevelMeter::setLevel (float linear)
{
    const float db = juce::jlimit (-60.0f, 6.0f, juce::Decibels::gainToDecibels (linear, -100.0f));
    levelDb = db > levelDb ? db : levelDb - 1.2f;
    if (db >= peakDb) { peakDb = db; peakHold = 45; }
    else if (--peakHold <= 0) peakDb -= 0.8f;
    repaint();
}

void LevelMeter::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    auto labelArea = r.removeFromBottom (16.0f);
    g.setColour (Colours::bg);
    g.fillRoundedRectangle (r, 3.0f);
    auto toY = [&r] (float db) { return juce::jmap (juce::jlimit (-60.0f, 6.0f, db), -60.0f, 6.0f, r.getBottom(), r.getY()); };
    const float y = toY (levelDb);
    auto bar = juce::Rectangle<float> (r.getX() + 2.0f, y, r.getWidth() - 4.0f, r.getBottom() - y);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff5cc28a), 0.0f, r.getBottom(),
                                             juce::Colour (0xffe0534f), 0.0f, r.getY(), false));
    g.fillRect (bar);
    g.setColour (Colours::text);
    g.fillRect (r.getX() + 2.0f, toY (peakDb), r.getWidth() - 4.0f, 1.5f);
    g.setColour (Colours::line);
    g.drawHorizontalLine ((int) toY (0.0f), r.getX(), r.getRight());
    g.setColour (Colours::textDim);
    g.setFont (font (11.0f));
    g.drawText (name, labelArea, juce::Justification::centred);
}

// ============================================================================
void FocusDisplay::refresh()
{
    const bool on = pOn->load() > 0.5f;
    const float lo = pLo->load(), hi = pHi->load();
    if (on != lastOn || lo != lastLo || hi != lastHi) { lastOn = on; lastLo = lo; lastHi = hi; repaint(); }
}

void FocusDisplay::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (Colours::bg);
    g.fillRoundedRectangle (r, 4.0f);
    auto toX = [&r] (float f) { return r.getX() + r.getWidth() * (std::log10 (juce::jlimit (20.0f, 20000.0f, f) / 20.0f) / 3.0f); };

    g.setColour (Colours::line);
    for (float f : { 100.0f, 1000.0f, 10000.0f })
        g.drawVerticalLine ((int) toX (f), r.getY() + 2.0f, r.getBottom() - 2.0f);

    const float lo = pLo->load(), hi = juce::jmax (pHi->load(), lo);
    const bool on = pOn->load() > 0.5f;
    auto band = juce::Rectangle<float> (toX (on ? lo : 20.0f), r.getY() + 3.0f, 0.0f, r.getHeight() - 6.0f)
                    .withRight (toX (on ? hi : 20000.0f));
    g.setColour (colour.withAlpha (on ? 0.55f : 0.15f));
    g.fillRoundedRectangle (band, 3.0f);
    g.setColour (Colours::textDim);
    g.setFont (font (10.5f));
    g.drawText ("100", juce::Rectangle<float> (toX (100.0f) + 2.0f, r.getY(), 30.0f, 12.0f), juce::Justification::left);
    g.drawText ("1k",  juce::Rectangle<float> (toX (1000.0f) + 2.0f, r.getY(), 30.0f, 12.0f), juce::Justification::left);
    g.drawText ("10k", juce::Rectangle<float> (toX (10000.0f) + 2.0f, r.getY(), 30.0f, 12.0f), juce::Justification::left);
}

void GainReductionMeter::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    auto textArea = r.removeFromLeft (92.0f);
    g.setColour (Colours::textDim);
    g.setFont (font (12.0f));
    g.drawText ("Gain reduction", textArea, juce::Justification::centredLeft);
    auto valArea = r.removeFromRight (64.0f);
    g.drawText (juce::String (value, 1) + " dB", valArea, juce::Justification::centredRight);
    r = r.reduced (4.0f, 4.0f);
    g.setColour (Colours::bg);
    g.fillRoundedRectangle (r, 3.0f);
    const float w = r.getWidth() * juce::jlimit (0.0f, 1.0f, -value / 24.0f);
    g.setColour (juce::Colour (0xff5cc28a));
    g.fillRoundedRectangle (r.withWidth (w), 3.0f);
}

// ============================================================================
ModuleTile::ModuleTile (juce::AudioProcessorValueTreeState& s, int id)
    : lockButton (IconToggle::Lock, Colours::brass),
      moduleId (id),
      colour (juce::Colour (getModuleInfos()[(size_t) id].colour)),
      title (getModuleInfos()[(size_t) id].name),
      powerButton (IconToggle::Power, juce::Colour (getModuleInfos()[(size_t) id].colour))
{
    addAndMakeVisible (powerButton);
    powerAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (s, moduleParam (id, "on"), powerButton);
    powerButton.setTooltip ("Enable / disable module");

    lockButton.setTooltip ("Lock: keep this module when loading presets");
    addAndMakeVisible (lockButton);

    amount.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    amount.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 60, 14);
    amount.setColour (juce::Slider::rotarySliderFillColourId, colour);
    amount.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    amount.setDoubleClickReturnValue (true, 0.5);
    addAndMakeVisible (amount);
    amountAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (s, moduleParam (id, "amount"), amount);

    pOn = s.getRawParameterValue (moduleParam (id, "on"));
    pTarget = s.getRawParameterValue (moduleParam (id, "target"));
    pFocus = s.getRawParameterValue (moduleParam (id, "focus"));
}

void ModuleTile::refresh()
{
    const bool on = pOn->load() > 0.5f;
    const int t = (int) pTarget->load();
    const bool f = pFocus->load() > 0.5f;
    if (on != lastOn || t != lastTarget || f != lastFocus)
    {
        lastOn = on; lastTarget = t; lastFocus = f;
        amount.setColour (juce::Slider::rotarySliderFillColourId, on ? colour : colour.withSaturation (0.15f).withAlpha (0.6f));
        repaint();
    }
}

void ModuleTile::resized()
{
    auto r = getLocalBounds().reduced (6);
    auto top = r.removeFromTop (24);
    powerButton.setBounds (top.removeFromLeft (24));
    lockButton.setBounds (top.removeFromRight (22));
    r.removeFromTop (4);
    r.removeFromBottom (14);
    amount.setBounds (r.withSizeKeepingCentre (juce::jmin (r.getWidth(), 86), r.getHeight()));
}

void ModuleTile::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    const bool on = lastOn;
    g.setColour (Colours::panel);
    g.fillRoundedRectangle (r, 7.0f);

    // цветная полоса сверху
    auto band = r.withHeight (34.0f);
    g.setColour (on ? colour.withAlpha (0.22f) : Colours::raised.withAlpha (0.5f));
    g.fillRoundedRectangle (band, 7.0f);
    g.setColour (on ? colour : Colours::textDim);
    g.setFont (font (12.5f, true));
    g.drawFittedText (title, band.reduced (27.0f, 0.0f).toNearestInt(), juce::Justification::centred, 1, 0.75f);

    // нижняя строка: номер в цепочке и флажки Target / Focus
    auto bottom = r.removeFromBottom (18.0f).reduced (8.0f, 0.0f);
    g.setFont (font (11.5f, true));
    g.setColour (Colours::textDim);
    g.drawText (juce::String (slot + 1), bottom, juce::Justification::centredLeft);
    juce::String flags;
    if (lastTarget == 1) flags << "TRN";
    if (lastTarget == 2) flags << "SUS";
    if (lastFocus) flags << (flags.isEmpty() ? "" : " ") << "FOC";
    g.setColour (on ? colour : Colours::textDim);
    g.drawText (flags, bottom, juce::Justification::centredRight);
}

void ModuleTile::paintOverChildren (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    if (selected || dragHover)
    {
        g.setColour (dragHover ? Colours::brass : colour);
        g.drawRoundedRectangle (r, 7.0f, dragHover ? 2.5f : 2.0f);
    }
    else
    {
        g.setColour (Colours::line);
        g.drawRoundedRectangle (r, 7.0f, 1.0f);
    }
}

void ModuleTile::mouseDown (const juce::MouseEvent&) { dragging = false; }

void ModuleTile::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging && e.getDistanceFromDragStart() > 6)
    {
        if (auto* c = juce::DragAndDropContainer::findParentDragContainerFor (this))
        {
            dragging = true;
            c->startDragging (juce::var (moduleId), this);
        }
    }
}

void ModuleTile::mouseUp (const juce::MouseEvent&)
{
    if (! dragging && onSelect) onSelect (moduleId);
    dragging = false;
}

void ModuleTile::itemDropped (const SourceDetails& d)
{
    dragHover = false;
    repaint();
    const int src = (int) d.description;
    if (src != moduleId && onDrop) onDrop (src, moduleId);
}
} // namespace pl::ui
