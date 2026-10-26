"""Download an existing public tester release and verify its binary checksums."""
import argparse,hashlib,json,os,urllib.request
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);a=p.parse_args()
base='https://api.github.com/repos/petervanderwalt/grblhal-panel/releases'
headers={'Accept':'application/vnd.github+json','User-Agent':'panel-installer'}
if os.environ.get('GH_TOKEN'):headers['Authorization']='Bearer '+os.environ['GH_TOKEN']
tag=os.environ.get('RELEASE_TAG','')
url=base+('/tags/'+urllib.parse.quote(tag,safe='') if tag else '')
r=json.load(urllib.request.urlopen(urllib.request.Request(url,headers=headers)))
if isinstance(r,list):r=next(release for release in r if not release['draft'])
a.output.mkdir(parents=True,exist_ok=True)
assets={asset['name']:asset['browser_download_url'] for asset in r['assets']}
for name in ['manifest.json','bootloader.bin','partition-table.bin','grblhal-panel.bin','SHA256SUMS.txt','INSTALL.txt']:
 data=urllib.request.urlopen(assets[name]).read()
 (a.output/name).write_bytes(data)
m=json.loads((a.output/'manifest.json').read_text())
if m['version']!=r['tag_name']:raise ValueError('Release version mismatch')
expected={'bootloader.bin':0x2000,'partition-table.bin':0x8000,'grblhal-panel.bin':0x10000}
if len(m['parts'])!=3:raise ValueError('Incomplete release')
for part in m['parts']:
 if part['path'] not in expected or part['offset']!=expected.pop(part['path']):raise ValueError('Unexpected flash layout')
 data=(a.output/part['path']).read_bytes()
 if len(data)!=part['size'] or hashlib.sha256(data).hexdigest()!=part['sha256']:raise ValueError('Release checksum mismatch')
if expected:raise ValueError('Missing firmware image')
print('Verified release',m['version'],m['commit'])
