#ifndef CRC_H
#define CRC_H

#include "stdint.h"

class Crc
{
 private:

 public:
  static uint16_t crc16(uint32_t bufferLength, uint8_t* buffer);
  static uint16_t crc16(uint32_t bufferLength, uint8_t* buffer, int seed);
  static void     crc16(uint32_t newByte, uint16_t* crc);

  static uint8_t crc7_byte(uint8_t crc, uint8_t data);
  static uint8_t crc7(uint8_t crc, const uint8_t *buffer, uint32_t len);
};

#endif