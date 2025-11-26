// parser.js - Binary log parsing and timestamp reconstruction

import { LOGID, ECU_CPU_EVENT_NAMES, EP_LOAD_ERR_NAMES } from './constants.js';

/**
 * TimeKeeper - Manages 16-bit to 64-bit timestamp reconstruction
 *
 * Uses delta-based accumulation matching the Python decoder logic.
 * Each timestamp is a 16-bit value (0-65535). Timer wraps at 65536.
 * Each tick = 2 microseconds = 2000 nanoseconds.
 */
export class TimeKeeper {
    constructor() {
        this.time_ns = 0n;           // Absolute time in nanoseconds
        this.prev_ts = -1;           // Previous 16-bit timestamp (-1 = first event)
        this.TIMER_MAX = 65536;      // 16-bit timer wraparound point
        this.TICKS_TO_NS = 2000n;    // Each tick = 2 microseconds = 2000 ns
    }

    /**
     * Process a timestamped event and advance time
     * @param {number} ts_16bit - Current 16-bit timestamp value
     */
    processTimestamp(ts_16bit) {
        if (this.prev_ts >= 0) {
            // Calculate delta from previous timestamp
            let delta_ticks = ts_16bit - this.prev_ts;

            // Handle wraparound: if delta is negative, timer wrapped
            // ECU guarantees timestamps are in non-decreasing order
            if (delta_ticks < 0) {
                delta_ticks += this.TIMER_MAX;
            }

            // Advance time by delta
            this.time_ns += BigInt(delta_ticks) * this.TICKS_TO_NS;
        }
        // else: first timestamp, time_ns stays at 0

        this.prev_ts = ts_16bit;
    }

    /**
     * Advance time by a fixed number of nanoseconds (for untimestamped events)
     * @param {number|bigint} delta_ns - Nanoseconds to advance
     */
    advanceByNs(delta_ns) {
        this.time_ns += BigInt(delta_ns);
    }

    /**
     * Get current time in seconds (floating point)
     * @returns {number} Current time in seconds
     */
    getTimeSec() {
        return Number(this.time_ns) / 1e9;
    }

    /**
     * Get current time in nanoseconds (bigint)
     * @returns {bigint} Current time in nanoseconds
     */
    getTimeNs() {
        return this.time_ns;
    }
}

/**
 * Parse binary log file into structured records
 * @param {Uint8Array} data - Binary log data
 * @returns {Object} Parsed log with records array and metadata
 */
