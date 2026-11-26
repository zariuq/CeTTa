# The _thread module, as CPython 3.15's Modules/_threadmodule.c defines it,
# for a runtime that runs one thread.  Locks behave as CPython's wherever
# one thread can finish the operation; a blocking acquire of a lock the only
# thread holds is a deadlock, raised as such rather than waited out; and a
# new thread cannot be started.
"""This module provides primitive operations to write multi-threaded programs.
The 'threading' module provides a more convenient interface."""

error = RuntimeError
TIMEOUT_MAX = 9223372036.0
_NAME_MAXLEN = 15
_IDENT = 1


def _timeout_args(name, blocking, timeout):
    # lock_acquire_parse_args
    if not blocking and timeout != -1:
        raise ValueError("can't specify a timeout for a non-blocking call")
    if timeout < 0 and timeout != -1:
        raise ValueError("timeout value must be a non-negative number")
    if timeout > TIMEOUT_MAX:
        raise OverflowError("timeout value is too large")


class lock:
    """A lock object is a synchronization primitive."""
    __slots__ = ("_locked",)

    def __new__(cls):
        self = object.__new__(cls)
        self._locked = False
        return self

    def acquire(self, blocking=True, timeout=-1):
        _timeout_args("acquire", blocking, timeout)
        if not self._locked:
            self._locked = True
            return True
        if not blocking or timeout != -1:
            return False
        raise RuntimeError("deadlock: the lock is held and no other thread can release it")

    acquire_lock = acquire

    def release(self):
        if not self._locked:
            raise RuntimeError("release unlocked lock")
        self._locked = False

    release_lock = release

    def locked(self):
        return self._locked

    locked_lock = locked

    def __enter__(self):
        return self.acquire()

    def __exit__(self, *args):
        self.release()

    def _at_fork_reinit(self):
        self._locked = False

    def __repr__(self):
        return ("<locked " if self._locked else "<unlocked ") + "_thread.lock object at " + hex(id(self)) + ">"


LockType = lock


class RLock:
    """A reentrant lock."""
    __slots__ = ("_owner", "_count")

    def __new__(cls, *args, **kwargs):
        self = object.__new__(cls)
        self._owner = 0
        self._count = 0
        return self

    def acquire(self, blocking=True, timeout=-1):
        _timeout_args("acquire", blocking, timeout)
        self._owner = _IDENT
        self._count += 1
        return True

    def release(self):
        if self._count == 0:
            raise RuntimeError("cannot release un-acquired lock")
        self._count -= 1
        if self._count == 0:
            self._owner = 0

    def locked(self):
        return self._count > 0

    def _is_owned(self):
        return self._count > 0

    def _recursion_count(self):
        return self._count

    def _release_save(self):
        if self._count == 0:
            raise RuntimeError("cannot release un-acquired lock")
        state = (self._count, self._owner)
        self._count = 0
        self._owner = 0
        return state

    def _acquire_restore(self, state):
        self._count, self._owner = state

    def _at_fork_reinit(self):
        self._owner = 0
        self._count = 0

    def __enter__(self):
        return self.acquire()

    def __exit__(self, *args):
        self.release()

    def __repr__(self):
        state = "locked" if self._count else "unlocked"
        return ("<" + state + " _thread.RLock object owner=" + str(self._owner) + " count=" + str(self._count)
                + " at " + hex(id(self)) + ">")


def allocate_lock():
    """Create a new lock object."""
    return lock()


allocate = allocate_lock


def get_ident():
    """Return a non-zero integer that uniquely identifies the current thread."""
    return _IDENT


def get_native_id():
    """Return the native integral Thread ID of the current thread."""
    return _IDENT


def _get_main_thread_ident():
    return _IDENT


def _is_main_interpreter():
    return True


def _count():
    """Return the number of currently running Python threads, excluding the main thread."""
    return 0


def daemon_threads_allowed():
    """Return True if daemon threads are allowed in the current interpreter."""
    return True


def start_new_thread(function, args, kwargs=None, /):
    """Start a new thread and return its identifier."""
    if not callable(function):
        raise TypeError("first arg must be callable")
    if not isinstance(args, tuple):
        raise TypeError("2nd arg must be a tuple")
    if kwargs is not None and not isinstance(kwargs, dict):
        raise TypeError("optional 3rd arg must be a dictionary")
    raise RuntimeError("can't start new thread")


start_new = start_new_thread


def start_joinable_thread(function, handle=None, daemon=True):
    raise RuntimeError("can't start new thread")


def exit():
    """This is synonymous to ``raise SystemExit''."""
    raise SystemExit


exit_thread = exit


def interrupt_main(signum=2, /):
    raise KeyboardInterrupt


def stack_size(size=0, /):
    """Return the thread stack size used when creating new threads."""
    if size != 0 and size < 32768:
        raise ValueError("size not valid: " + str(size) + " bytes")
    return 0


def _get_name():
    return "MainThread"


def set_name(name):
    pass


def _shutdown():
    pass


class _local:
    """Thread-local data"""
    # one thread: its attributes are the object's own
    pass
