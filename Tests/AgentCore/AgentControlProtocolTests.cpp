#include "../../Source/Agent/AgentControlProtocol.h"

#include <cstdlib>
#include <iostream>

namespace
{
void require (bool condition, const char* message)
{
    if (! condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        std::exit (1);
    }
}
}

int main()
{
    const auto directoryParsed =
        AgentControlProtocol::parseDirectoryRequest (
            R"({"run_id":"run-1","command_id":"dir-1","expected_session_id":"session-1","approved_root":"D:\\recordings","directory_name":"mouseA_shank_01","expected_revision":7})");
    require (directoryParsed.request.has_value(),
             "A valid directory request must parse");
    require (directoryParsed.request->request.approvedRoot
                 == "D:\\recordings",
             "The approved root must be preserved");
    require (directoryParsed.request->request.directoryName
                 == "mouseA_shank_01",
             "The exact native directory name must be preserved");
    require (! AgentControlProtocol::parseDirectoryRequest (
                   R"({"run_id":"run-1","command_id":"dir-1","expected_session_id":"session-1","approved_root":"D:\\recordings","directory_name":"mouseA_shank_01","expected_revision":7,"unexpected":true})")
                   .request.has_value(),
             "Unknown directory fields must fail closed");

    const auto parsed = AgentControlProtocol::parseTransportRequest (
        R"({"request_id":"cpp-1","expected_session_id":"session-1","target_mode":"RECORD","expected_revision":7})");
    require (parsed.request.has_value(),
             "A valid transport request must parse");
    require (parsed.request->requestId == "cpp-1",
             "The request ID must be preserved");
    require (parsed.request->targetMode == AgentObservedMode::record,
             "The target mode must parse as RECORD");
    require (parsed.request->expectedRevision == 7,
             "The expected revision must be preserved");
    require (parsed.expectedSessionId == "session-1",
             "The expected session must be preserved");

    require (! AgentControlProtocol::parseTransportRequest (
                   R"({"request_id":"","expected_session_id":"session-1","target_mode":"IDLE","expected_revision":0})")
                   .request.has_value(),
             "An empty request ID must fail");
    require (! AgentControlProtocol::parseTransportRequest (
                   R"({"request_id":"bad","expected_session_id":"session-1","target_mode":"TOGGLE","expected_revision":0})")
                   .request.has_value(),
             "Toggle semantics must never be accepted");
    require (! AgentControlProtocol::parseTransportRequest (
                   R"({"request_id":"bad","expected_session_id":"session-1","target_mode":"IDLE","expected_revision":-1})")
                   .request.has_value(),
             "A negative revision must fail");
    require (! AgentControlProtocol::parseTransportRequest (
                   R"({"request_id":"bad/slash","expected_session_id":"session-1","target_mode":"IDLE","expected_revision":0})")
                   .request.has_value(),
             "Request IDs must use the URL-safe ASCII allowlist");
    require (! AgentControlProtocol::parseTransportRequest (
                   R"({"request_id":"cpp-1","target_mode":"IDLE","expected_revision":0})")
                   .request.has_value(),
             "Every mutation must be scoped to an expected session");
    require (! AgentControlProtocol::parseTransportRequest ("not-json")
                   .request.has_value(),
             "Malformed JSON must fail closed");

    const auto status = AgentControlProtocol::serializeStatus (
        {
            AgentEndpointPhase::ready,
            {
                "1.0.2-agent-v0.0.1",
                AgentObservedMode::acquire,
                9
            }
        },
        "session-1",
        false);
    require (status.find (
                 "\"schema_version\":\"oe-agent-control-preview/v0.0.1\"")
                 != std::string::npos,
             "Status must not claim compatibility with the Python v1 schema");
    require (status.find ("\"session_id\":\"session-1\"")
                 != std::string::npos,
             "Status must identify the current process session");
    require (status.find ("\"mutation_allowed\":false")
                 != std::string::npos,
             "An unarmed server must report mutation disabled");
    require (status.find ("\"phase\":\"READY\"")
                 != std::string::npos,
             "Status must expose endpoint lifecycle");
    require (status.find ("\"mode\":\"ACQUIRE\"")
                 != std::string::npos,
             "Status must expose target-state mode");
    require (status.find ("\"revision\":9")
                 != std::string::npos,
             "Status must expose the observed revision");

    AgentMailboxLookup lookup {
        AgentMailboxRequestState::cancelled,
        AgentMailboxTerminalReason::executorDetached,
        std::nullopt
    };
    const auto query = AgentControlProtocol::serializeRequestLookup (
        "cpp-2",
        lookup,
        "session-1");
    require (query.find ("\"state\":\"CANCELLED\"")
                 != std::string::npos,
             "Lookup must expose terminal state");
    require (query.find ("\"terminal_reason\":\"EXECUTOR_DETACHED\"")
                 != std::string::npos,
             "Lookup must expose terminal reason");

    const auto receipt = AgentControlProtocol::serializeSubmitReceipt (
        "cpp-3",
        AgentMailboxSubmitOutcome::accepted,
        "session-1");
    require (receipt.find ("\"state\":\"PENDING\"")
                 != std::string::npos,
             "Accepted submission must return PENDING");

    const auto directorySnapshot =
        AgentControlProtocol::serializeDirectorySnapshot (
            {
                "D:\\recordings",
                "mouseA_shank_01",
                "D:\\recordings\\mouseA_shank_01",
                true,
                false,
                AgentObservedMode::idle,
                8
            },
            "session-1");
    require (directorySnapshot.find ("\"prepared\":true")
                 != std::string::npos,
             "Directory snapshot must expose prepared readback");
    require (directorySnapshot.find (
                 "\"directory_name\":\"mouseA_shank_01\"")
                 != std::string::npos,
             "Directory snapshot must expose exact native name");

    const AgentDirectoryLookup collision {
        AgentDirectoryRequestState::completed,
        AgentDirectoryApplyResult {
            {
                AgentDirectoryOutcome::alreadyExists,
                "D:\\recordings\\mouseA_shank_01"
            },
            {}
        }
    };
    const auto directoryLookup =
        AgentControlProtocol::serializeDirectoryRequestLookup (
            "dir-2", collision, "session-1");
    require (directoryLookup.find (
                 "\"outcome\":\"DIRECTORY_COLLISION\"")
                 != std::string::npos,
             "Existing native directory must serialize as collision");

    std::cout << "PASS AgentControlProtocolTests\n";
    return 0;
}
