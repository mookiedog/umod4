# logView Development Notes

## Current Status

### Phase 1: Prototype (Current)
- **File**: `viewer_prototype.html`
- **Technology**: Pure JavaScript (no WASM yet)
- **Status**: Ready for testing
- **Features**:
  - Text view showing decoded event stream
  - File drag-and-drop support
  - Offline capable (single HTML file)
  - Works on all platforms (desktop + mobile)

### Testing the Prototype

```bash
# Option 1: Open directly in browser
firefox ~/projects/umod4/logView/viewer_prototype.html

# Option 2: Serve locally (if you want to test on phone)
cd ~/projects/umod4/logView
python3 -m http.server 8080
# Then browse to http://localhost:8080/viewer_prototype.html
```

### Known Limitations (Prototype)

1. **Limited event support**: Only implements ~20 most common event types
2. **Output capped**: Shows first 5000 events only (for performance)
3. **No graphing yet**: Text view only
4. **JavaScript performance**: Slower than C++ for large files

## Phase 2: Full JavaScript Implementation (Next)

**Goals:**
- [ ] Add all event types from log headers
- [ ] Build in-memory stream index for graphing
- [ ] Add interactive plotting (Plotly.js)
- [ ] Support full file (no 5000 event limit)
- [ ] Add stream selection UI
- [ ] Add zoom/pan controls

**Estimated effort**: 1-2 weeks

## Phase 3: C++ WASM Parser (Future)

**Goals:**
- [ ] Port parser to C++ (reuse decodelog.py logic)
- [ ] Compile to WASM using Emscripten
- [ ] Create JavaScript/WASM bridge
- [ ] Add to CMake build system
- [ ] Performance testing vs pure JavaScript

**Benefits:**
- 5-10x faster parsing for large files
- Shared code with other C++ components
- Better memory efficiency

**Estimated effort**: 2-3 weeks

## Integration with Main Project

### Future CMakeLists.txt Addition

```cmake
# Add logView as external project (at same level as EP, WP, ecu)
if(BUILD_LOGVIEW)
    add_subdirectory(logView)
endif()
```

### Future logView/CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.20)
project(logView)

# Option 1: Pure JavaScript build (just copy HTML)
add_custom_target(logview_html
    COMMAND ${CMAKE_COMMAND} -E copy
        ${CMAKE_CURRENT_SOURCE_DIR}/viewer.html
        ${CMAKE_CURRENT_BINARY_DIR}/viewer.html
    COMMENT "Copying viewer HTML"
)

# Option 2: WASM build (requires Emscripten)
if(EMSCRIPTEN)
    add_executable(logparser_wasm src/parser.cpp)
    set_target_properties(logparser_wasm PROPERTIES
        COMPILE_FLAGS "-O3"
        LINK_FLAGS "-s WASM=1 -s EXPORTED_FUNCTIONS='[_parseLog]' -s MODULARIZE=1"
    )

    # Embed WASM into HTML
    add_custom_command(
        OUTPUT viewer_final.html
        COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/embed_wasm.py
            ${CMAKE_CURRENT_SOURCE_DIR}/viewer.html
            ${CMAKE_CURRENT_BINARY_DIR}/logparser_wasm.wasm
            ${CMAKE_CURRENT_BINARY_DIR}/viewer_final.html
        DEPENDS logparser_wasm viewer.html
    )

    add_custom_target(logview_final ALL DEPENDS viewer_final.html)
endif()
```

## Testing Strategy

### Unit Tests (Future)
- Test TimeKeeper timestamp reconstruction
- Test wraparound handling
- Test event parsing for each type
- Compare output with decodelog.py

### Integration Tests
1. Parse same log with both decodelog.py and viewer
2. Compare event counts
3. Compare timestamps
4. Verify no events missed

### Performance Benchmarks
- Target: Parse 100MB file in <3 seconds (JavaScript) or <1 second (WASM)
- Target: Display 5000 events in <100ms
- Target: Zoom/pan response <50ms

## File Format Notes

### Binary Log Structure
- Event-driven format (not time-series)
- Sparse timestamps (16-bit, 2μs resolution)
- Variable-length events (1-9 bytes typical)
- No framing/packetization

### Timestamp Reconstruction Challenges
1. **16-bit wraparound**: Every 131ms (65536 * 2μs)
2. **Overflow markers**: OFLO and HOFLO events
3. **Untimestamped events**: Inherit ordering between timestamps
4. **Prospective timestamps**: Some events schedule future actions

### Key Design Decision
Why not pre-convert to HDF5?
- Binary logs are 4x smaller than HDF5
- Conversion takes minutes
- Want instant viewing without preprocessing
- WebAssembly can parse fast enough

## Distribution Plans

### Option A: GitHub Releases
```
https://github.com/yourname/umod4/releases/
  └─ umod4-viewer-v1.0.html (5MB download)
```

### Option B: Motorcycle WiFi Serving
Hardware serves viewer at known URL:
```
http://192.168.4.1/viewer    (HTML viewer)
http://192.168.4.1/logs      (list logs)
http://192.168.4.1/log/N     (download log N)
```

User workflow:
1. Connect to bike WiFi
2. Browse to viewer URL
3. Auto-loads latest log
4. No file transfer needed!

### Option C: Progressive Web App
Host at domain:
```
https://umod4.app
```
Benefits:
- Caches for offline use
- "Add to home screen" on phones
- Push updates automatically
- Professional URL

## Next Steps

1. **Test prototype** with real log files
2. **Verify timestamp reconstruction** matches decodelog.py
3. **Add missing event types** one by one
4. **Implement graphing** with Plotly.js
5. **Optimize performance** for large files
6. **Consider WASM** if JavaScript too slow

## Questions to Answer

- [ ] Is pure JavaScript fast enough, or do we need WASM?
- [ ] Should we support streaming (chunk-by-chunk) for huge files?
- [ ] What's the largest log file we need to support?
- [ ] Do we need to save processed data (IndexedDB)?
- [ ] Should viewer support comparing two logs side-by-side?
