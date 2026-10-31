#pragma once
#include "PluginProcessor.h"
#include <map>
#include "UI/Widgets.h"
#include "UI/EqDisplay.h"
#include "UI/MonitorView.h"

class VoxRehabEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit VoxRehabEditor (VoxRehabProcessor&);
    ~VoxRehabEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int kW = 1560, kH = 1040;
    void selectView (int id) { viewBox.setSelectedId (id, juce::sendNotificationSync); }     // 1 = EQ, 100 + Monitor::Module
    void tickForTest() { timerCallback(); }

private:
    using Order = VoxRehabProcessor::Order;
    void timerCallback() override;
    ui::ModulePanel& panel (const juce::String& title, juce::Colour c, const juce::String& power = {});
    void showView (int monitorModule);   // -1 = EQ
    void layoutChain (bool animate);
    void dragMove (int stage, juce::Point<float> p);
    void dragEnd (int stage, juce::Point<float> p);
    int insertionIndexFor (juce::Point<float> p, juce::Rectangle<int>& bar) const;

    VoxRehabProcessor& proc;
    ui::Look look;
    juce::TooltipWindow tooltips { this, 600 };

    struct Content : juce::Component
    {
        void paint (juce::Graphics& g) override;
        void paintOverChildren (juce::Graphics& g) override;
        juce::String latencyText, chainText;
        juce::Rectangle<int> dropBar;
        std::vector<juce::Rectangle<int>> groupRects;
    } content;

    juce::ComboBox presetBox;
    juce::TextButton prevBtn { "<" }, nextBtn { ">" }, resetOrderBtn { "RESET ORDER" };
    ui::EqDisplay eqDisplay;
    ui::MonitorView monitorView;
    juce::ComboBox viewBox, spanBox;
    std::map<ui::ModulePanel*, int> panelModule;
    ui::BandEditor bandEditor;
    juce::OwnedArray<ui::ModulePanel> panels;
    ui::ModulePanel *pInput {}, *pHum {}, *pClick {}, *pPlos {}, *pNoise {}, *pBg {}, *pIso {}, *pBreath {}, *pVerb {},
                    *pEss {}, *pFet {}, *pOpto {}, *pArt {}, *pOut {}, *pEq {}, *pGlue {};
    std::array<std::vector<ui::ModulePanel*>, VoxRehabProcessor::kStages> stagePanels;
    std::map<ui::ModulePanel*, int> preferredWidth;
    Order shownOrder {};
    float inMeter = -100, outMeter = -100;
    int lastClicks = 0, clickFlash = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VoxRehabEditor)
};
