# Test Results - Viewer Prototype

## Test Log: ~/logs/2025-11-24/log.3

### File Info
- **Size**: 38KB
- **Format**: umod4 binary log
- **Contains**: EP EPROM loading events + ECU events

### Expected Output (from decodelog.py)

First 60 records should show:
```
[     1 @     0.0000s]: WP_VR:  0
[     2 @     0.0000s]: EP_VR:  0
[    10 @     0.0000s]: LOAD:   8796539
[    19 @     0.0000s]: ADDR:   0x0000
[    20 @     0.0000s]: LEN:    0x8000
[    21 @     0.0000s]: STAT:   ERR_NOERR
[    25 @     0.0000s]: LOAD:   UM4
[    30 @     0.0000s]: ADDR:   0x0000
[    31 @     0.0000s]: LEN:    0x8000
[    32 @     0.0000s]: STAT:   ERR_NOERR
...
[    59 @     0.0000s]: ECU_VR: 0
[    60 @     0.0000s]: CPU:    7
[    61 @     0.0000s]: VTA:    137
[    62 @     0.0000s]: THA:    7.0C
[    63 @     0.0000s]: THW:    12.9C
```

### Viewer Implementation Status

#### Implemented Events ✅
- **General**: ECU_VER, EP_VER, WP_VER
- **EP Events**:
  - LOAD_NAME (0xD0) - String accumulation
  - LOAD_ADDR (0xD2) - Address display
  - LOAD_LEN (0xD4) - Length display
  - LOAD_ERR (0xD6) - Error status with name lookup
- **ECU Events**:
  - CPU_EVENT - CPU interrupts
  - T1_OFLO, T1_HOFLO - Timestamp markers
  - Injector events (F/R INJ_ON, INJ_DUR)
  - Coil events (F/R COIL_ON, COIL_OFF)
  - Sensor data (VTA, MAP, AAP, THW, THA, VM)
  - Crank/Cam events (CRANKREF_START, CRANKREF_ID, CAMSHAFT)
  - Fuel pump
- **WP Events**:
  - GPS_VELO

#### Not Yet Implemented ⏳
- Temperature conversions (shows raw ADC values)
- Spark timing events
- Additional GPS events (position, time)
- Error bit decoding (L000C, L000D, L000E, L000F)
- PORTG decoding
- 5ms marker event

### Known Differences from decodelog.py

1. **Temperature Display**:
   - decodelog.py: Shows "7.0C" (converted)
   - Viewer: Shows raw ADC value "156" (0x9C)
   - TODO: Add temperature conversion function

2. **PORTG Display**:
   - decodelog.py: Shows "11101111" (binary)
   - Viewer: Shows decimal value
   - TODO: Add binary formatting

3. **Record Counting**:
   - EP_LOAD_NAME: Each character is a separate byte read, but decodelog.py counts them all
   - Viewer may have different record count due to accumulation logic
   - This is OK - final event sequence should match

### Testing Procedure

1. **Open Viewer**:
   ```bash
   firefox ~/projects/umod4/logView/viewer_prototype.html
   ```

2. **Load Log File**:
   - Click "Open Log File" button
   - Select `~/logs/2025-11-24/log.3`
   - Or drag-and-drop the file

3. **Verify Output**:
   - Check stats: File Size ~38KB, Duration ~0.0-0.1s
   - Compare first 60 events with decodelog.py output above
   - Verify EP LOAD sequence parses correctly
   - Verify ECU events appear after EPROM loading

4. **Expected Behavior**:
   - ✅ LOAD names should display as "8796539" and "UM4"
   - ✅ ADDR/LEN should show as hex (0x0000, 0x8000, 0x1c00)
   - ✅ STAT should show "ERR_NOERR"
   - ✅ ECU events should start appearing after record ~59
   - ✅ No UNKNOWN events (all 0xD0-0xD6 should decode)

### Performance Targets

- **Parse Time**: < 0.5s for 38KB file
- **Display Time**: < 0.1s to render 5000 events
- **Memory Usage**: < 50MB total

### Next Phase - Improvements

After validating prototype works correctly:

1. **Add temperature conversion** (ADC → °C)
2. **Add binary formatting** for PORTG
3. **Remove 5000 event limit** (process full file)
4. **Add missing event types**
5. **Implement graphing tab**

## Test Commands

```bash
# Compare outputs side-by-side
cd ~/projects/umod4
build/.venv/bin/python3 tools/src/decodelog.py ~/logs/2025-11-24/log.3 --format hr
head -100 ~/logs/2025-11-24/log.3.hr

# Test viewer
cd logView
./test_viewer.sh ~/logs/2025-11-24/log.3
```

## Success Criteria

✅ Viewer opens in browser
✅ File loads without errors
✅ First 60 events match decodelog.py (minus temperature conversion)
✅ No JavaScript errors in console
✅ Stats display correctly
✅ Performance < 1 second total
