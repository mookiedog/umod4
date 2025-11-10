#if !defined EP_LOG_H
#define EP_LOG_H

#include "log_base.h"

// -----------------------------------------------------------------
// Define the log IDs the ECU will be generating.
//
// This file is included by .S and .c files and will be processed by Python scripts,
// so it must only contain #defines!
//

// For the moment:
// In keeping with always writing ECU data in 2-byte chunks (log ID then data),
// the multi-byte EPROM log data will be written as logID, then data.
// The same log ID will be written in front of each data byte.
// Example: writing a 4-byte EPROM M3 with a value of 0x11223344 would get sent as:
//    LOGID_EP_LOAD_M3, 0x44
//    LOGID_EP_LOAD_M3, 0x33
//    LOGID_EP_LOAD_M3, 0x22
//    LOGID_EP_LOAD_M3, 0x11

#define     LOGID_GEN_EP_LOG_VER_VAL_V0                 (0x00)                          // Version 0

#define LOGID_EP_LOAD_NAME                          ((LOGID_EP_BASE) + 0x01)        // The name of the EPROM to be loaded, written as sequence of UTF-8 chars, NULL terminated
#define LOGID_EP_LOAD_ADDR                          ((LOGID_EP_BASE) + 0x02)        // 2 bytes, LSB first
#define LOGID_EP_LOAD_LEN                           ((LOGID_EP_BASE) + 0x03)        // 4 bytes, 2-byte start addr LSB first, then length (LSB first)

#define LOGID_EP_LOAD_ERR_TYPE_U8                       ((LOGID_EP_BASE) + 0x04)        // The error status byte of the most recent load operation
#define LOGID_EP_LOAD_ERR_DLEN                      1
#define     LOGID_EP_LOAD_ERR_VAL_NOERR                 0x00                            //   * no error: the load succeeded
#define     LOGID_EP_LOAD_ERR_VAL_NOTFOUND              0x01                            //   * The specified image name was not found in the BSON lib
#define     LOGID_EP_LOAD_ERR_VAL_NONAME                0x02                            //   * BSON EPROM object does not contain a name key
#define     LOGID_EP_LOAD_ERR_VAL_CKSUMERR              0x03                            //   * BSON memory contents verification failure
#define     LOGID_EP_LOAD_ERR_VAL_VERIFYERR             0x04                            //   * Verification failure after being copied to RAM
#define     LOGID_EP_LOAD_ERR_VAL_BADOFFSET             0x05                            //   * start offset is outside of EPROM size
#define     LOGID_EP_LOAD_ERR_VAL_BADLENGTH             0x06                            //   * the desired start offset plus length goes beyond end of EPROM
#define     LOGID_EP_LOAD_ERR_VAL_NODAUGHTERBOARDKEY    0x07                            //   * BSON doc is missing a DAUGHTERBOARD key
#define     LOGID_EP_LOAD_ERR_VAL_NOMEMKEY              0x08                            //   * BSON doc is missing a MEM key
#define     LOGID_EP_LOAD_ERR_VAL_M3FAIL                0x09                            //   * M3 checksum does not match the binary data
#define     LOGID_EP_LOAD_ERR_VAL_MISSINGKEYSTART       0x0A                            //   * mem object is missing its 'start' key
#define     LOGID_EP_LOAD_ERR_VAL_MISSINGKEYLENGTH      0x0B                            //   * mem object is missing its 'length' key
#define     LOGID_EP_LOAD_ERR_VAL_MISSINGKEYM3          0x0C                            //   * mem object is missing its 'm3' key
#define     LOGID_EP_LOAD_ERR_VAL_BADM3BSONTYPE         0x0D                            //   * m3 key has invalid data type (should be int32 or int64)
#define     LOGID_EP_LOAD_ERR_VAL_BADM3VALUE            0x0E                            //   * int64 m3 values should only contain 32 bits of data
#define     LOGID_EP_LOAD_ERR_VAL_NOBINKEY              0x0F                            //   * mem object is missing its 'bin' key: no binary data present
#define     LOGID_EP_LOAD_ERR_VAL_BADBINLENGTH          0x10                            //   * bin object must be exactly 32768 bytes
#define     LOGID_EP_LOAD_ERR_VAL_BADBINSUBTYPE         0x11                            //   * bin object has invalid BSON subtype

#endif