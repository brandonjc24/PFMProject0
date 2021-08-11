/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
//#include "PluginProcessor.h"

//==============================================================================
/**
*/
struct PFMProject0AudioProcessorEditor;

class PFMProject0AudioProcessorEditor  : public juce::AudioProcessorEditor, public juce::Timer
{
public:
    PFMProject0AudioProcessorEditor (PFMProject0AudioProcessor&);
    ~PFMProject0AudioProcessorEditor() override;

    //==============================================================================
    void paint (juce::Graphics&) override;
    void resized() override;

    void mouseUp(const juce::MouseEvent& e) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void timerCallback() override;

private:
    void update();
    juce::Point<int> lastClickPosition;
    // This reference is provided as a quick way for your editor to
    // access the processor object that created it.
    PFMProject0AudioProcessor& audioProcessor;
    float cachedBgColor = 0.f;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PFMProject0AudioProcessorEditor)
};
