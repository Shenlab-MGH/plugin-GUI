include(../ComponentRules.cmake)
add_sources(${COMPONENT_NAME}_tests
        AcquisitionRecordingControlTests.cpp
        AcquisitionRecordingRuntimeTests.cpp
        AcquisitionRecordingStatusTests.cpp
        ControlCapabilityTests.cpp
        ControlDispatchTests.cpp
        ControlReadTests.cpp
        ControlStatusJsonTests.cpp
        HttpAdmissionLoopbackTests.cpp
        HttpRequestAdmissionTests.cpp
        HttpServerLifecycleTests.cpp
        MessageThreadCallTests.cpp
        ParameterContractTests.cpp
        ParameterRouteSegmentTests.cpp
        RecordingOptionsControlTests.cpp
        SettingsDispatchTests.cpp
        StatusControlTests.cpp
        StatusHttpAdapterTests.cpp
        StatusRequestParserTests.cpp
)
set_source_files_properties(ParameterRouteSegmentTests.cpp PROPERTIES
        COMPILE_OPTIONS "$<$<CXX_COMPILER_ID:MSVC>:/bigobj>")
target_include_directories(${COMPONENT_NAME}_tests PRIVATE "${SOURCE_DIRECTORY}")
