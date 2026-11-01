#!/usr/bin/env python3
"""Compare two outputs of a MeTTa run, reading each printed result list as a bag.

Usage: compare_result_bags.py EXPECTED_FILE ACTUAL_FILE

A line of the form [r1, r2, ...] lists the results of one top-level
evaluation.  Those results form a bag: a space answers a query without
promising an order, so two runs may list the same results in different
orders.  Each such line is compared as the multiset of its elements; every
other line, and the order of the lines, is compared exactly.  An element is
a whole result, so the order inside an expression still matters.
"""
import difflib
import sys


def split_elements(body):
    """Split the inside of [ ... ] at its top-level commas."""
    elements, depth, start, i, quoted = [], 0, 0, 0, False
    while i < len(body):
        c = body[i]
        if quoted:
            if c == '\\':
                i += 1
            elif c == '"':
                quoted = False
        elif c == '"':
            quoted = True
        elif c in '([{':
            depth += 1
        elif c in ')]}':
            depth -= 1
            if depth < 0:
                return None
        elif c == ',' and depth == 0:
            elements.append(body[start:i].strip())
            start = i + 1
        i += 1
    if quoted or depth != 0:
        return None
    last = body[start:].strip()
    if last or elements:
        elements.append(last)
    return elements


def normalize(line):
    text = line.rstrip('\n')
    if len(text) >= 2 and text[0] == '[' and text[-1] == ']':
        elements = split_elements(text[1:-1])
        if elements is not None:
            return '[' + ', '.join(sorted(elements)) + ']'
    return text


def main():
    with open(sys.argv[1], encoding='utf-8', errors='replace') as f:
        expected = f.read().splitlines()
    with open(sys.argv[2], encoding='utf-8', errors='replace') as f:
        actual = f.read().splitlines()
    left = [normalize(line) for line in expected]
    right = [normalize(line) for line in actual]
    if left == right:
        return 0
    sys.stdout.writelines(line + '\n' for line in difflib.unified_diff(
        expected, actual, 'expected', 'actual', lineterm='', n=1))
    return 1


if __name__ == '__main__':
    sys.exit(main())
