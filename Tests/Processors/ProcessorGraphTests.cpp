#include "gtest/gtest.h"
#include <Audio/AudioComponent.h>
#include <Processors/ProcessorGraph/ProcessorGraph.h>
#include <UI/ControlPanel.h>
#include <UI/EditorViewport.h>
#include <modules/juce_gui_basics/juce_gui_basics.h>

#if JUCE_WINDOWS
namespace AccessClass
{
void JUCE_API setEditorViewportForTesting (EditorViewport* viewport);
}
#endif

namespace
{
class AccessibilityGraphTestProcessor : public GenericProcessor
{
public:
    explicit AccessibilityGraphTestProcessor (const String& name)
        : GenericProcessor (name, true)
    {
    }

    void process (AudioBuffer<float>&) override {}
};
} // namespace

class ProcessorGraphTest : public testing::Test
{
protected:
    void SetUp() override
    {
        MessageManager::deleteInstance();
        MessageManager::getInstance();
        AccessClass::clearAccessClassStateForTesting();

        customLookAndFeel = std::make_unique<CustomLookAndFeel>();
        LookAndFeel::setDefaultLookAndFeel (customLookAndFeel.get());

        // All of these sets the global state in AccessClass in their constructors
        audioComponent = std::make_unique<AudioComponent>();
        processorGraph = std::make_unique<ProcessorGraph> (true);
        controlPanel = std::make_unique<ControlPanel> (processorGraph.get(), audioComponent.get(), true);
    }

    void TearDown() override
    {
        controlPanel = nullptr;
        processorGraph = nullptr;
        audioComponent = nullptr;

        AccessClass::clearAccessClassStateForTesting();

        DeletedAtShutdown::deleteAll();
        MessageManager::deleteInstance();
    }

    std::unique_ptr<ProcessorGraph> processorGraph;

private:
    std::unique_ptr<AudioComponent> audioComponent;
    std::unique_ptr<ControlPanel> controlPanel;
    std::unique_ptr<CustomLookAndFeel> customLookAndFeel;
};

TEST_F (ProcessorGraphTest, LoadFromXMLTest)
{
    std::string defaultFileReaderPath = "default";

    File defaultFileReaderPathResources = File (String (RESOURCES_DIRECTORY)).getChildFile ("FileReader/resources/structure.oebin");

    if (defaultFileReaderPathResources.existsAsFile())
    {
        defaultFileReaderPath = defaultFileReaderPathResources.getFullPathName().toStdString();
    }

    // An example XML from a real run of OpenEphys. To generate this, we opened OpenEphys, dragged a FileReader and
    // BandpassFilter, and then did File > Save As... and saved the XML. The XML contents were then copied here.
    std::string docText = R"(<?xml version="1.0" encoding="UTF-8"?>
                            <SETTINGS>
                            <INFO>
                                <VERSION>1.0.0</VERSION>
                                <PLUGIN_API_VERSION>9</PLUGIN_API_VERSION>
                                <DATE>14 March 2025 12:00:00</DATE>
                                <OS>fedora</OS>
                                <MACHINE name="Open-Ephys" cpu_model="11th Gen Intel(R) Core(TM) i7-11700F @ 2.50GHz" cpu_num_cores="16"/>
                            </INFO>
                            <SIGNALCHAIN>
                                <PROCESSOR name="File Reader" insertionPoint="0" pluginName="File Reader"
                                        type="0" index="2" libraryName="" libraryVersion="" processorType="2"
                                        nodeId="100">
                                <PROCESSOR_PARAMETERS selected_file=")"
                                + defaultFileReaderPath + 
                                R"(" active_stream="0" start_time="00:00:00" end_time="00:00:03.999"/>
                                <STREAM name="example_data" description="A description of the File Reader Stream"
                                        sample_rate="40000.0" channel_count="16">
                                    <PARAMETERS enable_stream="1"/>
                                </STREAM>
                                <CUSTOM_PARAMETERS>
                                    <SCRUBBERINTERFACE show="false"/>
                                </CUSTOM_PARAMETERS>
                                <EDITOR isCollapsed="0" isDrawerOpen="0" displayName="File Reader" activeStream="0"/>
                                </PROCESSOR>
                                <PROCESSOR name="Bandpass Filter" insertionPoint="1" pluginName="Bandpass Filter"
                                        type="1" index="1" libraryName="Bandpass Filter" libraryVersion="1.0.0"
                                        processorType="1" nodeId="101">
                                <PROCESSOR_PARAMETERS threads="1"/>
                                <STREAM name="example_data" description="A description of the File Reader Stream"
                                        sample_rate="40000.0" channel_count="16">
                                    <PARAMETERS enable_stream="1" low_cut="300.0" high_cut="6000.0" channels=""/>
                                </STREAM>
                                <CUSTOM_PARAMETERS/>
                                <EDITOR isCollapsed="0" isDrawerOpen="0" displayName="Bandpass Filter"
                                        activeStream="0"/>
                                </PROCESSOR>
                            </SIGNALCHAIN>
                            <CONTROLPANEL isOpen="0" recordPath="/" recordEngine="BINARY"
                                            clockMode="0" clockReferenceTime="0" forceNewDirectory="0"/>
                            <AUDIOEDITOR isMuted="0" volume="50.0" noiseGate="0.0"/>
                            <FILENAMECONFIG>
                                <PREPEND state="0" value=""/>
                                <MAIN state="1" value="YYYY-MM-DD_HH-MM-SS"/>
                                <APPEND state="0" value=""/>
                            </FILENAMECONFIG>
                            <EDITORVIEWPORT selectedTab="0" scroll="0"/>
                            <GRAPHVIEWER>
                                <NODE id="100" isProcessorInfoVisible="0">
                                <STREAM key="100|example_data" isStreamVisible="0" isParamsVisible="0"/>
                                </NODE>
                                <NODE id="101" isProcessorInfoVisible="0">
                                <STREAM key="100|example_data" isStreamVisible="0" isParamsVisible="0"/>
                                </NODE>
                            </GRAPHVIEWER>
                            <DATAVIEWPORT>
                                <TABBEDCOMPONENT index="0" selectedTabNodeId="1">
                                <TAB nodeId="0"/>
                                <TAB nodeId="1"/>
                                </TABBEDCOMPONENT>
                            </DATAVIEWPORT>
                            <PROCESSORLIST>
                                <COLOUR ID="801" R="50" G="50" B="50"/>
                                <COLOUR ID="804" R="241" G="90" B="41"/>
                                <COLOUR ID="802" R="0" G="160" B="225"/>
                                <COLOUR ID="803" R="0" G="166" B="81"/>
                                <COLOUR ID="805" R="90" G="110" B="110"/>
                                <COLOUR ID="806" R="255" G="0" B="0"/>
                                <COLOUR ID="807" R="0" G="0" B="0"/>
                            </PROCESSORLIST>
                            <UICOMPONENT isProcessorListOpen="1" isEditorViewportOpen="1" consoleOpenInWindow="0"/>
                            <AUDIO sampleRate="44100.0" bufferSize="1024" deviceType="ALSA"/>
                            <MESSAGES/>
                            </SETTINGS>
                            )";

    XmlDocument doc (docText);
    std::unique_ptr<XmlElement> xml = doc.getDocumentElement();
    ASSERT_TRUE (xml);
    ASSERT_TRUE (xml->hasTagName ("SETTINGS"));

    processorGraph->loadFromXml (xml.get());
    auto processors = processorGraph->getListOfProcessors();
    ASSERT_EQ (processors.size(), 2);

    GenericProcessor* bandpassFilter = nullptr;
    GenericProcessor* fileReader = nullptr;
    for (const auto processor : processors)
    {
        if (processor->getName() == "Bandpass Filter")
        {
            bandpassFilter = processor;
        }
        if (processor->getName() == "File Reader")
        {
            fileReader = processor;
        }
    }

    ASSERT_TRUE (bandpassFilter != nullptr);
    ASSERT_TRUE (fileReader != nullptr);

    ASSERT_EQ (fileReader->getSourceNode(), nullptr);
    ASSERT_EQ (fileReader->getDestNode(), bandpassFilter);
    ASSERT_EQ (bandpassFilter->getSourceNode(), fileReader);
    ASSERT_EQ (bandpassFilter->getDestNode(), nullptr);
}

