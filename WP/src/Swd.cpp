#include "Swd.h"
#include "hardware/clocks.h"
#include <algorithm>
#include <stdio.h>

#include "swd.pio.h"

// --- Exclusive Program Management (Internal Tracking) ---
// These remain static to the file to keep the global namespace clean.
static const pio_program* s_pio_prog[2] = {nullptr, nullptr};
static uint16_t s_pio_offset[2] = {0xffff, 0xffff};

// --------------------------------------------------------------------------------
static void pio_remove_exclusive_program(PIO pio)
{
    uint8_t pio_index = pio == pio0 ? 0 : 1;
    const pio_program* current_program = s_pio_prog[pio_index];
    uint16_t current_offset = s_pio_offset[pio_index];
    if(current_program) {
        pio_remove_program(pio, current_program, current_offset);
        s_pio_prog[pio_index] = nullptr;
        s_pio_offset[pio_index] = 0xffff;
    }
}

// --------------------------------------------------------------------------------
static uint16_t pio_change_exclusive_program(PIO pio, const pio_program* prog)
{
    pio_remove_exclusive_program(pio);
    uint8_t pio_index = pio == pio0 ? 0 : 1;
    s_pio_prog[pio_index] = prog;
    s_pio_offset[pio_index] = pio_add_program(pio, prog);
    return s_pio_offset[pio_index];
}

// --------------------------------------------------------------------------------
Swd::Swd(PIO pio, uint32_t swdClk_gpio, uint32_t swdIo_gpio, bool verbose)
    : swd_pio(pio),
      swc(swdClk_gpio),
      swd(swdIo_gpio),
      pio_offset(0),
      pio_sm(0),
      pio_prog(nullptr),
      pio_clkdiv(1.0f),
      verbose(verbose),
      is_initialized(false)
{}

// --------------------------------------------------------------------------------
void Swd::wait_for_idle() {
    uint pull_offset = (pio_prog == &swd_raw_write_program) ? 2 :
                       (pio_prog == &swd_raw_read_program) ? 0 : 5;

#if 0
    // Safety timeout: prevents the host from hanging if the target stops responding.
    // 1,000,000 loops is roughly ~50-100ms depending on CPU clock.
    uint32_t timeout = 1000000;
    while (!pio_sm_is_tx_fifo_empty(swd_pio, pio_sm) || swd_pio->sm[pio_sm].addr != pio_offset + pull_offset) {
        if (--timeout == 0) {
            if (verbose) printf("SWD Timeout: PIO SM stuck at address %d\n", swd_pio->sm[pio_sm].addr - pio_offset);
            return;
        }
    }
#else
    while (!pio_sm_is_tx_fifo_empty(swd_pio, pio_sm) || swd_pio->sm[pio_sm].addr != pio_offset + pull_offset);
#endif
}

// --------------------------------------------------------------------------------
bool Swd::clear_sticky_errors()
{
    // To clear sticky errors, we write to the ABORT register (0x00 on the DP).
    // Bits: 4 (ORUNERRCLR), 3 (WDERRCLR), 2 (STICKYERRCLR), 1 (STICKYCMPCLR)
    // 0x1E clears all common error flags.
    return write_cmd(0x01, 0x1E);
}

// --------------------------------------------------------------------------------
void Swd::switch_program(bool read, bool raw)
{
    wait_for_idle();
    pio_sm_set_enabled(swd_pio, pio_sm, false);
    pio_prog = raw ? (read ? &swd_raw_read_program : &swd_raw_write_program) :
                     (read ? &swd_read_program : &swd_write_program);
    pio_offset = pio_change_exclusive_program(swd_pio, pio_prog);
    if (raw) {
        swd_raw_program_init(swd_pio, pio_sm, pio_offset, swc, swd, read, pio_clkdiv);
    } else {
        swd_program_init(swd_pio, pio_sm, pio_offset, swc, swd, read, pio_clkdiv);
        wait_for_idle();
        swd_pio->irq = 1;
    }
}

// --------------------------------------------------------------------------------
bool Swd::write_cmd(uint32_t cmd, uint32_t data)
{
    if (pio_prog != &swd_write_program) {
        switch_program(false);
    }
    pio_sm_put_blocking(swd_pio, pio_sm, cmd);
    pio_sm_put_blocking(swd_pio, pio_sm, data);
    wait_for_idle();
    if (swd_pio->irq & 0x1) {
        return false;
    }
    return true;
}

