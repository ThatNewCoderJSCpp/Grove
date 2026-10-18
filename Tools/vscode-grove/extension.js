const vscode = require('vscode');
const childProcess = require('child_process');
const fs = require('fs');
const os = require('os');
const path = require('path');
const colorText = require('./colors');
const designer = require('./color-designer');

const tokenTypes = ['tag', 'property', 'variable', 'unresolved'];
const tokenModifiers = ['declaration', 'input', 'custom'];
const legend = new vscode.SemanticTokensLegend(tokenTypes, tokenModifiers);
const languages = ['grove', 'bark'];

const categories = {
    'tags': { tm: ['entity.name.tag.grove', 'entity.name.tag.bark'], sem: ['tag'] },
    'input tags': { tm: ['entity.name.tag.input.grove'], sem: ['tag.input'] },
    'custom tags': { tm: ['entity.name.tag.custom.grove'], sem: ['tag.custom'] },
    'unknown names': { sem: ['unresolved'] },
    'attributes': { tm: ['entity.other.attribute-name.grove'] },
    'attribute values': { tm: ['string.quoted.double.grove', 'string.quoted.single.grove', 'string.unquoted.grove'] },
    'ids and classes': { tm: ['entity.other.attribute-name.id.bark', 'entity.other.attribute-name.class.bark', 'entity.other.attribute-name.pseudo-class.bark'] },
    'properties': { tm: ['support.type.property-name.bark'], sem: ['property'] },
    'variables': { tm: ['variable.other.bark'], sem: ['variable'] },
    'variable declarations': { tm: ['variable.other.declaration.bark'], sem: ['variable.declaration'] },
    'functions': { tm: ['support.function.bark'] },
    'values': { tm: ['support.constant.property-value.bark'] },
    'constants': { tm: ['constant.other.color.rgb-value.bark', 'constant.other.color.rgb-value.grove', 'support.constant.color.bark', 'keyword.other.important.bark'] },
    'numbers': { tm: ['constant.numeric.bark', 'constant.numeric.mathtext'] },
    'units': { tm: ['keyword.other.unit.bark'] },
    'strings': { tm: ['string.quoted.double.bark', 'string.quoted.single.bark', 'string.quoted.double.mathtext', 'string.quoted.single.mathtext'] },
    'escapes': { tm: ['constant.character.escape.bark', 'constant.character.entity.grove'] },
    'comments': { tm: ['comment.block.grove', 'comment.line.double-slash.bark', 'comment.block.bark'] },
    'at-rules': { tm: ['keyword.control.at-rule.bark', 'meta.tag.metadata.grove'] },
    'math commands': { tm: ['support.constant.command.mathtext'] },
    'math functions': { tm: ['entity.name.function.mathtext'] },
    'math names': { tm: ['variable.other.mathtext'] }
};

const rulePrefix = 'Grove: ';
const themeKeys = new Set(['name', 'extends', '$schema']);
const semanticSuffixes = languages.map(l => ':' + l);

let extensionPath = '';
let extensionContext = null;
let output = null;
let diagnostics = null;
let warnedMissing = false;
let lastColorsError = '';
let watchedColorsFile = '';
let applyTimer = null;
const analyses = new Map();
const running = new Map();
const tokensChanged = new vscode.EventEmitter();

function settings() {
    return vscode.workspace.getConfiguration('grove');
}

function log(text) {
    if (output) output.appendLine(text);
}

function findOnPath(name) {
    if (path.isAbsolute(name) || name.includes('/') || name.includes('\\')) return fs.existsSync(name) ? name : '';
    const exts = process.platform === 'win32' ? (process.env.PATHEXT || '.EXE').split(';').concat(['']) : [''];
    for (const dir of (process.env.PATH || '').split(path.delimiter)) {
        if (!dir) continue;
        for (const ext of exts) {
            const candidate = path.join(dir, name + ext);
            try {
                if (fs.statSync(candidate).isFile()) return candidate;
            } catch (e) {
            }
        }
    }
    return '';
}

function grovePath() {
    const configured = (settings().get('path') || '').trim();
    return configured || 'grove';
}

