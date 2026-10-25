"""LAN preview worker. Run: python server.py --bind <PC-LAN-IP> --port 47821."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import ipaddress
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import re
import threading
import time
from renderer import render, VERSION

MAX_BYTES = 512*1024*1024
KEY = re.compile(r'^/v2/jobs/([a-f0-9]{64})(?:/(manifest\.json|(?:top|bottom|front|back|left|right|isometric|perspective)\.(?:png|bin)))?$')

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bind',default='127.0.0.1')
    parser.add_argument('--port',type=int,default=47821)
    parser.add_argument('--allow-client',action='append',default=[],type=lambda value:str(ipaddress.ip_address(value)),help='Permit this display IP; may be repeated')
    parser.add_argument('--cache',type=Path,default=Path(__file__).parent/'cache')
    args=parser.parse_args();args.cache=args.cache/'v2';args.cache.mkdir(parents=True,exist_ok=True)
    allowed={'127.0.0.1',args.bind,*args.allow_client}
    executor=ThreadPoolExecutor(max_workers=1)
    busy=set();progress={};lock=threading.Lock()
    def generate(key, source):
        target=args.cache/key
        started=time.monotonic()
        try:
            def update(**fields):
                with lock: progress[key]={**fields,'elapsed':int(time.monotonic()-started)}
            meta=render(source,target,progress=update)
            meta.update(status='ready',sha256=key,seconds=round(time.monotonic()-started,2))
        except Exception as e:
            meta={'status':'error','sha256':key,'error':str(e)[:240],'renderer':VERSION}
        try:
            tmp=target/'manifest.tmp';tmp.write_text(json.dumps(meta),encoding='utf8');tmp.replace(target/'manifest.json')
        finally:
            try:
                source.unlink(missing_ok=True)
                (target/'geometry.f32').unlink(missing_ok=True)
            finally:
                with lock:
                    busy.discard(key)
                    progress.pop(key,None)
        print(key,meta,flush=True)
    class Handler(BaseHTTPRequestHandler):
        protocol_version='HTTP/1.1'
        def respond(self,code,body,kind='application/json'):
            if not isinstance(body,bytes): body=json.dumps(body).encode()
            self.send_response(code);self.send_header('Content-Type',kind);self.send_header('Content-Length',str(len(body)));self.send_header('Connection','close');self.end_headers()
            self.close_connection=True
            if self.command!='HEAD': self.wfile.write(body)
        def do_HEAD(self): self.do_GET()
        def do_GET(self):
            if self.client_address[0] not in allowed:return self.respond(403,{'error':'Client not allowed'})
            if self.path=='/health': return self.respond(200,{'status':'ok','renderer':VERSION})
            match=KEY.fullmatch(self.path)
            if not match or not match[2]: return self.respond(404,{'error':'Not found'})
            key,name=match.groups()
            with lock:
                pending=key in busy
                snapshot=dict(progress.get(key,{}))
            if pending: return self.respond(202,{'status':'pending',**snapshot})
            path=args.cache/key/name
            if not path.is_file(): return self.respond(404,{'error':'Not found'})
            return self.respond(200,path.read_bytes(),'image/png' if name.endswith('.png') else 'application/octet-stream' if name.endswith('.bin') else 'application/json')
        def do_POST(self):
            if self.client_address[0] not in allowed:return self.respond(403,{'error':'Client not allowed'})
            match=KEY.fullmatch(self.path)
            if not match or match[2]: return self.respond(404,{'error':'Not found'})
            try: length=int(self.headers.get('Content-Length','0'))
            except ValueError: return self.respond(400,{'error':'Content-Length required'})
            if not 0<length<=MAX_BYTES: return self.respond(413,{'error':'File must be 1 byte to 512 MiB'})
            key=match[1]
            with lock:
                if busy: return self.respond(409,{'error':'Renderer busy; retry while idle'})
                busy.add(key)
            target=args.cache/key
            source=target/'source.upload'
            try:
                target.mkdir(exist_ok=True)
                self.connection.settimeout(30)
                remaining=length;digest=hashlib.sha256()
                with source.open('wb') as out:
                    while remaining:
                        data=self.rfile.read(min(65536,remaining))
                        if not data: raise ValueError('Truncated upload')
                        out.write(data);digest.update(data);remaining-=len(data)
                if digest.hexdigest()!=key: raise ValueError('SHA256 did not match uploaded content')
                executor.submit(generate,key,source)
                self.respond(202,{'status':'pending','sha256':key})
            except Exception as e:
                source.unlink(missing_ok=True)
                with lock: busy.discard(key)
                self.respond(400,{'error':str(e)})
    server=ThreadingHTTPServer((args.bind,args.port),Handler)
    server.daemon_threads=True
    print(f'Preview server http://{args.bind}:{args.port} / renderer {VERSION}',flush=True)
    try: server.serve_forever()
    finally: server.server_close();executor.shutdown()
if __name__=='__main__': main()
