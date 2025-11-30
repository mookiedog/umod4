// streamingRenderer.js - Simple fixed-height virtual scrolling

export class StreamingRenderer {
    constructor(containerId) {
        this.container = document.getElementById(containerId);
        this.totalEvents = 0;
        this.rowHeight = 21;  // Fixed height per row (measured from CSS)
        this.overscan = 20;   // Extra rows to render above/below viewport
        this.VISUAL_PADDING = 20;  // Visual padding added to top/bottom spacers

        // Binary display configuration
        this.BYTES_PER_LINE = 4;  // Configurable: max bytes to display per line

        // DOM elements
        this.topSpacer = null;
        this.viewport = null;
        this.bottomSpacer = null;

        // Current rendered range
        this.currentStart = -1;
        this.currentEnd = -1;

        // Scroll state
        this.scrollTimeout = null;
        this.updatingSpacers = false;
        this.isProgrammaticScrolling = false;  // Flag to prevent re-entry
        this.currentTopRecord = 0;  // Track which record should be at the top of viewport

        // Chunk cache
        this.cache = new Map();
        this.maxCacheSize = 10;

        // Search state
        this.searchTerm = '';
        this.searchMatches = [];
        this.currentMatchIndex = -1;

        this.apiBase = '/api';
    }

    async initialize() {
        try {
            const response = await fetch(`${this.apiBase}/info`);
            const data = await response.json();
            this.totalEvents = data.total_events;

            this.setupContainer();

            // Measure actual row height
            await this.measureRowHeight();

            // Render first page immediately (no debounce on init)
            const viewportHeight = this.container.clientHeight;
            const end = Math.min(this.totalEvents, Math.ceil(viewportHeight / this.rowHeight) + this.overscan);
            console.log(`Init: viewport height=${viewportHeight}px, rowHeight=${this.rowHeight}px, calculated end=${end}`);
            await this.render(0, end);
        } catch (error) {
            console.error('Failed to initialize:', error);
            this.container.innerHTML = `<div class="error">Error: ${error.message}</div>`;
        }
    }

    async measureRowHeight() {
        // Fetch first TWO records to measure spacing including margins
        const records = await this.fetchChunk(0, 2);
        if (records.length >= 2) {
            const html = records.map(r => this.renderRecord(r)).join('');
            this.viewport.innerHTML = html;
            if (this.viewport.children.length >= 2) {
                // Measure the vertical distance between tops of consecutive records
                // This includes content height + margin-bottom
                const rect1 = this.viewport.children[0].getBoundingClientRect();
                const rect2 = this.viewport.children[1].getBoundingClientRect();
                const measured = rect2.top - rect1.top;
                // Round to whole pixels to avoid cumulative rounding errors
                this.rowHeight = Math.round(measured);
                console.log(`Measured row spacing: ${measured}px (including margins), rounded to ${this.rowHeight}px`);
            }
            this.viewport.innerHTML = '';
        }
    }

    setupContainer() {
        this.container.innerHTML = '';

        this.topSpacer = document.createElement('div');
        this.viewport = document.createElement('div');
        this.bottomSpacer = document.createElement('div');

        // CRITICAL: No padding on container OR viewport - it breaks scroll math
        // Instead, we'll add a fixed 20px to the spacers for visual spacing

        this.container.appendChild(this.topSpacer);
        this.container.appendChild(this.viewport);
        this.container.appendChild(this.bottomSpacer);

        this.container.addEventListener('scroll', () => this.onScroll());

        // Add keyboard controls
        this.container.setAttribute('tabindex', '0');  // Make container focusable
        this.container.addEventListener('keydown', (e) => this.onKeyDown(e));

        // Focus the container so keyboard events work
        this.container.focus();
    }

