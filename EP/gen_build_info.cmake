string(TIMESTAMP BUILD_DATETIME "%Y-%m-%d %H:%M:%S")
file(WRITE "${OUTPUT_FILE}"
    "const char* ep_build_datetime() { return \"${BUILD_DATETIME}\"; }\n"
)
