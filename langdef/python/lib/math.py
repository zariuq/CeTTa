# The math module, as CPython 3.15's Modules/mathmodule.c defines it, over
# C's libm (the runtime's _cetta_libm calls the C function and returns its
# result; the domain and range errors are decided here, as math_1 does), with
# its integer functions in math.integer (Modules/mathintegermodule.c).

pi = 3.141592653589793
e = 2.718281828459045
tau = 6.283185307179586
inf = float("inf")
nan = float("nan")

_DBL_MIN = 2.2250738585072014e-308


def _real(x):
    # PyFloat_AsDouble: a float, an int, else __float__ or __index__
    if type(x) is float:
        return x
    if isinstance(x, int) or isinstance(x, float):
        return float(x)
    t = type(x)
    if hasattr(t, "__float__") or hasattr(t, "__index__"):
        return float(x)
    raise TypeError("must be real number, not " + t.__name__)


def _finite(x):
    return x == x and x != inf and x != -inf


def _domain_error(x, err):
    # math_1's domain error: the function's message with x written as repr
    # writes a float (PyOS_double_to_string 'r'), or "math domain error"
    if err is None:
        return ValueError("math domain error")
    return ValueError(err + repr(x))


def _math_1(x, name, can_overflow, err=None):
    # math_1: a NaN from a non-NaN is a domain error; an infinity from a
    # finite x a range error when the function can overflow, else a domain
    # error (a singularity)
    x = _real(x)
    r = _cetta_libm(name, x)
    if r != r and x == x:
        raise _domain_error(x, err)
    if (r == inf or r == -inf) and _finite(x):
        if can_overflow:
            raise OverflowError("math range error")
        raise _domain_error(x, err)
    return r


def acos(x):
    return _math_1(x, "acos", False, "expected a number in range from -1 up to 1, got ")


def acosh(x):
    return _math_1(x, "acosh", False, "expected argument value not less than 1, got ")


def asin(x):
    return _math_1(x, "asin", False, "expected a number in range from -1 up to 1, got ")


def asinh(x):
    return _math_1(x, "asinh", False)


def atan(x):
    return _math_1(x, "atan", False)


def atanh(x):
    return _math_1(x, "atanh", False, "expected a number between -1 and 1, got ")


def cbrt(x):
    return _math_1(x, "cbrt", False)


def cos(x):
    return _math_1(x, "cos", False, "expected a finite input, got ")


def cosh(x):
    return _math_1(x, "cosh", True)


def erf(x):
    return _cetta_libm("erf", _real(x))


def erfc(x):
    return _cetta_libm("erfc", _real(x))


def exp(x):
    return _math_1(x, "exp", True)


def exp2(x):
    return _math_1(x, "exp2", True)


def expm1(x):
    return _math_1(x, "expm1", True)


def fabs(x):
    return _math_1(x, "fabs", False)


def sin(x):
    return _math_1(x, "sin", False, "expected a finite input, got ")


def sinh(x):
    return _math_1(x, "sinh", True)


def sqrt(x):
    return _math_1(x, "sqrt", False, "expected a nonnegative input, got ")


def tan(x):
    return _math_1(x, "tan", False, "expected a finite input, got ")


def tanh(x):
    return _math_1(x, "tanh", False)


def log1p(x):
    return _math_1(x, "log1p", False, "expected argument value > -1, got ")


def atan2(y, x):
    # m_atan2: C99's special cases for infinities and zeros
    y = _real(y)
    x = _real(x)
    if x != x or y != y:
        return nan
    if y == inf or y == -inf:
        if x == inf or x == -inf:
            if copysign(1.0, x) == 1.0:
                return copysign(0.25 * pi, y)
            return copysign(0.75 * pi, y)
        return copysign(0.5 * pi, y)
    if x == inf or x == -inf or y == 0.0:
        if copysign(1.0, x) == 1.0:
            return copysign(0.0, y)
        return copysign(pi, y)
    return _cetta_libm2("atan2", y, x)


