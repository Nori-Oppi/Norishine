// Generate all Norishine icon files from assets/norishine.svg (the logo).
//
// Usage, from the repository root:
//   npm install --no-save @resvg/resvg-js@2.6.2
//   node scripts/icons/generate-icons.js .
//
// Writes the application icons (norishine.svg/.png/.ico/.icns at the root) and the web UI
// and tray icons, including the playing, pausing and locked variants with a status badge.
const fs = require('fs');
const path = require('path');
const { Resvg } = require('@resvg/resvg-js');

const root = process.argv[2];
const images = path.join(root, 'src_assets/common/assets/web/public/images');
const base = fs.readFileSync(path.join(root, 'assets/norishine.svg'), 'utf8');

// Status badge in the bottom-right corner, like Apollo's tray icons.
const badges = {
  playing: { color: 'rgb(46,202,51)', glyph: '<path d="M188 170 L232 198 L188 226 Z" fill="white"/>' },
  pausing: { color: 'rgb(41,168,255)', glyph: '<rect x="184" y="172" width="13" height="52" rx="3" fill="white"/><rect x="207" y="172" width="13" height="52" rx="3" fill="white"/>' },
  locked: {
    color: 'rgb(245,158,11)',
    glyph: '<path d="M187 192 V182 A15 15 0 0 1 217 182 V192" fill="none" stroke="white" stroke-width="9"/>' +
      '<rect x="178" y="190" width="48" height="36" rx="5" fill="white"/>',
  },
};

function withBadge(name) {
  const b = badges[name];
  const badge = `\n  <!-- Status badge: ${name} -->\n  <circle cx="202" cy="198" r="50" fill="${b.color}" stroke="white" stroke-width="8"/>\n  ${b.glyph}\n`;
  return base.replace('<title>Norishine</title>', `<title>Norishine (${name})</title>`).replace(/<\/svg>\s*$/, badge + '</svg>\n');
}

function png(svg, size) {
  return new Resvg(svg, { fitTo: { mode: 'width', value: size } }).render().asPng();
}

function ico(svg, sizes = [16, 24, 32, 48, 64, 128, 256]) {
  const pngs = sizes.map((s) => png(svg, s));
  const header = Buffer.alloc(6 + 16 * sizes.length);
  header.writeUInt16LE(0, 0);
  header.writeUInt16LE(1, 2);
  header.writeUInt16LE(sizes.length, 4);
  let offset = header.length;
  sizes.forEach((s, i) => {
    const e = 6 + 16 * i;
    header.writeUInt8(s >= 256 ? 0 : s, e);
    header.writeUInt8(s >= 256 ? 0 : s, e + 1);
    header.writeUInt8(0, e + 2);
    header.writeUInt8(0, e + 3);
    header.writeUInt16LE(1, e + 4);
    header.writeUInt16LE(32, e + 6);
    header.writeUInt32LE(pngs[i].length, e + 8);
    header.writeUInt32LE(offset, e + 12);
    offset += pngs[i].length;
  });
  return Buffer.concat([header, ...pngs]);
}

function icns(svg) {
  const types = [['icp4', 16], ['icp5', 32], ['icp6', 64], ['ic07', 128], ['ic08', 256], ['ic09', 512], ['ic10', 1024]];
  const chunks = types.map(([t, s]) => {
    const data = png(svg, s);
    const h = Buffer.alloc(8);
    h.write(t, 0, 'ascii');
    h.writeUInt32BE(data.length + 8, 4);
    return Buffer.concat([h, data]);
  });
  const body = Buffer.concat(chunks);
  const h = Buffer.alloc(8);
  h.write('icns', 0, 'ascii');
  h.writeUInt32BE(body.length + 8, 4);
  return Buffer.concat([h, body]);
}

const out = (p, data) => { fs.writeFileSync(p, data); console.log(path.relative(root, p), data.length); };

// Application icons (installer, exe resource, Linux desktop, macOS bundle).
out(path.join(root, 'norishine.svg'), base);
out(path.join(root, 'norishine.png'), png(base, 1024));
out(path.join(root, 'norishine.ico'), ico(base));
out(path.join(root, 'norishine.icns'), icns(base));

// Web UI and tray.
out(path.join(images, 'logo-norishine.svg'), base);
out(path.join(images, 'logo-norishine-16.png'), png(base, 16));
out(path.join(images, 'logo-norishine-45.png'), png(base, 45));
out(path.join(images, 'norishine.ico'), ico(base));
for (const name of Object.keys(badges)) {
  const svg = withBadge(name);
  out(path.join(images, `norishine-${name}.svg`), svg);
  out(path.join(images, `norishine-${name}.png`), png(svg, 1024));
  out(path.join(images, `norishine-${name}-16.png`), png(svg, 16));
  out(path.join(images, `norishine-${name}-45.png`), png(svg, 45));
  out(path.join(images, `norishine-${name}.ico`), ico(svg));
}
