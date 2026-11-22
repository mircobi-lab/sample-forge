#include "PluginEditor.h"

namespace ui = mbstudio::ui;
namespace C = mbstudio::ui::colours;

namespace
{
const char keyLetters[sf::padsPerBank] = { 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K' };
const char* bankNames[sf::numBanks] = { "BATTERIA", "VOCI", "LOOP", "ACCORDI", "BASSO" };
juce::String u8 (const std::string& s) { return juce::String::fromUTF8 (s.c_str()); }
juce::String fmtBpm (double b) { return std::abs (b - std::round (b)) < 1e-9 ? juce::String ((int) std::round (b)) : juce::String (b, 2); }
}

// ======================================================================== PadView
void PadView::setPad (std::shared_ptr<const sf::Pad> p)
{
    pad = std::move (p);
    peaks.clear();
    if (pad)
    {
        const auto& L = pad->audio.L; const auto& R = pad->audio.R;
        const int n = 220; peaks.resize (n);
        const size_t step = std::max<size_t> (1, L.size() / n);
        for (int i = 0; i < n; ++i)
        {
            float m = 0;
            for (size_t j = (size_t) i * step; j < (size_t) (i + 1) * step && j < L.size(); j += 2) m = std::max ({ m, std::abs (L[j]), std::abs (R[j]) });
            peaks[(size_t) i] = m;
        }
    }
    repaint();
}

void PadView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.f);
    // tasto in gomma
    g.setColour (juce::Colours::black);
    g.fillRoundedRectangle (r.translated (0, pressed ? 1.f : 3.f), 4.f);
    auto body = r.withTrimmedBottom (3.f).translated (0, pressed ? 2.f : 0.f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff1d1e20), body.getX(), body.getY(), juce::Colour (0xff101112), body.getX(), body.getBottom(), false));
    g.fillRoundedRectangle (body, 4.f);
    g.setColour (lit ? juce::Colour (0xff5b5e64) : C::keyEdge);
    g.drawRoundedRectangle (body.reduced (0.5f), 4.f, 1.f);
    g.setColour (juce::Colours::white.withAlpha (0.07f));
    g.drawHorizontalLine ((int) body.getY() + 1, body.getX() + 4, body.getRight() - 4);

    auto in = body.reduced (8.f, 7.f);
    auto top = in.removeFromTop (18.f);
    const auto key = juce::String::charToString (letter);
    ui::paintChip (g, juce::Rectangle<float> (top.getX(), top.getCentreY() - 9.f, ui::chipWidth (key, 18.f), 18.f), key);
    ui::paintLed (g, { top.getRight() - 5.f, top.getCentreY() }, 4.f, C::ledGreen, lit ? 1.f : 0.f);

    in.removeFromTop (5.f);
    auto wave = in.removeFromTop (std::max (30.f, in.getHeight() - 36.f));
    ui::paintRecess (g, wave, 2.f);
    if (! peaks.empty())
    {
        auto w = wave.reduced (3.f, 3.f);
        g.setColour (juce::Colour (0xffc4c6ca));
        const float cy = w.getCentreY(), hh = w.getHeight() * 0.48f;
        for (int x = 0; x < (int) w.getWidth(); ++x)
        {
            const float v = peaks[(size_t) juce::jlimit (0, (int) peaks.size() - 1, (int) ((float) x * (float) peaks.size() / w.getWidth()))];
            const float h = std::max (0.6f, v * hh);
            g.fillRect (w.getX() + x, cy - h, 1.f, 2 * h);
        }
        if (playhead >= 0)
        {
            const float px = w.getX() + playhead * w.getWidth();
            g.setColour (C::print.withAlpha (0.35f)); g.fillRect (px - 2.f, wave.getY() + 1, 5.f, wave.getHeight() - 2);
            g.setColour (C::print); g.fillRect (px - 0.75f, wave.getY() + 1, 1.5f, wave.getHeight() - 2);
        }
    }
    in.removeFromTop (5.f);
    if (pad)
    {
        g.setColour (C::print); g.setFont (ui::Fonts::display (17.f));
        g.drawFittedText (u8 (pad->label).toUpperCase(), in.removeFromTop (17.f).toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);
        g.setColour (C::printDim); g.setFont (ui::Fonts::label (12.5f));
        g.drawFittedText (juce::String (pad->dur, 2) + " s  " + u8 (pad->sub), in.toNearestInt(), juce::Justification::topLeft, 1, 0.8f);
    }
    else
    {
        g.setColour (C::printDim.withAlpha (0.6f)); g.setFont (ui::Fonts::display (16.f));
        g.drawText ("VUOTO", in.toNearestInt(), juce::Justification::topLeft);
    }
}

