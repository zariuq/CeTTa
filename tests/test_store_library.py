#!/usr/bin/env python3
"""The actual MeTTa store API versus an offline SQLite byte-map oracle."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import random
import sqlite3
import struct
import sys
import tempfile

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/"tools"))
from space_service_client import E,S,FloatBits,BigInt,encode
from value_worker_client import Worker


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("worker",type=Path)
    parser.add_argument("--output",type=Path)
    args=parser.parse_args()
    module=ROOT/"tests/support/store_fixture.metta"
    checks=0
    def check(condition):
        nonlocal checks
        checks+=1
        if not condition:
            raise AssertionError("store library check "+str(checks))
    with tempfile.TemporaryDirectory(prefix="native-store-api-") as folder:
        worker=Worker(args.worker.resolve(),module,"store-fixture:run",language="petta")
        path=Path(folder)/"store";path.mkdir(mode=0o700)
        database=sqlite3.connect(":memory:")
        database.execute("CREATE TABLE rows(key BLOB PRIMARY KEY,value BLOB NOT NULL)")
        try:
            check(worker.call(E("Create",str(path)))==S("Created"))
            values=[E("Evidence","literal (+ 1 2)",False,S("JsonNull"),FloatBits(0x8000000000000000)),
                    E("Distinct",1,FloatBits(0x3ff0000000000000),"",S("empty"),BigInt(str(1<<90)))]
            for value in values:
                answer=worker.call(E("Codec",value))
                check(encode(answer)==encode(E("StoreDecoded",value)))
            key_rng=random.Random(31427)
            integers=[-(1<<63),-1,0,1,(1<<63)-1]+[key_rng.randrange(-(1<<63),1<<63) for _ in range(512)]
            for integer in integers:
                expected=struct.pack(">Q",(integer&((1<<64)-1))^(1<<63))
                actual=worker.call(E("IntegerKey",integer))
                check(encode(actual)==encode(E("IntegerKeyRoundTrip",expected,E("StoreKeyInteger",integer))))
            for size in list(range(8))+[9,16]:
                check(worker.call(E("DecodeIntegerKey",b"\0"*size))==E("StoreRefused",S("invalid-integer-key")))
            rng=random.Random(55127);revision=0
            keys=[b"",b"\0",b"\xff",b"a/one",b"a/two",b"b/one","世界".encode()]
            for step in range(180):
                key=rng.choice(keys);value=rng.randbytes(rng.randrange(100))
                remove=rng.randrange(4)==0
                op=E("remove",key) if remove else E("put",key,value)
                answer=worker.call(E("Apply",str(path),E("reads",E("point",key)),E("writes",E("point",key)),E("ops",op)))
                revision+=1;check(answer==E("Applied",revision))
                with database:
                    if remove:database.execute("DELETE FROM rows WHERE key=?",(key,))
                    else:database.execute("INSERT OR REPLACE INTO rows VALUES(?,?)",(key,value))
                expected=database.execute("SELECT value FROM rows WHERE key=?",(key,)).fetchone()
                actual=worker.call(E("Read",str(path),key,1024))
                check(encode(actual)==encode(S("StoreAbsent") if expected is None else E("StoreValue",expected[0])))
                if step%13==0:
                    for lower,upper in ((None,None),(b"\0",b"\xff"),(b"a/",b"b/"),
                                        (b"a/one",b"a/one"),(b"z",None),(None,b"")):
                        constraints,parameters=[],[]
                        if lower is not None:constraints.append("key>=?");parameters.append(lower)
                        if upper is not None:constraints.append("key<?");parameters.append(upper)
                        query="SELECT key,length(value) FROM rows"
                        if constraints:query+=" WHERE "+" AND ".join(constraints)
                        for descending in (False,True):
                            rows=database.execute(query+" ORDER BY key "+("DESC" if descending else "ASC"),parameters).fetchall()
                            actual=worker.call(E("ReverseKeys" if descending else "Keys",str(path),
                                S("NoBound") if lower is None else lower,S("NoBound") if upper is None else upper))
                            check(encode(actual)==encode(tuple(tuple(row) for row in rows)))
                if step%37==0:
                    answer=worker.call(E("Maintenance",str(path)))
                    check(answer[0]==S("Maintained") and answer[1]==revision)
            worker.close()
            worker=Worker(args.worker.resolve(),module,"store-fixture:run",language="petta")
            rows=database.execute("SELECT key,length(value) FROM rows ORDER BY key ASC").fetchall()
            check(encode(worker.call(E("Keys",str(path),S("NoBound"),S("NoBound"))))==encode(tuple(tuple(row) for row in rows)))
            result={"checks":checks,"language":"petta","sqlite_version":sqlite3.sqlite_version,
                "module_sha256":hashlib.sha256(module.read_bytes()).hexdigest(),
                "worker_sha256":hashlib.file_digest(args.worker.open("rb"),"sha256").hexdigest(),
                "live_cutover":False}
            if args.output:args.output.write_text(json.dumps(result,indent=2)+"\n")
            print(json.dumps(result,indent=2))
        finally:
            worker.close();database.close()


if __name__=="__main__":main()
