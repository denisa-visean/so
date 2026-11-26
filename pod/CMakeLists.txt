cmake_minimum_required(VERSION 3.10)
project(pod C)

# versiunea finala
add_executable(pod_windows pod_windows.c)
# versiunea 1, fara protectie la infometare
add_executable(pod_windows_v1 pod_windows.c)
target_compile_definitions(pod_windows_v1 PRIVATE FARA_PROTECTIE)

if(MSVC)
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
endif()
