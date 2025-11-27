// virtualRenderer.js - Virtual scrolling renderer for large log files

import { formatBinaryDataForRecord } from './textRenderer.js';

/**
 * VirtualRenderer - Efficiently renders only visible records
 *
 * Only renders records in viewport + overscan buffer, dramatically improving
 * performance for large log files (100K+ events).
 */
export class VirtualRenderer {
    constructor(containerId) {
        this.container = document.getElementById(containerId);
        this.records = [];
        this.visibleStart = 0;
        this.visibleEnd = 0;
        this.overscan = 100;  // Extra records above/below viewport

        // Height estimation (will be calibrated after first render)
        this.estimatedRowHeight = 20;  // Base height per record
        this.actualHeights = new Map();  // Track actual heights

        // DOM elements
        this.topSpacer = null;
        this.viewport = null;
        this.bottomSpacer = null;

        // Scroll state
        this.isUpdating = false;
        this.isProgrammaticScroll = false;
        this._scrollHandler = null;

        // Search state
        this.searchTerm = '';
        this.searchMatches = [];
        this.currentMatchIndex = -1;
    }

    /**
     * Initialize renderer with parsed log data
     */
    initialize(parsedData) {
        this.records = parsedData.records || [];
        this.setupContainer();

        // Calculate initial visible range
        this.updateVisibleRange();

        // If still no visible range (container not laid out yet), show first screen
        if (this.visibleEnd === 0) {
            this.visibleStart = 0;
            this.visibleEnd = Math.min(this.records.length, this.overscan * 2);
        }

        this.render();
    }

    /**
     * Setup container structure
     */
    setupContainer() {
        // Clear existing content
        this.container.innerHTML = '';

        // Create three-part structure: top spacer, viewport, bottom spacer
        this.topSpacer = document.createElement('div');
        this.topSpacer.style.height = '0px';

        this.viewport = document.createElement('div');
        this.viewport.className = 'virtual-viewport';

        this.bottomSpacer = document.createElement('div');
        this.bottomSpacer.style.height = '0px';

        this.container.appendChild(this.topSpacer);
        this.container.appendChild(this.viewport);
        this.container.appendChild(this.bottomSpacer);

        // Attach scroll listener to the container itself
        // Store bound handler for later removal if needed
        this._scrollHandler = this.onScroll.bind(this);
        this.container.addEventListener('scroll', this._scrollHandler);
    }

    /**
     * Handle scroll events with throttling
     */
    onScroll() {
        // Don't update if we're programmatically scrolling or already updating
        if (this.isProgrammaticScroll || this.isUpdating) return;

        this.isUpdating = true;

        // Use requestAnimationFrame for smooth updates
        requestAnimationFrame(() => {
            this.updateVisibleRange();
            this.isUpdating = false;
        });
    }

    /**
     * Calculate which records should be visible
     */
    updateVisibleRange() {
        if (!this.container) return;

        const scrollTop = this.container.scrollTop;
        const viewportHeight = this.container.clientHeight;

        // Estimate position based on average row height
        const estimatedStart = Math.floor(scrollTop / this.estimatedRowHeight);
        const estimatedEnd = Math.ceil((scrollTop + viewportHeight) / this.estimatedRowHeight);

        // Add overscan buffer
        const newStart = Math.max(0, estimatedStart - this.overscan);
        const newEnd = Math.min(this.records.length, estimatedEnd + this.overscan);

        // Only re-render if range changed significantly
        if (Math.abs(newStart - this.visibleStart) > 50 ||
            Math.abs(newEnd - this.visibleEnd) > 50) {
            this.visibleStart = newStart;
            this.visibleEnd = newEnd;
            this.render();
        }
    }

    /**
     * Render visible records
     */
    render() {
        if (this.records.length === 0) {
            this.viewport.innerHTML = '<div class="record">No records to display</div>';
            return;
        }

        // Render visible slice
        const html = [];
        for (let i = this.visibleStart; i < this.visibleEnd; i++) {
            const record = this.records[i];
            if (!record) {
                console.error('VirtualRenderer.render: Missing record at index', i);
                continue;
            }
            html.push(this.renderRecord(record, i));
        }

        this.viewport.innerHTML = html.join('');

        // Update spacer heights
        const topHeight = this.visibleStart * this.estimatedRowHeight;
        const bottomHeight = (this.records.length - this.visibleEnd) * this.estimatedRowHeight;

        this.topSpacer.style.height = `${topHeight}px`;
        this.bottomSpacer.style.height = `${bottomHeight}px`;

        // Apply search highlighting if active
        if (this.searchTerm) {
            this.highlightVisibleMatches();
        }
    }

    /**
     * Render a single record with binary data (always shown)
     */
    renderRecord(record, index) {
        if (record.binData && record.binData.length > 0) {
            const binLines = formatBinaryDataForRecord(record.binOffset, record.binData);
            const binLineArray = binLines.split('\n');

            // First line gets the record html appended
            let html = `<div class="record" data-index="${index}">${binLineArray[0]} ${record.html}</div>`;

            // Additional lines (for multi-byte events) are standalone
            for (let i = 1; i < binLineArray.length; i++) {
                html += `<div class="record" data-index="${index}">${binLineArray[i]}</div>`;
            }

            return html;
        } else {
            return `<div class="record" data-index="${index}">${record.html}</div>`;
        }
    }

