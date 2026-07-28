/*
    ------------------------------------------------------------------

    This file is part of the Open Ephys GUI
    Copyright (C) 2024 Open Ephys

    ------------------------------------------------------------------

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

*/

#include <stdio.h>

#include "gtest/gtest.h"

#include "../LfpDisplayCanvas.h"
#include "../LfpDisplayEditor.h"
#include "../LfpDisplayNode.h"
#include <ModelApplication.h>
#include <ModelProcessors.h>
#include <ProcessorHeaders.h>
#include <TestFixtures.h>
#include <array>
#include <atomic>
#include <cstdlib>
#include <limits>
#include <thread>

#if JUCE_WINDOWS
#include <UIAutomation.h>
#include <wrl/client.h>
#endif

namespace
{
class LfpGuiRuntimeEnvironment final
    : public testing::Environment
{
public:
    void SetUp() override
    {
        MessageManager::deleteInstance();
        MessageManager::getInstance();
    }

    void TearDown() override
    {
        DeletedAtShutdown::deleteAll();
        MessageManager::deleteInstance();
    }
};

[[maybe_unused]]
testing::Environment* const
    lfpGuiRuntimeEnvironment =
        testing::
            AddGlobalTestEnvironment (
                new LfpGuiRuntimeEnvironment());

Component* findLfpDescendantById (
    Component& parent,
    StringRef id)
{
    if (parent.getComponentID()
        == id)
    {
        return &parent;
    }

    for (auto* child :
         parent.getChildren())
    {
        if (auto* result =
                findLfpDescendantById (
                    *child,
                    id))
        {
            return result;
        }
    }

    return nullptr;
}

template <typename ComponentType>
ComponentType* findLfpDescendant (
    Component& parent)
{
    if (auto* result =
            dynamic_cast<
                ComponentType*> (
                &parent))
    {
        return result;
    }

    for (auto* child :
         parent.getChildren())
    {
        if (auto* result =
                findLfpDescendant<
                    ComponentType> (
                    *child))
        {
            return result;
        }
    }

    return nullptr;
}

template <typename ComponentType>
ComponentType* findLfpAncestor (
    Component& component)
{
    for (auto* parent =
             component
                 .getParentComponent();
         parent != nullptr;
         parent =
             parent
                 ->getParentComponent())
    {
        if (auto* result =
                dynamic_cast<
                    ComponentType*> (
                    parent))
        {
            return result;
        }
    }

    return nullptr;
}

class LfpThreadTrackingButtonListener final
    : public Button::Listener
{
public:
    void buttonClicked (Button*) override
    {
        callbackCount.fetch_add (1);
        callbackUsedMessageThread.store (
            MessageManager::getInstance()
                ->isThisTheMessageThread());
    }

    std::atomic<int> callbackCount { 0 };
    std::atomic<bool>
        callbackUsedMessageThread { false };
};

class LfpThreadTrackingComboBoxListener final
    : public ComboBox::Listener
{
public:
    void comboBoxChanged (
        ComboBox*) override
    {
        callbackCount.fetch_add (1);
        callbackUsedMessageThread.store (
            MessageManager::getInstance()
                ->isThisTheMessageThread());
    }

    std::atomic<int> callbackCount { 0 };
    std::atomic<bool>
        callbackUsedMessageThread { false };
};

class LfpDestroyCanvasButtonListener final
    : public Button::Listener
{
public:
    explicit LfpDestroyCanvasButtonListener (
        std::unique_ptr<
            LfpViewer::
                LfpDisplayCanvas>&
            canvasToDestroy)
        : canvas (
              canvasToDestroy)
    {
    }

    void buttonClicked (Button*) override
    {
        canvas.reset();
    }

private:
    std::unique_ptr<
        LfpViewer::
            LfpDisplayCanvas>&
        canvas;
};

void joinLfpWorkerOrAbort (
    std::thread& worker,
    const std::atomic<bool>&
        workerReturned)
{
    if (! workerReturned.load())
    {
        ADD_FAILURE()
            << "LFP worker did not return before its message-loop watchdog expired";
        std::abort();
    }

    worker.join();
}

#if JUCE_WINDOWS
enum class LfpWindowsUiaAction
{
    invoke,
    toggle,
    queryToggle,
    select,
    querySelection,
    expand,
    collapse,
    queryExpansion,
    queryValue,
    setValue
};

struct LfpWindowsUiaInvokeResult
{
    HRESULT invokeResult = E_PENDING;
    HRESULT invokePatternResult =
        E_PENDING;
    HRESULT togglePatternResult =
        E_PENDING;
    HRESULT selectionItemPatternResult =
        E_PENDING;
    HRESULT expandCollapsePatternResult =
        E_PENDING;
    HRESULT valuePatternResult =
        E_PENDING;
    bool invokePatternAvailable = false;
    bool togglePatternAvailable = false;
    bool selectionItemPatternAvailable =
        false;
    bool expandCollapsePatternAvailable =
        false;
    bool valuePatternAvailable = false;
    BOOL valueReadOnly = FALSE;
    CONTROLTYPEID controlType = 0;
    BOOL enabled = FALSE;
    BOOL selected = FALSE;
    ToggleState toggleState =
        ToggleState_Indeterminate;
    ExpandCollapseState expansionState =
        ExpandCollapseState_LeafNode;
    std::wstring name;
    std::wstring help;
    std::wstring value;
};

class LfpScopedComApartment
{
public:
    LfpScopedComApartment()
        : result (
            CoInitializeEx (
                nullptr,
                COINIT_MULTITHREADED))
    {
    }

    ~LfpScopedComApartment()
    {
        if (SUCCEEDED (result))
            CoUninitialize();
    }

    HRESULT getResult() const
    {
        return result;
    }

private:
    const HRESULT result;
};

LfpWindowsUiaInvokeResult
invokeLfpWindowsUiaControl (
    HWND window,
    const std::wstring& automationId,
    LfpWindowsUiaAction action,
    const std::wstring& valueToSet = {})
{
    LfpWindowsUiaInvokeResult output;
    const LfpScopedComApartment
        comApartment;
    const auto comResult =
        comApartment.getResult();
    if (FAILED (comResult))
    {
        output.invokeResult =
            comResult;
        return output;
    }

    const auto finish =
        [&] (HRESULT result)
    {
        output.invokeResult = result;
        return output;
    };

    Microsoft::WRL::ComPtr<
        IUIAutomation>
        automation;
    auto result =
        CoCreateInstance (
            CLSID_CUIAutomation,
            nullptr,
            CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS (
                &automation));
    if (FAILED (result))
        return finish (result);

    Microsoft::WRL::ComPtr<
        IUIAutomationElement>
        rootElement;
    result =
        automation
            ->ElementFromHandle (
                window,
                &rootElement);
    if (FAILED (result)
        || rootElement == nullptr)
    {
        return finish (
            FAILED (result)
                ? result
                : E_FAIL);
    }

    VARIANT expectedId;
    VariantInit (&expectedId);
    expectedId.vt = VT_BSTR;
    expectedId.bstrVal =
        SysAllocString (
            automationId.c_str());
    if (expectedId.bstrVal
        == nullptr)
    {
        return finish (
            E_OUTOFMEMORY);
    }

    Microsoft::WRL::ComPtr<
        IUIAutomationCondition>
        idCondition;
    result =
        automation
            ->CreatePropertyCondition (
                UIA_AutomationIdPropertyId,
                expectedId,
                &idCondition);
    VariantClear (&expectedId);
    if (FAILED (result))
        return finish (result);

    Microsoft::WRL::ComPtr<
        IUIAutomationElement>
        element;
    result =
        rootElement
            ->FindFirst (
                TreeScope_Subtree,
                idCondition.Get(),
                &element);
    if (FAILED (result)
        || element == nullptr)
    {
        return finish (
            FAILED (result)
                ? result
                : E_FAIL);
    }

    element->get_CurrentControlType (
        &output.controlType);
    element->get_CurrentIsEnabled (
        &output.enabled);
    BSTR text = nullptr;
    if (SUCCEEDED (
            element->get_CurrentName (
                &text))
        && text != nullptr)
    {
        output.name = text;
    }
    SysFreeString (text);
    text = nullptr;
    if (SUCCEEDED (
            element->get_CurrentHelpText (
                &text))
        && text != nullptr)
    {
        output.help = text;
    }
    SysFreeString (text);

    Microsoft::WRL::ComPtr<
        IUIAutomationInvokePattern>
        invokePattern;
    output.invokePatternResult =
        element
            ->GetCurrentPatternAs (
                UIA_InvokePatternId,
                IID_PPV_ARGS (
                    &invokePattern));
    output.invokePatternAvailable =
        invokePattern != nullptr;

    Microsoft::WRL::ComPtr<
        IUIAutomationTogglePattern>
        togglePattern;
    output.togglePatternResult =
        element
            ->GetCurrentPatternAs (
                UIA_TogglePatternId,
                IID_PPV_ARGS (
                    &togglePattern));
    output.togglePatternAvailable =
        togglePattern != nullptr;
    if (togglePattern != nullptr)
    {
        togglePattern
            ->get_CurrentToggleState (
                &output.toggleState);
    }

    Microsoft::WRL::ComPtr<
        IUIAutomationSelectionItemPattern>
        selectionItemPattern;
    output.selectionItemPatternResult =
        element
            ->GetCurrentPatternAs (
                UIA_SelectionItemPatternId,
                IID_PPV_ARGS (
                    &selectionItemPattern));
    output.selectionItemPatternAvailable =
        selectionItemPattern != nullptr;

    Microsoft::WRL::ComPtr<
        IUIAutomationExpandCollapsePattern>
        expandCollapsePattern;
    output.expandCollapsePatternResult =
        element
            ->GetCurrentPatternAs (
                UIA_ExpandCollapsePatternId,
                IID_PPV_ARGS (
                    &expandCollapsePattern));
    output.expandCollapsePatternAvailable =
        expandCollapsePattern != nullptr;

    Microsoft::WRL::ComPtr<
        IUIAutomationValuePattern>
        valuePattern;
    output.valuePatternResult =
        element
            ->GetCurrentPatternAs (
                UIA_ValuePatternId,
                IID_PPV_ARGS (
                    &valuePattern));
    output.valuePatternAvailable =
        valuePattern != nullptr;
    if (valuePattern != nullptr)
    {
        valuePattern
            ->get_CurrentIsReadOnly (
                &output.valueReadOnly);
        BSTR value = nullptr;
        if (SUCCEEDED (
                valuePattern
                    ->get_CurrentValue (
                        &value))
            && value != nullptr)
        {
            output.value = value;
        }
        SysFreeString (value);
    }

    if (action
        == LfpWindowsUiaAction::
               queryToggle
        || action
               == LfpWindowsUiaAction::
                      toggle)
    {
        if (togglePattern == nullptr)
        {
            return finish (
                E_NOINTERFACE);
        }

        if (action
            == LfpWindowsUiaAction::
                   toggle)
        {
            result =
                togglePattern
                    ->Toggle();
        }

        if (SUCCEEDED (result))
        {
            result =
                togglePattern
                    ->get_CurrentToggleState (
                        &output.toggleState);
        }
        if (SUCCEEDED (result)
            && valuePattern != nullptr)
        {
            BSTR currentValue =
                nullptr;
            result =
                valuePattern
                    ->get_CurrentValue (
                        &currentValue);
            if (SUCCEEDED (result)
                && currentValue
                       != nullptr)
            {
                output.value =
                    currentValue;
            }
            SysFreeString (
                currentValue);
        }
        return finish (result);
    }

    if (action
        == LfpWindowsUiaAction::
               queryValue)
    {
        return finish (
            valuePattern != nullptr
                ? S_OK
                : E_NOINTERFACE);
    }

    if (action
        == LfpWindowsUiaAction::
               setValue)
    {
        if (valuePattern == nullptr)
        {
            return finish (
                E_NOINTERFACE);
        }

        auto input =
            SysAllocString (
                valueToSet.c_str());
        if (input == nullptr)
        {
            return finish (
                E_OUTOFMEMORY);
        }
        result =
            valuePattern
                ->SetValue (
                    input);
        SysFreeString (
            input);
        if (FAILED (result))
            return finish (result);

        BSTR currentValue =
            nullptr;
        result =
            valuePattern
                ->get_CurrentValue (
                    &currentValue);
        if (SUCCEEDED (result)
            && currentValue
                   != nullptr)
        {
            output.value =
                currentValue;
        }
        SysFreeString (
            currentValue);
        return finish (result);
    }

    if (action
        == LfpWindowsUiaAction::
               queryExpansion
        || action
               == LfpWindowsUiaAction::
                      expand
        || action
               == LfpWindowsUiaAction::
                      collapse)
    {
        if (expandCollapsePattern
            == nullptr)
        {
            return finish (
                E_NOINTERFACE);
        }

        if (action
            == LfpWindowsUiaAction::
                   expand)
        {
            result =
                expandCollapsePattern
                    ->Expand();
        }
        else if (action
                 == LfpWindowsUiaAction::
                        collapse)
        {
            result =
                expandCollapsePattern
                    ->Collapse();
        }

        if (SUCCEEDED (result))
        {
            result =
                expandCollapsePattern
                    ->get_CurrentExpandCollapseState (
                        &output
                             .expansionState);
        }
        return finish (result);
    }

    if (action
        == LfpWindowsUiaAction::
               querySelection)
    {
        if (selectionItemPattern
            == nullptr)
        {
            return finish (
                E_NOINTERFACE);
        }

        result =
            selectionItemPattern
                ->get_CurrentIsSelected (
                    &output.selected);
        return finish (result);
    }

    if (action
        == LfpWindowsUiaAction::
               select)
    {
        if (selectionItemPattern
            == nullptr)
        {
            return finish (
                E_NOINTERFACE);
        }

        result =
            selectionItemPattern
                ->Select();
        if (SUCCEEDED (result))
        {
            result =
                selectionItemPattern
                    ->get_CurrentIsSelected (
                        &output.selected);
        }
        return finish (result);
    }

    result =
        output.invokePatternResult;
    if (FAILED (result)
        || invokePattern == nullptr)
    {
        return finish (
            FAILED (result)
                ? result
                : E_NOINTERFACE);
    }

    return finish (
        invokePattern->Invoke());
}
#endif
} // namespace

/*
    Class that can hold buffers of sample data written to the canvas and produce an 'expected' result image
     
    Used to verify the LFP Canvas' ability to accurately display voltages
 */
class ExpectedImage
{
public:
    ExpectedImage (int channels, int samples) : numChannels (channels), numSamplesInView (samples), lastSampleWritten (0)
    {
        for (int c = 0; c < numChannels; c++)
        {
            Array<float> newBuffer;
            newBuffer.insertMultiple (0, 0, samples);
            buffers.add (newBuffer);
        }
    }

    /*Adds samples from an AudioBuffer to internal buffer. Should be same buffer passed to the LFP Canvas*/
    void addToBuffer (AudioBuffer<float> newSamples)
    {
        for (int channelIndex = 0; channelIndex < numChannels; channelIndex++)
        {
            const float* channelReadPointer = newSamples.getReadPointer (channelIndex);
            int currentSampleBufferIndex = lastSampleWritten;
            for (int sampleIndex = 0; sampleIndex < newSamples.getNumSamples(); sampleIndex++)
            {
                float val = channelReadPointer[sampleIndex];
                Array<float>& writeBuffer = buffers.getReference (channelIndex);
                writeBuffer.set (currentSampleBufferIndex++, val);
                if (currentSampleBufferIndex >= numSamplesInView)
                {
                    currentSampleBufferIndex = 0;
                }
            }
        }
        lastSampleWritten += newSamples.getNumSamples() % numSamplesInView;
    }

    /*Constructs the trace information from internal buffers into an Image object*/
    Image getImage (int width, int height, Array<Colour> channelColours, Colour backgroundColour, Colour midlineColour, int scaleFactor) const
    {
        Image expectedImage (Image::ARGB, width, height, true, SoftwareImageType());

        //Fill image with background colour
        Graphics g (expectedImage);
        g.fillAll (backgroundColour);
        int heightPerChannel = height / numChannels;
        float samplesPerPixel = (float) numSamplesInView / (float) width;

        //Draw Midline
        for (int channelIndex = 0; channelIndex < numChannels; channelIndex += 1)
        {
            for (int x = 0; x < width; x++)
            {
                expectedImage.setPixelAt (x, channelIndex * heightPerChannel + heightPerChannel / 2, midlineColour);
            }
        }

        //Draw trace data
        for (int channelIndex = 0; channelIndex < numChannels; channelIndex++)
        {
            int lastPixelY = 0;
            for (int xWritePixel = 0; xWritePixel < width; xWritePixel++)
            {
                int avg = 0;
                int startSampleIndex = std::ceil (xWritePixel * samplesPerPixel);
                int endSampleIndex = (std::ceil ((xWritePixel + 1) * samplesPerPixel) - 1);
                for (int i = startSampleIndex; i < endSampleIndex; i++)
                {
                    avg += buffers[channelIndex][i];
                }

                avg /= (endSampleIndex - startSampleIndex);

                int amplitude = float (avg) / float (scaleFactor) * ((height / numChannels) / 2);
                int channelMidline = channelIndex * heightPerChannel + heightPerChannel / 2;
                int yWritePixel = channelMidline - amplitude;
                if (yWritePixel >= 0 && yWritePixel < height)
                {
                    expectedImage.setPixelAt (xWritePixel, yWritePixel, channelColours[channelIndex]);
                    if (xWritePixel != 0)
                    {
                        //Need to fill in gaps if  difference between y pixels
                        int currentY = lastPixelY;
                        while (currentY != yWritePixel)
                        {
                            expectedImage.setPixelAt (xWritePixel, currentY, channelColours[channelIndex]);
                            currentY = yWritePixel > currentY ? currentY + 1 : currentY - 1;
                        }
                    }
                    lastPixelY = yWritePixel;
                }
            }
        }

        //Draw playback line
        int playbackXPixel = int (std::ceil (float (lastSampleWritten) / float (samplesPerPixel))) % width + 1;
        for (int playbackYPixel = 0; playbackYPixel < height; playbackYPixel += 2)
        {
            expectedImage.setPixelAt (playbackXPixel, playbackYPixel + 1, Colours::yellow);
        }

        return expectedImage;
    }

    int lastSampleWritten;

    int numSamplesInView;
    int numChannels;
    Array<Array<float>> buffers;

    int height;
    int width;
};

class LfpDisplayNodeTests : public testing::Test
{
protected:
    void SetUp() override
    {
        numChannels = 16;
        tester =
            std::make_unique<
                ProcessorTester> (
                TestSourceNodeBuilder (
                    FakeSourceNodeParams {
                        numChannels,
                        sampleRate,
                        bitVolts }),
                TestGuiRuntimeLifetime::
                    process);

        processor = tester->createProcessor<LfpViewer::LfpDisplayNode> (Plugin::Processor::SINK);

        width = 0;
        height = 0;
        x = 0;
        y = 0;

        midlineColour = Colour (50, 50, 50);
        playheadColour = Colours::yellow;
    }

    /*Create a new AudioBuffer filled with step data*/
    AudioBuffer<float> createBuffer (float starting_value, float step, int numChannels, int numSamples)
    {
        AudioBuffer<float> inputBuffer (numChannels, numSamples);

        // in microvolts
        float currValue = starting_value;
        for (int chidx = 0; chidx < numChannels; chidx++)
        {
            for (int sampleIdx = 0; sampleIdx < numSamples; sampleIdx++)
            {
                inputBuffer.setSample (chidx, sampleIdx, currValue);
                currValue += step;
            }
        }
        return inputBuffer;
    }

    /*Creates a new AudioBuffer filled with sinusoidal waves*/
    AudioBuffer<float> createBufferSinusoidal (int cycles, int numChannels, int numSamples, int amplitude)
    {
        AudioBuffer<float> inputBuffer (numChannels, numSamples);
        float pi = 3.1415;
        // in microvolts
        for (int chidx = 0; chidx < numChannels; chidx++)
        {
            for (int sampleIdx = 0; sampleIdx < numSamples; sampleIdx++)
            {
                float value = amplitude * sin (float (sampleIdx) / (float (numSamples) / float (cycles)) * 2 * pi);
                inputBuffer.setSample (chidx, sampleIdx, value);
            }
        }
        return inputBuffer;
    }

    /*Writes data from an AudioBuffer to processor. Verifies that the AudioBuffer is not changed*/
    void writeBlock (AudioBuffer<float>& inputBuffer)
    {
        auto outputBuffer = tester->processBlock (processor, inputBuffer);
        // Assert the buffer hasn't changed after process()
        ASSERT_EQ (inputBuffer.getNumSamples(), outputBuffer.getNumSamples());
        ASSERT_EQ (inputBuffer.getNumChannels(), outputBuffer.getNumChannels());
        for (int chidx = 0; chidx < inputBuffer.getNumChannels(); chidx++)
        {
            for (int sampleIdx = 0; sampleIdx < inputBuffer.getNumSamples(); ++sampleIdx)
            {
                ASSERT_EQ (inputBuffer.getSample (chidx, sampleIdx), outputBuffer.getSample (chidx, sampleIdx));
            }
        }
        currentSampleIndex += inputBuffer.getNumSamples();
    }

