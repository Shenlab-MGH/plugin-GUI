
include(../ComponentRules.cmake)
add_sources(${COMPONENT_NAME}_tests
		ControlPanelAccessibilityTests.cpp
		DataViewportTests.cpp
		MainDocumentWindowAccessibilityTests.cpp
		SemanticComponentTests.cpp
)
target_include_directories(
		${COMPONENT_NAME}_tests
		PRIVATE
		"${SOURCE_DIRECTORY}"
)

set_tests_properties(${COMPONENT_NAME}_tests PROPERTIES TIMEOUT 30)

get_test_property(${COMPONENT_NAME}_tests TIMEOUT UI_TEST_TIMEOUT)

if(NOT UI_TEST_TIMEOUT)
	message(FATAL_ERROR "${COMPONENT_NAME}_tests must have an external process timeout")
endif()

if(UI_TEST_TIMEOUT GREATER 30)
	message(FATAL_ERROR "${COMPONENT_NAME}_tests timeout must not exceed 30 seconds")
endif()
