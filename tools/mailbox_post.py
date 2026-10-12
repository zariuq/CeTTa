"""Publish a text file or stdin to a configured logical mailbox directory."""
import argparse
import json
from pathlib import Path
import sys
import uuid
from mailbox_bridge import deliver


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config",required=True)
    parser.add_argument("--id",help="stable occurrence ID for an intentional retry")
    parser.add_argument("--correlation",help="conversation or reply identity; defaults to occurrence ID")
    parser.add_argument("--body-file",help="UTF-8 file; omitted means stdin")
    parser.add_argument("name",help="logical name declared by the operator")
    args=parser.parse_args()
    config=json.loads(Path(args.config).read_text())
    bindings=[item for item in config["bindings"] if item["name"]==args.name]
    if len(bindings)!=1: parser.error("mailbox name has no unique directory binding")
    binding=bindings[0]
    identity=args.id or uuid.uuid4().hex
    maximum=config.get("max_file_bytes",256*1024)
    if type(maximum) is not int or maximum<1: parser.error("positive mailbox file byte budget required")
    if args.body_file:
        with open(args.body_file,encoding="utf-8") as source: body=source.read(maximum+1)
    else:
        body=sys.stdin.read(maximum+1)
    # Check the same encoded file budget the watcher uses before publishing.
    envelope={"id":identity,"correlation":args.correlation or identity,"sender":binding["sender"],"body":body}
    if len((json.dumps(envelope,ensure_ascii=False)+"\n").encode())>maximum:
        parser.error("mailbox envelope exceeds its configured byte budget")
    path=deliver(binding["directory"],identity,args.correlation or identity,binding["sender"],body)
    print(json.dumps({"state":"published","id":identity,"path":str(path)},ensure_ascii=False))


if __name__=="__main__": main()
