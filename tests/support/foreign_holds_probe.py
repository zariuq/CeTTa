"""Objects whose lifetimes a foreign-hold test observes."""

_live = 0


class Payload:
    def __init__(self, ident):
        global _live
        self.ident = ident
        _live += 1

    def __del__(self):
        global _live
        _live -= 1


def make(ident):
    return Payload(ident)


def ident(payload):
    return payload.ident


def alive():
    import gc
    gc.collect()
    return _live
