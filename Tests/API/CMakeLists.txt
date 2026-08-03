include(../ComponentRules.cmake)
add_sources(${COMPONENT_NAME}_tests
        ControlCapabilityTests.cpp
        ControlReadTests.cpp
        ControlStatusJsonTests.cpp
        MessageThreadCallTests.cpp
        RecordingOptionsControlTests.cpp
        StatusApiHandlerTests.cpp
)
target_include_directories(${COMPONENT_NAME}_tests PRIVATE "${SOURCE_DIRECTORY}")
