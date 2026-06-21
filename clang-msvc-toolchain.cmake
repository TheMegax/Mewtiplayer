# Clang cross-compilation toolchain targeting MSVC ABI from Linux
# Uses xwin-provided Windows SDK and MSVC CRT headers/libs

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(XWIN_DIR "$ENV{HOME}/.xwin" CACHE PATH "Path to xwin sysroot")

# Compilers
set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)
set(CMAKE_RC_COMPILER llvm-rc)
set(CMAKE_LINKER lld-link)

# Target MSVC ABI
set(CMAKE_C_COMPILER_TARGET x86_64-pc-windows-msvc)
set(CMAKE_CXX_COMPILER_TARGET x86_64-pc-windows-msvc)

# Use lld-link as the linker
set(CMAKE_C_FLAGS_INIT "-fuse-ld=lld-link")
set(CMAKE_CXX_FLAGS_INIT "-fuse-ld=lld-link")

# Include paths: MSVC CRT + Windows SDK
set(XWIN_CRT_INCLUDE "${XWIN_DIR}/crt/include")
set(XWIN_SDK_INCLUDE "${XWIN_DIR}/sdk/include/um")
set(XWIN_SDK_UCRT_INCLUDE "${XWIN_DIR}/sdk/include/ucrt")
set(XWIN_SDK_SHARED_INCLUDE "${XWIN_DIR}/sdk/include/shared")

# Library paths
set(XWIN_CRT_LIB "${XWIN_DIR}/crt/lib/x86_64")
set(XWIN_SDK_LIB "${XWIN_DIR}/sdk/lib/um/x86_64")
set(XWIN_SDK_UCRT_LIB "${XWIN_DIR}/sdk/lib/ucrt/x86_64")

# Set system include/library flags
set(CMAKE_C_FLAGS_INIT "${CMAKE_C_FLAGS_INIT} -Xclang -internal-isystem -Xclang ${XWIN_CRT_INCLUDE} -Xclang -internal-isystem -Xclang ${XWIN_SDK_UCRT_INCLUDE} -Xclang -internal-isystem -Xclang ${XWIN_SDK_INCLUDE} -Xclang -internal-isystem -Xclang ${XWIN_SDK_SHARED_INCLUDE}")
set(CMAKE_CXX_FLAGS_INIT "${CMAKE_CXX_FLAGS_INIT} -Xclang -internal-isystem -Xclang ${XWIN_CRT_INCLUDE} -Xclang -internal-isystem -Xclang ${XWIN_SDK_UCRT_INCLUDE} -Xclang -internal-isystem -Xclang ${XWIN_SDK_INCLUDE} -Xclang -internal-isystem -Xclang ${XWIN_SDK_SHARED_INCLUDE}")

# Linker library search paths
set(CMAKE_EXE_LINKER_FLAGS_INIT "-L${XWIN_CRT_LIB} -L${XWIN_SDK_LIB} -L${XWIN_SDK_UCRT_LIB}")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "-L${XWIN_CRT_LIB} -L${XWIN_SDK_LIB} -L${XWIN_SDK_UCRT_LIB}")

# Don't search host paths for libraries/includes
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