    /*Gets information from canvas needed to build an ExpectedImage*/
    void setExpectedImageParameters (LfpViewer::LfpDisplayCanvas* canvas)
    {
        canvas->getChannelBitmapBounds (0, x, y, width, height);
        canvas->getChannelColours (0, channelColours, backgroundColour);
        ASSERT_EQ (channelColours.size(), numChannels);
    }

    /*Compares 2 Image objects and returns the number of different pixels*/
    int getImageDifferencePixelCount (Image expectedImage, Image actualImage) const
    {
        int missCount = 0;
        Image diffImage (Image::ARGB, width, height, true);
        for (int y = 0; y < height; y++)
        {
            for (int x = 0; x < width; x++)
            {
                Colour expectedPixel = expectedImage.getPixelAt (x, y);
                Colour actualPixel = actualImage.getPixelAt (x, y);
                if (expectedPixel != actualPixel)
                {
                    //Allow some variance - if expectedPixel is a voltage Pixel then see if actualPixel +- 1 pixel is also a voltage Pixel.
                    //If true then don't count as a miss
                    if (expectedPixel != backgroundColour && expectedPixel != midlineColour && expectedPixel != playheadColour)
                    {
                        Colour postiveVariance = actualImage.getPixelAt (x, y + 1);
                        Colour negativeVariance = actualImage.getPixelAt (x, y - 1);
                        if (postiveVariance == expectedPixel || negativeVariance == expectedPixel)
                        {
                            continue;
                        }
                    }
                    //Same for if actualPixel is a voltagePixel and expectedPixel isn't
                    if (actualPixel != backgroundColour && actualPixel != midlineColour && actualPixel != playheadColour)
                    {
                        Colour postiveVariance = expectedImage.getPixelAt (x, y + 1);
                        Colour negativeVariance = expectedImage.getPixelAt (x, y - 1);
                        if (postiveVariance == actualPixel || negativeVariance == actualPixel)
                        {
                            continue;
                        }
                    }
                    missCount += 1;
                }
            }
        }
        return missCount;
    }

    void dumpPng (String path, Image image)
    {
        FileOutputStream stream ((File (path)));
        PNGImageFormat pngWriter;
        pngWriter.writeImageToStream (image, stream);
    }

    LfpViewer::LfpDisplayEditor*
    createLayoutEditor()
    {
        processor->setHeadlessMode (
            false);
        processor->setProcessorType (
            Plugin::Processor::
                SPLITTER);
        auto* editor =
            static_cast<
                LfpViewer::
                    LfpDisplayEditor*> (
                processor
                    ->createEditor());
        processor->setProcessorType (
            Plugin::Processor::SINK);
        if (editor != nullptr)
        {
            editor->addToDesktop (0);
            editor->setVisible (true);
        }
        return editor;
    }

    LfpViewer::LfpDisplayNode* processor;

    int numChannels;
    float bitVolts = 1.0;
    std::unique_ptr<ProcessorTester> tester;
    int64_t currentSampleIndex = 0;
    float sampleRate = 2000;

    Image expectedImage;
    Colour midlineColour;
    Colour backgroundColour;
    Colour playheadColour;

    Array<Colour> channelColours;

    int width;
    int height;
    int x;
    int y;
};

TEST_F (LfpDisplayNodeTests,
        ExposesLayoutAndSyncControls)
{
    auto* editor =
        createLayoutEditor();
    ASSERT_NE (editor, nullptr);

    const auto prefix =
        "oe.processor."
        + String (
            processor->getNodeId())
        + ".lfp";
    struct ExpectedControl
    {
        String id;
        String title;
        String description;
        AccessibilityRole role;
        bool checked;
        bool enabled;
    };
    const std::array<
        ExpectedControl,
        6>
        expected {
            ExpectedControl {
                prefix
                    + ".layout.single",
                "Single display",
                "Show one LFP display pane.",
                AccessibilityRole::
                    radioButton,
                true,
                true },
            ExpectedControl {
                prefix
                    + ".layout.two_vertical",
                "Two vertical displays",
                "Show two LFP display panes side by side.",
                AccessibilityRole::
                    radioButton,
                false,
                true },
            ExpectedControl {
                prefix
                    + ".layout.three_vertical",
                "Three vertical displays",
                "Show three LFP display panes side by side.",
                AccessibilityRole::
                    radioButton,
                false,
                true },
            ExpectedControl {
                prefix
                    + ".layout.two_horizontal",
                "Two horizontal displays",
                "Show two stacked LFP display panes.",
                AccessibilityRole::
                    radioButton,
                false,
                true },
            ExpectedControl {
                prefix
                    + ".layout.three_horizontal",
                "Three horizontal displays",
                "Show three stacked LFP display panes.",
                AccessibilityRole::
                    radioButton,
                false,
                true },
            ExpectedControl {
                prefix
                    + ".sync_displays",
                "Sync displays",
                "Realign every LFP display pane with the latest data buffer position.",
                AccessibilityRole::button,
                false,
                false } };

    for (const auto& control :
         expected)
    {
        auto* component =
            findLfpDescendantById (
                *editor,
                control.id);
        ASSERT_NE (
            component,
            nullptr)
            << control.id;
        auto* handler =
            component
                ->getAccessibilityHandler();
        ASSERT_NE (handler, nullptr);
        EXPECT_EQ (
            handler->getTitle(),
            control.title);
        EXPECT_EQ (
            handler->getRole(),
            control.role);
        EXPECT_EQ (
            handler->getDescription(),
            control.description);
        EXPECT_EQ (
            handler->getHelp(),
            control.description);
        EXPECT_EQ (
            handler->isEnabled(),
            control.enabled);
        EXPECT_TRUE (
            handler->getActions()
                .contains (
                    AccessibilityActionType::
                        press));
        if (control.role
            == AccessibilityRole::
                   radioButton)
        {
            EXPECT_FALSE (
                handler->getActions()
                    .contains (
                        AccessibilityActionType::
                            toggle));
            EXPECT_EQ (
                handler
                    ->getCurrentState()
                    .isChecked(),
                control.checked);
        }
    }
}

TEST_F (LfpDisplayNodeTests,
        LayoutSelectionRunsFromWorker)
{
    auto* editor =
        createLayoutEditor();
    ASSERT_NE (editor, nullptr);

    const auto id =
        "oe.processor."
        + String (
            processor->getNodeId())
        + ".lfp.layout.two_vertical";
    auto* component =
        findLfpDescendantById (
            *editor,
            id);
    ASSERT_NE (component, nullptr);
    auto* button =
        dynamic_cast<Button*> (
            component);
    ASSERT_NE (button, nullptr);
    auto* handler =
        component
            ->getAccessibilityHandler();
    ASSERT_NE (handler, nullptr);
    const auto actions =
        handler->getActions();

    std::atomic<bool>
        workerReturned { false };
    bool invoked = false;
    std::thread worker (
        [&]
        {
            invoked =
                actions.invoke (
                    AccessibilityActionType::
                        press);
            workerReturned.store (
                true);
        });

    auto* messageManager =
        MessageManager::getInstance();
    for (int attempt = 0;
         attempt < 50
             && (! workerReturned.load()
                 || ! button
                           ->getToggleState());
         ++attempt)
    {
        messageManager
            ->runDispatchLoopUntil (10);
    }
    worker.join();

    EXPECT_TRUE (invoked);
    EXPECT_TRUE (
        workerReturned.load());
    EXPECT_TRUE (
        button->getToggleState());
    EXPECT_TRUE (
        handler
            ->getCurrentState()
            .isChecked());

    for (const auto* suffix :
         { "single",
           "two_vertical",
           "three_vertical",
           "two_horizontal",
           "three_horizontal" })
    {
        auto* layout =
            findLfpDescendantById (
                *editor,
                "oe.processor."
                    + String (
                        processor->getNodeId())
                    + ".lfp.layout."
                    + suffix);
        ASSERT_NE (layout, nullptr);
        EXPECT_EQ (
            layout
                ->getAccessibilityHandler()
                ->getCurrentState()
                .isChecked(),
            String (suffix)
                == "two_vertical");
    }
}

TEST_F (LfpDisplayNodeTests,
        LayoutProvidersReadFromWorker)
{
    auto* editor =
        createLayoutEditor();
    ASSERT_NE (editor, nullptr);

    const auto id =
        "oe.processor."
        + String (
            processor->getNodeId())
        + ".lfp.layout.single";
    auto* component =
        findLfpDescendantById (
            *editor,
            id);
    ASSERT_NE (component, nullptr);
    auto* handler =
        component
            ->getAccessibilityHandler();
    ASSERT_NE (handler, nullptr);

    String title;
    String description;
    String help;
    bool enabled = false;
    bool checkable = false;
    bool checked = false;
    std::thread worker (
        [&]
        {
            title = handler->getTitle();
            description =
                handler->getDescription();
            help = handler->getHelp();
            enabled =
                handler->isEnabled();
            const auto state =
                handler->getCurrentState();
            checkable =
                state.isCheckable();
            checked =
                state.isChecked();
        });
    worker.join();

    EXPECT_EQ (
        title,
        "Single display");
    EXPECT_EQ (
        description,
        "Show one LFP display pane.");
    EXPECT_EQ (help, description);
    EXPECT_TRUE (enabled);
    EXPECT_FALSE (checkable);
    EXPECT_TRUE (checked);
}

#if JUCE_WINDOWS
TEST_F (LfpDisplayNodeTests,
        WindowsUiaWorkerInvokesAndSelectsLayoutRadioButton)
{
    auto* editor =
        createLayoutEditor();
    ASSERT_NE (editor, nullptr);
    editor->setBounds (
        0,
        0,
        195,
        120);
    editor->setAlwaysOnTop (
        true);
    editor->toFront (false);
    ASSERT_TRUE (
        editor->isShowing());

    const auto id =
        "oe.processor."
        + String (
            processor->getNodeId())
        + ".lfp.layout.two_vertical";
    auto* component =
        findLfpDescendantById (
            *editor,
            id);
    ASSERT_NE (component, nullptr);
    auto* button =
        dynamic_cast<Button*> (
            component);
    ASSERT_NE (button, nullptr);
    const std::wstring wideId (
        id.toWideCharPointer());
    const auto window =
        static_cast<HWND> (
            editor
                ->getWindowHandle());
    ASSERT_NE (window, nullptr);

    LfpWindowsUiaInvokeResult
        result;
    std::atomic<bool>
        workerReturned { false };
    std::thread worker (
        [&]
        {
            result =
                invokeLfpWindowsUiaControl (
                    window,
                    wideId,
                    LfpWindowsUiaAction::
                        invoke);
            workerReturned.store (
                true);
        });
    for (int attempt = 0;
         attempt < 100
             && (! workerReturned.load()
                 || ! button
                           ->getToggleState());
         ++attempt)
    {
        MessageManager::getInstance()
            ->runDispatchLoopUntil (10);
    }
    worker.join();

    EXPECT_EQ (
        result.invokeResult,
        S_OK);
    EXPECT_NE (
        result.invokeResult,
        static_cast<HRESULT> (
            UIA_E_ELEMENTNOTAVAILABLE));
    EXPECT_FALSE (
        result
            .togglePatternAvailable);
    EXPECT_EQ (
        result
            .selectionItemPatternResult,
        S_OK);
    EXPECT_TRUE (
        result
            .selectionItemPatternAvailable);
    EXPECT_EQ (
        result.controlType,
        UIA_RadioButtonControlTypeId);
    EXPECT_EQ (
        result.enabled,
        TRUE);
    EXPECT_EQ (
        result.name,
        L"Two vertical displays");
    EXPECT_EQ (
        result.help,
        L"Show two LFP display panes side by side.");
    EXPECT_TRUE (
        button->getToggleState());

    const auto selectedId =
        "oe.processor."
        + String (
            processor->getNodeId())
        + ".lfp.layout.three_horizontal";
    auto* selectedButton =
        dynamic_cast<Button*> (
            findLfpDescendantById (
                *editor,
                selectedId));
    ASSERT_NE (
        selectedButton,
        nullptr);
    const std::wstring
        wideSelectedId (
            selectedId
                .toWideCharPointer());
    LfpWindowsUiaInvokeResult
        selectedResult;
    workerReturned.store (
        false);
    std::thread selectionWorker (
        [&]
        {
            selectedResult =
                invokeLfpWindowsUiaControl (
                    window,
                    wideSelectedId,
                    LfpWindowsUiaAction::
                        select);
            workerReturned.store (
                true);
        });
    for (int attempt = 0;
         attempt < 100
             && (! workerReturned.load()
                 || ! selectedButton
                           ->getToggleState());
         ++attempt)
    {
        MessageManager::getInstance()
            ->runDispatchLoopUntil (10);
    }
    selectionWorker.join();

    EXPECT_EQ (
        selectedResult
            .invokeResult,
        S_OK);
    EXPECT_TRUE (
        selectedButton
            ->getToggleState());
    EXPECT_FALSE (
        button->getToggleState());
    for (const auto* suffix :
         { "single",
           "two_vertical",
           "three_vertical",
           "two_horizontal",
           "three_horizontal" })
    {
        auto* layout =
            dynamic_cast<Button*> (
                findLfpDescendantById (
                    *editor,
                    "oe.processor."
                        + String (
                            processor
                                ->getNodeId())
                        + ".lfp.layout."
                        + suffix));
        ASSERT_NE (layout, nullptr);
        EXPECT_EQ (
            layout
                ->getToggleState(),
            String (suffix)
                == "three_horizontal");
    }

    LfpWindowsUiaInvokeResult
        queryResult;
    workerReturned.store (
        false);
    std::thread queryWorker (
        [&]
        {
            queryResult =
                invokeLfpWindowsUiaControl (
                    window,
                    wideSelectedId,
                    LfpWindowsUiaAction::
                        querySelection);
            workerReturned.store (
                true);
        });
    for (int attempt = 0;
         attempt < 100
             && ! workerReturned.load();
         ++attempt)
    {
        MessageManager::getInstance()
            ->runDispatchLoopUntil (10);
    }
    queryWorker.join();
    EXPECT_EQ (
        queryResult.invokeResult,
        S_OK);
    EXPECT_EQ (
        queryResult.selected,
        TRUE);
}
#endif

TEST_F (LfpDisplayNodeTests,
        QueuedLayoutPressHonoursLatestAvailability)
{
    auto* editor =
        createLayoutEditor();
    ASSERT_NE (editor, nullptr);

    const auto prefix =
        "oe.processor."
        + String (
            processor->getNodeId())
        + ".lfp.layout.";
    auto* component =
        findLfpDescendantById (
            *editor,
            prefix
                + "two_vertical");
    ASSERT_NE (component, nullptr);
    auto* button =
        dynamic_cast<Button*> (
            component);
    ASSERT_NE (button, nullptr);
    auto* handler =
        component
            ->getAccessibilityHandler();
    ASSERT_NE (handler, nullptr);
    const auto actions =
        handler->getActions();

    std::atomic<bool>
        workerReturned { false };
    bool invoked = false;
    std::thread worker (
        [&]
        {
            invoked =
                actions.invoke (
                    AccessibilityActionType::
                        press);
            workerReturned.store (
                true);
        });
    while (! workerReturned.load())
        std::this_thread::yield();
    worker.join();

    button->setEnabled (false);
    EXPECT_FALSE (
        handler->isEnabled());
    MessageManager::getInstance()
        ->runDispatchLoopUntil (30);
    EXPECT_TRUE (invoked);
    EXPECT_FALSE (
        button->getToggleState());

    invoked = false;
    workerReturned.store (
        false);
    std::thread disabledWorker (
        [&]
        {
            invoked =
                actions.invoke (
                    AccessibilityActionType::
                        press);
            workerReturned.store (
                true);
        });
    while (! workerReturned.load())
        std::this_thread::yield();
    disabledWorker.join();
    button->setEnabled (true);
    MessageManager::getInstance()
        ->runDispatchLoopUntil (30);
    EXPECT_TRUE (invoked);
    EXPECT_FALSE (
        button->getToggleState());

    EXPECT_TRUE (
        actions.invoke (
            AccessibilityActionType::
                press));
    for (int attempt = 0;
         attempt < 50
             && ! button
                     ->getToggleState();
         ++attempt)
    {
        MessageManager::getInstance()
            ->runDispatchLoopUntil (10);
    }
    EXPECT_TRUE (
        button->getToggleState());
}

TEST_F (LfpDisplayNodeTests,
        StaleLayoutActionDoesNotAffectReplacementEditor)
{
    auto* editor =
        createLayoutEditor();
    ASSERT_NE (editor, nullptr);

    const auto prefix =
        "oe.processor."
        + String (
            processor->getNodeId())
        + ".lfp.layout.";
    auto* oldControl =
        findLfpDescendantById (
            *editor,
            prefix
                + "two_vertical");
    ASSERT_NE (oldControl, nullptr);
    const auto staleActions =
        oldControl
            ->getAccessibilityHandler()
            ->getActions();

    auto* replacement =
        createLayoutEditor();
    ASSERT_NE (replacement, nullptr);
    auto* replacementSingle =
        dynamic_cast<Button*> (
            findLfpDescendantById (
                *replacement,
                prefix
                    + "single"));
    auto* replacementTwoVertical =
        dynamic_cast<Button*> (
            findLfpDescendantById (
                *replacement,
                prefix
                    + "two_vertical"));
    ASSERT_NE (
        replacementSingle,
        nullptr);
    ASSERT_NE (
        replacementTwoVertical,
        nullptr);

    std::atomic<bool>
        workerReturned { false };
    bool invoked = false;
    std::thread worker (
        [&]
        {
            invoked =
                staleActions.invoke (
                    AccessibilityActionType::
                        press);
            workerReturned.store (
                true);
        });
    for (int attempt = 0;
         attempt < 50
             && ! workerReturned.load();
         ++attempt)
    {
        MessageManager::getInstance()
            ->runDispatchLoopUntil (10);
    }
    worker.join();
    MessageManager::getInstance()
        ->runDispatchLoopUntil (30);

    EXPECT_TRUE (invoked);
    EXPECT_TRUE (
        workerReturned.load());
    EXPECT_TRUE (
        replacementSingle
            ->getToggleState());
    EXPECT_FALSE (
        replacementTwoVertical
            ->getToggleState());
}

TEST_F (LfpDisplayNodeTests,
        SyncDisplayRunsOnMessageThreadFromWorker)
{
    auto* editor =
        createLayoutEditor();
    ASSERT_NE (editor, nullptr);

    const auto id =
        "oe.processor."
        + String (
            processor->getNodeId())
        + ".lfp.sync_displays";
    auto* component =
        findLfpDescendantById (
            *editor,
            id);
    ASSERT_NE (component, nullptr);
    auto* button =
        dynamic_cast<Button*> (
            component);
    ASSERT_NE (button, nullptr);
    auto* handler =
        component
            ->getAccessibilityHandler();
    ASSERT_NE (handler, nullptr);
    EXPECT_FALSE (
        handler->isEnabled());

    LfpThreadTrackingButtonListener
        listener;
    button->addListener (&listener);
    const auto actions =
        handler->getActions();
    EXPECT_TRUE (
        actions.invoke (
            AccessibilityActionType::
                press));

    processor->setHeadlessMode (
        true);
    editor->canvas.reset (
        editor->createNewCanvas());
    ASSERT_NE (
        editor->canvas,
        nullptr);
    EXPECT_TRUE (
        handler->isEnabled());
    MessageManager::getInstance()
        ->runDispatchLoopUntil (30);
    EXPECT_EQ (
        listener
            .callbackCount
            .load(),
        0);

    std::atomic<bool>
        workerReturned { false };
    bool invoked = false;
    std::thread worker (
        [&]
        {
            invoked =
                actions.invoke (
                    AccessibilityActionType::
                        press);
            workerReturned.store (
                true);
        });
    for (int attempt = 0;
         attempt < 50
             && (! workerReturned.load()
                 || listener
                            .callbackCount
                            .load()
                        == 0);
         ++attempt)
    {
        MessageManager::getInstance()
            ->runDispatchLoopUntil (10);
    }
    worker.join();
    button->removeListener (
        &listener);

    EXPECT_TRUE (invoked);
    EXPECT_TRUE (
        workerReturned.load());
    EXPECT_EQ (
        listener
            .callbackCount
            .load(),
        1);
    EXPECT_TRUE (
        listener
            .callbackUsedMessageThread
            .load());

    editor->canvas.reset();
    processor->setHeadlessMode (
        false);
}

