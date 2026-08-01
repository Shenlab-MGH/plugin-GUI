include(../ComponentRules.cmake)
add_sources(${COMPONENT_NAME}_tests
        AcquisitionRecordingControlTests.cpp
        AcquisitionRecordingRuntimeTests.cpp
        AcquisitionRecordingStatusTests.cpp
        ControlCapabilityTests.cpp
        ControlDispatchTests.cpp
        ControlReadTests.cpp
        ControlStatusJsonTests.cpp
        HttpServerLifecycleTests.cpp
        MessageThreadCallTests.cpp
        RecordingOptionsControlTests.cpp
        SettingsDispatchTests.cpp
        StatusControlTests.cpp
        StatusHttpAdapterTests.cpp
        StatusRequestParserTests.cpp
)
target_include_directories(${COMPONENT_NAME}_tests PRIVATE "${SOURCE_DIRECTORY}")
