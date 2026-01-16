#include "SWDLoader.h"
#include "hardware/clocks.h"
#include <algorithm>        // min
#include <stdio.h>

#include "swd.pio.h"

// --- Exclusive Program Management (Internal Tracking) ---
static const pio_program* s_pio_prog[2] = {nullptr, nullptr};
static uint16_t s_pio_offset[2] = {0xffff, 0xffff};

void pio_remove_exclusive_program(PIO pio) {
    uint8_t pio_index = pio == pio0 ? 0 : 1;
    const pio_program* current_program = s_pio_prog[pio_index];
    uint16_t current_offset = s_pio_offset[pio_index];
    if(current_program) {
        pio_remove_program(pio, current_program, current_offset);
        s_pio_prog[pio_index] = nullptr;
        s_pio_offset[pio_index] = 0xffff;
    }
}

uint16_t pio_change_exclusive_program(PIO pio, const pio_program* prog) {
    pio_remove_exclusive_program(pio);
    uint8_t pio_index = pio == pio0 ? 0 : 1;
    s_pio_prog[pio_index] = prog;
    s_pio_offset[pio_index] = pio_add_program(pio, prog);
    return s_pio_offset[pio_index];
}

// --- Class Implementation ---

SWDLoader::SWDLoader(PIO pio, bool verbose_) : swd_pio(pio), pio_offset(0), pio_sm(0), pio_prog(nullptr), pio_clkdiv(1.0f), verbose(verbose_) {}

void SWDLoader::wait_for_idle() {
    uint pull_offset = (pio_prog == &swd_raw_write_program) ? 2 :
                       (pio_prog == &swd_raw_read_program) ? 0 : 5;
    while (!pio_sm_is_tx_fifo_empty(swd_pio, pio_sm) || swd_pio->sm[pio_sm].addr != pio_offset + pull_offset);
}

void SWDLoader::switch_program(bool read, bool raw) {
    wait_for_idle();
    pio_sm_set_enabled(swd_pio, pio_sm, false);
    pio_prog = raw ? (read ? &swd_raw_read_program : &swd_raw_write_program) :
                     (read ? &swd_read_program : &swd_write_program);
    pio_offset = pio_change_exclusive_program(swd_pio, pio_prog);
    if (raw) {
        swd_raw_program_init(swd_pio, pio_sm, pio_offset, 2, 3, read, pio_clkdiv);
    } else {
        swd_program_init(swd_pio, pio_sm, pio_offset, 2, 3, read, pio_clkdiv);
        wait_for_idle();
        swd_pio->irq = 1;
    }
}

void SWDLoader::unload_pio() {
    pio_sm_set_enabled(swd_pio, pio_sm, false);
    pio_remove_exclusive_program(swd_pio);
    pio_sm_unclaim(swd_pio, pio_sm);
}

