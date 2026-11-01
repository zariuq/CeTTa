"""Portable byte policy is selected consistently across library spellings."""
import argparse
import json
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/"tools"))
from value_worker_client import Worker
from space_service_client import E,MAX_BYTES


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("worker",type=Path)
    p.add_argument("--cli",type=Path,required=True)
    p.add_argument("--legacy-worker",type=Path)
    p.add_argument("--output",type=Path)
    args=p.parse_args();results=[]
    with tempfile.TemporaryDirectory(prefix="value-policy-") as directory:
        root=Path(directory)
        for language,spelling in [("petta","(library store)"),("prime","store")]:
            module=root/(language+".metta")
            module.write_text('!(import! &self '+spelling+')\n(= (policy:run $request) (store:value-byte-limit))\n')
            small=Worker(args.worker.resolve(),module,"policy:run",language=language)
            large=Worker(args.worker.resolve(),module,"policy:run",language=language,maximum_bytes=8*MAX_BYTES)
            try:
                for worker,wanted in [(small,MAX_BYTES),(large,8*MAX_BYTES),(small,MAX_BYTES),(large,8*MAX_BYTES)]:
                    assert worker.call(E("Probe"))==wanted
                results.append(dict(language=language,small=MAX_BYTES,large=8*MAX_BYTES,isolated=True))
            finally:small.close();large.close()
            if args.legacy_worker:
                old=Worker(args.legacy_worker.resolve(),module,"policy:run",language=language)
                try:assert old.call(E("Probe"))==MAX_BYTES
                finally:old.close()
                results.append(dict(language=language,legacy_default=MAX_BYTES))
        he=root/"he.metta";he.write_text('!(import! &self store)\n!(assertEqual (store:value-byte-limit) 8388608)\n')
        process=subprocess.run([str(args.cli.resolve()),"--lang","he","--value-bytes",str(8*MAX_BYTES),"--run-contract",str(he)],capture_output=True,text=True)
        assert process.returncode==0,(process.returncode,process.stderr)
        results.append(dict(language="he",exit_code=0))
    report=dict(results=results,scope="Policy getter/spelling only; persistent Space operation parity across all languages is not claimed.")
    if args.output:args.output.write_text(json.dumps(report,indent=2)+"\n")
    print(json.dumps(report,indent=2))


if __name__=="__main__":main()
