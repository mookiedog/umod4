# CMakeLists.txt for logView - umod4 Log Viewer
#
# This creates a custom target that bundles the ES6 modules into a single HTML file
# for distribution to phones, tablets, and other devices.

cmake_minimum_required(VERSION 3.20)

# Find Python3 (should already be found by parent CMake)
if(NOT Python3_FOUND)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
endif()

# Define source files
set(VIEWER_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/index.html
    ${CMAKE_CURRENT_SOURCE_DIR}/js/constants.js
    ${CMAKE_CURRENT_SOURCE_DIR}/js/parser.js
    ${CMAKE_CURRENT_SOURCE_DIR}/js/textRenderer.js
    ${CMAKE_CURRENT_SOURCE_DIR}/js/search.js
    ${CMAKE_CURRENT_SOURCE_DIR}/js/fileManager.js
)

set(BUNDLE_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/bundle.py)
set(BUNDLE_OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/viewer_bundle.html)

# Custom command to run bundler
add_custom_command(
    OUTPUT ${BUNDLE_OUTPUT}
    COMMAND ${Python3_EXECUTABLE} ${BUNDLE_SCRIPT}
    COMMAND ${CMAKE_COMMAND} -E copy
            ${CMAKE_CURRENT_SOURCE_DIR}/viewer_bundle.html
            ${BUNDLE_OUTPUT}
    DEPENDS ${VIEWER_SOURCES} ${BUNDLE_SCRIPT}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Bundling log viewer ES6 modules..."
    VERBATIM
)

# Custom target for bundled viewer
add_custom_target(logViewer ALL
    DEPENDS ${BUNDLE_OUTPUT}
)

# Install bundled viewer to build directory root for easy access
install(FILES ${BUNDLE_OUTPUT}
        DESTINATION ${CMAKE_BINARY_DIR}
        OPTIONAL
)

# Also install to a logView subdirectory in build for organization
install(FILES ${BUNDLE_OUTPUT}
        DESTINATION ${CMAKE_BINARY_DIR}/logView
        OPTIONAL
)

# Message at configuration time
message(STATUS "Log Viewer: Will bundle ES6 modules to viewer_bundle.html")
message(STATUS "  - Development: Use 'make serve' in logView/ directory")
message(STATUS "  - Distribution: Copy viewer_bundle.html to phone/tablet")
