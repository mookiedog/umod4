"""
Color utility functions for visualization.

Provides color parsing and conversion utilities.
"""


def parse_color_to_rgba(color_string, alpha=1.0):
    """
    Convert hex color string to RGBA tuple for QBrush.

    Args:
        color_string: Hex color like '#FF6600' or named color
        alpha: Transparency 0.0-1.0

    Returns:
        Tuple of (r, g, b, a) where each is 0-255
    """
    if isinstance(color_string, str) and color_string.startswith('#'):
        hex_str = color_string.lstrip('#')
        if len(hex_str) == 6:
            r = int(hex_str[0:2], 16)
            g = int(hex_str[2:4], 16)
            b = int(hex_str[4:6], 16)
            return (r, g, b, int(alpha * 255))
    # Fallback for invalid colors
    return (255, 0, 0, int(alpha * 255))
