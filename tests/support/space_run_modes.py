"""The two ways a differential of a space runs each of its programs.

As the program ships, and with a collection of stored terms at every
opportunity.  The second makes a reader of something nothing keeps alive
(an ordering key, a decoded atom) meet a reclaimed term at once, where an
ordinary run meets one only when a collection happens to fall there.
Neither mode checks or repairs anything: both run the code that ships.
"""

MODES = (
    ("as it ships", {}),
    ("a collection at every opportunity",
     {"CETTA_TERM_UNIVERSE_COLLECTION": "reclaim-every"}),
)


def run_both(once, same=None):
    """ONCE(env) runs the program under the extra environment ENV and gives
    (exit code, answers, stderr).  The answers of the two modes where they
    agree; where they differ, each differing answer shows both, so that no
    expectation is met by it.  SAME(a, b) says whether two answers agree
    (default: equal text); a gate whose answers are bags passes its bag
    comparison, since no mode promises an order of answers."""
    agree = same or (lambda a, b: a == b)
    (_, shipped), (name, collecting) = (
        (label, once(extra)) for label, extra in MODES)
    code = shipped[0] or collecting[0]
    stderr = shipped[2] or collecting[2]
    if isinstance(shipped[1], dict):
        merged, differ = {}, False
        for key in sorted(set(shipped[1]) | set(collecting[1])):
            a, b = shipped[1].get(key), collecting[1].get(key)
            if a is not None and b is not None and agree(a, b):
                merged[key] = a
            else:
                merged[key] = "%s || with %s: %s" % (a, name, b)
                differ = True
        return (code or 1) if differ else code, merged, stderr
    if len(shipped[1]) == len(collecting[1]) and all(
            agree(a, b) for a, b in zip(shipped[1], collecting[1])):
        return code, shipped[1], stderr
    return code or 1, list(shipped[1]) + ["with %s:" % name] + list(collecting[1]), stderr
