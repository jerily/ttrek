# Set the system name to Linux
set(CMAKE_SYSTEM_NAME Linux)

# Specify the cross-compiler
set(CMAKE_C_COMPILER musl-gcc)

# Set the root path for the target (Alpine Linux with musl)
set(CMAKE_FIND_ROOT_PATH /usr/lib/x86_64-linux-musl /usr/include/x86_64-linux-musl)

# Adjust the default behavior of the FIND_XXX() commands:
# Search for programs in the host environment
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)

# Search for libraries and headers in the target environment
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

# Optionally set system processor if needed
set(CMAKE_SYSTEM_PROCESSOR arm)

# Specify flags, if needed
set(CMAKE_C_FLAGS "-static -Os -I/usr/include/x86_64-linux-musl" CACHE STRING "C flags")

include_directories(/usr/include/x86_64-linux-musl)