// ======================================================================== Editor
SampleForgeEditor::SampleForgeEditor (SampleForgeProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);
    setWantsKeyboardFocus (true);

    for (auto* b : { &loadBtn, &panicBtn, &origBtn, &halfBtn, &doubleBtn, &resliceBtn, &exportBtn }) addAndMakeVisible (*b);
    panicBtn.getProperties().set ("led", true); panicBtn.getProperties().set ("ledColour", "ffff3b30");
    panicBtn.setTooltip ("Ferma subito tutti i suoni");
    loadBtn.onClick = [this] { chooseFiles(); };
    panicBtn.onClick = [this] { proc.requestPanic(); panicFlash = 15; heldKeys.clear(); };
    origBtn.getProperties().set ("led", true);
    origBtn.onClick = [this] { proc.playOriginal (proc.isPlayingOriginal() ? -1 : 0); };

    for (int b = 0; b < sf::numBanks; ++b)
    {
        auto& btn = bankBtns[(size_t) b];
        btn.setButtonText (juce::String (bankNames[b]) + "  " + juce::String (b + 1));
        btn.setClickingTogglesState (true);
        btn.setRadioGroupId (1);
        btn.onClick = [this, b] { selectBank (b); };
        addAndMakeVisible (btn);
    }
    for (int i = 0; i < sf::padsPerBank; ++i)
    {
        padViews[(size_t) i] = std::make_unique<PadView> (i, keyLetters[i]);
        padViews[(size_t) i]->onDown = [this] (int idx) { selectedPad = bank * sf::padsPerBank + idx; proc.padDown (selectedPad); repaint (overviewArea); };
        padViews[(size_t) i]->onUp = [this] (int idx) { proc.padUp (bank * sf::padsPerBank + idx); };
        addAndMakeVisible (*padViews[(size_t) i]);
    }

    const char* names[6] = { "VOLUME", "INTONAZ.", "ATTACCO", "DECAY", "SUSTAIN", "RILASCIO" };
    const double ranges[6][3] = { { 0, 1.5, 0.01 }, { -12, 12, 1 }, { 0, 1, 0.005 }, { 0.01, 2, 0.01 }, { 0, 1, 0.01 }, { 0.005, 2, 0.005 } };
    for (int i = 0; i < 6; ++i)
    {
        auto& k = knobs[(size_t) i];
        k.name = names[i];
        k.slider.setSliderStyle (juce::Slider::LinearHorizontal);
        k.slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 62, 22);
        k.slider.setRange (ranges[i][0], ranges[i][1], ranges[i][2]);
        k.slider.textFromValueFunction = [i] (double v)
        {
            switch (i) { case 0: case 4: return juce::String (juce::roundToInt (v * 100)) + "%"; case 1: return (v > 0 ? "+" : "") + juce::String ((int) v) + " st";
                         case 2: case 5: return juce::String (v, 3) + "s"; default: return juce::String (v, 2) + "s"; }
        };
        k.slider.onValueChange = [this, i]
        {
            auto& s = proc.sampler.settings[(size_t) bank];
            const float v = (float) knobs[(size_t) i].slider.getValue();
            switch (i) { case 0: s.vol = v; break; case 1: s.pitch = v; break; case 2: s.attack = v; break; case 3: s.decay = v; break; case 4: s.sustain = v; break; default: s.release = v; }
        };
        addAndMakeVisible (k.slider);
    }
    modeBox.addItem ("ONE-SHOT", 1); modeBox.addItem ("GATE (TIENI PREMUTO)", 2); modeBox.addItem ("LATCH (LOOP ON/OFF)", 3);
    modeBox.onChange = [this] { proc.sampler.settings[(size_t) bank].mode = modeBox.getSelectedId() - 1; };
    chokeBtn.onClick = [this] { proc.sampler.settings[(size_t) bank].choke = chokeBtn.getToggleState(); };
    addAndMakeVisible (modeBox); addAndMakeVisible (chokeBtn);

    bpmEdit.setInputRestrictions (6, "0123456789.,");
    bpmEdit.setJustification (juce::Justification::centred);
    bpmEdit.setText ("120");
    addAndMakeVisible (bpmEdit);
    auto bpmValue = [this] { return bpmEdit.getText().replaceCharacter (',', '.').getDoubleValue(); };
    halfBtn.onClick = [this, bpmValue] { proc.reslice (std::round (bpmValue() / 2 * 100) / 100); };
    doubleBtn.onClick = [this, bpmValue] { proc.reslice (std::round (bpmValue() * 2 * 100) / 100); };
    resliceBtn.onClick = [this, bpmValue] { const double b = bpmValue(); if (b >= 40 && b <= 300) proc.reslice (b); else showToast ("BPM fuori intervallo (40-300)"); };
    halfBtn.setTooltip ("Dimezza il tempo e ritaglia di nuovo"); doubleBtn.setTooltip ("Raddoppia il tempo e ritaglia di nuovo"); resliceBtn.setTooltip ("Ritaglia i campioni con il BPM scritto");

    bitsBox.addItem ("WAV 24 BIT", 24); bitsBox.addItem ("WAV 16 BIT", 16); bitsBox.setSelectedId (24);
    addAndMakeVisible (bitsBox); addAndMakeVisible (opt432); addAndMakeVisible (optStems);
    exportBtn.onClick = [this] { chooseExport(); };

    addAndMakeVisible (ledBtn);
    ledBtn.setToggleState (proc.controllerLedsOn(), juce::dontSendNotification);
    ledBtn.setTooltip ("Mostra lo stato dei pad sui LED di Push, Launchpad o Launchkey collegato");
    ledBtn.onClick = [this] { proc.setControllerLeds (ledBtn.getToggleState()); };
    ledBtn.setTooltip ("LED dei pad sul controller collegato (Push 2 USER, Launchpad Mini MK3, Launchkey)");

    proc.onStateChanged = [this] { refreshFromResult(); };
    proc.onToast = [this] (juce::String s) { showToast (s); };

    setResizable (true, true);
    setResizeLimits (980, 820, 1800, 1400);
    setSize (1160, 900);
    selectBank (0);
    refreshFromResult();
    startTimerHz (30);

    // prova automatica (CI): carica un file e salva una schermata dopo l'analisi
    if (auto f = juce::SystemStats::getEnvironmentVariable ("SF_AUTOLOAD", {}); f.isNotEmpty() && ! proc.hasSong())
    {
        juce::Array<juce::File> files;
        for (auto& s : juce::StringArray::fromTokens (f, ";", "")) files.add (juce::File (s));
        juce::MessageManager::callAsync ([this, files] { proc.loadFiles (files); });
    }
}

