/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/
/*
TODO
click on window
Drag to change pitch
Should we play a sound
*/
#pragma once

#include <JuceHeader.h>
#include <array>
//==============================================================================
template<typename T>
struct Fifo
{
    bool push(const T& itemToAdd)
    {
        auto write = fifo.write(1);
        if (write.blockSize1 >= 1)
        {
            buffer[write.startIndex1] = itemToAdd;
            return true;
        }
        return false;
    }
    bool pull(T& itemToUpdate)
    {
        auto read = fifo.read(1);
        if (read.blockSize1 >= 1)
        {
            itemToUpdate = buffer[read.startIndex1];
            return true;
        }
        return false;
    }
private:
    static constexpr int Capactiy = 5;
    std::array<T, Capactiy> buffer;
    juce::AbstractFifo fifo{ Capactiy };
};

//==============================================================================
struct VariableSizedBuffer 
{
    void prepare(int size)
    {
        buffer.setSize(1, size);
        buffer.clear();
        prepared = true;
    }
    void clone(const juce::dsp::AudioBlock<float>& other);
    void clone(const juce::AudioBuffer<float>& other);
    void clone(const VariableSizedBuffer& other);

    juce::AudioBuffer<float>& getBuffer() { return buffer; }
    size_t getNumSamples() const { return numSamples; }
private:
    juce::AudioBuffer<float> buffer; 
    size_t numSamples{ 0 };
    bool prepared = false;
    template<typename BufferType> 
    void clear(const BufferType& other)
    {
        jassert(prepared);
        jassert(other.getNumSamples() <= buffer.getNumSamples());
        buffer.clear();
    }

    void clear(const VariableSizedBuffer& other)
    {
        jassert(prepared);
        jassert(other.buffer.getNumSamples() <= buffer.getNumSamples());
        buffer.clear();
    }
};

struct VariableSizedBufferFifo
{
    void prepare(int samplesPerBlock)
    {
        for (auto& buffer : buffers)
            buffer.prepare(samplesPerBlock);
    }

    template<typename BlockType>
    bool push(const BlockType& blockToClone)
    {
        auto write = fifo.write(1);
        if (write.blockSize1 >= 1)
        {
            auto& buffer = buffers[write.startIndex1];
            buffer.clone(blockToClone);
            return true;
        }
        return false;
    }
    bool pull(VariableSizedBuffer& bufferToFill)
    {
        auto read = fifo.read(1);
        if (read.blockSize1 >= 1)
        {
            auto& buffer = buffers[read.startIndex1];
            bufferToFill.clone(buffer);
            return true;
        }
        return false;
    }
private:
    static constexpr int Capactiy = 5;
    std::array<VariableSizedBuffer, Capactiy> buffers;
    juce::AbstractFifo fifo{ Capactiy };
};
//============================================================================
enum FFTSizes
{
    fftOrder = 11,
    fftSize = 1 << fftOrder,
    numPoints = 512
};
//==============================================================================
struct FFTDataFfio
{
    using BufferType = std::array<float, 2 * FFTSizes::fftSize>;

    bool push(const BufferType& bufferToClone)
    {
        auto write = fifo.write(1);
        if (write.blockSize1 >= 1)
        {
            buffers[write.startIndex1] = bufferToClone;
            return true;
        }
        return false;
    }
    bool pull(BufferType& bufferToFill)
    {
        auto read = fifo.read(1);
        if (read.blockSize1 >= 1)
        {
            bufferToFill = buffers[read.startIndex1];
            return true;
        }
        return false;
    }

private:
    static constexpr int Capacity = 5;
    std::array<BufferType, Capacity> buffers;
    juce::AbstractFifo fifo{ Capacity };
};
//==============================================================================
struct PathFifo
{
    bool push(const juce::Path& pathToClone)
    {
        auto write = fifo.write(1);
        if (write.blockSize1 >= 1)
        {
            buffers[write.startIndex1] = pathToClone;
            return true;
        }
        return false;
    }
    bool pull(juce::Path& pathToFill)
    {
        auto read = fifo.read(1);
        if (read.blockSize1 >= 1)
        {
            pathToFill = buffers[read.startIndex1];
            return true;
        }
        return false;
    }

private:
    static constexpr int Capacity = 5;
    std::array<juce::Path, Capacity> buffers;
    juce::AbstractFifo fifo{ Capacity };
};
//==============================================================================
struct FFTProcessingThread : juce::Thread
{
    FFTProcessingThread(FFTDataFfio&, PathFifo&);
    ~FFTProcessingThread();
    void run() override;

private:
    FFTDataFfio::BufferType fftData;
    FFTDataFfio& fftDataFifo;

