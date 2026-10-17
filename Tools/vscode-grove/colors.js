function clampByte(v) {
    return Math.max(0, Math.min(255, Math.round(v)));
}

function toBytes(r, g, b, a) {
    return { r: clampByte(r * 255), g: clampByte(g * 255), b: clampByte(b * 255), a: clampByte(a * 255) };
}

function hex2(v) {
    return v.toString(16).padStart(2, '0');
}

function toHex(c, upper) {
    let s = '#' + hex2(c.r) + hex2(c.g) + hex2(c.b);
    if (c.a !== 255) s += hex2(c.a);
    return upper ? s.toUpperCase() : s;
}

function alphaText(a) {
    return String(Math.round((a / 255) * 100) / 100);
}

function toRgb(c) {
    if (c.a === 255) return `rgb(${c.r}, ${c.g}, ${c.b})`;
    return `rgba(${c.r}, ${c.g}, ${c.b}, ${alphaText(c.a)})`;
}

function toHsl(c) {
    const r = c.r / 255;
    const g = c.g / 255;
    const b = c.b / 255;
    const max = Math.max(r, g, b);
    const min = Math.min(r, g, b);
    const l = (max + min) / 2;
    let h = 0;
    let s = 0;
    if (max !== min) {
        const d = max - min;
        s = l > 0.5 ? d / (2 - max - min) : d / (max + min);
        if (max === r) h = (g - b) / d + (g < b ? 6 : 0);
        else if (max === g) h = (b - r) / d + 2;
        else h = (r - g) / d + 4;
        h *= 60;
    }
    const hs = Math.round(h);
    const ss = Math.round(s * 100);
    const ls = Math.round(l * 100);
    if (c.a === 255) return `hsl(${hs}, ${ss}%, ${ls}%)`;
    return `hsla(${hs}, ${ss}%, ${ls}%, ${alphaText(c.a)})`;
}

function presentations(c, original, hexOnly) {
    const text = (original || '').trim();
    const upper = /^#[0-9A-F]+$/.test(text) && /[A-F]/.test(text);
    const hex = toHex(c, upper);
    if (hexOnly) return [hex];
    const rgb = toRgb(c);
    const hsl = toHsl(c);
    if (/^rgba?\s*\(/i.test(text)) return [rgb, hex, hsl];
    if (/^hsla?\s*\(/i.test(text)) return [hsl, hex, rgb];
    return [hex, rgb, hsl];
}

function parseHex(h) {
    const d = h.replace(/^#/, '');
    const pair = i => parseInt(d.substr(i, 2), 16);
    const single = i => parseInt(d[i] + d[i], 16);
    if (d.length === 3) return { r: single(0), g: single(1), b: single(2), a: 255 };
    if (d.length === 4) return { r: single(0), g: single(1), b: single(2), a: single(3) };
    if (d.length === 6) return { r: pair(0), g: pair(2), b: pair(4), a: 255 };
    if (d.length === 8) return { r: pair(0), g: pair(2), b: pair(4), a: pair(6) };
    return null;
}

function channel(arg) {
    const m = /^(-?\d*\.?\d+)(%?)$/.exec(arg.trim());
    if (!m) return null;
    const n = parseFloat(m[1]);
    return clampByte(m[2] ? n * 255 / 100 : n);
}

function alphaChannel(arg) {
    const m = /^(-?\d*\.?\d+)(%?)$/.exec(arg.trim());
    if (!m) return null;
    let n = parseFloat(m[1]);
    if (m[2]) n /= 100;
    return n > 1 ? clampByte(n) : clampByte(n * 255);
}

function hueToRgb(p, q, t) {
    if (t < 0) t += 1;
    if (t > 1) t -= 1;
    if (t < 1 / 6) return p + (q - p) * 6 * t;
    if (t < 1 / 2) return q;
    if (t < 2 / 3) return p + (q - p) * (2 / 3 - t) * 6;
    return p;
}

function parseFunction(name, inner) {
    const args = inner.split(',').map(s => s.trim());
    if (args.length !== 3 && args.length !== 4) return null;
    let c;
    if (/^rgba?$/i.test(name)) {
        const [r, g, b] = args.slice(0, 3).map(channel);
        if (r === null || g === null || b === null) return null;
        c = { r, g, b, a: 255 };
    } else {
        const nums = args.slice(0, 3).map(a => /^(-?\d*\.?\d+)%?$/.exec(a));
        if (nums.some(n => !n)) return null;
        const h = ((parseFloat(nums[0][1]) % 360) + 360) % 360 / 360;
        const s = Math.min(1, Math.max(0, parseFloat(nums[1][1]) / 100));
        const l = Math.min(1, Math.max(0, parseFloat(nums[2][1]) / 100));
        let r = l, g = l, b = l;
        if (s > 0) {
            const q = l < 0.5 ? l * (1 + s) : l + s - l * s;
            const p = 2 * l - q;
            r = hueToRgb(p, q, h + 1 / 3);
            g = hueToRgb(p, q, h);
            b = hueToRgb(p, q, h - 1 / 3);
        }
        c = { r: clampByte(r * 255), g: clampByte(g * 255), b: clampByte(b * 255), a: 255 };
    }
    if (args.length === 4) {
        const a = alphaChannel(args[3]);
        if (a === null) return null;
        c.a = a;
    }
    return c;
}

function codePoints(s) {
    let n = 0;
    for (const ch of s) { void ch; n += 1; }
    return n;
}

function findColors(text, languageId) {
    const out = [];
    const lines = text.split('\n');
    const pattern = /#(?:[0-9a-fA-F]{8}|[0-9a-fA-F]{6}|[0-9a-fA-F]{3,4})\b|\b(rgba?|hsla?)\s*\(([^()]*)\)/g;
    lines.forEach((line, index) => {
        pattern.lastIndex = 0;
        let m;
        while ((m = pattern.exec(line)) !== null) {
            const before = line.slice(0, m.index);
            if (!/[:=]/.test(before)) continue;
            if (languageId === 'grove' && m[0][0] === '#' && !/(\bvalue\s*=\s*["']?|:[^;"']*)$/.test(before)) continue;
            const c = m[0][0] === '#' ? parseHex(m[0]) : parseFunction(m[1], m[2]);
            if (!c) continue;
            const col = codePoints(before);
            out.push({ line: index, col, endLine: index, endCol: col + codePoints(m[0]), r: c.r, g: c.g, b: c.b, a: c.a });
        }
    });
    return out;
}

module.exports = { toBytes, toHex, toRgb, toHsl, presentations, parseHex, parseFunction, findColors };
