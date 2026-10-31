#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>

namespace ui
{
using APVTS = juce::AudioProcessorValueTreeState;

namespace col
{
    const juce::Colour bg       { 0xff101215 };
    const juce::Colour panel    { 0xff181c21 };
    const juce::Colour panelHi  { 0xff1f242a };
    const juce::Colour border   { 0xff2a3038 };
    const juce::Colour text     { 0xffdfe4ea };
    const juce::Colour dim      { 0xff7d8792 };
    const juce::Colour knob     { 0xff262b32 };
}

inline juce::Font font (float h, bool bold = false)
{
    return juce::Font (juce::FontOptions (h, bold ? juce::Font::bold : juce::Font::plain));
}

//==============================================================================
class Look : public juce::LookAndFeel_V4
{
public:
    Look()
    {
        setColour (juce::Slider::textBoxTextColourId, col::text);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour (juce::Label::textColourId, col::dim);
        setColour (juce::ComboBox::backgroundColourId, col::knob);
        setColour (juce::ComboBox::outlineColourId, col::border);
        setColour (juce::ComboBox::textColourId, col::text);
        setColour (juce::ComboBox::arrowColourId, col::dim);
        setColour (juce::PopupMenu::backgroundColourId, col::panel);
        setColour (juce::PopupMenu::textColourId, col::text);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, col::border);
        setColour (juce::TextButton::buttonColourId, col::knob);
        setColour (juce::TextButton::textColourOffId, col::dim);
        setColour (juce::TextButton::textColourOnId, juce::Colours::black);
        setColour (juce::TooltipWindow::backgroundColourId, col::panelHi);
        setColour (juce::TooltipWindow::textColourId, col::text);
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider& s) override
    {
        const auto accent = s.findColour (juce::Slider::rotarySliderFillColourId);
        const float size = (float) juce::jmin (w, h) - 6.0f;
        const auto c = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).getCentre();
        const float r = size * 0.5f;
        const float ang = a0 + pos * (a1 - a0);

        juce::Path track;
        track.addCentredArc (c.x, c.y, r, r, 0, a0, a1, true);
        g.setColour (col::border);
        g.strokePath (track, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        const bool bipolar = s.getMinimum() < 0 && s.getMaximum() > 0;
        float from = a0;
        if (bipolar) from = a0 + (float) s.valueToProportionOfLength (0.0) * (a1 - a0);
        juce::Path val;
        val.addCentredArc (c.x, c.y, r, r, 0, juce::jmin (from, ang), juce::jmax (from, ang), true);
        g.setColour (s.isEnabled() ? accent : accent.withAlpha (0.3f));
        g.strokePath (val, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        const float kr = r - 5.0f;
        g.setGradientFill (juce::ColourGradient (col::panelHi.brighter (0.15f), c.x, c.y - kr, col::knob.darker (0.3f), c.x, c.y + kr, false));
        g.fillEllipse (c.x - kr, c.y - kr, 2 * kr, 2 * kr);
        g.setColour (col::border.brighter (0.1f));
        g.drawEllipse (c.x - kr, c.y - kr, 2 * kr, 2 * kr, 1.0f);

        juce::Path p;
        p.addRoundedRectangle (-1.25f, -kr + 2.0f, 2.5f, kr * 0.45f, 1.2f);
        g.setColour (col::text);
        g.fillPath (p, juce::AffineTransform::rotation (ang).translated (c.x, c.y));
    }

    juce::Label* createSliderTextBox (juce::Slider& s) override
    {
        auto* l = LookAndFeel_V4::createSliderTextBox (s);
        l->setFont (font (11.0f));
        l->setJustificationType (juce::Justification::centred);
        return l;
    }

    juce::Font getComboBoxFont (juce::ComboBox&) override { return font (12.0f); }
    juce::Font getPopupMenuFont() override { return font (13.0f); }
    juce::Font getTextButtonFont (juce::TextButton&, int) override { return font (11.0f, true); }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down) override
    {
        auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        const auto accent = b.findColour (juce::TextButton::buttonOnColourId);
        g.setColour (b.getToggleState() ? accent : (over ? col::panelHi.brighter (0.1f) : col::knob));
        if (down) g.setColour (accent.withAlpha (0.7f));
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (b.getToggleState() ? accent.brighter (0.2f) : col::border);
        g.drawRoundedRectangle (r, 4.0f, 1.0f);
    }
};