    std::array<float, FFTSizes::numPoints> curveData;

    juce::dsp::FFT forwardFFT{ FFTSizes::fftOrder };
    juce::dsp::WindowingFunction<float> window{ FFTSizes::fftSize, juce::dsp::WindowingFunction<float>::hann };

    PathFifo& pathFifo;
};
//==============================================================================
struct FFTCopyThread : juce::Thread
{

    FFTCopyThread(VariableSizedBufferFifo& vsb, PathFifo&);
    ~FFTCopyThread();
    void run() override;
    void prepare(int samplesPerBlock);
private:
    VariableSizedBufferFifo& vsbFifo;
    VariableSizedBuffer buffer;

    std::array<float, FFTSizes::fftSize> fifoBuffer;
    FFTDataFfio::BufferType fftData;
    int fifoIndex = 0;

    FFTDataFfio fftDataFifo;
    FFTProcessingThread fftProcessingThread;
};
//==============================================================================
struct BufferAnalyzer : juce::Component, juce::Timer
{
    BufferAnalyzer() { startTimerHz(20);  }
    ~BufferAnalyzer() { stopTimer();  }
    void prepare(double sampleRate, int samplesPerBlock);
    void cloneBuffer(const juce::dsp::AudioBlock<float>& other);
    void timerCallback() override;
    void paint(juce::Graphics& g) override;
private:
    juce::Path fftCurve;
    VariableSizedBufferFifo vsbFifo;
    PathFifo pathFifo;
    FFTCopyThread fftCopyThread{ vsbFifo, pathFifo };
};
//==============================================================================
struct BufferAnalyzer2 : juce::Thread, juce::Timer, juce::Component
{
    BufferAnalyzer2() ;
    ~BufferAnalyzer2() ;
    void prepare(double sampleRate, int samplesPerBlock);
    void cloneBuffer(const juce::dsp::AudioBlock<float>& other);
    void run() override;
    void timerCallback() override;
    void paint(juce::Graphics& g) override;
private:
    std::array<juce::AudioBuffer<float>, 2> buffers;
    juce::Atomic<bool> firstBuffer{ true };
    std::array<juce::Atomic<size_t>, 2> samplesCopied;


    float fifoBuffer [FFTSizes::fftSize];
    float fftData[2 * FFTSizes::fftSize];
    int fifoIndex = 0;

    void pushNextSampleIntoFifo(float);

    juce::Atomic<bool> nextFFTBlockReady{ false };
    float curveData[FFTSizes::numPoints];

    juce::dsp::FFT forwardFFT{ FFTSizes::fftOrder };
    juce::dsp::WindowingFunction<float> window{ FFTSizes::fftSize, juce::dsp::WindowingFunction<float>::hann};

    void drawNextFrameOfSpectrum();
    juce::Path fftCurve;
};
//==============================================================================
/**
*/
class PFMProject0AudioProcessor  : public juce::AudioProcessor
{
public:
    //==============================================================================
    PFMProject0AudioProcessor();
    ~PFMProject0AudioProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

   #ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
   #endif

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    //==============================================================================
    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    //==============================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioParameterBool* playSound = nullptr;
    juce::AudioParameterFloat* bgColor = nullptr;

    static void UpdateAutomatableParameter(juce::RangedAudioParameter*, float value);
    BufferAnalyzer leftBufferAnalyzer, rightBufferAnalyzer;
private:
    juce::AudioProcessorValueTreeState apvts;
    juce::Random r;

    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PFMProject0AudioProcessor)
};
 