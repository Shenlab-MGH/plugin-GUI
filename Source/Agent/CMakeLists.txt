# Experimental agent-native core. Keep these sources outside real-time callbacks.
add_sources(open-ephys
    AgentCommand.h
    AgentState.h
    AgentState.cpp
    AgentTransportPlanner.h
    AgentTransportPlanner.cpp
    AgentTransportCoordinator.h
    AgentTransportCoordinator.cpp
    TransportAccessibilityRegistry.h
    TransportAccessibilityRegistry.cpp
    ControlPanelCommandRouter.h
    ControlPanelCommandRouter.cpp
)
