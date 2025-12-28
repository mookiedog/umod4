# SDIO Speed Modes and Performance Analysis

## Available Speed Modes

The SDIO_RP2350 library supports the following speed modes:

| Mode | Frequency | Speed Type | Notes |
|------|-----------|------------|-------|
| `SDIO_INITIALIZE` | 300 kHz | Standard | Initialization only |
| `SDIO_MMC` | 20 MHz | Standard | Old MMC cards, proven stable |
| `SDIO_STANDARD` | 25 MHz | Standard | SD 1.0 spec default speed |
| `SDIO_HIGHSPEED` | 50 MHz | High-Speed | Requires CMD6 switch to HS mode |
| `SDIO_HIGHSPEED_OVERCLOCK` | 75 MHz | High-Speed | Experimental, not recommended |

**Current Setting:** `SDIO_STANDARD` (25 MHz)

## Measured Performance (Single 512-Byte Block)

Based on actual measurements at 25 MHz SDIO_STANDARD mode:

### READ Operations

- **Command time:** 205-343 μs (average ~250 μs)
- **DMA start time:** 107-323 μs (average ~240 μs)
- **Poll time (actual transfer):** 79-89 μs (average ~85 μs)
- **Total time:** 539-607 μs (average ~575 μs)
- **Measured throughput:** ~870 KB/s per block

### WRITE Operations

- **Command time:** 211-413 μs (average ~270 μs)
- **DMA start time:** 317-491 μs (average ~370 μs)
- **Poll time (transfer + card programming):** 671-846 μs (average ~760 μs)
- **Total time:** 1280-1509 μs (average ~1400 μs)
- **Measured throughput:** ~355 KB/s per block

### Performance Bottlenecks

The actual performance is significantly slower than theoretical due to:

1. **PIO Program Swapping:** Library unloads/reloads PIO programs between RX and TX operations (~200-400 μs overhead)
2. **Command Overhead:** Command execution takes 200-400 μs instead of theoretical 4 μs
3. **DMA Setup:** DMA initialization adds 100-500 μs per operation
4. **Write Programming Time:** SD card internal programming adds ~760 μs per write

### Comparison to SPI Mode

Even with current bottlenecks, SDIO provides better performance than SPI:

- **SPI mode:** ~3 MB/s = ~600 KB/s per 512-byte block
- **SDIO read:** ~870 KB/s per block (1.5× faster than SPI)
- **SDIO write:** ~355 KB/s per block (0.6× slower than SPI for single blocks)

Note: LittleFS performs many small single-block operations. Multi-block transfers would amortize overhead significantly.

## Multi-Block Transfer Performance

For multi-block transfers, command overhead is amortized:

**Example: 8 blocks (4096 bytes) at 50 MHz:**
- Command: 3.84 μs (once)
- Data: 8 × 20.8 μs = 166.4 μs
- Stop: 2 μs
- **Total:** 172.2 μs = **23.8 MB/s**

Multi-block transfers approach theoretical maximum much closer than single-block.

## Recommendations

### Conservative (Current):
- **Mode:** `SDIO_MMC` (20 MHz)
- **Pros:** Proven stable, wide card compatibility
- **Cons:** ~50% slower than SDIO_STANDARD
- **Throughput:** 6-10 MB/s (vs 3 MB/s SPI = 2-3× faster)

### Balanced:
- **Mode:** `SDIO_STANDARD` (25 MHz)
- **Pros:** SD spec default, good compatibility
- **Cons:** Some cards may need fallback to 20 MHz
- **Throughput:** 8-12 MB/s (vs 3 MB/s SPI = 2.5-4× faster)

### Aggressive:
- **Mode:** `SDIO_HIGHSPEED` (50 MHz)
- **Pros:** 2× faster than STANDARD
- **Cons:** Requires CMD6 to switch card to HS mode, may not work on all cards
- **Throughput:** 13-25 MB/s (vs 3 MB/s SPI = 4-8× faster)
- **Note:** Library has automatic fallback if CRC errors occur

## Switching to Higher Speed

To change from 20 MHz to 25 MHz or 50 MHz:

### Option 1: Change to SDIO_STANDARD (25 MHz)

**In `WP/src/sdio/sdio_rp2350_config.h`:**
```c
// Change from:
#define SDIO_DEFAULT_SPEED SDIO_MMC

// To:
#define SDIO_DEFAULT_SPEED SDIO_STANDARD
```

**In `WP/src/SdCardSDIO.cpp` init():**
```cpp
// Change from:
timing = rp2350_sdio_get_timing(SDIO_MMC);

// To:
timing = rp2350_sdio_get_timing(SDIO_STANDARD);
```

**In `WP/src/SdCardSDIO.h`:**
```cpp
// Update reported frequency:
uint32_t getClockFrequency_Hz() const override { return 25000000; }
```

### Option 2: Enable SDIO_HIGHSPEED (50 MHz)

Requires additional CMD6 to switch card to high-speed mode. See reference implementation in `/home/robin/projects/SDIO_RP2350/src/sdfat_sdcard_rp2350.cpp` lines 345-430 for complete high-speed initialization sequence.

**Key steps:**
1. After CMD16, issue CMD6 to check if card supports high-speed
2. Issue CMD6 with mode switch to enable high-speed
3. Call `rp2350_sdio_init(rp2350_sdio_get_timing(SDIO_HIGHSPEED))`
4. Test communication - library will auto-fallback on CRC errors

## Performance Testing

After changing speed, verify with:

1. **Init succeeds** - Card initializes without errors
2. **testCard() passes** - Read operations work
3. **Filesystem mounts** - LittleFS can mount existing filesystem
4. **Write verification** - Create/write/read files successfully
5. **Sustained operation** - No CRC errors during continuous logging
6. **Speed measurement** - Time large file writes to confirm throughput

## Conclusion

**Current Status:** Running at 25 MHz SDIO_STANDARD mode

- Single-block performance is limited by library overhead (~870 KB/s reads, ~355 KB/s writes)
- Performance is comparable to SPI mode for single-block operations
- Multi-block transfers would significantly improve throughput by amortizing overhead
- Further optimization would require modifying the SDIO library to keep both RX and TX PIO programs loaded simultaneously