bool SWDLoader::write_cmd(uint cmd, uint data) {
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

bool SWDLoader::write_block(uint addr, const uint* data, uint len_in_words) {
    if (!write_cmd(0x0B, addr)) return false;
    for (uint i = 0; i < len_in_words; ++i) {
        if (!write_cmd(0x3B, *data++)) return false;
    }
    return true;
}

bool SWDLoader::write_reg(uint addr, uint data) {
    return write_block(addr, &data, 1);
}

bool SWDLoader::read_cmd(uint cmd, uint& data) {
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

bool SWDLoader::read_reg(uint addr, uint &data) {
    if (!write_cmd(0x0B, addr)) return false;
    if (!read_cmd(0x1F, data)) return false;
    if (!read_cmd(0x3D, data)) return false;
    return true;
}

void SWDLoader::idle() {
    switch_program(false, true);
    pio_sm_put_blocking(swd_pio, pio_sm, 7);
    pio_sm_put_blocking(swd_pio, pio_sm, 0);
}

bool SWDLoader::connect(bool first, uint core) {
    if (first) {
        pio_prog = &swd_raw_write_program;
        pio_offset = pio_change_exclusive_program(swd_pio, &swd_raw_write_program);
        pio_sm = pio_claim_unused_sm(swd_pio, true);
        swd_initial_init(swd_pio, pio_sm, 2, 3);
        swd_raw_program_init(swd_pio, pio_sm, pio_offset, 2, 3, false, pio_clkdiv);
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
    swd_program_init(swd_pio, pio_sm, pio_offset, 2, 3, false, pio_clkdiv);
    wait_for_idle();
    swd_pio->irq = 1;
    pio_sm_put_blocking(swd_pio, pio_sm, 0x19);
    pio_sm_put_blocking(swd_pio, pio_sm, 0x01002927 | (core << 28));

    if (verbose) printf("%s: Read ID\n", __FUNCTION__);
    uint id;
    if (!read_cmd(0x25, id)) {
        if (verbose) printf("%s: Read ID failed\n", __FUNCTION__);
        return false;
    }
    if (verbose) printf("Received ID: %08x\n", __FUNCTION__, id);

    if (core != 0xf && id != 0x0bc12477) return false;

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

    uint status;
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

        if (verbose) printf("%s: Halt CPU\n", __FUNCTION__);
        if (!write_reg(0xe000edf0, 0xA05F0003)) {
            if (verbose) printf("%s: Halt failed\n", __FUNCTION__);
            return false;
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

bool SWDLoader::load(uint address, const uint* data, uint len_in_bytes) {
    if (verbose) printf("%s: Loading %d bytes at %08x\n", __FUNCTION__, len_in_bytes, address);
    idle();
    constexpr uint BLOCK_SIZE = 1024;
    uint block_len_in_words = std::min((BLOCK_SIZE - (address & (BLOCK_SIZE - 1))) >> 2, len_in_bytes >> 2);
    for (uint i = 0; i < len_in_bytes; ) {
        if (!write_block(address + i, &data[i >> 2], block_len_in_words)) {
            if (verbose) printf("%s: Block write failed\n", __FUNCTION__);
            return false;
        }
        i += block_len_in_words << 2;
        block_len_in_words = std::min(BLOCK_SIZE >> 2, (len_in_bytes - i) >> 2);
    }

    uint j = 0;
    {
        uint check_data;
        if (!read_reg(address + j, check_data)) {
            if (verbose) printf("%s: Read failed\n", __FUNCTION__);
            return false;
        }
        if (check_data != data[j >> 2]) {
            if (verbose) printf("%s: Verify failed at %08x, %08x != %08x\n", __FUNCTION__, address + j, check_data, data[j >> 2]);
            return false;
        }
    }
    idle();
    return true;
}

bool SWDLoader::start(uint pc, uint sp) {
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

bool SWDLoader::swd_reset_internal() {
    gpio_init(2);
    gpio_init(3);
    gpio_disable_pulls(2);
    gpio_pull_up(3);
    uint sys_clk_hz = clock_get_hz(clk_sys);
    pio_clkdiv = sys_clk_hz / (1 * MHZ);
    bool ok = connect(true, 0xf);
    if (verbose) printf("%s: Reset %s\n", __FUNCTION__, ok ? "OK" : "Fail");
    return ok;
}

bool SWDLoader::swd_reset() {
    bool ok = swd_reset_internal();
    unload_pio();
    return ok;
}

bool SWDLoader::swd_load_program_internal(const uint* addresses, const uint** data, const uint* data_len_in_bytes, uint num_sections, uint pc, uint sp, bool use_xip_as_ram) {
    bool ok = swd_reset_internal();
    if (!ok) return false;
    if (verbose) printf("%s: Connecting\n", __FUNCTION__);
    ok = connect(false, 0);
    if (verbose) printf("%s: Connected core 0 %s\n", __FUNCTION__, ok ? "OK" : "Fail");
    if (!ok) return false;
    if (use_xip_as_ram) {
        if (verbose) printf("%s: Disable XIP\n", __FUNCTION__);
        if (!write_reg(0x14000000, 0)) {
            if (verbose) printf("%s: Disable XIP failed\n", __FUNCTION__);
            return false;
        }
    }
    for (uint i = 0; i < num_sections; ++i) {
        if (!load(addresses[i], data[i], data_len_in_bytes[i])) {
            if (verbose) printf("%s: Failed to load section %d\n", __FUNCTION__, i);
            return false;
        }
    }
    return start(pc, sp);
}

bool SWDLoader::swd_load_program(const uint* addresses, const uint** data, const uint* data_len_in_bytes, uint num_sections, uint pc, uint sp, bool use_xip_as_ram) {
    bool ok = swd_load_program_internal(addresses, data, data_len_in_bytes, num_sections, pc, sp, use_xip_as_ram);
    unload_pio();
    return ok;
}