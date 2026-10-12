"""Send and durably receive on logical native mailbox channels."""
import argparse
import json
from native_mailbox import Mailbox
from space_service_client import E, Symbol


def json_value(value):
    if isinstance(value,Symbol): return value.name
    if isinstance(value,tuple): return [json_value(x) for x in value]
    return value


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--socket",required=True)
    parser.add_argument("--space",required=True)
    parser.add_argument("--worker",required=True)
    parser.add_argument("operation",choices=("send","wait","read","cancel","settle"))
    parser.add_argument("id")
    parser.add_argument("--channel")
    parser.add_argument("--correlation")
    parser.add_argument("--sender")
    parser.add_argument("--owner")
    parser.add_argument("--generation",type=int,default=1)
    parser.add_argument("--body")
    parser.add_argument("--continuation-tag",default="mailbox-arrival")
    args=parser.parse_args()
    required={"send":("channel","correlation","sender","body"),
              "wait":("channel","correlation","sender","owner"),
              "cancel":("owner",),"settle":("owner",)}
    for name in required.get(args.operation,()):
        if getattr(args,name) is None:
            parser.error("--"+name+" is required for "+args.operation)
    if args.generation<1: parser.error("--generation must be positive")
    box=Mailbox(args.socket,args.space,args.worker)
    try:
        if args.operation=="send":
            value=box.send(args.id,args.channel,args.correlation,args.sender,args.body)
        elif args.operation=="wait":
            box.register(args.id,args.channel,args.correlation,args.sender,args.owner,args.generation,
                         E("MailboxResume",1,args.continuation_tag,args.id))
            value=box.await_result(args.id)
        elif args.operation=="read": value=box.resolve(args.id)
        elif args.operation=="cancel": value=box.cancel(args.id,args.owner,args.generation)
        else: value=box.settle(args.id,args.owner,args.generation)
        print(json.dumps(json_value(value),ensure_ascii=False))
    finally:
        box.close()


if __name__=="__main__": main()
