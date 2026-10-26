"""Exercise the HTTP contract against a temporary local service."""
import hashlib
import json
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import time
import unittest
import urllib.error
import urllib.request


class ServerTests(unittest.TestCase):
    def test_upload_hash_manifest_and_views(self):
        root=Path(__file__).resolve().parent
        base=root.parent/'captures'/'server-tests'
        base.mkdir(parents=True,exist_ok=True)
        with tempfile.TemporaryDirectory(dir=base) as directory:
            with socket.socket() as sock:
                sock.bind(('127.0.0.1',0));port=sock.getsockname()[1]
            process=subprocess.Popen([sys.executable,str(root/'server.py'),'--port',str(port),'--cache',directory],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
            url=f'http://127.0.0.1:{port}'
            try:
                for _ in range(100):
                    try:
                        with urllib.request.urlopen(url+'/health',timeout=1) as response:
                            self.assertEqual(json.load(response)['renderer'],4)
                        break
                    except urllib.error.URLError: time.sleep(.1)
                else: self.fail('Service did not start')
                code=b'G21G90\nG1X10Y5Z2\nM30\n'
                key=hashlib.sha256(code).hexdigest()
                with self.assertRaises(urllib.error.HTTPError) as wrong:
                    urllib.request.urlopen(urllib.request.Request(url+'/v4/jobs/'+'0'*64,data=code))
                self.assertEqual(wrong.exception.code,400)
                with urllib.request.urlopen(urllib.request.Request(url+'/v4/jobs/'+key,data=code)) as response:
                    self.assertEqual(response.status,202)
                for _ in range(100):
                    with urllib.request.urlopen(url+'/v4/jobs/'+key+'/manifest.json') as response:
                        meta=json.load(response)
                    if meta['status']!='pending':break
                    time.sleep(.1)
                self.assertEqual(meta['status'],'ready')
                self.assertEqual(meta['renderer'],4)
                self.assertEqual(meta['sha256'],key)
                with self.assertRaises(urllib.error.HTTPError) as old:
                    urllib.request.urlopen(url+'/v3/jobs/'+key+'/manifest.json')
                self.assertEqual(old.exception.code,404)
                for view in meta['views']:
                    with urllib.request.urlopen(url+'/v4/jobs/'+key+'/'+view+'.bin') as response:
                        bitmap=response.read()
                    self.assertEqual(len(bitmap),12+740*328*2)
                    self.assertEqual(bitmap[:2],b'\x19\x12')
                    with urllib.request.urlopen(url+'/v4/jobs/'+key+'/'+view+'.png') as response:
                        self.assertEqual(response.read(8),b'\x89PNG\r\n\x1a\n')
                with self.assertRaises(urllib.error.HTTPError) as missing:
                    urllib.request.urlopen(url+'/v4/jobs/../source.upload')
                self.assertEqual(missing.exception.code,404)
            finally:
                process.terminate();process.wait(timeout=10)


if __name__=='__main__':unittest.main()
