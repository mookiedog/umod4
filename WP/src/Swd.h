#ifndef SWD_LOADER_H
#define SWD_LOADER_H

#include "pico/stdlib.h"
#include "hardware/pio.h"

class Swd {
public:
    Swd(PIO pio, uint32_t swdClk_gpio, uint32_t swdIo_gpio, bool verbose_ = false);

    bool connect_target(uint32_t core = 0, bool halt = true);
    bool write_target_mem(uint32_t target_addr, const uint32_t* data, uint32_t len_in_bytes);
    bool read_target_mem(uint32_t target_addr, uint32_t* data, uint32_t len_in_bytes);
    bool start_target(uint32_t pc, uint32_t sp);

    void unload();

private:
    PIO swd_pio;
    uint32_t swc;       // SWD Clock GPIO
    uint32_t swd;       // SWD IO GPIO
    uint32_t pio_offset;
    uint32_t pio_sm;
    const pio_program_t* pio_prog;
    float pio_clkdiv;
    bool verbose;
    bool is_initialized = false; // Tracks if PIO hardware is set up

    // Helper functions
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

// For convenience, a global Swd instance:
extern Swd* swd;
#endif