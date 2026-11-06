"""Protocol fixtures shared by native and Python-hosted interoperability tests."""
events = []
checks = 0


def check(value, expected):
    global checks
    assert value == expected, (value, expected)
    checks += 1
    return checks


def total(): return checks
def identity(value): return value
def reset():
    global checks, bump
    checks = 0
    events.clear()
    bump = original_bump
    return None


def original_bump(value):
    events.append(value)
    return value + 1


bump = original_bump


def patch():
    global bump
    bump = lambda value: -1


def huge(): return 2**200
def nul(): return 'a\x00b'
def nested(): return [[1, 2], {'x': 3}]
def cycle():
    value = []
    value.append(value)
    return value


class Callable:
    def __call__(self, x, *, scale=1): return x * scale


def stream():
    try:
        events.append('yield4')
        yield 4
        events.append('yield5')
        yield 5
    finally:
        events.append('closed')


class Context:
    def __enter__(self):
        events.append('enter')
        return [7, 8]
    def __exit__(self, kind, value, traceback):
        events.append('exit')


def event_list(): return events
def event_tail(): return events[-2:]
