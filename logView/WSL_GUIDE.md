# WSL Guide - Opening the Viewer on Windows

## Quick Start

```bash
cd ~/projects/umod4/logView
./test_viewer.sh
```

The script will automatically detect WSL and open the viewer in your Windows browser.

## Method 1: Direct Open (Easiest)

```bash
# From WSL terminal
explorer.exe ~/projects/umod4/logView/viewer_prototype.html
```

This opens the HTML file in your default Windows browser. The browser can read files directly from WSL!

## Method 2: Windows File Explorer

1. Open Windows File Explorer
2. In the address bar, type:
   ```
   \\wsl$\Ubuntu\home\robin\projects\umod4\logView
   ```
3. Double-click `viewer_prototype.html`

## Method 3: Local Web Server (Best for Development)

```bash
# From WSL terminal
cd ~/projects/umod4/logView
python3 -m http.server 8080
```

Then in Windows browser:
```
http://localhost:8080/viewer_prototype.html
```

**Advantages:**
- Faster reload during development
- Can test on phone (connect to `http://YOUR_PC_IP:8080`)
- Works with browser dev tools
- No file path issues

## Accessing Log Files from Windows Browser

When the viewer prompts you to select a log file, you have several options:

### Option A: Direct WSL Path Access

Modern browsers can access WSL files. When you click "Open Log File", navigate to:

```
\\wsl$\Ubuntu\home\robin\logs\2025-11-24\log.3
```

### Option B: Copy to Windows

```bash
# From WSL terminal
cp ~/logs/2025-11-24/log.3 /mnt/c/Users/$USER/Downloads/
```

Then in Windows browser, open from:
```
C:\Users\YourUsername\Downloads\log.3
```

### Option C: Drag and Drop from WSL

1. Open Windows File Explorer
2. Navigate to: `\\wsl$\Ubuntu\home\robin\logs\2025-11-24`
3. Drag `log.3` onto the browser window with the viewer open

## URL Format Reference

### File URLs

**From Windows browser to WSL file:**
```
file://wsl$/Ubuntu/home/robin/projects/umod4/logView/viewer_prototype.html
```

**From WSL browser (if installed in WSL):**
```
file:///home/robin/projects/umod4/logView/viewer_prototype.html
```

### HTTP URLs (Web Server)

```
http://localhost:8080/viewer_prototype.html    # From Windows
http://127.0.0.1:8080/viewer_prototype.html   # Same thing
```

## Troubleshooting

### "File not found" when opening

**Problem:** Browser can't find the HTML file.

**Solution:** Use `explorer.exe` instead:
```bash
explorer.exe viewer_prototype.html
```

### "Cannot access log file" when loading

**Problem:** Browser can't read files from WSL.

**Solutions:**
1. Use local web server (Method 3 above)
2. Copy log to Windows filesystem
3. Try different browser (Chrome/Edge usually work better with WSL than Firefox)

### Browser opens but shows blank page

**Problem:** JavaScript error or file permission issue.

**Solution:**
1. Press F12 to open browser console
2. Check for errors (red messages)
3. Make sure file is readable: `chmod 644 viewer_prototype.html`

### Drag-and-drop doesn't work

**Problem:** Browser security restrictions.

**Solutions:**
1. Use "Open Log File" button instead
2. Or use local web server method
3. Or copy file to Windows filesystem first

## Development Workflow (WSL + Windows Browser)

**Recommended setup:**

```bash
# Terminal 1: Start web server
cd ~/projects/umod4/logView
python3 -m http.server 8080

# Terminal 2: Edit files in VS Code
code ~/projects/umod4/logView/

# Windows Browser: Open
http://localhost:8080/viewer_prototype.html
```

**Edit → Save → Reload browser** - instant updates!

## Testing on Phone (Same WiFi)

```bash
# Find your PC's IP address
ip addr show eth0 | grep "inet " | awk '{print $2}' | cut -d/ -f1

# Start server
python3 -m http.server 8080

# On phone browser:
http://YOUR_PC_IP:8080/viewer_prototype.html
```

**Note:** You may need to allow port 8080 through Windows Firewall.

## Quick Commands

```bash
# Open viewer in Windows browser
explorer.exe ~/projects/umod4/logView/viewer_prototype.html

# Start web server
cd ~/projects/umod4/logView && python3 -m http.server 8080

# Copy log to Windows
cp ~/logs/2025-11-24/log.3 /mnt/c/Users/$USER/Downloads/

# Run test script
~/projects/umod4/logView/test_viewer.sh

# Check if WSL
grep -i microsoft /proc/version
```

## Browser Compatibility

| Browser | WSL File Access | Drag & Drop | Recommended |
|---------|----------------|-------------|-------------|
| **Edge** | ✅ Excellent | ✅ Yes | ⭐ Best |
| **Chrome** | ✅ Good | ✅ Yes | ⭐ Good |
| **Firefox** | ⚠️ Limited | ⚠️ Sometimes | Use web server |
| **Brave** | ✅ Good | ✅ Yes | Good |

**Recommendation:** Use Edge or Chrome on Windows for best WSL compatibility.

## Summary

**Easiest method:**
```bash
./test_viewer.sh
```

**Most flexible method:**
```bash
python3 -m http.server 8080
# Then browse to http://localhost:8080/viewer_prototype.html
```

Choose based on your needs!
