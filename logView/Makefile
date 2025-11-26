# Makefile for umod4 Log Viewer
#
# Targets:
#   bundle     - Create viewer_bundle.html from ES6 modules
#   clean      - Remove generated files
#   serve      - Start development server for modular viewer
#   help       - Show this help

.PHONY: bundle clean serve help

bundle: viewer_bundle.html

viewer_bundle.html: index.html js/*.js bundle.py
	@echo "📦 Bundling ES6 modules..."
	python3 bundle.py

clean:
	@echo "🗑️  Cleaning generated files..."
	rm -f viewer_bundle.html

serve:
	@echo "🌐 Starting development server at http://localhost:8000"
	@echo "   Open http://localhost:8000/index.html in your browser"
	@echo "   Press Ctrl+C to stop"
	@python3 -m http.server 8000

help:
	@echo "umod4 Log Viewer - Build Commands"
	@echo ""
	@echo "Usage: make [target]"
	@echo ""
	@echo "Targets:"
	@echo "  bundle    - Bundle modules into single HTML file (for phones/tablets)"
	@echo "  clean     - Remove generated files"
	@echo "  serve     - Start dev server at http://localhost:8000"
	@echo "  help      - Show this help"
	@echo ""
	@echo "Development workflow:"
	@echo "  1. make serve         # Test with web server"
	@echo "  2. Edit js/*.js files"
	@echo "  3. make bundle        # Create distributable file"
	@echo "  4. Copy viewer_bundle.html to phone"

.DEFAULT_GOAL := help
