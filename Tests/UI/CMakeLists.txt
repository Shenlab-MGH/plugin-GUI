
if(WIN32)
	set(COMPONENT_REGISTER_AGGREGATE_TEST OFF)
endif()

include(../ComponentRules.cmake)
add_sources(${COMPONENT_NAME}_tests
		ControlPanelAccessibilityTests.cpp
		DataViewportTests.cpp
		MainDocumentWindowAccessibilityTests.cpp
		SemanticComponentTests.cpp
		SignalChainAccessibilityTests.cpp
)
target_include_directories(
		${COMPONENT_NAME}_tests
		PRIVATE
		"${SOURCE_DIRECTORY}"
)

if(WIN32)
	include(GoogleTest)
	gtest_discover_tests(
		${COMPONENT_NAME}_tests
		TEST_PREFIX "${COMPONENT_NAME}_tests_"
		PROPERTIES TIMEOUT 30
	)
else()
	set_tests_properties(${COMPONENT_NAME}_tests PROPERTIES TIMEOUT 30)
endif()

unset(COMPONENT_REGISTER_AGGREGATE_TEST)