function repoColorsFile() {
    const program = findOnPath(grovePath());
    if (!program) return '';
    let dir;
    try {
        dir = path.dirname(fs.realpathSync(program));
    } catch (e) {
        return '';
    }
    for (let i = 0; i < 6; ++i) {
        const candidate = path.join(dir, 'Tools', 'vscode-grove', 'colors.json');
        if (fs.existsSync(candidate)) return candidate;
        const parent = path.dirname(dir);
        if (parent === dir) break;
        dir = parent;
    }
    return '';
}

function colorsFile() {
    const configured = (settings().get('colorsFile') || '').trim();
    if (configured) return configured;
    return repoColorsFile() || path.join(extensionPath, 'colors.json');
}

function styleOf(value) {
    if (typeof value === 'string') return value.trim() ? { foreground: value.trim() } : null;
    if (!value || typeof value !== 'object') return null;
    const style = {};
    if (typeof value.color === 'string' && value.color.trim()) style.foreground = value.color.trim();
    const font = [];
    if (value.bold) font.push('bold');
    if (value.italic) font.push('italic');
    if (value.underline) font.push('underline');
    if (font.length || value.bold === false || value.italic === false || value.underline === false) style.fontStyle = font.join(' ');
    return Object.keys(style).length ? style : null;
}

function semanticStyleOf(style) {
    const out = {};
    if (style.foreground) out.foreground = style.foreground;
    if (style.fontStyle !== undefined) {
        out.bold = style.fontStyle.includes('bold');
        out.italic = style.fontStyle.includes('italic');
        out.underline = style.fontStyle.includes('underline');
    }
    return out;
}

function readColors(file) {
    const text = fs.readFileSync(file, 'utf8').replace(/^﻿/, '');
    const data = JSON.parse(text);
    if (!data || typeof data !== 'object' || Array.isArray(data)) throw new Error('the file should hold one { ... } object of "name": "#RRGGBB" pairs');
    return data;
}

function isBundledColorsFile(file) {
    return path.resolve(file) === path.resolve(path.join(extensionPath, 'colors.json'));
}

function knownColorNames() {
    return Object.keys(categories);
}

function themeFolders() {
    const folders = [];
    const file = colorsFile();
    if (file && !isBundledColorsFile(file)) folders.push(path.join(path.dirname(file), 'color-themes'));
    const bundled = path.join(extensionPath, 'color-themes');
    if (!folders.some(f => path.resolve(f) === path.resolve(bundled))) folders.push(bundled);
    return folders;
}

function listThemes() {
    const themes = new Map();
    for (const folder of themeFolders()) {
        let names = [];
        try {
            names = fs.readdirSync(folder);
        } catch (e) {
            continue;
        }
        for (const name of names.sort()) {
            if (!name.toLowerCase().endsWith('.json')) continue;
            const file = path.join(folder, name);
            let label = name.slice(0, -5);
            try {
                const data = readColors(file);
                if (typeof data.name === 'string' && data.name.trim()) label = data.name.trim();
            } catch (e) {
            }
            if (!themes.has(label)) themes.set(label, file);
        }
    }
    return themes;
}

function resolveColors(data, seen) {
    const out = {};
    if (typeof data.extends === 'string' && data.extends.trim()) {
        const name = data.extends.trim();
        const file = listThemes().get(name);
        if (!file) throw new Error(`there is no color theme named "${name}" to extend`);
        const key = path.resolve(file);
        if (seen.has(key)) throw new Error(`the color theme "${name}" ends up extending itself`);
        seen.add(key);
        Object.assign(out, resolveColors(readColors(file), seen));
    }
    for (const [name, value] of Object.entries(data)) if (!themeKeys.has(name)) out[name] = value;
    return out;
}

function chosenTheme() {
    return (settings().get('colorTheme') || '').trim();
}

function activeColorsFile() {
    const theme = chosenTheme();
    if (theme) {
        const file = listThemes().get(theme);
        if (file) return file;
        log(`Grove: there is no color theme named "${theme}", so the colors file is used`);
    }
    return colorsFile();
}

function colorsFrom(file) {
    return resolveColors(readColors(file), new Set([path.resolve(file)]));
}

