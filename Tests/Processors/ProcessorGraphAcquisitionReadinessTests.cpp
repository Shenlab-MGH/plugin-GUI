#include "../../Source/Processors/ProcessorGraph/ProcessorGraphAcquisitionReadiness.h"

#include "gtest/gtest.h"

#include <string>
#include <vector>

namespace
{
using Code =
    ProcessorGraphAcquisitionReadinessIssueCode;
using Facts =
    ProcessorGraphAcquisitionReadinessFacts;
using Parameter =
    ProcessorGraphAcquisitionReadinessParameter;
using Processor =
    ProcessorGraphAcquisitionReadinessProcessor;

Processor processor (
    const char* name,
    int id,
    bool enabled = true,
    bool ready = true,
    bool nwbUser = false)
{
    Processor result;
    result.name = name;
    result.id = id;
    result.enabled = enabled;
    result.ready = ready;
    result.nwbUser = nwbUser;
    return result;
}

Facts validFacts()
{
    Facts facts;
    facts.nodeCount = 4;
    facts.minimumNodeCount = 4;
    facts.processors = {
        processor ("Source", 101),
        processor ("Record Node", 102),
        processor ("Message Center", 103)
    };
    return facts;
}

struct PresentationSpy
{
    int soundCount = 0;
    int bubbleCount = 0;
    int modalAsyncCount = 0;
    int statusMessageCount = 0;
    int disableCallbacksCount = 0;
    int logCriticalCount = 0;
    String lastBubble;
    String lastModalAsyncTitle;
    String lastModalAsyncMessage;
    String lastStatusMessage;
    StringArray criticalLogs;
    std::vector<std::string> trace;

    void playAlertSound()
    {
        ++soundCount;
    }

    void showBubble (const String& message)
    {
        ++bubbleCount;
        lastBubble = message;
        trace.push_back ("bubble");
    }

    void showModalAsync (const String& title,
                         const String& message)
    {
        ++modalAsyncCount;
        lastModalAsyncTitle = title;
        lastModalAsyncMessage = message;
        trace.push_back ("modal");
    }

    void sendStatusMessage (const String& message)
    {
        ++statusMessageCount;
        lastStatusMessage = message;
        trace.push_back ("status");
    }

    void disableCallbacks()
    {
        ++disableCallbacksCount;
    }

    void logCritical (const String& message)
    {
        ++logCriticalCount;
        criticalLogs.add (message);
        trace.push_back ("log:" + message.toStdString());
    }
};

struct StagedProcessor
{
    Processor base;
    bool nwbUser = false;
    bool enabled = true;
    bool ready = true;
};

struct StagedInspectorOwner
{
    std::size_t nodes = 4;
    std::vector<StagedProcessor> processors;
    std::vector<int> baseReads;
    std::vector<int> nwbReads;
    std::vector<int> enabledReads;
    std::vector<int> readyReads;
    std::vector<std::vector<int>> parameterValidReads;
    std::vector<std::vector<int>> parameterKeyReads;
    std::vector<std::vector<int>>
        parameterDisplayNameReads;
    std::vector<std::string> trace;

    void resetCounts()
    {
        const auto count = processors.size();
        baseReads.assign (count, 0);
        nwbReads.assign (count, 0);
        enabledReads.assign (count, 0);
        readyReads.assign (count, 0);
        parameterValidReads.clear();
        parameterKeyReads.clear();
        parameterDisplayNameReads.clear();
        for (const auto& staged : processors)
        {
            const auto parameterCount =
                staged.base.parameters.size();
            parameterValidReads.emplace_back (
                parameterCount,
                0);
            parameterKeyReads.emplace_back (
                parameterCount,
                0);
            parameterDisplayNameReads.emplace_back (
                parameterCount,
                0);
        }
    }

    std::size_t processorCount() const
    {
        return processors.size();
    }

    std::size_t nodeCount()
    {
        trace.push_back ("node_count");
        return nodes;
    }

    Processor readBaseAndParameters (
        std::size_t index)
    {
        ++baseReads[index];
        trace.push_back (
            "base:" + std::to_string (index));
        for (std::size_t parameterIndex = 0;
             parameterIndex
             < processors[index]
                   .base.parameters.size();
             ++parameterIndex)
        {
            ++parameterKeyReads[index]
                                [parameterIndex];
            ++parameterDisplayNameReads[index]
                                        [parameterIndex];
            ++parameterValidReads[index]
                                  [parameterIndex];
        }
        return processors[index].base;
    }

    Processor readBase (std::size_t index)
    {
        ++baseReads[index];
        trace.push_back (
            "base:" + std::to_string (index));
        auto copied = processors[index].base;
        copied.parameters.clear();
        return copied;
    }

    std::size_t parameterCount (
        std::size_t processorIndex) const
    {
        return processors[processorIndex]
            .base.parameters.size();
    }

    bool readParameterValid (
        std::size_t processorIndex,
        std::size_t parameterIndex)
    {
        ++parameterValidReads[processorIndex]
                             [parameterIndex];
        return processors[processorIndex]
            .base.parameters[parameterIndex]
            .valid;
    }

