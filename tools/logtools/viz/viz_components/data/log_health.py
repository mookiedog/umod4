"""
Log health: turn the decoder's unknown-LOGID counts into a message for the user.

decodelog.py counts the bytes it could not understand while decoding a log, and writes the counts into the HDF5
file as root attributes (see "Damaged Logs" in tools/README_LogDecoder.md).  A rider never runs the decoder by
hand: they meet it through the visualizer, so the visualizer is where the warning has to appear.

This module has no GUI code, so it can be tested without Qt.
"""


def damaged_log_message(attrs):
    """Return a plain-language warning if the log looks damaged, else None.

    attrs: the root attribute mapping of an HDF5 file made by decodelog.py (an h5py attribute manager or a dict).

    Only bytes after the first few bytes of the log count.  A log that was opened while the WP was already
    running starts with the tail of a record that was split at the start of the log: harmless, and the decoder
    keeps it out of unknown_logid_bytes_after_start.  A file made by an older decoder has none of these
    attributes: no warning (we cannot tell).
    """
    def get(name):
        if name not in attrs:
            return None
        try:
            return int(attrs[name])
        except (TypeError, ValueError):
            return None

    after_start = get('unknown_logid_bytes_after_start')
    if not after_start or after_start <= 0:
        return None

    total = get('unknown_logid_bytes') or after_start
    runs = get('unknown_logid_runs') or 1
    first = get('unknown_logid_first_offset')
    last = get('unknown_logid_last_offset')

    stretches = "1 stretch" if runs == 1 else f"{runs} separate stretches"
    msg = (f"This log contains {total} bytes that the decoder could not understand, in {stretches}")
    if first is not None and last is not None:
        msg += (f", between byte {first:#x} and byte {last:#x} of the log file "
                f"(about {first / 1024:.0f} KB from its start)")
    msg += (".\n\nThe data at and around those places is probably missing or wrong, so the graphs "
            "may show gaps or odd values there. Values far from those places are probably fine.")
    return msg
