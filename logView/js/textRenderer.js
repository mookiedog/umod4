// textRenderer.js - Text rendering with binary display support

import { BYTES_PER_LINE } from './constants.js';

/**
 * Render parsed records to HTML with optional binary display
 * @param {Object} parsedData - Parsed log data from parser.js
 * @param {boolean} showBinary - Whether to display binary data
 * @returns {string} HTML string
 */
export function renderRecords(parsedData, showBinary) {
    const output = [];

    for (const record of parsedData.records) {
        if (showBinary && record.binData.length > 0) {
            // Format binary data with proper multi-line support
            const binLines = formatBinaryDataForRecord(record.binOffset, record.binData);
            const binLineArray = binLines.split('\n');

            // First line gets the record html appended
            output.push(`<div class="record">${binLineArray[0]} ${record.html}</div>`);

            // Additional lines (for multi-byte events) are standalone
            for (let i = 1; i < binLineArray.length; i++) {
                output.push(`<div class="record">${binLineArray[i]}</div>`);
            }
        } else {
            output.push(`<div class="record">${record.html}</div>`);
        }
    }

    return output.join('');
}

/**
 * Format binary data for a record with address and hex bytes
 * @param {number} startOffset - Starting byte offset
 * @param {Uint8Array} bytes - Binary data bytes
 * @returns {string} Formatted binary data lines
 */
export function formatBinaryDataForRecord(startOffset, bytes) {
    const chunks = [];
    for (let i = 0; i < bytes.length; i += BYTES_PER_LINE) {
        const chunk = bytes.slice(i, Math.min(i + BYTES_PER_LINE, bytes.length));
        const hexBytes = chunk.map(b => b.toString(16).padStart(2, '0').toUpperCase());

        // Pad with nbsp entities for alignment
        while (hexBytes.length < BYTES_PER_LINE) {
            hexBytes.push('&nbsp;&nbsp;');
        }

        const bytesStr = hexBytes.join(' ');
        const addr = (startOffset + i).toString(16).padStart(8, '0');
        chunks.push(`<span style="color: #569cd6;">0x${addr}: ${bytesStr}</span>`);
    }
    return chunks.join('\n');
}
