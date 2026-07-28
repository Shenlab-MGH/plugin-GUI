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
#include <thread>

#if JUCE_WINDOWS
#include <UIAutomation.h>
#include <wrl/client.h>
#endif

namespace
{
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
    setValue
};

struct LfpWindowsUiaInvokeResult
{
    HRESULT invokeResult = E_PENDING;
    HRESULT togglePatternResult =
        E_PENDING;
    HRESULT selectionItemPatternResult =
        E_PENDING;
    HRESULT expandCollapsePatternResult =
        E_PENDING;
    HRESULT valuePatternResult =
        E_PENDING;
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

LfpWindowsUiaInvokeResult
invokeLfpWindowsUiaControl (
    HWND window,
    const std::wstring& automationId,
    LfpWindowsUiaAction action,
    const std::wstring& valueToSet = {})
{
    LfpWindowsUiaInvokeResult output;
    const auto comResult =
        CoInitializeEx (
            nullptr,
            COINIT_MULTITHREADED);
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
        CoUninitialize();
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

        return finish (
            selectionItemPattern
                ->Select());
    }

    Microsoft::WRL::ComPtr<
        IUIAutomationInvokePattern>
        invokePattern;
    result =
        element
            ->GetCurrentPatternAs (
                UIA_InvokePatternId,
                IID_PPV_ARGS (
                    &invokePattern));
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
        tester = std::make_unique<ProcessorTester> (TestSourceNodeBuilder (FakeSourceNodeParams {
            numChannels,
            sampleRate,
            bitVolts }));

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
