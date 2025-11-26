// fileManager.js - File I/O and recent files management

const MAX_RECENT_FILES = 10;

/**
 * Get recent files from localStorage
 * @returns {Array} Array of recent file objects
 */
export function getRecentFiles() {
    const stored = localStorage.getItem('recentFiles');
    return stored ? JSON.parse(stored) : [];
}

/**
 * Save recent file to localStorage
 * @param {Object} fileInfo - File information {name, size, timestamp}
 */
export function saveRecentFile(fileInfo) {
    let recent = getRecentFiles();

    // Remove if already exists
    recent = recent.filter(f => f.name !== fileInfo.name);

    // Add to front
    recent.unshift(fileInfo);

    // Keep only MAX_RECENT_FILES
    if (recent.length > MAX_RECENT_FILES) {
        recent = recent.slice(0, MAX_RECENT_FILES);
    }

    localStorage.setItem('recentFiles', JSON.stringify(recent));
}

/**
 * Clear all recent files
 */
export function clearRecentFiles() {
    localStorage.removeItem('recentFiles');
}

/**
 * Read file as Uint8Array
 * @param {File} file - File object from input
 * @returns {Promise<Uint8Array>} File data
 */
export function readFileAsUint8Array(file) {
    return new Promise((resolve, reject) => {
        const reader = new FileReader();

        reader.onload = (e) => {
            resolve(new Uint8Array(e.target.result));
        };

        reader.onerror = (e) => {
            reject(new Error('Failed to read file'));
        };

        reader.readAsArrayBuffer(file);
    });
}

/**
 * Format bytes to human-readable string
 * @param {number} bytes - Number of bytes
 * @returns {string} Formatted string (e.g., "1.5 MB")
 */
export function formatBytes(bytes) {
    if (bytes === 0) return '0 Bytes';
    const k = 1024;
    const sizes = ['Bytes', 'KB', 'MB', 'GB'];
    const i = Math.floor(Math.log(bytes) / Math.log(k));
    return Math.round((bytes / Math.pow(k, i)) * 100) / 100 + ' ' + sizes[i];
}
