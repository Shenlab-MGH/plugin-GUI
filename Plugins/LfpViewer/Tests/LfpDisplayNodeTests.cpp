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
    select,
    querySelection
};

struct LfpWindowsUiaInvokeResult
{
    HRESULT invokeResult = E_PENDING;
    HRESULT togglePatternResult =
        E_PENDING;
    HRESULT selectionItemPatternResult =
        E_PENDING;
    bool togglePatternAvailable = false;
    bool selectionItemPatternAvailable =
        false;
    CONTROLTYPEID controlType = 0;
    BOOL enabled = FALSE;
    BOOL selected = FALSE;
    std::wstring name;
    std::wstring help;
};

LfpWindowsUiaInvokeResult
invokeLfpWindowsUiaControl (
    HWND window,
    const std::wstring& automationId,
    LfpWindowsUiaAction action)
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
