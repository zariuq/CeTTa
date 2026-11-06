#!/usr/bin/env python3
"""Exercise multiline submission, Ctrl+C and literal command text over a PTY."""
import argparse
import os
from pathlib import Path
import pty
import select
import signal
import subprocess
import termios
import time

def interactive_signals():
    # A background runner can inherit SIGINT ignored. This child models an
    # interactive terminal, where Ctrl+C must reach the REPL's own handler.
    signal.signal(signal.SIGINT, signal.SIG_DFL)


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('binary',type=Path);p.add_argument('work',type=Path);p.add_argument('--timeout',type=float,default=30)
    a=p.parse_args();a.work.mkdir(parents=True,exist_ok=False)
    master,slave=pty.openpty();settings=termios.tcgetattr(slave);settings[3]&=~termios.ECHO;termios.tcsetattr(slave,termios.TCSANOW,settings)
    proc=subprocess.Popen([str(a.binary.resolve(strict=True)),'--repl'],stdin=slave,stdout=slave,stderr=subprocess.PIPE,start_new_session=True,preexec_fn=interactive_signals)
    os.close(slave);history=bytearray()
    def until(marker):
        chunk=bytearray();deadline=time.monotonic()+a.timeout
        while marker not in chunk:
            remaining=deadline-time.monotonic()
            assert remaining>0,(marker,bytes(chunk))
            ready,_,_=select.select([master],[],[],remaining)
            assert ready,(marker,bytes(chunk))
            data=os.read(master,65536);assert data,(marker,bytes(chunk))
            chunk.extend(data);history.extend(data)
        return bytes(chunk).replace(b'\r\n',b'\n')
    try:
        until(b'cetta> ')
        os.write(master,b'(= (repl:constant) 37)\n');until(b'cetta> ')
        os.write(master,b'!(repl:constant)\n');assert b'37\n' in until(b'cetta> ')
        os.write(master,b'!(+ 1\n');until(b'... ')
        proc.send_signal(signal.SIGINT);until(b'cetta> ')
        os.write(master,b'!(+ 5 6)\n');assert b'11\n' in until(b'cetta> ')
        os.write(master,b'!(println! r##"first\n:cancel\n:quit\nlast"##)\n')
        literal=until(b'cetta> ');assert b'first\n:cancel\n:quit\nlast' in literal,literal
        os.write(master,b':cancel\n');until(b'cetta> ')
        os.write(master,b'!(+ 2 3)\n');assert b'5\n' in until(b'cetta> ')
        os.write(master,b':quit\n');proc.wait(timeout=a.timeout)
        error=proc.stderr.read();assert proc.returncode==0 and not error,(proc.returncode,error)
        (a.work/'transcript').write_bytes(history);(a.work/'stderr').write_bytes(error)
    finally:
        if proc.poll() is None:proc.terminate();proc.wait(timeout=a.timeout)
        (a.work/'transcript').write_bytes(history)
        proc.stderr.close();os.close(master)
    print('(LangReplV1Summary definitions multiline cancel literal-command-text passed)')

if __name__=='__main__':main()
