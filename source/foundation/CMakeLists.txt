set(
    OS_FOUNDATION_PUBLIC_HEADERS
    include/os/foundation/address_range.hpp
    include/os/foundation/reference_counter.hpp
    include/os/foundation/scope_rollback.hpp
)

set(
    OS_FOUNDATION_PRIVATE_SOURCES
    src/address_range.cpp
    src/reference_counter.cpp
    src/scope_rollback.cpp
)

function(os_configure_foundation_target OS_FOUNDATION_TARGET)
    target_sources(
        ${OS_FOUNDATION_TARGET}
        PRIVATE
            ${OS_FOUNDATION_PRIVATE_SOURCES}
        PUBLIC
            FILE_SET HEADERS
            BASE_DIRS
                "${CMAKE_CURRENT_SOURCE_DIR}/include"
            FILES
                ${OS_FOUNDATION_PUBLIC_HEADERS}
    )
    target_include_directories(
        ${OS_FOUNDATION_TARGET}
        PUBLIC
            "${CMAKE_CURRENT_SOURCE_DIR}/include"
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/src"
    )
    target_compile_options(
        ${OS_FOUNDATION_TARGET}
        PRIVATE
            ${OS_COMMON_WARNING_FLAGS}
    )
endfunction()

add_library(os_foundation_host STATIC)
os_configure_foundation_target(os_foundation_host)

add_library(os_foundation_x86_64 STATIC)
os_configure_foundation_target(os_foundation_x86_64)
target_compile_options(
    os_foundation_x86_64
    PRIVATE
        --target=x86_64-unknown-none-elf
        -ffreestanding
        -fno-builtin
        -fno-exceptions
        -fno-pic
        -fno-pie
        -fno-rtti
        -fno-stack-protector
        -fno-threadsafe-statics
        -fno-use-cxa-atexit
        -m64
        -mcmodel=kernel
        -mno-mmx
        -mno-red-zone
        -mno-sse
        -mno-sse2
        -nostdinc++
)