    onKeyDown(e) {
        // Ignore key events if we're already processing a programmatic scroll
        if (this.isProgrammaticScrolling) {
            e.preventDefault();
            e.stopPropagation();
            return;
        }

        const viewportHeight = this.container.clientHeight;
        const recordsPerPage = Math.floor(viewportHeight / this.rowHeight);

        console.log(`onKeyDown: key=${e.key}, currentTopRecord=${this.currentTopRecord}, recordsPerPage=${recordsPerPage}`);

        switch(e.key) {
            case 'ArrowUp':
                e.preventDefault();
                e.stopPropagation();
                this.currentTopRecord = Math.max(0, this.currentTopRecord - 1);
                this.scrollToRecord(this.currentTopRecord);
                break;
            case 'ArrowDown':
                e.preventDefault();
                e.stopPropagation();
                this.currentTopRecord = Math.min(this.totalEvents - 1, this.currentTopRecord + 1);
                this.scrollToRecord(this.currentTopRecord);
                break;
            case 'PageUp':
                e.preventDefault();
                e.stopPropagation();
                this.currentTopRecord = Math.max(0, this.currentTopRecord - recordsPerPage);
                this.scrollToRecord(this.currentTopRecord);
                break;
            case 'PageDown':
                e.preventDefault();
                e.stopPropagation();
                this.currentTopRecord = Math.min(this.totalEvents - 1, this.currentTopRecord + recordsPerPage);
                this.scrollToRecord(this.currentTopRecord);
                break;
            case 'Home':
                e.preventDefault();
                e.stopPropagation();
                this.currentTopRecord = 0;
                this.scrollToRecord(0);  // Go to start
                break;
            case 'End':
                e.preventDefault();
                e.stopPropagation();
                this.scrollToRecord(this.totalEvents - 1);  // Go to end
                break;
        }
    }

    scrollByRecords(delta) {
        // Cancel any pending scroll handler to prevent feedback loop
        if (this.scrollTimeout) {
            clearTimeout(this.scrollTimeout);
            this.scrollTimeout = null;
        }

        // Get current top record index from current scroll position
        const scrollTop = this.container.scrollTop;
        const currentTopRecord = Math.floor(scrollTop / this.rowHeight);

        // Calculate new top record - move by exactly delta records
        const newTopRecord = currentTopRecord + delta;
        const clampedTopRecord = Math.max(0, Math.min(this.totalEvents - 1, newTopRecord));

        console.log(`ScrollByRecords: delta=${delta}, current=${currentTopRecord}, new=${newTopRecord}, clamped=${clampedTopRecord}`);

        // Scroll to new position
        this.scrollToRecord(clampedTopRecord);
    }

    scrollToRecord(recordIndex) {
        // Cancel any pending scroll handler to prevent feedback loop
        if (this.scrollTimeout) {
            clearTimeout(this.scrollTimeout);
            this.scrollTimeout = null;
        }

        // Lock IMMEDIATELY before any async operations
        this.isProgrammaticScrolling = true;
        this.updatingSpacers = true;

        // Clamp to valid range
        recordIndex = Math.max(0, Math.min(this.totalEvents - 1, recordIndex));

        // Calculate what should be rendered at this position
        const viewportHeight = this.container.clientHeight;

        // The scroll position should put recordIndex at the TOP of the viewport
        // Add VISUAL_PADDING because topSpacer includes it
        // Round to whole pixels to avoid sub-pixel rendering issues
        const newScrollTop = Math.round(recordIndex * this.rowHeight + this.VISUAL_PADDING);

        // Calculate visible range from the scroll position
        const visibleStart = recordIndex;
        const visibleEnd = Math.min(this.totalEvents, recordIndex + Math.ceil(viewportHeight / this.rowHeight));

        // Add overscan for smooth scrolling
        const start = Math.max(0, visibleStart - this.overscan);
        const end = Math.min(this.totalEvents, visibleEnd + this.overscan);

        console.log(`ScrollToRecord: recordIndex=${recordIndex}, newScrollTop=${newScrollTop}, visible=${visibleStart}-${visibleEnd}, render=${start}-${end}`);

        // Render first, THEN scroll
        this.renderWithScroll(start, end, newScrollTop);
    }