//==============================================================================
class Knob : public juce::Component
{
public:
    Knob (const juce::String& name, juce::Colour accent)
    {
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 14);
        slider.setColour (juce::Slider::rotarySliderFillColourId, accent);
        slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
        slider.setVelocityBasedMode (false);
        slider.setMouseDragSensitivity (220);
        addAndMakeVisible (slider);
        label.setText (name.toUpperCase(), juce::dontSendNotification);
        label.setFont (font (10.0f, true));
        label.setJustificationType (juce::Justification::centred);
        label.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (label);
    }

    void attach (APVTS& s, const juce::String& id)
    {
        att.reset();
        att = std::make_unique<APVTS::SliderAttachment> (s, id, slider);
        if (auto* p = s.getParameter (id))
        {
            slider.setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));
            slider.setTooltip (p->getName (64) + "  (double-click to reset)");
        }
    }
    void setAccent (juce::Colour c) { slider.setColour (juce::Slider::rotarySliderFillColourId, c); repaint(); }
    void setName (const juce::String& n) { label.setText (n.toUpperCase(), juce::dontSendNotification); }

    void resized() override
    {
        auto r = getLocalBounds();
        label.setBounds (r.removeFromTop (13));
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, r.getWidth(), 14);
        slider.setBounds (r);
    }

    juce::Slider slider;
    juce::Label label;

private:
    std::unique_ptr<APVTS::SliderAttachment> att;
};

//==============================================================================
class PowerButton : public juce::ToggleButton
{
public:
    explicit PowerButton (juce::Colour c) : accent (c) { setTooltip ("Module on/off"); }
    void paintButton (juce::Graphics& g, bool over, bool) override
    {
        auto r = getLocalBounds().toFloat().reduced (2.0f);
        const float d = juce::jmin (r.getWidth(), r.getHeight());
        auto cr = r.withSizeKeepingCentre (d, d);
        if (getToggleState())
        {
            g.setColour (accent.withAlpha (0.25f));
            g.fillEllipse (cr.expanded (2.0f));
            g.setColour (accent);
        }
        else g.setColour (over ? col::dim : col::border.brighter (0.2f));
        g.fillEllipse (cr.reduced (2.0f));
    }
    juce::Colour accent;
};

//==============================================================================
// A titled module panel with a power LED, an activity / gain-reduction meter and a
// row of knobs plus optional combo boxes and toggle buttons.
class ModulePanel : public juce::Component, public juce::SettableTooltipClient
{
public:
    ModulePanel (APVTS& s, const juce::String& t, juce::Colour a, const juce::String& powerId = {})
        : state (s), title (t), accent (a), power (a)
    {
        if (powerId.isNotEmpty())
        {
            addAndMakeVisible (power);
            powerAtt = std::make_unique<APVTS::ButtonAttachment> (s, powerId, power);
            power.onStateChange = [this] { updateEnabled(); };
            hasPower = true;
        }
    }

