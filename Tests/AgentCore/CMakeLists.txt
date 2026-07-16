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