    String readParameterKey (
        std::size_t processorIndex,
        std::size_t parameterIndex)
    {
        ++parameterKeyReads[processorIndex]
                           [parameterIndex];
        return processors[processorIndex]
            .base.parameters[parameterIndex]
            .key;
    }

    String readParameterDisplayName (
        std::size_t processorIndex,
        std::size_t parameterIndex)
    {
        ++parameterDisplayNameReads
            [processorIndex][parameterIndex];
        return processors[processorIndex]
            .base.parameters[parameterIndex]
            .displayName;
    }

    bool readNwbUser (std::size_t index)
    {
        ++nwbReads[index];
        trace.push_back (
            "nwb:" + std::to_string (index));
        return processors[index].nwbUser;
    }

    bool readEnabled (std::size_t index)
    {
        ++enabledReads[index];
        trace.push_back (
            "enabled:" + std::to_string (index));
        return processors[index].enabled;
    }

    bool readReady (std::size_t index)
    {
        ++readyReads[index];
        trace.push_back (
            "ready:" + std::to_string (index));
        return processors[index].ready;
    }
};

StagedInspectorOwner stagedOwner()
{
    StagedInspectorOwner owner;
    owner.processors = {
        { processor ("Source", 101) },
        { processor ("Record Node", 102) },
        { processor ("Message Center", 103) }
    };
    owner.resetCounts();
    return owner;
}

void expectNoPresentation (const PresentationSpy& spy)
{
    EXPECT_EQ (spy.soundCount, 0);
    EXPECT_EQ (spy.bubbleCount, 0);
    EXPECT_EQ (spy.modalAsyncCount, 0);
    EXPECT_EQ (spy.statusMessageCount, 0);
    EXPECT_EQ (spy.disableCallbacksCount, 0);
    EXPECT_EQ (spy.logCriticalCount, 0);
}
} // namespace

TEST (ProcessorGraphAcquisitionReadinessStagedInspectorTests,
      InvalidParameterStopsBeforeLaterBaseOrScanReads)
{
    auto owner = stagedOwner();
    owner.processors[0].base.parameters.push_back ({
        "gain",
        "Gain",
        false
    });
    owner.resetCounts();

    const auto result =
        inspectProcessorGraphAcquisitionReadinessStaged (
            owner);

    EXPECT_FALSE (result.ready);
    EXPECT_EQ (
        owner.baseReads,
        (std::vector<int> { 1, 0, 0 }));
    EXPECT_EQ (
        owner.nwbReads,
        (std::vector<int> { 0, 0, 0 }));
    EXPECT_EQ (
        owner.enabledReads,
        (std::vector<int> { 0, 0, 0 }));
    EXPECT_EQ (
        owner.readyReads,
        (std::vector<int> { 0, 0, 0 }));
    EXPECT_EQ (
        owner.trace,
        (std::vector<std::string> { "base:0" }));
}

TEST (ProcessorGraphAcquisitionReadinessStagedInspectorTests,
      ValidParametersReadOnlyValidity)
{
    auto owner = stagedOwner();
    owner.processors[0].base.parameters = {
        { "gain", "Gain", true },
        { "offset", "Offset", true }
    };
    owner.resetCounts();

    const auto result =
        inspectProcessorGraphAcquisitionReadinessStaged (
            owner);

    EXPECT_TRUE (result.ready);
    EXPECT_EQ (
        owner.parameterValidReads[0],
        (std::vector<int> { 1, 1 }));
    EXPECT_EQ (
        owner.parameterKeyReads[0],
        (std::vector<int> { 0, 0 }));
    EXPECT_EQ (
        owner.parameterDisplayNameReads[0],
        (std::vector<int> { 0, 0 }));
}

TEST (ProcessorGraphAcquisitionReadinessStagedInspectorTests,
      FirstInvalidParameterReadsIdentityOnceAndStops)
{
    auto owner = stagedOwner();
    owner.processors[0].base.parameters = {
        { "gain", "Gain", true },
        { "offset", "Offset", false },
        { "phase", "Phase", false }
    };
    owner.processors[1].base.parameters = {
        { "later", "Later", false }
    };
    owner.resetCounts();

    const auto result =
        inspectProcessorGraphAcquisitionReadinessStaged (
            owner);

    ASSERT_FALSE (result.ready);
    ASSERT_EQ (result.issues.size(), 1);
    EXPECT_EQ (
        result.issues[0].code,
        Code::invalidParameter);
    EXPECT_EQ (
        result.issues[0].parameterKey,
        "offset");
    EXPECT_EQ (
        result.issues[0].parameterDisplayName,
        "Offset");
    EXPECT_EQ (
        owner.baseReads,
        (std::vector<int> { 1, 0, 0 }));
    EXPECT_EQ (
        owner.parameterValidReads[0],
        (std::vector<int> { 1, 1, 0 }));
    EXPECT_EQ (
        owner.parameterKeyReads[0],
        (std::vector<int> { 0, 1, 0 }));
    EXPECT_EQ (
        owner.parameterDisplayNameReads[0],
        (std::vector<int> { 0, 1, 0 }));
    EXPECT_EQ (
        owner.parameterValidReads[1],
        (std::vector<int> { 0 }));
    EXPECT_EQ (
        owner.nwbReads,
        (std::vector<int> { 0, 0, 0 }));
}

