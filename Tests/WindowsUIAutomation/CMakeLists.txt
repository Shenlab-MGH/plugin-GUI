include(../ComponentRules.cmake)
add_sources(${COMPONENT_NAME}_tests
    ProcessorInventoryWindowsUIAutomationTests.cpp
)
target_include_directories(${COMPONENT_NAME}_tests PRIVATE "${SOURCE_DIRECTORY}")
target_link_libraries(${COMPONENT_NAME}_tests PRIVATE Ole32 OleAut32 Uiautomationcore)
set_tests_properties(WindowsUIAutomation_tests PROPERTIES TIMEOUT 30)
