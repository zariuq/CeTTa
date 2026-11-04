#!/usr/bin/env python3
"""Execute a CeTTa notebook with the selected installed Python kernel."""
import argparse
from pathlib import Path

import nbformat
from nbclient import NotebookClient


def check_interruption(kernel_name):
    from jupyter_client import KernelManager

    manager = KernelManager(kernel_name=kernel_name)
    manager.start_kernel()
    client = manager.client()
    client.start_channels()
    try:
        client.wait_for_ready(timeout=30)
        for language in ("petta", "he"):
            source = f'''
from cetta import Engine, Callback
engine = Engine(language={language!r})
engine.run('!(pragma! max-stack-depth 100000000) (= (spin) (spin))')
def entered():
    print('cetta-query-entered', flush=True)
    return ()
engine.register_callback('host:entered', Callback(entered, 0))
query = engine.query('(let $ready (host:entered) (spin))')
next(query)
'''
            request = client.execute(source)
            interrupted = raised = False
            while True:
                message = client.get_iopub_msg(timeout=30)
                if message.get("parent_header", {}).get("msg_id") != request:
                    continue
                kind, content = message["msg_type"], message["content"]
                if kind == "stream" and "cetta-query-entered" in content["text"] and not interrupted:
                    manager.interrupt_kernel()
                    interrupted = True
                if kind == "error":
                    if content["ename"] != "KeyboardInterrupt":
                        raise AssertionError(content)
                    raised = True
                if kind == "status" and content["execution_state"] == "idle":
                    break
            assert interrupted and raised, f"{language}: kernel interruption was not observed"
            request = client.execute(
                "assert query._closed\n"
                "assert engine.call('+', 20, 22).to_python() == [42]\n"
                "engine.close()\n")
            while True:
                message = client.get_iopub_msg(timeout=30)
                if message.get("parent_header", {}).get("msg_id") != request:
                    continue
                if message["msg_type"] == "error":
                    raise AssertionError(message["content"])
                if message["msg_type"] == "status" and message["content"]["execution_state"] == "idle":
                    break
            print(f"CeTTa notebook interruption: {language} query closed and engine recovered")
    finally:
        manager.shutdown_kernel(now=True)
        client.stop_channels()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("notebook", type=Path)
    parser.add_argument("--kernel-name", default="python3")
    parser.add_argument("--check-interruption", action="store_true")
    args = parser.parse_args()
    notebook = nbformat.read(args.notebook, as_version=4)
    nbformat.validate(notebook)
    NotebookClient(notebook, kernel_name=args.kernel_name, timeout=60,
                   resources={"metadata": {"path": str(Path.cwd())}}).execute()
    print("CeTTa Python notebook:", sum(cell.cell_type == "code" for cell in notebook.cells),
          "code cells executed")
    if args.check_interruption:
        check_interruption(args.kernel_name)


if __name__ == "__main__":
    main()
