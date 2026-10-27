"""Exercise the display-SD WebDAV share using only a reserved test folder."""
import argparse,hashlib,http.client,json,socket,time,urllib.parse,xml.etree.ElementTree as ET
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--host',default='192.168.0.34');p.add_argument('--port',type=int,default=47820)
p.add_argument('--large',type=Path,help='Also stream this local file into the test folder, then verify its SHA-256')
a=p.parse_args();root='/sd/000-00-serial-webdav';events=[]

def request(method,path,body=None,headers=None,status=None):
 c=http.client.HTTPConnection(a.host,a.port,timeout=30)
 started=time.monotonic()
 try:
  c.request(method,path,body=body,headers=headers or {});r=c.getresponse();data=r.read();result=(r.status,dict(r.getheaders()),data)
 finally:c.close()
 events.append({'method':method,'path':path,'status':result[0],'seconds':round(time.monotonic()-started,3)})
 if status is not None:assert result[0] in ([status] if isinstance(status,int) else status),(method,path,result[0],data[:300])
 return result

def propfind(path,depth='1',body=None):
 _,_,body=request('PROPFIND',path,body,{'Depth':depth,'Content-Type':'application/xml'},207)
 return ET.fromstring(body)

def contents(path):return [e.text for e in propfind(path).findall('{DAV:}response/{DAV:}href')]