// --------------------------------------------------------------------------------
bool Swd::write_block(uint32_t addr, const uint32_t* data, uint32_t len_in_words)
{
    if (!write_cmd(0x0B, addr)) return false;
    for (uint32_t i = 0; i < len_in_words; ++i) {
        if (!write_cmd(0x3B, *data++)) return false;
    }
    return true;
}

// --------------------------------------------------------------------------------
bool Swd::write_reg(uint32_t addr, uint32_t data)
{
    return write_block(addr, &data, 1);
}

// --------------------------------------------------------------------------------
bool Swd::read_cmd(uint32_t cmd, uint32_t& data)
{
    if (pio_prog != &swd_read_program) {
        switch_program(true);
    }
    pio_sm_put_blocking(swd_pio, pio_sm, cmd);
    wait_for_idle();
    if (swd_pio->irq & 0x1) {
        if (verbose) printf("%s: Read ID failed\n", __FUNCTION__);
        return false;
    }
    data = pio_sm_get_blocking(swd_pio, pio_sm);
    return true;
}

// --------------------------------------------------------------------------------
bool Swd::read_reg(uint32_t addr, uint32_t &data)
{
    if (!write_cmd(0x0B, addr)) return false;
    if (!read_cmd(0x1F, data)) return false;
    if (!read_cmd(0x3D, data)) return false;
    return true;
}

// --------------------------------------------------------------------------------
void Swd::idle()
{
    switch_program(false, true);
    pio_sm_put_blocking(swd_pio, pio_sm, 7);
    pio_sm_put_blocking(swd_pio, pio_sm, 0);
}