def copysign(x, y):
    return _cetta_libm2("copysign", _real(x), _real(y))


def _math_2(x, y, name):
    # math_2: a NaN from non-NaNs is a domain error, an infinity from finite
    # arguments a range error
    x = _real(x)
    y = _real(y)
    r = _cetta_libm2(name, x, y)
    if r != r:
        if x == x and y == y:
            raise ValueError("math domain error")
        return r
    if (r == inf or r == -inf) and _finite(x) and _finite(y):
        raise OverflowError("math range error")
    return r


def fmod(x, y):
    # m_fmod: fmod(x, inf) is x for finite x
    x = _real(x)
    y = _real(y)
    if (y == inf or y == -inf) and _finite(x):
        return x
    return _math_2(x, y, "fmod")


def remainder(x, y):
    return _math_2(x, y, "remainder")


def _m_log(x, name):
    # m_log, m_log2, m_log10 through math_1: zero and the negatives are
    # domain errors, which name x
    if x == x and x != inf and x != -inf:
        if x > 0.0:
            return _cetta_libm(name, x)
        raise _domain_error(x, "expected a positive input, got ")
    if x != x:
        return x
    if x > 0.0:
        return x
    raise _domain_error(x, "expected a positive input, got ")


def _loghelper_int(x, name):
    # loghelper_int: an int too big for a float has its log from its frexp,
    # log(x) + log(2) * e as one fused multiply-add
    if x <= 0:
        raise ValueError("expected a positive input")
    try:
        f = float(x)
    except OverflowError:
        m, k = _int_frexp(x)
        return _cetta_libm3("fma", _m_log(2.0, name), float(k), _m_log(m, name))
    return _m_log(f, name)


def _loghelper(x, name):
    # loghelper: an int by loghelper_int; anything else as a float, and an
    # index too big for one again as an int
    if isinstance(x, int):
        return _loghelper_int(x, name)
    try:
        f = _real(x)
    except OverflowError:
        if hasattr(type(x), "__index__"):
            return _loghelper_int(_index(x), name)
        raise
    return _m_log(f, name)


def _int_frexp(n):
    # _PyLong_Frexp: n as m times two to the k, m in [0.5, 1) correctly
    # rounded (int / int is), a carry to 1.0 moving into the exponent
    k = _bit_length(n)
    m = n / (1 << k)
    if m == 1.0:
        return 0.5, k + 1
    return m, k


def _bit_length(n):
    return n.bit_length()


def log(x, base=None):
    num = _loghelper(x, "log")
    if base is None:
        return num
    den = _loghelper(base, "log")
    return num / den


def log2(x):
    return _loghelper(x, "log2")


def log10(x):
    return _loghelper(x, "log10")


def pow(x, y):
    # math_pow_impl: C99's special cases for NaNs and infinities, then pow
    x = _real(x)
    y = _real(y)
    if not _finite(x) or not _finite(y):
        if x != x:
            return 1.0 if y == 0.0 else x
        if y != y:
            return 1.0 if x == 1.0 else y
        if x == inf or x == -inf:
            odd_y = _finite(y) and fmod(fabs(y), 2.0) == 1.0
            if y > 0.0:
                return x if odd_y else fabs(x)
            if y == 0.0:
                return 1.0
            return copysign(0.0, x) if odd_y else 0.0
        if fabs(x) == 1.0:
            return 1.0
        if y > 0.0 and fabs(x) > 1.0:
            return y
        if y < 0.0 and fabs(x) < 1.0:
            return -y
        return 0.0
    r = _cetta_libm2("pow", x, y)
    if r != r:
        raise ValueError("math domain error")
    if r == inf or r == -inf:
        if x == 0.0:
            raise ValueError("math domain error")
        raise OverflowError("math range error")
    return r


