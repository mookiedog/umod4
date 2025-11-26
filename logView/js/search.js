// search.js - Text search with highlighting

// Search state
let searchMatches = [];
let currentMatchIndex = -1;
let lastSearchTerm = '';

/**
 * Perform search through log records
 * @param {string} searchTerm - Search query
 */
export function performSearch(searchTerm) {
    lastSearchTerm = searchTerm;
    searchMatches = [];
    currentMatchIndex = -1;

    const outputDiv = document.getElementById('textOutput');
    const records = outputDiv.querySelectorAll('.record');

    if (!searchTerm || searchTerm.length === 0) {
        // Clear all highlights
        records.forEach(record => {
            record.innerHTML = record.innerHTML
                .replace(/<mark class="search-highlight-current">(.+?)<\/mark>/g, '$1')
                .replace(/<mark class="search-highlight">(.+?)<\/mark>/g, '$1');
        });
        updateSearchUI();
        return;
    }

    // Search through all records (case-insensitive)
    const searchRegex = new RegExp(escapeRegExp(searchTerm), 'gi');

    records.forEach((record, index) => {
        const textContent = record.textContent || record.innerText;
        if (searchRegex.test(textContent)) {
            searchMatches.push(index);
        }
    });

    if (searchMatches.length > 0) {
        currentMatchIndex = 0;
        highlightMatches(searchTerm);
        scrollToCurrentMatch();
    }

    updateSearchUI();
}

/**
 * Navigate to next search match
 */
export function searchNext() {
    if (searchMatches.length === 0) return;
    currentMatchIndex = (currentMatchIndex + 1) % searchMatches.length;
    highlightMatches(lastSearchTerm);
    scrollToCurrentMatch();
    updateSearchUI();
}

/**
 * Navigate to previous search match
 */
export function searchPrevious() {
    if (searchMatches.length === 0) return;
    currentMatchIndex = (currentMatchIndex - 1 + searchMatches.length) % searchMatches.length;
    highlightMatches(lastSearchTerm);
    scrollToCurrentMatch();
    updateSearchUI();
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
    return lastSearchTerm;
}

// Private helper functions
function highlightMatches(searchTerm) {
    const outputDiv = document.getElementById('textOutput');
    const records = outputDiv.querySelectorAll('.record');

    records.forEach((record, index) => {
        // Remove existing highlights
        let html = record.innerHTML
            .replace(/<mark class="search-highlight-current">(.+?)<\/mark>/g, '$1')
            .replace(/<mark class="search-highlight">(.+?)<\/mark>/g, '$1');

        record.innerHTML = html;

        // Check if this record contains a match
        if (searchMatches.includes(index)) {
            const isCurrent = (index === searchMatches[currentMatchIndex]);
            highlightTextNodes(record, searchTerm, isCurrent);
        }
    });
}

function highlightTextNodes(element, searchTerm, isCurrent) {
    const highlightClass = isCurrent ? 'search-highlight-current' : 'search-highlight';
    const searchRegex = new RegExp(`(${escapeRegExp(searchTerm)})`, 'gi');

    const walker = document.createTreeWalker(
        element,
        NodeFilter.SHOW_TEXT,
        null,
        false
    );

    const nodesToReplace = [];
    let node;

    // Collect all text nodes that contain matches
    while (node = walker.nextNode()) {
        if (searchRegex.test(node.textContent)) {
            nodesToReplace.push(node);
        }
    }

    // Replace text nodes with highlighted versions
    nodesToReplace.forEach(textNode => {
        const text = textNode.textContent;
        const fragment = document.createDocumentFragment();
        let lastIndex = 0;
        const regex = new RegExp(escapeRegExp(searchTerm), 'gi');
        let match;

        while ((match = regex.exec(text)) !== null) {
            // Add text before match
            if (match.index > lastIndex) {
                fragment.appendChild(document.createTextNode(text.substring(lastIndex, match.index)));
            }

            // Add highlighted match
            const mark = document.createElement('mark');
            mark.className = highlightClass;
            mark.textContent = match[0];
            fragment.appendChild(mark);

            lastIndex = regex.lastIndex;
        }

        // Add remaining text
        if (lastIndex < text.length) {
            fragment.appendChild(document.createTextNode(text.substring(lastIndex)));
        }

        textNode.parentNode.replaceChild(fragment, textNode);
    });
}

function scrollToCurrentMatch() {
    if (currentMatchIndex >= 0 && currentMatchIndex < searchMatches.length) {
        const outputDiv = document.getElementById('textOutput');
        const records = outputDiv.querySelectorAll('.record');
        const currentRecord = records[searchMatches[currentMatchIndex]];

        if (currentRecord) {
            currentRecord.scrollIntoView({ behavior: 'smooth', block: 'center' });
        }
    }
}

function updateSearchUI() {
    const hasResults = searchMatches.length > 0;
    const hasSearch = lastSearchTerm.length > 0;

    document.getElementById('searchPrevBtn').disabled = !hasResults;
    document.getElementById('searchNextBtn').disabled = !hasResults;
    document.getElementById('searchClearBtn').style.display = hasSearch ? 'block' : 'none';

    const countSpan = document.getElementById('searchCount');
    if (hasResults) {
        countSpan.textContent = `${currentMatchIndex + 1}/${searchMatches.length}`;
    } else if (hasSearch) {
        countSpan.textContent = 'No matches';
    } else {
        countSpan.textContent = '';
    }
}

function escapeRegExp(string) {
    return string.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
}