TEST (ProcessorGraphAcquisitionReadinessStagedInspectorTests,
      InsufficientCountPerformsNoScanStageReads)
{
    auto owner = stagedOwner();
    owner.nodes = 3;

    const auto result =
        inspectProcessorGraphAcquisitionReadinessStaged (
            owner);

    EXPECT_FALSE (result.ready);
    EXPECT_EQ (
        owner.baseReads,
        (std::vector<int> { 1, 1, 1 }));
    EXPECT_EQ (
        owner.nwbReads,
        (std::vector<int> { 0, 0, 0 }));
    EXPECT_EQ (
        owner.enabledReads,
        (std::vector<int> { 0, 0, 0 }));
    EXPECT_EQ (
        owner.readyReads,
        (std::vector<int> { 0, 0, 0 }));
    EXPECT_EQ (
        owner.trace,
        (std::vector<std::string> {
            "base:0",
            "base:1",
            "base:2",
            "node_count"
        }));
}

TEST (ProcessorGraphAcquisitionReadinessStagedInspectorTests,
      ValidGraphReadsEveryOwnerFactExactlyOnceInOrder)
{
    auto owner = stagedOwner();

    const auto result =
        inspectProcessorGraphAcquisitionReadinessStaged (
            owner);

    EXPECT_TRUE (result.ready);
    EXPECT_EQ (
        owner.baseReads,
        (std::vector<int> { 1, 1, 1 }));
    EXPECT_EQ (
        owner.nwbReads,
        (std::vector<int> { 1, 1, 1 }));
    EXPECT_EQ (
        owner.enabledReads,
        (std::vector<int> { 1, 1, 1 }));
    EXPECT_EQ (
        owner.readyReads,
        (std::vector<int> { 1, 1, 1 }));
    EXPECT_EQ (
        owner.trace,
        (std::vector<std::string> {
            "base:0",
            "base:1",
            "base:2",
            "node_count",
            "nwb:0",
            "enabled:0",
            "ready:0",
            "nwb:1",
            "enabled:1",
            "ready:1",
            "nwb:2",
            "enabled:2",
            "ready:2"
        }));
}

TEST (ProcessorGraphAcquisitionReadinessStagedInspectorTests,
      EarlierNotReadyPreventsEveryLaterScanRead)
{
    auto owner = stagedOwner();
    owner.processors[1].ready = false;

    const auto result =
        inspectProcessorGraphAcquisitionReadinessStaged (
            owner);

    EXPECT_FALSE (result.ready);
    EXPECT_EQ (
        owner.nwbReads,
        (std::vector<int> { 1, 1, 0 }));
    EXPECT_EQ (
        owner.enabledReads,
        (std::vector<int> { 1, 1, 0 }));
    EXPECT_EQ (
        owner.readyReads,
        (std::vector<int> { 1, 1, 0 }));
}

TEST (ProcessorGraphAcquisitionReadinessStagedInspectorTests,
      SecondNwbStopsBeforeThatProcessorEnabledOrReady)
{
    auto owner = stagedOwner();
    owner.processors[0].nwbUser = true;
    owner.processors[1].nwbUser = true;

    const auto result =
        inspectProcessorGraphAcquisitionReadinessStaged (
            owner);

    EXPECT_FALSE (result.ready);
    EXPECT_EQ (
        owner.nwbReads,
        (std::vector<int> { 1, 1, 0 }));
    EXPECT_EQ (
        owner.enabledReads,
        (std::vector<int> { 1, 0, 0 }));
    EXPECT_EQ (
        owner.readyReads,
        (std::vector<int> { 1, 0, 0 }));
}

TEST (ProcessorGraphAcquisitionReadinessLazyOwnerTests,
      ReadyResultNeverCreatesPresentationOwner)
{
    int factoryCount = 0;
    const auto result =
        evaluateProcessorGraphAcquisitionReadiness (
            validFacts());

    EXPECT_TRUE (
        presentProcessorGraphAcquisitionReadinessWithLazyOwner (
            result,
            ProcessorGraphAcquisitionReadinessPresentationPolicy::
                legacyInteractive,
            [&]() -> PresentationSpy*
            {
                ++factoryCount;
                return nullptr;
            }));
    EXPECT_EQ (factoryCount, 0);
}

TEST (ProcessorGraphAcquisitionReadinessLazyOwnerTests,
      FailureWithMissingPresentationOwnerIsSafe)
{
    int factoryCount = 0;
    auto facts = validFacts();
    facts.nodeCount = 3;
    const auto result =
        evaluateProcessorGraphAcquisitionReadiness (
            facts);

    EXPECT_FALSE (
        presentProcessorGraphAcquisitionReadinessWithLazyOwner (
            result,
            ProcessorGraphAcquisitionReadinessPresentationPolicy::
                legacyInteractive,
            [&]() -> PresentationSpy*
            {
                ++factoryCount;
                return nullptr;
            }));
    EXPECT_EQ (factoryCount, 1);
}