    async renderWithScroll(start, end, targetScrollTop) {
        // This is for programmatic scrolling - render then scroll to avoid flicker
        // NOTE: Locks should already be set by scrollToRecord() before calling this
        try {
            // Fetch records
            const records = await this.fetchChunk(start, end);

            // Render HTML
            const html = records.map(r => this.renderRecord(r)).join('');
            this.viewport.innerHTML = html;

            // Update spacers - topSpacer represents the height of all records BEFORE start
            // Add VISUAL_PADDING to top spacer for visual spacing at top of list
            this.topSpacer.style.height = `${start * this.rowHeight + this.VISUAL_PADDING}px`;
            // Add VISUAL_PADDING to bottom spacer for visual spacing at bottom of list
            this.bottomSpacer.style.height = `${(this.totalEvents - end) * this.rowHeight + this.VISUAL_PADDING}px`;

            // Set scroll position - targetScrollTop is absolute position in the full document
            // The topSpacer already accounts for records before 'start', so this should align correctly
            this.container.scrollTop = targetScrollTop;

            // Track current range
            this.currentStart = start;
            this.currentEnd = end;

            // Debug: log first and last record details
            if (records.length > 0) {
                const firstRec = records[0];
                const lastRec = records[records.length - 1];
                console.log(`RenderWithScroll: rendered ${records.length} records, range ${start}-${end}`);
                console.log(`  First record: index=${firstRec.index}, binOffset=0x${firstRec.binOffset?.toString(16) || '?'}`);
                console.log(`  Last record: index=${lastRec.index}, binOffset=0x${lastRec.binOffset?.toString(16) || '?'}`);
                console.log(`  topSpacer=${start * this.rowHeight}px, requested scrollTop=${targetScrollTop}, actual scrollTop=${this.container.scrollTop}`);

                // Calculate which record should be at top based on actual scroll position
                const actualTopRecord = Math.floor(this.container.scrollTop / this.rowHeight);
                console.log(`  Expected top record: ${Math.floor(targetScrollTop / this.rowHeight)}, Actual top record: ${actualTopRecord}`);
            }

            // Re-enable scroll events after everything settles
            requestAnimationFrame(() => {
                requestAnimationFrame(() => {
                    this.updatingSpacers = false;
                    this.isProgrammaticScrolling = false;
                });
            });
        } catch (error) {
            console.error('RenderWithScroll error:', error);
            this.updatingSpacers = false;
            this.isProgrammaticScrolling = false;
        }
    }

    onScroll() {
        // Ignore scroll events from spacer updates or programmatic scrolling
        if (this.updatingSpacers || this.isProgrammaticScrolling) {
            console.log(`onScroll: BLOCKED (updatingSpacers=${this.updatingSpacers}, isProgrammaticScrolling=${this.isProgrammaticScrolling})`);
            return;
        }

        // Debounce scroll events
        if (this.scrollTimeout) {
            clearTimeout(this.scrollTimeout);
        }

        this.scrollTimeout = setTimeout(() => {
            const scrollTop = this.container.scrollTop;
            const viewportHeight = this.container.clientHeight;

            // Update currentTopRecord based on scroll position (for manual scrolling)
            // Subtract VISUAL_PADDING since topSpacer includes it
            this.currentTopRecord = Math.floor((scrollTop - this.VISUAL_PADDING) / this.rowHeight);

            // Calculate visible range
            const startIndex = Math.floor((scrollTop - this.VISUAL_PADDING) / this.rowHeight);
            const endIndex = Math.ceil((scrollTop + viewportHeight - this.VISUAL_PADDING) / this.rowHeight);

            // Add overscan
            const start = Math.max(0, startIndex - this.overscan);
            const end = Math.min(this.totalEvents, endIndex + this.overscan);

            console.log(`Scroll: top=${scrollTop}, currentTopRecord=${this.currentTopRecord}, start=${start}, end=${end}, current=${this.currentStart}-${this.currentEnd}`);

            // Only render if range changed significantly
            if (start !== this.currentStart || end !== this.currentEnd) {
                this.render(start, end);
            }
        }, 16);  // ~60fps
    }

