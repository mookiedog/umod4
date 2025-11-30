#!/home/robin/projects/umod4/build/.venv/bin/python3
"""
Log Chunk Decoder - Mirrors the JavaScript parser logic

Decodes binary log events into HTML + binary data format exactly
like the browser parser does, ensuring consistency.
"""

import sys
import os
from pathlib import Path

# Add Logsyms to path
venv_path = Path(__file__).parent.parent / "build" / ".venv"
site_packages = venv_path / "lib" / f"python{sys.version_info.major}.{sys.version_info.minor}" / "site-packages"
sys.path.insert(0, str(site_packages))

import Logsyms as ls
L = ls.Logsyms

# Configuration constants
BYTES_PER_LINE = 4  # Maximum number of bytes to display per line in binary output


class TimeKeeper:
    """Manages 16-bit to 64-bit timestamp reconstruction (matches JS version)"""

    def __init__(self):
        self.time_ns = 0           # Absolute time in nanoseconds
        self.prev_ts = -1          # Previous 16-bit timestamp
        self.TIMER_MAX = 65536     # 16-bit wraparound
        self.TICKS_TO_NS = 2000    # 2us per tick

    def process_timestamp(self, ts_16bit):
        """Process timestamped event and advance time"""
        if self.prev_ts >= 0:
            delta_ticks = ts_16bit - self.prev_ts
            if delta_ticks < 0:
                delta_ticks += self.TIMER_MAX
            self.time_ns += delta_ticks * self.TICKS_TO_NS
        self.prev_ts = ts_16bit

    def advance_by_ns(self, delta_ns):
        """Advance time for untimestamped events"""
        self.time_ns += delta_ns

    def get_time_sec(self):
        """Get current time in seconds"""
        return self.time_ns / 1e9

    def get_time_ns(self):
        """Get current time in nanoseconds"""
        return self.time_ns