TEST (ProcessorGraphAcquisitionReadinessLazyOwnerTests,
      FailureWithValidOwnerDelegatesExactlyOnce)
{
    int factoryCount = 0;
    PresentationSpy spy;
    auto facts = validFacts();
    facts.nodeCount = 3;
    const auto result =
        evaluateProcessorGraphAcquisitionReadiness (
            facts);

    EXPECT_FALSE (
        presentProcessorGraphAcquisitionReadinessWithLazyOwner (
            result,
            ProcessorGraphAcquisitionReadinessPresentationPolicy::
                legacyInteractive,
            [&]
            {
                ++factoryCount;
                return &spy;
            }));
    EXPECT_EQ (factoryCount, 1);
    EXPECT_EQ (spy.bubbleCount, 1);
    EXPECT_EQ (spy.disableCallbacksCount, 1);
}

TEST (ProcessorGraphAcquisitionReadinessTests,
      ValidCopiedFactsAreReady)
{
    const auto result =
        evaluateProcessorGraphAcquisitionReadiness (
            validFacts());

    EXPECT_TRUE (result.ready);
    EXPECT_TRUE (result.issues.empty());
}

TEST (ProcessorGraphAcquisitionReadinessTests,
      InvalidParameterRetainsExactCopiedIdentity)
{
    auto facts = validFacts();
    facts.processors[0].parameters.push_back ({
        "channel_map",
        "Channel Map",
        false
    });

    const auto result =
        evaluateProcessorGraphAcquisitionReadiness (
            facts);

    EXPECT_FALSE (result.ready);
    ASSERT_EQ (result.issues.size(), 1u);
    const auto& issue = result.issues[0];
    EXPECT_EQ (issue.code, Code::invalidParameter);
    ASSERT_TRUE (issue.processor.has_value());
    EXPECT_EQ (issue.processor->name, "Source");
    EXPECT_EQ (issue.processor->id, 101);
    EXPECT_EQ (issue.parameterKey, "channel_map");
    EXPECT_EQ (
        issue.parameterDisplayName,
        "Channel Map");
}

TEST (ProcessorGraphAcquisitionReadinessTests,
      InsufficientNodeCountRetainsActualAndMinimum)
{
    auto facts = validFacts();
    facts.nodeCount = 3;

    const auto result =
        evaluateProcessorGraphAcquisitionReadiness (
            facts);

    EXPECT_FALSE (result.ready);
    ASSERT_EQ (result.issues.size(), 1u);
    EXPECT_EQ (
        result.issues[0].code,
        Code::insufficientNodeCount);
    EXPECT_EQ (result.issues[0].actualCount, 3u);
    EXPECT_EQ (result.issues[0].requiredCount, 4u);
}

TEST (ProcessorGraphAcquisitionReadinessTests,
      MultipleNwbUsersAreOneExactIssue)
{
    auto facts = validFacts();
    facts.processors[0].nwbUser = true;
    facts.processors[1].nwbUser = true;

    const auto result =
        evaluateProcessorGraphAcquisitionReadiness (
            facts);

    EXPECT_FALSE (result.ready);
    ASSERT_EQ (result.issues.size(), 1u);
    EXPECT_EQ (
        result.issues[0].code,
        Code::multipleNwbUsers);
    EXPECT_EQ (result.issues[0].actualCount, 2u);
    EXPECT_EQ (result.issues[0].requiredCount, 1u);
}

TEST (ProcessorGraphAcquisitionReadinessTests,
      CollectsEveryDisabledProcessorInGraphOrder)
{
    auto facts = validFacts();
    facts.processors[0].enabled = false;
    facts.processors[2].enabled = false;

    const auto result =
        evaluateProcessorGraphAcquisitionReadiness (
            facts);

    EXPECT_FALSE (result.ready);
    ASSERT_EQ (result.issues.size(), 1u);
    const auto& issue = result.issues[0];
    EXPECT_EQ (
        issue.code,
        Code::disabledProcessors);
    ASSERT_EQ (issue.processors.size(), 2u);
    EXPECT_EQ (issue.processors[0].name, "Source");
    EXPECT_EQ (issue.processors[0].id, 101);
    EXPECT_EQ (
        issue.processors[1].name,
        "Message Center");
    EXPECT_EQ (issue.processors[1].id, 103);
}

TEST (ProcessorGraphAcquisitionReadinessTests,
      ProcessorNotReadyRetainsExactCopiedIdentity)
{
    auto facts = validFacts();
    facts.processors[1].ready = false;

    const auto result =
        evaluateProcessorGraphAcquisitionReadiness (
            facts);

    EXPECT_FALSE (result.ready);
    ASSERT_EQ (result.issues.size(), 1u);
    EXPECT_EQ (
        result.issues[0].code,
        Code::processorNotReady);
    ASSERT_TRUE (
        result.issues[0].processor.has_value());
    EXPECT_EQ (
        result.issues[0].processor->name,
        "Record Node");
    EXPECT_EQ (
        result.issues[0].processor->id,
        102);
}

