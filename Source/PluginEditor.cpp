/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
PFMProject0AudioProcessorEditor::PFMProject0AudioProcessorEditor 
    (PFMProject0AudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    // Make sure that before the constructor has finished, you've set the
    // editor's size to whatever you need it to be.
    cachedBgColor = audioProcessor.bgColor->get();
    PFMProject0AudioProcessor::UpdateAutomatableParameter(audioProcessor.playSound, true);

    addAndMakeVisible(audioProcessor.leftBufferAnalyzer);
    addAndMakeVisible(audioProcessor.rightBufferAnalyzer);

    audioProcessor.leftBufferAnalyzer.setInterceptsMouseClicks(false, false);
    audioProcessor.rightBufferAnalyzer.setInterceptsMouseClicks(false, false);


    setSize (400, 300);
    startTimerHz(20);
}

PFMProject0AudioProcessorEditor::~PFMProject0AudioProcessorEditor()
{
//    audioProcessor.playSound->beginChangeGesture();
//    audioProcessor.playSound->setValueNotifyingHost(false) ;
//    audioProcessor.playSound->endChangeGesture();
    PFMProject0AudioProcessor::UpdateAutomatableParameter(audioProcessor.playSound, false);
}

void PFMProject0AudioProcessorEditor::timerCallback()
{
    update();
}

void PFMProject0AudioProcessorEditor::update()
{
    cachedBgColor = audioProcessor.bgColor->get();
    repaint();
}

//==============================================================================
void PFMProject0AudioProcessorEditor::paint (juce::Graphics& g)
{
    // (Our component is opaque, so we must completely fill the background with a solid colour)
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId).interpolatedWith(juce::Colours::red, cachedBgColor));

    g.setColour (juce::Colours::white);
    g.setFont (15.0f);
    g.drawFittedText ("Hello World!", getLocalBounds(), juce::Justification::centred, 1);
}

void PFMProject0AudioProcessorEditor::resized()
{
    // This is generally where you'll want to lay out the positions of any
    // subcomponents in your editor..
    audioProcessor.leftBufferAnalyzer.setBounds(0, 0, getWidth(), getHeight() * 0.5);
    audioProcessor.rightBufferAnalyzer.setBounds(0, getHeight()*0.5, getWidth(), getHeight() * 0.5);

}

void PFMProject0AudioProcessorEditor::mouseUp(const juce::MouseEvent &e)
{
    //audioprocessor.playsound->beginchangegesture();
    //audioprocessor.playsound->setvaluenotifyinghost( !audioprocessor.playsound->get() );
    //audioprocessor.playsound->endchangegesture();
    PFMProject0AudioProcessor::UpdateAutomatableParameter(audioProcessor.playSound, !audioProcessor.playSound->get() );
}

void PFMProject0AudioProcessorEditor::mouseDown(const juce::MouseEvent& e)
{
    lastClickPosition = e.getPosition();
}

void PFMProject0AudioProcessorEditor::mouseDrag(const juce::MouseEvent& e)
{
    auto clickPosition = e.getPosition();
    auto difY = juce::jlimit(-1.0, 1.0, (lastClickPosition.y - clickPosition.y) / 200.0 );
    difY = juce::jmap(difY, -1.0, 1.0, 0.0, 1.0);

    DBG("difY: " << difY);

    PFMProject0AudioProcessor::UpdateAutomatableParameter(audioProcessor.bgColor, difY);
    update();
}