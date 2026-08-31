cmake_minimum_required(VERSION 3.16)
project(preemptive_user_thread_runtime C)

if(NOT UNIX)
    message(FATAL_ERROR "This runtime requires Linux/POSIX ucontext and SIGVTALRM")
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

add_library(uthread STATIC
    ThreadSystemCode/ThreadLibrary/thread.c
    ThreadSystemCode/ThreadLibrary/queue.c
)
target_include_directories(uthread PUBLIC ThreadSystemCode/ThreadLibrary)
target_compile_options(uthread PRIVATE -Wall -Wextra -Wpedantic)

enable_testing()
add_executable(uthread_smoke tests/smoke_test.c)
target_link_libraries(uthread_smoke PRIVATE uthread)
add_test(NAME uthread_smoke COMMAND uthread_smoke)
