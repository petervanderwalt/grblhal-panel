export function flashParts(manifest) {
  if (manifest.chip !== 'ESP32-P4' || manifest.board !== 'viewesmart-7-1024x600-p4-c6') throw Error('This release is for a different display.');
  const expected = [['bootloader.bin',0x2000,0x8000],['partition-table.bin',0x8000,0x9000],['grblhal-panel.bin',0x10000,0x810000]];
  if (manifest.parts?.length !== 3) throw Error('Incomplete firmware manifest.');
  for (const [name,offset,end] of expected) {
    const p=manifest.parts.find(p=>p.path===name);
    if (!p || p.offset!==offset || !Number.isInteger(p.size) || p.size<=0 || offset+p.size>end || !/^[a-f0-9]{64}$/.test(p.sha256)) throw Error('Invalid firmware layout.');
  }
  return manifest.parts;
}
export async function verifiedBytes(part, buffer) {
  const bytes=new Uint8Array(buffer);
  const hash=Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256',bytes)),b=>b.toString(16).padStart(2,'0')).join('');
  if (bytes.length!==part.size || hash!==part.sha256) throw Error(`Download check failed: ${part.path}. Reload and retry.`);
  return bytes;
}
