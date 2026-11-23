cmake_minimum_required(VERSION 3.20)
project(unilauncher LANGUAGES CXX)

if (NOT WIN32)
    message(FATAL_ERROR
        "unilauncher solo tiene sentido en Windows: lanza Leonida, XValkyrie "
        "y VIctory, que son los tres backends que produce (gta6rt.exe, "
        "xvboot.exe, victory.exe), y los tres son binarios de Windows.")
endif()

add_executable(unilauncher WIN32
    src/main.cpp
)

target_compile_features(unilauncher PRIVATE cxx_std_17)
target_compile_definitions(unilauncher PRIVATE UNICODE _UNICODE)

if (MSVC)
    target_compile_options(unilauncher PRIVATE /W4)
endif()

target_link_libraries(unilauncher PRIVATE
    user32
    gdi32
    shell32
    advapi32
    comdlg32
    ole32
)
