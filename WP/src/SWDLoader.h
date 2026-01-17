#ifndef SWD_LOADER_H
#define SWD_LOADER_H

#include "pico/stdlib.h"
#include "hardware/pio.h"

class SWDLoader {
public:
    // verbose defaults to false to keep console clean
    SWDLoader(PIO pio = pio0, bool verbose_ = false);

    // Public API Lifecycle
    bool connect(uint32_t core = 0, bool halt = true);
    bool load_ram(uint32_t address, const uint32_t* data, uint32_t len_in_bytes);
    bool start(uint32_t pc, uint32_t sp);

    // Cleanup
    void unload();

private:
    // Internal State
    PIO swd_pio;
    uint32_t pio_offset;
    uint32_t pio_sm;
    const pio_program_t* pio_prog;
    float pio_clkdiv;
    bool verbose;
    bool is_initialized = false; // Tracks if PIO hardware is set up

    // Helper functions (Strictly preserved logic)
    bool clear_sticky_errors();
    void wait_for_idle();
    void switch_program(bool read, bool raw = false);
    bool write_cmd(uint32_t cmd, uint32_t data);
    bool write_block(uint32_t addr, const uint32_t* data, uint32_t len_in_words);
    bool write_reg(uint32_t addr, uint32_t data);
    bool read_cmd(uint32_t cmd, uint32_t& data);
    bool read_reg(uint32_t addr, uint32_t &data);
    void idle();
};

// For convenience, a global SWDLoader instance:
extern SWDLoader* swdLoader;
#endif