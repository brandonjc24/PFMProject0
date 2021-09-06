/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
BufferAnalyzer::~BufferAnalyzer()
{
    notify();
    stopThread(100);
}
void BufferAnalyzer::prepare(double sampleRate, int samplesPerBlock)
{
    firstBuffer = true;
    buffers[0].setSize(1, samplesPerBlock);
    buffers[1].setSize(1, samplesPerBlock);

    samplesCopied[0] = 0;
    samplesCopied[1] = 0;


    fifoIndex = 0;
    juce::zeromem(fifoBuffer, sizeof(fifoBuffer));
    juce::zeromem(fftData, sizeof(fftData));
    juce::zeromem(curveData, sizeof(curveData));

}

void BufferAnalyzer::cloneBuffer(const juce::dsp::AudioBlock<float> &other)
{
    auto whichIndex = firstBuffer.get();
    auto index = whichIndex ? 0 : 1;
    firstBuffer.set(!whichIndex);

    jassert(other.getNumChannels() == buffers[index].getNumChannels());
    jassert(other.getNumSamples() <= buffers[index].getNumSamples());

    buffers[index].clear();

    juce::dsp::AudioBlock<float> buffer(buffers[index]);
    buffer.copyFrom(other);

    samplesCopied[index] = other.getNumSamples();

    notify();
}

void BufferAnalyzer::run()
{
    while(true)
    {
        wait(-1);

        DBG("BufferAnalyzer::run() awake!");

        if (threadShouldExit())
            break;

        auto index = !firstBuffer.get() ? 0 : 1;

        auto* readPtr = buffers[index].getReadPointer(0);
        for (int i = 0; i < samplesCopied[index]; ++i)
        {
            //pushNextSampleIntoFifo(buffers[index].getSample(0, i));
            // get sample is slow use pointer
            pushNextSampleIntoFifo(*(readPtr + i));
        }
    }
}

void BufferAnalyzer::pushNextSampleIntoFifo(float sample)
{
    if (fifoIndex == fftSize)
    {
        if (nextFFTBlockReady == false)
        {
            juce::zeromem(fftData, sizeof(fftData));
            memcpy(fftData, fifoBuffer, sizeof(fifoBuffer));
            nextFFTBlockReady = true;
        }
        fifoIndex = 0;
    }
    fifoBuffer[fifoIndex++] = sample;
}

void BufferAnalyzer::timerCallback()
{
    if (nextFFTBlockReady)
    {
        drawNextFrameOfSpectrum();
        nextFFTBlockReady = false;
        repaint();
    }
}

void BufferAnalyzer::drawNextFrameOfSpectrum()
{
    // first apply a windowing function to our data
    window.multiplyWithWindowingTable(fftData, fftSize);       // [1]

    // then render our FFT data..
    forwardFFT.performFrequencyOnlyForwardTransform(fftData);  // [2]

    auto mindB = -100.0f;
    auto maxdB = 0.0f;

    for (int i = 0; i < numPoints; ++i)                         // [3]
    {
        auto skewedProportionX = 1.0f - std::exp(std::log(1.0f - (float)i / (float)numPoints) * 0.2f);
        auto fftDataIndex = juce::jlimit(0, fftSize / 2, (int)(skewedProportionX * (float)fftSize * 0.5f));
        auto level = juce::jmap(juce::jlimit(mindB, maxdB, juce::Decibels::gainToDecibels(fftData[fftDataIndex])
            - juce::Decibels::gainToDecibels((float)fftSize)),
            mindB, maxdB, 0.0f, 1.0f);

        curveData[i] = level;                                   // [4]
    }
}

void BufferAnalyzer::paint(juce::Graphics& g)
{
    float w = getWidth();
    float h = getHeight();
    juce::Path fftCurve;
    fftCurve.startNewSubPath(0, juce::jmap(curveData[0], 0.f, 1.f, h, 0.f) );

    for (int i = 1; i < numPoints; ++i)
    {
        auto data = curveData[i];
        auto endX = juce::jmap((float)i, 0.f, float(numPoints), 0.f, w);
        auto endY = juce::jmap(data, 0.f, 1.f, h, 0.f);

        fftCurve.lineTo(endX, endY);


    }

    g.fillAll(juce::Colours::black);
    g.setColour(juce::Colours::white);
    g.strokePath(fftCurve, juce::PathStrokeType(1));
}
//==============================================================================
PFMProject0AudioProcessor::PFMProject0AudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
     : AudioProcessor (BusesProperties()
                     #if ! JucePlugin_IsMidiEffect
                      #if ! JucePlugin_IsSynth
                       .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      #endif
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                     #endif
                       ),
