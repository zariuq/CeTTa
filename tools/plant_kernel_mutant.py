#!/usr/bin/env python3
"""Create a deliberately faulty kernel test input from the active source."""
import argparse
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('kind',choices=['matcher','scope']);p.add_argument('source',type=Path);p.add_argument('out',type=Path)
a=p.parse_args();text=a.source.read_text()
if a.kind=='matcher':
    old='CettaKernelMode kernel=cetta_kernel_requested();'
    if text.count(old)!=3:raise SystemExit('matcher entrypoint inventory changed; review the control')
    text=text.replace(old,'CettaKernelMode kernel=CETTA_KERNEL_C;')
else:
    old='NP_F_LEFT_SCOPE=8'
    if text.count(old)!=1:raise SystemExit('scope layout coordinate changed; review the control')
    text=text.replace(old,'NP_F_LEFT_SCOPE=10')
a.out.parent.mkdir(parents=True,exist_ok=True);a.out.write_text(text)