// --------------------------------------------------------------------------------
bool Swd::connect_target(uint32_t core, bool halt)
{
    if (!is_initialized) {
        gpio_init(2);
        gpio_init(3);
        gpio_disable_pulls(2);
        gpio_pull_up(3);

        uint32_t sys_clk_hz = clock_get_hz(clk_sys);
        pio_clkdiv = sys_clk_hz / (1 * MHZ);

        pio_prog = &swd_raw_write_program;
        pio_offset = pio_change_exclusive_program(swd_pio, &swd_raw_write_program);
        pio_sm = pio_claim_unused_sm(swd_pio, true);

        swd_initial_init(swd_pio, pio_sm, swc, swd);
        swd_raw_program_init(swd_pio, pio_sm, pio_offset, swc, swd, false, pio_clkdiv);
        is_initialized = true;
    } else {
        switch_program(false, true);
    }

    if (verbose) printf("%s: Begin transaction\n", __FUNCTION__);
    pio_sm_put_blocking(swd_pio, pio_sm, 7);
    pio_sm_put_blocking(swd_pio, pio_sm, 0);

    if (verbose) printf("%s: SWD Mode\n", __FUNCTION__);
    pio_sm_put_blocking(swd_pio, pio_sm, 8-1);
    pio_sm_put_blocking(swd_pio, pio_sm, 0xFF);

    if (verbose) printf("%s: Tag\n", __FUNCTION__);
    pio_sm_put_blocking(swd_pio, pio_sm, 32*4+4+8-1);
    pio_sm_put_blocking(swd_pio, pio_sm, 0x6209F392);
    pio_sm_put_blocking(swd_pio, pio_sm, 0x86852D95);
    pio_sm_put_blocking(swd_pio, pio_sm, 0xE3DDAFE9);
    pio_sm_put_blocking(swd_pio, pio_sm, 0x19BC0EA2);
    pio_sm_put_blocking(swd_pio, pio_sm, 0x1A0);

    if (verbose) printf("%s: Line Reset\n", __FUNCTION__);
    pio_sm_put_blocking(swd_pio, pio_sm, 58-1);
    pio_sm_put_blocking(swd_pio, pio_sm, 0xFFFFFFFF);
    pio_sm_put_blocking(swd_pio, pio_sm, 0x003FFFF);

    if (verbose) printf("%s: Target Select\n", __FUNCTION__);
    wait_for_idle();
    pio_sm_set_enabled(swd_pio, pio_sm, false);
    pio_prog = &swd_write_ignore_error_program;
    pio_offset = pio_change_exclusive_program(swd_pio, pio_prog);
    swd_program_init(swd_pio, pio_sm, pio_offset, swc, swd, false, pio_clkdiv);
    wait_for_idle();
    swd_pio->irq = 1;
    pio_sm_put_blocking(swd_pio, pio_sm, 0x19);
    pio_sm_put_blocking(swd_pio, pio_sm, 0x01002927 | (core << 28));

    if (verbose) printf("%s: Read ID\n", __FUNCTION__);
    uint32_t id;
    if (!read_cmd(0x25, id)) {
        if (verbose) printf("%s: Read ID failed\n", __FUNCTION__);
        return false;
    }
    if (verbose) printf("Received ID: %08x\n", id);

    if (core != 0xf && id != 0x0bc12477) return false;

    #if 0
    // FIXME add this later
    // If the previous session crashed or timed out during a bus access,
    // we should clear sticky errors here before trying to Power Up the DP.
    if (verbose) printf("%s: Clearing sticky errors\n", __FUNCTION__);
    if (!clear_sticky_errors()) {
        if (verbose) printf("%s: Abort/Clear failed\n", __FUNCTION__);
        return false;
    }
    #endif

    if (verbose) printf("%s: Abort\n", __FUNCTION__);
    if (!write_cmd(0x01, 0x1E)) {
        if (verbose) printf("%s: Abort failed\n", __FUNCTION__);
        return false;
    }

    if (verbose) printf("%s: Select\n", __FUNCTION__);
    if (!write_cmd(0x31, 0)) {
        if (verbose) printf("%s: Select failed\n", __FUNCTION__);
        return false;
    }

    if (verbose) printf("%s: Ctrl/Stat\n", __FUNCTION__);
    if (!write_cmd(0x29, 0x50000001)) {
        if (verbose) printf("%s: Ctrl power up failed\n", __FUNCTION__);
        return false;
    }

    uint32_t status;
    if (!read_cmd(0x0D, status)) {
        if (verbose) printf("%s: Read status on power up failed\n", __FUNCTION__);
        return false;
    }
    if (verbose) printf("%s: Status: %08x\n", __FUNCTION__, status);
    if ((status & 0xA0000000) != 0xA0000000) {
        if (verbose) printf("%s: Power up not acknowledged\n", __FUNCTION__);
        return false;
    }

    if (core != 0xf) {
        if (verbose) printf("%s: Setup memory access\n", __FUNCTION__);
        if (!write_cmd(0x23, 0xA2000052)) {
            if (verbose) printf("%s: Memory access setup failed\n", __FUNCTION__);
            return false;
        }

        if (halt) {
            if (verbose) printf("%s: Halt CPU\n", __FUNCTION__);
            if (!write_reg(0xe000edf0, 0xA05F0003)) {
                if (verbose) printf("%s: Halt failed\n", __FUNCTION__);
                return false;
            }
        }
    } else {
        if (!write_cmd(0x29, 0x00000001)) {
            if (verbose) printf("%s: Clear reset failed\n", __FUNCTION__);
            return false;
        }
    }

    idle();
    if (verbose) printf("%s: Connect complete\n", __FUNCTION__);
    return true;
}

// --------------------------------------------------------------------------------
bool Swd::write_target_mem(uint32_t target_addr, const uint32_t* data, uint32_t len_in_bytes)
{
    if (verbose) printf("%s: Writing %d bytes at %08x\n", __FUNCTION__, len_in_bytes, target_addr);
    idle();
    constexpr uint32_t BLOCK_SIZE = 1024;

    for (uint32_t i = 0; i < len_in_bytes; ) {
        uint32_t block_len_in_words = std::min((BLOCK_SIZE - ((target_addr + i) & (BLOCK_SIZE - 1))) >> 2, (len_in_bytes - i) >> 2);
        if (!write_block(target_addr + i, &data[i >> 2], block_len_in_words)) {
            if (verbose) printf("%s: Block write failed\n", __FUNCTION__);
            return false;
        }
        i += block_len_in_words << 2;
    }

    #if  0
    // Verify first word
    uint32_t check_data;
    if (!read_reg(target_addr, check_data)) {
        if (verbose) printf("%s: Read failed\n", __FUNCTION__);
        return false;
    }
    if (check_data != data[0]) {
        if (verbose) printf("%s: Verify failed at %08x, %08x != %08x\n", __FUNCTION__, target_addr, check_data, data[0]);
        return false;
    }
    #endif

    idle();
    return true;
}