function sameJson(a, b) {
    return JSON.stringify(a) === JSON.stringify(b);
}

function isOurSemanticKey(key) {
    return semanticSuffixes.some(s => key.endsWith(s));
}

let applyQueue = Promise.resolve();

function applyColors(showErrors, override) {
    applyQueue = applyQueue.then(() => applyColorsNow(showErrors, override)).catch(e => log(`Grove couldn't apply the colors: ${e && e.message ? e.message : e}`));
    return applyQueue;
}

async function applyColorsNow(showErrors, override) {
    const config = vscode.workspace.getConfiguration('editor');
    let colors = {};

    if (override) {
        colors = override;
    } else if (settings().get('colors.enabled', true)) {
        const file = activeColorsFile();
        try {
            colors = colorsFrom(file);
            lastColorsError = '';
        } catch (e) {
            const message = `Grove couldn't read the colors file ${file}: ${e.message}`;
            log(message);
            if (showErrors || message !== lastColorsError) vscode.window.showWarningMessage(message);
            lastColorsError = message;
            return;
        }
        const known = Object.keys(categories);
        for (const name of Object.keys(colors)) {
            if (!known.includes(name)) log(`Grove colors: unknown name "${name}" in ${file}; the names are: ${known.join(', ')}`);
        }
    }

    const textMateRules = [];
    const semanticRules = {};
    for (const [name, category] of Object.entries(categories)) {
        const style = styleOf(colors[name]);
        if (!style) continue;
        if (category.tm) textMateRules.push({ name: rulePrefix + name, scope: category.tm, settings: style });
        if (category.sem) {
            for (const selector of category.sem) {
                for (const suffix of semanticSuffixes) semanticRules[selector + suffix] = semanticStyleOf(style);
            }
        }
    }

    const tokenColors = Object.assign({}, config.inspect('tokenColorCustomizations').globalValue || {});
    const keptRules = (tokenColors.textMateRules || []).filter(r => !(r && typeof r.name === 'string' && r.name.startsWith(rulePrefix)));
    const newTokenColors = Object.assign({}, tokenColors, { textMateRules: keptRules.concat(textMateRules) });
    if (!newTokenColors.textMateRules.length) delete newTokenColors.textMateRules;

    const semanticColors = Object.assign({}, config.inspect('semanticTokenColorCustomizations').globalValue || {});
    const keptSemantic = {};
    for (const [key, value] of Object.entries(semanticColors.rules || {})) if (!isOurSemanticKey(key)) keptSemantic[key] = value;
    const newSemanticColors = Object.assign({}, semanticColors, { rules: Object.assign(keptSemantic, semanticRules) });
    if (!Object.keys(newSemanticColors.rules).length) delete newSemanticColors.rules;

    try {
        if (!sameJson(newTokenColors, tokenColors)) await config.update('tokenColorCustomizations', Object.keys(newTokenColors).length ? newTokenColors : undefined, vscode.ConfigurationTarget.Global);
        if (!sameJson(newSemanticColors, semanticColors)) await config.update('semanticTokenColorCustomizations', Object.keys(newSemanticColors).length ? newSemanticColors : undefined, vscode.ConfigurationTarget.Global);
    } catch (e) {
        log(`Grove couldn't save the colors to your settings: ${e.message}`);
    }
}

function scheduleApply() {
    if (applyTimer) clearTimeout(applyTimer);
    applyTimer = setTimeout(() => { applyTimer = null; applyColors(false); }, 150);
}

function watchColors() {
    if (watchedColorsFile) fs.unwatchFile(watchedColorsFile);
    watchedColorsFile = activeColorsFile();
    if (watchedColorsFile) fs.watchFile(watchedColorsFile, { interval: 1000 }, scheduleApply);
}

async function removeColors() {
    const before = settings().get('colors.enabled', true);
    await settings().update('colors.enabled', false, vscode.ConfigurationTarget.Global);
    await applyColors(false);
    if (before) vscode.window.showInformationMessage('Grove colors were removed from your settings. Turn "grove.colors.enabled" back on to apply them again.');
}

