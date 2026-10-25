"""Bounded G-code geometry parser and fixed-view PNG renderer."""
from pathlib import Path
import math
import struct
import re
import numpy as np
from PIL import Image, ImageDraw

VERSION = 2
VIEWS = ('top', 'bottom', 'front', 'back', 'left', 'right', 'isometric', 'perspective')
WIDTH, HEIGHT = 740, 328
WORD = re.compile(r'([A-Z])([+-]?(?:\d+(?:\.\d*)?|\.\d+))')

class PreviewError(ValueError):
    pass

def geometry(source, target, progress=None):
    source_size = Path(source).stat().st_size
    source_bytes = 0
    if progress: progress(phase="parsing", percent=0)
    pos = [0.,0.,0.]
    low, high = [math.inf]*3, [-math.inf]*3
    absolute, mm, plane, motion, center_absolute = True, True, 0, 0, False
    spindle, power, maximum_power, wcs = False, 1., 0., None
    points, lines, chunk = 0, 0, []
    last = tuple(pos)
    def emit(end, kind):
        nonlocal pos, low, high, points, chunk, last
        if any(not math.isfinite(v) or abs(v)>1e6 for v in end):
            raise PreviewError('Coordinate outside preview range')
        if sum((end[i]-last[i])**2 for i in range(3)) > 1e-16:
            chunk.append((*last, *end, kind))
            for i in range(3):
                low[i]=min(low[i],last[i],end[i]);high[i]=max(high[i],last[i],end[i])
            last = tuple(end)
            points += 1
            if points > 15000000: raise PreviewError('Preview exceeds 15 million segments')
            if len(chunk) >= 16384:
                np.asarray(chunk, dtype='<f4').tofile(out); chunk.clear()
    with open(source, 'r', encoding='ascii', errors='strict', newline='') as inp, open(target, 'wb') as out:
        for lines, raw in enumerate(inp, 1):
            source_bytes += len(raw)
            if progress and lines % 16384 == 0:
                progress(phase="parsing", percent=min(100, source_bytes*100//max(1,source_size)))
            if len(raw) > 4096: raise PreviewError(f'Line {lines}: line too long')
            line = raw.split(';',1)[0].upper().strip()
            line = re.sub(r'\([^()]*\)', '', line)
            if not line or line == '%': continue
            if any(c in line for c in '#[]()!~?$'):
                raise PreviewError(f'Line {lines}: unsupported expression or command')
            words = WORD.findall(line)
            if re.sub(r'\s+', '', WORD.sub('',line)):
                raise PreviewError(f'Line {lines}: malformed G-code')
            data = {}
            for letter, value in words:
                v = float(value)
                if not math.isfinite(v): raise PreviewError('Non-finite word')
                if letter not in 'GMN' and letter in data: raise PreviewError(f'Line {lines}: duplicate {letter}')
                if letter not in 'GMN': data[letter] = v
                if letter == 'G':
                    if v in (0,1,2,3): motion = int(v)
                    elif v in (17,18,19): plane = int(v)-17
                    elif v == 20: mm = False
                    elif v == 21: mm = True
                    elif v == 90: absolute = True
                    elif v == 91: absolute = False
                    elif v == 90.1: center_absolute = True
                    elif v == 91.1: center_absolute = False
                    elif v in (54,55,56,57,58,59):
                        if wcs is not None and wcs != v: raise PreviewError('Multiple work coordinate systems need controller offsets')
                        wcs = v
                    elif v not in (4,40,49,61,64,80,93,94,95):
                        raise PreviewError(f'Line {lines}: G{v:g} is not supported for preview')
                elif letter == 'M':
                    if v in (98,99,6): raise PreviewError(f'Line {lines}: subroutine/tool change preview unsupported')
                    if v in (3,4): spindle = True
                    if v == 5: spindle = False
            if any(a in data for a in 'ABCUVW'): raise PreviewError('Rotary/additional-axis geometry is not supported')
            if 'S' in data: power = data['S']; maximum_power = max(maximum_power,power)
            if not any(a in data for a in 'XYZIJKR'):
                if any(l == 'M' and float(v) in (2,30) for l,v in words): break
                continue
            scale = 1. if mm else 25.4
            end = pos.copy()
            for i,a in enumerate('XYZ'):
                if a in data: end[i] = data[a]*scale + (0 if absolute else pos[i])
            kind = -1. if motion == 0 else power
            if motion in (0,1): emit(end,kind)
            else:
                pos=np.asarray(pos);end=np.asarray(end)
                u,v,w = ((0,1,2),(2,0,1),(1,2,0))[plane]
                offsets = np.array([data.get(a,0)*scale for a in 'IJK'])
                if 'R' in data:
                    radius = data['R']*scale
                    delta = end[[u,v]] - pos[[u,v]]; chord = np.linalg.norm(delta)
                    if chord <= 1e-9 or chord > 2*abs(radius)+.001: raise PreviewError('Invalid radius arc')
                    perpendicular = np.array([-delta[1],delta[0]])/chord
                    sign = (-1 if motion == 2 else 1)*(-1 if radius < 0 else 1)
                    center = (end[[u,v]]+pos[[u,v]])/2 + perpendicular*math.sqrt(max(0,radius*radius-chord*chord/4))*sign
                else:
                    center = offsets[[u,v]] if center_absolute else pos[[u,v]]+offsets[[u,v]]
                radius = np.linalg.norm(pos[[u,v]]-center)
                if radius <= 1e-9 or abs(np.linalg.norm(end[[u,v]]-center)-radius) > max(.01,radius*.001):
                    raise PreviewError(f'Line {lines}: inconsistent arc radii')
                start = math.atan2(pos[v]-center[1],pos[u]-center[0])
                stop = math.atan2(end[v]-center[1],end[u]-center[0]); sweep = stop-start
                if motion == 2 and sweep >= 0: sweep -= 2*math.pi
                if motion == 3 and sweep <= 0: sweep += 2*math.pi
                step = min(.1,2*math.acos(max(-1.,1-min(.05/radius,2.))))
                count = max(1,math.ceil(abs(sweep)/max(step,1e-6)))
                if count > 100000: raise PreviewError('Arc requires too many preview segments')
                origin = pos.copy()
                for i in range(1,count+1):
                    point = origin+(end-origin)*(i/count)
                    point[u] = center[0]+radius*math.cos(start+sweep*i/count)
                    point[v] = center[1]+radius*math.sin(start+sweep*i/count)
                    emit(end if i == count else point,kind)
            pos = list(end)
            if any(l == 'M' and float(v) in (2,30) for l,v in words): break
        if chunk: np.asarray(chunk,dtype='<f4').tofile(out)
    if points == 0: raise PreviewError('No supported toolpath')
    return {'segments':points,'lines':lines,'minimum':low,'maximum':high,'maximum_power':maximum_power}

def render(source, directory, progress=None):
    directory = Path(directory); directory.mkdir(parents=True, exist_ok=True)
    geometry_path = directory/'geometry.f32'
    meta = geometry(source,geometry_path,progress)
    segments = np.memmap(geometry_path, dtype='<f4',mode='r').reshape(-1,7)
    center = (np.array(meta['minimum'])+meta['maximum'])/2
    matrices = {
      'top':np.array([[1,0,0],[0,1,0]]), 'bottom':np.array([[-1,0,0],[0,1,0]]),
      'front':np.array([[1,0,0],[0,0,1]]), 'back':np.array([[-1,0,0],[0,0,1]]),
      'left':np.array([[0,-1,0],[0,0,1]]), 'right':np.array([[0,1,0],[0,0,1]]),
      'isometric':np.array([[.70710678,-.70710678,0],[.40824829,.40824829,.81649658]]),
      'perspective':np.array([[.70710678,-.70710678,0],[.40824829,.40824829,.81649658]])}
    # Screen-space deduplication bounds drawing work without dropping distinct pixels.
    for view,(name,matrix) in enumerate(matrices.items(),1):
        if progress: progress(phase="rendering", percent=0, view=view, views=len(VIEWS))
        all_low, all_high = np.full(2,np.inf), np.full(2,-np.inf)
        depth_axis=np.array([-.57735027,-.57735027,.57735027]); camera=max(np.linalg.norm(np.array(meta['maximum'])-meta['minimum'])*2,1)
        def project(points):
            pts=points-center; xy=pts@matrix.T
            if name == 'perspective': xy*= (camera/(camera-pts@depth_axis))[:,None]
            return xy
        for i in range(0,len(segments),65536):
            xy=project(np.concatenate((segments[i:i+65536,:3],segments[i:i+65536,3:6])))
            all_low=np.minimum(all_low,xy.min(0));all_high=np.maximum(all_high,xy.max(0))
            if progress: progress(phase="rendering", percent=min(20,(i+65536)*20//len(segments)), view=view, views=len(VIEWS))
        extent=np.maximum(all_high-all_low,1e-6); scale=min((WIDTH-32)/extent[0],(HEIGHT-32)/extent[1]); mid=(all_low+all_high)/2
        image=Image.new('RGB',(WIDTH,HEIGHT),'#ffffff'); draw=ImageDraw.Draw(image)
        seen=set()
        for i in range(0,len(segments),65536):
            batch=segments[i:i+65536]
            a=np.rint((project(batch[:,:3])-mid)*[scale,-scale]+[WIDTH/2,HEIGHT/2]).astype(np.int32)
            b=np.rint((project(batch[:,3:6])-mid)*[scale,-scale]+[WIDTH/2,HEIGHT/2]).astype(np.int32)
            # Quantise laser intensity into 16 bands; CNC constant-S moves retain one colour.
            kinds=np.where(batch[:,6]<0,0,np.where(batch[:,6]==0,1,2+np.minimum(15,(batch[:,6]/max(meta['maximum_power'],1)*15).astype(int))))
            packed=np.column_stack((a,b,kinds)); packed=np.unique(packed,axis=0)
            for x0,y0,x1,y1,k in packed:
                key=(int(x0),int(y0),int(x1),int(y1),int(k))
                if key in seen: continue
                seen.add(key)
                color=(215,220,228) if k<2 else (int(170-(k-2)*9),int(185-(k-2)*8),int(230-(k-2)*3))
                draw.line((int(x0),int(y0),int(x1),int(y1)),fill=color,width=1)
            if len(seen)>2000000: seen.clear()
            if progress: progress(phase="rendering", percent=min(99,20+(i+65536)*79//len(segments)), view=view, views=len(VIEWS))
        image.save(directory/(name+'.png'),optimize=False)
        rgb=np.asarray(image,dtype=np.uint16)
        rgb565=((rgb[:,:,0]>>3)<<11)|((rgb[:,:,1]>>2)<<5)|(rgb[:,:,2]>>3)
        header=struct.pack('<BBHHHHH',0x19,0x12,0,WIDTH,HEIGHT,WIDTH*2,0)
        (directory/(name+'.bin')).write_bytes(header+rgb565.astype('<u2').tobytes())
        if progress: progress(phase="rendering", percent=100, view=view, views=len(VIEWS))
    segments._mmap.close()
    del segments
    geometry_path.unlink()
    return {**meta,'renderer':VERSION,'width':WIDTH,'height':HEIGHT,'views':list(VIEWS)}
