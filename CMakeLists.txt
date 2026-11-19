cmake_minimum_required(VERSION 3.10)
project(pesonaserver C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# 1. Define the executable and point to the files inside src/
add_executable(personaserver 
    src/main.c 
    src/app.c
    src/log.c
    src/linkedlist.c
    src/websocketserver.c
    src/buspackage.c
    src/bus.c
    src/wssession.c
    src/wssessionmanager.c
    src/grokstt.c
    src/grokllm.c
    src/elevenlabstts.c
    src/packagering.c
)

# 2. Tell the compiler where to look for your header files (.h)
target_include_directories(personaserver PRIVATE 
    include
    # src   # Uncomment this if your header files are inside the src/ folder instead
)

# 3. Add Libraries (e.g., pthread required for your mutex locks)
target_link_libraries(personaserver PRIVATE pthread websockets curl cjson)

# 4. Add Compiler Flags (Optional: add AddressSanitizer and debug symbols for debugging crashes)
target_compile_options(personaserver PRIVATE -g -fsanitize=address)
target_link_options(personaserver PRIVATE -fsanitize=address)