SampleForgeEditor::~SampleForgeEditor()
{
    proc.onStateChanged = nullptr; proc.onToast = nullptr;
    setLookAndFeel (nullptr);
}

void SampleForgeEditor::selectBank (int b)
{
    proc.uiBank = b;
    for (int i = 0; i < sf::numBanks; ++i) if (i != b) heldKeys.erase (i);
    bank = b;
    bankBtns[(size_t) b].setToggleState (true, juce::dontSendNotification);
    refreshControls();
    auto r = proc.getResult();
    for (int i = 0; i < sf::padsPerBank; ++i)
        padViews[(size_t) i]->setPad (r ? std::shared_ptr<const sf::Pad> (r, r->banks[(size_t) b].pads[(size_t) i].get()) : nullptr);
    repaint();
}

void SampleForgeEditor::refreshControls()
{
    auto& s = proc.sampler.settings[(size_t) bank];
    const float vals[6] = { s.vol.load(), s.pitch.load(), s.attack.load(), s.decay.load(), s.sustain.load(), s.release.load() };
    for (int i = 0; i < 6; ++i) knobs[(size_t) i].slider.setValue (vals[i], juce::dontSendNotification);
    modeBox.setSelectedId (s.mode.load() + 1, juce::dontSendNotification);
    chokeBtn.setToggleState (s.choke.load(), juce::dontSendNotification);
}