async function chooseColorsFile() {
    const picked = await vscode.window.showOpenDialog({ canSelectMany: false, filters: { 'Colors file': ['json'] }, title: 'Choose a Grove colors file' });
    if (picked && picked.length) await settings().update('colorsFile', picked[0].fsPath, vscode.ConfigurationTarget.Global);
}

async function editColors() {
    const file = colorsFile();
    const bundled = path.resolve(file) === path.resolve(path.join(extensionPath, 'colors.json'));
    if (bundled) {
        const copy = path.join(os.homedir(), '.grove-colors.json');
        if (!fs.existsSync(copy)) fs.copyFileSync(file, copy);
        await settings().update('colorsFile', copy, vscode.ConfigurationTarget.Global);
        await vscode.window.showTextDocument(vscode.Uri.file(copy));
        return;
    }
    await vscode.window.showTextDocument(vscode.Uri.file(file));
}

function themeItems() {
    const items = [{ label: 'colors.json', description: 'your own colors file', theme: '' }];
    for (const [name, file] of listThemes()) {
        const bundled = path.resolve(path.dirname(file)) === path.resolve(path.join(extensionPath, 'color-themes'));
        items.push({ label: name, description: bundled ? 'came with the extension' : path.dirname(file), theme: name });
    }
    return items;
}

function themeFile(theme) {
    return theme ? listThemes().get(theme) : colorsFile();
}

async function chooseColorTheme() {
    const items = themeItems();
    const current = chosenTheme();
    const pick = vscode.window.createQuickPick();
    pick.items = items;
    pick.placeholder = 'Pick the Grove and Bark colors (moving through the list previews them)';
    pick.activeItems = items.filter(i => i.theme === current);
    let accepted = false;

    pick.onDidChangeActive(active => {
        const item = active[0];
        if (!item) return;
        try {
            applyColors(false, colorsFrom(themeFile(item.theme)));
        } catch (e) {
            log(`Grove couldn't preview "${item.label}": ${e.message}`);
        }
    });

    pick.onDidAccept(async () => {
        const item = pick.selectedItems[0] || pick.activeItems[0];
        accepted = true;
        pick.hide();
        if (!item) return;
        if (!settings().get('colors.enabled', true)) await settings().update('colors.enabled', true, vscode.ConfigurationTarget.Global);
        await settings().update('colorTheme', item.theme, vscode.ConfigurationTarget.Global);
        watchColors();
        await applyColors(true);
    });

    pick.onDidHide(() => {
        if (!accepted) applyColors(false);
        pick.dispose();
    });

    pick.show();
}

function orderedColors(colors) {
    const out = {};
    for (const name of knownColorNames()) if (colors[name] !== undefined) out[name] = colors[name];
    for (const [name, value] of Object.entries(colors)) if (out[name] === undefined) out[name] = value;
    return out;
}

async function writeColorsFile(file, colors, extra) {
    const data = Object.assign({}, extra || {}, orderedColors(colors));
    await fs.promises.mkdir(path.dirname(file), { recursive: true });
    await fs.promises.writeFile(file, JSON.stringify(data, null, 4) + '\n', 'utf8');
}