TEST_F (LfpDisplayNodeTests,
        ExposesStableStreamSelectorForEveryDisplay)
{
    auto canvas =
        std::make_unique<
            LfpViewer::
                LfpDisplayCanvas> (
            processor,
            LfpViewer::
                SplitLayouts::SINGLE,
            false);
    canvas->updateSettings();
    canvas->setSize (600, 800);
    canvas->addToDesktop (0);
    canvas->setVisible (true);

    const auto prefix =
        "oe.processor."
        + String (
            processor->getNodeId())
        + ".lfp.display_";
    const auto expectedValue =
        processor
            ->getDisplayBuffers()[0]
            ->name;
    std::array<Component*, 3>
        selectors {};

    for (int displayIndex = 0;
         displayIndex < 3;
         ++displayIndex)
    {
        const auto displayNumber =
            displayIndex + 1;
        const auto id =
            prefix
            + String (displayNumber)
            + ".stream";
        selectors[displayIndex] =
            findLfpDescendantById (
                *canvas,
                id);
        ASSERT_NE (
            selectors[displayIndex],
            nullptr)
            << id;
        auto* handler =
            selectors[displayIndex]
                ->getAccessibilityHandler();
        ASSERT_NE (handler, nullptr);
        EXPECT_EQ (
            handler->getRole(),
            AccessibilityRole::
                comboBox);
        EXPECT_EQ (
            handler->getTitle(),
            "LFP display "
                + String (
                    displayNumber)
                + " stream");
        const auto description =
            "Choose the data stream shown in LFP display "
            + String (
                displayNumber)
            + ".";
        EXPECT_EQ (
            handler->getDescription(),
            description);
        EXPECT_EQ (
            handler->getHelp(),
            description);
        ASSERT_NE (
            handler
                ->getValueInterface(),
            nullptr);
        EXPECT_EQ (
            handler
                ->getValueInterface()
                ->getCurrentValueAsString(),
            expectedValue);
        EXPECT_TRUE (
            handler->getActions()
                .contains (
                    AccessibilityActionType::
                        press));
        EXPECT_TRUE (
            handler->getActions()
                .contains (
                    AccessibilityActionType::
                        showMenu));
        EXPECT_EQ (
            selectors[displayIndex]
                ->getParentComponent()
                ->isVisible(),
            displayIndex == 0);
    }

    canvas->setLayout (
        LfpViewer::
            SplitLayouts::
                THREE_HORZ);
    for (int displayIndex = 0;
         displayIndex < 3;
         ++displayIndex)
    {
        EXPECT_TRUE (
            selectors[displayIndex]
                ->getParentComponent()
                ->isVisible());
        EXPECT_EQ (
            selectors[displayIndex]
                ->getComponentID(),
            prefix
                + String (
                    displayIndex + 1)
                + ".stream");
    }
}

TEST_F (LfpDisplayNodeTests,
        ExposesEditableDisplayParametersForEveryPane)
{
    auto canvas =
        std::make_unique<
            LfpViewer::
                LfpDisplayCanvas> (
            processor,
            LfpViewer::
                SplitLayouts::SINGLE,
            false);
    canvas->updateSettings();
    canvas->setSize (600, 800);
    canvas->addToDesktop (0);
    canvas->setVisible (true);

    struct ParameterExpectation
    {
        String suffix;
        String title;
        String description;
        String initialValue;
    };
    const std::array<
        ParameterExpectation,
        3>
        parameters {
            ParameterExpectation {
                "timebase",
                "timebase",
                "Choose the time span shown in LFP display ",
                "2.0" },
            ParameterExpectation {
                "channel_height",
                "channel height",
                "Choose the channel height used in LFP display ",
                "40" },
            ParameterExpectation {
                "voltage_range",
                "voltage range",
                "Choose the voltage range shown in LFP display ",
                "250" }
        };
    const auto prefix =
        "oe.processor."
        + String (
            processor->getNodeId())
        + ".lfp.display_";
    std::array<
        std::array<Component*, 3>,
        3>
        controls {};

    for (int displayIndex = 0;
         displayIndex < 3;
         ++displayIndex)
    {
        const auto displayNumber =
            displayIndex + 1;
        for (int parameterIndex = 0;
             parameterIndex
             < static_cast<int> (
                 parameters.size());
             ++parameterIndex)
        {
            const auto& expected =
                parameters[parameterIndex];
            const auto id =
                prefix
                + String (displayNumber)
                + "."
                + expected.suffix;
            auto* component =
                findLfpDescendantById (
                    *canvas,
                    id);
            controls[displayIndex]
                    [parameterIndex] =
                component;
            ASSERT_NE (
                component,
                nullptr)
                << id;
            auto* comboBox =
                dynamic_cast<
                    MessageThreadComboBox*> (
                    component);
            ASSERT_NE (
                comboBox,
                nullptr)
                << id;
            auto handler =
                comboBox
                    ->createAccessibilityHandler();
            ASSERT_NE (
                handler,
                nullptr);
            EXPECT_EQ (
                handler->getRole(),
                AccessibilityRole::
                    comboBox);
            EXPECT_EQ (
                handler->getTitle(),
                "LFP display "
                    + String (
                        displayNumber)
                    + " "
                    + expected.title);

            auto description =
                expected.description
                + String (
                    displayNumber);
            if (expected.suffix
                == "timebase")
            {
                description +=
                    ", in seconds.";
            }
            else if (expected.suffix
                     == "channel_height")
            {
                description +=
                    ", in pixels.";
            }
            else
            {
                description =
                    "Choose the DATA voltage range shown in LFP display "
                    + String (
                        displayNumber)
                    + ", in "
                    + String (
                        CharPointer_UTF8 (
                            "\xC2\xB5V"))
                    + ".";
            }
            EXPECT_EQ (
                handler
                    ->getDescription(),
                description);
            EXPECT_EQ (
                handler->getHelp(),
                description);
            auto* value =
                handler
                    ->getValueInterface();
            ASSERT_NE (
                value,
                nullptr);
            EXPECT_FALSE (
                value->isReadOnly());
            EXPECT_EQ (
                value
                    ->getCurrentValueAsString(),
                expected.initialValue);
            EXPECT_EQ (
                findLfpAncestor<
                    LfpViewer::
                        LfpDisplayOptions> (
                    *component)
                    ->isVisible(),
                displayIndex == 0);
        }
    }

    canvas->setLayout (
        LfpViewer::
            SplitLayouts::
                THREE_HORZ);
    for (int displayIndex = 0;
         displayIndex < 3;
         ++displayIndex)
    {
        for (int parameterIndex = 0;
             parameterIndex
             < static_cast<int> (
                 parameters.size());
             ++parameterIndex)
        {
            auto* control =
                controls[displayIndex]
                        [parameterIndex];
            EXPECT_EQ (
                findLfpAncestor<
                    LfpViewer::
                        LfpDisplayOptions> (
                    *control)
                    ->isVisible(),
                displayIndex == 0);
            EXPECT_EQ (
                control
                    ->getComponentID(),
                prefix
                    + String (
                        displayIndex + 1)
                    + "."
                    + parameters
                          [parameterIndex]
                              .suffix);
        }
    }
}

TEST_F (LfpDisplayNodeTests,
        ExposesPauseToggleForEveryPane)
{
    auto canvas =
        std::make_unique<
            LfpViewer::
                LfpDisplayCanvas> (
            processor,
            LfpViewer::
                SplitLayouts::SINGLE,
            false);
    canvas->updateSettings();
    canvas->setSize (600, 800);
    canvas->addToDesktop (0);
    canvas->setVisible (true);

    const auto prefix =
        "oe.processor."
        + String (
            processor->getNodeId())
        + ".lfp.display_";
    std::array<Component*, 3>
        pauseControls {};

    for (int displayIndex = 0;
         displayIndex < 3;
         ++displayIndex)
    {
        const auto displayNumber =
            displayIndex + 1;
        const auto id =
            prefix
            + String (displayNumber)
            + ".pause";
        auto* component =
            findLfpDescendantById (
                *canvas,
                id);
        pauseControls[displayIndex] =
            component;
        ASSERT_NE (
            component,
            nullptr)
            << id;
        auto* handler =
            component
                ->getAccessibilityHandler();
        ASSERT_NE (
            handler,
            nullptr);
        EXPECT_EQ (
            handler->getRole(),
            AccessibilityRole::
                toggleButton);
        EXPECT_EQ (
            handler->getTitle(),
            "LFP display "
                + String (
                    displayNumber)
                + " pause");
        const auto description =
            "Pause or resume live updates in LFP display "
            + String (
                displayNumber)
            + ". Acquisition and recording continue.";
        EXPECT_EQ (
            handler->getDescription(),
            description);
        EXPECT_EQ (
            handler->getHelp(),
            description);
        EXPECT_TRUE (
            handler->getCurrentState()
                .isCheckable());
        EXPECT_FALSE (
            handler->getCurrentState()
                .isChecked());
        EXPECT_TRUE (
            handler->getActions()
                .contains (
                    AccessibilityActionType::
                        toggle));
        EXPECT_FALSE (
            handler->getActions()
                .contains (
                    AccessibilityActionType::
                        press));
        auto* value =
            handler
                ->getValueInterface();
        ASSERT_NE (
            value,
            nullptr);
        EXPECT_TRUE (
            value->isReadOnly());
        EXPECT_EQ (
            value
                ->getCurrentValueAsString(),
            "Running");
        EXPECT_EQ (
            findLfpAncestor<
                LfpViewer::
                    LfpDisplayOptions> (
                *component)
                ->isVisible(),
            displayIndex == 0);
    }

    canvas->setLayout (
        LfpViewer::
            SplitLayouts::
                THREE_HORZ);
    for (int displayIndex = 0;
         displayIndex < 3;
         ++displayIndex)
    {
        ASSERT_NE (
            pauseControls[displayIndex],
            nullptr);
        EXPECT_EQ (
            pauseControls[displayIndex]
                ->getComponentID(),
            prefix
                + String (
                    displayIndex + 1)
                + ".pause");
    }
}

TEST_F (LfpDisplayNodeTests,
        ExposesEventOverlayLinesForEveryPane)
{
    auto canvas =
        std::make_unique<
            LfpViewer::
                LfpDisplayCanvas> (
            processor,
            LfpViewer::
                SplitLayouts::SINGLE,
            false);
    canvas->updateSettings();
    canvas->setSize (1200, 800);
    canvas->addToDesktop (0);
    canvas->setVisible (true);

    const auto prefix =
        "oe.processor."
        + String (
            processor->getNodeId())
        + ".lfp.display_";
    for (int displayIndex = 0;
         displayIndex < 3;
         ++displayIndex)
    {
        const auto displayNumber =
            displayIndex + 1;
        for (int lineIndex = 0;
             lineIndex < 8;
             ++lineIndex)
        {
            const auto lineNumber =
                lineIndex + 1;
            const auto id =
                prefix
                + String (displayNumber)
                + ".event_overlay.line_"
                + String (lineNumber);
            auto* button =
                dynamic_cast<Button*> (
                    findLfpDescendantById (
                        *canvas,
                        id));
            ASSERT_NE (
                button,
                nullptr)
                << id;
            auto* handler =
                button
                    ->getAccessibilityHandler();
            ASSERT_NE (
                handler,
                nullptr);
            const auto description =
                "Show or hide TTL line "
                + String (lineNumber)
                + " event markers in LFP display "
                + String (displayNumber)
                + ". This changes the display overlay only; acquisition and recording are unaffected.";
            EXPECT_EQ (
                handler->getRole(),
                AccessibilityRole::
                    toggleButton);
            EXPECT_EQ (
                handler->getTitle(),
                "LFP display "
                    + String (
                        displayNumber)
                    + " event overlay line "
                    + String (
                        lineNumber));
            EXPECT_EQ (
                handler
                    ->getDescription(),
                description);
            EXPECT_EQ (
                handler->getHelp(),
                description);
            EXPECT_TRUE (
                handler
                    ->getCurrentState()
                    .isCheckable());
            EXPECT_TRUE (
                handler
                    ->getCurrentState()
                    .isChecked());
            EXPECT_TRUE (
                handler->getActions()
                    .contains (
                        AccessibilityActionType::
                            toggle));
            EXPECT_FALSE (
                handler->getActions()
                    .contains (
                        AccessibilityActionType::
                            press));
            auto* value =
                handler
                    ->getValueInterface();
            ASSERT_NE (
                value,
                nullptr);
            EXPECT_TRUE (
                value->isReadOnly());
            EXPECT_EQ (
                value
                    ->getCurrentValueAsString(),
                "Shown");
            EXPECT_EQ (
                findLfpAncestor<
                    LfpViewer::
                        LfpDisplayOptions> (
                    *button)
                    ->isVisible(),
                displayIndex == 0);
        }
    }
}

TEST_F (LfpDisplayNodeTests,
        EventOverlayWorkerToggleIsPerPaneAndLifecycleSafe)
{
    auto canvas =
        std::make_unique<
            LfpViewer::
                LfpDisplayCanvas> (
            processor,
            LfpViewer::
                SplitLayouts::SINGLE,
            false);
    canvas->updateSettings();
    canvas->setSize (1200, 800);
    canvas->addToDesktop (0);
    canvas->setVisible (true);

    const auto prefix =
        "oe.processor."
        + String (
            processor->getNodeId())
        + ".lfp.display_";
    auto* line1 =
        dynamic_cast<Button*> (
            findLfpDescendantById (
                *canvas,
                prefix
                    + "1.event_overlay.line_1"));
    auto* line2 =
        dynamic_cast<Button*> (
            findLfpDescendantById (
                *canvas,
                prefix
                    + "1.event_overlay.line_2"));
    auto* otherPaneLine1 =
        dynamic_cast<Button*> (
            findLfpDescendantById (
                *canvas,
                prefix
                    + "2.event_overlay.line_1"));
    ASSERT_NE (
        line1,
        nullptr);
    ASSERT_NE (
        line2,
        nullptr);
    ASSERT_NE (
        otherPaneLine1,
        nullptr);
    auto* handler =
        line1
            ->getAccessibilityHandler();
    auto* line2Handler =
        line2
            ->getAccessibilityHandler();
    auto* otherPaneHandler =
        otherPaneLine1
            ->getAccessibilityHandler();
    ASSERT_NE (
        handler,
        nullptr);
    ASSERT_NE (
        line2Handler,
        nullptr);
    ASSERT_NE (
        otherPaneHandler,
        nullptr);
    auto* eventInterface =
        findLfpAncestor<
            LfpViewer::
                EventDisplayInterface> (
            *line1);
    ASSERT_NE (
        eventInterface,
        nullptr);

    const auto actions =
        handler->getActions();
    bool invoked = false;
    std::atomic<bool>
        workerReturned { false };
    std::thread worker (
        [&]
        {
            invoked =
                actions.invoke (
                    AccessibilityActionType::
                        toggle);
            workerReturned.store (
                true);
        });
    for (int attempt = 0;
         attempt < 100
             && ! workerReturned.load();
         ++attempt)
    {
        MessageManager::getInstance()
            ->runDispatchLoopUntil (
                10);
    }
    joinLfpWorkerOrAbort (
        worker,
        workerReturned);

    EXPECT_TRUE (
        invoked);
    EXPECT_FALSE (
        eventInterface
            ->getEventDisplayState());
    EXPECT_FALSE (
        handler
            ->getCurrentState()
            .isChecked());
    EXPECT_EQ (
        handler
            ->getValueInterface()
            ->getCurrentValueAsString(),
        "Hidden");
    EXPECT_TRUE (
        line2Handler
            ->getCurrentState()
            .isChecked());
    EXPECT_TRUE (
        otherPaneHandler
            ->getCurrentState()
            .isChecked());
    EXPECT_FALSE (
        line1->getToggleState());

    eventInterface
        ->setEventDisplayState (
            true);
    EXPECT_TRUE (
        handler
            ->getCurrentState()
            .isChecked());
    EXPECT_EQ (
        handler
            ->getValueInterface()
            ->getCurrentValueAsString(),
        "Shown");

    line1->triggerClick();
    for (int attempt = 0;
         attempt < 100
             && eventInterface
                    ->getEventDisplayState();
         ++attempt)
    {
        MessageManager::getInstance()
            ->runDispatchLoopUntil (
                10);
    }
    EXPECT_FALSE (
        eventInterface
            ->getEventDisplayState());
    EXPECT_FALSE (
        handler
            ->getCurrentState()
            .isChecked());

    line1->triggerClick();
    for (int attempt = 0;
         attempt < 100
             && ! eventInterface
                      ->getEventDisplayState();
         ++attempt)
    {
        MessageManager::getInstance()
            ->runDispatchLoopUntil (
                10);
    }
    EXPECT_TRUE (
        eventInterface
            ->getEventDisplayState());

    line1->setEnabled (
        false);
    EXPECT_FALSE (
        handler->isEnabled());
    EXPECT_TRUE (
        actions.invoke (
            AccessibilityActionType::
                toggle));
    EXPECT_TRUE (
        eventInterface
            ->getEventDisplayState());

    line1->setEnabled (
        true);
    canvas.reset();

    workerReturned.store (
        false);
    std::thread staleWorker (
        [&]
        {
            actions.invoke (
                AccessibilityActionType::
                    toggle);
            workerReturned.store (
                true);
        });
    for (int attempt = 0;
         attempt < 100
             && ! workerReturned.load();
         ++attempt)
    {
        MessageManager::getInstance()
            ->runDispatchLoopUntil (
                10);
    }
    joinLfpWorkerOrAbort (
        staleWorker,
        workerReturned);
}

TEST_F (LfpDisplayNodeTests,
        EventOverlayXmlMaskRoundTripsAndPreservesPaneIsolation)
{
    auto canvas =
        std::make_unique<
            LfpViewer::
                LfpDisplayCanvas> (
            processor,
            LfpViewer::
                SplitLayouts::SINGLE,
            false);
    canvas->updateSettings();
    canvas->setSize (1200, 800);
    canvas->addToDesktop (0);
    canvas->setVisible (true);

    const auto prefix =
        "oe.processor."
        + String (
            processor->getNodeId())
        + ".lfp.display_";
    auto* pane1Line1 =
        findLfpDescendantById (
            *canvas,
            prefix
                + "1.event_overlay.line_1");
    auto* pane2Line1 =
        findLfpDescendantById (
            *canvas,
            prefix
                + "2.event_overlay.line_1");
    ASSERT_NE (
        pane1Line1,
        nullptr);
    ASSERT_NE (
        pane2Line1,
        nullptr);
    auto* pane1Options =
        findLfpAncestor<
            LfpViewer::
                LfpDisplayOptions> (
            *pane1Line1);
    auto* pane2Options =
        findLfpAncestor<
            LfpViewer::
                LfpDisplayOptions> (
            *pane2Line1);
    ASSERT_NE (
        pane1Options,
        nullptr);
    ASSERT_NE (
        pane2Options,
        nullptr);
    EXPECT_EQ (
        pane1Options
            ->getEventOverlayMask(),
        0xff);
    EXPECT_EQ (
        pane2Options
            ->getEventOverlayMask(),
        0xff);

    for (const auto mask :
         { 0x00,
           0x01,
           0x55,
           0x80,
           0xff })
    {
        XmlElement xml (
            "LFPDISPLAY");
        xml.setAttribute (
            "EventButtonState",
            mask);
        pane1Options
            ->restoreEventOverlayState (
                xml);
        EXPECT_EQ (
            pane1Options
                ->getEventOverlayMask(),
            mask);
        EXPECT_EQ (
            pane2Options
                ->getEventOverlayMask(),
            0xff);
        for (int lineIndex = 0;
             lineIndex < 8;
             ++lineIndex)
        {
            auto* button =
                findLfpDescendantById (
                    *canvas,
                    prefix
                        + "1.event_overlay.line_"
                        + String (
                            lineIndex + 1));
            ASSERT_NE (
                button,
                nullptr);
            auto* handler =
                button
                    ->getAccessibilityHandler();
            ASSERT_NE (
                handler,
                nullptr);
            const auto expectedShown =
                ((mask >> lineIndex)
                 & 1)
                != 0;
            EXPECT_EQ (
                handler
                    ->getCurrentState()
                    .isChecked(),
                expectedShown);
            EXPECT_EQ (
                handler
                    ->getValueInterface()
                    ->getCurrentValueAsString(),
                expectedShown
                    ? "Shown"
                    : "Hidden");
        }

        XmlElement saved (
            "LFPDISPLAY");
        pane1Options
            ->saveEventOverlayState (
                saved);
        EXPECT_EQ (
            saved.getIntAttribute (
                "EventButtonState"),
            mask);
    }

    XmlElement missingAttribute (
        "LFPDISPLAY");
    pane1Options
        ->restoreEventOverlayState (
            missingAttribute);
    EXPECT_EQ (
        pane1Options
            ->getEventOverlayMask(),
        0x00);
    EXPECT_EQ (
        pane2Options
            ->getEventOverlayMask(),
        0xff);
}

TEST_F (LfpDisplayNodeTests,
        ExposesLatestNonzeroTtlWordForEveryPane)
{
    auto canvas =
        std::make_unique<
            LfpViewer::
                LfpDisplayCanvas> (
            processor,
            LfpViewer::
                SplitLayouts::SINGLE,
            false);
    canvas->updateSettings();
    canvas->setSize (1200, 800);
    canvas->addToDesktop (0);
    canvas->setVisible (true);

    const auto prefix =
        "oe.processor."
        + String (
            processor->getNodeId())
        + ".lfp.display_";
    for (int displayIndex = 0;
         displayIndex < 3;
         ++displayIndex)
    {
        const auto displayNumber =
            displayIndex + 1;
        const auto id =
            prefix
            + String (displayNumber)
            + ".ttl_word";
        auto* component =
            findLfpDescendantById (
                *canvas,
                id);
        ASSERT_NE (
            component,
            nullptr)
            << id;
        auto* handler =
            component
                ->getAccessibilityHandler();
        ASSERT_NE (
            handler,
            nullptr);
        const auto description =
            "Latest non-zero 64-bit TTL word received while its stream was selected in LFP display "
            + String (displayNumber)
            + ", shown as an unsigned decimal value. NONE means no non-zero word has been received by this display pane.";
        EXPECT_EQ (
            handler->getRole(),
            AccessibilityRole::
                staticText);
        EXPECT_EQ (
            handler->getTitle(),
            "LFP display "
                + String (
                    displayNumber)
                + " latest non-zero TTL word");
        EXPECT_EQ (
            handler
                ->getDescription(),
            description);
        EXPECT_EQ (
            handler->getHelp(),
            description);
        EXPECT_FALSE (
            handler->getActions()
                .contains (
                    AccessibilityActionType::
                        press));
        EXPECT_FALSE (
            handler->getActions()
                .contains (
                    AccessibilityActionType::
                        toggle));
        auto* value =
            handler
                ->getValueInterface();
        ASSERT_NE (
            value,
            nullptr);
        EXPECT_TRUE (
            value->isReadOnly());
        EXPECT_EQ (
            value
                ->getCurrentValueAsString(),
            "NONE");
        EXPECT_EQ (
            findLfpAncestor<
                LfpViewer::
                    LfpDisplayOptions> (
                *component)
                ->isVisible(),
            displayIndex == 0);
    }
}