void SampleForgeEditor::refreshFromResult()
{
    auto r = proc.getResult();
    if (r != shown)
    {
        shown = r;
        if (r) bpmEdit.setText (fmtBpm (r->bpm), juce::dontSendNotification);
        selectedPad = -1;
        selectBank (bank);
    }
    const bool ready = r != nullptr;
    for (auto* b : { &halfBtn, &doubleBtn, &resliceBtn, &exportBtn, &origBtn }) b->setEnabled (ready);
    repaint();
}

void SampleForgeEditor::showToast (const juce::String& s)
{
    toastText = s; toastUntil = juce::Time::currentTimeMillis() + 4000; repaint();
}

void SampleForgeEditor::chooseFiles()
{
    chooser = std::make_unique<juce::FileChooser> ("Scegli il brano oppure tutti gli stem insieme", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg;*.m4a");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::canSelectMultipleItems,
                          [this] (const juce::FileChooser& fc)
                          {
                              auto files = fc.getResults();
                              if (! files.isEmpty()) proc.loadFiles (files);
                          });
}

void SampleForgeEditor::chooseExport()
{
    auto name = "MBStudio91_SampleForge_" + juce::File::createLegalFileName (proc.getSongName()).replaceCharacter (' ', '_') + ".zip";
    chooser = std::make_unique<juce::FileChooser> ("Salva il pacchetto", juce::File::getSpecialLocation (juce::File::userDesktopDirectory).getChildFile (name), "*.zip");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this] (const juce::FileChooser& fc)
                          {
                              auto f = fc.getResult();
                              if (f == juce::File()) return;
                              if (! f.hasFileExtension ("zip")) f = f.withFileExtension ("zip");
                              proc.exportPack (f, { bitsBox.getSelectedId(), opt432.getToggleState(), optStems.getToggleState() });
                          });
}

void SampleForgeEditor::filesDropped (const juce::StringArray& paths, int, int)
{
    dragOver = false;
    juce::Array<juce::File> files;
    for (auto& p : paths)
    {
        juce::File f (p);
        if (f.hasFileExtension ("wav;aif;aiff;flac;mp3;ogg;m4a;caf")) files.add (f);
    }
    if (files.isEmpty()) showToast ("Nessun file audio riconosciuto");
    else proc.loadFiles (files);
    repaint();
}

bool SampleForgeEditor::keyPressed (const juce::KeyPress& k)
{
    const auto c = juce::CharacterFunctions::toUpperCase (k.getTextCharacter());
    if (k == juce::KeyPress::spaceKey) { proc.requestPanic(); proc.playOriginal (-1); heldKeys.clear(); return true; }
    if (c >= '1' && c <= '5') { selectBank (c - '1'); return true; }
    for (int i = 0; i < sf::padsPerBank; ++i)
        if (c == keyLetters[i])
        {
            if (! heldKeys.count (i)) { heldKeys.insert (i); selectedPad = bank * sf::padsPerBank + i; proc.padDown (selectedPad); repaint (overviewArea); }
            return true;
        }
    return false;
}

bool SampleForgeEditor::keyStateChanged (bool isKeyDown)
{
    if (isKeyDown) return false;
    for (auto it = heldKeys.begin(); it != heldKeys.end();)
    {
        const int i = *it;
        if (! juce::KeyPress::isKeyCurrentlyDown (juce::CharacterFunctions::toLowerCase ((juce::juce_wchar) keyLetters[i])) && ! juce::KeyPress::isKeyCurrentlyDown (keyLetters[i]))
        {
            proc.padUp (bank * sf::padsPerBank + i);
            it = heldKeys.erase (it);
        }
        else ++it;
    }
    return false;
}

