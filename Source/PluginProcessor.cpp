/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
void VariableSizedBuffer::clone(const juce::dsp::AudioBlock<float>& other)
{
    clear(other);

    buffer.copyFrom(0,0, other.getChannelPointer(0), (int)other.getNumSamples());
    numSamples = other.getNumSamples();
}
void VariableSizedBuffer::clone(const juce::AudioBuffer<float>& other)
{
    clear(other);

    buffer.copyFrom(0, 0, other, 0, 0, other.getNumSamples());
    numSamples = other.getNumSamples();
}
void VariableSizedBuffer::clone(const VariableSizedBuffer& other)
{
    clear(other);

    buffer.copyFrom(0, 0, other.buffer, 0, 0, (int) other.numSamples);

    numSamples = other.numSamples;
}
//==============================================================================
void BufferAnalyzer::prepare(double sampleRate, int samplesPerBlock)
{
    vsbFifo.prepare(samplesPerBlock);
    fftCopyThread.prepare(samplesPerBlock);
}
void BufferAnalyzer::cloneBuffer(const juce::dsp::AudioBlock<float>& other)
{
    if (vsbFifo.push(other))
    {
        fftCopyThread.notify();
    }
}
void BufferAnalyzer::timerCallback()
{
    if (pathFifo.pull(fftCurve))
    {
        //auto tx = fftCurve.getTransformToScaleToFit(getLocalBounds().toFloat(), false);
        //fftCurve.applyTransform(tx);
        auto pathBounds = fftCurve.getBounds();

        fftCurve.applyTransform(juce::AffineTransform().scale( float(getWidth()) / pathBounds.getWidth(), getHeight() ));
        repaint();
    }
}
void BufferAnalyzer::paint(juce::Graphics& g)
{

    //g.fillAll(juce::Colours::black);
    //g.setColour(juce::Colours::white);

    juce::ColourGradient cg;

    auto colours = std::vector<juce::Colour>
    {
        juce::Colours::violet,
        juce::Colours::blue,
        juce::Colours::green,
        juce::Colours::yellow,
        juce::Colours::orange,
        juce::Colours::red,
        juce::Colours::white
    };

    for (int i = 0; i < colours.size(); ++i)
    {
        cg.addColour(double(i) / double(colours.size() - 1), colours[i]);
    }

    cg.point1 = { 0, (float)getHeight() };
    cg.point2 = { 0, 0 };

    g.setGradientFill(cg);
    g.strokePath(fftCurve, juce::PathStrokeType(1));
}
//==============================================================================
FFTProcessingThread::FFTProcessingThread(FFTDataFfio& fifo, PathFifo& pf) :
    Thread("FFTProcessingThread"), fftDataFifo(fifo), pathFifo(pf)
{
    startThread();
}
FFTProcessingThread::~FFTProcessingThread()
{
    notify();
    stopThread(100);
}
void FFTProcessingThread::run()
{
    while (true)
    {
        wait(-1);

        if (threadShouldExit())
            break;

        if (fftDataFifo.pull(fftData))
        {
            // first apply a windowing function to our data
            window.multiplyWithWindowingTable(fftData.data(), FFTSizes::fftSize);       // [1]

            // then render our FFT data..
            forwardFFT.performFrequencyOnlyForwardTransform(fftData.data());  // [2]

            auto mindB = -100.0f;
            auto maxdB = 0.0f;

            if (threadShouldExit())
                return;

            for (int i = 0; i < FFTSizes::numPoints; ++i)                         // [3]
            {
                auto skewedProportionX = 1.0f - std::exp(std::log(1.0f - (float)i / (float)FFTSizes::numPoints) * 0.2f);
                auto fftDataIndex = juce::jlimit(0, FFTSizes::fftSize / 2, (int)(skewedProportionX * (float)FFTSizes::fftSize * 0.5f));
                auto level = juce::jmap(juce::jlimit(mindB, maxdB, juce::Decibels::gainToDecibels(fftData[fftDataIndex])
                    - juce::Decibels::gainToDecibels((float)FFTSizes::fftSize)),
                    mindB, maxdB, 0.0f, 1.0f);

                curveData[i] = level;                                   // [4]
            }
            if (threadShouldExit())
                break;
            //Make path


            juce::Path fftCurve;

            fftCurve.startNewSubPath(0, 0.5f);

            for (int i = 4; i < FFTSizes::numPoints; ++i)
            {
                fftCurve.lineTo(float(i), juce::jmap(curveData[i], 0.f, 1.f, 1.f, 0.f));

            }

            if (threadShouldExit())
                break;

            pathFifo.push(fftCurve);

        }
    }
}
//==============================================================================
FFTCopyThread::FFTCopyThread(VariableSizedBufferFifo& vsb, PathFifo& pf) : Thread("FFTCopyThread"),
vsbFifo(vsb), fftProcessingThread(fftDataFifo, pf)
{

}

FFTCopyThread::~FFTCopyThread()
{
    notify();
    stopThread(100);
}

void FFTCopyThread::run()
{
    while (true)
    {
        wait(-1);

        if (threadShouldExit())
            break;

        if (vsbFifo.pull(buffer))
        {
            auto* ptr = buffer.getBuffer().getReadPointer(0);
            auto num = buffer.getNumSamples();

            if (threadShouldExit())
                return;

            for (size_t i = 0; i < num; ++i)
            {
                if (fifoIndex == FFTSizes::fftSize)
                {
                    if (threadShouldExit())
                        return;

                    fftData.fill({});
                    juce::FloatVectorOperations::copy(fftData.data(), fifoBuffer.data(), (int)fifoBuffer.size());

                    fftDataFifo.push(fftData);
                    fftProcessingThread.notify();

                    fifoIndex = 0;
                }
                fifoBuffer[fifoIndex++] = *(ptr + i);
            }
        }
    }
}