TEST (ProcessorGraphAcquisitionReadinessTests,
      ResultRetainsNoOwnerOrMutableFact)
{
    auto facts = validFacts();
    facts.processors[0].parameters.push_back ({
        "gain",
        "Gain",
        false
    });

    const auto result =
        evaluateProcessorGraphAcquisitionReadiness (
            facts);

    facts.processors[0].name = "Mutated";
    facts.processors[0].id = 999;
    facts.processors[0].parameters[0].key =
        "mutated";
    facts.processors.clear();

    ASSERT_EQ (result.issues.size(), 1u);
    ASSERT_TRUE (
        result.issues[0].processor.has_value());
    EXPECT_EQ (
        result.issues[0].processor->name,
        "Source");
    EXPECT_EQ (
        result.issues[0].processor->id,
        101);
    EXPECT_EQ (
        result.issues[0].parameterKey,
        "gain");
}

TEST (ProcessorGraphAcquisitionReadinessTests,
      LegacyPolicyPreservesFirstFailurePrecedence)
{
    auto facts = validFacts();
    facts.nodeCount = 2;
    facts.processors[0].parameters.push_back ({
        "gain",
        "Gain",
        false
    });
    facts.processors[0].nwbUser = true;
    facts.processors[1].nwbUser = true;
    facts.processors[1].enabled = false;
    facts.processors[2].ready = false;
    const auto result =
        evaluateProcessorGraphAcquisitionReadiness (
            facts);
    PresentationSpy spy;

    const auto ready =
        presentProcessorGraphAcquisitionReadiness (
            result,
            ProcessorGraphAcquisitionReadinessPresentationPolicy::
                legacyInteractive,
            spy);

    EXPECT_FALSE (ready);
    EXPECT_EQ (spy.soundCount, 1);
    EXPECT_EQ (spy.bubbleCount, 1);
    EXPECT_EQ (spy.modalAsyncCount, 0);
    EXPECT_EQ (spy.statusMessageCount, 1);
    EXPECT_EQ (spy.disableCallbacksCount, 1);
    EXPECT_EQ (
        spy.lastBubble,
        "Source (101) - Gain is invalid"
        " and blocking acquisition.");
    EXPECT_EQ (
        spy.lastStatusMessage,
        "Parameter gain is not valid.");
}

TEST (ProcessorGraphAcquisitionReadinessTests,
      LegacyPolicyPresentsTheCompleteDisabledList)
{
    auto facts = validFacts();
    facts.processors[0].enabled = false;
    facts.processors[2].enabled = false;
    const auto result =
        evaluateProcessorGraphAcquisitionReadiness (
            facts);
    PresentationSpy spy;

    const auto ready =
        presentProcessorGraphAcquisitionReadiness (
            result,
            ProcessorGraphAcquisitionReadinessPresentationPolicy::
                legacyInteractive,
            spy);

    EXPECT_FALSE (ready);
    EXPECT_EQ (spy.soundCount, 1);
    EXPECT_EQ (spy.bubbleCount, 1);
    EXPECT_EQ (spy.modalAsyncCount, 0);
    EXPECT_EQ (spy.statusMessageCount, 0);
    EXPECT_EQ (spy.disableCallbacksCount, 1);
    EXPECT_EQ (
        spy.lastBubble,
        "The following processors are disabled"
        " and blocking acquisition:\n\n"
        "Source (101)\n"
        "Message Center (103)");
}

TEST (ProcessorGraphAcquisitionReadinessTests,
      LegacyInteractivePresentsInsufficientNodesExactly)
{
    auto facts = validFacts();
    facts.nodeCount = 3;
    const auto result =
        evaluateProcessorGraphAcquisitionReadiness (
            facts);
    PresentationSpy spy;

    const auto ready =
        presentProcessorGraphAcquisitionReadiness (
            result,
            ProcessorGraphAcquisitionReadinessPresentationPolicy::
                legacyInteractive,
            spy);

    EXPECT_FALSE (ready);
    EXPECT_EQ (spy.soundCount, 1);
    EXPECT_EQ (spy.bubbleCount, 1);
    EXPECT_EQ (spy.modalAsyncCount, 0);
    EXPECT_EQ (spy.statusMessageCount, 1);
    EXPECT_EQ (spy.disableCallbacksCount, 1);
    EXPECT_EQ (
        spy.lastBubble,
        "Add a source processor to the signal chain"
        " before starting acquisition");
    EXPECT_EQ (
        spy.lastStatusMessage,
        "Not enough processors in signal chain"
        " to acquire data.");
}

