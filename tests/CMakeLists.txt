add_executable(test_separator_configuration configuration.cpp ../extern/argtable3-3.2.2/argtable3.c)
target_compile_definitions(test_separator_configuration PRIVATE MODE_NODESEP)
add_test(NAME separator.configuration COMMAND test_separator_configuration)

add_executable(test_ordering_configuration configuration.cpp ../extern/argtable3-3.2.2/argtable3.c)
target_compile_definitions(test_ordering_configuration PRIVATE MODE_NODESEP MODE_NODEORDERING)
add_test(NAME ordering.configuration COMMAND test_ordering_configuration)

add_executable(test_separator_structures separator_structures.cpp)
target_link_libraries(test_separator_structures PRIVATE kahip_static)
add_test(NAME separator.structures COMMAND test_separator_structures)

add_executable(test_library_ordering library_ordering.cpp)
target_link_libraries(test_library_ordering PRIVATE kahip_static)
add_test(NAME ordering.library COMMAND test_library_ordering)
add_test(NAME interface.smoke COMMAND interface_test)

if(CMAKE_VERSION VERSION_LESS "3.12")
  find_package(PythonInterp 3 REQUIRED)
  set(KAHIP_TEST_PYTHON "${PYTHON_EXECUTABLE}")
else()
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  set(KAHIP_TEST_PYTHON "${Python3_EXECUTABLE}")
endif()
add_test(NAME ordering.cli COMMAND "${KAHIP_TEST_PYTHON}" "${CMAKE_CURRENT_SOURCE_DIR}/ordering_checks.py"
  --ordering "$<TARGET_FILE:node_ordering>" --separator "$<TARGET_FILE:node_separator>")
set_tests_properties(ordering.cli PROPERTIES TIMEOUT 180)