export function parseLog(data) {
    const timekeeper = new TimeKeeper();
    const records = [];  // Store records with binary data
    let recordCount = 0;
    let offset = 0;
    let byteOffset = 0;  // Track absolute byte position for binary display

    // State for EP_LOAD_NAME accumulation
    let epromNameBuffer = '';
    let epromNameBinaryData = [];
    let epromNameStartOffset = 0;

    const view = new DataView(data.buffer);

    // Helper functions
    function formatRecord(recordNum, tk) {
        const elapsed = tk.getTimeSec();
        return `[${recordNum.toString().padStart(6)}  @ ${elapsed.toFixed(4).padStart(10)}s]`;
    }

    function padEvent(name) {
        const targetLen = 12;
        if (name.length >= targetLen) return name;
        const padding = '&nbsp;'.repeat(targetLen - name.length);
        return name + padding;
    }

    // Track bytes read for current event
    let currentEventBytes = [];
    let currentEventStartOffset = 0;

    function startEvent() {
        currentEventBytes = [];
        currentEventStartOffset = byteOffset;
    }

    function readU8() {
        if (offset >= data.length) return null;
        const val = data[offset++];
        byteOffset++;
        currentEventBytes.push(val);
        return val;
    }

    function readU16LE() {
        if (offset + 1 >= data.length) return null;
        const val = view.getUint16(offset, true); // little-endian
        const b1 = data[offset];
        const b2 = data[offset + 1];
        offset += 2;
        byteOffset += 2;
        currentEventBytes.push(b1, b2);
        return val;
    }

    function readI16LE() {
        if (offset + 1 >= data.length) return null;
        const val = view.getInt16(offset, true); // little-endian
        const b1 = data[offset];
        const b2 = data[offset + 1];
        offset += 2;
        byteOffset += 2;
        currentEventBytes.push(b1, b2);
        return val;
    }

    function readBytes(n) {
        if (offset + n > data.length) return null;
        const bytes = data.slice(offset, offset + n);
        for (let i = 0; i < n; i++) {
            currentEventBytes.push(data[offset + i]);
        }
        offset += n;
        byteOffset += n;
        return bytes;
    }

    // Process all events - no limit
    const MAX_EVENTS = Infinity;

    while (offset < data.length && recordCount < MAX_EVENTS) {
        startEvent();
        const logid = readU8();
        if (logid === null) break;

        recordCount++;
        const prefix = formatRecord(recordCount, timekeeper);
        let line = '';
        let eventType = 'unknown';  // Track event type for graphing

        switch (logid) {
            // ===== GENERAL IDs =====
            case LOGID.GEN_ECU_LOG_VER:
                line = `${prefix}: <span class="event">${padEvent('ECU_VER')}</span>: <span class="value">${readU8()}</span>`;
                eventType = 'version';
                break;

            case LOGID.GEN_EP_LOG_VER:
                line = `${prefix}: <span class="event">${padEvent('EP_VER')}</span>: <span class="value">${readU8()}</span>`;
                eventType = 'version';
                break;

            case LOGID.GEN_WP_LOG_VER:
                line = `${prefix}: <span class="event">${padEvent('WP_VER')}</span>: <span class="value">${readU8()}</span>`;
                eventType = 'version';
                break;

            // ===== ECU IDs =====
            case LOGID.ECU_CPU_EVENT: {
                const evt = readU8();
                const evtName = ECU_CPU_EVENT_NAMES[evt] || `UNKNOWN(${evt})`;
                line = `${prefix}: <span class="event">${padEvent('CPU_EVT')}</span>: <span class="value">${evtName}</span>`;
                eventType = 'cpu_event';
                timekeeper.advanceByNs(1);
                break;
            }

            case LOGID.ECU_T1_OFLO: {
                const oflo_ts = readU16LE();
                timekeeper.processTimestamp(oflo_ts);
                line = `${prefix}: <span class="event">${padEvent('OFLO')}</span>: <span class="value">${oflo_ts}</span>`;
                eventType = 'timestamp';
                break;
            }

            case LOGID.ECU_L4000_EVENT:
                line = `${prefix}: <span class="event">${padEvent('L4000')}</span>: <span class="value">${readU8()}</span>`;
                eventType = 'timing';
                timekeeper.advanceByNs(1);
                break;

            case LOGID.ECU_T1_HOFLO: {
                const hoflo_ts = readU16LE();
                timekeeper.processTimestamp(hoflo_ts);
                line = `${prefix}: <span class="event">${padEvent('HOFLO')}</span>: <span class="value">${hoflo_ts}</span>`;
                eventType = 'timestamp';
                break;
            }

            // Injector events
            case LOGID.ECU_F_INJ_ON: {
                const val = readU16LE();
                line = `${prefix}: <span class="event">${padEvent('FI_ON')}</span>: <span class="value">${val}</span>`;
                eventType = 'injector';
                timekeeper.advanceByNs(1);
                break;
            }

            case LOGID.ECU_F_INJ_DUR: {
                const val = readU16LE();
                line = `${prefix}: <span class="event">${padEvent('FI_DUR')}</span>: <span class="value">${val}</span>`;
                eventType = 'injector';
                timekeeper.advanceByNs(1);
                break;
            }

            case LOGID.ECU_R_INJ_ON: {
                const val = readU16LE();
                line = `${prefix}: <span class="event">${padEvent('RI_ON')}</span>: <span class="value">${val}</span>`;
                eventType = 'injector';
                timekeeper.advanceByNs(1);
                break;
            }

            case LOGID.ECU_R_INJ_DUR: {
                const val = readU16LE();
                line = `${prefix}: <span class="event">${padEvent('RI_DUR')}</span>: <span class="value">${val}</span>`;
                eventType = 'injector';
                timekeeper.advanceByNs(1);
                break;
            }

            // Coil events
            case LOGID.ECU_F_COIL_ON: {
                const val = readU16LE();
                line = `${prefix}: <span class="event">${padEvent('FC_ON')}</span>: <span class="value">${val}</span>`;
                eventType = 'ignition';
                timekeeper.advanceByNs(1);
                break;
            }

            case LOGID.ECU_F_COIL_OFF: {
                const val = readU16LE();
                line = `${prefix}: <span class="event">${padEvent('FC_OFF')}</span>: <span class="value">${val}</span>`;
                eventType = 'ignition';
                timekeeper.advanceByNs(1);
                break;
            }

            case LOGID.ECU_R_COIL_ON: {
                const val = readU16LE();
                line = `${prefix}: <span class="event">${padEvent('RC_ON')}</span>: <span class="value">${val}</span>`;
                eventType = 'ignition';
                timekeeper.advanceByNs(1);
                break;
            }

            case LOGID.ECU_R_COIL_OFF: {
                const val = readU16LE();
                line = `${prefix}: <span class="event">${padEvent('RC_OFF')}</span>: <span class="value">${val}</span>`;
                eventType = 'ignition';
                timekeeper.advanceByNs(1);
                break;
            }

            case LOGID.ECU_F_COIL_MAN_ON: {
                const val = readU16LE();
                line = `${prefix}: <span class="event">${padEvent('FC_MAN_ON')}</span>: <span class="value">${val}</span>`;
                eventType = 'ignition';
                timekeeper.advanceByNs(1);
                break;
            }

            case LOGID.ECU_F_COIL_MAN_OFF: {
                const val = readU16LE();
                line = `${prefix}: <span class="event">${padEvent('FC_MAN_OFF')}</span>: <span class="value">${val}</span>`;
                eventType = 'ignition';
                timekeeper.advanceByNs(1);
                break;
            }

            case LOGID.ECU_R_COIL_MAN_ON: {
                const val = readU16LE();
                line = `${prefix}: <span class="event">${padEvent('RC_MAN_ON')}</span>: <span class="value">${val}</span>`;
                eventType = 'ignition';
                timekeeper.advanceByNs(1);
                break;
            }

            case LOGID.ECU_R_COIL_MAN_OFF: {
                const val = readU16LE();
                line = `${prefix}: <span class="event">${padEvent('RC_MAN_OFF')}</span>: <span class="value">${val}</span>`;
                eventType = 'ignition';
                timekeeper.advanceByNs(1);
                break;
            }

            // Ignition delay
            case LOGID.ECU_F_IGN_DLY: {
                const val = readU8();
                line = `${prefix}: <span class="event">${padEvent('F_IGN_DLY')}</span>: <span class="value">${val}</span>`;
                eventType = 'ignition';
                timekeeper.advanceByNs(1);
                break;
            }

            case LOGID.ECU_R_IGN_DLY: {
                const val = readU8();
                line = `${prefix}: <span class="event">${padEvent('R_IGN_DLY')}</span>: <span class="value">${val}</span>`;
                eventType = 'ignition';
                timekeeper.advanceByNs(1);
                break;
            }

            // Other ECU events
            case LOGID.ECU_5MILLISEC_EVENT:
                readU8();  // Ignore garbage byte
                line = `${prefix}: <span class="event">5MS_EVENT</span>`;
                eventType = 'timing';
                timekeeper.advanceByNs(1);
                break;

            case LOGID.ECU_CRANK_P6_MAX:
                readU8();  // Ignore garbage byte
                line = `${prefix}: <span class="event">CRANK_P6_MAX</span>`;
                eventType = 'crank';
                timekeeper.advanceByNs(1);
                break;

            case LOGID.ECU_FUEL_PUMP: {
                const val = readU8();
                line = `${prefix}: <span class="event">${padEvent('FUEL_PUMP')}</span>: <span class="value">${val ? 'ON' : 'OFF'}</span>`;
                eventType = 'fuel';
                timekeeper.advanceByNs(1);
                break;
            }

            // Error events
            case LOGID.ECU_ERROR_L000C:
            case LOGID.ECU_ERROR_L000D:
            case LOGID.ECU_ERROR_L000E:
            case LOGID.ECU_ERROR_L000F: {
                const val = readU8();
                const errName = ['ERR_L000C', 'ERR_L000D', 'ERR_L000E', 'ERR_L000F'][logid - LOGID.ECU_ERROR_L000C];
                line = `${prefix}: <span class="event">${padEvent(errName)}</span>: <span class="value">0x${val.toString(16).padStart(2, '0')}</span>`;
                eventType = 'error';
                timekeeper.advanceByNs(1);
                break;
            }

            // Sensor raw values
            case LOGID.ECU_RAW_VTA: {
                const val = readU16LE();
                line = `${prefix}: <span class="event">${padEvent('VTA')}</span>: <span class="value">${val}</span>`;
                eventType = 'sensor';
                timekeeper.advanceByNs(1);
                break;
            }

            case LOGID.ECU_RAW_MAP: {
                const val = readU8();
                line = `${prefix}: <span class="event">${padEvent('MAP')}</span>: <span class="value">${val}</span>`;
                eventType = 'sensor';
                timekeeper.advanceByNs(1);
                break;
            }

            case LOGID.ECU_RAW_AAP: {
                const val = readU8();
                line = `${prefix}: <span class="event">${padEvent('AAP')}</span>: <span class="value">${val}</span>`;
                eventType = 'sensor';
                timekeeper.advanceByNs(1);
                break;
            }

            case LOGID.ECU_RAW_THW: {
                const val = readU8();
                line = `${prefix}: <span class="event">${padEvent('THW')}</span>: <span class="value">${val}</span>`;
                eventType = 'sensor';
                timekeeper.advanceByNs(1);
                break;
            }

            case LOGID.ECU_RAW_THA: {
                const val = readU8();
                line = `${prefix}: <span class="event">${padEvent('THA')}</span>: <span class="value">${val}</span>`;
                eventType = 'sensor';
                timekeeper.advanceByNs(1);
                break;
            }

            case LOGID.ECU_RAW_VM: {
                const val = readU8();
                line = `${prefix}: <span class="event">${padEvent('VM')}</span>: <span class="value">${val}</span>`;
                eventType = 'sensor';
                timekeeper.advanceByNs(1);
                break;
            }

            case LOGID.ECU_PORTG_DB: {
                const val = readU8();
                line = `${prefix}: <span class="event">${padEvent('PORTG_DB')}</span>: <span class="value">0b${val.toString(2).padStart(8, '0')}</span>`;
                eventType = 'debug';
                timekeeper.advanceByNs(1);
                break;
            }

            // Crankshaft events
            case LOGID.ECU_CRANKREF_START: {
                const cr_ts = readU16LE();
                timekeeper.processTimestamp(cr_ts);
                line = `${prefix}: <span class="event">${padEvent('CRANK_TS')}</span>: <span class="value">${cr_ts}</span>`;
                eventType = 'crank';
                break;
            }

            case LOGID.ECU_CRANKREF_ID: {
                const val = readU8();
                line = `${prefix}: <span class="event">${padEvent('CRID')}</span>: <span class="value">${val}</span>`;
                eventType = 'crank';
                timekeeper.advanceByNs(1);
                break;
            }

            case LOGID.ECU_CAM_ERR: {
                const val = readU8();
                line = `${prefix}: <span class="event">${padEvent('CAM_ERR')}</span>: <span class="value">${val}</span>`;
                eventType = 'crank';
                timekeeper.advanceByNs(1);
                break;
            }

            case LOGID.ECU_CAMSHAFT: {
                const cam_ts = readU16LE();
                timekeeper.processTimestamp(cam_ts);
                line = `${prefix}: <span class="event">${padEvent('CAM_TS')}</span>: <span class="value">${cam_ts}</span>`;
                eventType = 'crank';
                break;
            }

            // Spark events
            case LOGID.ECU_SPRK_X1: {
                const val = readU16LE();
                line = `${prefix}: <span class="event">${padEvent('SPRK_X1')}</span>: <span class="value">${val}</span>`;
                eventType = 'ignition';
                timekeeper.advanceByNs(1);
                break;
            }

            case LOGID.ECU_SPRK_X2: {
                const val = readU16LE();
                line = `${prefix}: <span class="event">${padEvent('SPRK_X2')}</span>: <span class="value">${val}</span>`;
                eventType = 'ignition';
                timekeeper.advanceByNs(1);
                break;
            }

            case LOGID.ECU_NOSPARK: {
                const val = readU8();
                line = `${prefix}: <span class="event">${padEvent('NOSPARK')}</span>: <span class="value">0x${val.toString(16).padStart(2, '0')}</span>`;
                eventType = 'ignition';
                timekeeper.advanceByNs(1);
                break;
            }

            // ===== EP Events =====
            case LOGID.EP_FIND_NAME:
            case LOGID.EP_LOAD_NAME: {
                const ch = readU8();

                if (epromNameBuffer === '') {
                    epromNameStartOffset = currentEventStartOffset;
                    epromNameBinaryData = [];
                }

                epromNameBinaryData.push(...currentEventBytes);

                if (ch === 0) {
                    if (logid == LOGID.EP_FIND_NAME) {
                        line = `${prefix}: <span class="event">${padEvent('FIND')}</span>: <span class="value">\"${epromNameBuffer}\"</span>`;
                    }
                    else {
                        line = `${prefix}: <span class="event">${padEvent('LOAD')}</span>: <span class="value">\"${epromNameBuffer}\"</span>`;
                    }

                    if (line) {
                        records.push({
                            html: line,
                            binData: epromNameBinaryData.slice(),
                            binOffset: epromNameStartOffset,
                            timestamp: timekeeper.getTimeSec(),
                            type: 'eprom',
                            logid: logid
                        });
                        recordCount++;
                        if (recordCount >= MAX_EVENTS) break;
                        line = '';
                    }

                    epromNameBuffer = '';
                    epromNameBinaryData = [];
                } else {
                    epromNameBuffer += String.fromCharCode(ch);
                    line = '';
                }
                eventType = 'eprom';
                timekeeper.advanceByNs(1);
                break;
            }

            case LOGID.EP_LOAD_ADDR: {
                const addr = readU16LE();
                line = `${prefix}: <span class="event">${padEvent('ADDR')}</span>: <span class="value">0x${addr.toString(16).padStart(4, '0')}</span>`;
                eventType = 'eprom';
                timekeeper.advanceByNs(1);
                break;
            }

            case LOGID.EP_LOAD_LEN: {
                const len = readU16LE();
                line = `${prefix}: <span class="event">${padEvent('LEN')}</span>: <span class="value">0x${len.toString(16).padStart(4, '0')}</span>`;
                eventType = 'eprom';
                timekeeper.advanceByNs(1);
                break;
            }

            case LOGID.EP_LOAD_ERR: {
                const err = readU8();
                const errName = EP_LOAD_ERR_NAMES[err] || `UNKNOWN(${err})`;
                line = `${prefix}: <span class="event">${padEvent('STAT')}</span>: <span class="value">${errName}</span>`;
                eventType = 'eprom';
                timekeeper.advanceByNs(1);
                break;
            }

            // ===== WP Events =====
            case LOGID.WP_CSECS:
            case LOGID.WP_SECS:
            case LOGID.WP_MINS:
            case LOGID.WP_HOURS:
            case LOGID.WP_DATE:
            case LOGID.WP_MONTH:
            case LOGID.WP_YEAR:
            case LOGID.WP_FIXTYPE: {
                const val = readU8();
                const names = {
                    [LOGID.WP_CSECS]: 'CSECS',
                    [LOGID.WP_SECS]: 'SECS',
                    [LOGID.WP_MINS]: 'MINS',
                    [LOGID.WP_HOURS]: 'HOURS',
                    [LOGID.WP_DATE]: 'DATE',
                    [LOGID.WP_MONTH]: 'MONTH',
                    [LOGID.WP_YEAR]: 'YEAR',
                    [LOGID.WP_FIXTYPE]: 'FIXTYPE'
                };
                line = `${prefix}: <span class="event">${padEvent(names[logid])}</span>: <span class="value">${val}</span>`;
                eventType = 'gps_time';
                timekeeper.advanceByNs(1);
                break;
            }

            case LOGID.WP_GPS_POSN: {
                const posBytes = readBytes(8);
                const lat = new DataView(posBytes.buffer).getInt32(0, true);
                const lon = new DataView(posBytes.buffer).getInt32(4, true);
                line = `${prefix}: <span class="event">${padEvent('GPS_POSN')}</span>: <span class="value">lat=${lat} lon=${lon}</span>`;
                eventType = 'gps';
                timekeeper.advanceByNs(1);
                break;
            }

            case LOGID.WP_GPS_VELO: {
                const vel = readU16LE();
                line = `${prefix}: <span class="event">${padEvent('GPS_VELO')}</span>: <span class="value">${vel}</span>`;
                eventType = 'gps';
                timekeeper.advanceByNs(1);
                break;
            }

            case LOGID.WP_GPS_PPS:
                line = `${prefix}: <span class="event">GPS_PPS</span>`;
                eventType = 'gps';
                timekeeper.advanceByNs(1);
                break;

            case LOGID.WP_SYNC_TIME: {
                const val = readU16LE();
                line = `${prefix}: <span class="event">${padEvent('SYNC_TIME')}</span>: <span class="value">${val}</span>`;
                eventType = 'timing';
                timekeeper.advanceByNs(1);
                break;
            }

            default:
                line = `${prefix}: <span style="color: #ff6b6b;">UNKNOWN(0x${logid.toString(16).padStart(2, '0')})</span>`;
                eventType = 'unknown';
                timekeeper.advanceByNs(1);
                break;
        }

        if (line) {
            records.push({
                html: line,
                binData: currentEventBytes.slice(),
                binOffset: currentEventStartOffset,
                timestamp: timekeeper.getTimeSec(),
                type: eventType,
                logid: logid
            });
        }
    }

    return {
        records: records,
        recordCount: recordCount,
        duration: timekeeper.getTimeSec(),
        truncated: recordCount >= MAX_EVENTS
    };
}