TEST (ProcessorGraphAcquisitionReadinessTests,
      LegacyInteractivePresentsMultipleNwbUsersExactly)
{
    auto facts = validFacts();
    facts.processors[0].nwbUser = true;
    facts.processors[1].nwbUser = true;
    const auto result =
        evaluateProcessorGraphAcquisitionReadiness (
            facts);
    PresentationSpy spy;

    const auto ready =
        presentProcessorGraphAcquisitionReadiness (
            result,
            ProcessorGraphAcquisitionReadinessPresentationPolicy::
                legacyInteractive,
            spy);

    EXPECT_FALSE (ready);
    EXPECT_EQ (spy.soundCount, 0);
    EXPECT_EQ (spy.bubbleCount, 0);
    EXPECT_EQ (spy.modalAsyncCount, 1);
    EXPECT_EQ (spy.statusMessageCount, 0);
    EXPECT_EQ (spy.disableCallbacksCount, 1);
    EXPECT_EQ (spy.lastModalAsyncTitle, "WARNING");
    EXPECT_EQ (
        spy.lastModalAsyncMessage,
        "Open Ephys currently does not support"
        " multiple processors using NWB format."
        " Please modify the signal chain accordingly"
        " to proceed with acquisition.");
    EXPECT_EQ (spy.logCriticalCount, 1);
    ASSERT_EQ (spy.criticalLogs.size(), 1);
    EXPECT_EQ (
        spy.criticalLogs[0],
        "Multiple processors using NWB format"
        " is not supported. Please modify the signal"
        " chain accordingly to proceed with acquisition.");
}

TEST (ProcessorGraphAcquisitionReadinessTests,
      LegacyInteractivePresentsProcessorNotReadyExactly)
{
    auto facts = validFacts();
    facts.processors[1].ready = false;
    const auto result =
        evaluateProcessorGraphAcquisitionReadiness (
            facts);
    PresentationSpy spy;

    const auto ready =
        presentProcessorGraphAcquisitionReadiness (
            result,
            ProcessorGraphAcquisitionReadinessPresentationPolicy::
                legacyInteractive,
            spy);

    EXPECT_FALSE (ready);
    EXPECT_EQ (spy.soundCount, 0);
    EXPECT_EQ (spy.bubbleCount, 1);
    EXPECT_EQ (spy.modalAsyncCount, 0);
    EXPECT_EQ (spy.statusMessageCount, 1);
    EXPECT_EQ (spy.disableCallbacksCount, 1);
    EXPECT_EQ (
        spy.lastBubble,
        "Record Node (102) is not ready"
        " and blocking acquisition");
    EXPECT_EQ (
        spy.lastStatusMessage,
        "Record Node (102) is not ready"
        " and blocking acquisition");
}

TEST (ProcessorGraphAcquisitionReadinessTests,
      ReadyLegacyPoliciesProduceNoPresentationEffects)
{
    const auto result =
        evaluateProcessorGraphAcquisitionReadiness (
            validFacts());

    for (const auto policy : {
             ProcessorGraphAcquisitionReadinessPresentationPolicy::
                 legacyInteractive,
             ProcessorGraphAcquisitionReadinessPresentationPolicy::
                 legacyHeadless })
    {
        PresentationSpy spy;
        EXPECT_TRUE (
            presentProcessorGraphAcquisitionReadiness (
                result,
                policy,
                spy));
        expectNoPresentation (spy);
    }
}

TEST (ProcessorGraphAcquisitionReadinessTests,
      LegacyHeadlessPreservesNonUiEffectsByIssue)
{
    struct Case
    {
        const char* name;
        Facts facts;
        int expectedStatusCount;
        const char* expectedStatus;
    };

    auto invalid = validFacts();
    invalid.processors[0].parameters.push_back ({
        "gain",
        "Gain",
        false
    });
    auto insufficient = validFacts();
    insufficient.nodeCount = 3;
    auto notReady = validFacts();
    notReady.processors[1].ready = false;
    auto nwb = validFacts();
    nwb.processors[0].nwbUser = true;
    nwb.processors[1].nwbUser = true;
    auto disabled = validFacts();
    disabled.processors[0].enabled = false;
    disabled.processors[2].enabled = false;

    for (const auto& testCase : {
             Case {
                 "invalid",
                 invalid,
                 1,
                 "Parameter gain is not valid."
             },
             Case {
                 "insufficient",
                 insufficient,
                 1,
                 "Not enough processors in signal chain"
                 " to acquire data."
             },
             Case {
                 "not-ready",
                 notReady,
                 1,
                 "Record Node (102) is not ready"
                 " and blocking acquisition"
             },
             Case { "nwb", nwb, 0, "" },
             Case { "disabled", disabled, 0, "" } })
    {
        SCOPED_TRACE (testCase.name);
        const auto result =
            evaluateProcessorGraphAcquisitionReadiness (
                testCase.facts);
        PresentationSpy spy;

        EXPECT_FALSE (
            presentProcessorGraphAcquisitionReadiness (
                result,
                ProcessorGraphAcquisitionReadinessPresentationPolicy::
                    legacyHeadless,
                spy));

        EXPECT_EQ (spy.soundCount, 0);
        EXPECT_EQ (spy.bubbleCount, 0);
        EXPECT_EQ (spy.modalAsyncCount, 0);
        EXPECT_EQ (
            spy.statusMessageCount,
            testCase.expectedStatusCount);
        EXPECT_EQ (spy.disableCallbacksCount, 1);
        if (testCase.expectedStatusCount == 1)
        {
            EXPECT_EQ (
                spy.lastStatusMessage,
                testCase.expectedStatus);
        }
    }
}

