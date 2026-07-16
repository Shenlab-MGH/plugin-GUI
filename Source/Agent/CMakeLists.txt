# Experimental agent-native core. Keep these sources outside real-time callbacks.
add_sources(open-ephys
    AgentCommand.h
    AgentState.h
    AgentState.cpp
    ControlPanelCommandRouter.h
    ControlPanelCommandRouter.cpp
)
