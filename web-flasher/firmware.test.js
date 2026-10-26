import test from 'node:test';
import assert from 'node:assert/strict';
import {flashParts,verifiedBytes} from './firmware.js';
const good=()=>({chip:'ESP32-P4',board:'viewesmart-7-1024x600-p4-c6',parts:[['bootloader.bin',0x2000],['partition-table.bin',0x8000],['grblhal-panel.bin',0x10000]].map(([path,offset])=>({path,offset,size:100,sha256:'a'.repeat(64)}))});
test('first install and app-only update exclude saved storage',()=>{
 assert.deepEqual(flashParts(good(),false).map(p=>p.offset),[0x2000,0x8000,0x10000]);
 assert.deepEqual(flashParts(good(),true).map(p=>p.offset),[0x10000]);
});
test('reject wrong board, missing files and NVS overlap',()=>{
 let m=good();m.chip='ESP32-S3';assert.throws(()=>flashParts(m,false));
 m=good();m.parts.pop();assert.throws(()=>flashParts(m,false));
 m=good();m.parts[1].size=0x2000;assert.throws(()=>flashParts(m,false));
 m=good();m.parts[2].path='../app.bin';assert.throws(()=>flashParts(m,false));
});
test('download length and SHA-256 must match before flash',async()=>{
 const bytes=new TextEncoder().encode('abc');
 const p={path:'test',size:3,sha256:'ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad'};
 assert.deepEqual(await verifiedBytes(p,bytes.buffer),bytes);
 await assert.rejects(verifiedBytes({...p,size:4},bytes.buffer));
 await assert.rejects(verifiedBytes({...p,sha256:'0'.repeat(64)},bytes.buffer));
});