    /**
     * Search through all records (in-memory, not DOM)
     */
    performSearch(searchTerm) {
        this.searchTerm = searchTerm;
        this.searchMatches = [];
        this.currentMatchIndex = -1;

        if (!searchTerm || searchTerm.length === 0) {
            this.render();  // Clear highlights
            return { matches: 0, current: -1 };
        }

        // Search through all records in memory
        const lowerSearch = searchTerm.toLowerCase();

        for (let i = 0; i < this.records.length; i++) {
            const record = this.records[i];
            // Strip HTML tags for text-only search
            const text = this.stripHtml(record.html).toLowerCase();
            if (text.includes(lowerSearch)) {
                this.searchMatches.push(i);
            }
        }

        if (this.searchMatches.length > 0) {
            this.currentMatchIndex = 0;
            // Scroll to first match and update viewport
            setTimeout(() => this.scrollToMatch(0), 50);
        } else {
            this.render();  // Re-render to clear any highlights
        }

        return {
            matches: this.searchMatches.length,
            current: this.currentMatchIndex
        };
    }

    /**
     * Navigate to next search match
     */
    searchNext() {
        if (this.searchMatches.length === 0) return { matches: 0, current: -1 };

        this.currentMatchIndex = (this.currentMatchIndex + 1) % this.searchMatches.length;

        // Scroll to match with small delay
        setTimeout(() => this.scrollToMatch(this.currentMatchIndex), 50);

        return {
            matches: this.searchMatches.length,
            current: this.currentMatchIndex
        };
    }

    /**
     * Navigate to previous search match
     */
    searchPrevious() {
        if (this.searchMatches.length === 0) return { matches: 0, current: -1 };

        this.currentMatchIndex = (this.currentMatchIndex - 1 + this.searchMatches.length) % this.searchMatches.length;

        // Scroll to match with small delay
        setTimeout(() => this.scrollToMatch(this.currentMatchIndex), 50);

        return {
            matches: this.searchMatches.length,
            current: this.currentMatchIndex
        };
    }

    /**
     * Scroll to a specific match
     */
    scrollToMatch(matchIndex) {
        if (matchIndex < 0 || matchIndex >= this.searchMatches.length) return;

        const recordIndex = this.searchMatches[matchIndex];
        if (!this.container) return;

        // Calculate scroll position
        const targetPosition = recordIndex * this.estimatedRowHeight;
        const viewportHeight = this.container.clientHeight;

        // Center the match in viewport
        const scrollTo = targetPosition - (viewportHeight / 2);

        // Set flag to prevent scroll handler from interfering
        this.isProgrammaticScroll = true;

        this.container.scrollTo({
            top: Math.max(0, scrollTo),
            behavior: 'smooth'
        });

        // Clear flag and force update after scroll animation
        setTimeout(() => {
            this.isProgrammaticScroll = false;
            this.updateVisibleRange();
            // Force re-render to apply highlights
            this.render();
        }, 300);
    }

    /**
     * Highlight search matches in visible records
     */
    highlightVisibleMatches() {
        if (!this.searchTerm) return;

        const records = this.viewport.querySelectorAll('.record');

        records.forEach(element => {
            const index = parseInt(element.dataset.index, 10);
            if (isNaN(index)) return;

            if (this.searchMatches.includes(index)) {
                const isCurrent = (index === this.searchMatches[this.currentMatchIndex]);
                this.highlightTextNodes(element, this.searchTerm, isCurrent);
            }
        });
    }

    /**
     * Highlight text nodes within an element
     */
    highlightTextNodes(element, searchTerm, isCurrent) {
        const highlightClass = isCurrent ? 'search-highlight-current' : 'search-highlight';
        const searchRegex = new RegExp(`(${this.escapeRegExp(searchTerm)})`, 'gi');

        const walker = document.createTreeWalker(
            element,
            NodeFilter.SHOW_TEXT,
            null,
            false
        );

        const nodesToReplace = [];
        let node;

        while (node = walker.nextNode()) {
            if (searchRegex.test(node.textContent)) {
                nodesToReplace.push(node);
            }
        }

        nodesToReplace.forEach(textNode => {
            const text = textNode.textContent;
            const fragment = document.createDocumentFragment();
            let lastIndex = 0;
            const regex = new RegExp(this.escapeRegExp(searchTerm), 'gi');
            let match;

            while ((match = regex.exec(text)) !== null) {
                if (match.index > lastIndex) {
                    fragment.appendChild(document.createTextNode(text.substring(lastIndex, match.index)));
                }

                const mark = document.createElement('mark');
                mark.className = highlightClass;
                mark.textContent = match[0];
                fragment.appendChild(mark);

                lastIndex = regex.lastIndex;
            }

            if (lastIndex < text.length) {
                fragment.appendChild(document.createTextNode(text.substring(lastIndex)));
            }

            textNode.parentNode.replaceChild(fragment, textNode);
        });
    }

    /**
     * Strip HTML tags from string
     */
    stripHtml(html) {
        const div = document.createElement('div');
        div.innerHTML = html;
        return div.textContent || div.innerText || '';
    }

    /**
     * Escape special regex characters
     */
    escapeRegExp(string) {
        return string.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
    }

    /**
     * Get total record count
     */
    getRecordCount() {
        return this.records.length;
    }

    /**
     * Clear all data
     */
    clear() {
        this.records = [];
        this.visibleStart = 0;
        this.visibleEnd = 0;
        this.searchTerm = '';
        this.searchMatches = [];
        this.currentMatchIndex = -1;

        if (this.container) {
            this.container.innerHTML = '';
        }
    }
}