#endif
    apvts(*this, nullptr)
{
//    playSound = new juce::AudioParameterBool("playSoundParam", "playSound", false);
//    addParameter(playSound);
    auto playSoundParam = std::make_unique<juce::AudioParameterBool>("playSoundParam", "playSound", false);

    auto* param = apvts.createAndAddParameter( std::move(playSoundParam) );

    playSound = dynamic_cast<juce::AudioParameterBool*>(param);

    auto bgColorParam = std::make_unique < juce::AudioParameterFloat>("Background Color", "background color", 0.f, 1.f, 0.5f);
    param = apvts.createAndAddParameter(std::move(bgColorParam));
    bgColor = dynamic_cast<juce::AudioParameterFloat*>(param);

    apvts.state = juce::ValueTree("PFMSynthValueTree");
}

PFMProject0AudioProcessor::~PFMProject0AudioProcessor()
{
}

//==============================================================================
const juce::String PFMProject0AudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool PFMProject0AudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool PFMProject0AudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool PFMProject0AudioProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double PFMProject0AudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int PFMProject0AudioProcessor::getNumPrograms()
{
    return 1;   // NB: some hosts don't cope very well if you tell them there are 0 programs,
                // so this should be at least 1, even if you're not really implementing programs.
}

int PFMProject0AudioProcessor::getCurrentProgram()
{
    return 0;
}

void PFMProject0AudioProcessor::setCurrentProgram (int index)
{
}

const juce::String PFMProject0AudioProcessor::getProgramName (int index)
{
    return {};
}

void PFMProject0AudioProcessor::changeProgramName (int index, const juce::String& newName)
{
}

//==============================================================================
void PFMProject0AudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    // Use this method as the place to do any pre-playback
    // initialisation that you need..
    leftBufferAnalyzer.prepare(sampleRate, samplesPerBlock);
    rightBufferAnalyzer.prepare(sampleRate, samplesPerBlock);
}

void PFMProject0AudioProcessor::releaseResources()
{
    // When playback stops, you can use this as an opportunity to free up any
    // spare memory, etc.
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool PFMProject0AudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
  #if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
    return true;
  #else
    // This is the place where you check if the layout is supported.
    // In this template code we only support mono or stereo.
    // Some plugin hosts, such as certain GarageBand versions, will only
    // load plugins that support stereo bus layouts.
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    // This checks if the input layout matches the output layout
   #if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
   #endif

    return true;
  #endif
}
#endif

void PFMProject0AudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    auto totalNumInputChannels  = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    // In case we have more outputs than inputs, this code clears any output
    // channels that didn't contain input data, (because these aren't
    // guaranteed to be empty - they may contain garbage).
    // This is here to avoid people getting screaming feedback
    // when they first compile a plugin, but obviously you don't need to keep
    // this code if your algorithm always overwrites all the output channels.
    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear (i, 0, buffer.getNumSamples());

    // This is the place where you'd normally do the guts of your plugin's
    // audio processing...
    // Make sure to reset the state if your inner loop is processing
    // the samples and the outer loop is handling the channels.
    // Alternatively, you can process the samples with the channels
    // interleaved by keeping the same state.


    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            if (playSound->get())
            {
                buffer.setSample(channel, i, r.nextFloat());
            }
            else
            {
                buffer.setSample(channel, i, 0);
            }
        }
    }

    juce::dsp::AudioBlock<float> block(buffer);
    auto left = block.getSingleChannelBlock(0);
    leftBufferAnalyzer.cloneBuffer(left);

    if (buffer.getNumChannels() == 2)
    {
        auto right = block.getSingleChannelBlock(1);
        rightBufferAnalyzer.cloneBuffer(right);
    }

    buffer.clear();
}

//==============================================================================
bool PFMProject0AudioProcessor::hasEditor() const
{
    return true; // (change this to false if you choose to not supply an editor)
}

juce::AudioProcessorEditor* PFMProject0AudioProcessor::createEditor()
{
    return new PFMProject0AudioProcessorEditor (*this);
}

//==============================================================================
void PFMProject0AudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // You should use this method to store your parameters in the memory block.
    // You could do that either as raw data, or use the XML or ValueTree classes
    // as intermediaries to make it easy to save and load complex data.
    DBG(apvts.state.toXmlString());
    juce::MemoryOutputStream mos(destData, false);
    apvts.state.writeToStream(mos);
}

void PFMProject0AudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // You should use this method to restore your parameters from this memory block,
    // whose contents will have been created by the getStateInformation() call.
    juce::ValueTree tree = juce::ValueTree::readFromData(data, sizeInBytes);
    if (tree.isValid())
    {
        apvts.state = tree;
    }

    DBG(apvts.state.toXmlString());
}

void PFMProject0AudioProcessor::UpdateAutomatableParameter(juce::RangedAudioParameter* param, float value)
{
    param->beginChangeGesture();
    param->setValueNotifyingHost(value);
    param->endChangeGesture();
}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PFMProject0AudioProcessor();
}
