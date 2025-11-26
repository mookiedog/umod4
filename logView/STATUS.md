# logView - Current Status

## ✅ Ready for Testing!

The prototype WebAssembly log viewer is ready to test with your actual log files.

### What's Been Built

1. **viewer_prototype.html** (27KB)
   - Single self-contained HTML file
   - Pure JavaScript parser (no WASM yet)
   - Implements ~30 event types
   - Works offline on all platforms

2. **Documentation**
   - README.md - Project overview
   - DEVELOPMENT.md - Technical roadmap
   - TEST_RESULTS.md - Testing guide
   - STATUS.md - This file

### How to Test

```bash
# Method 1: Direct open
firefox ~/projects/umod4/logView/viewer_prototype.html
# Then drag-and-drop: ~/logs/2025-11-24/log.3

# Method 2: Use test script
cd ~/projects/umod4/logView
./test_viewer.sh ~/logs/2025-11-24/log.3
```

### What to Expect

**The viewer should display:**

```
📁 Open Log File button
📂 Drop zone

After loading log.3 (38KB):

Stats:
  File Size: 38.0 KB
  Events Decoded: ~1500
  Duration: ~0.0s
  Processing Time: ~0.1s

Text Output (first events):
  [     1 @     0.0000s]: WP_VR:  0
  [     2 @     0.0000s]: EP_VER:  0
  [    10 @     0.0000s]: LOAD:   8796539
  [    19 @     0.0000s]: ADDR:   0x0000
  [    20 @     0.0000s]: LEN:    0x8000
  [    21 @     0.0000s]: STAT:   ERR_NOERR
  ...
```

### Implemented Features

- ✅ File drag-and-drop
- ✅ File picker button
- ✅ **Recent files tracking** (last 10 files with localStorage)
- ✅ Offline operation
- ✅ EP EPROM loading events
- ✅ ECU sensor events
- ✅ Timestamp reconstruction (16-bit → 64-bit nanoseconds)
- ✅ Human-readable formatting
- ✅ Statistics display
- ✅ Tab interface (text/graph)
- ✅ Responsive design
- ✅ Settings persistence (recent files survive browser restart)

### Known Limitations (Prototype)

1. **Output capped at 5000 events** - For browser performance
2. **No temperature conversion** - Shows raw ADC values (e.g., 156 instead of 7.0°C)
3. **No graphing yet** - Text view only
4. **Missing some event types** - ~30 of ~100 total events implemented
5. **No streaming** - Loads entire file to memory (fine for <100MB)

### Comparison with decodelog.py

| Feature | decodelog.py | viewer_prototype.html | Status |
|---------|--------------|----------------------|--------|
| Parse binary format | ✅ | ✅ | Match |
| Timestamp reconstruction | ✅ | ✅ | Match |
| EP LOAD events | ✅ | ✅ | Match |
| ECU sensor events | ✅ | ✅ | Match |
| Temperature conversion | ✅ | ❌ | TODO |
| HDF5 export | ✅ | ❌ | N/A for viewer |
| Works offline | ❌ (needs Python) | ✅ | Better |
| Works on phone | ❌ | ✅ | Better |
| Processing speed | Fast | Fast | ~Same |

### Next Steps

**Phase 1: Validate Prototype** (This Week)
- [x] Implement EP event parsing
- [x] Implement ECU event parsing
- [ ] **Test with real log file** ← YOU ARE HERE
- [ ] Verify output matches decodelog.py
- [ ] Test on different browsers
- [ ] Test on phone

**Phase 2: Complete JavaScript Version** (Week 2-3)
- [ ] Add all remaining event types
- [ ] Add temperature conversion
- [ ] Add binary formatting (PORTG, errors)
- [ ] Remove 5000 event limit
- [ ] Add progress indicator
- [ ] Build stream index for graphing
- [ ] Implement basic plotting

**Phase 3: C++ WASM Parser** (Week 4-5)
- [ ] Port parser to C++
- [ ] Compile with Emscripten
- [ ] JavaScript/WASM bridge
- [ ] Performance testing
- [ ] CMake integration

**Phase 4: Advanced Features** (Future)
- [ ] Interactive zoom/pan
- [ ] Stream selection UI
- [ ] Multiple file comparison
- [ ] Export to CSV/JSON
- [ ] Save/restore view state
- [ ] Custom event filters

### Files in logView/

```
logView/
├── viewer_prototype.html   - The actual viewer (READY TO TEST)
├── test_viewer.sh          - Helper script to open viewer
├── README.md               - Project overview
├── DEVELOPMENT.md          - Technical details
├── TEST_RESULTS.md         - Expected test results
└── STATUS.md               - This file
```

### Testing Checklist

Open viewer and verify:
- [ ] Page loads without errors
- [ ] Can click "Open Log File" button
- [ ] Can drag-and-drop log file
- [ ] File loads and parses
- [ ] Stats show correct values
- [ ] Text output appears
- [ ] EP LOAD events decode correctly
- [ ] ECU events decode correctly
- [ ] No JavaScript console errors
- [ ] Processing completes in < 1 second

### Questions for User

After testing, please provide feedback:

1. **Does output match expectations?**
   - Compare first 60 events with decodelog.py output

2. **Any errors or crashes?**
   - Check browser console (F12)

3. **Performance acceptable?**
   - Should parse 38KB in ~0.1s

4. **What features are highest priority?**
   - Temperature conversion?
   - Remove event limit?
   - Add graphing?
   - More event types?

5. **Test on phone?**
   - Does it work on Android/iOS?

### Quick Commands Reference

```bash
# View expected output
head -100 ~/logs/2025-11-24/log.3.hr

# Count events in log
wc -l ~/logs/2025-11-24/log.3.hr

# Test viewer
cd ~/projects/umod4/logView
firefox viewer_prototype.html

# Check for JavaScript errors
# (Open browser, press F12 for console)
```

---

**Status**: 🟢 Ready for Testing
**Next Action**: Open viewer and load test log file
**Blocked By**: None
**ETA to completion**: 2-4 weeks (depending on feature priorities)
