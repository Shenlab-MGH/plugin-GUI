include(../ComponentRules.cmake)
add_sources(${COMPONENT_NAME}_tests
        ControlCapabilityTests.cpp
        ControlStatusJsonTests.cpp
        MessageThreadCallTests.cpp
        RecordingOptionsControlTests.cpp
)
target_include_directories(${COMPONENT_NAME}_tests PRIVATE "${SOURCE_DIRECTORY}")
