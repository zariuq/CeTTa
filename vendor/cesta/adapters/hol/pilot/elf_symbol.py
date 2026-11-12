"""Select an exact x86-64 ELF function and its containing .text image.

The proof independently loads .text with s2n-bignum. This selector checks the
symbol/section boundary used in the artifact receipt; it is not a prover.
"""

from dataclasses import dataclass
import hashlib
from pathlib import Path
import struct


@dataclass(frozen=True)
class Symbol:
    image: bytes
    text: bytes
    code: bytes
    offset: int
    size: int
    file_offset: int
    symbol_offset: int
    address: int

    def identity(self):
        return dict(image_sha256=hashlib.sha256(self.image).hexdigest(),
                    symbol_offset=self.offset,symbol_size=self.size,
                    symbol_address=self.address,target='x86_64-sysv-lp64')


def select(path: Path, name: str) -> Symbol:
    data=path.read_bytes()
    def part(offset,count):
        if offset<0 or count<0 or offset+count>len(data):
            raise ValueError('ELF extent exceeds file')
        return data[offset:offset+count]
    def string(table,offset):
        if offset<0 or offset>=len(table):raise ValueError('bad ELF string offset')
        end=table.find(b'\0',offset)
        if end<0:raise ValueError('unterminated ELF string')
        return table[offset:end]
    header=struct.unpack('<16sHHIQQQIHHHHHH',part(0,64))
    ident,kind,machine,version,_,_,sections,_,ehsize,_,_,section_size,count,names=header
    if ident[:7]!=b'\x7fELF\x02\x01\x01' or kind!=2 or machine!=62 or version!=1 or ehsize!=64:
        raise ValueError('expected a linked little-endian ELF64 x86 executable')
    if section_size!=64 or not count or names>=count:
        raise ValueError('unsupported ELF section table')
    table=[struct.unpack('<IIQQQQIIQQ',part(sections+i*64,64)) for i in range(count)]
    strings=part(table[names][4],table[names][5])
    text_indices=[i for i,s in enumerate(table) if string(strings,s[0])==b'.text']
    if len(text_indices)!=1:raise ValueError('one .text section is required')
    index=text_indices[0];text_section=table[index]
    if text_section[1]!=1 or text_section[2]&7!=6:
        raise ValueError('text must be allocated, executable and nonwritable')
    text=part(text_section[4],text_section[5]);matches=[]
    for section in table:
        if section[1] in (4,9) and section[7]==index and section[5]:
            raise ValueError('unresolved text relocations')
        if section[1]!=2:continue
        if section[9]!=24 or section[5]%24 or section[6]>=count:
            raise ValueError('bad symbol table')
        st=table[section[6]]
        if st[1]!=3:raise ValueError('symbol strings must use a string table')
        names_data=part(st[4],st[5])
        for offset in range(section[4],section[4]+section[5],24):
            symbol,info,_,where,value,size=struct.unpack('<IBBHQQ',part(offset,24))
            if string(names_data,symbol)!=name.encode():continue
            if info&15!=2 or where!=index or not size:
                raise ValueError('selected symbol is not a nonempty text function')
            start=value-text_section[3]
            if start<0 or start+size>len(text):raise ValueError('function exceeds text section')
            matches.append(Symbol(data,text,text[start:start+size],start,size,
                                  text_section[4]+start,offset,value))
    if len(matches)!=1:raise ValueError('exactly one selected function symbol is required')
    return matches[0]


def assert_identity(path: Path, name: str, expected: dict):
    if select(path,name).identity()!=expected:
        raise ValueError('artifact no longer matches the qualified symbol')