void FFTCopyThread::prepare(int samples)
{
    buffer.prepare(samples);
    startThread();
}
//==============================================================================
BufferAnalyzer2::BufferAnalyzer2() : Thread("BufferAnalyzer")
{
    startThread();
    startTimerHz(20);

    fftCurve.preallocateSpace(3 * FFTSizes::numPoints);
}

BufferAnalyzer2::~BufferAnalyzer2()
{
    notify();
    stopThread(100);
}
void BufferAnalyzer2::prepare(double sampleRate, int samplesPerBlock)
{
    firstBuffer = true;
    buffers[0].setSize(1, samplesPerBlock);
    buffers[1].setSize(1, samplesPerBlock);

    samplesCopied[0].set(0);
    samplesCopied[1].set(0);


    fifoIndex = 0;
    juce::zeromem(fifoBuffer, sizeof(fifoBuffer));
    juce::zeromem(fftData, sizeof(fftData));
    juce::zeromem(curveData, sizeof(curveData));

}
void BufferAnalyzer2::cloneBuffer(const juce::dsp::AudioBlock<float> &other)
{
    auto whichIndex = firstBuffer.get();
    auto index = whichIndex ? 0 : 1;
    firstBuffer.set(!whichIndex);

    jassert(other.getNumChannels() == buffers[index].getNumChannels());
    jassert(other.getNumSamples() <= buffers[index].getNumSamples());

    buffers[index].clear();

    juce::dsp::AudioBlock<float> buffer(buffers[index]);
    buffer.copyFrom(other);

    samplesCopied[index].set(other.getNumSamples());

    notify();
}

void BufferAnalyzer2::run()
{
    while(true)
    {
        wait(-1);

        DBG("BufferAnalyzer::run() awake!");

        if (threadShouldExit())
            break;

        auto index = !firstBuffer.get() ? 0 : 1;

        auto numSamples = samplesCopied[index].get();
        auto* readPtr = buffers[index].getReadPointer(0);
        for (int i = 0; i < numSamples; ++i)
        {
            //pushNextSampleIntoFifo(buffers[index].getSample(0, i));
            // get sample is slow use pointer
            pushNextSampleIntoFifo(*(readPtr + i));
        }
    }
}

void BufferAnalyzer2::pushNextSampleIntoFifo(float sample)
{
    if (fifoIndex == FFTSizes::fftSize)
    {
        if (nextFFTBlockReady.get() == false)
        {
            juce::zeromem(fftData, sizeof(fftData));
            memcpy(fftData, fifoBuffer, sizeof(fifoBuffer));
            nextFFTBlockReady.set(true);
        }
        fifoIndex = 0;
    }
    fifoBuffer[fifoIndex++] = sample;
}

void BufferAnalyzer2::timerCallback()
{
    if (nextFFTBlockReady.get())
    {
        drawNextFrameOfSpectrum();
        nextFFTBlockReady.set(false);
        repaint();
    }
}

void BufferAnalyzer2::drawNextFrameOfSpectrum()
{
    // first apply a windowing function to our data
    window.multiplyWithWindowingTable(fftData, FFTSizes::fftSize);       // [1]

    // then render our FFT data..
    forwardFFT.performFrequencyOnlyForwardTransform(fftData);  // [2]

    auto mindB = -100.0f;
    auto maxdB = 0.0f;

    for (int i = 0; i < FFTSizes::numPoints; ++i)                         // [3]
    {
        auto skewedProportionX = 1.0f - std::exp(std::log(1.0f - (float)i / (float)FFTSizes::numPoints) * 0.2f);
        auto fftDataIndex = juce::jlimit(0, FFTSizes::fftSize / 2, (int)(skewedProportionX * (float)FFTSizes::fftSize * 0.5f));
        auto level = juce::jmap(juce::jlimit(mindB, maxdB, juce::Decibels::gainToDecibels(fftData[fftDataIndex])
            - juce::Decibels::gainToDecibels((float)FFTSizes::fftSize)),
            mindB, maxdB, 0.0f, 1.0f);

        curveData[i] = level;                                   // [4]
    }
}

void BufferAnalyzer2::paint(juce::Graphics& g)
{
    float w = getWidth();
    float h = getHeight();

    fftCurve.clear();

    fftCurve.startNewSubPath(0, juce::jmap(curveData[0], 0.f, 1.f, h, 0.f) );

    for (int i = 1; i < FFTSizes::numPoints; ++i)
    {
        auto data = curveData[i];
        auto endX = juce::jmap((float)i, 0.f, float(FFTSizes::numPoints), 0.f, w);
        auto endY = juce::jmap(data, 0.f, 1.f, h, 0.f);

        fftCurve.lineTo(endX, endY);
    }

    g.fillAll(juce::Colours::black);
    //g.setColour(juce::Colours::white);

    juce::ColourGradient cg;

    auto colours = std::vector<juce::Colour>
    {
        juce::Colours::violet,
        juce::Colours::blue,
        juce::Colours::green,
        juce::Colours::yellow,
        juce::Colours::orange,
        juce::Colours::red,
        juce::Colours::white
    };

    for (int i = 0; i < colours.size(); ++i)
    {
        cg.addColour(double(i) / double(colours.size()-1), colours[i]);
    }

    cg.point1 = { 0, h };
    cg.point2 = { 0, 0 };

    g.setGradientFill(cg);
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