TEST_F (LfpDisplayNodeTests,
        TtlWordPublishingIsThreadSafeAndPaneLocal)
{
    auto canvas =
        std::make_unique<
            LfpViewer::
                LfpDisplayCanvas> (
            processor,
            LfpViewer::
                SplitLayouts::SINGLE,
            false);
    canvas->updateSettings();
    canvas->setSize (1200, 800);
    canvas->addToDesktop (0);
    canvas->setVisible (true);

    const auto prefix =
        "oe.processor."
        + String (
            processor->getNodeId())
        + ".lfp.display_";
    auto* pane1Label =
        dynamic_cast<Label*> (
            findLfpDescendantById (
                *canvas,
                prefix
                    + "1.ttl_word"));
    auto* pane2Label =
        dynamic_cast<Label*> (
            findLfpDescendantById (
                *canvas,
                prefix
                    + "2.ttl_word"));
    ASSERT_NE (
        pane1Label,
        nullptr);
    ASSERT_NE (
        pane2Label,
        nullptr);
    auto* pane1Options =
        findLfpAncestor<
            LfpViewer::
                LfpDisplayOptions> (
            *pane1Label);
    ASSERT_NE (
        pane1Options,
        nullptr);
    auto* pane1Handler =
        pane1Label
            ->getAccessibilityHandler();
    auto* pane2Handler =
        pane2Label
            ->getAccessibilityHandler();
    ASSERT_NE (
        pane1Handler,
        nullptr);
    ASSERT_NE (
        pane2Handler,
        nullptr);
    auto* pane1Value =
        pane1Handler
            ->getValueInterface();
    ASSERT_NE (
        pane1Value,
        nullptr);

    std::atomic<bool>
        writerReturned { false };
    std::atomic<bool>
        readerReturned { false };
    std::atomic<bool>
        startWorkers { false };
    std::atomic<bool>
        publishingFinished { false };
    std::atomic<bool>
        readerSawInvalidValue {
            false
        };
    std::atomic<bool>
        readerSawPublishedValue {
            false
        };
    std::thread writer (
        [&]
        {
            while (! startWorkers.load())
                std::this_thread::yield();

            for (uint64 word = 1;
                 word <= 10000;
                 ++word)
            {
                pane1Options
                    ->setTTLWord (
                        word);
            }
            pane1Options
                ->setTTLWord (
                    0);
            writerReturned.store (
                true);
        });
    std::thread reader (
        [&]
        {
            while (! startWorkers.load())
                std::this_thread::yield();

            do
            {
                const auto value =
                    pane1Value
                        ->getCurrentValueAsString();
                if (value != "NONE")
                {
                    readerSawPublishedValue
                        .store (true);
                    if (! value
                             .containsOnly (
                                 "0123456789"))
                    {
                        readerSawInvalidValue
                            .store (
                                true);
                    }
                }
            }
            while (! publishingFinished
                          .load());
            readerReturned.store (
                true);
        });
    startWorkers.store (true);
    while (! writerReturned.load())
    {
        pane1Options
            ->timerCallback();
        std::this_thread::yield();
    }
    pane1Options
        ->timerCallback();
    publishingFinished.store (
        true);
    writer.join();
    reader.join();
    EXPECT_TRUE (
        writerReturned.load());
    EXPECT_TRUE (
        readerReturned.load());
    EXPECT_FALSE (
        readerSawInvalidValue
            .load());
    EXPECT_TRUE (
        readerSawPublishedValue
            .load());
    EXPECT_EQ (
        pane1Label->getText(),
        "10000");
    EXPECT_EQ (
        pane1Value
            ->getCurrentValueAsString(),
        "10000");
    EXPECT_EQ (
        pane1Handler->getTitle(),
        "LFP display 1 latest non-zero TTL word");
    EXPECT_EQ (
        pane2Label->getText(),
        "NONE");
    EXPECT_EQ (
        pane2Handler
            ->getValueInterface()
            ->getCurrentValueAsString(),
        "NONE");

    pane1Options
        ->setTTLWord (
            0);
    pane1Options
        ->timerCallback();
    EXPECT_EQ (
        pane1Value
            ->getCurrentValueAsString(),
        "10000");

    pane1Options
        ->setTTLWord (
            std::numeric_limits<
                uint64>::max());
    pane1Options
        ->timerCallback();
    EXPECT_EQ (
        pane1Label->getText(),
        "18446744073709551615");
    EXPECT_EQ (
        pane1Value
            ->getCurrentValueAsString(),
        "18446744073709551615");
    EXPECT_EQ (
        pane1Handler->getTitle(),
        "LFP display 1 latest non-zero TTL word");
}

TEST_F (LfpDisplayNodeTests,
        TtlWordFlowsThroughSerializedEventsAndRemainsTransient)
{
    auto canvas =
        std::make_unique<
            LfpViewer::
                LfpDisplayCanvas> (
            processor,
            LfpViewer::
                SplitLayouts::SINGLE,
            false);
    canvas->updateSettings();
    canvas->setSize (
        1200,
        800);
    canvas->addToDesktop (
        0);
    canvas->setVisible (
        true);
    ASSERT_TRUE (
        canvas->isShowing());

    const auto prefix =
        "oe.processor."
        + String (
            processor->getNodeId())
        + ".lfp.display_";
    std::array<Label*, 3>
        labels {};
    std::array<
        LfpViewer::
            LfpDisplayOptions*,
        3>
        options {};
    std::array<
        LfpViewer::
            LfpDisplaySplitter*,
        3>
        splitters {};
    for (int paneIndex = 0;
         paneIndex < 3;
         ++paneIndex)
    {
        const auto paneNumber =
            paneIndex + 1;
        labels[paneIndex] =
            dynamic_cast<Label*> (
                findLfpDescendantById (
                    *canvas,
                    prefix
                        + String (
                            paneNumber)
                        + ".ttl_word"));
        ASSERT_NE (
            labels[paneIndex],
            nullptr);
        options[paneIndex] =
            findLfpAncestor<
                LfpViewer::
                    LfpDisplayOptions> (
                *labels[paneIndex]);
        ASSERT_NE (
            options[paneIndex],
            nullptr);

        auto* selector =
            findLfpDescendantById (
                *canvas,
                prefix
                    + String (
                        paneNumber)
                    + ".stream");
        ASSERT_NE (
            selector,
            nullptr);
        splitters[paneIndex] =
            findLfpAncestor<
                LfpViewer::
                    LfpDisplaySplitter> (
                *selector);
        ASSERT_NE (
            splitters[paneIndex],
            nullptr);
    }
    const auto streamId =
        processor
            ->getDataStreams()[0]
            ->getStreamId();
    const auto* sourceStream =
        tester
            ->getSourceNodeDataStream (
                streamId);
    ASSERT_NE (
        sourceStream,
        nullptr);
    const auto eventChannels =
        sourceStream
            ->getEventChannels();
    ASSERT_GE (
        eventChannels.size(),
        1);
    auto processEvent =
        [&] (TTLEventPtr event)
    {
        auto input =
            createBuffer (
                0.0f,
                1.0f,
                numChannels,
                8);
        tester->processBlock (
            processor,
            input,
            event.get());
        for (auto* paneOptions :
             options)
        {
            paneOptions
                ->timerCallback();
        }
    };

    splitters[1]
        ->selectedStreamId = 0;
    auto highBitEvent =
        TTLEvent::
            createTTLEvent (
                eventChannels[0],
                0,
                uint8 (63),
                true);
    processEvent (
        highBitEvent);

    EXPECT_EQ (
        labels[0]->getText(),
        "9223372036854775808");
    EXPECT_EQ (
        labels[1]->getText(),
        "NONE");
    EXPECT_EQ (
        labels[2]->getText(),
        "9223372036854775808");

    splitters[1]
        ->selectedStreamId =
        streamId;
    for (auto* paneOptions :
         options)
    {
        paneOptions
            ->timerCallback();
    }
    EXPECT_EQ (
        labels[1]->getText(),
        "NONE");

    XmlElement before (
        "ROOT");
    canvas
        ->saveCustomParametersToXml (
            &before);
    auto lowBitEvent =
        TTLEvent::
            createTTLEvent (
                eventChannels[0],
                8,
                uint8 (0),
                true);
    processEvent (
        lowBitEvent);
    for (auto* label :
         labels)
    {
        EXPECT_EQ (
            label->getText(),
            "9223372036854775809");
        auto* handler =
            label
                ->getAccessibilityHandler();
        ASSERT_NE (
            handler,
            nullptr);
        auto* value =
            handler
                ->getValueInterface();
        ASSERT_NE (
            value,
            nullptr);
        EXPECT_EQ (
            value
                ->getCurrentValueAsString(),
            "9223372036854775809");
    }

    auto clearHighBitEvent =
        TTLEvent::
            createTTLEvent (
                eventChannels[0],
                16,
                uint8 (63),
                false);
    processEvent (
        clearHighBitEvent);
    for (auto* label :
         labels)
    {
        EXPECT_EQ (
            label->getText(),
            "1");
    }

    auto zeroWordEvent =
        TTLEvent::
            createTTLEvent (
                eventChannels[0],
                24,
                uint8 (0),
                false);
    ASSERT_EQ (
        zeroWordEvent
            ->getWord(),
        0);
    processEvent (
        zeroWordEvent);
    for (auto* label :
         labels)
    {
        EXPECT_EQ (
            label->getText(),
            "1");
    }

    XmlElement after (
        "ROOT");
    canvas
        ->saveCustomParametersToXml (
            &after);
    EXPECT_TRUE (
        before
            .isEquivalentTo (
                &after,
                true));
}

TEST_F (LfpDisplayNodeTests,
        ExposesOptionsDrawerForEveryPane)
{
    auto canvas =
        std::make_unique<
            LfpViewer::
                LfpDisplayCanvas> (
            processor,
            LfpViewer::
                SplitLayouts::SINGLE,
            false);
    canvas->updateSettings();
    canvas->setSize (900, 800);
    canvas->addToDesktop (0);
    canvas->setVisible (true);

    const auto prefix =
        "oe.processor."
        + String (
            processor->getNodeId())
        + ".lfp.display_";
    for (int displayIndex = 0;
         displayIndex < 3;
         ++displayIndex)
    {
        const auto displayNumber =
            displayIndex + 1;
        const auto id =
            prefix
            + String (displayNumber)
            + ".options_drawer";
        auto* button =
            dynamic_cast<Button*> (
                findLfpDescendantById (
                    *canvas,
                    id));
        ASSERT_NE (
            button,
            nullptr)
            << id;
        auto* handler =
            button
                ->getAccessibilityHandler();
        ASSERT_NE (
            handler,
            nullptr);
        EXPECT_EQ (
            handler->getRole(),
            AccessibilityRole::
                button);
        EXPECT_EQ (
            handler->getTitle(),
            "LFP display "
                + String (
                    displayNumber)
                + " options drawer");
        const auto description =
            "Show or hide advanced options for all LFP displays. Opening the drawer reveals threshold, channel, signal-processing, and triggered-display controls.";
        EXPECT_EQ (
            handler
                ->getDescription(),
            description);
        EXPECT_EQ (
            handler->getHelp(),
            description);
        EXPECT_FALSE (
            button->getToggleState());
        EXPECT_FALSE (
            handler
                ->getCurrentState()
                .isCheckable());
        EXPECT_TRUE (
            handler
                ->getCurrentState()
                .isExpandable());
        EXPECT_TRUE (
            handler
                ->getCurrentState()
                .isCollapsed());
        auto* value =
            handler
                ->getValueInterface();
        ASSERT_NE (
            value,
            nullptr);
        EXPECT_TRUE (
            value->isReadOnly());
        EXPECT_EQ (
            value
                ->getCurrentValueAsString(),
            "Closed");
        for (const auto action :
             { AccessibilityActionType::
                   press,
               AccessibilityActionType::
                   expand,
               AccessibilityActionType::
                   collapse })
        {
            EXPECT_TRUE (
                handler->getActions()
                    .contains (
                        action));
        }
        EXPECT_FALSE (
            handler->getActions()
                .contains (
                    AccessibilityActionType::
                        toggle));
        EXPECT_EQ (
            findLfpAncestor<
                LfpViewer::
                    LfpDisplayOptions> (
                *button)
                ->isVisible(),
            displayIndex == 0);
    }

    canvas->setLayout (
        LfpViewer::
            SplitLayouts::
                THREE_HORZ);
    int visibleOptionsCount = 0;
    for (int displayNumber = 1;
         displayNumber <= 3;
         ++displayNumber)
    {
        auto* button =
            findLfpDescendantById (
                *canvas,
                prefix
                    + String (
                        displayNumber)
                    + ".options_drawer");
        ASSERT_NE (
            button,
            nullptr);
        if (
            findLfpAncestor<
                LfpViewer::
                    LfpDisplayOptions> (
                *button)
                ->isVisible())
        {
            ++visibleOptionsCount;
        }
    }
    EXPECT_EQ (
        visibleOptionsCount,
        1);
}

TEST_F (LfpDisplayNodeTests,
        OptionsDrawerWorkerActionsSynchroniseEveryPane)
{
    auto canvas =
        std::make_unique<
            LfpViewer::
                LfpDisplayCanvas> (
            processor,
            LfpViewer::
                SplitLayouts::SINGLE,
            false);
    canvas->updateSettings();
    canvas->setSize (900, 800);
    canvas->addToDesktop (0);
    canvas->setVisible (true);

    const auto prefix =
        "oe.processor."
        + String (
            processor->getNodeId())
        + ".lfp.display_";
    std::array<Button*, 3>
        buttons {};
    std::array<
        AccessibilityHandler*,
        3>
        handlers {};
    for (int displayIndex = 0;
         displayIndex < 3;
         ++displayIndex)
    {
        buttons[displayIndex] =
            dynamic_cast<Button*> (
                findLfpDescendantById (
                    *canvas,
                    prefix
                        + String (
                            displayIndex
                            + 1)
                        + ".options_drawer"));
        ASSERT_NE (
            buttons[displayIndex],
            nullptr);
        handlers[displayIndex] =
            buttons[displayIndex]
                ->getAccessibilityHandler();
        ASSERT_NE (
            handlers[displayIndex],
            nullptr);
    }

    LfpThreadTrackingButtonListener
        listener;
    buttons[0]->addListener (
        &listener);
    const auto invokeFromWorker =
        [&] (
            AccessibilityActionType
                action)
    {
        bool invoked = false;
        std::atomic<bool>
            workerReturned { false };
        std::thread worker (
            [&]
            {
                invoked =
                    handlers[0]
                        ->getActions()
                        .invoke (
                            action);
                workerReturned.store (
                    true);
            });
        for (int attempt = 0;
             attempt < 100
                 && ! workerReturned.load();
             ++attempt)
        {
            MessageManager::getInstance()
                ->runDispatchLoopUntil (
                    10);
        }
        joinLfpWorkerOrAbort (
            worker,
            workerReturned);
        EXPECT_TRUE (
            invoked);
    };

    invokeFromWorker (
        AccessibilityActionType::
            expand);
    EXPECT_TRUE (
        canvas
            ->optionsDrawerIsOpen);
    EXPECT_EQ (
        listener
            .callbackCount
            .load(),
        1);
    EXPECT_TRUE (
        listener
            .callbackUsedMessageThread
            .load());
    for (int displayIndex = 0;
         displayIndex < 3;
         ++displayIndex)
    {
        EXPECT_TRUE (
            buttons[displayIndex]
                ->getToggleState());
        EXPECT_TRUE (
            handlers[displayIndex]
                ->getCurrentState()
                .isExpanded());
        EXPECT_EQ (
            handlers[displayIndex]
                ->getValueInterface()
                ->getCurrentValueAsString(),
            "Open");
        EXPECT_EQ (
            findLfpAncestor<
                LfpViewer::
                    LfpDisplayOptions> (
                *buttons[
                    displayIndex])
                ->getHeight(),
            210);
        EXPECT_EQ (
            findLfpAncestor<
                LfpViewer::
                    LfpDisplayOptions> (
                *buttons[
                    displayIndex])
                ->getExtendedOptionsHeight(),
            150);
    }

    invokeFromWorker (
        AccessibilityActionType::
            expand);
    EXPECT_TRUE (
        canvas
            ->optionsDrawerIsOpen);
    EXPECT_EQ (
        listener
            .callbackCount
            .load(),
        1);

    invokeFromWorker (
        AccessibilityActionType::
            collapse);
    EXPECT_FALSE (
        canvas
            ->optionsDrawerIsOpen);
    EXPECT_EQ (
        listener
            .callbackCount
            .load(),
        2);
    for (int displayIndex = 0;
         displayIndex < 3;
         ++displayIndex)
    {
        EXPECT_FALSE (
            buttons[displayIndex]
                ->getToggleState());
        EXPECT_TRUE (
            handlers[displayIndex]
                ->getCurrentState()
                .isCollapsed());
        EXPECT_EQ (
            handlers[displayIndex]
                ->getValueInterface()
                ->getCurrentValueAsString(),
            "Closed");
        EXPECT_EQ (
            findLfpAncestor<
                LfpViewer::
                    LfpDisplayOptions> (
                *buttons[
                    displayIndex])
                ->getHeight(),
            60);
        EXPECT_EQ (
            findLfpAncestor<
                LfpViewer::
                    LfpDisplayOptions> (
                *buttons[
                    displayIndex])
                ->getExtendedOptionsHeight(),
            0);
    }

    invokeFromWorker (
        AccessibilityActionType::
            collapse);
    EXPECT_EQ (
        listener
            .callbackCount
            .load(),
        2);

    canvas
        ->toggleOptionsDrawer (
            true);
    EXPECT_EQ (
        listener
            .callbackCount
            .load(),
        2);
    for (int displayIndex = 0;
         displayIndex < 3;
         ++displayIndex)
    {
        EXPECT_TRUE (
            handlers[displayIndex]
                ->getCurrentState()
                .isExpanded());
    }

    buttons[0]->removeListener (
        &listener);
}

TEST_F (LfpDisplayNodeTests,
        OptionsDrawerActionsIgnoreDisabledAndDestroyedControls)
{
    auto canvas =
        std::make_unique<
            LfpViewer::
                LfpDisplayCanvas> (
            processor,
            LfpViewer::
                SplitLayouts::SINGLE,
            false);
    canvas->updateSettings();
    canvas->setSize (900, 800);
    canvas->addToDesktop (0);
    canvas->setVisible (true);

    const auto id =
        "oe.processor."
        + String (
            processor->getNodeId())
        + ".lfp.display_1.options_drawer";
    auto* button =
        dynamic_cast<Button*> (
            findLfpDescendantById (
                *canvas,
                id));
    ASSERT_NE (
        button,
        nullptr);
    auto* handler =
        button
            ->getAccessibilityHandler();
    ASSERT_NE (
        handler,
        nullptr);
    const auto actions =
        handler->getActions();

    button->setEnabled (
        false);
    EXPECT_FALSE (
        handler->isEnabled());
    EXPECT_TRUE (
        actions.invoke (
            AccessibilityActionType::
                expand));
    EXPECT_FALSE (
        canvas
            ->optionsDrawerIsOpen);
    EXPECT_FALSE (
        button->getToggleState());

    button->setEnabled (
        true);
    canvas.reset();

    std::atomic<bool>
        workerReturned { false };
    std::thread worker (
        [&]
        {
            actions.invoke (
                AccessibilityActionType::
                    expand);
            actions.invoke (
                AccessibilityActionType::
                    press);
            workerReturned.store (
                true);
        });
    for (int attempt = 0;
         attempt < 100
             && ! workerReturned.load();
         ++attempt)
    {
        MessageManager::getInstance()
            ->runDispatchLoopUntil (
                10);
    }
    joinLfpWorkerOrAbort (
        worker,
        workerReturned);
}