void SampleForgeEditor::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    if (overviewArea.contains (e.getPosition()) && proc.hasSong())
        proc.playOriginal ((e.x - overviewArea.getX()) / (double) overviewArea.getWidth() * proc.getSongDuration());
}

void SampleForgeEditor::timerCallback()
{
    if (const int ctrlBank = proc.uiBank.load(); ctrlBank != bank) selectBank (ctrlBank);   // banco scelto sul controller
    if (const auto name = proc.controllerName(); name.isNotEmpty()) ledBtn.setButtonText ("LED: " + name);
    else ledBtn.setButtonText ("LED CONTROLLER");

    for (int i = 0; i < sf::padsPerBank; ++i)
    {
        const int idx = bank * sf::padsPerBank + i;
        const float ph = proc.sampler.playhead[(size_t) idx].load();
        padViews[(size_t) i]->setPlayhead (ph);
        padViews[(size_t) i]->setLit (ph >= 0 || proc.sampler.latched[(size_t) idx].load());
    }
    const bool orig = proc.isPlayingOriginal();
    origBtn.setToggleState (orig, juce::dontSendNotification);
    origBtn.setButtonText (orig ? "FERMA ORIGINALE" : "ASCOLTA ORIGINALE");
    panicBtn.setToggleState (panicFlash > 0, juce::dontSendNotification);
    if (panicFlash > 0) panicFlash--;
    const auto st = proc.getStatus();
    loadBtn.setEnabled (! st.busy);
    exportBtn.setEnabled (! st.busy && proc.getResult() != nullptr);
    repaint (lcdArea); repaint (overviewArea);
    if (toastUntil > 0 && juce::Time::currentTimeMillis() > toastUntil) { toastUntil = 0; repaint(); }

    static const juce::String shotPath = juce::SystemStats::getEnvironmentVariable ("SF_SCREENSHOT", {});
    if (shotPath.isNotEmpty() && proc.getResult() != nullptr && ! st.busy)
    {
        if (++shotCountdown == 20) { selectBank (2); proc.padDown (2 * sf::padsPerBank); }   // un loop in play per vedere la linea
        if (shotCountdown == 50)
        {
            juce::File out (shotPath); out.deleteFile();
            juce::FileOutputStream os (out);
            juce::PNGImageFormat().writeImageToStream (createComponentSnapshot (getLocalBounds()), os);
            os.flush();
            std::printf ("SCREENSHOT %s\n", out.getFullPathName().toRawUTF8());
            if (auto r = proc.getResult()) { int n = 0; for (auto& b : r->banks) for (auto& p : b.pads) if (p) n++;
                std::printf ("RISULTATO bpm %g key %s pad %d\n", r->bpm, r->key.name.c_str(), n); }
            std::fflush (stdout);
            if (auto* app = juce::JUCEApplicationBase::getInstance()) app->systemRequestedQuit();
        }
    }
}