TEST (ProcessorGraphAcquisitionReadinessTests,
      LegacyPrecedenceChecksNodeCountBeforeGraphScan)
{
    auto facts = validFacts();
    facts.nodeCount = 3;
    facts.processors[0].nwbUser = true;
    facts.processors[1].nwbUser = true;
    facts.processors[0].ready = false;
    facts.processors[2].enabled = false;
    const auto result =
        evaluateProcessorGraphAcquisitionReadiness (
            facts);
    PresentationSpy spy;

    presentProcessorGraphAcquisitionReadiness (
        result,
        ProcessorGraphAcquisitionReadinessPresentationPolicy::
            legacyInteractive,
        spy);

    EXPECT_EQ (
        spy.lastBubble,
        "Add a source processor to the signal chain"
        " before starting acquisition");
    EXPECT_EQ (spy.modalAsyncCount, 0);
    EXPECT_EQ (spy.statusMessageCount, 1);
}

TEST (ProcessorGraphAcquisitionReadinessTests,
      LegacyScanStopsAtNotReadyBeforeASecondNwbUser)
{
    auto facts = validFacts();
    facts.processors[0].nwbUser = true;
    facts.processors[1].ready = false;
    facts.processors[2].nwbUser = true;
    facts.processors[2].enabled = false;
    const auto result =
        evaluateProcessorGraphAcquisitionReadiness (
            facts);
    PresentationSpy spy;

    presentProcessorGraphAcquisitionReadiness (
        result,
        ProcessorGraphAcquisitionReadinessPresentationPolicy::
            legacyInteractive,
        spy);

    EXPECT_EQ (
        spy.lastBubble,
        "Record Node (102) is not ready"
        " and blocking acquisition");
    EXPECT_EQ (spy.modalAsyncCount, 0);
    EXPECT_EQ (spy.statusMessageCount, 1);
}

TEST (ProcessorGraphAcquisitionReadinessTests,
      LegacyScanChecksSecondNwbBeforeThatProcessorReadiness)
{
    auto facts = validFacts();
    facts.processors[0].nwbUser = true;
    facts.processors[1].nwbUser = true;
    facts.processors[1].ready = false;
    facts.processors[2].enabled = false;
    const auto result =
        evaluateProcessorGraphAcquisitionReadiness (
            facts);
    PresentationSpy spy;

    presentProcessorGraphAcquisitionReadiness (
        result,
        ProcessorGraphAcquisitionReadinessPresentationPolicy::
            legacyInteractive,
        spy);

    EXPECT_EQ (spy.modalAsyncCount, 1);
    EXPECT_EQ (spy.lastModalAsyncTitle, "WARNING");
    EXPECT_EQ (spy.bubbleCount, 0);
    EXPECT_EQ (spy.statusMessageCount, 0);
}

TEST (ProcessorGraphAcquisitionReadinessTests,
      LegacyScanLogsEarlierDisabledBeforeLaterNotReadyFailure)
{
    auto facts = validFacts();
    facts.processors[0].enabled = false;
    facts.processors[1].ready = false;
    PresentationSpy spy;

    presentProcessorGraphAcquisitionReadiness (
        evaluateProcessorGraphAcquisitionReadiness (
            facts),
        ProcessorGraphAcquisitionReadinessPresentationPolicy::
            legacyInteractive,
        spy);

    ASSERT_EQ (spy.criticalLogs.size(), 1);
    EXPECT_EQ (
        spy.criticalLogs[0],
        "Source (101) is disabled"
        " and blocking acquisition.");
    EXPECT_EQ (
        spy.lastBubble,
        "Record Node (102) is not ready"
        " and blocking acquisition");
    EXPECT_EQ (
        spy.trace,
        (std::vector<std::string> {
            "log:Source (101) is disabled"
            " and blocking acquisition.",
            "bubble",
            "status"
        }));
    EXPECT_EQ (spy.disableCallbacksCount, 1);
}

TEST (ProcessorGraphAcquisitionReadinessTests,
      LegacyScanLogsEarlierDisabledBeforeSecondNwbFailure)
{
    auto facts = validFacts();
    facts.processors[0].enabled = false;
    facts.processors[0].nwbUser = true;
    facts.processors[1].nwbUser = true;
    PresentationSpy spy;

    presentProcessorGraphAcquisitionReadiness (
        evaluateProcessorGraphAcquisitionReadiness (
            facts),
        ProcessorGraphAcquisitionReadinessPresentationPolicy::
            legacyInteractive,
        spy);

    ASSERT_EQ (spy.criticalLogs.size(), 2);
    EXPECT_EQ (
        spy.criticalLogs[0],
        "Source (101) is disabled"
        " and blocking acquisition.");
    EXPECT_EQ (
        spy.criticalLogs[1],
        "Multiple processors using NWB format"
        " is not supported. Please modify the signal"
        " chain accordingly to proceed with acquisition.");
    EXPECT_EQ (
        spy.trace,
        (std::vector<std::string> {
            "log:Source (101) is disabled"
            " and blocking acquisition.",
            "modal",
            "log:Multiple processors using NWB format"
            " is not supported. Please modify the signal"
            " chain accordingly to proceed with acquisition."
        }));
    EXPECT_EQ (spy.disableCallbacksCount, 1);
}