TEST_F (LfpDisplayNodeTests,
        OptionsDrawerXmlStateRoundTripsWithoutNotifications)
{
    auto canvas =
        std::make_unique<
            LfpViewer::
                LfpDisplayCanvas> (
            processor,
            LfpViewer::
                SplitLayouts::SINGLE,
            false);
    canvas->updateSettings();
    canvas->setSize (900, 800);
    canvas->addToDesktop (0);
    canvas->setVisible (true);

    const auto prefix =
        "oe.processor."
        + String (
            processor->getNodeId())
        + ".lfp.display_";
    std::array<Button*, 3>
        buttons {};
    std::array<
        AccessibilityHandler*,
        3>
        handlers {};
    for (int displayIndex = 0;
         displayIndex < 3;
         ++displayIndex)
    {
        buttons[displayIndex] =
            dynamic_cast<Button*> (
                findLfpDescendantById (
                    *canvas,
                    prefix
                        + String (
                            displayIndex
                            + 1)
                        + ".options_drawer"));
        ASSERT_NE (
            buttons[displayIndex],
            nullptr);
        handlers[displayIndex] =
            buttons[displayIndex]
                ->getAccessibilityHandler();
        ASSERT_NE (
            handlers[displayIndex],
            nullptr);
    }

    LfpThreadTrackingButtonListener
        listener;
    buttons[0]->addListener (
        &listener);
    canvas
        ->toggleOptionsDrawer (
            true);
    XmlElement savedNode (
        "CANVAS");
    canvas
        ->saveOptionsDrawerState (
            savedNode);
    EXPECT_TRUE (
        savedNode.hasAttribute (
            "showAllOptions"));
    EXPECT_TRUE (
        savedNode.getBoolAttribute (
            "showAllOptions"));

    canvas
        ->toggleOptionsDrawer (
            false);
    canvas
        ->restoreOptionsDrawerState (
            savedNode);
    EXPECT_TRUE (
        canvas
            ->optionsDrawerIsOpen);
    EXPECT_EQ (
        listener
            .callbackCount
            .load(),
        0);
    for (int displayIndex = 0;
         displayIndex < 3;
         ++displayIndex)
    {
        EXPECT_TRUE (
            buttons[displayIndex]
                ->getToggleState());
        EXPECT_TRUE (
            handlers[displayIndex]
                ->getCurrentState()
                .isExpanded());
        auto* options =
            findLfpAncestor<
                LfpViewer::
                    LfpDisplayOptions> (
                *buttons[
                    displayIndex]);
        ASSERT_NE (
            options,
            nullptr);
        EXPECT_EQ (
            options->getHeight(),
            210);
        EXPECT_EQ (
            options
                ->getExtendedOptionsHeight(),
            150);
    }

    XmlElement explicitClosedNode (
        "CANVAS");
    explicitClosedNode.setAttribute (
        "showAllOptions",
        false);
    canvas
        ->restoreOptionsDrawerState (
            explicitClosedNode);
    EXPECT_FALSE (
        canvas
            ->optionsDrawerIsOpen);

    canvas
        ->toggleOptionsDrawer (
            true);
    XmlElement missingAttributeNode (
        "CANVAS");
    canvas
        ->restoreOptionsDrawerState (
            missingAttributeNode);
    EXPECT_FALSE (
        canvas
            ->optionsDrawerIsOpen);
    EXPECT_EQ (
        listener
            .callbackCount
            .load(),
        0);
    for (int displayIndex = 0;
         displayIndex < 3;
         ++displayIndex)
    {
        EXPECT_FALSE (
            buttons[displayIndex]
                ->getToggleState());
        EXPECT_TRUE (
            handlers[displayIndex]
                ->getCurrentState()
                .isCollapsed());
        EXPECT_EQ (
            findLfpAncestor<
                LfpViewer::
                    LfpDisplayOptions> (
                *buttons[
                    displayIndex])
                ->getExtendedOptionsHeight(),
            0);
    }
    buttons[0]->removeListener (
        &listener);
}

TEST_F (LfpDisplayNodeTests,
        ExposesRangeTypeSelectorsForEveryPane)
{
    auto canvas =
        std::make_unique<
            LfpViewer::
                LfpDisplayCanvas> (
            processor,
            LfpViewer::
                SplitLayouts::SINGLE,
            false);
    canvas->updateSettings();
    canvas->setSize (600, 800);
    canvas->addToDesktop (0);
    canvas->setVisible (true);

    const std::array<String, 3>
        typeNames {
            "DATA",
            "AUX",
            "ADC"
        };
    const auto prefix =
        "oe.processor."
        + String (
            processor->getNodeId())
        + ".lfp.display_";

    for (int displayIndex = 0;
         displayIndex < 3;
         ++displayIndex)
    {
        const auto displayNumber =
            displayIndex + 1;
        for (int typeIndex = 0;
             typeIndex < 3;
             ++typeIndex)
        {
            const auto typeName =
                typeNames[typeIndex];
            const auto id =
                prefix
                + String (displayNumber)
                + ".channel_type."
                + typeName
                      .toLowerCase();
            auto* component =
                findLfpDescendantById (
                    *canvas,
                    id);
            ASSERT_NE (
                component,
                nullptr)
                << id;
            auto* handler =
                component
                    ->getAccessibilityHandler();
            ASSERT_NE (
                handler,
                nullptr);
            EXPECT_EQ (
                handler->getRole(),
                AccessibilityRole::
                    radioButton);
            EXPECT_EQ (
                handler->getTitle(),
                "LFP display "
                    + String (
                        displayNumber)
                    + " "
                    + "channel type "
                    + typeName);
            const auto description =
                "Select "
                + typeName
                + " channels as the target for the voltage range control in LFP display "
                + String (
                    displayNumber)
                + ".";
            EXPECT_EQ (
                handler
                    ->getDescription(),
                description);
            EXPECT_EQ (
                handler->getHelp(),
                description);
            EXPECT_EQ (
                handler
                    ->getCurrentState()
                    .isChecked(),
                typeIndex == 0);
            EXPECT_TRUE (
                handler->getActions()
                    .contains (
                        AccessibilityActionType::
                            press));
            EXPECT_FALSE (
                handler->getActions()
                    .contains (
                        AccessibilityActionType::
                            toggle));
            EXPECT_FALSE (
                handler
                    ->getCurrentState()
                    .isCheckable());
            EXPECT_EQ (
                handler
                    ->getValueInterface(),
                nullptr);
            EXPECT_EQ (
                findLfpAncestor<
                    LfpViewer::
                        LfpDisplayOptions> (
                    *component)
                    ->isVisible(),
                displayIndex == 0);
        }
    }
}

TEST_F (LfpDisplayNodeTests,
        ExposesColourGroupingForEveryPane)
{
    auto canvas =
        std::make_unique<
            LfpViewer::
                LfpDisplayCanvas> (
            processor,
            LfpViewer::
                SplitLayouts::SINGLE,
            false);
    canvas->updateSettings();
    canvas->setSize (600, 800);
    canvas->addToDesktop (0);
    canvas->setVisible (true);

    const std::array<String, 6>
        choices {
            "1",
            "2",
            "4",
            "8",
            "16",
            "By Shank"
        };
    const std::array<String, 6>
        choiceIds {
            "1",
            "2",
            "4",
            "8",
            "16",
            "by_shank"
        };
    const auto prefix =
        "oe.processor."
        + String (
            processor->getNodeId())
        + ".lfp.display_";

    for (int displayIndex = 0;
         displayIndex < 3;
         ++displayIndex)
    {
        const auto displayNumber =
            displayIndex + 1;
        const auto id =
            prefix
            + String (displayNumber)
            + ".colour_grouping";
        auto* grouping =
            dynamic_cast<
                MessageThreadComboBox*> (
                findLfpDescendantById (
                    *canvas,
                    id));
        ASSERT_NE (
            grouping,
            nullptr)
            << id;
        EXPECT_EQ (
            grouping->getNumItems(),
            static_cast<int> (
                choices.size()));
        for (int choiceIndex = 0;
             choiceIndex
             < static_cast<int> (
                   choices.size());
             ++choiceIndex)
        {
            EXPECT_EQ (
                grouping->getItemText (
                    choiceIndex),
                choices[choiceIndex]);
        }

        int choiceIndex = 0;
        for (PopupMenu::
                 MenuItemIterator
                 iterator (
                     *grouping
                          ->getRootMenu(),
                     false);
             iterator.next();)
        {
            ASSERT_LT (
                choiceIndex,
                static_cast<int> (
                    choices.size()));
            EXPECT_EQ (
                iterator
                    .getItem()
                    .accessibilityId,
                id
                    + ".choice."
                    + choiceIds[choiceIndex]);
            ++choiceIndex;
        }
        EXPECT_EQ (
            choiceIndex,
            static_cast<int> (
                choices.size()));

        auto* handler =
            grouping
                ->getAccessibilityHandler();
        ASSERT_NE (
            handler,
            nullptr);
        EXPECT_EQ (
            handler->getRole(),
            AccessibilityRole::
                comboBox);
        EXPECT_EQ (
            handler->getTitle(),
            "LFP display "
                + String (
                    displayNumber)
                + " colour grouping");
        const auto description =
            "Choose how LFP display "
            + String (
                displayNumber)
            + " assigns display colours: 1, 2, 4, 8, or 16 groups that many adjacent channels, while By Shank uses channel group metadata with a legacy depth-based fallback. This changes display colours only; acquisition and recording are unaffected.";
        EXPECT_EQ (
            handler
                ->getDescription(),
            description);
        EXPECT_EQ (
            handler->getHelp(),
            description);
        auto* value =
            handler
                ->getValueInterface();
        ASSERT_NE (
            value,
            nullptr);
        EXPECT_FALSE (
            value->isReadOnly());
        EXPECT_EQ (
            value
                ->getCurrentValueAsString(),
            "1");
        EXPECT_TRUE (
            handler->getActions()
                .contains (
                    AccessibilityActionType::
                        expand));
        EXPECT_TRUE (
            handler->getActions()
                .contains (
                    AccessibilityActionType::
                        collapse));
        EXPECT_EQ (
            findLfpAncestor<
                LfpViewer::
                    LfpDisplayOptions> (
                *grouping)
                ->isVisible(),
            displayIndex == 0);
    }
}

TEST_F (LfpDisplayNodeTests,
        ColourGroupingWorkerSelectsByShankOnMessageThread)
{
    auto canvas =
        std::make_unique<
            LfpViewer::
                LfpDisplayCanvas> (
            processor,
            LfpViewer::
                SplitLayouts::SINGLE,
            false);
    canvas->updateSettings();
    canvas->setSize (600, 800);
    canvas->addToDesktop (0);
    canvas->setVisible (true);

    const auto id =
        "oe.processor."
        + String (
            processor->getNodeId())
        + ".lfp.display_1.colour_grouping";
    auto* grouping =
        dynamic_cast<
            MessageThreadComboBox*> (
            findLfpDescendantById (
                *canvas,
                id));
    ASSERT_NE (
        grouping,
        nullptr);
    auto* display =
        findLfpDescendant<
            LfpViewer::
                LfpDisplay> (
            *canvas);
    ASSERT_NE (
        display,
        nullptr);
    auto* handler =
        grouping
            ->getAccessibilityHandler();
    ASSERT_NE (
        handler,
        nullptr);
    auto* value =
        handler
            ->getValueInterface();
    ASSERT_NE (
        value,
        nullptr);

    MessageManager::getInstance()
        ->runDispatchLoopUntil (
            30);
    LfpThreadTrackingComboBoxListener
        listener;
    grouping->addListener (
        &listener);
    std::atomic<bool>
        workerReturned { false };
    std::thread worker (
        [&]
        {
            value->setValueAsString (
                "By Shank");
            workerReturned.store (
                true);
        });
    for (int attempt = 0;
         attempt < 100
             && (! workerReturned.load()
                 || grouping->getSelectedId()
                        != 6);
         ++attempt)
    {
        MessageManager::getInstance()
            ->runDispatchLoopUntil (
                10);
    }
    joinLfpWorkerOrAbort (
        worker,
        workerReturned);
    grouping->removeListener (
        &listener);

    EXPECT_EQ (
        listener
            .callbackCount
            .load(),
        1);
    EXPECT_TRUE (
        listener
            .callbackUsedMessageThread
            .load());
    EXPECT_EQ (
        grouping->getSelectedId(),
        6);
    EXPECT_EQ (
        grouping->getText(),
        "By Shank");
    EXPECT_EQ (
        value
            ->getCurrentValueAsString(),
        "By Shank");
    EXPECT_EQ (
        display
            ->getColourGrouping(),
        "By Shank");

    value->setValueAsString (
        "unsupported");
    EXPECT_EQ (
        grouping->getSelectedId(),
        6);
    EXPECT_EQ (
        value
            ->getCurrentValueAsString(),
        "By Shank");
}

TEST_F (LfpDisplayNodeTests,
        ColourGroupingRemainsConsistentWithoutChannels)
{
    auto zeroChannelTester =
        std::make_unique<
            ProcessorTester> (
            TestSourceNodeBuilder (
                FakeSourceNodeParams {
                    0,
                    sampleRate,
                    bitVolts }));
    auto* zeroChannelProcessor =
        zeroChannelTester
            ->createProcessor<
                LfpViewer::
                    LfpDisplayNode> (
                Plugin::Processor::
                    SINK);
    auto canvas =
        std::make_unique<
            LfpViewer::
                LfpDisplayCanvas> (
            zeroChannelProcessor,
            LfpViewer::
                SplitLayouts::SINGLE,
            false);
    canvas->updateSettings();
    canvas->setSize (600, 800);
    canvas->addToDesktop (0);
    canvas->setVisible (true);

    const auto id =
        "oe.processor."
        + String (
            zeroChannelProcessor
                ->getNodeId())
        + ".lfp.display_1.colour_grouping";
    auto* grouping =
        dynamic_cast<
            MessageThreadComboBox*> (
            findLfpDescendantById (
                *canvas,
                id));
    ASSERT_NE (
        grouping,
        nullptr);
    auto* display =
        findLfpDescendant<
            LfpViewer::
                LfpDisplay> (
            *canvas);
    ASSERT_NE (
        display,
        nullptr);
    ASSERT_EQ (
        zeroChannelProcessor
            ->getNumInputs(),
        0);

    auto* value =
        grouping
            ->getAccessibilityHandler()
            ->getValueInterface();
    ASSERT_NE (
        value,
        nullptr);
    value->setValueAsString (
        "By Shank");

    EXPECT_EQ (
        grouping->getSelectedId(),
        6);
    EXPECT_EQ (
        value
            ->getCurrentValueAsString(),
        "By Shank");
    EXPECT_EQ (
        display
            ->getColourGrouping(),
        "By Shank");
}

TEST (LfpColourGroupingTests,
      NormalisesMalformedPersistedIds)
{
    constexpr int numberOfChoices =
        6;
    for (int validId = 1;
         validId <= numberOfChoices;
         ++validId)
    {
        EXPECT_EQ (
            LfpViewer::
                normaliseLfpColourGroupingId (
                    validId,
                    numberOfChoices),
            validId);
    }

    for (const auto invalidId :
         { -1, 0, 7, 999 })
    {
        EXPECT_EQ (
            LfpViewer::
                normaliseLfpColourGroupingId (
                    invalidId,
                    numberOfChoices),
            1);
    }
}

TEST_F (LfpDisplayNodeTests,
        ColourGroupingXmlParameterRoundTripsAndNormalisesMalformedIds)
{
    auto canvas =
        std::make_unique<
            LfpViewer::
                LfpDisplayCanvas> (
            processor,
            LfpViewer::
                SplitLayouts::SINGLE,
            false);
    canvas->updateSettings();
    canvas->setSize (600, 800);
    canvas->addToDesktop (0);
    canvas->setVisible (true);

    const auto id =
        "oe.processor."
        + String (
            processor->getNodeId())
        + ".lfp.display_1.colour_grouping";
    auto* grouping =
        dynamic_cast<
            MessageThreadComboBox*> (
            findLfpDescendantById (
                *canvas,
                id));
    ASSERT_NE (
        grouping,
        nullptr);
    auto* options =
        findLfpAncestor<
            LfpViewer::
                LfpDisplayOptions> (
            *grouping);
    auto* display =
        findLfpDescendant<
            LfpViewer::
                LfpDisplay> (
            *canvas);
    ASSERT_NE (
        options,
        nullptr);
    ASSERT_NE (
        display,
        nullptr);
    auto* value =
        grouping
            ->getAccessibilityHandler()
            ->getValueInterface();
    ASSERT_NE (
        value,
        nullptr);

    LfpThreadTrackingComboBoxListener
        listener;
    grouping->addListener (
        &listener);
    options
        ->setColourGroupingSelection (
            6);
    XmlElement savedNode (
        "LFPDISPLAY0");
    options
        ->saveColourGroupingParameter (
            savedNode);
    EXPECT_TRUE (
        savedNode.hasAttribute (
            "colourGrouping"));
    EXPECT_EQ (
        savedNode.getIntAttribute (
            "colourGrouping"),
        6);

    options
        ->setColourGroupingSelection (
            1);
    EXPECT_EQ (
        grouping->getSelectedId(),
        1);
    EXPECT_EQ (
        value
            ->getCurrentValueAsString(),
        "1");
    options
        ->restoreColourGroupingParameter (
            savedNode);
    EXPECT_EQ (
        grouping->getSelectedId(),
        6);
    EXPECT_EQ (
        grouping->getText(),
        "By Shank");
    EXPECT_EQ (
        value
            ->getCurrentValueAsString(),
        "By Shank");
    EXPECT_EQ (
        display
            ->getColourGrouping(),
        "By Shank");
    EXPECT_EQ (
        listener
            .callbackCount
            .load(),
        0);

    const std::array<String, 6>
        expectedValues {
            "1",
            "2",
            "4",
            "8",
            "16",
            "By Shank"
        };
    for (int itemId = 1;
         itemId <= 6;
         ++itemId)
    {
        XmlElement node (
            "LFPDISPLAY0");
        node.setAttribute (
            "colourGrouping",
            itemId);
        options
            ->restoreColourGroupingParameter (
                node);
        EXPECT_EQ (
            grouping->getSelectedId(),
            itemId);
        EXPECT_EQ (
            grouping->getText(),
            expectedValues[
                itemId - 1]);
        EXPECT_EQ (
            value
                ->getCurrentValueAsString(),
            expectedValues[
                itemId - 1]);
        EXPECT_EQ (
            display
                ->getColourGrouping(),
            expectedValues[
                itemId - 1]);
    }

    for (const auto invalidId :
         { -1, 0, 7, 999 })
    {
        XmlElement node (
            "LFPDISPLAY0");
        node.setAttribute (
            "colourGrouping",
            invalidId);
        options
            ->setColourGroupingSelection (
                6);
        options
            ->restoreColourGroupingParameter (
                node);
        EXPECT_EQ (
            grouping->getSelectedId(),
            1);
        EXPECT_EQ (
            grouping->getText(),
            "1");
        EXPECT_EQ (
            value
                ->getCurrentValueAsString(),
            "1");
        EXPECT_EQ (
            display
                ->getColourGrouping(),
            "1");
    }

    XmlElement missingAttributeNode (
        "LFPDISPLAY0");
    options
        ->setColourGroupingSelection (
            6);
    options
        ->restoreColourGroupingParameter (
            missingAttributeNode);
    EXPECT_EQ (
        grouping->getSelectedId(),
        1);
    EXPECT_EQ (
        value
            ->getCurrentValueAsString(),
        "1");
    EXPECT_EQ (
        display
            ->getColourGrouping(),
        "1");
    EXPECT_EQ (
        listener
            .callbackCount
            .load(),
        0);
    grouping->removeListener (
        &listener);
}

TEST_F (LfpDisplayNodeTests,
        RangeTypeSelectionRunsOnMessageThreadAndTracksProgrammaticState)
{
    auto canvas =
        std::make_unique<
            LfpViewer::
                LfpDisplayCanvas> (
            processor,
            LfpViewer::
                SplitLayouts::SINGLE,
            false);
    canvas->updateSettings();
    canvas->setSize (600, 800);
    canvas->addToDesktop (0);
    canvas->setVisible (true);

    const auto prefix =
        "oe.processor."
        + String (
            processor->getNodeId())
        + ".lfp.display_1.";
    auto* data =
        dynamic_cast<Button*> (
            findLfpDescendantById (
                *canvas,
                prefix
                    + "channel_type.data"));
    auto* aux =
        dynamic_cast<Button*> (
            findLfpDescendantById (
                *canvas,
                prefix
                    + "channel_type.aux"));
    auto* adc =
        dynamic_cast<Button*> (
            findLfpDescendantById (
                *canvas,
                prefix
                    + "channel_type.adc"));
    auto* range =
        findLfpDescendantById (
            *canvas,
            prefix
                + "voltage_range");
    ASSERT_NE (data, nullptr);
    ASSERT_NE (aux, nullptr);
    ASSERT_NE (adc, nullptr);
    ASSERT_NE (range, nullptr);
    auto* options =
        findLfpAncestor<
            LfpViewer::
                LfpDisplayOptions> (
            *aux);
    ASSERT_NE (
        options,
        nullptr);
    auto* dataHandler =
        data
            ->getAccessibilityHandler();
    auto* auxHandler =
        aux
            ->getAccessibilityHandler();
    auto* adcHandler =
        adc
            ->getAccessibilityHandler();
    ASSERT_NE (
        dataHandler,
        nullptr);
    ASSERT_NE (
        auxHandler,
        nullptr);
    ASSERT_NE (
        adcHandler,
        nullptr);
    const auto auxActions =
        auxHandler->getActions();
    LfpThreadTrackingButtonListener
        listener;
    aux->addListener (
        &listener);

    std::atomic<bool>
        workerReturned { false };
    bool pressed = false;
    std::thread worker (
        [&]
        {
            pressed =
                auxActions.invoke (
                    AccessibilityActionType::
                        press);
            workerReturned.store (
                true);
        });
    for (int attempt = 0;
         attempt < 100
             && (! workerReturned.load()
                 || options
                            ->getSelectedType()
                        != ContinuousChannel::
                               Type::AUX);
         ++attempt)
    {
        MessageManager::getInstance()
            ->runDispatchLoopUntil (
                10);
    }
    joinLfpWorkerOrAbort (
        worker,
        workerReturned);
    aux->removeListener (
        &listener);

    EXPECT_TRUE (pressed);
    EXPECT_TRUE (
        workerReturned.load());
    EXPECT_EQ (
        listener
            .callbackCount
            .load(),
        1);
    EXPECT_TRUE (
        listener
            .callbackUsedMessageThread
            .load());
    EXPECT_FALSE (
        dataHandler
            ->getCurrentState()
            .isChecked());
    EXPECT_TRUE (
        auxHandler
            ->getCurrentState()
            .isChecked());
    EXPECT_FALSE (
        adcHandler
            ->getCurrentState()
            .isChecked());
    EXPECT_EQ (
        range
            ->getAccessibilityHandler()
            ->getValueInterface()
            ->getCurrentValueAsString(),
        "Auto");
    EXPECT_EQ (
        range
            ->getAccessibilityHandler()
            ->getHelp(),
        "Choose the AUX voltage range shown in LFP display 1, in mV, or Auto.");

    options->setSelectedType (
        ContinuousChannel::Type::
            ADC);
    EXPECT_FALSE (
        dataHandler
            ->getCurrentState()
            .isChecked());
    EXPECT_FALSE (
        auxHandler
            ->getCurrentState()
            .isChecked());
    EXPECT_TRUE (
        adcHandler
            ->getCurrentState()
            .isChecked());
    EXPECT_EQ (
        range
            ->getAccessibilityHandler()
            ->getValueInterface()
            ->getCurrentValueAsString(),
        "10.0");
}