def _m_sinpi(x):
    # m_sinpi: sin(pi * x) for finite x, exact at the integers and halves
    y = _cetta_libm2("fmod", fabs(x), 2.0)
    v = 2.0 * y
    n = int(_cetta_libm("floor", v))
    if v - n >= 0.5:
        n += 1
    if n == 0:
        r = _cetta_libm("sin", pi * y)
    elif n == 1:
        r = _cetta_libm("cos", pi * (y - 0.5))
    elif n == 2:
        r = _cetta_libm("sin", pi * (1.0 - y))
    elif n == 3:
        r = -_cetta_libm("cos", pi * (y - 1.5))
    else:
        r = _cetta_libm("sin", pi * (y - 2.0))
    return copysign(1.0, x) * r


_LANCZOS_G = 6.024680040776729583740234375
_LANCZOS_G_MINUS_HALF = 5.524680040776729583740234375
_LANCZOS_NUM = (23531376880.410759688572007674451636754734846804940,
                42919803642.649098768957899047001988850926355848959,
                35711959237.355668049440185451547166705960488635843,
                17921034426.037209699919755754458931112671403265390,
                6039542586.3520280050642916443072979210699388420708,
                1439720407.3117216736632230727949123939715485786772,
                248874557.86205415651146038641322942321632125127801,
                31426415.585400194380614231628318205362874684987640,
                2876370.6289353724412254090516208496135991145378768,
                186056.26539522349504029498971604569928220784236328,
                8071.6720023658162106380029022722506138218516325024,
                210.82427775157934587250973392071336271166969580291,
                2.5066282746310002701649081771338373386264310793408)
_LANCZOS_DEN = (0.0, 39916800.0, 120543840.0, 150917976.0, 105258076.0, 45995730.0,
                13339535.0, 2637558.0, 357423.0, 32670.0, 1925.0, 66.0, 1.0)
_GAMMA_INTEGRAL = (1.0, 1.0, 2.0, 6.0, 24.0, 120.0, 720.0, 5040.0, 40320.0, 362880.0,
                   3628800.0, 39916800.0, 479001600.0, 6227020800.0, 87178291200.0,
                   1307674368000.0, 20922789888000.0, 355687428096000.0,
                   6402373705728000.0, 121645100408832000.0, 2432902008176640000.0,
                   51090942171709440000.0, 1124000727777607680000.0)
_LOGPI = 1.144729885849400174143427351353058711647


def _lanczos_sum(x):
    # lanczos_sum: the rational function, in 1/x past 5.0
    num = 0.0
    den = 0.0
    if x < 5.0:
        i = 13
        while i > 0:
            i -= 1
            num = num * x + _LANCZOS_NUM[i]
            den = den * x + _LANCZOS_DEN[i]
    else:
        for i in range(13):
            num = num / x + _LANCZOS_NUM[i]
            den = den / x + _LANCZOS_DEN[i]
    return num / den


def _m_tgamma(x):
    # m_tgamma: (value, errno) with errno None, "EDOM" or "ERANGE"
    if not _finite(x):
        if x != x or x > 0.0:
            return x, None
        return nan, "EDOM"
    if x == 0.0:
        return copysign(inf, x), "EDOM"
    if x == _cetta_libm("floor", x):
        if x < 0.0:
            return nan, "EDOM"
        if x <= 23:
            return _GAMMA_INTEGRAL[int(x) - 1], None
    absx = fabs(x)
    if absx < 1e-20:
        r = 1.0 / x
        return r, ("ERANGE" if r == inf or r == -inf else None)
    if absx > 200.0:
        if x < 0.0:
            return 0.0 / _m_sinpi(x), None
        return inf, "ERANGE"
    y = absx + _LANCZOS_G_MINUS_HALF
    if absx > _LANCZOS_G_MINUS_HALF:
        q = y - absx
        z = q - _LANCZOS_G_MINUS_HALF
    else:
        q = y - _LANCZOS_G_MINUS_HALF
        z = q - absx
    z = z * _LANCZOS_G / y
    if x < 0.0:
        r = -pi / _m_sinpi(absx) / absx * _cetta_libm("exp", y) / _lanczos_sum(absx)
        r -= z * r
        if absx < 140.0:
            r /= _cetta_libm2("pow", y, absx - 0.5)
        else:
            sqrtpow = _cetta_libm2("pow", y, absx / 2.0 - 0.25)
            r /= sqrtpow
            r /= sqrtpow
    else:
        r = _lanczos_sum(absx) / _cetta_libm("exp", y)
        r += z * r
        if absx < 140.0:
            r *= _cetta_libm2("pow", y, absx - 0.5)
        else:
            sqrtpow = _cetta_libm2("pow", y, absx / 2.0 - 0.25)
            r *= sqrtpow
            r *= sqrtpow
    return r, ("ERANGE" if r == inf or r == -inf else None)


