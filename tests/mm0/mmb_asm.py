"""Independent MMB writer for tests, written from mm0-c/mmb.md alone.

It shares no code with the native verifier, so a file it writes checks the
decoder against the format description rather than against itself.  It
writes exactly what it is told, including ill-formed files: the tests use it
for negative controls as much as for accepted proofs.

    m = Mmb()
    wff = m.sort(PROVABLE | STRICT)
    wi = m.term([reg(wff), reg(wff)], ret(wff))
    ...
    data = m.build()

Commands are (opcode, data) pairs; data defaults to 0.  `stmts` are added in
the order the methods are called, which is also the order of the proof
stream, as the format requires.
"""

import struct

PURE, STRICT, PROVABLE, FREE = 1, 2, 4, 8

# Unify stream opcodes.
UTERM, UTERM_SAVE, UREF, UDUMMY, UHYP = 0x30, 0x31, 0x32, 0x33, 0x36
# Proof stream opcodes.
TERM, TERM_SAVE, REF, DUMMY, THM, THM_SAVE, HYP = 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16
CONV, REFL, SYMM, CONG, UNFOLD, CONV_CUT, CONV_SAVE, SAVE, SORRY = (
    0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1E, 0x1F, 0x20)
END = 0x00
# Statement opcodes.
S_SORT, S_TERM, S_DEF, S_LOCAL_DEF, S_AXIOM, S_THM, S_LOCAL_THM = (
    0x04, 0x05, 0x05, 0x0D, 0x02, 0x06, 0x0E)


def bound(sort, ordinal):
    """The `ordinal`-th bound binder of a declaration has deps = 1 << ordinal."""
    return (1 << 63) | (sort << 56) | (1 << ordinal)


def reg(sort, deps=0):
    return (sort << 56) | deps


def ret(sort, deps=0):
    return (sort << 56) | deps


def cmd(op, data=0, width=None):
    """Encode one (cmd, data) pair; `width` forces 0, 1, 2 or 4 data bytes."""
    if width is None:
        width = 0 if data == 0 else 1 if data < 1 << 8 else 2 if data < 1 << 16 else 4
    size_bits = {0: 0, 1: 1, 2: 2, 4: 3}[width]
    out = bytes([(size_bits << 6) | op])
    if width:
        out += data.to_bytes(width, "little")
    return out


def stream(cmds):
    out = b""
    for c in cmds:
        if isinstance(c, bytes):
            out += c
        elif isinstance(c, tuple):
            out += cmd(*c)
        else:
            out += cmd(c)
    return out


class Mmb:
    def __init__(self):
        self.sorts = []
        self.terms = []   # (args, ret, unify-or-None)
        self.thms = []    # (args, unify)
        self.stmts = []   # (opcode, proof bytes or None)
        self.names = {"sorts": [], "terms": [], "thms": []}

    def sort(self, mods=0, name=None):
        self.sorts.append(mods)
        self.stmts.append((S_SORT, None))
        self.names["sorts"].append(name)
        return len(self.sorts) - 1

    def term(self, args, ret_type, name=None):
        self.terms.append((list(args), ret_type, None))
        self.stmts.append((S_TERM, None))
        self.names["terms"].append(name)
        return len(self.terms) - 1

    def define(self, args, ret_type, unify, proof, local=False, name=None):
        self.terms.append((list(args), ret_type, stream(unify)))
        self.stmts.append((S_LOCAL_DEF if local else S_DEF, stream(proof)))
        self.names["terms"].append(name)
        return len(self.terms) - 1

    def axiom(self, args, unify, proof, name=None):
        self.thms.append((list(args), stream(unify)))
        self.stmts.append((S_AXIOM, stream(proof)))
        self.names["thms"].append(name)
        return len(self.thms) - 1

    def theorem(self, args, unify, proof, local=False, name=None):
        self.thms.append((list(args), stream(unify)))
        self.stmts.append((S_LOCAL_THM if local else S_THM, stream(proof)))
        self.names["thms"].append(name)
        return len(self.thms) - 1

    def raw_statement(self, opcode, proof=None):
        """Append a statement that touches no table, for malformed streams."""
        self.stmts.append((opcode, None if proof is None else stream(proof)))

    def build(self, index=False, header=None):
        """Lay the file out as mm0-rs does: header, sorts, tables, data,
        proof stream, padding, then the optional index."""
        out = bytearray(40 + len(self.sorts))
        out[40:] = bytes(self.sorts)

        def align(n):
            while len(out) % n:
                out.append(0)

        align(8)
        p_terms = len(out)
        out += bytes(8 * len(self.terms))
        p_thms = len(out)
        out += bytes(8 * len(self.thms))
        for i, (args, ret_type, unify) in enumerate(self.terms):
            align(8)
            p_data = len(out)
            out += b"".join(struct.pack("<Q", a) for a in args)
            out += struct.pack("<Q", ret_type)
            # mm0-c and mm0-rs read the unify stream directly after `ret`;
            # mmb.md's `p32<unify_stream>` field is not what either does.
            if unify is not None:
                out += unify
            sort_byte = (ret_type >> 56) & 0x7F
            struct.pack_into("<HBBI", out, p_terms + 8 * i, len(args),
                             sort_byte | (0x80 if unify is not None else 0), 0, p_data)
        for i, (args, unify) in enumerate(self.thms):
            align(8)
            p_data = len(out)
            out += b"".join(struct.pack("<Q", a) for a in args)
            out += unify
            struct.pack_into("<HHI", out, p_thms + 8 * i, len(args), 0, p_data)
        p_proof = len(out)
        for opcode, proof in self.stmts:
            body = proof if proof is not None else b""
            # The statement's data is the distance to the next statement.
            # The 4-byte form keeps the header a fixed 5 bytes.
            length = 5 + len(body)
            out += cmd(opcode, length, width=4) + body
        out += bytes([END])
        out += bytes(4)  # END must begin at least 5 bytes before EOF.
        p_index = 0
        if index:
            p_index = self._index(out)
        fields = dict(magic=0x42304D4D, version=1, num_sorts=len(self.sorts),
                      reserved=0, num_terms=len(self.terms), num_thms=len(self.thms),
                      p_terms=p_terms, p_thms=p_thms, p_proof=p_proof, reserved2=0,
                      p_index=p_index)
        if header:
            fields.update(header)
        struct.pack_into("<IBBHIIIIIIQ", out, 0, fields["magic"], fields["version"],
                         fields["num_sorts"], fields["reserved"], fields["num_terms"],
                         fields["num_thms"], fields["p_terms"], fields["p_thms"],
                         fields["p_proof"], fields["reserved2"], fields["p_index"])
        return bytes(out)

    def _index(self, out):
        """A Name table only; advisory, as the format says."""
        while len(out) % 8:
            out.append(0)
        strings = []
        entries = []
        for kind in ("sorts", "terms", "thms"):
            for name in self.names[kind]:
                entries.append(name)
        p_index = len(out)
        out += struct.pack("<Q", 1)
        name_entry_at = len(out)
        out += struct.pack("<IIQ", 0x656D614E, 0, 0)
        p_names = len(out)
        out += bytes(16 * len(entries))
        for i, name in enumerate(entries):
            if name is None:
                continue
            p = len(out)
            out += name.encode() + b"\0"
            strings.append(p)
            struct.pack_into("<QQ", out, p_names + 16 * i, 0, p)
        struct.pack_into("<IIQ", out, name_entry_at, 0x656D614E, 0, p_names)
        return p_index