TEST_F (LfpDisplayNodeTests,
        RangeTypeActionsIgnoreDisabledAndDestroyedControls)
{
    AccessibilityActions
        retainedActions;
    std::atomic<bool>
        workerReturned { false };
    {
        auto canvas =
            std::make_unique<
                LfpViewer::
                    LfpDisplayCanvas> (
                processor,
                LfpViewer::
                    SplitLayouts::SINGLE,
                false);
        canvas->updateSettings();
        canvas->setSize (600, 800);
        canvas->addToDesktop (0);
        canvas->setVisible (true);

        const auto prefix =
            "oe.processor."
            + String (
                processor->getNodeId())
            + ".lfp.display_1.";
        auto* data =
            dynamic_cast<Button*> (
                findLfpDescendantById (
                    *canvas,
                    prefix
                        + "channel_type.data"));
        auto* aux =
            dynamic_cast<Button*> (
                findLfpDescendantById (
                    *canvas,
                    prefix
                        + "channel_type.aux"));
        ASSERT_NE (data, nullptr);
        ASSERT_NE (aux, nullptr);
        auto* options =
            findLfpAncestor<
                LfpViewer::
                    LfpDisplayOptions> (
                *aux);
        ASSERT_NE (
            options,
            nullptr);
        retainedActions =
            aux
                ->getAccessibilityHandler()
                ->getActions();

        aux->setEnabled (false);
        bool disabledActionFound =
            false;
        std::thread disabledWorker (
            [&]
            {
                disabledActionFound =
                    retainedActions
                        .invoke (
                            AccessibilityActionType::
                                press);
                workerReturned.store (
                    true);
            });
        for (int attempt = 0;
             attempt < 100
                 && ! workerReturned
                           .load();
             ++attempt)
        {
            MessageManager::getInstance()
                ->runDispatchLoopUntil (
                    10);
        }
        joinLfpWorkerOrAbort (
            disabledWorker,
            workerReturned);
        EXPECT_TRUE (
            disabledActionFound);
        EXPECT_TRUE (
            data->getToggleState());
        EXPECT_FALSE (
            aux->getToggleState());
        EXPECT_EQ (
            options
                ->getSelectedType(),
            ContinuousChannel::Type::
                ELECTRODE);
    }

    workerReturned.store (
        false);
    bool staleActionFound = false;
    std::thread staleWorker (
        [&]
        {
            staleActionFound =
                retainedActions
                    .invoke (
                        AccessibilityActionType::
                            press);
            workerReturned.store (
                true);
        });
    for (int attempt = 0;
         attempt < 100
             && ! workerReturned
                       .load();
         ++attempt)
    {
        MessageManager::getInstance()
            ->runDispatchLoopUntil (
                10);
    }
    joinLfpWorkerOrAbort (
        staleWorker,
        workerReturned);
    EXPECT_TRUE (
        staleActionFound);
}

TEST_F (LfpDisplayNodeTests,
        PauseToggleRunsOnMessageThreadAndTracksProgrammaticState)
{
    auto canvas =
        std::make_unique<
            LfpViewer::
                LfpDisplayCanvas> (
            processor,
            LfpViewer::
                SplitLayouts::SINGLE,
            false);
    canvas->updateSettings();
    canvas->setSize (600, 800);
    canvas->addToDesktop (0);
    canvas->setVisible (true);

    const auto prefix =
        "oe.processor."
        + String (
            processor->getNodeId())
        + ".lfp.display_1.";
    auto* pause =
        dynamic_cast<Button*> (
            findLfpDescendantById (
                *canvas,
                prefix
                    + "pause"));
    auto* timebase =
        findLfpDescendantById (
            *canvas,
            prefix
                + "timebase");
    ASSERT_NE (
        pause,
        nullptr);
    ASSERT_NE (
        timebase,
        nullptr);
    auto* options =
        findLfpAncestor<
            LfpViewer::
                LfpDisplayOptions> (
            *pause);
    auto* display =
        findLfpDescendant<
            LfpViewer::
                LfpDisplay> (
            *canvas);
    auto* timescale =
        findLfpDescendant<
            LfpViewer::
                LfpTimescale> (
            *canvas);
    ASSERT_NE (
        options,
        nullptr);
    ASSERT_NE (
        display,
        nullptr);
    ASSERT_NE (
        timescale,
        nullptr);
    auto* handler =
        pause
            ->getAccessibilityHandler();
    ASSERT_NE (
        handler,
        nullptr);
    const auto actions =
        handler->getActions();
    LfpThreadTrackingButtonListener
        listener;
    pause->addListener (
        &listener);

    std::atomic<bool>
        workerReturned { false };
    bool toggled = false;
    std::thread worker (
        [&]
        {
            toggled =
                actions.invoke (
                    AccessibilityActionType::
                        toggle);
            workerReturned.store (
                true);
        });
    for (int attempt = 0;
         attempt < 100
             && ! workerReturned.load();
         ++attempt)
    {
        MessageManager::getInstance()
            ->runDispatchLoopUntil (
                10);
    }
    worker.join();
    pause->removeListener (
        &listener);

    EXPECT_TRUE (toggled);
    EXPECT_TRUE (
        workerReturned.load());
    EXPECT_EQ (
        listener
            .callbackCount
            .load(),
        1);
    EXPECT_TRUE (
        listener
            .callbackUsedMessageThread
            .load());
    EXPECT_TRUE (
        pause->getToggleState());
    EXPECT_TRUE (
        handler->getCurrentState()
            .isChecked());
    EXPECT_EQ (
        handler
            ->getValueInterface()
            ->getCurrentValueAsString(),
        "Paused");
    EXPECT_FALSE (
        timebase->isEnabled());
    EXPECT_TRUE (
        display->isPaused());
    EXPECT_TRUE (
        timescale
            ->getPausedState());

    EXPECT_TRUE (
        actions.invoke (
            AccessibilityActionType::
                toggle));
    EXPECT_FALSE (
        pause->getToggleState());
    EXPECT_FALSE (
        handler->getCurrentState()
            .isChecked());
    EXPECT_EQ (
        handler
            ->getValueInterface()
            ->getCurrentValueAsString(),
        "Running");
    EXPECT_TRUE (
        timebase->isEnabled());
    EXPECT_FALSE (
        display->isPaused());
    EXPECT_FALSE (
        timescale
            ->getPausedState());

    options->setPausedState (
        true);
    EXPECT_EQ (
        handler
            ->getValueInterface()
            ->getCurrentValueAsString(),
        "Paused");
    options->setPausedState (
        false);
    EXPECT_EQ (
        handler
            ->getValueInterface()
            ->getCurrentValueAsString(),
        "Running");

    pause->triggerClick();
    for (int attempt = 0;
         attempt < 50
             && ! pause
                       ->getToggleState();
         ++attempt)
    {
        MessageManager::getInstance()
            ->runDispatchLoopUntil (
                10);
    }
    EXPECT_TRUE (
        pause->getToggleState());
    EXPECT_TRUE (
        display->isPaused());
    EXPECT_TRUE (
        timescale
            ->getPausedState());

    pause->triggerClick();
    for (int attempt = 0;
         attempt < 50
             && pause
                    ->getToggleState();
         ++attempt)
    {
        MessageManager::getInstance()
            ->runDispatchLoopUntil (
                10);
    }
    EXPECT_FALSE (
        pause->getToggleState());
    EXPECT_FALSE (
        display->isPaused());
    EXPECT_FALSE (
        timescale
            ->getPausedState());
}

TEST_F (LfpDisplayNodeTests,
        PauseToggleActionsIgnoreDisabledAndDestroyedControls)
{
    AccessibilityActions
        retainedActions;
    {
        auto canvas =
            std::make_unique<
                LfpViewer::
                    LfpDisplayCanvas> (
                processor,
                LfpViewer::
                    SplitLayouts::SINGLE,
                false);
        canvas->updateSettings();
        canvas->setSize (600, 800);
        canvas->addToDesktop (0);
        canvas->setVisible (true);

        const auto prefix =
            "oe.processor."
            + String (
                processor->getNodeId())
            + ".lfp.display_";
        auto* visiblePause =
            dynamic_cast<Button*> (
                findLfpDescendantById (
                    *canvas,
                    prefix
                        + "1.pause"));
        ASSERT_NE (
            visiblePause,
            nullptr);
        ASSERT_TRUE (
            visiblePause->isShowing());

        auto* visibleHandler =
            visiblePause
                ->getAccessibilityHandler();
        ASSERT_NE (
            visibleHandler,
            nullptr);
        retainedActions =
            visibleHandler
                ->getActions();

        visiblePause->setEnabled (
            false);
        EXPECT_FALSE (
            visibleHandler
                ->isEnabled());
        bool disabledActionFound =
            false;
        std::thread disabledWorker (
            [&]
            {
                disabledActionFound =
                    retainedActions
                        .invoke (
                            AccessibilityActionType::
                                toggle);
            });
        disabledWorker.join();
        EXPECT_TRUE (
            disabledActionFound);
        EXPECT_FALSE (
            visiblePause
                ->getToggleState());
    }

    bool staleActionFound =
        false;
    std::thread staleWorker (
        [&]
        {
            staleActionFound =
                retainedActions
                    .invoke (
                        AccessibilityActionType::
                            toggle);
        });
    staleWorker.join();
    EXPECT_TRUE (
        staleActionFound);
}

TEST_F (LfpDisplayNodeTests,
        SingleLayoutSpaceKeyTogglesEveryDisplayPause)
{
    auto canvas =
        std::make_unique<
            LfpViewer::
                LfpDisplayCanvas> (
            processor,
            LfpViewer::
                SplitLayouts::SINGLE,
            false);
    canvas->updateSettings();

    const auto prefix =
        "oe.processor."
        + String (
            processor->getNodeId())
        + ".lfp.display_";
    std::array<Button*, 3>
        pauseControls {};
    for (int displayIndex = 0;
         displayIndex < 3;
         ++displayIndex)
    {
        pauseControls[displayIndex] =
            dynamic_cast<Button*> (
                findLfpDescendantById (
                    *canvas,
                    prefix
                        + String (
                            displayIndex + 1)
                        + ".pause"));
        ASSERT_NE (
            pauseControls[displayIndex],
            nullptr);
        EXPECT_FALSE (
            pauseControls[displayIndex]
                ->getToggleState());
    }

    EXPECT_TRUE (
        canvas->keyPressed (
            KeyPress (
                KeyPress::spaceKey,
                ModifierKeys(),
                ' ')));
    for (auto* pause :
         pauseControls)
    {
        EXPECT_TRUE (
            pause
                ->getToggleState());
    }

    EXPECT_TRUE (
        canvas->keyPressed (
            KeyPress (
                KeyPress::spaceKey,
                ModifierKeys(),
                ' ')));
    for (auto* pause :
         pauseControls)
    {
        EXPECT_FALSE (
            pause
                ->getToggleState());
    }
}

TEST_F (LfpDisplayNodeTests,
        ProgrammaticDisplayParameterUpdatesRefreshAccessibleValues)
{
    auto canvas =
        std::make_unique<
            LfpViewer::
                LfpDisplayCanvas> (
            processor,
            LfpViewer::
                SplitLayouts::SINGLE,
            false);
    canvas->updateSettings();

    const auto prefix =
        "oe.processor."
        + String (
            processor->getNodeId())
        + ".lfp.display_1.";
    auto* timebase =
        dynamic_cast<
            MessageThreadComboBox*> (
            findLfpDescendantById (
                *canvas,
                prefix
                    + "timebase"));
    auto* channelHeight =
        dynamic_cast<
            MessageThreadComboBox*> (
            findLfpDescendantById (
                *canvas,
                prefix
                    + "channel_height"));
    auto* voltageRange =
        dynamic_cast<
            MessageThreadComboBox*> (
            findLfpDescendantById (
                *canvas,
                prefix
                    + "voltage_range"));
    ASSERT_NE (
        timebase,
        nullptr);
    ASSERT_NE (
        channelHeight,
        nullptr);
    ASSERT_NE (
        voltageRange,
        nullptr);
    auto timebaseHandler =
        timebase
            ->createAccessibilityHandler();
    auto channelHeightHandler =
        channelHeight
            ->createAccessibilityHandler();
    auto voltageRangeHandler =
        voltageRange
            ->createAccessibilityHandler();
    ASSERT_NE (
        timebaseHandler,
        nullptr);
    ASSERT_NE (
        channelHeightHandler,
        nullptr);
    ASSERT_NE (
        voltageRangeHandler,
        nullptr);

    auto* options =
        findLfpAncestor<
            LfpViewer::
                LfpDisplayOptions> (
            *timebase);
    ASSERT_NE (
        options,
        nullptr);

    options
        ->setTimebaseAndSelectionText (
            0.75f);
    options
        ->setSpreadSelection (
            55);
    options
        ->setRangeSelection (
            333.0f);

    EXPECT_EQ (
        timebaseHandler
            ->getValueInterface()
            ->getCurrentValueAsString(),
        "0.8");
    EXPECT_EQ (
        channelHeightHandler
            ->getValueInterface()
            ->getCurrentValueAsString(),
        "55");
    EXPECT_EQ (
        voltageRangeHandler
            ->getValueInterface()
            ->getCurrentValueAsString(),
        "333");

    const auto stableRangeId =
        voltageRange
            ->getComponentID();
    options->setSelectedType (
        ContinuousChannel::Type::
            AUX);
    EXPECT_EQ (
        voltageRangeHandler
            ->getValueInterface()
            ->getCurrentValueAsString(),
        "Auto");
    EXPECT_EQ (
        voltageRangeHandler
            ->getDescription(),
        "Choose the AUX voltage range shown in LFP display 1, in mV, or Auto.");
    EXPECT_EQ (
        voltageRangeHandler
            ->getHelp(),
        "Choose the AUX voltage range shown in LFP display 1, in mV, or Auto.");
    options->setSelectedType (
        ContinuousChannel::Type::
            ADC);
    EXPECT_EQ (
        voltageRangeHandler
            ->getValueInterface()
            ->getCurrentValueAsString(),
        "10.0");
    EXPECT_EQ (
        voltageRangeHandler
            ->getDescription(),
        "Choose the ADC voltage range shown in LFP display 1, in V.");
    EXPECT_EQ (
        voltageRangeHandler
            ->getHelp(),
        "Choose the ADC voltage range shown in LFP display 1, in V.");
    EXPECT_EQ (
        voltageRange
            ->getComponentID(),
        stableRangeId);
}

#if JUCE_WINDOWS
TEST_F (LfpDisplayNodeTests,
        WindowsUiaWorkerExpandsAndCollapsesStreamSelector)
{
    auto canvas =
        std::make_unique<
            LfpViewer::
                LfpDisplayCanvas> (
            processor,
            LfpViewer::
                SplitLayouts::SINGLE,
            false);
    canvas->updateSettings();
    canvas->setSize (600, 800);
    canvas->addToDesktop (0);
    canvas->setVisible (true);
    ASSERT_TRUE (
        canvas->isShowing());

    const auto id =
        "oe.processor."
        + String (
            processor->getNodeId())
        + ".lfp.display_1.stream";
    auto* selector =
        dynamic_cast<
            MessageThreadComboBox*> (
            findLfpDescendantById (
                *canvas,
                id));
    ASSERT_NE (selector, nullptr);
    const auto expectedValue =
        processor
            ->getDisplayBuffers()[0]
            ->name;
    const std::wstring wideId (
        id.toWideCharPointer());
    const auto window =
        static_cast<HWND> (
            canvas
                ->getWindowHandle());
    ASSERT_NE (window, nullptr);

    const auto invokeUiaAction =
        [&] (
            LfpWindowsUiaAction action,
            bool expectedPopupActive)
    {
        LfpWindowsUiaInvokeResult
            actionResult;
        std::atomic<bool>
            workerReturned { false };
        std::thread worker (
            [&]
            {
                actionResult =
                    invokeLfpWindowsUiaControl (
                        window,
                        wideId,
                        action);
                workerReturned.store (
                    true);
            });
        for (int attempt = 0;
             attempt < 100
                 && (! workerReturned.load()
                     || selector
                                ->isPopupActive()
                            != expectedPopupActive);
             ++attempt)
        {
            MessageManager::getInstance()
                ->runDispatchLoopUntil (
                    10);
        }
        worker.join();
        return actionResult;
    };

    const auto expandResult =
        invokeUiaAction (
            LfpWindowsUiaAction::
                expand,
            true);

    EXPECT_EQ (
        expandResult.invokeResult,
        S_OK);
    EXPECT_EQ (
        expandResult.controlType,
        UIA_ComboBoxControlTypeId);
    EXPECT_EQ (
        expandResult.enabled,
        TRUE);
    EXPECT_EQ (
        expandResult.name,
        L"LFP display 1 stream");
    EXPECT_EQ (
        expandResult.help,
        L"Choose the data stream shown in LFP display 1.");
    EXPECT_EQ (
        expandResult
            .expandCollapsePatternResult,
        S_OK);
    EXPECT_TRUE (
        expandResult
            .expandCollapsePatternAvailable);
    EXPECT_EQ (
        expandResult.valuePatternResult,
        S_OK);
    EXPECT_TRUE (
        expandResult
            .valuePatternAvailable);
    EXPECT_EQ (
        expandResult.value,
        std::wstring (
            expectedValue
                .toWideCharPointer()));
    EXPECT_EQ (
        expandResult.expansionState,
        ExpandCollapseState_Expanded);
    EXPECT_TRUE (
        selector->isPopupActive());

    const auto repeatedExpandResult =
        invokeUiaAction (
            LfpWindowsUiaAction::
                expand,
            true);
    EXPECT_EQ (
        repeatedExpandResult
            .invokeResult,
        S_OK);
    EXPECT_EQ (
        repeatedExpandResult
            .expansionState,
        ExpandCollapseState_Expanded);
    EXPECT_TRUE (
        selector->isPopupActive());

    const auto collapseResult =
        invokeUiaAction (
            LfpWindowsUiaAction::
                collapse,
            false);
    EXPECT_EQ (
        collapseResult.invokeResult,
        S_OK);
    EXPECT_EQ (
        collapseResult.expansionState,
        ExpandCollapseState_Collapsed);
    EXPECT_FALSE (
        selector->isPopupActive());

    const auto repeatedCollapseResult =
        invokeUiaAction (
            LfpWindowsUiaAction::
                collapse,
            false);
    EXPECT_EQ (
        repeatedCollapseResult
            .invokeResult,
        S_OK);
    EXPECT_EQ (
        repeatedCollapseResult
            .expansionState,
        ExpandCollapseState_Collapsed);
    EXPECT_FALSE (
        selector->isPopupActive());

    selector->setEnabled (false);
    const auto disabledExpandResult =
        invokeUiaAction (
            LfpWindowsUiaAction::
                expand,
            false);
    EXPECT_EQ (
        disabledExpandResult
            .invokeResult,
        static_cast<HRESULT> (
            UIA_E_ELEMENTNOTENABLED));
    EXPECT_FALSE (
        selector->isPopupActive());
    selector->setEnabled (true);

    auto* handler =
        selector
            ->getAccessibilityHandler();
    ASSERT_NE (handler, nullptr);
    EXPECT_TRUE (
        handler->getCurrentState()
            .isCollapsed());
}