def _m_lgamma(x):
    # m_lgamma: (value, errno)
    if not _finite(x):
        if x != x:
            return x, None
        return inf, None
    if x == _cetta_libm("floor", x) and x <= 2.0:
        if x <= 0.0:
            return inf, "EDOM"
        return 0.0, None
    absx = fabs(x)
    if absx < 1e-20:
        return -_cetta_libm("log", absx), None
    r = _cetta_libm("log", _lanczos_sum(absx)) - _LANCZOS_G
    r += (absx - 0.5) * (_cetta_libm("log", absx + _LANCZOS_G - 0.5) - 1)
    if x < 0.0:
        r = _LOGPI - _cetta_libm("log", fabs(_m_sinpi(absx))) - _cetta_libm("log", absx) - r
    return r, ("ERANGE" if r == inf or r == -inf else None)


def _math_1a(x, f, err):
    # math_1a: errno decides, a pole naming x, an overflow a range error (an
    # underflow, below 1.5, is no error: is_error)
    x = _real(x)
    r, errno = f(x)
    if errno == "EDOM":
        raise ValueError(err + repr(x))
    if errno == "ERANGE" and fabs(r) >= 1.5:
        raise OverflowError("math range error")
    return r


def gamma(x):
    return _math_1a(x, _m_tgamma, "expected a noninteger or positive integer, got ")


def lgamma(x):
    return _math_1a(x, _m_lgamma, "expected a noninteger or positive integer, got ")


def fma(x, y, z):
    # math_fma_impl: one rounding; a NaN from non-NaNs and an infinity from
    # finite arguments refused
    x = _real(x)
    y = _real(y)
    z = _real(z)
    r = _cetta_libm3("fma", x, y, z)
    if _finite(r):
        return r
    if r != r:
        if x == x and y == y and z == z:
            raise ValueError("invalid operation in fma")
    elif _finite(x) and _finite(y) and _finite(z):
        raise OverflowError("overflow in fma")
    return r


def fmax(x, y):
    # C's fmax as glibc gives it: a NaN loses to a number; of two equal
    # operands (0.0 and -0.0 among them) the second
    x = _real(x)
    y = _real(y)
    if x != x:
        return y
    if y != y:
        return x
    return x if x > y else y


def fmin(x, y):
    # C's fmin as glibc gives it: a NaN loses to a number; of two equal
    # operands (0.0 and -0.0 among them) the second
    x = _real(x)
    y = _real(y)
    if x != x:
        return y
    if y != y:
        return x
    return x if x < y else y


def signbit(x):
    return copysign(1.0, _real(x)) < 0.0


def isnormal(x):
    x = _real(x)
    return _finite(x) and fabs(x) >= _DBL_MIN


def issubnormal(x):
    x = _real(x)
    return _finite(x) and x != 0.0 and fabs(x) < _DBL_MIN


def degrees(x):
    return _real(x) * (180.0 / pi)


def radians(x):
    return _real(x) * (pi / 180.0)


def isnan(x):
    x = _real(x)
    return x != x


def isinf(x):
    x = _real(x)
    return x == inf or x == -inf


def isfinite(x):
    return _finite(_real(x))


