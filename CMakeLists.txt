cmake_minimum_required(VERSION 3.20)
project(SendToScreen CXX RC)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
add_compile_definitions(UNICODE _UNICODE NOMINMAX)
add_compile_options(/W4)

add_executable(SendToScreen WIN32 main.cpp taskbar.cpp mover.cpp logic.cpp app.rc app.manifest)
target_compile_features(SendToScreen PRIVATE cxx_std_17)
string(REPLACE "/O2" "/O1" CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE}")
target_compile_options(SendToScreen PRIVATE $<$<CONFIG:Release>:/GL /Gy /Gw>)
target_link_options(SendToScreen PRIVATE $<$<CONFIG:Release>:/LTCG /OPT:REF /OPT:ICF>
    /DELAYLOAD:propsys.dll /DELAYLOAD:version.dll /DELAYLOAD:dwmapi.dll /DELAYLOAD:oleaut32.dll /DELAYLOAD:ole32.dll)
target_link_libraries(SendToScreen PRIVATE user32 shell32 ole32 oleaut32 propsys version dwmapi delayimp)

add_executable(tests tests.cpp logic.cpp)
target_compile_features(tests PRIVATE cxx_std_17)
enable_testing()
add_test(NAME unit COMMAND tests)