TEST_F (LfpDisplayNodeTests,
        WindowsUiaWorkerWritesCoreDisplayParameters)
{
    auto canvas =
        std::make_unique<
            LfpViewer::
                LfpDisplayCanvas> (
            processor,
            LfpViewer::
                SplitLayouts::SINGLE,
            false);
    canvas->updateSettings();
    canvas->setSize (600, 800);
    canvas->addToDesktop (0);
    canvas->setVisible (true);
    ASSERT_TRUE (
        canvas->isShowing());

    const auto id =
        "oe.processor."
        + String (
            processor->getNodeId())
        + ".lfp.display_1.timebase";
    auto* timebase =
        dynamic_cast<
            MessageThreadComboBox*> (
            findLfpDescendantById (
                *canvas,
                id));
    ASSERT_NE (
        timebase,
        nullptr);
    const auto channelHeightId =
        "oe.processor."
        + String (
            processor->getNodeId())
        + ".lfp.display_1.channel_height";
    const auto voltageRangeId =
        "oe.processor."
        + String (
            processor->getNodeId())
        + ".lfp.display_1.voltage_range";
    auto* channelHeight =
        dynamic_cast<
            MessageThreadComboBox*> (
            findLfpDescendantById (
                *canvas,
                channelHeightId));
    auto* voltageRange =
        dynamic_cast<
            MessageThreadComboBox*> (
            findLfpDescendantById (
                *canvas,
                voltageRangeId));
    ASSERT_NE (
        channelHeight,
        nullptr);
    ASSERT_NE (
        voltageRange,
        nullptr);
    const auto window =
        static_cast<HWND> (
            canvas
                ->getWindowHandle());
    ASSERT_NE (
        window,
        nullptr);

    const auto writeValue =
        [&] (
            StringRef targetId,
            const std::wstring& value)
    {
        LfpWindowsUiaInvokeResult
            actionResult;
        std::atomic<bool>
            workerReturned { false };
        std::thread worker (
            [&]
            {
                actionResult =
                    invokeLfpWindowsUiaControl (
                        window,
                        std::wstring (
                            String (
                                targetId)
                                .toWideCharPointer()),
                        LfpWindowsUiaAction::
                            setValue,
                        value);
                workerReturned.store (
                    true);
            });
        for (int attempt = 0;
             attempt < 100
                 && ! workerReturned.load();
             ++attempt)
        {
            MessageManager::getInstance()
                ->runDispatchLoopUntil (
                    10);
        }
        worker.join();
        EXPECT_TRUE (
            workerReturned.load());
        return actionResult;
    };

    const auto result =
        writeValue (
            id,
            L"0.75");
    EXPECT_EQ (
        result.invokeResult,
        S_OK);
    EXPECT_EQ (
        result.valuePatternResult,
        S_OK);
    EXPECT_TRUE (
        result
            .valuePatternAvailable);
    EXPECT_EQ (
        result.value,
        L"0.8");
    EXPECT_EQ (
        timebase->getText(),
        "0.8");

    auto* options =
        findLfpAncestor<
            LfpViewer::
                LfpDisplayOptions> (
            *timebase);
    ASSERT_NE (
        options,
        nullptr);
    options->setPausedState (
        true);
    const auto disabledResult =
        writeValue (
            id,
            L"1.0");
    EXPECT_EQ (
        disabledResult.invokeResult,
        static_cast<HRESULT> (
            UIA_E_ELEMENTNOTENABLED));
    EXPECT_EQ (
        timebase->getText(),
        "0.8");
    options->setPausedState (
        false);

    const auto clampedHeightResult =
        writeValue (
            channelHeightId,
            L"2");
    EXPECT_EQ (
        clampedHeightResult
            .invokeResult,
        S_OK);
    EXPECT_EQ (
        clampedHeightResult.value,
        L"6");
    EXPECT_EQ (
        channelHeight->getText(),
        "6");

    const auto clampedRangeResult =
        writeValue (
            voltageRangeId,
            L"10");
    EXPECT_EQ (
        clampedRangeResult
            .invokeResult,
        S_OK);
    EXPECT_EQ (
        clampedRangeResult.value,
        L"25");
    EXPECT_EQ (
        voltageRange->getText(),
        "25");

    const auto customRangeResult =
        writeValue (
            voltageRangeId,
            L"333");
    EXPECT_EQ (
        customRangeResult
            .invokeResult,
        S_OK);
    EXPECT_EQ (
        customRangeResult.value,
        L"333");
    EXPECT_EQ (
        voltageRange->getText(),
        "333");

    options->setSelectedType (
        ContinuousChannel::Type::
            AUX);
    const auto auxAutoResult =
        writeValue (
            voltageRangeId,
            L"Auto");
    EXPECT_EQ (
        auxAutoResult
            .invokeResult,
        S_OK);
    EXPECT_EQ (
        auxAutoResult.value,
        L"Auto");
    EXPECT_EQ (
        auxAutoResult.help,
        L"Choose the AUX voltage range shown in LFP display 1, in mV, or Auto.");
    EXPECT_EQ (
        voltageRange->getText(),
        "Auto");

    const auto hiddenPaneResult =
        writeValue (
            "oe.processor."
                + String (
                    processor
                        ->getNodeId())
                + ".lfp.display_2.timebase",
            L"1.0");
    EXPECT_EQ (
        hiddenPaneResult
            .invokeResult,
        E_FAIL);
}

TEST_F (LfpDisplayNodeTests,
        WindowsUiaWorkerSetsColourGroupingByShank)
{
    auto canvas =
        std::make_unique<
            LfpViewer::
                LfpDisplayCanvas> (
            processor,
            LfpViewer::
                SplitLayouts::SINGLE,
            false);
    canvas->updateSettings();
    canvas->setSize (1200, 800);
    canvas->addToDesktop (0);
    canvas->setVisible (true);
    ASSERT_TRUE (
        canvas->isShowing());

    const auto prefix =
        "oe.processor."
        + String (
            processor->getNodeId())
        + ".lfp.display_1.";
    const auto id =
        prefix
        + "colour_grouping";
    auto* grouping =
        dynamic_cast<
            MessageThreadComboBox*> (
            findLfpDescendantById (
                *canvas,
                id));
    auto* display =
        findLfpDescendant<
            LfpViewer::
                LfpDisplay> (
            *canvas);
    ASSERT_NE (
        grouping,
        nullptr);
    ASSERT_NE (
        display,
        nullptr);
    const auto window =
        static_cast<HWND> (
            canvas
                ->getWindowHandle());
    ASSERT_NE (
        window,
        nullptr);

    const auto runAction =
        [&] (
            StringRef targetId,
            LfpWindowsUiaAction action,
            const std::wstring&
                valueToSet = {})
    {
        LfpWindowsUiaInvokeResult
            actionResult;
        std::atomic<bool>
            workerReturned { false };
        std::thread worker (
            [&]
            {
                actionResult =
                    invokeLfpWindowsUiaControl (
                        window,
                        std::wstring (
                            String (
                                targetId)
                                .toWideCharPointer()),
                        action,
                        valueToSet);
                workerReturned.store (
                    true);
            });
        for (int attempt = 0;
             attempt < 100
                 && ! workerReturned.load();
             ++attempt)
        {
            MessageManager::getInstance()
                ->runDispatchLoopUntil (
                    10);
        }
        joinLfpWorkerOrAbort (
            worker,
            workerReturned);
        return actionResult;
    };

    MessageManager::getInstance()
        ->runDispatchLoopUntil (
            30);
    LfpThreadTrackingComboBoxListener
        listener;
    grouping->addListener (
        &listener);
    const auto selectedResult =
        runAction (
            id,
            LfpWindowsUiaAction::
                setValue,
            L"By Shank");
    grouping->removeListener (
        &listener);

    EXPECT_EQ (
        selectedResult.invokeResult,
        S_OK);
    EXPECT_EQ (
        selectedResult.controlType,
        UIA_ComboBoxControlTypeId);
    EXPECT_EQ (
        selectedResult.enabled,
        TRUE);
    EXPECT_EQ (
        selectedResult.name,
        L"LFP display 1 colour grouping");
    EXPECT_EQ (
        selectedResult.help,
        L"Choose how LFP display 1 assigns display colours: 1, 2, 4, 8, or 16 groups that many adjacent channels, while By Shank uses channel group metadata with a legacy depth-based fallback. This changes display colours only; acquisition and recording are unaffected.");
    EXPECT_EQ (
        selectedResult
            .valuePatternResult,
        S_OK);
    EXPECT_TRUE (
        selectedResult
            .valuePatternAvailable);
    EXPECT_EQ (
        selectedResult
            .valueReadOnly,
        FALSE);
    EXPECT_EQ (
        selectedResult.value,
        L"By Shank");
    EXPECT_EQ (
        selectedResult
            .expandCollapsePatternResult,
        S_OK);
    EXPECT_TRUE (
        selectedResult
            .expandCollapsePatternAvailable);
    EXPECT_EQ (
        listener
            .callbackCount
            .load(),
        1);
    EXPECT_TRUE (
        listener
            .callbackUsedMessageThread
            .load());
    EXPECT_EQ (
        grouping->getSelectedId(),
        6);
    EXPECT_EQ (
        display
            ->getColourGrouping(),
        "By Shank");

    const auto invalidValueResult =
        runAction (
            id,
            LfpWindowsUiaAction::
                setValue,
            L"unsupported");
    EXPECT_EQ (
        invalidValueResult
            .invokeResult,
        E_INVALIDARG);
    EXPECT_EQ (
        grouping->getSelectedId(),
        6);
    EXPECT_EQ (
        display
            ->getColourGrouping(),
        "By Shank");

    const auto expandedResult =
        runAction (
            id,
            LfpWindowsUiaAction::
                expand);
    EXPECT_EQ (
        expandedResult.invokeResult,
        S_OK);
    EXPECT_EQ (
        expandedResult
            .expansionState,
        ExpandCollapseState_Expanded);
    EXPECT_TRUE (
        grouping->isPopupActive());
    const auto collapsedResult =
        runAction (
            id,
            LfpWindowsUiaAction::
                collapse);
    EXPECT_EQ (
        collapsedResult.invokeResult,
        S_OK);
    EXPECT_EQ (
        collapsedResult
            .expansionState,
        ExpandCollapseState_Collapsed);
    EXPECT_FALSE (
        grouping->isPopupActive());

    grouping->setEnabled (
        false);
    const auto disabledValueResult =
        runAction (
            id,
            LfpWindowsUiaAction::
                setValue,
            L"8");
    EXPECT_EQ (
        disabledValueResult
            .invokeResult,
        static_cast<HRESULT> (
            UIA_E_ELEMENTNOTENABLED));
    const auto disabledExpandResult =
        runAction (
            id,
            LfpWindowsUiaAction::
                expand);
    EXPECT_EQ (
        disabledExpandResult
            .invokeResult,
        static_cast<HRESULT> (
            UIA_E_ELEMENTNOTENABLED));
    EXPECT_EQ (
        grouping->getSelectedId(),
        6);
    EXPECT_EQ (
        display
            ->getColourGrouping(),
        "By Shank");

    const auto hiddenResult =
        runAction (
            prefix
                .replace (
                    "display_1.",
                    "display_2.")
                + "colour_grouping",
            LfpWindowsUiaAction::
                setValue,
            L"8");
    EXPECT_EQ (
        hiddenResult.invokeResult,
        E_FAIL);
}

TEST_F (LfpDisplayNodeTests,
        WindowsUiaWorkerReadsLatestNonzeroTtlWord)
{
    auto canvas =
        std::make_unique<
            LfpViewer::
                LfpDisplayCanvas> (
            processor,
            LfpViewer::
                SplitLayouts::SINGLE,
            false);
    canvas->updateSettings();
    canvas->setSize (1200, 800);
    canvas->addToDesktop (0);
    canvas->setVisible (true);
    ASSERT_TRUE (
        canvas->isShowing());

    const auto prefix =
        "oe.processor."
        + String (
            processor->getNodeId())
        + ".lfp.display_";
    const auto id =
        prefix
        + "1.ttl_word";
    auto* label =
        dynamic_cast<Label*> (
            findLfpDescendantById (
                *canvas,
                id));
    ASSERT_NE (
        label,
        nullptr);
    auto* options =
        findLfpAncestor<
            LfpViewer::
                LfpDisplayOptions> (
            *label);
    ASSERT_NE (
        options,
        nullptr);
    const auto window =
        static_cast<HWND> (
            canvas
                ->getWindowHandle());
    ASSERT_NE (
        window,
        nullptr);

    const auto runAction =
        [&] (
            StringRef targetId,
            LfpWindowsUiaAction action,
            StringRef value = {})
    {
        LfpWindowsUiaInvokeResult
            actionResult;
        std::atomic<bool>
            workerReturned { false };
        std::thread worker (
            [&]
            {
                actionResult =
                    invokeLfpWindowsUiaControl (
                        window,
                        std::wstring (
                            String (
                                targetId)
                                .toWideCharPointer()),
                        action,
                        std::wstring (
                            String (value)
                                .toWideCharPointer()));
                workerReturned.store (
                    true);
            });
        for (int attempt = 0;
             attempt < 100
                 && ! workerReturned.load();
             ++attempt)
        {
            MessageManager::getInstance()
                ->runDispatchLoopUntil (
                    10);
        }
        joinLfpWorkerOrAbort (
            worker,
            workerReturned);
        return actionResult;
    };

    const auto initialResult =
        runAction (
            id,
            LfpWindowsUiaAction::
                queryValue);
    EXPECT_EQ (
        initialResult.invokeResult,
        S_OK);
    EXPECT_EQ (
        initialResult.controlType,
        UIA_TextControlTypeId);
    EXPECT_EQ (
        initialResult.enabled,
        TRUE);
    EXPECT_EQ (
        initialResult.name,
        L"LFP display 1 latest non-zero TTL word");
    EXPECT_EQ (
        initialResult.help,
        L"Latest non-zero 64-bit TTL word received while its stream was selected in LFP display 1, shown as an unsigned decimal value. NONE means no non-zero word has been received by this display pane.");
    EXPECT_TRUE (
        initialResult
            .valuePatternAvailable);
    EXPECT_EQ (
        initialResult.valueReadOnly,
        TRUE);
    EXPECT_EQ (
        initialResult.value,
        L"NONE");
    EXPECT_FALSE (
        initialResult
            .invokePatternAvailable);
    EXPECT_FALSE (
        initialResult
            .togglePatternAvailable);

    options->setTTLWord (
        4294967297ULL);
    options->timerCallback();
    const auto updatedResult =
        runAction (
            id,
            LfpWindowsUiaAction::
                queryValue);
    EXPECT_EQ (
        updatedResult.invokeResult,
        S_OK);
    EXPECT_EQ (
        updatedResult.name,
        initialResult.name);
    EXPECT_EQ (
        updatedResult.value,
        L"4294967297");

    const auto writeResult =
        runAction (
            id,
            LfpWindowsUiaAction::
                setValue,
            "7");
    EXPECT_EQ (
        writeResult.invokeResult,
        static_cast<HRESULT> (
            UIA_E_INVALIDOPERATION));
    EXPECT_EQ (
        label->getText(),
        "4294967297");

    const auto hiddenPaneResult =
        runAction (
            prefix
                + "2.ttl_word",
            LfpWindowsUiaAction::
                queryValue);
    EXPECT_EQ (
        hiddenPaneResult.invokeResult,
        E_FAIL);
}

TEST_F (LfpDisplayNodeTests,
        WindowsUiaWorkerTogglesEventOverlayLine)
{
    auto canvas =
        std::make_unique<
            LfpViewer::
                LfpDisplayCanvas> (
            processor,
            LfpViewer::
                SplitLayouts::SINGLE,
            false);
    canvas->updateSettings();
    canvas->setSize (1200, 800);
    canvas->addToDesktop (0);
    canvas->setVisible (true);
    ASSERT_TRUE (
        canvas->isShowing());

    const auto prefix =
        "oe.processor."
        + String (
            processor->getNodeId())
        + ".lfp.display_";
    const auto id =
        prefix
        + "1.event_overlay.line_1";
    auto* button =
        dynamic_cast<Button*> (
            findLfpDescendantById (
                *canvas,
                id));
    ASSERT_NE (
        button,
        nullptr);
    auto* eventInterface =
        findLfpAncestor<
            LfpViewer::
                EventDisplayInterface> (
            *button);
    ASSERT_NE (
        eventInterface,
        nullptr);
    const auto window =
        static_cast<HWND> (
            canvas
                ->getWindowHandle());
    ASSERT_NE (
        window,
        nullptr);

    const auto runAction =
        [&] (
            StringRef targetId,
            LfpWindowsUiaAction action,
            bool expectedShown)
    {
        LfpWindowsUiaInvokeResult
            actionResult;
        std::atomic<bool>
            workerReturned { false };
        std::thread worker (
            [&]
            {
                actionResult =
                    invokeLfpWindowsUiaControl (
                        window,
                        std::wstring (
                            String (
                                targetId)
                                .toWideCharPointer()),
                        action);
                workerReturned.store (
                    true);
            });
        for (int attempt = 0;
             attempt < 100
                 && (! workerReturned.load()
                     || eventInterface
                                ->getEventDisplayState()
                            != expectedShown);
             ++attempt)
        {
            MessageManager::getInstance()
                ->runDispatchLoopUntil (
                    10);
        }
        joinLfpWorkerOrAbort (
            worker,
            workerReturned);
        return actionResult;
    };

    const auto initialResult =
        runAction (
            id,
            LfpWindowsUiaAction::
                queryToggle,
            true);
    EXPECT_EQ (
        initialResult.invokeResult,
        S_OK);
    EXPECT_EQ (
        initialResult.controlType,
        UIA_CheckBoxControlTypeId);
    EXPECT_EQ (
        initialResult.enabled,
        TRUE);
    EXPECT_EQ (
        initialResult.name,
        L"LFP display 1 event overlay line 1");
    EXPECT_EQ (
        initialResult.help,
        L"Show or hide TTL line 1 event markers in LFP display 1. This changes the display overlay only; acquisition and recording are unaffected.");
    EXPECT_TRUE (
        initialResult
            .togglePatternAvailable);
    EXPECT_FALSE (
        initialResult
            .invokePatternAvailable);
    EXPECT_TRUE (
        initialResult
            .valuePatternAvailable);
    EXPECT_EQ (
        initialResult.valueReadOnly,
        TRUE);
    EXPECT_EQ (
        initialResult.toggleState,
        ToggleState_On);
    EXPECT_EQ (
        initialResult.value,
        L"Shown");

    const auto hiddenResult =
        runAction (
            id,
            LfpWindowsUiaAction::
                toggle,
            false);
    EXPECT_EQ (
        hiddenResult.invokeResult,
        S_OK);
    EXPECT_EQ (
        hiddenResult.toggleState,
        ToggleState_Off);
    EXPECT_EQ (
        hiddenResult.value,
        L"Hidden");

    button->setEnabled (
        false);
    const auto disabledResult =
        runAction (
            id,
            LfpWindowsUiaAction::
                toggle,
            false);
    EXPECT_EQ (
        disabledResult.invokeResult,
        static_cast<HRESULT> (
            UIA_E_ELEMENTNOTENABLED));
    EXPECT_FALSE (
        eventInterface
            ->getEventDisplayState());

    const auto hiddenPaneResult =
        runAction (
            prefix
                + "2.event_overlay.line_1",
            LfpWindowsUiaAction::
                queryToggle,
            false);
    EXPECT_EQ (
        hiddenPaneResult.invokeResult,
        E_FAIL);
}

