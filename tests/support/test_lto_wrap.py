"""Failure injection remains active with fat LTO objects and wrapping links."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[2]


def run(command,cwd,env=None):
    result=subprocess.run(list(map(str,command)),cwd=cwd,env=env,capture_output=True,text=True)
    assert result.returncode==0,(command,result.returncode,result.stdout,result.stderr)
    return result


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--cesta',type=Path,required=True)
    parser.add_argument('--cc',default='gcc')
    parser.add_argument('--output',type=Path,default=ROOT/'runtime/lto-wrap-controls')
    args=parser.parse_args();args.output.mkdir(parents=True,exist_ok=True)
    macros=subprocess.run([args.cc,'-dM','-E','-x','c','/dev/null'],capture_output=True,text=True,check=True).stdout
    if '__GNUC__' not in macros or '__clang__' in macros:
        print('SKIP: fat-object LTO wrapping controls apply to the GCC performance profile')
        return
    work=Path(tempfile.mkdtemp(prefix='wrap-',dir=args.output.resolve()))
    files={
        'callee.c':'int boundary(int n){return n+1;}\n',
        'caller.c':'int boundary(int);int invoke(void){return boundary(2);}\n',
        'test.c':'int invoke(void);int __real_boundary(int);static int calls;int __wrap_boundary(int n){++calls;return __real_boundary(n)+100;}int main(void){return invoke()!=103 || calls!=1;}\n',
        'ordinary.c':'int invoke(void);int main(void){return invoke()!=3;}\n',
    }
    for name,text in files.items():(work/name).write_text(text)
    objects=[]
    for name in ('callee','caller','test','ordinary'):
        obj=name+'.o';run([args.cc,'-std=c11','-O3','-flto=auto','-ffat-lto-objects','-c',name+'.c','-o',obj],work)
        objects.append(obj)
    run([args.cc,'-O3','-flto=auto','callee.o','caller.o','ordinary.o','-o','ordinary'],work)
    run([work/'ordinary'],work)
    env=dict(os.environ,CESTA=str(args.cesta.resolve()),CESTA_NATIVE_CC=args.cc,CETTA_LTO='1')
    forms=[['-Wl,--wrap=boundary'],['-Xlinker','--wrap','-Xlinker','boundary'],['@outer.rsp']]
    (work/'inner.rsp').write_text('-Wl,--wrap=boundary\n')
    (work/'outer.rsp').write_text('@inner.rsp\n')
    rows=[]
    for native in (False,True):
        for index,flags in enumerate(forms):
            output=work/f'test-{native}-{index}'
            command=[ROOT/'tools/cesta_cc.sh',*(['--native'] if native else []),'-std=c11','-O3','-flto=auto',
                     'callee.o','caller.o','test.o',*flags,'-o',output]
            run(command,work,env);run([output],work,env)
            rows.append(dict(native=native,form=index,passed=True))
    (work/'summary.json').write_text(json.dumps(dict(wrapping_links=rows,ordinary_lto=True),indent=2)+'\n')
    print('PASS: six native/Cesta wrapping links retain injection; ordinary LTO remains enabled')


if __name__=='__main__':main()
