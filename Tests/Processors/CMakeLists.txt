include(../ComponentRules.cmake)
add_sources(${COMPONENT_NAME}_tests
		EventChannelTests.cpp
		ContinuousChannelTests.cpp
		DataBufferTests.cpp
		PluginManagerTests.cpp
		SourceNodeTests.cpp
		RecordNodeTests.cpp
		SIMDConverterTests.cpp
		ProcessorGraphTests.cpp
		EventTests.cpp
		DataThreadTests.cpp
		GenericProcessorTests.cpp
		MessageCenterTests.cpp
		ChannelInfoObjectTests.cpp
		InfoObjectTests.cpp
		MetadataEventLockTests.cpp
		MetadataEventObjectTests.cpp
		MetadataEventTests.cpp
		ParameterOwnerTests.cpp
		../../Source/Processors/PluginManager/PluginManager.cpp
)
target_include_directories(
		${COMPONENT_NAME}_tests
		PRIVATE
		"${SOURCE_DIRECTORY}"
)

target_compile_definitions(
		${COMPONENT_NAME}_tests
		PRIVATE
		RESOURCES_DIRECTORY="${RESOURCES_DIRECTORY}"
)

if(WIN32)
	# Compile the production AccessClass implementation once, with one test-only
	# setter added in the same translation unit so ProcessorGraph uses the real state.
	get_target_property(gui_testable_sources gui_testable_source SOURCES)
	set(access_class_source "${SOURCE_DIRECTORY}/AccessClass.cpp")
	list(REMOVE_ITEM gui_testable_sources "${access_class_source}")
	set_property(TARGET gui_testable_source PROPERTY SOURCES "${gui_testable_sources}")
	target_sources(gui_testable_source PRIVATE ${CMAKE_CURRENT_LIST_DIR}/TestableAccessClass.cpp)
endif()
