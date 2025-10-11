import sys

# Number of bytes to skip at the start before decoding the rest of the file
skipBytes = 0

vtaHi = -1
vta = 0
vtaPrev = -1
tPrev = -1
elapsed = -1
cridPrev = -1
crid = -1
epromIdString = ""
currentEpromId = epromIdString
rpm_avg = 0.0
secs=-1
crankref_ts_hi = -1
crankref_ts_lo = -1
cam_ts_hi = -1
cam_ts_lo = -1
frt_inj_on_ts_hi = -1
frt_inj_on_ts_lo = -1
rear_inj_on_ts_hi = -1
rear_inj_on_ts_lo = -1
frt_inj_off_ts_hi = -1
frt_inj_off_ts_lo = -1
rear_inj_off_ts_hi = -1
rear_inj_off_ts_lo = -1

import array as arr
rpm_hist = arr.array('d', [])


logfilename = sys.argv[1]

recordCnt = 0
showBinData = True
address=0

def read(f, readCount, showAddress=False, newLine=True):
    global address
    global showBinData

    bytes = f.read(readCount)
    if (showBinData):
        if (showAddress):
            print(f"0x{address:08X}: ", end="")

        for byte in bytes:
            print(f"{byte:02X} ", end="")

        if (not showAddress and newLine):
            print("")

    address += readCount
    return bytes