async function saveColorsAsTheme(colors) {
    const name = await vscode.window.showInputBox({
        prompt: 'Name the new Grove color theme',
        placeHolder: 'Ocean',
        validateInput: v => !v.trim() ? 'Give the theme a name' : (/[\\/:*?"<>|]/.test(v) ? 'A theme name can\'t have \\ / : * ? " < > |' : null)
    });
    if (!name) return;

    if (!colors) {
        try {
            colors = colorsFrom(activeColorsFile());
        } catch (e) {
            vscode.window.showWarningMessage(`Grove couldn't read the current colors: ${e.message}`);
            return;
        }
    }

    const folder = themeFolders()[0];
    const file = path.join(folder, name.trim() + '.json');
    if (fs.existsSync(file)) {
        const answer = await vscode.window.showWarningMessage(`There is already a theme file named ${path.basename(file)}. Replace it?`, { modal: true }, 'Replace');
        if (answer !== 'Replace') return;
    }

    try {
        await writeColorsFile(file, colors, { name: name.trim() });
    } catch (e) {
        vscode.window.showWarningMessage(`Grove couldn't save ${file}: ${e.message}`);
        return;
    }

    const where = path.resolve(folder) === path.resolve(path.join(extensionPath, 'color-themes'))
        ? ' (inside the installed extension, so reinstalling it removes the file; pick your Grove folder\'s colors.json with "Grove: Choose Colors File..." to keep themes there)'
        : '';
    const choice = await vscode.window.showInformationMessage(`Saved the color theme "${name.trim()}" to ${file}${where}.`, 'Use It', 'Open File');
    if (choice === 'Use It') await settings().update('colorTheme', name.trim(), vscode.ConfigurationTarget.Global);
    if (choice === 'Open File') await vscode.window.showTextDocument(await vscode.workspace.openTextDocument(file));
}

function openColorDesigner() {
    designer.open({
        context: extensionContext,
        names: knownColorNames(),
        themes: () => themeItems().map(i => ({ label: i.label, theme: i.theme })),
        colorsOf: theme => colorsFrom(themeFile(theme)),
        current: () => ({ theme: chosenTheme(), colors: colorsFrom(activeColorsFile()) }),
        preview: colors => applyColors(false, colors),
        restore: () => applyColors(false),
        saveToColorsFile: async colors => {
            const file = colorsFile();
            if (isBundledColorsFile(file)) {
                const choice = await vscode.window.showWarningMessage('This would save into the colors file that came with the extension, which is replaced when it is reinstalled. Pick the colors.json in your Grove folder first?', 'Choose File...', 'Save Anyway');
                if (choice === 'Choose File...') { await chooseColorsFile(); return saveDesignerColors(colors); }
                if (choice !== 'Save Anyway') return false;
            }
            return saveDesignerColors(colors);
        },
        saveAsTheme: colors => saveColorsAsTheme(colors)
    });
}

async function saveDesignerColors(colors) {
    const file = colorsFile();
    try {
        await writeColorsFile(file, colors);
    } catch (e) {
        vscode.window.showWarningMessage(`Grove couldn't save ${file}: ${e.message}`);
        return false;
    }
    if (chosenTheme()) await settings().update('colorTheme', '', vscode.ConfigurationTarget.Global);
    watchColors();
    await applyColors(true);
    vscode.window.showInformationMessage(`Saved the colors to ${file}.`);
    return true;
}

async function choosePath() {
    const picked = await vscode.window.showOpenDialog({ canSelectMany: false, title: 'Choose the grove program (in Grove\'s build folder)' });
    if (!picked || !picked.length) return;
    await settings().update('path', picked[0].fsPath, vscode.ConfigurationTarget.Global);
    warnedMissing = false;
    analyses.clear();
    tokensChanged.fire();
}

function documentPath(document) {
    if (document.uri.scheme === 'file') return document.uri.fsPath;
    const base = vscode.workspace.workspaceFolders && vscode.workspace.workspaceFolders.length ? vscode.workspace.workspaceFolders[0].uri.fsPath : os.tmpdir();
    return path.join(base, document.languageId === 'bark' ? 'untitled.bark' : 'untitled.grove');
}

function runGrove(document) {
    return new Promise((resolve, reject) => {
        const file = documentPath(document);
        let child;
        try {
            child = childProcess.spawn(grovePath(), ['highlight', file, '--stdin'], { cwd: path.dirname(file), windowsHide: true });
        } catch (e) {
            reject(e);
            return;
        }

        const key = document.uri.toString();
        const previous = running.get(key);
        if (previous) previous.kill();
        running.set(key, child);

        let stdout = '';
        let stderr = '';
        child.stdout.setEncoding('utf8');
        child.stderr.setEncoding('utf8');
        child.stdout.on('data', d => { stdout += d; });
        child.stderr.on('data', d => { stderr += d; });
        child.on('error', e => { if (running.get(key) === child) running.delete(key); reject(e); });
        child.on('close', code => {
            if (running.get(key) === child) running.delete(key);
            if (child.killed) { const e = new Error('replaced by a newer run'); e.code = 'REPLACED'; reject(e); return; }
            const start = stdout.indexOf('{');
            if (start < 0) { reject(new Error(`grove printed no result (exit code ${code})${stderr ? ': ' + stderr.trim() : ''}`)); return; }
            try {
                resolve(JSON.parse(stdout.slice(start)));
            } catch (e) {
                reject(new Error(`grove's result couldn't be read: ${e.message}`));
            }
        });
        child.stdin.on('error', () => {});
        child.stdin.end(document.getText(), 'utf8');
    });
}

function fallbackAnalysis(document) {
    return { tokens: [], diagnostics: [], colors: colorText.findColors(document.getText(), document.languageId), fallback: true };
}

function analyze(document) {
    const key = document.uri.toString();
    const cached = analyses.get(key);
    if (cached && cached.version === document.version) return cached.promise;

    const promise = runGrove(document).catch(e => {
        if (e && e.code === 'REPLACED') return fallbackAnalysis(document);
        if (e && e.code === 'ENOENT') {
            if (!warnedMissing) {
                warnedMissing = true;
                vscode.window.showWarningMessage(
                    `Grove can't find the program "${grovePath()}", so it can only do basic coloring and color swatches. Choose your grove program (in Grove's build folder) for full coloring and error underlines.`,
                    'Choose grove...').then(choice => { if (choice) choosePath(); });
            }
        } else {
            log(`Grove highlighting failed: ${e && e.message ? e.message : e}`);
        }
        return fallbackAnalysis(document);
    });
    analyses.set(key, { version: document.version, promise });
    return promise;
}

function toUtf16(lineText, codePointColumn) {
    let units = 0;
    let points = 0;
    for (const ch of lineText) {
        if (points >= codePointColumn) break;
        units += ch.length;
        points += 1;
    }
    return units;
}

function position(document, line, codePointColumn) {
    const l = Math.min(Math.max(line || 0, 0), Math.max(document.lineCount - 1, 0));
    return new vscode.Position(l, toUtf16(document.lineAt(l).text, codePointColumn || 0));
}

function rangeOf(document, item) {
    const start = position(document, item.line, item.col);
    const endLine = item.endLine === undefined ? item.line : item.endLine;
    let end = position(document, endLine, item.endCol === undefined ? item.col : item.endCol);
    if (end.isBeforeOrEqual(start)) {
        const text = document.lineAt(start.line).text;
        const word = document.getWordRangeAtPosition(start);
        end = word && word.end.isAfter(start) ? word.end : new vscode.Position(start.line, text.length);
        if (end.isBeforeOrEqual(start)) return new vscode.Range(start.line, document.lineAt(start.line).firstNonWhitespaceCharacterIndex, start.line, text.length);
    }
    return new vscode.Range(start, end);
}

function publishDiagnostics(document, result) {
    if (!settings().get('diagnostics', true) || result.fallback) { diagnostics.delete(document.uri); return; }
    const list = [];
    for (const d of result.diagnostics || []) {
        const severity = d.severity === 'warning' ? vscode.DiagnosticSeverity.Warning : d.severity === 'note' ? vscode.DiagnosticSeverity.Information : vscode.DiagnosticSeverity.Error;
        const diagnostic = new vscode.Diagnostic(rangeOf(document, d), d.message || '', severity);
        diagnostic.source = 'grove';
        list.push(diagnostic);
    }
    diagnostics.set(document.uri, list);
}

function buildTokens(document, result) {
    const builder = new vscode.SemanticTokensBuilder(legend);
    for (const [line, col, len, type, mods] of result.tokens || []) {
        const typeIndex = tokenTypes.indexOf(type);
        if (typeIndex < 0 || len <= 0 || line >= document.lineCount) continue;
        const text = document.lineAt(line).text;
        const start = toUtf16(text, col);
        const end = toUtf16(text, col + len);
        let bits = 0;
        for (const m of mods || []) { const i = tokenModifiers.indexOf(m); if (i >= 0) bits |= 1 << i; }
        builder.push(line, start, end - start, typeIndex, bits);
    }
    return builder.build();
}

function delay(ms, cancel) {
    return new Promise((resolve, reject) => {
        const timer = setTimeout(() => { stop.dispose(); resolve(); }, ms);
        const stop = cancel.onCancellationRequested(() => { clearTimeout(timer); stop.dispose(); reject(new vscode.CancellationError()); });
    });
}

const tokenProvider = {
    onDidChangeSemanticTokens: tokensChanged.event,

    async provideDocumentSemanticTokens(document, cancel) {
        await delay(Math.max(0, settings().get('highlightDelay', 250)), cancel);
        const version = document.version;
        const result = await analyze(document);
        if (cancel.isCancellationRequested || document.version !== version) throw new vscode.CancellationError();
        publishDiagnostics(document, result);
        if (!settings().get('semanticHighlighting', true)) return new vscode.SemanticTokensBuilder(legend).build();
        return buildTokens(document, result);
    }
};

const colorProvider = {
    async provideDocumentColors(document, cancel) {
        if (!settings().get('colorPickers', true)) return [];
        const version = document.version;
        const result = await analyze(document);
        if (cancel.isCancellationRequested || document.version !== version) return [];
        return (result.colors || []).map(c => new vscode.ColorInformation(rangeOf(document, c), new vscode.Color(c.r / 255, c.g / 255, c.b / 255, c.a / 255)));
    },

    provideColorPresentations(color, context) {
        const original = context.document.getText(context.range);
        const before = context.document.lineAt(context.range.start.line).text.slice(0, context.range.start.character);
        const hexOnly = context.document.languageId === 'grove' && /\bvalue\s*=\s*["']?$/.test(before);
        const rgba = colorText.toBytes(color.red, color.green, color.blue, color.alpha);
        return colorText.presentations(rgba, original, hexOnly).map(text => {
            const p = new vscode.ColorPresentation(text);
            p.textEdit = new vscode.TextEdit(context.range, text);
            return p;
        });
    }
};

function activate(context) {
    extensionPath = context.extensionPath;
    extensionContext = context;
    output = vscode.window.createOutputChannel('Grove');
    diagnostics = vscode.languages.createDiagnosticCollection('grove');
    const selector = languages.map(language => ({ language }));

    context.subscriptions.push(
        output,
        diagnostics,
        tokensChanged,
        vscode.languages.registerDocumentSemanticTokensProvider(selector, tokenProvider, legend),
        vscode.languages.registerColorProvider(selector, colorProvider),
        vscode.workspace.onDidCloseTextDocument(doc => { diagnostics.delete(doc.uri); analyses.delete(doc.uri.toString()); }),
        vscode.workspace.onDidSaveTextDocument(doc => {
            if (watchedColorsFile && doc.uri.scheme === 'file' && path.resolve(doc.uri.fsPath) === path.resolve(watchedColorsFile)) applyColors(true);
            if (languages.includes(doc.languageId)) { analyses.clear(); tokensChanged.fire(); }
        }),
        vscode.workspace.onDidChangeConfiguration(e => {
            if (e.affectsConfiguration('grove.colorsFile') || e.affectsConfiguration('grove.colors.enabled') || e.affectsConfiguration('grove.path') || e.affectsConfiguration('grove.colorTheme')) { watchColors(); applyColors(true); }
            if (e.affectsConfiguration('grove')) { analyses.clear(); tokensChanged.fire(); }
        }),
        vscode.commands.registerCommand('grove.editColors', editColors),
        vscode.commands.registerCommand('grove.chooseColorsFile', chooseColorsFile),
        vscode.commands.registerCommand('grove.removeColors', removeColors),
        vscode.commands.registerCommand('grove.chooseColorTheme', chooseColorTheme),
        vscode.commands.registerCommand('grove.openColorDesigner', openColorDesigner),
        vscode.commands.registerCommand('grove.saveColorsAsTheme', () => saveColorsAsTheme()),
        vscode.commands.registerCommand('grove.choosePath', choosePath),
        vscode.commands.registerCommand('grove.refresh', () => { warnedMissing = false; analyses.clear(); tokensChanged.fire(); }),
        { dispose: () => { if (watchedColorsFile) fs.unwatchFile(watchedColorsFile); for (const child of running.values()) child.kill(); } }
    );

    watchColors();
    applyColors(false);
}

function deactivate() {
}

module.exports = { activate, deactivate };
