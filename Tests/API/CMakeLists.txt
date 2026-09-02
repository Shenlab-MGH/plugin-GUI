include(../ComponentRules.cmake)
add_sources(${COMPONENT_NAME}_tests
        ControlCapabilityTests.cpp
        ControlReadTests.cpp
        ConfigSnapshotApiHandlerTests.cpp
        ControlStatusJsonTests.cpp
        MessageThreadCallTests.cpp
        ProcessorInventoryApiHandlerTests.cpp
        RecordingOptionsControlTests.cpp
        StatusApiHandlerTests.cpp
        StatusControlTests.cpp
)
target_include_directories(${COMPONENT_NAME}_tests PRIVATE "${SOURCE_DIRECTORY}")
