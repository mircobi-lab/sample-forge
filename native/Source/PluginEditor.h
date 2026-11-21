// Sample Forge · MBStudio91 — interfaccia hardware (kit MBStudioUI)
#pragma once
#include <juce_gui_extra/juce_gui_extra.h>
#include "PluginProcessor.h"
#include "MBStudioLookAndFeel.h"
#include <set>

class PadView : public juce::Component
{
public:
    PadView (int indexInBank, char keyLetter) : index (indexInBank), letter (keyLetter) {}
    void setPad (std::shared_ptr<const sf::Pad> p);
    void setPlayhead (float pos) { if (std::abs (pos - playhead) > 0.002f) { playhead = pos; repaint(); } }
    void setLit (bool b) { if (b != lit) { lit = b; repaint(); } }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override { if (pad && onDown) onDown (index); pressed = true; repaint(); }
    void mouseUp (const juce::MouseEvent&) override { if (onUp) onUp (index); pressed = false; repaint(); }
    std::function<void (int)> onDown, onUp;
    bool hasPad() const { return pad != nullptr; }
private:
    int index; char letter;
    std::shared_ptr<const sf::Pad> pad;
    std::vector<float> peaks;
    float playhead = -1; bool lit = false, pressed = false;
};

class SampleForgeEditor : public juce::AudioProcessorEditor, public juce::FileDragAndDropTarget, private juce::Timer
{
public:
    explicit SampleForgeEditor (SampleForgeProcessor&);
    ~SampleForgeEditor() override;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    bool keyStateChanged (bool isKeyDown) override;
    void mouseDown (const juce::MouseEvent&) override;

    bool isInterestedInFileDrag (const juce::StringArray&) override { return true; }
    void fileDragEnter (const juce::StringArray&, int, int) override { dragOver = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dragOver = false; repaint(); }
    void filesDropped (const juce::StringArray&, int, int) override;

private:
    void timerCallback() override;
    void selectBank (int b);
    void refreshFromResult();
    void refreshControls();
    void showToast (const juce::String&);
    void chooseFiles();
    void chooseExport();
    juce::Rectangle<int> lcdArea, overviewArea, padsArea, controlsArea, transportArea, exportArea, footerArea, headerArea;

    SampleForgeProcessor& proc;
    mbstudio::ui::HardwareLookAndFeel lnf;

    juce::TextButton loadBtn { "CARICA BRANO / STEM" }, panicBtn { "PANIC" }, origBtn { "ASCOLTA ORIGINALE" };
    std::array<juce::TextButton, sf::numBanks> bankBtns;
    std::array<std::unique_ptr<PadView>, sf::padsPerBank> padViews;

    struct Knob { juce::Slider slider; juce::String name; };
    std::array<Knob, 6> knobs;
    juce::ComboBox modeBox;
    juce::ToggleButton chokeBtn { "CHOKE" };

    juce::TextEditor bpmEdit;
    juce::TextButton halfBtn { "/2" }, doubleBtn { "x2" }, resliceBtn { "RITAGLIA" };

    juce::ComboBox bitsBox;
    juce::ToggleButton opt432 { "COPIA A 432 HZ" }, optStems { "STEM COMPLETI" };
    juce::TextButton exportBtn { "ESPORTA PACK (.ZIP)" };

    std::unique_ptr<juce::FileChooser> chooser;
    int bank = 0, selectedPad = -1;
    bool dragOver = false;
    std::set<int> heldKeys;
    juce::String toastText; juce::int64 toastUntil = 0;
    int panicFlash = 0, shotCountdown = 0;
    std::shared_ptr<const sf::Result> shown;
    juce::Rectangle<int> bankChipArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SampleForgeEditor)
};