TEST_F (LfpDisplayNodeTests,
        WindowsUiaWorkerControlsOptionsDrawer)
{
    auto canvas =
        std::make_unique<
            LfpViewer::
                LfpDisplayCanvas> (
            processor,
            LfpViewer::
                SplitLayouts::SINGLE,
            false);
    canvas->updateSettings();
    canvas->setSize (1200, 800);
    canvas->addToDesktop (0);
    canvas->setVisible (true);
    ASSERT_TRUE (
        canvas->isShowing());

    const auto prefix =
        "oe.processor."
        + String (
            processor->getNodeId())
        + ".lfp.display_";
    const auto id =
        prefix
        + "1.options_drawer";
    auto* button =
        dynamic_cast<Button*> (
            findLfpDescendantById (
                *canvas,
                id));
    ASSERT_NE (
        button,
        nullptr);
    const auto window =
        static_cast<HWND> (
            canvas
                ->getWindowHandle());
    ASSERT_NE (
        window,
        nullptr);

    const auto runAction =
        [&] (
            StringRef targetId,
            LfpWindowsUiaAction action,
            bool expectedOpen)
    {
        LfpWindowsUiaInvokeResult
            actionResult;
        std::atomic<bool>
            workerReturned { false };
        std::thread worker (
            [&]
            {
                actionResult =
                    invokeLfpWindowsUiaControl (
                        window,
                        std::wstring (
                            String (
                                targetId)
                                .toWideCharPointer()),
                        action);
                workerReturned.store (
                    true);
            });
        for (int attempt = 0;
             attempt < 100
                 && (! workerReturned.load()
                     || canvas
                                ->optionsDrawerIsOpen
                            != expectedOpen);
             ++attempt)
        {
            MessageManager::getInstance()
                ->runDispatchLoopUntil (
                    10);
        }
        joinLfpWorkerOrAbort (
            worker,
            workerReturned);
        return actionResult;
    };

    const auto initialResult =
        runAction (
            id,
            LfpWindowsUiaAction::
                queryExpansion,
            false);
    EXPECT_EQ (
        initialResult.invokeResult,
        S_OK);
    EXPECT_EQ (
        initialResult.controlType,
        UIA_ButtonControlTypeId);
    EXPECT_EQ (
        initialResult.enabled,
        TRUE);
    EXPECT_EQ (
        initialResult.name,
        L"LFP display 1 options drawer");
    EXPECT_EQ (
        initialResult.help,
        L"Show or hide advanced options for all LFP displays. Opening the drawer reveals threshold, channel, signal-processing, and triggered-display controls.");
    EXPECT_TRUE (
        initialResult
            .expandCollapsePatternAvailable);
    EXPECT_FALSE (
        initialResult
            .togglePatternAvailable);
    EXPECT_TRUE (
        initialResult
            .invokePatternAvailable);
    EXPECT_EQ (
        initialResult
            .expansionState,
        ExpandCollapseState_Collapsed);

    LfpThreadTrackingButtonListener
        listener;
    button->addListener (
        &listener);
    const auto expandedResult =
        runAction (
            id,
            LfpWindowsUiaAction::
                expand,
            true);
    EXPECT_EQ (
        expandedResult.invokeResult,
        S_OK);
    EXPECT_EQ (
        expandedResult
            .expansionState,
        ExpandCollapseState_Expanded);
    EXPECT_EQ (
        listener
            .callbackCount
            .load(),
        1);
    EXPECT_TRUE (
        listener
            .callbackUsedMessageThread
            .load());

    const auto repeatedExpandResult =
        runAction (
            id,
            LfpWindowsUiaAction::
                expand,
            true);
    EXPECT_EQ (
        repeatedExpandResult
            .invokeResult,
        S_OK);
    EXPECT_EQ (
        listener
            .callbackCount
            .load(),
        1);

    const auto collapsedResult =
        runAction (
            id,
            LfpWindowsUiaAction::
                collapse,
            false);
    EXPECT_EQ (
        collapsedResult.invokeResult,
        S_OK);
    EXPECT_EQ (
        collapsedResult
            .expansionState,
        ExpandCollapseState_Collapsed);
    EXPECT_EQ (
        listener
            .callbackCount
            .load(),
        2);

    const auto toggledResult =
        runAction (
            id,
            LfpWindowsUiaAction::
                toggle,
            false);
    EXPECT_EQ (
        toggledResult.invokeResult,
        E_NOINTERFACE);
    EXPECT_EQ (
        listener
            .callbackCount
            .load(),
        2);

    const auto invokedResult =
        runAction (
            id,
            LfpWindowsUiaAction::
                invoke,
            true);
    EXPECT_EQ (
        invokedResult.invokeResult,
        S_OK);
    EXPECT_EQ (
        listener
            .callbackCount
            .load(),
        3);
    const auto repeatedInvokeResult =
        runAction (
            id,
            LfpWindowsUiaAction::
                invoke,
            false);
    EXPECT_EQ (
        repeatedInvokeResult.invokeResult,
        S_OK);
    EXPECT_EQ (
        listener
            .callbackCount
            .load(),
        4);
    button->removeListener (
        &listener);

    button->setEnabled (
        false);
    for (const auto action :
         { LfpWindowsUiaAction::
               expand,
           LfpWindowsUiaAction::
               invoke })
    {
        const auto disabledResult =
            runAction (
                id,
                action,
                false);
        EXPECT_EQ (
            disabledResult
                .invokeResult,
            static_cast<HRESULT> (
                UIA_E_ELEMENTNOTENABLED));
        EXPECT_FALSE (
            canvas
                ->optionsDrawerIsOpen);
    }

    const auto hiddenResult =
        runAction (
            prefix
                + "2.options_drawer",
            LfpWindowsUiaAction::
                queryExpansion,
            false);
    EXPECT_EQ (
        hiddenResult.invokeResult,
        E_FAIL);

    button->setEnabled (
        true);
    LfpDestroyCanvasButtonListener
        destroyListener (
            canvas);
    button->addListener (
        &destroyListener);
    LfpWindowsUiaInvokeResult
        destroyedResult;
    std::atomic<bool>
        destroyWorkerReturned {
            false
        };
    std::thread destroyWorker (
        [&]
        {
            destroyedResult =
                invokeLfpWindowsUiaControl (
                    window,
                    std::wstring (
                        id
                            .toWideCharPointer()),
                    LfpWindowsUiaAction::
                        invoke);
            destroyWorkerReturned.store (
                true);
        });
    for (int attempt = 0;
         attempt < 100
             && (! destroyWorkerReturned
                       .load()
                 || canvas != nullptr);
         ++attempt)
    {
        MessageManager::getInstance()
            ->runDispatchLoopUntil (
                10);
    }
    joinLfpWorkerOrAbort (
        destroyWorker,
        destroyWorkerReturned);
    EXPECT_EQ (
        canvas,
        nullptr);
    EXPECT_EQ (
        destroyedResult.invokeResult,
        static_cast<HRESULT> (
            UIA_E_ELEMENTNOTAVAILABLE));
}

TEST_F (LfpDisplayNodeTests,
        WindowsUiaWorkerSelectsRangeTypeRadioButton)
{
    auto canvas =
        std::make_unique<
            LfpViewer::
                LfpDisplayCanvas> (
            processor,
            LfpViewer::
                SplitLayouts::SINGLE,
            false);
    canvas->updateSettings();
    canvas->setSize (1200, 800);
    canvas->addToDesktop (0);
    canvas->setVisible (true);
    ASSERT_TRUE (
        canvas->isShowing());

    const auto prefix =
        "oe.processor."
        + String (
            processor->getNodeId())
        + ".lfp.display_1.";
    const auto auxId =
        prefix
        + "channel_type.aux";
    auto* data =
        dynamic_cast<Button*> (
            findLfpDescendantById (
                *canvas,
                prefix
                    + "channel_type.data"));
    auto* aux =
        dynamic_cast<Button*> (
            findLfpDescendantById (
                *canvas,
                auxId));
    auto* adc =
        dynamic_cast<Button*> (
            findLfpDescendantById (
                *canvas,
                prefix
                    + "channel_type.adc"));
    auto* range =
        findLfpDescendantById (
            *canvas,
            prefix
                + "voltage_range");
    ASSERT_NE (data, nullptr);
    ASSERT_NE (aux, nullptr);
    ASSERT_NE (adc, nullptr);
    ASSERT_NE (range, nullptr);
    auto* options =
        findLfpAncestor<
            LfpViewer::
                LfpDisplayOptions> (
            *aux);
    ASSERT_NE (
        options,
        nullptr);
    const auto window =
        static_cast<HWND> (
            canvas
                ->getWindowHandle());
    ASSERT_NE (
        window,
        nullptr);

    const auto runAction =
        [&] (
            StringRef id,
            LfpWindowsUiaAction action,
            ContinuousChannel::Type
                expectedType)
    {
        LfpWindowsUiaInvokeResult
            actionResult;
        std::atomic<bool>
            workerReturned { false };
        std::thread worker (
            [&]
            {
                actionResult =
                    invokeLfpWindowsUiaControl (
                        window,
                        std::wstring (
                            String (id)
                                .toWideCharPointer()),
                        action);
                workerReturned.store (
                    true);
            });
        for (int attempt = 0;
             attempt < 100
                 && (! workerReturned.load()
                     || options
                                ->getSelectedType()
                            != expectedType);
             ++attempt)
        {
            MessageManager::getInstance()
                ->runDispatchLoopUntil (
                    10);
        }
        joinLfpWorkerOrAbort (
            worker,
            workerReturned);
        EXPECT_TRUE (
            workerReturned.load());
        return actionResult;
    };

    aux->setEnabled (
        false);
    const auto disabledResult =
        runAction (
            auxId,
            LfpWindowsUiaAction::
                select,
            ContinuousChannel::Type::
                ELECTRODE);
    EXPECT_EQ (
        disabledResult.invokeResult,
        static_cast<HRESULT> (
            UIA_E_ELEMENTNOTENABLED));
    EXPECT_TRUE (
        data->getToggleState());
    EXPECT_FALSE (
        aux->getToggleState());
    EXPECT_EQ (
        options
            ->getSelectedType(),
        ContinuousChannel::Type::
            ELECTRODE);
    const auto disabledInvokeResult =
        runAction (
            auxId,
            LfpWindowsUiaAction::
                invoke,
            ContinuousChannel::Type::
                ELECTRODE);
    EXPECT_EQ (
        disabledInvokeResult
            .invokeResult,
        static_cast<HRESULT> (
            UIA_E_ELEMENTNOTENABLED));
    EXPECT_TRUE (
        data->getToggleState());
    EXPECT_FALSE (
        aux->getToggleState());
    aux->setEnabled (
        true);

    LfpThreadTrackingButtonListener
        listener;
    aux->addListener (
        &listener);
    const auto auxResult =
        runAction (
            auxId,
            LfpWindowsUiaAction::
                select,
            ContinuousChannel::Type::
                AUX);
    aux->removeListener (
        &listener);

    EXPECT_EQ (
        auxResult.invokeResult,
        S_OK);
    EXPECT_EQ (
        auxResult.controlType,
        UIA_RadioButtonControlTypeId);
    EXPECT_EQ (
        auxResult.enabled,
        TRUE);
    EXPECT_EQ (
        auxResult.name,
        L"LFP display 1 channel type AUX");
    EXPECT_EQ (
        auxResult.help,
        L"Select AUX channels as the target for the voltage range control in LFP display 1.");
    EXPECT_EQ (
        auxResult
            .selectionItemPatternResult,
        S_OK);
    EXPECT_TRUE (
        auxResult
            .selectionItemPatternAvailable);
    EXPECT_EQ (
        auxResult.selected,
        TRUE);
    EXPECT_FALSE (
        auxResult
            .togglePatternAvailable);
    EXPECT_FALSE (
        auxResult
            .valuePatternAvailable);
    EXPECT_EQ (
        auxResult
            .invokePatternResult,
        S_OK);
    EXPECT_TRUE (
        auxResult
            .invokePatternAvailable);
    EXPECT_EQ (
        listener
            .callbackCount
            .load(),
        1);
    EXPECT_TRUE (
        listener
            .callbackUsedMessageThread
            .load());
    EXPECT_FALSE (
        data->getToggleState());
    EXPECT_TRUE (
        aux->getToggleState());
    EXPECT_FALSE (
        adc->getToggleState());
    EXPECT_EQ (
        range
            ->getAccessibilityHandler()
            ->getValueInterface()
            ->getCurrentValueAsString(),
        "Auto");
    EXPECT_EQ (
        range
            ->getAccessibilityHandler()
            ->getHelp(),
        "Choose the AUX voltage range shown in LFP display 1, in mV, or Auto.");

    const auto hiddenResult =
        runAction (
            prefix
                .replace (
                    "display_1.",
                    "display_2.")
                + "channel_type.aux",
            LfpWindowsUiaAction::
                querySelection,
            ContinuousChannel::Type::
                AUX);
    EXPECT_EQ (
        hiddenResult.invokeResult,
        E_FAIL);
}

TEST_F (LfpDisplayNodeTests,
        WindowsUiaInvokeReportsDestroyedRangeTypeControl)
{
    auto canvas =
        std::make_unique<
            LfpViewer::
                LfpDisplayCanvas> (
            processor,
            LfpViewer::
                SplitLayouts::SINGLE,
            false);
    canvas->updateSettings();
    canvas->setSize (1200, 800);
    canvas->addToDesktop (0);
    canvas->setVisible (true);
    ASSERT_TRUE (
        canvas->isShowing());

    const auto id =
        "oe.processor."
        + String (
            processor->getNodeId())
        + ".lfp.display_1.channel_type.aux";
    auto* aux =
        dynamic_cast<Button*> (
            findLfpDescendantById (
                *canvas,
                id));
    ASSERT_NE (aux, nullptr);
    const auto window =
        static_cast<HWND> (
            canvas
                ->getWindowHandle());
    ASSERT_NE (
        window,
        nullptr);

    LfpDestroyCanvasButtonListener
        listener (canvas);
    aux->addListener (
        &listener);

    LfpWindowsUiaInvokeResult
        actionResult;
    std::atomic<bool>
        workerReturned { false };
    std::thread worker (
        [&]
        {
            actionResult =
                invokeLfpWindowsUiaControl (
                    window,
                    std::wstring (
                        id
                            .toWideCharPointer()),
                    LfpWindowsUiaAction::
                        invoke);
            workerReturned.store (
                true);
        });
    for (int attempt = 0;
         attempt < 100
             && (! workerReturned.load()
                 || canvas != nullptr);
         ++attempt)
    {
        MessageManager::getInstance()
            ->runDispatchLoopUntil (
                10);
    }
    joinLfpWorkerOrAbort (
        worker,
        workerReturned);

    EXPECT_EQ (
        canvas,
        nullptr);
    EXPECT_EQ (
        actionResult.invokeResult,
        static_cast<HRESULT> (
            UIA_E_ELEMENTNOTAVAILABLE));
}

TEST_F (LfpDisplayNodeTests,
        WindowsUiaWorkerTogglesPause)
{
    auto canvas =
        std::make_unique<
            LfpViewer::
                LfpDisplayCanvas> (
            processor,
            LfpViewer::
                SplitLayouts::SINGLE,
            false);
    canvas->updateSettings();
    canvas->setSize (1200, 800);
    canvas->addToDesktop (0);
    canvas->setVisible (true);
    ASSERT_TRUE (
        canvas->isShowing());

    const auto prefix =
        "oe.processor."
        + String (
            processor->getNodeId())
        + ".lfp.display_1.";
    const auto id =
        prefix + "pause";
    auto* pause =
        dynamic_cast<Button*> (
            findLfpDescendantById (
                *canvas,
                id));
    auto* timebase =
        findLfpDescendantById (
            *canvas,
            prefix
                + "timebase");
    ASSERT_NE (
        pause,
        nullptr);
    ASSERT_NE (
        timebase,
        nullptr);
    const auto window =
        static_cast<HWND> (
            canvas
                ->getWindowHandle());
    ASSERT_NE (
        window,
        nullptr);
    const auto wideId =
        std::wstring (
            id.toWideCharPointer());

    const auto runAction =
        [&] (
            LfpWindowsUiaAction action,
            bool expectedPaused)
    {
        LfpWindowsUiaInvokeResult
            actionResult;
        std::atomic<bool>
            workerReturned { false };
        std::thread worker (
            [&]
            {
                actionResult =
                    invokeLfpWindowsUiaControl (
                        window,
                        wideId,
                        action);
                workerReturned.store (
                    true);
            });
        for (int attempt = 0;
             attempt < 100
                 && (! workerReturned.load()
                     || pause
                                ->getToggleState()
                            != expectedPaused);
             ++attempt)
        {
            MessageManager::getInstance()
                ->runDispatchLoopUntil (
                    10);
        }
        worker.join();
        EXPECT_TRUE (
            workerReturned.load());
        return actionResult;
    };

    const auto pausedResult =
        runAction (
            LfpWindowsUiaAction::
                toggle,
            true);
    EXPECT_EQ (
        pausedResult.invokeResult,
        S_OK);
    EXPECT_EQ (
        pausedResult.controlType,
        UIA_CheckBoxControlTypeId);
    EXPECT_EQ (
        pausedResult.enabled,
        TRUE);
    EXPECT_EQ (
        pausedResult.name,
        L"LFP display 1 pause");
    EXPECT_EQ (
        pausedResult.help,
        L"Pause or resume live updates in LFP display 1. Acquisition and recording continue.");
    EXPECT_EQ (
        pausedResult
            .togglePatternResult,
        S_OK);
    EXPECT_TRUE (
        pausedResult
            .togglePatternAvailable);
    EXPECT_EQ (
        pausedResult
            .toggleState,
        ToggleState_On);
    EXPECT_EQ (
        pausedResult
            .valuePatternResult,
        S_OK);
    EXPECT_TRUE (
        pausedResult
            .valuePatternAvailable);
    EXPECT_EQ (
        pausedResult
            .valueReadOnly,
        TRUE);
    EXPECT_EQ (
        pausedResult.value,
        L"Paused");
    EXPECT_TRUE (
        pause->getToggleState());
    EXPECT_FALSE (
        timebase->isEnabled());

    const auto runningResult =
        runAction (
            LfpWindowsUiaAction::
                toggle,
            false);
    EXPECT_EQ (
        runningResult.invokeResult,
        S_OK);
    EXPECT_EQ (
        runningResult
            .toggleState,
        ToggleState_Off);
    EXPECT_EQ (
        runningResult.value,
        L"Running");
    EXPECT_FALSE (
        pause->getToggleState());
    EXPECT_TRUE (
        timebase->isEnabled());

    pause->setEnabled (
        false);
    const auto disabledResult =
        runAction (
            LfpWindowsUiaAction::
                toggle,
            false);
    EXPECT_EQ (
        disabledResult.invokeResult,
        static_cast<HRESULT> (
            UIA_E_ELEMENTNOTENABLED));
    EXPECT_FALSE (
        pause->getToggleState());

    LfpWindowsUiaInvokeResult
        hiddenPaneResult;
    std::atomic<bool>
        hiddenWorkerReturned { false };
    std::thread hiddenWorker (
        [&]
        {
            hiddenPaneResult =
                invokeLfpWindowsUiaControl (
                    window,
                    std::wstring (
                        (prefix
                         .replace (
                             "display_1.",
                             "display_2.")
                         + "pause")
                            .toWideCharPointer()),
                    LfpWindowsUiaAction::
                        queryToggle);
            hiddenWorkerReturned.store (
                true);
        });
    for (int attempt = 0;
         attempt < 100
             && ! hiddenWorkerReturned
                       .load();
         ++attempt)
    {
        MessageManager::getInstance()
            ->runDispatchLoopUntil (
                10);
    }
    hiddenWorker.join();
    EXPECT_TRUE (
        hiddenWorkerReturned.load());
    EXPECT_EQ (
        hiddenPaneResult.invokeResult,
        E_FAIL);
}
#endif

TEST_F (LfpDisplayNodeTests, VisualIntegrityTest)
{
    const int canvasX = 600;
    const int canvasY = 800;
    const float errorThreshold = 0.05f; //5% error threshold

    //Initialize LFP Canvas
    std::unique_ptr<LfpViewer::LfpDisplayCanvas> canvas = std::make_unique<LfpViewer::LfpDisplayCanvas> (processor, LfpViewer::SplitLayouts::SINGLE, false);
    canvas->updateSettings();
    canvas->setSize (canvasX, canvasY);
    canvas->resized();
    canvas->setVisible (true);

    //Get LFP size parameters from canvas
    setExpectedImageParameters (canvas.get());

    //Create snapshot of canvas channel bitmap and expected image
    juce::Rectangle<int> canvasSnapshot (
        x,
        y,
        width,
        height);
    ExpectedImage expected (numChannels, sampleRate * 2); //2 seconds to match canvas timebase

    processor->startAcquisition ();
    canvas->beginAnimation();

    //Add 5 10Hz waves with +-125uV amplitude
    auto inputBuffer = createBufferSinusoidal (5, numChannels, 1000, 125);
    writeBlock (inputBuffer);
    expected.addToBuffer (inputBuffer);
    canvas->refreshState();
    Image canvasImage = canvas->createComponentSnapshot (canvasSnapshot);
    Image expectedImage = expected.getImage (width, height, channelColours, backgroundColour, midlineColour, 125);
    int missCount = getImageDifferencePixelCount (expectedImage, canvasImage);
    EXPECT_LE (float(missCount) / float(width * height), errorThreshold);

    //Add 5 10Hz waves with +-250uV amplitude
    inputBuffer = createBufferSinusoidal (5, numChannels, 1000, 250);
    writeBlock (inputBuffer);
    expected.addToBuffer (inputBuffer);
    canvas->refreshState();

    canvasImage = canvas->createComponentSnapshot (canvasSnapshot);
    expectedImage = expected.getImage (width, height, channelColours, backgroundColour, midlineColour, 125);
    missCount = getImageDifferencePixelCount (expectedImage, canvasImage);
    EXPECT_LE (float (missCount) / float (width * height), errorThreshold);

    //Add 10 40Hz waves with +-250uV amplitude
    inputBuffer = createBufferSinusoidal (10, numChannels, 1000, 250);
    writeBlock (inputBuffer);
    expected.addToBuffer (inputBuffer);
    canvas->refreshState();

    canvasImage = canvas->createComponentSnapshot (canvasSnapshot);
    expectedImage = expected.getImage (width, height, channelColours, backgroundColour, midlineColour, 125);
    missCount = getImageDifferencePixelCount (expectedImage, canvasImage);
    EXPECT_LE (float (missCount) / float (width * height), errorThreshold);

    //Resize canvas to have half the vertical height and twice the uV range
    canvas->setChannelHeight (0, 20);
    canvas->setChannelRange (0, 500, ContinuousChannel::Type::ELECTRODE);
    canvas->refreshState();
    setExpectedImageParameters (canvas.get());

    canvasSnapshot.setBounds (x, y, width, height);

    canvasImage = canvas->createComponentSnapshot (canvasSnapshot);
    expectedImage = expected.getImage (width, height, channelColours, backgroundColour, midlineColour, 125);
    missCount = getImageDifferencePixelCount (expectedImage, canvasImage);
    EXPECT_LE (float (missCount) / float (width * height), errorThreshold);

    processor->stopAcquisition();
}

TEST_F (LfpDisplayNodeTests, DataIntegrityTest)
{
    int numSamples = 100;
    processor->startAcquisition ();

    auto inputBuffer = createBuffer (1000.0, 20.0, numChannels, numSamples);
    writeBlock (inputBuffer);

    processor->stopAcquisition();
}