def isclose(a, b, *, rel_tol=1e-09, abs_tol=0.0):
    # math_isclose_impl
    a = _real(a)
    b = _real(b)
    rel_tol = _real(rel_tol)
    abs_tol = _real(abs_tol)
    if rel_tol < 0.0 or abs_tol < 0.0:
        raise ValueError("tolerances must be non-negative")
    if a == b:
        return True
    if a == inf or a == -inf or b == inf or b == -inf:
        return False
    diff = fabs(b - a)
    return ((diff <= fabs(rel_tol * b)) or (diff <= fabs(rel_tol * a))) or (diff <= abs_tol)


def floor(x):
    # math_floor: a float's floor as an int, else __floor__
    if type(x) is float:
        return int(_cetta_libm("floor", x))
    if isinstance(x, int):
        return int(x)
    f = getattr(type(x), "__floor__", None)
    if f is not None:
        return f(x)
    return int(_cetta_libm("floor", _real(x)))


def ceil(x):
    if type(x) is float:
        return int(_cetta_libm("ceil", x))
    if isinstance(x, int):
        return int(x)
    f = getattr(type(x), "__ceil__", None)
    if f is not None:
        return f(x)
    return int(_cetta_libm("ceil", _real(x)))


def trunc(x):
    if type(x) is float:
        return int(x)
    if isinstance(x, int):
        return int(x)
    f = getattr(type(x), "__trunc__", None)
    if f is None:
        raise TypeError("type " + type(x).__name__ + " doesn't define __trunc__ method")
    return f(x)


def _index(x):
    if isinstance(x, int):
        return int(x)
    f = getattr(type(x), "__index__", None)
    if f is None:
        raise TypeError("'" + type(x).__name__ + "' object cannot be interpreted as an integer")
    return f(x)


def gcd(*integers):
    g = 0
    for x in integers:
        x = _index(x)
        a, b = abs(g), abs(x)
        while b:
            a, b = b, a % b
        g = a
    return g


