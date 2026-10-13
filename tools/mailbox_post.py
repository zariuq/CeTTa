"""Publish a text file or stdin to a configured logical mailbox directory."""
import argparse
import json
import getpass
from pathlib import Path
import sys
import uuid
if __package__:
    from .mailbox_bridge import deliver
else:
    from mailbox_bridge import deliver


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config",required=True)
    parser.add_argument("--id",help="stable occurrence ID for an intentional retry")
    parser.add_argument("--correlation",help="conversation or reply identity; defaults to occurrence ID")
    parser.add_argument("--body-file",help="UTF-8 file; omitted means stdin")
    parser.add_argument("--from",dest="from_name",help="sender name; defaults to configured identity or Unix account")
    parser.add_argument("--reply-to",help="configured mailbox for replies; defaults to the sender's mailbox when present")
    parser.add_argument("--in-reply-to",help="message being answered")
    parser.add_argument("name",help="logical name declared by the operator")
    args=parser.parse_args()
    config=json.loads(Path(args.config).read_text())
    bindings=[item for item in config["bindings"] if item["name"]==args.name]
    if len(bindings)!=1: parser.error("mailbox name has no unique directory binding")
    binding=bindings[0]
    identity=args.id or uuid.uuid4().hex
    origin=args.from_name or config.get("identity") or getpass.getuser()
    return_address=args.reply_to or config.get("return_address")
    if return_address is None and any(b["name"]==origin for b in config["bindings"]):
        return_address=origin
    if return_address is not None and sum(b["name"]==return_address for b in config["bindings"])!=1:
        parser.error("reply address has no unique directory binding")
    headers={"from":origin,"return_address":return_address,"in_reply_to":args.in_reply_to}
    correlation=args.correlation or args.in_reply_to or identity
    maximum=config.get("max_file_bytes",256*1024)
    if type(maximum) is not int or maximum<1: parser.error("positive mailbox file byte budget required")
    if args.body_file:
        with open(args.body_file,encoding="utf-8") as source: body=source.read(maximum+1)
    else:
        body=sys.stdin.read(maximum+1)
    # Check the same encoded file budget the watcher uses before publishing.
    envelope={"id":identity,"correlation":correlation,"sender":binding["sender"],"body":body,"headers":headers}
    if len((json.dumps(envelope,ensure_ascii=False)+"\n").encode())>maximum:
        parser.error("mailbox envelope exceeds its configured byte budget")
    try:
        path=deliver(binding["directory"],identity,correlation,binding["sender"],body,headers=headers)
    except (OSError,ValueError) as error:
        print(json.dumps({"state":"unknown" if getattr(error,"published",False) else "failed",
                          "message":identity,"reason":str(error)},ensure_ascii=False))
        return 1
    print(json.dumps({"state":"published","message":identity,"from":origin,"to":args.name,
                      "return_address":return_address,"path":str(path)},ensure_ascii=False))


if __name__=="__main__": sys.exit(main())
