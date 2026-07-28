add_library(os_system_abi INTERFACE)
target_sources(
    os_system_abi
    INTERFACE
        FILE_SET HEADERS
        BASE_DIRS
            "${CMAKE_CURRENT_SOURCE_DIR}/include"
        FILES
            include/os/abi/system_call.hpp
            include/os/abi/signal.hpp
            include/os/abi/terminal.hpp
            include/os/abi/thread.hpp
            include/os/abi/time.hpp
            include/os/abi/virtual_memory.hpp
)
target_include_directories(
    os_system_abi
    INTERFACE
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