// ------------------------------------------------------------------------ disegno
void SampleForgeEditor::paint (juce::Graphics& g)
{
    ui::paintChassis (g, getLocalBounds());
    ui::paintBrand (g, headerArea.toFloat().withTrimmedRight (360).removeFromTop (34.f), "SAMPLE FORGE");
    g.setColour (C::printDim); g.setFont (ui::Fonts::label (13.f));
    g.drawText ("BRANO DENTRO  >  PACCHETTO DI CAMPIONI FUORI", headerArea.withTrimmedTop (38).withTrimmedRight (360), juce::Justification::topLeft);

    // display
    ui::paintRecess (g, lcdArea.toFloat());
    auto lcd = lcdArea.reduced (14, 10);
    auto r = proc.getResult();
    const auto st = proc.getStatus();
    auto right = lcd.removeFromRight (230);
    g.setColour (C::print); g.setFont (ui::Fonts::thin (46.f));
    auto bpmRow = right.removeFromTop (48);
    g.drawText (r ? fmtBpm (r->bpm) : juce::String ("-"), bpmRow.withTrimmedRight (40), juce::Justification::centredRight);
    g.setColour (C::printDim); g.setFont (ui::Fonts::display (16.f));
    g.drawText ("BPM", bpmRow.removeFromRight (36), juce::Justification::centredRight);
    g.setColour (C::print); g.setFont (ui::Fonts::label (15.f));
    const juce::String song = proc.hasSong() ? proc.getSongName() + "  -  " + juce::String ((int) proc.getSongDuration() / 60) + ":" + juce::String ((int) proc.getSongDuration() % 60).paddedLeft ('0', 2)
                                             : juce::String::fromUTF8 ("NESSUN BRANO CARICATO - TRASCINA QUI UN FILE AUDIO O GLI STEM");
    g.drawFittedText (song.toUpperCase(), lcd.removeFromTop (22), juce::Justification::centredLeft, 1, 0.85f);
    g.drawText (r ? juce::String::fromUTF8 ("TONALITÀ: ") + u8 (r->key.name) + " (" + u8 (r->key.it).toUpperCase() + ")" : juce::String::fromUTF8 ("TONALITÀ: -"), lcd.removeFromTop (22), juce::Justification::centredLeft);
    juce::StringArray prog;
    if (r) for (auto& c : r->progression) { auto s = u8 (c); if (prog.isEmpty() || prog[prog.size() - 1] != s) prog.add (s); }
    g.setColour (C::printDim); g.setFont (ui::Fonts::label (14.f));
    g.drawFittedText ("Accordi: " + (prog.isEmpty() ? juce::String ("-") : prog.joinIntoString ("  ")), lcd.removeFromTop (20), juce::Justification::centredLeft, 1, 0.9f);
    auto statusRow = lcd.removeFromTop (22);
    g.setColour (C::print); g.setFont (ui::Fonts::label (14.f));
    g.drawFittedText (st.message.isEmpty() ? juce::String ("Pronto. Carica un brano (mp3, wav, aiff, flac) o gli stem separati.") : st.message, statusRow, juce::Justification::centredLeft, 1, 0.9f);
    if (st.busy && st.progress >= 0)
    {
        auto bar = lcdArea.reduced (14, 0).removeFromBottom (12).withHeight (5).toFloat();
        g.setColour (juce::Colour (0xff0d0e0f)); g.fillRoundedRectangle (bar, 2.f);
        auto fill = bar.withWidth (bar.getWidth() * (float) juce::jlimit (0.0, 1.0, st.progress));
        g.setColour (C::ledGreen.withAlpha (0.35f)); g.fillRoundedRectangle (fill.expanded (0, 2), 3.f);
        g.setColour (C::ledGreen); g.fillRoundedRectangle (fill, 2.f);
    }

    // forma d'onda del brano
    ui::paintRecess (g, overviewArea.toFloat());
    const auto& ov = proc.getOverview();
    auto ow = overviewArea.reduced (3, 4).toFloat();
    if (! ov.empty() && proc.getSongDuration() > 0)
    {
        const double dur = proc.getSongDuration();
        if (r)
        {
            g.setColour (C::print.withAlpha (0.08f));
            for (double t = r->beat0; t < dur; t += r->T * 4) g.fillRect ((float) (ow.getX() + t / dur * ow.getWidth()), ow.getY(), 1.f, ow.getHeight());
            if (selectedPad >= 0)
                if (auto pad = r->banks[(size_t) (selectedPad / 8)].pads[(size_t) (selectedPad % 8)])
                {
                    g.setColour (C::ledGreen.withAlpha (0.22f));
                    g.fillRect ((float) (ow.getX() + pad->start / dur * ow.getWidth()), ow.getY(), (float) std::max (2.0, pad->dur / dur * ow.getWidth()), ow.getHeight());
                }
        }
        g.setColour (C::printDim);
        for (int x = 0; x < (int) ow.getWidth(); ++x)
        {
            const float v = ov[(size_t) juce::jlimit (0, (int) ov.size() - 1, (int) ((float) x * (float) ov.size() / ow.getWidth()))];
            const float h = std::max (0.5f, v * ow.getHeight() * 0.46f);
            g.fillRect (ow.getX() + x, ow.getCentreY() - h, 1.f, 2 * h);
        }
        const double op = proc.originalPositionSeconds();
        if (op >= 0) { g.setColour (C::print); g.fillRect ((float) (ow.getX() + op / dur * ow.getWidth()), ow.getY(), 2.f, ow.getHeight()); }
    }
    else
    {
        g.setColour (C::printDim.withAlpha (0.7f)); g.setFont (ui::Fonts::label (14.f));
        g.drawText (juce::String::fromUTF8 ("LA FORMA D'ONDA DEL BRANO APPARIRÀ QUI  -  CLIC PER ASCOLTARE DA QUEL PUNTO"), ow.toNearestInt().reduced (10, 0), juce::Justification::centredLeft);
    }

    // sezioni incise con titolo-chip
    auto section = [&g] (juce::Rectangle<int> area, const juce::String& title)
    {
        ui::paintEngravedLine (g, { (float) area.getX(), (float) area.getY() }, { (float) area.getRight(), (float) area.getY() });
        if (title.isNotEmpty()) ui::paintChip (g, juce::Rectangle<float> ((float) area.getX(), area.getY() + 12.f, ui::chipWidth (title, 20.f), 20.f), title);
    };
    section (padsArea.withX (getLocalBounds().reduced (20).getX()).withWidth (getWidth() - 40).withHeight (2).translated (0, -18), {});
    ui::paintChip (g, bankChipArea.toFloat().withWidth (ui::chipWidth (juce::String ("BANCO ") + bankNames[bank], 22.f)), juce::String ("BANCO ") + bankNames[bank]);
    auto ctrl = controlsArea.withTrimmedTop (36);
    g.setColour (C::print); g.setFont (ui::Fonts::label (14.f));
    for (int i = 0; i < 6; ++i)
        g.drawText (knobs[(size_t) i].name, ctrl.getX(), knobs[(size_t) i].slider.getY(), 74, knobs[(size_t) i].slider.getHeight(), juce::Justification::centredLeft);
    ui::paintEngravedLine (g, { (float) controlsArea.getX() - 14, (float) padsArea.getY() }, { (float) controlsArea.getX() - 14, (float) padsArea.getBottom() });

    section (transportArea, "TEMPO E RITAGLIO");
    g.setColour (C::print); g.setFont (ui::Fonts::label (14.f));
    g.drawText ("BPM", bpmEdit.getBounds().translated (-48, 0).withWidth (40), juce::Justification::centredRight);
    g.setColour (C::printDim);
    g.drawFittedText (juce::String::fromUTF8 ("Se il tempo rilevato è il doppio o la metà, usa /2 o x2: i campioni vengono ritagliati di nuovo."), resliceBtn.getBounds().withX (resliceBtn.getRight() + 18).withRight (transportArea.getRight()), juce::Justification::centredLeft, 2, 0.9f);

    section (exportArea, "ESPORTA PACCHETTO");

    // piede
    ui::paintEngravedLine (g, { (float) footerArea.getX(), (float) footerArea.getY() }, { (float) footerArea.getRight(), (float) footerArea.getY() });
    g.setColour (C::printDim); g.setFont (ui::Fonts::label (13.f));
    g.drawFittedText ("TASTI A-K: PAD  -  1-5: BANCHI  -  SPAZIO: STOP  -  MIDI: NOTE 36-75 (C1-D#4)  -  USA SOLO BRANI DI CUI HAI I DIRITTI",
                      footerArea.withTrimmedTop (8).withTrimmedRight (240), juce::Justification::centredLeft, 1, 0.8f);
    ui::paintLogo (g, footerArea.withTrimmedTop (6).removeFromRight (220).toFloat().withHeight (24), false, ui::Imprint::debossed, juce::Justification::right);
}

