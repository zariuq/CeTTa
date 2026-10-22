#!/usr/bin/env python3
"""Exercise embedding batching through the actual HTTP event boundary.

The default provider is a deterministic local fixture. --live-url additionally
checks a configured Qwen-compatible service with synthetic, nonpersistent data.
No model is loaded and no collection or memory is modified.
"""
import argparse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import subprocess
import threading

ROOT=Path(__file__).resolve().parents[2]

HELPERS=r'''
!(import! &self proc)
(= (embedding-http:await $id $timer)
   (case (io:poll)
     (((io:event $actual $response)
        (if (== $actual $id) $response (Embedding:Fault unexpected-event)))
      ((io:idle)
        (case (proc:wait $timer 5)
          ((proc:pending (embedding-http:await $id $timer))
           ($other (Embedding:Fault wait-process)))))
      ($other (Embedding:Fault poll)))))
(= (embedding-http:exchange $request)
   (case (embedding:submit $request)
     (((Embedding:Pending $saved (io:pending $id))
        (let* (($timer (proc:spawn ("sleep" "infinity") ((lifetime owner))))
               ($response (embedding-http:await $id $timer))
               ($cancelled (proc:cancel $timer)) ($closed (proc:close $timer)))
          (embedding:accept-response $saved $response)))
      ($other $other))))
(= (embedding-http:run $profile)
   (let* (($documents
            ("Paris is the capital of France."
             "A banana is a yellow fruit."
             "The Moon orbits Earth."
             "Addition combines two numbers."))
          ($query ("What is the capital of France?"))
          ($batch (embedding-http:exchange
            (embedding:request $profile "document" $documents ())))
          ($question (embedding-http:exchange
            (embedding:request $profile "query" $query ())))
          ($index (embedding:index $batch))
          ($ranked (embedding:rank-cosine $index $question all 4))
          ($matches (embedding:matches $ranked (france fruit moon arithmetic)))
          ($closed (embedding:index-close $index)))
     $matches))
'''


class Provider(BaseHTTPRequestHandler):
    observations=[]

    def log_message(self,*args):
        pass

    def do_POST(self):
        request=json.loads(self.rfile.read(int(self.headers['Content-Length'])))
        assert request['kind'] in ('document','query')
        self.observations.append(request)
        payload={'protocol':'fixture-v1','dim':3}
        if request['kind']=='document':
            payload['embeddings']=[[1,0,0],[0,1,0],[0,0,1],[-1,0,0]]
        else:
            payload['embeddings']=[[1,0,0]]
        if self.path=='/indices':
            payload['indices']=list(range(len(payload['embeddings'])))
        elif self.path=='/bad-indices':
            payload['indices']=[1]*len(payload['embeddings'])
        elif self.path=='/bad-protocol':
            payload['protocol']='other'
        elif self.path=='/bad-finite':
            payload['embeddings'][0][0]=None
        body=json.dumps(payload).encode()
        self.send_response(503 if self.path=='/status' else 200)
        self.send_header('Content-Type','application/json')
        self.send_header('Content-Length',str(len(body)))
        self.end_headers()
        self.wfile.write(body)


def run(binary,language,source):
    library='(library embedding)' if language.startswith('petta') else 'embedding'
    source=f'!(import! &self {library})\n'+HELPERS+source
    result=subprocess.run([str(binary),'--lang',language,'-e',source],cwd=ROOT,
                          text=True,capture_output=True,timeout=180)
    assert result.returncode==0,(language,result.stdout,result.stderr)
    return result.stdout.splitlines(),result.stderr


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cetta',type=Path,required=True)
    parser.add_argument('--live-url')
    parser.add_argument('--protocol')
    parser.add_argument('--dimension',type=int)
    parser.add_argument('--model')
    args=parser.parse_args()
    binary=args.cetta.resolve()
    server=ThreadingHTTPServer(('127.0.0.1',0),Provider)
    thread=threading.Thread(target=server.serve_forever,daemon=True)
    thread.start()
    checks=0
    try:
        for language in ('petta@1.0.6','prime'):
            url=f'http://127.0.0.1:{server.server_port}'
            profile=f'(EmbeddingProfile "fixture" "fixture" "fixture-v1" 3 "fixture-text-v1" {json.dumps(url+"/indices")})'
            source=f'!(embedding-http:run {profile})\n'
            lines,stderr=run(binary,language,source)
            wanted='((Embedding:Match 0 france 1.0) (Embedding:Match 1 fruit 0.0) (Embedding:Match 2 moon 0.0) (Embedding:Match 3 arithmetic -1.0))'
            if language=='prime':
                wanted='['+wanted+']'
            assert lines[-1:]==[wanted],(language,lines,stderr)
            checks+=1
            for suffix,expected in (
                ('bad-indices','(Embedding:Fault response-indices)'),
                ('bad-protocol','(Embedding:Fault incompatible-profile)'),
                ('bad-finite','(Embedding:Fault numeric-response typed-json)'),
                ('status','(Embedding:Fault http-status 503)'),
            ):
                profile=f'(EmbeddingProfile "fixture" "fixture" "fixture-v1" 3 "fixture-text-v1" {json.dumps(url+"/"+suffix)})'
                source=f'!(embedding-http:exchange (embedding:request {profile} "query" ("question") ()))\n'
                lines,stderr=run(binary,language,source)
                wanted=expected if language.startswith('petta') else '['+expected+']'
                assert lines[-1:]==[wanted],(language,suffix,lines,stderr)
                checks+=1
        kinds=[row['kind'] for row in Provider.observations]
        assert kinds.count('document')==2 and kinds.count('query')==10,kinds
        checks+=1
    finally:
        server.shutdown()
        server.server_close()
        thread.join()
    print(f'EmbeddingHTTP {checks} 0')
    if args.live_url:
        if not args.protocol or not args.dimension or not args.model:
            parser.error('a live service requires protocol, dimension and configured model identity')
        profile='(EmbeddingProfile '+ ' '.join(json.dumps(v) for v in
            ('live-fixture',args.model,args.protocol))+f' {args.dimension} "synthetic-four-doc-v1" '+json.dumps(args.live_url)+')'
        for language in ('petta@1.0.6','prime'):
            lines,stderr=run(binary,language,f'!(embedding-http:run {profile})\n')
            assert lines and 'Embedding:Match 0 france' in lines[-1],(language,lines,stderr)
            assert all(f'Embedding:Match {i} ' in lines[-1] for i in range(4)),(language,lines)
            print(f'EmbeddingLive {language} {lines[-1]}')
        print('Synthetic retrieval only; no premise-recall improvement is claimed.')


if __name__=='__main__':
    main()
