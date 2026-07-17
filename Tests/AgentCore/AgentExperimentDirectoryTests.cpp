#include "../../Source/Agent/AgentExperimentDirectory.h"

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

AgentDirectoryObservation idleObservation()
{
    return { false, {}, AgentObservedMode::idle, 7 };
}
}

int main()
{
    AgentExperimentDirectory policy;
    const AgentDirectoryRequest valid {
        "D:\\OpenEphys\\RUN001",
        "RUN001_M3_part01_1-96",
        7
    };

    const auto ready = policy.validate (valid, idleObservation());
    require (ready.outcome == AgentDirectoryOutcome::ready,
             "new confined IDLE directory must be ready");
    require (ready.targetPath
                 == "D:\\OpenEphys\\RUN001\\RUN001_M3_part01_1-96",
             "decision must return the exact planned native path");

    auto exists = idleObservation();
    exists.targetExists = true;
    require (policy.validate (valid, exists).outcome
                 == AgentDirectoryOutcome::alreadyExists,
             "an existing target must fail closed");

    auto collision = idleObservation();
    collision.siblingNames = { "run001_m3_PART01_1-96" };
    require (policy.validate (valid, collision).outcome
                 == AgentDirectoryOutcome::caseCollision,
             "Windows case-folded collisions must fail closed");

    const AgentDirectoryRequest traversal {
        "D:\\OpenEphys\\RUN001", "..\\escape", 7
    };
    require (policy.validate (traversal, idleObservation()).outcome
                 == AgentDirectoryOutcome::invalidNativeName,
             "directory names must be one native segment");

    const AgentDirectoryRequest period {
        "D:\\OpenEphys\\RUN001", "part.01", 7
    };
    require (policy.validate (period, idleObservation()).outcome
                 == AgentDirectoryOutcome::invalidNativeName,
             "native GUI periods must be rejected");

    const AgentDirectoryRequest autoSuffix {
        "D:\\OpenEphys\\RUN001", "part01 (1)", 7
    };
    require (policy.validate (autoSuffix, idleObservation()).outcome
                 == AgentDirectoryOutcome::autoSuffixForbidden,
             "official collision suffixes must never be planned");

    const AgentDirectoryRequest relativeRoot {
        "relative\\root", "part01", 7
    };
    require (policy.validate (relativeRoot, idleObservation()).outcome
                 == AgentDirectoryOutcome::approvedRootInvalid,
             "approved root must be an absolute non-root path");

    auto acquiring = idleObservation();
    acquiring.mode = AgentObservedMode::acquire;
    require (policy.validate (valid, acquiring).outcome
                 == AgentDirectoryOutcome::recordingNotInactive,
             "directory preparation must be IDLE-only");

    auto unknown = idleObservation();
    unknown.mode = AgentObservedMode::unknown;
    require (policy.validate (valid, unknown).outcome
                 == AgentDirectoryOutcome::recordingNotInactive,
             "unknown state must fail closed");

    auto stale = idleObservation();
    stale.revision = 8;
    require (policy.validate (valid, stale).outcome
                 == AgentDirectoryOutcome::revisionConflict,
             "stale directory preparation must be rejected");

    std::cout << "PASS AgentExperimentDirectoryTests\n";
    return 0;
}

