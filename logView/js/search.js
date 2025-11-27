// search.js - Text search with highlighting
// NOTE: This module is now a thin wrapper around VirtualRenderer's search functionality

// Global reference to virtual renderer (set by index.html)
let _virtualRenderer = null;

/**
 * Set the virtual renderer instance
 */
export function setVirtualRenderer(renderer) {
    _virtualRenderer = renderer;
}

/**
 * Perform search through log records
 * @param {string} searchTerm - Search query
 */
export function performSearch(searchTerm) {
    if (!_virtualRenderer) {
        console.error('Virtual renderer not initialized');
        return;
    }

    const result = _virtualRenderer.performSearch(searchTerm);
    updateSearchUI(result);
}

/**
 * Navigate to next search match
 */
export function searchNext() {
    if (!_virtualRenderer) return;

    const result = _virtualRenderer.searchNext();
    updateSearchUI(result);
}

/**
 * Navigate to previous search match
 */
export function searchPrevious() {
    if (!_virtualRenderer) return;

    const result = _virtualRenderer.searchPrevious();
    updateSearchUI(result);
}

/**
 * Clear current search
 */
export function clearSearch() {
    document.getElementById('searchInput').value = '';
    performSearch('');
}

/**
 * Get last search term (for re-applying after re-render)
 */
export function getLastSearchTerm() {
    return _virtualRenderer ? _virtualRenderer.searchTerm : '';
}

// Private helper function
function updateSearchUI(result) {
    if (!result) {
        result = { matches: 0, current: -1 };
    }

    const hasResults = result.matches > 0;
    const hasSearch = _virtualRenderer && _virtualRenderer.searchTerm.length > 0;

    document.getElementById('searchPrevBtn').disabled = !hasResults;
    document.getElementById('searchNextBtn').disabled = !hasResults;
    document.getElementById('searchClearBtn').style.display = hasSearch ? 'block' : 'none';

    const countSpan = document.getElementById('searchCount');
    if (hasResults) {
        countSpan.textContent = `${result.current + 1}/${result.matches}`;
    } else if (hasSearch) {
        countSpan.textContent = 'No matches';
    } else {
        countSpan.textContent = '';
    }
}