with open(logfilename, "rb") as f:

    if (skipBytes > 0) :
        print(f"Skipping {skipBytes} at the start of the log")

        while (skipBytes > 0):
            b = read(f, 1, False);
            skipBytes -= 1

        print(f"Decoding rest of log:")

    while (True):
        b = read(f, 1, True)
        if (len(b) < 1):
            break

        recordCnt = recordCnt+1

        byte = b[0]
        match byte:
            case 0x01:
                # LOG VERSION
                rd = read(f, 3)
                major=rd[0]
                minor=rd[2]
                print(f"{recordCnt:10}: VER: {major}.{minor}")

            case 0x10:
                # CPU Event
                event = read(f, 1)[0]
                print(f"{recordCnt:10}: CPU: {event}")

            case 0x20:
                # LOG_TS_FRT_INJ_ON_U16 (high)
                rd = read(f, 1)
                frt_inj_on_ts_hi=rd[0]

            case 0x21:
                # LOG_TS_FRT_INJ_ON_U16 (lo)
                rd = read(f, 1)
                frt_inj_on_ts_lo=rd[0]
                f_inj_on = (frt_inj_on_ts_hi*256) + frt_inj_on_ts_lo
                print(f"{recordCnt:10}: FIO: {f_inj_on}")

            case 0x22:
                # LOG_TS_FRT_INJ_OFF_U16 (high)
                rd = read(f, 1)
                frt_inj_off_ts_hi=rd[0]

            case 0x23:
                # LOG_TS_FRT_INJ_OFF_U16 (lo)
                rd = read(f, 1)
                frt_inj_off_ts_lo=rd[0]
                f_inj_off = (frt_inj_off_ts_hi*256) + frt_inj_off_ts_lo
                print(f"{recordCnt:10}: FIF: {f_inj_off}")

            case 0x24:
                # LOG_TS_REAR_INJ_ON_U16 (high)
                rd = read(f, 1)
                rear_inj_on_ts_hi=rd[0]

            case 0x25:
                # LOG_TS_REAR_INJ_ON_U16 (lo)
                rd = read(f, 1)
                rear_inj_on_ts_lo=rd[0]
                r_inj_on = (rear_inj_on_ts_hi*256) + rear_inj_on_ts_lo
                print(f"{recordCnt:10}: RIO: {r_inj_on}")

            case 0x26:
                # LOG_TS_REAR_INJ_OFF_U16 (high)
                rd = read(f, 1)
                rear_inj_off_ts_hi=rd[0]

            case 0x27:
                # LOG_TS_REAR_INJ_OFF_U16 (lo)
                rd = read(f, 1)
                rear_inj_off_ts_lo=rd[0]
                r_inj_off = (rear_inj_off_ts_hi*256) + rear_inj_off_ts_lo
                print(f"{recordCnt:10}: RIF: {r_inj_off}")

            case 0x30:
                # LOG_TS_FRT_COIL_ON_U16
                rd = read(f, 3)
                hi=rd[0]
                lo=rd[2]
                f_coil_on = (hi*256) + lo
                print(f"{recordCnt:10}: FCO: {f_coil_on}")

            case 0x32:
                # LOG_TS_FRT_COIL_OFF_U16
                rd = read(f, 3)
                hi=rd[0]
                lo=rd[2]
                f_coil_off = (hi*256) + lo
                print(f"{recordCnt:10}: FCF: {f_coil_off}")

            case 0x34:
                # LOG_TS_REAR_COIL_ON_U16
                rd = read(f, 3)
                hi=rd[0]
                lo=rd[2]
                r_coil_on = (hi*256) + lo
                print(f"{recordCnt:10}: RCO: {r_coil_on}")

            case 0x36:
                # LOG_TS_REAR_COIL_OFF_U16
                rd = read(f, 3)
                hi=rd[0]
                lo=rd[2]
                r_coil_off = (hi*256) + lo
                print(f"{recordCnt:10}: RCF: {r_coil_off}")

            case 0x40:
                # LOG_TS_FRT_COIL_MAN_ON_U16
                rd = read(f, 3)
                hi=rd[0]
                lo=rd[2]
                f_coil_man_on = (hi*256) + lo
                print(f"{recordCnt:10}: FCMO: {f_coil_man_on}")

            case 0x42:
                # LOG_TS_FRT_COIL_MAN_OFF_U16
                rd = read(f, 3)
                hi=rd[0]
                lo=rd[2]
                f_coil_man_off = (hi*256) + lo
                print(f"{recordCnt:10}: FCMF: {f_coil_man_off}")

            case 0x44:
                # LOG_TS_REAR_COIL_MAN_ON_U16
                rd = read(f, 3)
                hi=rd[0]
                lo=rd[2]
                r_coil_man_on = (hi*256) + lo
                print(f"{recordCnt:10}: RCMO: {r_coil_man_on}")

            case 0x46:
                # LOG_TS_REAR_COIL_MAN_OFF_U16
                rd = read(f, 3)
                hi=rd[0]
                lo=rd[2]
                r_coil_man_off = (hi*256) + lo
                print(f"{recordCnt:10}: RCMF: {r_coil_man_off}")

            case 0x50:
                # LOG_TS_FRT_IGN_DLY_0P8
                b = read(f, 1)[0]
                dly= (b/256)*90.0
                print(f"{recordCnt:10}: FID: {dly:.1f}")

            case 0x52:
                # LOG_TS_REAR_IGN_DLY_0P8
                b = read(f, 1)[0]
                dly= (b/256)*90.0
                print(f"{recordCnt:10}: RID: {dly:.1f}")


            case 0x60:
                # 5 msec event
                print(f"{recordCnt:10}: 5MS:")
                ignore = read(f, 1)

            case 0x61:
                # LOG_CRANK_P6_MAX_V
                ignore = read(f, 1)
                print(f"{recordCnt:10}: CMX: Crank Max")

            case 0x62:
                # LOG_FUEL_PUMP_B
                pumpstate = read(f, 1)[0]
                print(f"{recordCnt:10}: FP:  {pumpstate}")

            case 0x70:
                # LOG_ECU_ERROR_L000C_U8
                L000C = read(f, 1)[0]
                print("ELC: {:08b}".format(L000C))

            case 0x71:
                # LOG_ECU_ERROR_L000D_U8
                L000D = read(f, 1)[0]
                print("ELD: {:08b}".format(L000D))

            case 0x72:
                # LOG_ECU_ERROR_L000E_U8
                L000E = read(f, 1)[0]
                print("ELE: {:08b}".format(L000E))

            case 0x73:
                # LOG_ECU_ERROR_L000F_U8
                L000F = read(f, 1)[0]
                print("ELF: {:08b}".format(L000F))

            case 0x80:
                # LOG_RAW_VTA_U16 (hi byte)
                vtaHi = read(f, 1)[0]

            case 0x81:
                # LOG_RAW_VTA_U16 (lo byte)
                if (vtaHi<0):
                    print(f"{recordCnt:10}: ERR: VTA.HI is not valid!")
                else:
                    lo = read(f, 1)[0]

                    vta = (vtaHi*256) + lo
                    if (vta != vtaPrev):
                        print(f"{recordCnt:10}: VTA: {vta}")
                    vtaPrev = vta
                    vtaHi = -1


            case 0x82:
                # LOG_RAW_MAP_U8
                map = read(f, 1)[0]
                print(f"{recordCnt:10}: MAP: {map}")

            case 0x83:
                # LOG_RAW_AAP_U8
                aap = read(f, 1)[0]
                print(f"{recordCnt:10}: AAP: {aap}")

            case 0x84:
                # LOG_RAW_THW_U8
                thw = read(f, 1)[0]
                # thw =
                print(f"{recordCnt:10}: THW: {thw}")

            case 0x85:
                # LOG_RAW_THA_U8
                tha = read(f, 1)[0]
                print(f"{recordCnt:10}: THA: {tha}")

            case 0x86:
                # LOG_RAW_VM_U8
                vm = read(f, 1)[0]
                print(f"{recordCnt:10}: VM:  {vm}")

            case 0x87:
                # LOG_PORTG_DB_U8
                portg = read(f, 1)[0]
                print(f"{recordCnt:10}: PTG: {portg}")

            case 0x90:
                # LOG_TS_CRANKREF_START_U16 (high byte)
                rd = read(f, 1)
                crankref_ts_hi=rd[0]

            case 0x91:
                # LOG_TS_CRANKREF_START_U16 (low byte)
                rd = read(f, 1)
                crankref_ts_lo=rd[0]
                tNow = (crankref_ts_hi*256) + crankref_ts_lo
                print(f"{recordCnt:10}: CRK TS {tNow}")

                if (tPrev > -1):
                    elapsed = tNow - tPrev
                    if (elapsed<0):
                        elapsed += 65536
                tPrev = tNow
                if (elapsed >= 0):
                    rpm = 60000000 / (elapsed * 2 * 6)

                    rpm_hist.append(rpm)
                    if (len(rpm_hist) == 7):
                        rpm_hist.pop(0)
                    rpm_avg = sum(rpm_hist) / len(rpm_hist)

                    print(f"{recordCnt:10}: tNow: {tNow}, elapsed: {elapsed}, RPM-INST {rpm:.0f}, RPM-AVG {rpm_avg:.0f}")

            case 0x92:
                crid = read(f, 1)[0]
                print(f"{recordCnt:10}: CR:  {crid}")
                if (elapsed > 0):
                    print(f"XL, {secs:2}, {vta:3}, {crid:2}, {elapsed:5}, {rpm_avg:5.0f}")

                if (cridPrev >= 0):
                    expectedId = cridPrev+1
                    if (expectedId>11):
                        expectedId = 0
                    if (crid != expectedId):
                        print(f"{recordCnt:10}: ERROR: expected CRID {expectedId}, saw {crid}")

            case 0x93:
                # LOG_CAMSHAFT_U16
                rd = read(f, 1)
                cam_ts_hi=rd[0]

            case 0x94:
                # LOG_CAMSHAFT_U16
                rd = read(f, 1)
                cam_ts_lo=rd[0]
                camTimestamp = (cam_ts_hi*256) + cam_ts_lo
                print(f"{recordCnt:10}: CAM TS: {camTimestamp}")

            case 0x95:
                # LOG_CAM_ERR_U8
                camErr = read(f, 1)[0]
                print(f"{recordCnt:10}: CAM ERR: {camErr:02X}")

            # The EP log events
            case 0xD0:
                # LOG_EP_LOAD_NAME
                # Each write to this address appends the next byte as a char to the EPROM_ID_STR
                c = read(f, 1)[0]
                if (c != 0):
                    epromIdString = "".join([epromIdString, chr(c)])
                else:
                    currentEpromId = epromIdString
                    epromIdString = ""
                    print(f"{recordCnt:10}: LOAD: {currentEpromId}")

            case 0xD1:
                # LOG_EP_LOAD_ADDR
                rd = read(f, 3)
                hi=rd[0]
                lo=rd[2]
                epromStartaddr = (hi*256) + lo
                print(f"{recordCnt:10}: ADDR: 0x{epromStartaddr:04X}")

            case 0xD2:
                # LOG_EP_LOAD_LEN
                rd = read(f, 3)
                hi=rd[0]
                lo=rd[2]
                epromLen = (hi*256) + lo
                print(f"{recordCnt:10}: LEN:  0x{epromLen:04X}")

            case 0xD3:
                # LOG_EP_LOAD_ERR
                loadErr = read(f, 1)[0]
                match loadErr:
                    case 0x00:
                        print(f"{recordCnt:10}: STAT: ERR_NOERR")
                    case 0x01:
                        print(f"{recordCnt:10}: STAT: ERR_NOTFOUND")
                    case 0x02:
                        print(f"{recordCnt:10}: STAT: ERR_NONAME")
                    case 0x03:
                        print(f"{recordCnt:10}: STAT: ERR_CKSUMERR")
                    case 0x04:
                        print(f"{recordCnt:10}: STAT: ERR_VERIFYERR")
                    case 0x05:
                        print(f"{recordCnt:10}: STAT: ERR_BADOFFSET")
                    case 0x06:
                        print(f"{recordCnt:10}: STAT: ERR_BADLENGTH")
                    case 0x07:
                        print(f"{recordCnt:10}: STAT: ERR_NODAUGHTERBOARDKEY")
                    case 0x08:
                        print(f"{recordCnt:10}: STAT: ERR_NOMEMKEY")
                    case 0x09:
                        print(f"{recordCnt:10}: STAT: ERR_BADMEMINFO")
                    case 0x0A:
                        print(f"{recordCnt:10}: STAT: ERR_M3FAIL")
                    case 0x0B:
                        print(f"{recordCnt:10}: STAT: ERR_MISSING_KEY_START")
                    case 0x0C:
                        print(f"{recordCnt:10}: STAT: ERR_MISSING_KEY_LENGTH")
                    case 0x0D:
                        print(f"{recordCnt:10}: STAT: ERR_KEY_M3")
                    case 0x0E:
                        print(f"{recordCnt:10}: STAT: ERR_BAD_M3_BSON_TYPE")
                    case 0x0F:
                        print(f"{recordCnt:10}: STAT: ERR_BAD_M3_VALUE")
                    case 0x10:
                        print(f"{recordCnt:10}: STAT: ERR_NOBINKEY")
                    case 0x11:
                        print(f"{recordCnt:10}: STAT: ERR_BADBINLENGTH")
                    case 0x12:
                        print(f"{recordCnt:10}: STAT: ERR_BADBINSUBTYPE")
                    case _:
                        print(f"{recordCnt:10}: STAT: Unknown error: 0x{loadErr:02X}")

            # The WP log events
            case 0xE0:
                # LOG_CSECS
                csecs = read(f, 1)[0]
                print(f"{recordCnt:10}: CS:  {csecs:02}")

            case 0xE1:
                # LOG_SECS
                secs = read(f, 1)[0]
                print(f"{recordCnt:10}: SEC: {secs:02}")

            case 0xE2:
                # LOG_MINS
                mins = read(f, 1)[0]
                print(f"{recordCnt:10}: MIN: {mins:02}")

            case 0xE3:
                # LOG_HOURS
                hours = read(f, 1)[0]
                print(f"{recordCnt:10}: HRS: {hours:02}")

            case 0xE4:
                # LOG_DATE
                date = read(f, 1)[0]
                print(f"{recordCnt:10}: DT:  {date:02}")

            case 0xE5:
                # LOG_MONTH
                month = read(f, 1)[0]
                print(f"{recordCnt:10}: MON: {month:02}")

            case 0xE6:
                # LOG_YEAR
                year = read(f, 1)[0]
                print(f"{recordCnt:10}: YR:  {year:02}")

            case 0xE7:
                # LOG_FIXTYPE
                fix = read(f, 1)[0]
                print(f"{recordCnt:10}: FIX: {fix}")

            case 0xE8:
                # LOG_PV: 3 args in 10 bytes follow
                lat =  int.from_bytes(read(f, 4, newLine=False), byteorder='little', signed=True) / 10000000.0
                long = int.from_bytes(read(f, 4, newLine=False), byteorder='little', signed=True) / 10000000.0
                vel =  int.from_bytes(read(f, 2), byteorder='little', signed=True) / 10.0
                print(f"{recordCnt:10}: GPS: {lat:.8f} {long:.8f} {vel:.1f}")

            case 0xF0:
                # LOG_WR_TIME: time follows as 2 bytes, LSB first
                wrTime = int.from_bytes(read(f, 2), byteorder='little', signed=False)
                print(f"{recordCnt:10}: WRT: {wrTime} msec")

            case 0xF1:
                # LOG_SYNC_TIME: time follows as 2 bytes, LSB first
                syncTime = int.from_bytes(read(f, 2), byteorder='little', signed=False)
                print(f"{recordCnt:10}: SYT: {syncTime} msec")


            case _:
                print(f"{recordCnt:10}: ERR: Unknown LOGID 0x{byte:02X}")
                read(f, 1)

