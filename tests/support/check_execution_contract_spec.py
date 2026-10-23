#!/usr/bin/env python3
"""The execution-contract generator accepts the authored spec and refuses
spellings of it that break an operand discipline's well-formedness."""
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
import gen_execution_contracts as contracts  # noqa: E402

SHORT_AND = '    and 2 short-circuit grounded-dispatch (dialects prime))'
ABSORB_OR = '  (short-circuit-absorbing or 2 True)'
ABS_MATH = '    abs-math 1 strict-all grounded-dispatch)'

# Each mutation replaces one line of the spec and must be refused with the
# reason quoted.
REFUSED = (
    ('a dialect reads two rows for one head',
     SHORT_AND,
     SHORT_AND + '\n  (prepared-pure-intrinsic-head\n'
     '    and 2 strict-all grounded-dispatch (dialects prime))',
     'unique by symbol, arity and dialect'),
    ('a short-circuit head without its absorbing value',
     ABSORB_OR, '', 'declares no absorbing value'),
    ('an absorbing value that is not a truth value',
     ABSORB_OR, '  (short-circuit-absorbing or 2 Maybe)',
     'must absorb a truth value'),
    ('an absorbing value of a head with no short-circuit row',
     ABSORB_OR, ABSORB_OR + '\n  (short-circuit-absorbing + 2 False)',
     'which no short-circuit head declares'),
    ('a short-circuit head that is not binary',
     ABS_MATH, '    abs-math 1 short-circuit grounded-dispatch)',
     'must be a binary grounded dispatch'),
    ('a discipline the presentation does not declare',
     '  (operand-discipline short-circuit)', '',
     'names undeclared operand discipline short-circuit'),
)


def main():
    text = contracts.SPEC.read_text()
    contracts.Spec(text)
    checks = 1
    for name, old, new, reason in REFUSED:
        assert text.count(old) == 1, (name, 'mutation site is not unique')
        try:
            contracts.Spec(text.replace(old, new))
        except contracts.SpecError as error:
            assert reason in str(error), (name, str(error))
        else:
            raise AssertionError(f'accepted: {name}')
        checks += 1
    print(f'ExecutionContractSpec {checks} 0')


if __name__ == '__main__':
    main()