class LogChunkDecoder:
    """Decodes a chunk of log events (mirrors JS parser logic)"""

    def __init__(self, file_handle, indexer):
        self.file = file_handle
        self.indexer = indexer
        self.L = L

        # Binary data tracking
        self.current_event_bytes = []
        self.current_event_start_offset = 0
        self.byte_offset = 0

        # String accumulation (for displaying complete string on NULL char)
        self.find_name_buffer = ''
        self.find_name_start_event = -1
        self.load_name_buffer = ''
        self.load_name_start_event = -1

    def decode_chunk(self, start_event, end_event, timekeeper=None):
        """Decode events from start to end (exclusive)"""
        if timekeeper is None:
            timekeeper = TimeKeeper()

        records = []

        # Clamp range
        start_event = max(0, start_event)
        end_event = min(self.indexer.total_events, end_event)

        if start_event >= end_event:
            return records

        # Check if we're starting mid-string - need to read backwards to find string start
        actual_start = self._find_string_start(start_event)

        # Seek to actual start event
        offset = self.indexer.get_event_offset(actual_start)
        if offset is None:
            return records

        self.file.seek(offset)
        self.byte_offset = offset

        # Decode events from actual_start, but only return records from start_event onwards
        for event_num in range(actual_start, end_event):
            record = self._decode_event(event_num, timekeeper)
            if event_num >= start_event:
                records.append(record)

        return records

    def _find_string_start(self, start_event):
        """
        Find the actual start event when start_event might be mid-string.

        Reads backwards from start_event to find if we're in the middle of a
        FIND_NAME or LOAD_NAME string. Returns the event number where we should
        actually start decoding.
        """
        if start_event == 0:
            return 0

        # Read backwards up to 256 events (max reasonable string length)
        max_lookback = min(start_event, 256)

        for lookback in range(1, max_lookback + 1):
            check_event = start_event - lookback
            offset = self.indexer.get_event_offset(check_event)
            if offset is None:
                break

            # Peek at the event ID
            saved_pos = self.file.tell()
            self.file.seek(offset)
            logid_byte = self.file.read(1)
            self.file.seek(saved_pos)

            if len(logid_byte) < 1:
                break

            logid = logid_byte[0]

            # If we hit a FIND_NAME or LOAD_NAME, keep looking for the start
            if logid == self.L.LOGID_EP_FIND_NAME_TYPE_U8 or logid == self.L.LOGID_EP_LOAD_NAME_TYPE_U8:
                # Read the character byte
                self.file.seek(offset + 1)
                ch_byte = self.file.read(1)
                self.file.seek(saved_pos)

                if len(ch_byte) == 1:
                    ch = ch_byte[0]
                    if ch == 0:
                        # Found NULL terminator - string ends here, so we're not mid-string
                        return start_event
                    # Non-NULL character - we're potentially mid-string, keep looking back
                    continue
            else:
                # Hit a non-string event - we weren't mid-string after all
                return start_event

        # If we looked back and found string chars, start from the first char we found
        return max(0, start_event - max_lookback)

    def _start_event(self):
        """Start tracking a new event's binary data"""
        self.current_event_bytes = []
        self.current_event_start_offset = self.byte_offset

    def _read_u8(self):
        """Read unsigned 8-bit value"""
        b = self.file.read(1)
        if len(b) < 1:
            return None
        val = b[0]
        self.byte_offset += 1
        self.current_event_bytes.append(val)
        return val

    def _read_u16_le(self):
        """Read unsigned 16-bit little-endian"""
        b = self.file.read(2)
        if len(b) < 2:
            return None
        val = int.from_bytes(b, byteorder='little', signed=False)
        self.byte_offset += 2
        self.current_event_bytes.extend(b)
        return val

    def _read_i16_le(self):
        """Read signed 16-bit little-endian"""
        b = self.file.read(2)
        if len(b) < 2:
            return None
        val = int.from_bytes(b, byteorder='little', signed=True)
        self.byte_offset += 2
        self.current_event_bytes.extend(b)
        return val

    def _read_bytes(self, n):
        """Read n bytes"""
        b = self.file.read(n)
        if len(b) < n:
            return None
        self.byte_offset += n
        self.current_event_bytes.extend(b)
        return list(b)

    def _format_record(self, record_num, tk):
        """Format record prefix (matches JS)"""
        elapsed = tk.get_time_sec()
        return f'<span class="label">Rec#{record_num:6d}</span> [{elapsed:10.4f}s]'

    def _pad_event(self, name):
        """Pad event name to 12 chars (matches JS)"""
        target_len = 12
        if len(name) >= target_len:
            return name
        return name + ('&nbsp;' * (target_len - len(name)))

    def _decode_event(self, event_num, tk):
        """Decode a single event (mirrors JS switch statement)"""
        self._start_event()

        logid = self._read_u8()
        if logid is None:
            return None

        record_num = event_num + 1  # 1-indexed display
        prefix = self._format_record(record_num, tk)
        line = ''

        # ===== GENERAL IDs =====
        if logid == L.LOGID_GEN_ECU_LOG_VER_TYPE_U8:
            ver = self._read_u8()
            line = f'{prefix}: <span class="event">{self._pad_event("ECU_VER")}</span>: <span class="value">{ver}</span>'

        elif logid == L.LOGID_GEN_EP_LOG_VER_TYPE_U8:
            ver = self._read_u8()
            line = f'{prefix}: <span class="event">{self._pad_event("EP_VER")}</span>: <span class="value">{ver}</span>'

        elif logid == L.LOGID_GEN_WP_LOG_VER_TYPE_U8:
            ver = self._read_u8()
            line = f'{prefix}: <span class="event">{self._pad_event("WP_VER")}</span>: <span class="value">{ver}</span>'

        # ===== ECU Events =====
        elif logid == L.LOGID_ECU_CPU_EVENT_TYPE_U8:
            evt = self._read_u8()
            line = f'{prefix}: <span class="event">{self._pad_event("CPU_EVT")}</span>: <span class="value">{evt}</span>'
            tk.advance_by_ns(1)

        elif logid == L.LOGID_ECU_T1_OFLO_TYPE_TS:
            oflo_ts = self._read_u16_le()
            tk.process_timestamp(oflo_ts)
            line = f'{prefix}: <span class="event">{self._pad_event("OFLO")}</span>: <span class="value">{oflo_ts}</span>'

        elif logid == L.LOGID_ECU_T1_HOFLO_TYPE_TS:
            hoflo_ts = self._read_u16_le()
            tk.process_timestamp(hoflo_ts)
            line = f'{prefix}: <span class="event">{self._pad_event("HOFLO")}</span>: <span class="value">{hoflo_ts}</span>'

        elif logid == L.LOGID_ECU_L4000_EVENT_TYPE_U8:
            val = self._read_u8()
            line = f'{prefix}: <span class="event">{self._pad_event("L4000")}</span>: <span class="value">{val}</span>'
            tk.advance_by_ns(1)

        # Injection events
        elif logid == L.LOGID_ECU_F_INJ_ON_TYPE_PTS:
            fi_on = self._read_u16_le()
            line = f'{prefix}: <span class="event">{self._pad_event("FI_ON")}</span>: <span class="value">{fi_on}</span>'
            tk.advance_by_ns(1)

        elif logid == L.LOGID_ECU_F_INJ_DUR_TYPE_U16:
            fi_dur = self._read_u16_le()
            line = f'{prefix}: <span class="event">{self._pad_event("FI_DUR")}</span>: <span class="value">{fi_dur}</span>'
            tk.advance_by_ns(1)

        elif logid == L.LOGID_ECU_R_INJ_ON_TYPE_PTS:
            ri_on = self._read_u16_le()
            line = f'{prefix}: <span class="event">{self._pad_event("RI_ON")}</span>: <span class="value">{ri_on}</span>'
            tk.advance_by_ns(1)

        elif logid == L.LOGID_ECU_R_INJ_DUR_TYPE_U16:
            ri_dur = self._read_u16_le()
            line = f'{prefix}: <span class="event">{self._pad_event("RI_DUR")}</span>: <span class="value">{ri_dur}</span>'
            tk.advance_by_ns(1)

        # Ignition events
        elif logid == L.LOGID_ECU_F_COIL_ON_TYPE_PTS:
            fc_on = self._read_u16_le()
            line = f'{prefix}: <span class="event">{self._pad_event("FC_ON")}</span>: <span class="value">{fc_on}</span>'
            tk.advance_by_ns(1)

        elif logid == L.LOGID_ECU_F_COIL_OFF_TYPE_PTS:
            fc_off = self._read_u16_le()
            tk.process_timestamp(fc_off)
            line = f'{prefix}: <span class="event">{self._pad_event("FC_OFF")}</span>: <span class="value">{fc_off}</span>'

        elif logid == L.LOGID_ECU_R_COIL_ON_TYPE_PTS:
            rc_on = self._read_u16_le()
            line = f'{prefix}: <span class="event">{self._pad_event("RC_ON")}</span>: <span class="value">{rc_on}</span>'
            tk.advance_by_ns(1)

        elif logid == L.LOGID_ECU_R_COIL_OFF_TYPE_PTS:
            rc_off = self._read_u16_le()
            tk.process_timestamp(rc_off)
            line = f'{prefix}: <span class="event">{self._pad_event("RC_OFF")}</span>: <span class="value">{rc_off}</span>'

        # Sensors
        elif logid == L.LOGID_ECU_RAW_MAP_TYPE_U8:
            val = self._read_u8()
            line = f'{prefix}: <span class="event">{self._pad_event("MAP")}</span>: <span class="value">{val}</span>'
            tk.advance_by_ns(1)

        elif logid == L.LOGID_ECU_RAW_AAP_TYPE_U8:
            val = self._read_u8()
            line = f'{prefix}: <span class="event">{self._pad_event("AAP")}</span>: <span class="value">{val}</span>'
            tk.advance_by_ns(1)

        elif logid == L.LOGID_ECU_RAW_THA_TYPE_U8:
            val = self._read_u8()
            line = f'{prefix}: <span class="event">{self._pad_event("THA")}</span>: <span class="value">{val}</span>'
            tk.advance_by_ns(1)

        elif logid == L.LOGID_ECU_RAW_THW_TYPE_U8:
            val = self._read_u8()
            line = f'{prefix}: <span class="event">{self._pad_event("THW")}</span>: <span class="value">{val}</span>'
            tk.advance_by_ns(1)

        elif logid == L.LOGID_ECU_RAW_VM_TYPE_U8:
            val = self._read_u8()
            line = f'{prefix}: <span class="event">{self._pad_event("VM")}</span>: <span class="value">{val}</span>'
            tk.advance_by_ns(1)

        elif logid == L.LOGID_ECU_RAW_VTA_TYPE_U16:
            val = self._read_u16_le()
            line = f'{prefix}: <span class="event">{self._pad_event("VTA")}</span>: <span class="value">{val}</span>'
            tk.advance_by_ns(1)

        # Crankshaft
        elif logid == L.LOGID_ECU_CRANKREF_START_TYPE_TS:
            cr_ts = self._read_u16_le()
            tk.process_timestamp(cr_ts)
            line = f'{prefix}: <span class="event">{self._pad_event("CRANK_TS")}</span>: <span class="value">{cr_ts}</span>'

        elif logid == L.LOGID_ECU_CRANKREF_ID_TYPE_U8:
            val = self._read_u8()
            line = f'{prefix}: <span class="event">{self._pad_event("CRID")}</span>: <span class="value">{val}</span>'
            tk.advance_by_ns(1)

        # Manual coil events
        elif logid == L.LOGID_ECU_F_COIL_MAN_ON_TYPE_PTS:
            fc_man_on = self._read_u16_le()
            line = f'{prefix}: <span class="event">{self._pad_event("FC_MAN_ON")}</span>: <span class="value">{fc_man_on}</span>'
            tk.advance_by_ns(1)

        elif logid == L.LOGID_ECU_F_COIL_MAN_OFF_TYPE_PTS:
            fc_man_off = self._read_u16_le()
            line = f'{prefix}: <span class="event">{self._pad_event("FC_MAN_OFF")}</span>: <span class="value">{fc_man_off}</span>'
            tk.advance_by_ns(1)

        elif logid == L.LOGID_ECU_R_COIL_MAN_ON_TYPE_PTS:
            rc_man_on = self._read_u16_le()
            line = f'{prefix}: <span class="event">{self._pad_event("RC_MAN_ON")}</span>: <span class="value">{rc_man_on}</span>'
            tk.advance_by_ns(1)

        elif logid == L.LOGID_ECU_R_COIL_MAN_OFF_TYPE_PTS:
            rc_man_off = self._read_u16_le()
            line = f'{prefix}: <span class="event">{self._pad_event("RC_MAN_OFF")}</span>: <span class="value">{rc_man_off}</span>'
            tk.advance_by_ns(1)

        # Ignition delay
        elif logid == L.LOGID_ECU_F_IGN_DLY_TYPE_0P8:
            val = self._read_u8()
            line = f'{prefix}: <span class="event">{self._pad_event("F_IGN_DLY")}</span>: <span class="value">{val}</span>'
            tk.advance_by_ns(1)

        elif logid == L.LOGID_ECU_R_IGN_DLY_TYPE_0P8:
            val = self._read_u8()
            line = f'{prefix}: <span class="event">{self._pad_event("R_IGN_DLY")}</span>: <span class="value">{val}</span>'
            tk.advance_by_ns(1)

        # Other ECU events
        elif logid == L.LOGID_ECU_5MILLISEC_EVENT_TYPE_V:
            self._read_u8()  # Ignore garbage byte
            line = f'{prefix}: <span class="event">5MS_EVENT</span>'
            tk.advance_by_ns(1)

        elif logid == L.LOGID_ECU_CRANK_P6_MAX_TYPE_V:
            self._read_u8()  # Ignore garbage byte
            line = f'{prefix}: <span class="event">CRANK_P6_MAX</span>'
            tk.advance_by_ns(1)

        elif logid == L.LOGID_ECU_FUEL_PUMP_TYPE_B:
            val = self._read_u8()
            status = 'ON' if val else 'OFF'
            line = f'{prefix}: <span class="event">{self._pad_event("FUEL_PUMP")}</span>: <span class="value">{status}</span>'
            tk.advance_by_ns(1)

        # Error events
        elif logid == L.LOGID_ECU_ECU_ERROR_L000C_TYPE_U8:
            val = self._read_u8()
            line = f'{prefix}: <span class="event">{self._pad_event("ERR_L000C")}</span>: <span class="value">0x{val:02x}</span>'
            tk.advance_by_ns(1)

        elif logid == L.LOGID_ECU_ECU_ERROR_L000D_TYPE_U8:
            val = self._read_u8()
            line = f'{prefix}: <span class="event">{self._pad_event("ERR_L000D")}</span>: <span class="value">0x{val:02x}</span>'
            tk.advance_by_ns(1)

        elif logid == L.LOGID_ECU_ECU_ERROR_L000E_TYPE_U8:
            val = self._read_u8()
            line = f'{prefix}: <span class="event">{self._pad_event("ERR_L000E")}</span>: <span class="value">0x{val:02x}</span>'
            tk.advance_by_ns(1)

        elif logid == L.LOGID_ECU_ECU_ERROR_L000F_TYPE_U8:
            val = self._read_u8()
            line = f'{prefix}: <span class="event">{self._pad_event("ERR_L000F")}</span>: <span class="value">0x{val:02x}</span>'
            tk.advance_by_ns(1)

        # Debug port
        elif logid == L.LOGID_ECU_PORTG_DB_TYPE_U8:
            val = self._read_u8()
            line = f'{prefix}: <span class="event">{self._pad_event("PORTG_DB")}</span>: <span class="value">0b{val:08b}</span>'
            tk.advance_by_ns(1)

        # Camshaft events
        elif logid == L.LOGID_ECU_CAM_ERR_TYPE_U8:
            val = self._read_u8()
            line = f'{prefix}: <span class="event">{self._pad_event("CAM_ERR")}</span>: <span class="value">{val}</span>'
            tk.advance_by_ns(1)

        elif logid == L.LOGID_ECU_CAMSHAFT_TYPE_TS:
            cam_ts = self._read_u16_le()
            tk.process_timestamp(cam_ts)
            line = f'{prefix}: <span class="event">{self._pad_event("CAM_TS")}</span>: <span class="value">{cam_ts}</span>'

        # Spark events
        elif logid == L.LOGID_ECU_SPRK_X1_TYPE_PTS:
            val = self._read_u16_le()
            line = f'{prefix}: <span class="event">{self._pad_event("SPRK_X1")}</span>: <span class="value">{val}</span>'
            tk.advance_by_ns(1)

        elif logid == L.LOGID_ECU_SPRK_X2_TYPE_PTS:
            val = self._read_u16_le()
            line = f'{prefix}: <span class="event">{self._pad_event("SPRK_X2")}</span>: <span class="value">{val}</span>'
            tk.advance_by_ns(1)

        elif logid == L.LOGID_ECU_NOSPARK_TYPE_U8:
            val = self._read_u8()
            line = f'{prefix}: <span class="event">{self._pad_event("NOSPARK")}</span>: <span class="value">0x{val:02x}</span>'
            tk.advance_by_ns(1)

        # ===== EP Events =====
        # FIND_NAME handling - each character gets its own record
        elif logid == L.LOGID_EP_FIND_NAME_TYPE_U8:
            ch = self._read_u8()

            # Track string accumulation for displaying complete string on NULL
            if self.find_name_buffer == '':
                self.find_name_start_event = event_num

            if ch == 0:  # Null terminator - show complete string
                line = f'{prefix}: <span class="event">{self._pad_event("FIND")}</span>: <span class="value">"{self.find_name_buffer}"</span>'
                self.find_name_buffer = ''
                self.find_name_start_event = -1
            elif self.find_name_buffer == '':  # First character
                line = f'{prefix}: <span class="event">{self._pad_event("Begin FIND")}</span>'
                self.find_name_buffer = chr(ch)
            else:  # Middle character
                line = f'{prefix}:'  # Show record number with binary data
                self.find_name_buffer += chr(ch)

            tk.advance_by_ns(1)

        # LOAD_NAME handling - each character gets its own record
        elif logid == L.LOGID_EP_LOAD_NAME_TYPE_U8:
            ch = self._read_u8()

            # Track string accumulation for displaying complete string on NULL
            if self.load_name_buffer == '':
                self.load_name_start_event = event_num

            if ch == 0:  # Null terminator - show complete string
                line = f'{prefix}: <span class="event">{self._pad_event("LOAD")}</span>: <span class="value">"{self.load_name_buffer}"</span>'
                self.load_name_buffer = ''
                self.load_name_start_event = -1
            elif self.load_name_buffer == '':  # First character
                line = f'{prefix}: <span class="event">{self._pad_event("Begin LOAD")}</span>'
                self.load_name_buffer = chr(ch)
            else:  # Middle character
                line = f'{prefix}:'  # Show record number with binary data
                self.load_name_buffer += chr(ch)

            tk.advance_by_ns(1)

        elif logid == L.LOGID_EP_LOAD_ADDR_TYPE_U16:
            addr = self._read_u16_le()
            line = f'{prefix}: <span class="event">{self._pad_event("ADDR")}</span>: <span class="value">0x{addr:04x}</span>'
            tk.advance_by_ns(1)

        elif logid == L.LOGID_EP_LOAD_LEN_TYPE_U16:
            length = self._read_u16_le()
            line = f'{prefix}: <span class="event">{self._pad_event("LEN")}</span>: <span class="value">0x{length:04x}</span>'
            tk.advance_by_ns(1)

        elif logid == L.LOGID_EP_LOAD_ERR_TYPE_U8:
            err = self._read_u8()
            # EP_LOAD_ERR error name lookup
            err_names = {
                0x00: 'ERR_NOERR',
                0x01: 'ERR_NOTFOUND',
                0x02: 'ERR_NONAME',
                0x03: 'ERR_CKSUMERR',
                0x04: 'ERR_VERIFYERR',
                0x05: 'ERR_BADOFFSET',
            }
            err_name = err_names.get(err, f'UNKNOWN({err})')
            line = f'{prefix}: <span class="event">{self._pad_event("STAT")}</span>: <span class="value">{err_name}</span>'
            tk.advance_by_ns(1)

        # ===== WP Events =====
        # GPS time fields
        elif logid == L.LOGID_WP_CSECS_TYPE_U8:
            val = self._read_u8()
            line = f'{prefix}: <span class="event">{self._pad_event("CSECS")}</span>: <span class="value">{val}</span>'
            tk.advance_by_ns(1)

        elif logid == L.LOGID_WP_SECS_TYPE_U8:
            val = self._read_u8()
            line = f'{prefix}: <span class="event">{self._pad_event("SECS")}</span>: <span class="value">{val}</span>'
            tk.advance_by_ns(1)

        elif logid == L.LOGID_WP_MINS_TYPE_U8:
            val = self._read_u8()
            line = f'{prefix}: <span class="event">{self._pad_event("MINS")}</span>: <span class="value">{val}</span>'
            tk.advance_by_ns(1)

        elif logid == L.LOGID_WP_HOURS_TYPE_U8:
            val = self._read_u8()
            line = f'{prefix}: <span class="event">{self._pad_event("HOURS")}</span>: <span class="value">{val}</span>'
            tk.advance_by_ns(1)

        elif logid == L.LOGID_WP_DATE_TYPE_U8:
            val = self._read_u8()
            line = f'{prefix}: <span class="event">{self._pad_event("DATE")}</span>: <span class="value">{val}</span>'
            tk.advance_by_ns(1)

        elif logid == L.LOGID_WP_MONTH_TYPE_U8:
            val = self._read_u8()
            line = f'{prefix}: <span class="event">{self._pad_event("MONTH")}</span>: <span class="value">{val}</span>'
            tk.advance_by_ns(1)

        elif logid == L.LOGID_WP_YEAR_TYPE_U8:
            val = self._read_u8()
            line = f'{prefix}: <span class="event">{self._pad_event("YEAR")}</span>: <span class="value">{val}</span>'
            tk.advance_by_ns(1)

        elif logid == L.LOGID_WP_FIXTYPE_TYPE_U8:
            val = self._read_u8()
            line = f'{prefix}: <span class="event">{self._pad_event("FIXTYPE")}</span>: <span class="value">{val}</span>'
            tk.advance_by_ns(1)

        # GPS position (8 bytes: lat + lon as i32)
        elif logid == L.LOGID_WP_GPS_POSN_TYPE_8B:
            pos_bytes = self._read_bytes(8)
            if pos_bytes:
                # Decode lat and lon as signed 32-bit little-endian
                lat = int.from_bytes(pos_bytes[0:4], byteorder='little', signed=True)
                lon = int.from_bytes(pos_bytes[4:8], byteorder='little', signed=True)
                line = f'{prefix}: <span class="event">{self._pad_event("GPS_POSN")}</span>: <span class="value">lat={lat} lon={lon}</span>'
            tk.advance_by_ns(1)

        # GPS velocity
        elif logid == L.LOGID_WP_GPS_VELO_TYPE_U16:
            vel = self._read_u16_le()
            line = f'{prefix}: <span class="event">{self._pad_event("GPS_VELO")}</span>: <span class="value">{vel}</span>'
            tk.advance_by_ns(1)

        # GPS PPS
        elif logid == L.LOGID_WP_GPS_PPS_TYPE_V:
            line = f'{prefix}: <span class="event">GPS_PPS</span>'
            tk.advance_by_ns(1)

        # WP sync time
        elif logid == L.LOGID_WP_SYNC_TIME_TYPE_U16:
            val = self._read_u16_le()
            line = f'{prefix}: <span class="event">{self._pad_event("SYNC_TIME")}</span>: <span class="value">{val}</span>'
            tk.advance_by_ns(1)

        else:
            # Unknown event - skip data if we know the length
            data_len = self.indexer._get_event_data_length(logid)
            if data_len > 0:
                self._read_bytes(data_len)
            line = f'{prefix}: <span class="event">{self._pad_event(f"ID_0x{logid:02X}")}</span>: <span class="value">?</span>'
            tk.advance_by_ns(1)

        # Always return a record (even if line is empty for middle string chars)
        return {
            'index': event_num,
            'html': line,
            'binData': self.current_event_bytes[:],
            'binOffset': self.current_event_start_offset,
            'time_ns': tk.get_time_ns(),
        }
