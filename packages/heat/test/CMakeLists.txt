macro(include_executable test_exec test_src)
    include_directories(${global_include_directories})
    set(test_dependencies utilities solid_geometry spatial_discretization heat)
    
    add_executable(${test_exec} ${test_src})
    target_link_libraries(${test_exec} ${test_dependencies})
    install(TARGETS ${test_exec} DESTINATION ${CMAKE_INSTALL_PREFIX}/test)
endmacro()

macro(include_test test_name test_exec test_args)
    add_test(${test_name} ${test_exec} ${test_args})
endmacro()

include_executable(tst_cylindrical_heat tst_Constant_Cylindrical.cc)
include_test(tst_cylindrical_heat_1d tst_cylindrical_heat "")
include_test(tst_cylindrical_heat_2d tst_cylindrical_heat ${CMAKE_CURRENT_SOURCE_DIR}/input/heat_two_region.xml)
# include_executable(tst_heat_integration tst_Heat_Integration.cc)
# include_test(tst_heat_integration tst_heat_integration "")

add_subdirectory(input)