TEST (ProcessorGraphAcquisitionReadinessTests,
      FinalDisabledFailureLogsEachProcessorOnlyOnce)
{
    auto facts = validFacts();
    facts.processors[0].enabled = false;
    PresentationSpy spy;

    presentProcessorGraphAcquisitionReadiness (
        evaluateProcessorGraphAcquisitionReadiness (
            facts),
        ProcessorGraphAcquisitionReadinessPresentationPolicy::
            legacyInteractive,
        spy);

    EXPECT_EQ (spy.logCriticalCount, 1);
    ASSERT_EQ (spy.criticalLogs.size(), 1);
    EXPECT_EQ (
        spy.criticalLogs[0],
        "Source (101) is disabled"
        " and blocking acquisition.");
}

TEST (ProcessorGraphAcquisitionReadinessTests,
      LegacyScanDefersDisabledListUntilAllNodesPass)
{
    auto facts = validFacts();
    facts.processors[0].enabled = false;
    facts.processors[2].enabled = false;
    const auto result =
        evaluateProcessorGraphAcquisitionReadiness (
            facts);
    PresentationSpy spy;

    presentProcessorGraphAcquisitionReadiness (
        result,
        ProcessorGraphAcquisitionReadinessPresentationPolicy::
            legacyInteractive,
        spy);

    EXPECT_EQ (spy.modalAsyncCount, 0);
    EXPECT_EQ (spy.statusMessageCount, 0);
    EXPECT_EQ (spy.bubbleCount, 1);
    EXPECT_EQ (
        spy.lastBubble,
        "The following processors are disabled"
        " and blocking acquisition:\n\n"
        "Source (101)\n"
        "Message Center (103)");
    EXPECT_EQ (spy.logCriticalCount, 2);
    ASSERT_EQ (spy.criticalLogs.size(), 2);
    EXPECT_EQ (
        spy.criticalLogs[0],
        "Source (101) is disabled"
        " and blocking acquisition.");
    EXPECT_EQ (
        spy.criticalLogs[1],
        "Message Center (103) is disabled"
        " and blocking acquisition.");
}

TEST (ProcessorGraphAcquisitionReadinessTests,
      LegacyHeadlessRestoresNwbAndDisabledDiagnostics)
{
    auto nwbFacts = validFacts();
    nwbFacts.processors[0].nwbUser = true;
    nwbFacts.processors[1].nwbUser = true;
    PresentationSpy nwbSpy;
    presentProcessorGraphAcquisitionReadiness (
        evaluateProcessorGraphAcquisitionReadiness (
            nwbFacts),
        ProcessorGraphAcquisitionReadinessPresentationPolicy::
            legacyHeadless,
        nwbSpy);

    EXPECT_EQ (nwbSpy.logCriticalCount, 1);
    ASSERT_EQ (nwbSpy.criticalLogs.size(), 1);
    EXPECT_EQ (
        nwbSpy.criticalLogs[0],
        "Multiple processors using NWB format"
        " is not supported. Please modify the signal"
        " chain accordingly to proceed with acquisition.");

    auto disabledFacts = validFacts();
    disabledFacts.processors[0].enabled = false;
    disabledFacts.processors[2].enabled = false;
    PresentationSpy disabledSpy;
    presentProcessorGraphAcquisitionReadiness (
        evaluateProcessorGraphAcquisitionReadiness (
            disabledFacts),
        ProcessorGraphAcquisitionReadinessPresentationPolicy::
            legacyHeadless,
        disabledSpy);

    EXPECT_EQ (disabledSpy.logCriticalCount, 2);
    ASSERT_EQ (disabledSpy.criticalLogs.size(), 2);
    EXPECT_EQ (
        disabledSpy.criticalLogs[0],
        "Source (101) is disabled"
        " and blocking acquisition.");
    EXPECT_EQ (
        disabledSpy.criticalLogs[1],
        "Message Center (103) is disabled"
        " and blocking acquisition.");
}

TEST (ProcessorGraphAcquisitionReadinessTests,
      SilentApiPolicyProducesNoPresentationSideEffects)
{
    auto facts = validFacts();
    facts.processors[0].parameters.push_back ({
        "gain",
        "Gain",
        false
    });
    const auto result =
        evaluateProcessorGraphAcquisitionReadiness (
            facts);
    PresentationSpy spy;

    const auto ready =
        presentProcessorGraphAcquisitionReadiness (
            result,
            ProcessorGraphAcquisitionReadinessPresentationPolicy::
                silent,
            spy);

    EXPECT_FALSE (ready);
    expectNoPresentation (spy);
}
