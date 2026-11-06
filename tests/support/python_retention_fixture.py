"""Observe lifetime during a single MeTTa directive, before top-level cleanup."""
made = live = peak = 0


class Item:
    def __init__(self):
        global made, live, peak
        self.payload = bytearray(1024)
        made += 1
        live += 1
        peak = max(peak, live)

    def __del__(self):
        global live
        live -= 1


def counts(): return made, live, peak
