function(add_agent_core_test target)
    add_executable(${target} ${ARGN})
    target_compile_features(${target} PRIVATE cxx_std_17)
    add_test(NAME ${target} COMMAND ${target})
endfunction()

add_agent_core_test(
    AgentCommandTests
    AgentCommandTests.cpp
)

add_agent_core_test(
    AgentStateTests
    AgentStateTests.cpp
    ../../Source/Agent/AgentState.cpp
)

add_agent_core_test(
    ControlPanelCommandRouterTests
    ControlPanelCommandRouterTests.cpp
    ../../Source/Agent/ControlPanelCommandRouter.cpp
)

add_agent_core_test(
    AgentTransportPlannerTests
    AgentTransportPlannerTests.cpp
    ../../Source/Agent/AgentTransportPlanner.cpp
)

add_agent_core_test(
    TransportAccessibilityRegistryTests
    TransportAccessibilityRegistryTests.cpp
    ../../Source/Agent/TransportAccessibilityRegistry.cpp
)

add_agent_core_test(
    AgentTransportCoordinatorTests
    AgentTransportCoordinatorTests.cpp
    ../../Source/Agent/AgentTransportCoordinator.cpp
    ../../Source/Agent/AgentTransportPlanner.cpp
)

add_agent_core_test(
    AgentTransportMailboxTests
    AgentTransportMailboxTests.cpp
    ../../Source/Agent/AgentTransportMailbox.cpp
)

add_agent_core_test(
    AgentTransportEndpointTests
    AgentTransportEndpointTests.cpp
    ../../Source/Agent/AgentTransportEndpoint.cpp
    ../../Source/Agent/AgentTransportMailbox.cpp
    ../../Source/Agent/AgentStateSnapshotCache.cpp
)

add_agent_core_test(
    AgentStateSnapshotCacheTests
    AgentStateSnapshotCacheTests.cpp
    ../../Source/Agent/AgentStateSnapshotCache.cpp
)