#if JUCE_WINDOWS
class ProcessorGraphAccessibilityWiringTest : public testing::Test
{
protected:
    class ScopedAccessClassEditorViewport
    {
    public:
        explicit ScopedAccessClassEditorViewport (EditorViewport* viewport)
        {
            AccessClass::setEditorViewportForTesting (viewport);
        }

        ~ScopedAccessClassEditorViewport()
        {
            AccessClass::setEditorViewportForTesting (nullptr);
        }
    };

    void SetUp() override
    {
        MessageManager::deleteInstance();
        MessageManager::getInstance();
        AccessClass::clearAccessClassStateForTesting();
        lookAndFeel = std::make_unique<CustomLookAndFeel>();
        LookAndFeel::setDefaultLookAndFeel (lookAndFeel.get());
        processorGraph = std::make_unique<ProcessorGraph> (false);
        tabs = std::make_unique<SignalChainTabComponent>();
        viewport = new EditorViewport (tabs.get());
        accessClassViewport = std::make_unique<ScopedAccessClassEditorViewport> (viewport);
    }

    void TearDown() override
    {
        accessClassViewport.reset();
        tabs.reset();
        viewport = nullptr;
        processorGraph.reset();
        AccessClass::clearAccessClassStateForTesting();
        DeletedAtShutdown::deleteAll();
        LookAndFeel::setDefaultLookAndFeel (nullptr);
        lookAndFeel.reset();
        MessageManager::deleteInstance();
    }

    std::unique_ptr<ProcessorGraph> processorGraph;
    EditorViewport* viewport = nullptr;

private:
    std::unique_ptr<SignalChainTabComponent> tabs;
    std::unique_ptr<ScopedAccessClassEditorViewport> accessClassViewport;
    std::unique_ptr<CustomLookAndFeel> lookAndFeel;
};

TEST_F (ProcessorGraphAccessibilityWiringTest, UpdateViewsPublishesSelectedAndUnselectedProcessorsToViewport)
{
    auto selectedRoot = std::make_unique<AccessibilityGraphTestProcessor> ("Probe 1");
    auto unselectedRoot = std::make_unique<AccessibilityGraphTestProcessor> ("Probe 8");
    auto* selectedRootPointer = selectedRoot.get();
    selectedRootPointer->setNodeId (5101);
    unselectedRoot->setNodeId (5801);
    selectedRootPointer->createEditor();
    unselectedRoot->createEditor();
    processorGraph->addNode (std::move (selectedRoot), AudioProcessorGraph::NodeID (5101));
    processorGraph->addNode (std::move (unselectedRoot), AudioProcessorGraph::NodeID (5801));

    processorGraph->updateViews (selectedRootPointer);

    const auto inventory = viewport->getAccessibleProcessorInventorySnapshot();
    ASSERT_EQ (inventory.size(), 2u);
    EXPECT_EQ (inventory[0].nodeId, 5101);
    EXPECT_EQ (inventory[0].name, "Probe 1");
    EXPECT_EQ (inventory[1].nodeId, 5801);
    EXPECT_EQ (inventory[1].name, "Probe 8");
}
#endif