// --------------------------------------------------------------------------------
/**
 * Reads a small block of memory from the target.
 * @param target_addr    The starting address (must be word-aligned).
 * @param data           Pointer to the buffer to be filled.
 * @param len_in_bytes   Length to read. Max 1024 bytes (1KB).
 * @return true if successful, false otherwise.
 */
bool Swd::read_target_mem(uint32_t target_addr, uint32_t* data, uint32_t len_in_bytes)
{
    if (verbose) printf("%s: Reading %d bytes from %08x\n", __FUNCTION__, len_in_bytes, target_addr);

    if ((target_addr & 0x3) || (len_in_bytes & 0x3)) return false;

    // 1. CLEAR ERRORS & IDLE
    // This clears the "Sticky Error" and "Sticky Compare" flags.
    // If the previous 10ms-ago call hit a glitch, the AP will be locked until this happens.
    if (!write_cmd(0x01, 0x1E)) return false;
    idle();

    // 2. FORCE AP CONFIG (CSW)
    // Re-verify that the AP is set for 32-bit word access with auto-increment.
    // Address 0x00 on the AP (Command 0x23).
    if (!write_cmd(0x23, 0xA2000052)) return false;

    // 3. SET STARTING ADDRESS (TAR)
    // Address 0x08 on the AP (Command 0x0B).
    if (!write_cmd(0x0B, target_addr)) return false;

    uint32_t words_to_read = len_in_bytes >> 2;
    uint32_t dummy;

    // 4. PRIME THE PIPELINE
    // Trigger the fetch for the first word.
    if (!read_cmd(0x1F, dummy)) return false;

    // 5. THE DATA LOOP
    for (uint32_t i = 0; i < words_to_read; ++i) {
        // To get the data and trigger the next increment:
        // For all words, we read from the Data Read/Write register (0x1F).
        // This is the most consistent way to handle auto-increment in a loop.
        if (!read_cmd(0x1F, data[i])) return false;
    }

    // 6. FINALIZE & CLEANUP
    // After the loop, the AP has actually fetched one word TOO MANY.
    // We read RDBUFF (0x0D) to clear that pending result out of the DP.
    if (!read_cmd(0x0D, dummy)) return false;

    idle();
    return true;
}

// --------------------------------------------------------------------------------
bool Swd::start_target(uint32_t pc, uint32_t sp)
{
    idle();
    write_reg(0xe000e180, 0xFFFFFFFF);
    write_reg(0xe000e280, 0xFFFFFFFF);
    if (!write_reg(0xe000ed08, 0x20000100)) {
        if (verbose) printf("Failed to set VTOR\n");
        return false;
    }
    if (verbose) printf("%s: Set PC: 0x%08X\n", __FUNCTION__, pc);
    if (!write_reg(0xe000edf8, pc) || !write_reg(0xe000edf4, 0x1000F)) {
        if (verbose) printf("%s: Failed to set PC\n", __FUNCTION__);
        return false;
    }
    if (verbose) printf("%s: Set SP: %08X\n", __FUNCTION__, sp);
    if (!write_reg(0xe000edf8, sp) || !write_reg(0xe000edf4, 0x1000D)) {
        if (verbose) printf("%s: Failed to set SP\n", __FUNCTION__);
        return false;
    }
    idle();
    if (verbose) printf("%s: Resuming CPU at PC: 0x%08X\n", __FUNCTION__, pc);
    if (!write_reg(0xe000edf0, 0xA05F0001)) {
        if (verbose) printf("%s: Start failed\n", __FUNCTION__);
        return false;
    }
    idle();
    wait_for_idle();
    return true;
}

// --------------------------------------------------------------------------------
void Swd::unload()
{
    pio_sm_set_enabled(swd_pio, pio_sm, false);
    pio_remove_exclusive_program(swd_pio);
    pio_sm_unclaim(swd_pio, pio_sm);
    is_initialized = false;
}