try:
 request('OPTIONS','/sd/',status=200)
 # Clear only this harness's explicitly reserved fixture directory.
 request('DELETE',root,status=[204,404])
 before=set(contents('/sd/'))
 request('MKCOL',root,status=201)
 name=root+'/part%20%26%20one.nc';payload=b'G21\nG90\nG4P0.1\nM2\n'
 request('PUT',name,payload,status=201)
 assert request('GET',name,status=200)[2]==payload
 status,headers,body=request('HEAD',name,status=200);assert not body and int(headers['Content-Length'])==len(payload)
 tag=headers['ETag']
 assert request('GET',name,headers={'Range':'bytes=2-8'},status=206)[2]==payload[2:9]
 assert request('GET',name,headers={'Range':'bytes=-3'},status=206)[2]==payload[-3:]
 request('GET',name,headers={'Range':'bytes=999999-'},status=416)
 request('GET',name,headers={'If-None-Match':tag},status=304)
 request('PUT',name,b'new',{'If-None-Match':'*'},412)
 request('PUT',name,b'G21\nG90\nG4P0.2\nM2\n',{'If-Match':tag},204)
 request('PUT',name,b'wrong',{'If-Match':tag},412)
 assert b'0.2' in request('GET',name,status=200)[2]
 # Interrupted overwrite must not replace the complete original.
 old=request('GET',name,status=200)[2]
 with socket.create_connection((a.host,a.port),timeout=5) as s:
  s.sendall(f'PUT {name} HTTP/1.1\r\nHost: {a.host}:{a.port}\r\nContent-Length: 100000\r\nConnection: close\r\n\r\n'.encode()+b'incomplete')
 time.sleep(.5)
 assert request('GET',name,status=200)[2]==old
 names=contents(root+'/');assert name in names,names
 selected=b'<d:propfind xmlns:d="DAV:" xmlns:z="urn:schemas-microsoft-com:"><d:prop><d:getcontentlength/><z:Win32FileAttributes/><d:unknown/></d:prop></d:propfind>'
 xml=propfind(name,'0',selected)
 assert xml.find('.//{DAV:}getcontentlength').text==str(len(old))
 assert any('404' in e.text for e in xml.findall('.//{DAV:}status'))
 request('PROPFIND',root,headers={'Depth':'infinity'},status=403)
 request('PROPFIND',root,b'<!DOCTYPE x><propfind/>',{'Depth':'0'},400)
 request('PROPFIND',root,b'<propfind><allprop/>',{'Depth':'0'},400)
 lock_body=b'<d:lockinfo xmlns:d="DAV:"><d:lockscope><d:exclusive/></d:lockscope><d:locktype><d:write/></d:locktype></d:lockinfo>'
 _,lock_headers,_=request('LOCK',name,lock_body,{'Depth':'0','Timeout':'Second-60'},200)
 token=lock_headers['Lock-Token'];condition={'If':'('+token+')'}
 request('PUT',name,old,status=423)
 request('DELETE',name,status=423)
 request('LOCK',name,lock_body,{'Depth':'0'},423)
 request('PUT',name,old,condition,204)
 request('PUT',name,b'wrong',{'If':'(Not '+token+')'},412)
 request('PUT',name,b'wrong',{'If':'(Not <opaquelocktoken:missing>)'},423)
 request('LOCK',name,b'',dict(condition,Timeout='Second-120'),200)
 request('PUT',name,old,{'If':f'<http://{a.host}:{a.port}{name}> ('+token+')'},204)
 locked_xml=propfind(name,'0');assert locked_xml.find('.//{DAV:}locktoken/{DAV:}href').text==token[1:-1]
 request('UNLOCK',name,headers={'Lock-Token':'<opaquelocktoken:wrong>'},status=409)
 request('UNLOCK',name,headers={'Lock-Token':token},status=204)
 new_lock=root+'/locked-empty.nc'
 _,new_headers,_=request('LOCK',new_lock,lock_body,{'Depth':'0'},201)
 request('UNLOCK',new_lock,headers={'Lock-Token':new_headers['Lock-Token']},status=204)
 assert request('GET',new_lock,status=200)[2]==b''
 request('DELETE',new_lock,status=204)
 assert set(e.text for e in propfind('/', '1').findall('{DAV:}response/{DAV:}href'))=={'/','/sd/'}
 request('PROPPATCH',name,b'<d:propertyupdate xmlns:d="DAV:"><d:set><d:prop><d:displayname>changed</d:displayname></d:prop></d:set></d:propertyupdate>',{'Content-Type':'application/xml'},207)
 for bad in ['/sd/../outside','/sd/%2e%2e/outside','/sd/a%2fb','/sd/%252e%252e','/sd/.panel-previews/','/sd/.PANEL-WEBDAV/journal','/sd/a%00.nc']:
  request('PUT',bad,b'no',status=403)
 request('DELETE','/sd/',status=403)
 request('PUT',root+'/missing/part.nc',b'no',status=409)
 request('PUT',root+'/origin.nc',b'no',{'Origin':'https://example.com'},403)
 copied=root+'/copy.nc';moved=root+'/renamed.nc'
 request('COPY',name,headers={'Destination':f'http://{a.host}:{a.port}{copied}','Overwrite':'F'},status=201)
 request('COPY',name,headers={'Destination':copied,'Overwrite':'F'},status=412)
 request('MOVE',copied,headers={'Destination':moved},status=201)
 assert request('GET',moved,status=200)[2]==old
 request('MOVE',moved,headers={'Destination':'http://elsewhere.invalid/sd/file.nc'},status=502)
 folder=root+'/folder';request('MKCOL',folder,status=201);request('PUT',folder+'/nested.nc',payload,status=201)
 request('COPY',folder,headers={'Destination':root+'/folder-copy'},status=201)
 assert request('GET',root+'/folder-copy/nested.nc',status=200)[2]==payload
 request('DELETE',root+'/folder-copy',status=204)
 request('MOVE',folder,headers={'Destination':folder+'/inside'},status=403)
 request('PUT',root+'/empty.txt',b'',status=201);assert request('GET',root+'/empty.txt',status=200)[2]==b''
 if a.large:
  size=a.large.stat().st_size;started=time.monotonic();digest=hashlib.sha256()
  c=http.client.HTTPConnection(a.host,a.port,timeout=45)
  c.putrequest('PUT',root+'/large.nc');c.putheader('Content-Length',str(size));c.endheaders()
  with a.large.open('rb') as file:
   while block:=file.read(65536):c.send(block);digest.update(block)
  r=c.getresponse();response=r.read();assert r.status==201,(r.status,response);c.close()
  upload=time.monotonic()-started;print(f'Uploaded {size:,} bytes in {upload:.1f}s; checking download hash',flush=True);started=time.monotonic()
  c=http.client.HTTPConnection(a.host,a.port,timeout=45);c.request('GET',root+'/large.nc');r=c.getresponse();assert r.status==200
  received=hashlib.sha256();count=0
  while block:=r.read(65536):received.update(block);count+=len(block)
  c.close();assert count==size and digest.digest()==received.digest()
  events.append({'large_bytes':size,'upload_seconds':round(upload,2),'download_seconds':round(time.monotonic()-started,2),'sha256':digest.hexdigest()})
  print('LARGE',json.dumps(events[-1]),flush=True)
 request('DELETE',root,status=204);assert set(contents('/sd/'))==before
 print('PASS WebDAV round trips, conditions, interrupted overwrite, properties, folders and invalid paths',flush=True)
finally:
 try:request('DELETE',root,status=[204,404])
 except Exception as e:print('Cleanup failed:',e,flush=True)
 Path('captures/webdav-http-results.json').write_text(json.dumps(events,indent=2))