def lcm(*integers):
    r = 1
    for x in integers:
        x = _index(x)
        if r == 0 or x == 0:
            r = 0
            continue
        g = gcd(r, x)
        r = abs(r // g * x)
    return r


def factorial(n):
    if type(n) is float:
        raise TypeError("'float' object cannot be interpreted as an integer")
    n = _index(n)
    if n < 0:
        raise ValueError("factorial() not defined for negative values")
    r = 1
    i = 2
    while i <= n:
        r *= i
        i += 1
    return r


def isqrt(n):
    n = _index(n)
    if n < 0:
        raise ValueError("isqrt() argument must be nonnegative")
    if n == 0:
        return 0
    x = 1 << ((_bit_length(n) + 1) // 2)
    while True:
        y = (x + n // x) // 2
        if y >= x:
            return x
        x = y


def comb(n, k):
    n = _index(n)
    k = _index(k)
    if n < 0:
        raise ValueError("n must be a non-negative integer")
    if k < 0:
        raise ValueError("k must be a non-negative integer")
    if k > n:
        return 0
    k = min(k, n - k)
    r = 1
    i = 1
    while i <= k:
        r = r * (n - k + i) // i
        i += 1
    return r


def perm(n, k=None):
    n = _index(n)
    if k is None:
        if n < 0:
            raise ValueError("n must be a non-negative integer")
        return factorial(n)
    k = _index(k)
    if n < 0:
        raise ValueError("n must be a non-negative integer")
    if k < 0:
        raise ValueError("k must be a non-negative integer")
    if k > n:
        return 0
    r = 1
    i = 0
    while i < k:
        r *= n - i
        i += 1
    return r


def prod(iterable, *, start=1):
    r = start
    for x in iterable:
        r = r * x
    return r


_LONG_MIN = -(1 << 63)
_LONG_MAX = (1 << 63) - 1


def _dl_sum(a, b):
    # dl_sum: a + b exactly, as a sum and its error
    x = a + b
    z = x - a
    y = (a - (x - z)) + (b - z)
    return x, y


def _tl_fma(x, y, total):
    # tl_fma: total + x * y in triple length (dl_mul by fma)
    hi, lo, tiny = total
    pr_hi = x * y
    pr_lo = _cetta_libm3("fma", x, y, -pr_hi)
    sm_hi, sm_lo = _dl_sum(hi, pr_hi)
    r1_hi, r1_lo = _dl_sum(lo, pr_lo)
    r2_hi, r2_lo = _dl_sum(r1_hi, sm_lo)
    return (sm_hi, r2_hi, tiny + r1_lo + r2_lo)


def _tl_to_d(total):
    hi, lo, tiny = total
    last_hi, last_lo = _dl_sum(lo, hi)
    return tiny + last_lo + last_hi


def sumprod(p, q):
    # math_sumprod_impl: exact ints while each product and the sum fit a C
    # long, then floats (and int-float pairs) in triple length while the
    # total is finite, then + and * of the items; each path once given up
    # stays so, its total added in
    p_it = iter(p)
    q_it = iter(q)
    total = 0
    int_path = True
    int_total = 0
    int_in_use = False
    flt_path = True
    flt_total = (0.0, 0.0, 0.0)
    flt_in_use = False
    while True:
        p_stopped = False
        q_stopped = False
        p_i = None
        q_i = None
        try:
            p_i = next(p_it)
        except StopIteration:
            p_stopped = True
        try:
            q_i = next(q_it)
        except StopIteration:
            q_stopped = True
        if p_stopped != q_stopped:
            raise ValueError("Inputs are not the same length")
        finished = p_stopped and q_stopped
        if int_path:
            if not finished and type(p_i) is int and type(q_i) is int:
                if _LONG_MIN <= p_i <= _LONG_MAX and _LONG_MIN <= q_i <= _LONG_MAX:
                    prod_i = p_i * q_i
                    if _LONG_MIN <= prod_i <= _LONG_MAX and _LONG_MIN <= int_total + prod_i <= _LONG_MAX:
                        int_total += prod_i
                        int_in_use = True
                        continue
            int_path = False
            if int_in_use:
                total = total + int_total
                int_total = 0
                int_in_use = False
        if flt_path:
            if not finished:
                ok = True
                tp = type(p_i)
                tq = type(q_i)
                if tp is float and tq is float:
                    fp = p_i
                    fq = q_i
                elif tp is float and (tq is int or tq is bool):
                    fp = p_i
                    try:
                        fq = float(q_i)
                    except OverflowError:
                        ok = False
                elif tq is float and (tp is int or tp is bool):
                    fq = q_i
                    try:
                        fp = float(p_i)
                    except OverflowError:
                        ok = False
                else:
                    ok = False
                if ok:
                    new_total = _tl_fma(fp, fq, flt_total)
                    if _finite(new_total[0]):
                        flt_total = new_total
                        flt_in_use = True
                        continue
            flt_path = False
            if flt_in_use:
                total = total + _tl_to_d(flt_total)
                flt_total = (0.0, 0.0, 0.0)
                flt_in_use = False
        if finished:
            return total
        total = total + p_i * q_i


def fsum(seq):
    # math_fsum: Shewchuk's exact partials, then a half-even fix-up
    partials = []
    special_sum = 0.0
    inf_sum = 0.0
    for item in seq:
        x = _real(item)
        xsave = x
        i = 0
        kept = []
        for y in partials:
            if fabs(x) < fabs(y):
                x, y = y, x
            hi = x + y
            yr = hi - x
            lo = y - yr
            if lo != 0.0:
                kept.append(lo)
            x = hi
        partials = kept
        if x != 0.0:
            if not _finite(x):
                if _finite(xsave):
                    raise OverflowError("intermediate overflow in fsum")
                if xsave == inf or xsave == -inf:
                    inf_sum += xsave
                special_sum += xsave
                partials = []
            else:
                partials.append(x)
    if special_sum != 0.0:
        if inf_sum != inf_sum:
            raise ValueError("-inf + inf in fsum")
        return special_sum
    hi = 0.0
    n = len(partials)
    lo = 0.0
    if n > 0:
        n -= 1
        hi = partials[n]
        while n > 0:
            x = hi
            n -= 1
            y = partials[n]
            hi = x + y
            yr = hi - x
            lo = y - yr
            if lo != 0.0:
                break
        if n > 0 and ((lo < 0.0 and partials[n - 1] < 0.0) or (lo > 0.0 and partials[n - 1] > 0.0)):
            y = lo * 2.0
            x = hi + y
            yr = x - hi
            if y == yr:
                hi = x
    return hi


def _vector_norm(vec, mx, found_nan):
    # vector_norm: scaled, squared exactly (fma), summed with compensation,
    # the square root corrected once
    if mx == inf:
        return mx
    if found_nan:
        return nan
    n = len(vec)
    if mx == 0.0 or n <= 1:
        return mx
    m, max_e = frexp(mx)
    if max_e < -1023:
        return _DBL_MIN * _vector_norm([v / _DBL_MIN for v in vec], mx / _DBL_MIN, found_nan)
    scale = ldexp(1.0, -max_e)
    csum = 1.0
    frac1 = 0.0
    frac2 = 0.0
    for v in vec:
        x = v * scale
        hi = x * x
        lo = _cetta_libm3("fma", x, x, -hi)
        s = csum + hi
        frac2 += (csum - s) + hi
        csum = s
        frac1 += lo
    h = sqrt(csum - 1.0 + (frac1 + frac2))
    hi = -h * h
    lo = _cetta_libm3("fma", -h, h, -hi)
    s = csum + hi
    frac2 += (csum - s) + hi
    csum = s
    frac1 += lo
    x = csum - 1.0 + (frac1 + frac2)
    h += x / (2.0 * h)
    return h / scale


def hypot(*coordinates):
    vec = []
    mx = 0.0
    found_nan = False
    for c in coordinates:
        x = fabs(_real(c))
        vec.append(x)
        found_nan = found_nan or x != x
        if x > mx:
            mx = x
    return _vector_norm(vec, mx, found_nan)


def dist(p, q):
    p = tuple(p)
    q = tuple(q)
    if len(p) != len(q):
        raise ValueError("both points must have the same number of dimensions")
    vec = []
    mx = 0.0
    found_nan = False
    for i in range(len(p)):
        x = fabs(_real(p[i]) - _real(q[i]))
        vec.append(x)
        found_nan = found_nan or x != x
        if x > mx:
            mx = x
    return _vector_norm(vec, mx, found_nan)


def frexp(x):
    x = _real(x)
    if x != x or x == inf or x == -inf or x == 0.0:
        return (x, 0)
    return _cetta_frexp(x)


def ldexp(x, i):
    x = _real(x)
    if not isinstance(i, int):
        raise TypeError("Expected an int as second argument to ldexp.")
    if x == 0.0 or not _finite(x):
        return x
    r = _cetta_ldexp(x, i)
    if r == inf or r == -inf:
        raise OverflowError("math range error")
    return r


def modf(x):
    x = _real(x)
    if x == inf or x == -inf:
        return (copysign(0.0, x), x)
    return _cetta_modf(x)


def nextafter(x, y, *, steps=None):
    x = _real(x)
    y = _real(y)
    if steps is None:
        return _cetta_libm2("nextafter", x, y)
    steps = _index(steps)
    if steps < 0:
        raise ValueError("steps must be a non-negative integer")
    while steps > 0:
        x = _cetta_libm2("nextafter", x, y)
        steps -= 1
    return x


def ulp(x):
    x = _real(x)
    if x != x:
        return x
    x = fabs(x)
    if x == inf:
        return x
    x2 = _cetta_libm2("nextafter", x, inf)
    if x2 == inf:
        x2 = _cetta_libm2("nextafter", x, -inf)
        return x - x2
    return x2 - x


def _integer_module():
    # math.integer (3.15): the integer functions, one object each with
    # math's own, their module math.integer
    import sys
    m = type(sys)("math.integer")
    for f in (comb, factorial, gcd, isqrt, lcm, perm):
        f.__module__ = "math.integer"
        setattr(m, f.__name__, f)
    sys.modules["math.integer"] = m
    sys.modules["_math_integer"] = m
    return m


integer = _integer_module()