void SampleForgeEditor::paintOverChildren (juce::Graphics& g)
{
    if (dragOver)
    {
        g.fillAll (juce::Colour (0xe0050506));
        g.setColour (C::printDim); g.drawRect (getLocalBounds().reduced (6), 2);
        g.setColour (C::print); g.setFont (ui::Fonts::display (38.f));
        g.drawText ("RILASCIA IL BRANO O GLI STEM", getLocalBounds(), juce::Justification::centred);
    }
    if (toastUntil > 0)
    {
        g.setFont (ui::Fonts::label (15.f));
        const int w = std::min (getWidth() - 40, 40 + (int) juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), toastText));
        auto r = juce::Rectangle<int> (w, 40).withCentre ({ getWidth() / 2, getHeight() - 60 });
        g.setColour (C::recess); g.fillRoundedRectangle (r.toFloat(), 3.f);
        g.setColour (C::keyEdge); g.drawRoundedRectangle (r.toFloat(), 3.f, 1.f);
        g.setColour (C::print); g.drawFittedText (toastText, r.reduced (14, 0), juce::Justification::centred, 1, 0.8f);
    }
}

void SampleForgeEditor::resized()
{
    auto area = getLocalBounds().reduced (20, 16);
    headerArea = area.removeFromTop (58);
    {
        auto h = headerArea.withTrimmedTop (8).removeFromTop (34);
        panicBtn.setBounds (h.removeFromRight (110));
        h.removeFromRight (10);
        ledBtn.setBounds (h.removeFromRight (190));
        h.removeFromRight (10);
        loadBtn.setBounds (h.removeFromRight (220));
    }
    area.removeFromTop (10);
    lcdArea = area.removeFromTop (118);
    origBtn.setBounds (lcdArea.getRight() - 14 - 200, lcdArea.getY() + 68, 200, 30);
    area.removeFromTop (8);
    overviewArea = area.removeFromTop (78);
    area.removeFromTop (16);
    {
        auto banks = area.removeFromTop (38);
        const int w = (banks.getWidth() - 4 * 8) / 5;
        for (int b = 0; b < sf::numBanks; ++b) { bankBtns[(size_t) b].setBounds (banks.removeFromLeft (w)); banks.removeFromLeft (8); }
    }
    area.removeFromTop (34);
    footerArea = area.removeFromBottom (40);
    exportArea = area.removeFromBottom (92);
    transportArea = area.removeFromBottom (84);
    area.removeFromBottom (14);
    padsArea = area;
    controlsArea = padsArea.removeFromRight (300);
    padsArea.removeFromRight (28);
    {
        const int gap = 10, cols = 4, rows = 2;
        const int pw = (padsArea.getWidth() - gap * (cols - 1)) / cols, ph = (padsArea.getHeight() - gap * (rows - 1)) / rows;
        for (int i = 0; i < sf::padsPerBank; ++i)
            padViews[(size_t) i]->setBounds (padsArea.getX() + (i % cols) * (pw + gap), padsArea.getY() + (i / cols) * (ph + gap), pw, ph);
    }
    {
        auto c = controlsArea;
        bankChipArea = c.removeFromTop (22).withWidth (180);
        c.removeFromTop (14);
        const int rowH = std::max (28, std::min (36, (c.getHeight() - 44) / 6));
        for (auto& k : knobs) { auto row = c.removeFromTop (rowH); k.slider.setBounds (row.withTrimmedLeft (78)); }
        c.removeFromTop (8);
        auto last = c.removeFromTop (32);
        modeBox.setBounds (last.removeFromLeft (190));
        last.removeFromLeft (10);
        chokeBtn.setBounds (last);
    }
    {
        auto t = transportArea.withTrimmedTop (42).removeFromTop (34);
        t.removeFromLeft (52);
        bpmEdit.setBounds (t.removeFromLeft (90)); t.removeFromLeft (10);
        halfBtn.setBounds (t.removeFromLeft (54)); t.removeFromLeft (6);
        doubleBtn.setBounds (t.removeFromLeft (54)); t.removeFromLeft (6);
        resliceBtn.setBounds (t.removeFromLeft (120));
    }
    {
        auto e = exportArea.withTrimmedTop (44).removeFromTop (34);
        bitsBox.setBounds (e.removeFromLeft (160)); e.removeFromLeft (16);
        opt432.setBounds (e.removeFromLeft (190)); e.removeFromLeft (8);
        optStems.setBounds (e.removeFromLeft (190));
        exportBtn.setBounds (e.removeFromRight (230));
    }
}