    Knob& addKnob (const juce::String& id, const juce::String& name)
    {
        auto* k = knobs.add (new Knob (name, accent));
        k->attach (state, id);
        addAndMakeVisible (k);
        return *k;
    }
    juce::ComboBox& addCombo (const juce::String& id)
    {
        auto* cb = combos.add (new juce::ComboBox());
        if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (state.getParameter (id)))
            cb->addItemList (p->choices, 1);
        comboAtts.add (new APVTS::ComboBoxAttachment (state, id, *cb));
        addAndMakeVisible (cb);
        return *cb;
    }
    juce::TextButton& addToggle (const juce::String& id, const juce::String& text, const juce::String& tip = {})
    {
        auto* b = toggles.add (new juce::TextButton (text));
        b->setClickingTogglesState (true);
        b->setColour (juce::TextButton::buttonOnColourId, accent);
        b->setTooltip (tip);
        toggleAtts.add (new APVTS::ButtonAttachment (state, id, *b));
        addAndMakeVisible (b);
        return *b;
    }

    std::function<void()> onTitleClick;           // click the header to open this module in the monitor
    bool monitored = false;
    void mouseDown (const juce::MouseEvent& e) override { if (e.y < 26 && onTitleClick) onTitleClick(); }
    std::function<float()> meter;                 // dB of reduction (>= 0)
    std::function<juce::String()> statusText;     // optional extra readout in header
    float meterRangeDb = 24.0f;
    int reservedRight = 0;                        // px kept free for custom painting
    std::function<void (juce::Graphics&, juce::Rectangle<int>)> paintExtra;

    void updateEnabled()
    {
        const bool on = ! hasPower || power.getToggleState();
        for (auto* k : knobs) k->setAlpha (on ? 1.0f : 0.45f);
        for (auto* c : combos) c->setAlpha (on ? 1.0f : 0.45f);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (col::panel);
        g.fillRoundedRectangle (r, 7.0f);
        g.setColour (monitored ? accent : col::border);
        g.drawRoundedRectangle (r.reduced (0.5f), 7.0f, monitored ? 1.6f : 1.0f);
        const bool on = ! hasPower || power.getToggleState();

        auto head = getLocalBounds().removeFromTop (26).reduced (10, 0);
        if (hasPower) head.removeFromLeft (20);
        g.setColour (on ? accent : col::dim);
        g.setFont (font (12.0f, true));
        g.drawText (title, head, juce::Justification::centredLeft);

        if (meter)
        {
            auto mr = head.removeFromRight (juce::jmin (90, head.getWidth() / 3)).withSizeKeepingCentre (juce::jmin (90, head.getWidth() / 3), 6).toFloat();
            g.setColour (col::bg);
            g.fillRoundedRectangle (mr, 3.0f);
            const float v = on ? juce::jlimit (0.0f, 1.0f, meter() / meterRangeDb) : 0.0f;
            g.setColour (accent);
            g.fillRoundedRectangle (mr.withWidth (mr.getWidth() * v), 3.0f);
            if (statusText)
            {
                g.setColour (col::dim);
                g.setFont (font (10.0f));
                g.drawText (statusText(), head.removeFromRight (90).withTrimmedRight (6), juce::Justification::centredRight);
            }
        }
        if (paintExtra) paintExtra (g, extraArea);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (8, 4);
        auto head = r.removeFromTop (22);
        if (hasPower) power.setBounds (head.removeFromLeft (20).reduced (1));
        if (reservedRight > 0) extraArea = r.removeFromRight (reservedRight);
        if (combos.size() + toggles.size() > 0)
        {
            auto row = r.removeFromBottom (22);
            r.removeFromBottom (4);
            const int count = combos.size() + toggles.size();
            const int w = juce::jmin (110, (row.getWidth() - (count - 1) * 6) / count);
            auto items = row.withSizeKeepingCentre (count * w + (count - 1) * 6, 22);
            for (auto* c : combos)  { c->setBounds (items.removeFromLeft (w)); items.removeFromLeft (6); }
            for (auto* b : toggles) { b->setBounds (items.removeFromLeft (w)); items.removeFromLeft (6); }
        }
        if (knobs.isEmpty()) return;
        const int n = knobs.size();
        const int rows = (r.getWidth() / n < 58) ? 2 : 1;
        const int perRow = (n + rows - 1) / rows;
        const int rowH = r.getHeight() / rows;
        for (int row = 0; row < rows; ++row)
        {
            auto rr = r.removeFromTop (rowH);
            const int count = juce::jmin (perRow, n - row * perRow);
            const int cw = juce::jmin (84, rr.getWidth() / perRow);
            auto line = rr.withSizeKeepingCentre (cw * count, rr.getHeight());
            for (int i = 0; i < count; ++i)
                knobs[row * perRow + i]->setBounds (line.removeFromLeft (cw).reduced (2, 0));
        }
    }

    juce::OwnedArray<Knob> knobs;
    juce::OwnedArray<juce::ComboBox> combos;
    juce::OwnedArray<juce::TextButton> toggles;

private:
    APVTS& state;
    juce::String title;
    juce::Colour accent;
    PowerButton power;
    bool hasPower = false;
    juce::Rectangle<int> extraArea;
    std::unique_ptr<APVTS::ButtonAttachment> powerAtt;
    juce::OwnedArray<APVTS::ComboBoxAttachment> comboAtts;
    juce::OwnedArray<APVTS::ButtonAttachment> toggleAtts;
};
} // namespace ui