    async render(start, end) {
        // This is for scroll-triggered rendering (not programmatic keyboard/button scrolling)
        try {
            // Fetch records
            const records = await this.fetchChunk(start, end);

            // Render HTML
            const html = records.map(r => this.renderRecord(r)).join('');
            this.viewport.innerHTML = html;

            // Update spacers - block scroll events during this
            this.updatingSpacers = true;
            this.topSpacer.style.height = `${start * this.rowHeight + this.VISUAL_PADDING}px`;
            this.bottomSpacer.style.height = `${(this.totalEvents - end) * this.rowHeight + this.VISUAL_PADDING}px`;

            // Track current range
            this.currentStart = start;
            this.currentEnd = end;

            console.log(`Render: rendered ${records.length} records, range ${start}-${end}`);

            // Allow scroll events again after a frame
            requestAnimationFrame(() => {
                this.updatingSpacers = false;
            });
        } catch (error) {
            console.error('Render error:', error);
            this.updatingSpacers = false;
        }
    }

    async fetchChunk(start, end) {
        const key = `${start}-${end}`;

        // Check cache
        if (this.cache.has(key)) {
            return this.cache.get(key).records;
        }

        // Fetch from server
        try {
            const response = await fetch(`${this.apiBase}/events?start=${start}&end=${end}`);
            const data = await response.json();

            console.log(`Fetch: requested ${start}-${end}, got ${data.records.length} records`);

            // Cache it
            this.cache.set(key, { records: data.records, timestamp: Date.now() });

            // Evict old entries
            if (this.cache.size > this.maxCacheSize) {
                const oldest = [...this.cache.entries()]
                    .sort((a, b) => a[1].timestamp - b[1].timestamp)[0][0];
                this.cache.delete(oldest);
            }

            return data.records;
        } catch (error) {
            console.error('Fetch error:', error);
            return [];
        }
    }

    renderRecord(record) {
        const html = record.html || '';

        if (record.binData && record.binData.length > 0) {
            // Format hex bytes with padding for consistent alignment
            const hex = record.binData.map(b => b.toString(16).padStart(2, '0').toUpperCase()).join(' ');

            // Calculate padding needed to align with BYTES_PER_LINE
            const bytesDisplayed = record.binData.length;
            const paddingNeeded = this.BYTES_PER_LINE - bytesDisplayed;
            const padding = paddingNeeded > 0 ? '&nbsp;&nbsp;&nbsp;'.repeat(paddingNeeded) : '';

            // Format: [offset] HEX HEX HEX HEX (padded) html_content
            const binLine = `<span class="bin-offset">[${record.binOffset.toString(16).padStart(8, '0')}]</span> <span class="value">${hex}${padding}</span>`;
            return `<div class="record" data-index="${record.index}">${binLine} ${html}</div>`;
        }

        return `<div class="record" data-index="${record.index}">${html}</div>`;
    }

    // Search methods
    async performSearch(term) {
        this.searchTerm = term.toLowerCase();
        this.searchMatches = [];
        this.currentMatchIndex = -1;

        // For now, return empty - server-side search would go here
        return { matches: 0, current: -1 };
    }

    searchNext() {
        if (this.searchMatches.length === 0) return null;
        this.currentMatchIndex = (this.currentMatchIndex + 1) % this.searchMatches.length;
        return { matches: this.searchMatches.length, current: this.currentMatchIndex };
    }

    searchPrevious() {
        if (this.searchMatches.length === 0) return null;
        this.currentMatchIndex = (this.currentMatchIndex - 1 + this.searchMatches.length) % this.searchMatches.length;
        return { matches: this.searchMatches.length, current: this.currentMatchIndex };
    }

    highlightVisibleMatches() {
        // Implement if needed
    }

    destroy() {
        this.cache.clear();
        if (this.container) {
            this.container.innerHTML = '';
        }
    }
}
