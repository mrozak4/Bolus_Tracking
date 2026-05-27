/**
 * Bolus Tracking Studio — Renderer
 * Handles: splash screen animation, IPC with bolus_server, ROI management,
 * SVG plot display, draggable markers, localization, and sounds.
 *
 * Memory-conscious: only one ROI's data at a time. Plots rendered in C++.
 */

// ═══════════════════════════════════════════════════════════════════════════
// STATE
// ═══════════════════════════════════════════════════════════════════════════

const state = {
    // Data
    roiIds: [],           // ROI ids from compute_traces
    roiRecords: [],       // summary records from load_csv or auto_estimate
    nFrames: 0,
    tiffWidth: 0,
    tiffHeight: 0,
    mipBase64: '',
    datasetPath: '',
    csvPath: '',

    // Current selection
    selectedRoiIdx: -1,
    cropMin: 0,
    cropMax: 100,
    onsetMarker: 0,
    peakMarker: 0,
    endMarker: 0,
    baselineMarker: 0,
    plotCoord: null,

    // Filter / triage
    filterMode: 'all',
    filteredIndices: [],
    triagePos: 0,

    // Locale
    lang: 'en',
    tr: {},

    // Sounds
    sounds: { thx: null, squeak: null, hallelujah: null },

    // Flags
    dataLoaded: false,
    splashDone: false,
};

// ═══════════════════════════════════════════════════════════════════════════
// SOUND SYSTEM
// ═══════════════════════════════════════════════════════════════════════════

async function initSounds() {
    const thxEl = document.getElementById('sound-thx');
    const squeakEl = document.getElementById('sound-squeak');
    const hallelujahEl = document.getElementById('sound-hallelujah');

    try {
        thxEl.src = await window.bolusAPI.getSoundPath('thx_crescendo.wav');
        squeakEl.src = await window.bolusAPI.getSoundPath('minion_squeak.wav');
        hallelujahEl.src = await window.bolusAPI.getSoundPath('hallelujah.mp3');

        state.sounds.thx = thxEl;
        state.sounds.squeak = squeakEl;
        state.sounds.hallelujah = hallelujahEl;
    } catch (e) {
        console.warn('Sound init failed:', e);
    }
}

function playSound(name) {
    const el = state.sounds[name];
    if (!el) return;
    try {
        el.currentTime = 0;
        el.play().catch(() => {});
    } catch (e) { /* ignore */ }
}

function playSqueak() { playSound('squeak'); }

// Attach squeak to all interactive elements
function initSqueakListeners() {
    document.addEventListener('mousedown', (e) => {
        const tag = e.target.tagName;
        if (tag === 'BUTTON' || tag === 'SELECT' || tag === 'INPUT' ||
            e.target.classList.contains('roi-item')) {
            playSqueak();
        }
    });
}

// ═══════════════════════════════════════════════════════════════════════════
// SPLASH SCREEN (Canvas-based, matches ImGui intro)
// ═══════════════════════════════════════════════════════════════════════════

function runSplashScreen() {
    const canvas = document.getElementById('splash-canvas');
    const ctx = canvas.getContext('2d');
    const splashDiv = document.getElementById('splash-screen');
    const startTime = performance.now();
    const duration = 6500; // 6.5 seconds

    // Colors (matching ImGui intro)
    const COL_BG         = '#131316';
    const COL_CREAM      = '#f4ead4';
    const COL_MUSTARD    = '#e6ad45';
    const COL_TERRACOTTA = '#d95d39';
    const COL_RED_DARK   = '#8a2522';
    const COL_TEAL       = '#3a6073';
    const COL_TEAL_LIGHT = '#52849b';

    // Play THX sound
    playSound('thx');

    // Static blood cell seeds
    const bcells = [
        { xr: 0.15, yo: -10, r: 12, c: 'rgba(138,37,34,0.47)' },
        { xr: 0.28, yo: 15,  r: 10, c: 'rgba(217,93,57,0.39)' },
        { xr: 0.45, yo: -8,  r: 14, c: 'rgba(138,37,34,0.31)' },
        { xr: 0.62, yo: 12,  r: 11, c: 'rgba(217,93,57,0.43)' },
        { xr: 0.78, yo: -12, r: 13, c: 'rgba(138,37,34,0.35)' },
        { xr: 0.90, yo: 5,   r: 10, c: 'rgba(217,93,57,0.47)' },
    ];

    function bezier(t, p0, p1, p2, p3) {
        const omt = 1 - t;
        return {
            x: omt*omt*omt*p0.x + 3*omt*omt*t*p1.x + 3*omt*t*t*p2.x + t*t*t*p3.x,
            y: omt*omt*omt*p0.y + 3*omt*omt*t*p1.y + 3*omt*t*t*p2.y + t*t*t*p3.y,
        };
    }

    function hexToRGBA(hex, alpha) {
        const r = parseInt(hex.slice(1,3), 16);
        const g = parseInt(hex.slice(3,5), 16);
        const b = parseInt(hex.slice(5,7), 16);
        return `rgba(${r},${g},${b},${alpha})`;
    }

    function draw(timestamp) {
        const elapsed = (timestamp - startTime) / 1000;
        if (elapsed > 6.5 || state.splashDone) {
            splashDiv.classList.add('hidden');
            document.getElementById('app').classList.remove('hidden');
            showNoDataScreen();
            state.splashDone = true;
            return;
        }

        const dpr = window.devicePixelRatio || 1;
        const w = canvas.clientWidth;
        const h = canvas.clientHeight;
        canvas.width = w * dpr;
        canvas.height = h * dpr;
        ctx.setTransform(dpr, 0, 0, dpr, 0, 0);

        // Global fade out in last 0.5s
        let globalAlpha = 1.0;
        if (elapsed > 6.0) globalAlpha = (6.5 - elapsed) / 0.5;

        // Background
        ctx.fillStyle = COL_BG;
        ctx.fillRect(0, 0, w, h);

        // Retro grid
        ctx.globalAlpha = globalAlpha * 0.15;
        ctx.strokeStyle = '#282830';
        ctx.lineWidth = 1;
        const gridY = h * 0.7;
        for (let i = 0; i <= 16; i++) {
            const x = (w * i) / 16;
            ctx.beginPath(); ctx.moveTo(x, gridY); ctx.lineTo(x, h); ctx.stroke();
        }
        for (let i = 0; i < 8; i++) {
            const ratio = i / 7;
            const y = gridY + (h - gridY) * ratio * ratio;
            ctx.beginPath(); ctx.moveTo(0, y); ctx.lineTo(w, y); ctx.stroke();
        }

        // Vessel bezier control points
        const p0 = { x: 0, y: h * 0.45 };
        const cp1 = { x: w * 0.3, y: h * 0.3 };
        const cp2 = { x: w * 0.6, y: h * 0.6 };
        const p3 = { x: w, y: h * 0.45 };
        const thickness = 70;
        const steps = 100;

        // Compute points along bezier
        const points = [];
        for (let i = 0; i <= steps; i++) {
            points.push(bezier(i / steps, p0, cp1, cp2, p3));
        }

        // Normals
        function getNormal(i) {
            const a = points[Math.min(i, steps - 1)];
            const b = points[Math.min(i + 1, steps)];
            const dx = b.x - a.x, dy = b.y - a.y;
            const len = Math.sqrt(dx*dx + dy*dy) || 1;
            return { x: -dy/len, y: dx/len };
        }

        // Draw vessel ribbon
        ctx.globalAlpha = globalAlpha * 0.35;
        for (let i = 0; i < steps; i++) {
            const n = getNormal(i);
            const a = points[i], b = points[i+1];
            ctx.fillStyle = 'rgba(35,55,65,0.35)';
            ctx.beginPath();
            ctx.moveTo(a.x + n.x*thickness*0.5, a.y + n.y*thickness*0.5);
            ctx.lineTo(b.x + n.x*thickness*0.5, b.y + n.y*thickness*0.5);
            ctx.lineTo(b.x - n.x*thickness*0.5, b.y - n.y*thickness*0.5);
            ctx.lineTo(a.x - n.x*thickness*0.5, a.y - n.y*thickness*0.5);
            ctx.fill();
        }

        // Vessel borders
        ctx.globalAlpha = globalAlpha;
        ctx.lineWidth = 3;
        ctx.strokeStyle = COL_CREAM;
        ctx.beginPath();
        for (let i = 0; i <= steps; i++) {
            const n = getNormal(i);
            const pt = { x: points[i].x + n.x*thickness*0.5, y: points[i].y + n.y*thickness*0.5 };
            i === 0 ? ctx.moveTo(pt.x, pt.y) : ctx.lineTo(pt.x, pt.y);
        }
        ctx.stroke();

        ctx.strokeStyle = COL_MUSTARD;
        ctx.beginPath();
        for (let i = 0; i <= steps; i++) {
            const n = getNormal(i);
            const pt = { x: points[i].x - n.x*thickness*0.5, y: points[i].y - n.y*thickness*0.5 };
            i === 0 ? ctx.moveTo(pt.x, pt.y) : ctx.lineTo(pt.x, pt.y);
        }
        ctx.stroke();

        // Static blood cells
        for (const bc of bcells) {
            const idx = Math.round(bc.xr * steps);
            if (idx >= 0 && idx <= steps) {
                const center = points[idx];
                const n = getNormal(idx);
                const cx = center.x + n.x * bc.yo;
                const cy = center.y + n.y * bc.yo;
                ctx.globalAlpha = globalAlpha;
                ctx.fillStyle = bc.c;
                ctx.beginPath(); ctx.arc(cx, cy, bc.r, 0, Math.PI*2); ctx.fill();
                ctx.strokeStyle = hexToRGBA(COL_CREAM, 0.4 * globalAlpha);
                ctx.lineWidth = 1.5;
                ctx.stroke();
            }
        }

        // Active cell position
        let cellT = Math.min(elapsed / 2.0, 1.0);
        const bezierParam = cellT * 0.5;
        const cellPos = bezier(bezierParam, p0, cp1, cp2, p3);

        // Tracking ticks
        ctx.globalAlpha = globalAlpha * 0.6;
        ctx.strokeStyle = COL_TEAL_LIGHT;
        ctx.lineWidth = 1.5;
        const cellIdxLimit = Math.round(bezierParam * steps);
        for (let i = 0; i <= cellIdxLimit; i += 4) {
            const n = getNormal(i);
            ctx.beginPath();
            ctx.moveTo(points[i].x + n.x*thickness*0.3, points[i].y + n.y*thickness*0.3);
            ctx.lineTo(points[i].x - n.x*thickness*0.3, points[i].y - n.y*thickness*0.3);
            ctx.stroke();
        }

        // Crescendo effects
        let rumbleX = 0, rumbleY = 0, pulseScale = 1, crescendoIntensity = 0;
        if (elapsed >= 1.5 && elapsed < 6.5) {
            crescendoIntensity = elapsed < 3.5 ? (elapsed - 1.5) / 2.0 : 1.0;
            pulseScale = 1.0 + 0.3 * crescendoIntensity * Math.sin(elapsed * 25);
            rumbleX = 8 * crescendoIntensity * Math.sin(elapsed * 45);
            rumbleY = 8 * crescendoIntensity * Math.cos(elapsed * 37);

            // Shockwave rings
            for (let w2 = 0; w2 < 4; w2++) {
                const waveAge = elapsed * 1.5 - w2 * 0.35;
                if (waveAge > 0) {
                    const waveR = 24 * (1 + 8 * (waveAge % 1));
                    const waveAlpha = (1 - (waveAge % 1)) * crescendoIntensity * globalAlpha * 0.7;
                    ctx.globalAlpha = waveAlpha;
                    ctx.strokeStyle = w2 % 2 === 0 ? COL_TERRACOTTA : COL_MUSTARD;
                    ctx.lineWidth = 2.5;
                    ctx.beginPath();
                    ctx.arc(cellPos.x + rumbleX, cellPos.y + rumbleY, waveR, 0, Math.PI*2);
                    ctx.stroke();
                }
            }
        }

        // Active cell
        const acx = cellPos.x + rumbleX;
        const acy = cellPos.y + rumbleY;
        const finalR = 24 * pulseScale;
        ctx.globalAlpha = globalAlpha;
        ctx.fillStyle = COL_TERRACOTTA;
        ctx.beginPath(); ctx.arc(acx, acy, finalR, 0, Math.PI*2); ctx.fill();
        ctx.fillStyle = COL_RED_DARK;
        ctx.beginPath(); ctx.arc(acx - finalR*0.15, acy - finalR*0.15, finalR*0.5, 0, Math.PI*2); ctx.fill();
        ctx.strokeStyle = COL_CREAM;
        ctx.lineWidth = 3;
        ctx.beginPath(); ctx.arc(acx, acy, finalR, 0, Math.PI*2); ctx.stroke();

        // Title: BOLUS KINETICS
        let logoAlpha = 0;
        if (elapsed > 1.2) logoAlpha = Math.min((elapsed - 1.2) / 1.0, 1.0);
        if (logoAlpha > 0) {
            const logoY = h * 0.28;
            const fontSize = 36;
            ctx.font = `bold ${fontSize}px ${getComputedStyle(document.body).fontFamily}`;
            const titleStr = 'BOLUS KINETICS';
            const textW = ctx.measureText(titleStr).width;
            const tx = w * 0.5 - textW * 0.5 + rumbleX;
            const ty = logoY + rumbleY;

            // Shadow layers
            ctx.globalAlpha = logoAlpha * globalAlpha;
            ctx.fillStyle = COL_TERRACOTTA;
            ctx.fillText(titleStr, tx + 5, ty + 5);
            ctx.globalAlpha = logoAlpha * globalAlpha * 0.8;
            ctx.fillStyle = COL_TEAL;
            ctx.fillText(titleStr, tx - 4, ty - 4);
            ctx.globalAlpha = logoAlpha * globalAlpha;
            ctx.fillStyle = COL_CREAM;
            ctx.fillText(titleStr, tx, ty);
        }

        // MADE BY MATT badge
        let mattAlpha = 0;
        if (elapsed > 1.8) mattAlpha = Math.min((elapsed - 1.8) / 0.8, 1.0);
        if (mattAlpha > 0) {
            const subY = h * 0.65;
            const scale = 0.30 + 3.70 * mattAlpha;
            const subFontSize = 28 * scale;
            ctx.font = `bold ${subFontSize}px ${getComputedStyle(document.body).fontFamily}`;
            const subStr = 'MADE BY MATT';
            const subW = ctx.measureText(subStr).width;
            const subH = subFontSize;
            const padX = 28 * scale, padY = 10 * scale;
            const cornerR = 24 * scale;

            const bx = w*0.5 - subW*0.5 - padX;
            const by = subY - subH*0.5 - padY;
            const bw = subW + padX*2;
            const bh = subH + padY*2;

            // Badge background
            ctx.globalAlpha = mattAlpha * globalAlpha * 0.9;
            ctx.fillStyle = COL_TEAL;
            ctx.beginPath();
            ctx.roundRect(bx, by, bw, bh, cornerR);
            ctx.fill();

            // Badge border
            ctx.globalAlpha = mattAlpha * globalAlpha;
            ctx.strokeStyle = COL_CREAM;
            ctx.lineWidth = 2 * scale;
            ctx.stroke();

            // Badge text
            ctx.fillStyle = COL_CREAM;
            ctx.fillText(subStr, w*0.5 - subW*0.5, subY + subH*0.3);
        }

        // Skip hint
        ctx.globalAlpha = 0.5 * (elapsed > 6.0 ? (6.5 - elapsed) / 0.5 : 1.0);
        ctx.font = '13px sans-serif';
        ctx.fillStyle = COL_CREAM;
        const skipStr = 'Press SPACE to Skip';
        const skipW = ctx.measureText(skipStr).width;
        ctx.fillText(skipStr, w - skipW - 20, h - 20);

        ctx.globalAlpha = 1.0;
        requestAnimationFrame(draw);
    }

    // Skip handlers
    function skipSplash() {
        if (!state.splashDone) {
            state.splashDone = true;
            if (state.sounds.thx) state.sounds.thx.pause();
            splashDiv.classList.add('hidden');
            document.getElementById('app').classList.remove('hidden');
            showNoDataScreen();
        }
    }

    document.addEventListener('keydown', (e) => {
        if (!state.splashDone && (e.code === 'Space' || e.code === 'Enter')) {
            e.preventDefault();
            skipSplash();
        }
    });
    splashDiv.addEventListener('click', skipSplash);

    requestAnimationFrame(draw);
}

// ═══════════════════════════════════════════════════════════════════════════
// LOCALIZATION
// ═══════════════════════════════════════════════════════════════════════════

const LANGUAGES = [
    { code: 'af', name: 'AF (Suid-Afrika)' },
    { code: 'bn', name: 'বাংলা (Bengali)' },
    { code: 'bg', name: 'BG (България)' },
    { code: 'ca', name: 'CA (Catalunya)' },
    { code: 'zh_cn', name: '简体中文' },
    { code: 'da', name: 'DA (Danmark)' },
    { code: 'nl', name: 'NL (Nederland)' },
    { code: 'en', name: 'EN (Canada)' },
    { code: 'fi', name: 'FI (Suomi)' },
    { code: 'fr', name: 'FR (Québec)' },
    { code: 'de_ch', name: 'DE (Schweiz)' },
    { code: 'el', name: 'EL (Ελλάδα)' },
    { code: 'gl', name: 'Kalaallisut' },
    { code: 'ht', name: 'Kreyòl (Ayiti)' },
    { code: 'hi', name: 'हिन्दी (Hindi)' },
    { code: 'id', name: 'ID (Indonesia)' },
    { code: 'iu', name: 'IU (ᐃᓄᒃᑎᑐᑦ)' },
    { code: 'ga', name: 'Gaeilge' },
    { code: 'it', name: 'IT (Italia)' },
    { code: 'ja', name: '日本語' },
    { code: 'ko', name: '한국어' },
    { code: 'la', name: 'LA (Vatican City)' },
    { code: 'no', name: 'NO (Norge)' },
    { code: 'ru', name: 'RU (Россия)' },
    { code: 'scots', name: 'Scots' },
    { code: 'sr', name: 'SR (Србија)' },
    { code: 'es', name: 'ES (España)' },
    { code: 'sv', name: 'SV (Sverige)' },
    { code: 'tl', name: 'TL (Pilipinas)' },
    { code: 'ta', name: 'தமிழ் (Tamil)' },
    { code: 'th', name: 'ไทย (Thai)' },
    { code: 'tr', name: 'TR (Türkiye)' },
    { code: 'uk', name: 'UK (Україна)' },
    { code: 'vi', name: 'VI (Việt Nam)' },
    // ─── separator ───
    { code: '_sep', name: '───────────────' },
    // No Ancient Egyptian (LANG_EGY) per user request
    { code: 'grc', name: 'Ancient Greek' },
    { code: 'eo', name: 'Esperanto' },
    { code: 'genalpha', name: 'Gen Alpha English' },
    { code: 'genz', name: 'Gen Z English' },
    { code: 'kl', name: 'tlhIngan Hol' },
    { code: 'leet', name: 'Leet Speak' },
    { code: 'minion', name: 'Minion (Bello!)' },
    { code: 'pirate', name: 'Pirate English' },
    { code: 'shakespeare', name: 'Shakespearean' },
    { code: 'yoda', name: 'Yoda Speak' },
];

function initLanguageSelector() {
    const sel = document.getElementById('lang-select');
    sel.innerHTML = '';
    for (const lang of LANGUAGES) {
        if (lang.code === '_sep') {
            const opt = document.createElement('option');
            opt.disabled = true;
            opt.textContent = lang.name;
            sel.appendChild(opt);
        } else {
            const opt = document.createElement('option');
            opt.value = lang.code;
            opt.textContent = lang.name;
            if (lang.code === 'en') opt.selected = true;
            sel.appendChild(opt);
        }
    }
    sel.addEventListener('change', async () => {
        state.lang = sel.value;
        applyThemeForLanguage(sel.value);
        await loadLocale(sel.value);
    });
}

/**
 * Swap CSS custom properties when Minion language is selected.
 * Pixel-matched to bolus_gui.cpp apply_theme_colors() LANG_MINION branch.
 */
function applyThemeForLanguage(code) {
    const root = document.documentElement.style;
    if (code === 'minion') {
        // Minion Theme: Denim Blue + Minion Yellow
        root.setProperty('--bg-deep',           '#0D1B3A');       // Deep denim
        root.setProperty('--bg-primary',        '#FBD91C');       // Minion Yellow far bg
        root.setProperty('--bg-secondary',      '#142B61');       // Denim Blue panels
        root.setProperty('--bg-elevated',       '#0D1938');       // Dark denim frames
        root.setProperty('--bg-hover',          '#142B57');       // Denim hover
        root.setProperty('--bg-active',         '#1F3870');       // Denim active

        root.setProperty('--text-primary',      '#F2F2E6');
        root.setProperty('--text-secondary',    '#8090A6');
        root.setProperty('--text-muted',        '#607090');
        root.setProperty('--text-inverse',      '#0D1938');

        root.setProperty('--accent-burnt-orange', '#FBD91C');     // Minion Yellow accent
        root.setProperty('--accent-orange-hover', '#FFE64D');
        root.setProperty('--accent-orange-dim',   'rgba(251,217,28,0.35)');

        root.setProperty('--btn-sage',          '#CC9900');       // Rich Gold buttons
        root.setProperty('--btn-sage-hover',    '#E6B31A');
        root.setProperty('--btn-sage-active',   '#FFD933');

        root.setProperty('--grab-sage',         '#FBD91C');

        root.setProperty('--border-warm',       'rgba(251,217,28,0.50)');
        root.setProperty('--border-medium',     'rgba(251,217,28,0.70)');
        root.setProperty('--border-bright',     'rgba(251,217,28,0.80)');

        root.setProperty('--title-bg',          '#142B61');
        root.setProperty('--title-bg-active',   '#142B61');

        root.setProperty('--scrollbar-thumb',       '#CC9900');
        root.setProperty('--scrollbar-thumb-hover', '#E6B31A');
    } else {
        // Restore MCM theme (default values from style.css :root)
        root.setProperty('--bg-deep',           '#242420');
        root.setProperty('--bg-primary',        '#2e2e2b');
        root.setProperty('--bg-secondary',      '#383833');
        root.setProperty('--bg-elevated',       '#42403b');
        root.setProperty('--bg-hover',          '#524d47');
        root.setProperty('--bg-active',         '#615952');

        root.setProperty('--text-primary',      '#f2f0e6');
        root.setProperty('--text-secondary',    '#b5b0a5');
        root.setProperty('--text-muted',        '#99948c');
        root.setProperty('--text-inverse',      '#2e2e2b');

        root.setProperty('--accent-burnt-orange', '#E08C40');
        root.setProperty('--accent-orange-hover', '#f29d4f');
        root.setProperty('--accent-orange-dim',   'rgba(224,140,64,0.35)');

        root.setProperty('--btn-sage',          '#616b59');
        root.setProperty('--btn-sage-hover',    '#75856b');
        root.setProperty('--btn-sage-active',   '#8a9980');

        root.setProperty('--grab-sage',         '#809473');

        root.setProperty('--border-warm',       'rgba(89,82,71,0.50)');
        root.setProperty('--border-medium',     'rgba(89,82,71,0.70)');
        root.setProperty('--border-bright',     'rgba(224,140,64,0.50)');

        root.setProperty('--title-bg',          '#474038');
        root.setProperty('--title-bg-active',   '#52473d');

        root.setProperty('--scrollbar-thumb',       '#666157');
        root.setProperty('--scrollbar-thumb-hover', '#80766b');
    }
}

async function loadLocale(code) {
    const data = await window.bolusAPI.loadLocale(code);
    if (data) {
        state.tr = data;
        state.localeCode = code;
        // Toggle pIqaD font class for Klingon
        document.body.classList.toggle('locale-kl', code === 'kl');
        applyLocale();
    }
}

// Translate a raw QC flag (PASS/WARN/FAIL/STALL/REVIEW) to the current locale
function translateQC(flag) {
    const t = state.tr;
    if (!t) return flag;
    const upper = (flag || '').toUpperCase();
    if (upper === 'PASS') return t.qc_pass || flag;
    if (upper === 'WARN') return t.qc_warn || flag;
    if (upper === 'FAIL') return t.qc_fail || flag;
    if (upper === 'STALL') return t.qc_stall || flag;
    if (upper === 'REVIEW') return t.qc_review || flag;
    return flag;
}

// Translate a fit source to the current locale
function translateSource(src) {
    const t = state.tr;
    if (!t) return src;
    const lower = (src || '').toLowerCase();
    if (lower === 'auto') return t.source_auto || src;
    if (lower === 'manual') return t.source_manual || src;
    if (lower === 'override') return t.source_override || src;
    if (lower === 'prior') return t.source_prior || src;
    return src;
}

function applyLocale() {
    const t = state.tr;
    if (!t || !t.title_app) return;

    // Map locale keys to element IDs
    const map = {
        'app-title': t.title_app,
        'btn-load-data': t.text_load_subject_data,
        'btn-clear-data': t.btn_clear_data,
        'btn-save-state': t.btn_save_state,
        'btn-load-state': t.btn_load_state,
        'btn-save-csv': t.btn_save_csv,
        'btn-reset-all': t.btn_reset_all,
        'sidebar-title': t.sidebar_title,
        'label-filter': t.label_filter,
        'btn-prev-problem': t.btn_prev_problem,
        'btn-next-problem': t.btn_next_problem,
        'section-params-title': t.section_params,
        'col-variable': t.col_variable,
        'col-amplitude': t.col_amplitude,
        'col-t2p': t.col_t2p,
        'col-fwhm': t.col_fwhm,
        'col-baseline-h': t.col_baseline,
        'col-cnr': t.col_cnr,
        'col-onset-h': t.col_onset,
        'label-estimated-init': t.label_estimated_init,
        'label-fitted': t.label_fitted,
        'kinetics-title': t.text_kinetics_title,
        'label-denoise': t.label_denoise_strength,
        'btn-refit': t.btn_refit,
        'btn-override': t.btn_override,
        'btn-force-stall': t.btn_force_stall,
        'btn-revert': t.btn_revert,
        'btn-revert-loaded': t.btn_revert_loaded,
        'btn-reset-crop': t.btn_reset_crop,
        'btn-crop-bounds': t.btn_crop_bounds,
        'btn-view-mip': t.btn_view_roi_mip,
        'btn-prev-roi': t.btn_prev_short,
        'btn-next-roi': t.btn_next_short,
        'text-no-data': t.text_no_data,
        'title-roi-mip': t.title_roi_mip,
        'modal-reset-title': t.modal_reset_title,
        'modal-reset-desc': t.modal_reset_desc,
        'btn-reset-confirm': t.btn_reset_confirm,
        'btn-close-mip': t.btn_close_dialog,
        // New grid section headers
        'section-markers-title': t.section_markers,
        'section-crop-title': t.section_crop,
        'section-denoise-title': t.section_denoise,
        'section-fit-actions-title': t.section_actions,
        // Marker readout labels
        'label-onset-marker': t.label_onset,
        'label-peak-marker': t.label_peak,
        'label-end-marker': t.label_end,
        'label-base-marker': t.label_baseline,
        // Dataset info labels
        'label-dataset': t.label_dataset || 'Dataset:',
        'label-roi-count': t.label_roi_count || 'ROI Count:',
        'label-flagged': t.label_flagged || 'Flagged:',
        'label-manual': t.label_manual || 'Manual:',
        // Kinetics table headers
        'col-onset-scan': t.col_onset_scan,
        'col-tt-lower': t.col_tt_lower,
        'col-tt-peak': t.col_tt_peak,
        'col-tt-upper': t.col_tt_upper,
        'col-vessel-type': t.col_vessel_type,
    };

    for (const [id, text] of Object.entries(map)) {
        const el = document.getElementById(id);
        if (el && text) el.textContent = text;
    }

    // Translate filter dropdown options
    const filterSel = document.getElementById('filter-select');
    if (filterSel) {
        const opts = filterSel.options;
        for (let i = 0; i < opts.length; i++) {
            const v = opts[i].value;
            if (v === 'all' && t.filter_all) opts[i].textContent = t.filter_all;
            else if (v === 'flagged' && t.filter_flagged) opts[i].textContent = t.filter_flagged;
            else if (v === 'fail' && t.filter_fail) opts[i].textContent = t.filter_fail;
            else if (v === 'warn' && t.filter_warn) opts[i].textContent = t.filter_warn;
            else if (v === 'pass' && t.filter_pass) opts[i].textContent = t.filter_pass;
            else if (v === 'review' && t.filter_review) opts[i].textContent = t.filter_review;
            else if (v === 'stall' && t.filter_stall) opts[i].textContent = t.filter_stall;
        }
    }

    // Re-build sidebar to translate QC badges
    if (state.dataLoaded) {
        buildRoiList();
        updateSidebarCounts();
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// IPC HELPERS
// ═══════════════════════════════════════════════════════════════════════════

async function serverCmd(action, params = {}) {
    const resp = await window.bolusAPI.sendCommand(action, params);
    if (!resp.ok) {
        console.error(`[IPC] ${action} failed:`, resp.error);
    }
    return resp;
}

// ═══════════════════════════════════════════════════════════════════════════
// UI HELPERS
// ═══════════════════════════════════════════════════════════════════════════

function showToast(msg, durationMs = 3000) {
    const toast = document.getElementById('toast');
    toast.textContent = msg;
    toast.classList.remove('hidden');
    toast.classList.add('show');
    setTimeout(() => {
        toast.classList.remove('show');
        setTimeout(() => toast.classList.add('hidden'), 300);
    }, durationMs);
}

function showNoDataScreen() {
    document.getElementById('no-data-screen').classList.remove('hidden');
    document.getElementById('main-content').classList.add('hidden');
    document.getElementById('preflight-panel').classList.add('hidden');
    document.getElementById('batch-panel').classList.add('hidden');
}

function showMainContent() {
    document.getElementById('no-data-screen').classList.add('hidden');
    document.getElementById('main-content').classList.remove('hidden');
    document.getElementById('preflight-panel').classList.add('hidden');
    document.getElementById('batch-panel').classList.add('hidden');
}

function showPreflight() {
    document.getElementById('no-data-screen').classList.add('hidden');
    document.getElementById('main-content').classList.add('hidden');
    document.getElementById('preflight-panel').classList.remove('hidden');
    document.getElementById('batch-panel').classList.add('hidden');
}

function showBatchPanel() {
    document.getElementById('no-data-screen').classList.add('hidden');
    document.getElementById('main-content').classList.add('hidden');
    document.getElementById('preflight-panel').classList.add('hidden');
    document.getElementById('batch-panel').classList.remove('hidden');
}

function fmtVal(v, digits = 3) {
    if (v === null || v === undefined || isNaN(v)) return '—';
    return Number(v).toFixed(digits);
}

// ═══════════════════════════════════════════════════════════════════════════
// DATA LOADING PIPELINE
// ═══════════════════════════════════════════════════════════════════════════

async function loadSubjectFolder(folderPath) {
    state.datasetPath = folderPath;

    // Pre-flight scan
    showPreflight();
    const pfResults = document.getElementById('preflight-results');
    pfResults.innerHTML = '<div>Scanning folder...</div>';

    const scanResp = await serverCmd('scan_folder', { path: folderPath });
    if (!scanResp.ok) {
        pfResults.innerHTML = '<div class="preflight-warn">⚠ Pre-flight scan unavailable. Loading directly...</div>';
        const btnProceed = document.getElementById('btn-preflight-proceed');
        btnProceed.disabled = false;
        btnProceed.onclick = () => directLoad({ tiff: folderPath, roi: folderPath, csv: folderPath, fr: folderPath });
        return;
    }

    const scan = scanResp.data;
    const datasets = scan.datasets || [];

    let html = '<div style="font-family: monospace; font-size: 13px; line-height: 1.8;">';

    if (datasets.length === 0) {
        html += '<div class="preflight-error">❌ No bolus datasets found in this folder</div>';
    } else if (datasets.length === 1) {
        // Single dataset — show simple view
        const ds = datasets[0];
        html += `<div class="preflight-ok">✅ TIFF: ${ds.tiff_name || 'not found'}</div>`;
        html += `<div class="${ds.roi_path ? 'preflight-ok' : 'preflight-error'}">${ds.roi_path ? '✅' : '❌'} ROIs: ${ds.roi_name || 'not found'}</div>`;
        html += `<div class="${ds.csv_path ? 'preflight-ok' : 'preflight-warn'}">${ds.csv_path ? '✅' : '⚠'} CSV: ${ds.csv_name || 'will compute from scratch'}</div>`;
    } else {
        // Multiple datasets — show selection
        html += `<div style="margin-bottom:8px;color:var(--accent-gold);">📦 ${datasets.length} bolus datasets found — select one:</div>`;
        html += '<div id="dataset-selector" style="display:flex;flex-direction:column;gap:6px;">';
        // Default: first root-level bolus1, or first root-level, or first overall
        let defaultIdx = 0;
        for (let i = 0; i < datasets.length; i++) {
            if (datasets[i].bolus_id === 'bolus1' && datasets[i].is_root) { defaultIdx = i; break; }
            if (datasets[i].is_root && defaultIdx === 0) defaultIdx = i;
        }
        datasets.forEach((ds, i) => {
            const ready = ds.ready;
            const checked = (i === defaultIdx) ? 'checked' : '';
            const label = ds.label || ds.bolus_id;
            html += `<label style="display:flex;align-items:center;gap:8px;padding:6px 10px;background:var(--bg-panel);border-radius:var(--radius-sm);cursor:pointer;border:1px solid ${ready ? 'var(--accent-gold)' : 'var(--text-disabled)'};">`;
            html += `<input type="radio" name="dataset-choice" value="${i}" ${checked} ${ready ? '' : 'disabled'}>`;
            html += `<span style="font-weight:bold;color:${ready ? 'var(--text-primary)' : 'var(--text-disabled)'};">${label.toUpperCase()}</span>`;
            html += `<span style="color:var(--text-secondary);font-size:11px;">${ds.tiff_name || '(no TIFF)'}</span>`;
            if (!ready) html += '<span style="color:var(--text-disabled);font-size:10px;">(missing files)</span>';
            html += '</label>';
        });
        html += '</div>';
    }

    // Framerate
    if (scan.framerate_found) {
        html += `<div class="preflight-ok" style="margin-top:8px;">✅ Framerate file found</div>`;
    } else {
        html += '<div class="preflight-warn" style="margin-top:8px;">⚠ No framerate file (default 9.39 fps)</div>';
    }

    html += '</div>';

    // Overall status
    if (scan.ready) {
        html += '<div class="preflight-ok" style="margin-top: 10px; font-weight: bold;">✓ Ready to load</div>';
    } else {
        html += '<div class="preflight-error" style="margin-top: 10px; font-weight: bold;">✗ Missing required files — cannot proceed</div>';
    }

    pfResults.innerHTML = html;

    // Store datasets for selection
    state.availableDatasets = datasets;

    const btnProceed = document.getElementById('btn-preflight-proceed');
    btnProceed.disabled = !scan.ready;
    btnProceed.onclick = () => {
        // Get selected dataset
        let selectedIdx = 0;
        const radioEl = document.querySelector('input[name="dataset-choice"]:checked');
        if (radioEl) selectedIdx = parseInt(radioEl.value);

        const ds = datasets[selectedIdx] || datasets[0];
        directLoad({
            tiff: ds.tiff_path || folderPath,
            roi: ds.roi_path || folderPath,
            csv: ds.csv_path || folderPath,
            fr: scan.framerate_path || folderPath,
        });
    };
}

async function directLoad(paths) {
    showToast('Loading data...');

    // Load TIFF
    const tiffResp = await serverCmd('load_tiff', { path: paths.tiff });
    if (!tiffResp.ok) {
        showToast('Failed to load TIFF: ' + (tiffResp.error || 'unknown'));
        showNoDataScreen();
        return;
    }
    state.tiffWidth = tiffResp.data.width;
    state.tiffHeight = tiffResp.data.height;
    state.nFrames = tiffResp.data.n_frames;
    state.mipBase64 = tiffResp.data.mip_base64;

    // Load ROIs
    const roiResp = await serverCmd('load_rois', { path: paths.roi });
    if (!roiResp.ok) {
        showToast('Failed to load ROIs: ' + (roiResp.error || 'unknown'));
        showNoDataScreen();
        return;
    }

    // Load CSV if exists
    const csvResp = await serverCmd('load_csv', { path: paths.csv });
    if (csvResp.ok) {
        state.csvPath = csvResp.data.path || '';
        state.roiRecords = csvResp.data.records || [];
        // Build lookup map: roi_id -> record
        state.roiRecordMap = {};
        for (const rec of state.roiRecords) {
            if (rec.roi_id !== undefined) {
                state.roiRecordMap[rec.roi_id] = rec;
            }
        }
    }

    // Parse framerate
    const frResp = await serverCmd('parse_framerate', { path: paths.fr });
    const framerate = frResp.ok ? frResp.data.framerate : 9.39;

    // Compute traces
    showToast('Computing traces...');
    const traceResp = await serverCmd('compute_traces', {
        framerate: framerate,
        denoise_strength: 1.0,
    });
    if (!traceResp.ok) {
        showToast('Failed to compute traces: ' + (traceResp.error || 'unknown'));
        showNoDataScreen();
        return;
    }

    state.roiIds = traceResp.data.roi_ids;
    state.dataLoaded = true;

    // Build ROI list and show main content
    buildRoiList();
    updateSidebarCounts();
    showMainContent();

    // Select first ROI
    if (state.roiIds.length > 0) {
        selectRoi(0);
    }

    showToast(`Loaded ${state.roiIds.length} ROIs (${state.nFrames} frames)`);
}

// ═══════════════════════════════════════════════════════════════════════════
// ROI LIST & SIDEBAR
// ═══════════════════════════════════════════════════════════════════════════

function buildRoiList() {
    const container = document.getElementById('roi-list');
    container.innerHTML = '';
    applyFilter();

    for (const idx of state.filteredIndices) {
        const roiId = state.roiIds[idx];
        const rec = (state.roiRecordMap || {})[roiId] || {};
        const qcFlag = rec.qc_flag || '—';
        const qcDisplay = translateQC(qcFlag);

        const item = document.createElement('div');
        item.className = 'roi-item';
        item.dataset.index = idx;

        const badgeClass = `qc-${qcFlag.toLowerCase()}`;

        item.innerHTML = `
            <div>
                <span class="roi-item-id">ROI ${roiId}</span>
                <span class="roi-item-info">${rec.ves_type || ''}</span>
            </div>
            <span class="qc-badge ${badgeClass}">${qcDisplay}</span>
        `;

        item.addEventListener('click', () => selectRoi(idx));
        container.appendChild(item);
    }
}

function applyFilter() {
    const mode = state.filterMode;
    state.filteredIndices = [];
    for (let i = 0; i < state.roiIds.length; i++) {
        const rec = state.roiRecords[i] || {};
        const qc = (rec.qc_flag || '').toUpperCase();
        if (mode === 'all') state.filteredIndices.push(i);
        else if (mode === 'flagged' && ['FAIL','WARN','REVIEW','STALL'].includes(qc)) state.filteredIndices.push(i);
        else if (mode === 'fail' && qc === 'FAIL') state.filteredIndices.push(i);
        else if (mode === 'warn' && qc === 'WARN') state.filteredIndices.push(i);
        else if (mode === 'pass' && qc === 'PASS') state.filteredIndices.push(i);
        else if (mode === 'review' && qc === 'REVIEW') state.filteredIndices.push(i);
        else if (mode === 'stall' && qc === 'STALL') state.filteredIndices.push(i);
    }
    state.triagePos = 0;
    updateTriageInfo();
}

function updateSidebarCounts() {
    const t = state.tr || {};
    const total = state.roiIds.length;
    const active = state.filteredIndices.length;
    const manual = state.roiRecords.filter(r => r && r.fit_source === 'manual').length;
    const sidebarTpl = t.text_sidebar_counts || 'ROIs: %d | Active: %d | Manual: %d';
    document.getElementById('sidebar-counts').textContent =
        sidebarTpl.replace('%d', total).replace('%d', active).replace('%d', manual);
    document.getElementById('val-roi-count').textContent = total;
    document.getElementById('val-flagged').textContent = state.roiRecords.filter(r => r && ['FAIL','WARN','STALL'].includes((r.qc_flag||'').toUpperCase())).length;
    document.getElementById('val-manual').textContent = manual;

    const pathParts = (state.datasetPath || '').split('/');
    document.getElementById('val-dataset').textContent = pathParts[pathParts.length - 1] || '—';
}

function updateTriageInfo() {
    document.getElementById('triage-queue-info').textContent =
        `${state.triagePos + 1} / ${state.filteredIndices.length}`;
}

// ═══════════════════════════════════════════════════════════════════════════
// ROI SELECTION & PLOT
// ═══════════════════════════════════════════════════════════════════════════

async function selectRoi(idx) {
    state.selectedRoiIdx = idx;

    // Highlight in sidebar
    document.querySelectorAll('.roi-item').forEach(el => {
        el.classList.toggle('selected', parseInt(el.dataset.index) === idx);
    });

    // Update triage position
    const triageIdx = state.filteredIndices.indexOf(idx);
    if (triageIdx >= 0) state.triagePos = triageIdx;
    updateTriageInfo();

    // Reset marker state for new ROI (so server values load fresh)
    state.onsetMarker = 0;
    state.peakMarker = 0;
    state.endMarker = 0;
    state.baselineMarker = 0;
    state.cropMin = 0;
    state.cropMax = 100;

    // Get trace data (lightweight — just for params, C++ renders the plot)
    const traceResp = await serverCmd('get_trace', { roi_index: idx });
    if (!traceResp.ok) {
        showToast('Failed to get trace: ' + traceResp.error);
        return;
    }
    const trace = traceResp.data;

    // Update status header
    const roiId = state.roiIds[idx];
    const rec = (state.roiRecordMap || {})[roiId] || {};
    const qc = rec.qc_flag || '—';
    const src = rec.fit_source || '—';
    const t = state.tr || {};
    const statusTpl = t.text_plot_status_header || 'Signal Time Series (SU) - ROI #%d | Status: %s (Source: %s)';
    document.getElementById('roi-status-text').textContent =
        statusTpl.replace('%d', trace.roi_id).replace('%s', translateQC(qc)).replace('%s', translateSource(src));

    // Update parameters table — clearly separate Initial vs Fitted (BUG-7 fix)
    document.getElementById('val-est-amp').textContent = fmtVal(rec.init_amp);
    document.getElementById('val-est-t2p').textContent = fmtVal(rec.init_t2p);
    document.getElementById('val-est-fwhm').textContent = fmtVal(rec.init_fwhm);
    document.getElementById('val-est-base').textContent = fmtVal(rec.init_m);
    document.getElementById('val-est-cnr').textContent = fmtVal(rec.init_cnr);
    document.getElementById('val-est-onset').textContent = fmtVal(rec.ont);

    document.getElementById('val-fit-amp').textContent = fmtVal(rec.f_amp);
    document.getElementById('val-fit-t2p').textContent = fmtVal(rec.f_t2p);
    document.getElementById('val-fit-fwhm').textContent = fmtVal(rec.f_fwhm);
    document.getElementById('val-fit-base').textContent = fmtVal(rec.f_m);
    document.getElementById('val-fit-cnr').textContent = fmtVal(rec.f_cnr);
    document.getElementById('val-fit-onset').textContent = fmtVal(rec.ont);

    // Kinetics table
    document.getElementById('val-onset-scan').textContent = fmtVal(rec.ont_sc);
    document.getElementById('val-tt-lower').textContent = fmtVal(rec.ttlb);
    document.getElementById('val-tt-peak').textContent = fmtVal(rec.ttm);
    document.getElementById('val-tt-upper').textContent = fmtVal(rec.tthb);
    document.getElementById('val-auc').textContent = fmtVal(rec.auc, 1);
    document.getElementById('val-aucn').textContent = fmtVal(rec.aucn, 3);
    document.getElementById('val-vessel-type').textContent = rec.ves_type || '—';

    // Request SVG plot from C++
    await renderPlot(idx);
}

async function renderPlot(idx) {
    const container = document.getElementById('plot-container');
    const containerW = container.clientWidth || 900;
    const containerH = container.clientHeight || 400;

    const resp = await serverCmd('render_plot', {
        roi_index: idx,
        crop_min: state.cropMin,
        crop_max: state.cropMax,
        width: containerW,
        height: containerH,
        onset: state.onsetMarker > 0 ? state.onsetMarker : undefined,
        peak: state.peakMarker > 0 ? state.peakMarker : undefined,
        end_t: state.endMarker > 0 ? state.endMarker : undefined,
    });

    if (resp.ok && resp.data && resp.data.svg) {
        document.getElementById('svg-plot').innerHTML = resp.data.svg;

        // Position draggable markers using coordinate mapping
        if (resp.data.coord && resp.data.markers) {
            const coord = resp.data.coord;
            const markers = resp.data.markers;

            state.plotCoord = coord;  // Store for drag calculations

            // Store marker values from server ONLY if not already set by user drag
            if (!state.onsetMarker && markers.onset > 0) state.onsetMarker = markers.onset;
            if (!state.peakMarker && markers.peak > 0) state.peakMarker = markers.peak;
            if (!state.endMarker && markers.end > 0) state.endMarker = markers.end;
            if (!state.baselineMarker && markers.baseline) state.baselineMarker = markers.baseline;

            // Helper: convert time value to pixel X position (% of container)
            const tToPx = (t) => {
                const frac = (t - coord.min_x) / (coord.max_x - coord.min_x);
                return coord.pad_l + frac * (coord.svg_w - coord.pad_l - coord.pad_r);
            };

            // Position onset marker (use state value to preserve drags)
            const onsetVal = state.onsetMarker || markers.onset;
            const onsetEl = document.getElementById('marker-onset');
            if (onsetVal > 0 && onsetVal >= coord.min_x && onsetVal <= coord.max_x) {
                const px = tToPx(onsetVal);
                onsetEl.style.left = px + 'px';
                onsetEl.classList.add('active');
                document.getElementById('label-onset-val').textContent = onsetVal.toFixed(1) + 's';
            } else {
                onsetEl.classList.remove('active');
            }

            // Position peak marker
            const peakVal = state.peakMarker || markers.peak;
            const peakEl = document.getElementById('marker-peak');
            if (peakVal > 0 && peakVal >= coord.min_x && peakVal <= coord.max_x) {
                const px = tToPx(peakVal);
                peakEl.style.left = px + 'px';
                peakEl.classList.add('active');
                document.getElementById('label-peak-val').textContent = peakVal.toFixed(1) + 's';
            } else {
                peakEl.classList.remove('active');
            }

            // Position end marker
            const endVal = state.endMarker || markers.end;
            const endEl = document.getElementById('marker-end');
            if (endVal > 0 && endVal >= coord.min_x && endVal <= coord.max_x) {
                const px = tToPx(endVal);
                endEl.style.left = px + 'px';
                endEl.classList.add('active');
                document.getElementById('label-end-val').textContent = endVal.toFixed(1) + 's';
            } else {
                endEl.classList.remove('active');
            }

            // Position baseline marker (horizontal) — use state to preserve drags
            const baseVal = state.baselineMarker || markers.baseline;
            const baseEl = document.getElementById('marker-baseline');
            if (baseVal && !isNaN(baseVal)) {
                const frac = (baseVal - coord.min_y) / (coord.max_y - coord.min_y);
                const py = coord.svg_h - coord.pad_b - frac * (coord.svg_h - coord.pad_t - coord.pad_b);
                baseEl.style.bottom = 'auto';
                baseEl.style.top = py + 'px';
                baseEl.classList.add('active');
                document.getElementById('label-base-val').textContent = baseVal.toFixed(1);
            } else {
                baseEl.classList.remove('active');
            }

            // Update marker readout values in controls grid
            const ro = document.getElementById('readout-onset-val');
            const rp = document.getElementById('readout-peak-val');
            const re = document.getElementById('readout-end-val');
            const rb = document.getElementById('readout-base-val');
            if (ro) ro.textContent = onsetVal > 0 ? onsetVal.toFixed(1) : '—';
            if (rp) rp.textContent = peakVal > 0 ? peakVal.toFixed(1) : '—';
            if (re) re.textContent = endVal > 0 ? endVal.toFixed(1) : '—';
            if (rb) rb.textContent = (baseVal && !isNaN(baseVal)) ? baseVal.toFixed(1) : '—';

            // Update crop range label with actual time values
            const cropLabel = document.getElementById('crop-range-label');
            if (cropLabel) {
                const t = state.tr || {};
                const tpl = t.text_visual_crop_range || 'Visual Crop Range';
                const minT = coord.min_x.toFixed(1);
                const maxT = coord.max_x.toFixed(1);
                cropLabel.textContent = `${tpl}: ${minT}s - ${maxT}s`;
            }

            // Update ROI position counter
            const posInfo = document.getElementById('roi-position-info');
            if (posInfo) {
                posInfo.textContent = `${state.selectedRoiIdx + 1} / ${state.roiIds.length}`;
            }
        }
    } else {
        document.getElementById('svg-plot').innerHTML =
            `<div style="padding:40px;text-align:center;color:#99948c;">
                Plot will appear here once render_plot is implemented in bolus_server.
                <br>ROI index: ${idx}
            </div>`;
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// DRAGGABLE MARKERS
// ═══════════════════════════════════════════════════════════════════════════

function initMarkerDrag() {
    const container = document.getElementById('plot-container');
    let dragging = null;  // 'onset', 'peak', 'end', 'baseline'

    const pxToT = (px) => {
        const c = state.plotCoord;
        if (!c) return 0;
        const frac = (px - c.pad_l) / (c.svg_w - c.pad_l - c.pad_r);
        return c.min_x + frac * (c.max_x - c.min_x);
    };

    const startDrag = (name) => (e) => {
        e.preventDefault();
        dragging = name;
        document.body.style.cursor = name === 'baseline' ? 'row-resize' : 'col-resize';
    };

    document.getElementById('marker-onset').addEventListener('mousedown', startDrag('onset'));
    document.getElementById('marker-peak').addEventListener('mousedown', startDrag('peak'));
    document.getElementById('marker-end').addEventListener('mousedown', startDrag('end'));
    document.getElementById('marker-baseline').addEventListener('mousedown', startDrag('baseline'));

    document.addEventListener('mousemove', (e) => {
        if (!dragging || !state.plotCoord) return;
        const rect = container.getBoundingClientRect();

        if (dragging === 'baseline') {
            const py = e.clientY - rect.top;
            const c = state.plotCoord;
            const frac = (c.svg_h - c.pad_b - py) / (c.svg_h - c.pad_t - c.pad_b);
            state.baselineMarker = c.min_y + frac * (c.max_y - c.min_y);
            const el = document.getElementById('marker-baseline');
            el.style.top = py + 'px';
            document.getElementById('label-base-val').textContent = state.baselineMarker.toFixed(1);
        } else {
            const px = e.clientX - rect.left;
            const t = pxToT(px);
            const el = document.getElementById('marker-' + dragging);
            el.style.left = px + 'px';

            if (dragging === 'onset') {
                state.onsetMarker = t;
                document.getElementById('label-onset-val').textContent = t.toFixed(1) + 's';
            } else if (dragging === 'peak') {
                state.peakMarker = t;
                document.getElementById('label-peak-val').textContent = t.toFixed(1) + 's';
            } else if (dragging === 'end') {
                state.endMarker = t;
                document.getElementById('label-end-val').textContent = t.toFixed(1) + 's';
            }
        }
    });

    document.addEventListener('mouseup', async () => {
        if (!dragging) return;
        const wasDragging = dragging;
        dragging = null;
        document.body.style.cursor = '';

        // Re-render plot with updated marker positions
        if (state.selectedRoiIdx >= 0) {
            await renderPlot(state.selectedRoiIdx);
        }
    });
}

// ═══════════════════════════════════════════════════════════════════════════
// MIP VIEWER
// ═══════════════════════════════════════════════════════════════════════════

function showMipModal() {
    if (!state.mipBase64) return;

    const modal = document.getElementById('mip-modal');
    modal.classList.remove('hidden');

    const canvas = document.getElementById('mip-canvas');
    const ctx = canvas.getContext('2d');

    // Decode base64 to grayscale pixels
    const raw = atob(state.mipBase64);
    const pixels = new Uint8Array(raw.length);
    for (let i = 0; i < raw.length; i++) pixels[i] = raw.charCodeAt(i);

    const w = state.tiffWidth;
    const h = state.tiffHeight;
    canvas.width = w;
    canvas.height = h;

    const imgData = ctx.createImageData(w, h);
    for (let i = 0; i < pixels.length; i++) {
        imgData.data[i*4]   = pixels[i];
        imgData.data[i*4+1] = pixels[i];
        imgData.data[i*4+2] = pixels[i];
        imgData.data[i*4+3] = 255;
    }
    ctx.putImageData(imgData, 0, 0);

    // Release reference to free memory
    state.mipBase64 = '';
}

// ═══════════════════════════════════════════════════════════════════════════
// ACTION HANDLERS
// ═══════════════════════════════════════════════════════════════════════════

async function handleRefit() {
    if (state.selectedRoiIdx < 0) return;
    showToast('Re-fitting...');
    const resp = await serverCmd('run_fit', {
        roi_index: state.selectedRoiIdx,
        onset: state.onsetMarker,
        peak: state.peakMarker,
        end: state.endMarker,
        baseline: state.baselineMarker,
    });
    if (resp.ok && resp.data) {
        const d = resp.data;
        const roiId = state.roiIds[state.selectedRoiIdx];

        // Build a record-like object from the refit response
        const newRec = {
            ...(state.roiRecordMap || {})[roiId] || {},
            roi_id: roiId,
            f_amp: d.params?.[0],
            f_t2p: d.params?.[1],
            f_fwhm: d.params?.[2],
            f_m: d.params?.[3],
            f_cnr: d.cnr,
            qc_flag: d.qc_flag,
            fit_source: 'manual',
            click_onset: d.onset,
            click_peak: d.peak,
            click_end: d.end,
        };

        // Update the record map
        if (!state.roiRecordMap) state.roiRecordMap = {};
        state.roiRecordMap[roiId] = newRec;

        // Set markers from the refit response (don't reset to 0)
        if (d.onset > 0) state.onsetMarker = d.onset;
        if (d.peak > 0) state.peakMarker = d.peak;
        if (d.end > 0) state.endMarker = d.end;
        if (d.baseline) state.baselineMarker = d.baseline;

        // Update parameter display and re-render plot (without resetting markers)
        const rec = newRec;
        document.getElementById('val-fit-amp').textContent = fmtVal(rec.f_amp);
        document.getElementById('val-fit-t2p').textContent = fmtVal(rec.f_t2p);
        document.getElementById('val-fit-fwhm').textContent = fmtVal(rec.f_fwhm);
        document.getElementById('val-fit-base').textContent = fmtVal(rec.f_m);
        document.getElementById('val-fit-cnr').textContent = fmtVal(rec.f_cnr);

        // Re-render plot with current marker positions
        await renderPlot(state.selectedRoiIdx);
        buildRoiList();
        showToast('Re-fit complete');
    } else {
        showToast('Re-fit failed: ' + (resp.error || 'unknown'));
    }
}

async function handleSaveCsv() {
    const resp = await serverCmd('save_csv', {});
    if (resp.ok) {
        playSound('hallelujah');
        showToast(state.tr.text_save_csv_msg
            ? state.tr.text_save_csv_msg.replace('%s', resp.data?.path || '')
            : `Results saved to: ${resp.data?.path || ''}`);
    } else {
        showToast('Save failed: ' + (resp.error || 'unknown'));
    }
}

async function handleSaveState() {
    const resp = await serverCmd('save_state', {});
    if (resp.ok) {
        playSound('hallelujah');
        showToast('State saved');
    }
}

async function handleLoadState() {
    const resp = await serverCmd('load_state', {});
    if (resp.ok) {
        showToast('State loaded');
        if (state.selectedRoiIdx >= 0) await selectRoi(state.selectedRoiIdx);
    }
}

async function handleForceQC(flag) {
    if (state.selectedRoiIdx < 0) return;
    const rec = state.roiRecords[state.selectedRoiIdx];
    if (rec) {
        rec.qc_flag = flag;
        rec.fit_source = 'manual';
    } else {
        state.roiRecords[state.selectedRoiIdx] = {
            roi_id: state.roiIds[state.selectedRoiIdx],
            qc_flag: flag,
            fit_source: 'manual'
        };
    }
    if (state.roiRecordMap) {
        state.roiRecordMap[state.roiIds[state.selectedRoiIdx]] = state.roiRecords[state.selectedRoiIdx];
    }
    await selectRoi(state.selectedRoiIdx);
    buildRoiList();
    updateSidebarCounts();
    const t = state.tr || {};
    showToast((t['toast_forced'] || 'Forced %s').replace('%s', flag));
}

function handleResetAll() {
    document.getElementById('reset-modal').classList.remove('hidden');
}

// ═══════════════════════════════════════════════════════════════════════════
// EVENT BINDINGS
// ═══════════════════════════════════════════════════════════════════════════

function bindEvents() {
    // Top bar
    document.getElementById('btn-load-data').addEventListener('click', async () => {
        const folder = await window.bolusAPI.openFolderDialog();
        if (folder) await loadSubjectFolder(folder);
    });
    document.getElementById('btn-clear-data').addEventListener('click', () => {
        state.dataLoaded = false;
        state.roiIds = [];
        state.roiRecords = [];
        state.selectedRoiIdx = -1;
        showNoDataScreen();
    });
    document.getElementById('btn-save-csv').addEventListener('click', handleSaveCsv);
    document.getElementById('btn-save-state').addEventListener('click', handleSaveState);
    document.getElementById('btn-load-state').addEventListener('click', handleLoadState);
    document.getElementById('btn-reset-all').addEventListener('click', handleResetAll);

    // Reset modal
    document.getElementById('btn-reset-confirm').addEventListener('click', async () => {
        const resp = await serverCmd('reset_all', {});
        if (resp.ok) {
            showToast('All changes reset');
            if (state.selectedRoiIdx >= 0) await selectRoi(state.selectedRoiIdx);
            buildRoiList();
        }
        document.getElementById('reset-modal').classList.add('hidden');
    });
    document.getElementById('btn-reset-cancel').addEventListener('click', () => {
        document.getElementById('reset-modal').classList.add('hidden');
    });

    // Actions
    document.getElementById('btn-refit').addEventListener('click', handleRefit);
    document.getElementById('btn-override').addEventListener('click', () => handleForceQC('PASS'));
    document.getElementById('btn-force-stall').addEventListener('click', () => handleForceQC('STALL'));
    document.getElementById('btn-view-mip').addEventListener('click', showMipModal);
    document.getElementById('btn-close-mip').addEventListener('click', () => {
        document.getElementById('mip-modal').classList.add('hidden');
    });

    // Navigation
    document.getElementById('btn-prev-roi').addEventListener('click', () => {
        if (state.selectedRoiIdx > 0) selectRoi(state.selectedRoiIdx - 1);
    });
    document.getElementById('btn-next-roi').addEventListener('click', () => {
        if (state.selectedRoiIdx < state.roiIds.length - 1) selectRoi(state.selectedRoiIdx + 1);
    });
    document.getElementById('btn-prev-problem').addEventListener('click', () => {
        if (state.triagePos > 0) {
            state.triagePos--;
            selectRoi(state.filteredIndices[state.triagePos]);
        }
    });
    document.getElementById('btn-next-problem').addEventListener('click', () => {
        if (state.triagePos < state.filteredIndices.length - 1) {
            state.triagePos++;
            selectRoi(state.filteredIndices[state.triagePos]);
        }
    });

    // Filter
    document.getElementById('filter-select').addEventListener('change', (e) => {
        state.filterMode = e.target.value;
        buildRoiList();
        updateSidebarCounts();
    });

    // Denoise slider
    document.getElementById('denoise-slider').addEventListener('input', (e) => {
        document.getElementById('denoise-value').textContent = Number(e.target.value).toFixed(1);
    });

    // Dual-thumb crop range slider
    function updateCropFill() {
        const fill = document.getElementById('dual-range-fill');
        if (fill) {
            fill.style.left = state.cropMin + '%';
            fill.style.width = (state.cropMax - state.cropMin) + '%';
        }
    }
    document.getElementById('crop-min').addEventListener('input', (e) => {
        state.cropMin = Math.min(Number(e.target.value), state.cropMax - 1);
        e.target.value = state.cropMin;
        updateCropFill();
        if (state.selectedRoiIdx >= 0) renderPlot(state.selectedRoiIdx);
    });
    document.getElementById('crop-max').addEventListener('input', (e) => {
        state.cropMax = Math.max(Number(e.target.value), state.cropMin + 1);
        e.target.value = state.cropMax;
        updateCropFill();
        if (state.selectedRoiIdx >= 0) renderPlot(state.selectedRoiIdx);
    });
    updateCropFill();

    // Preflight cancel
    document.getElementById('btn-preflight-cancel').addEventListener('click', showNoDataScreen);

    // Keyboard shortcuts
    document.addEventListener('keydown', (e) => {
        if (!state.dataLoaded) return;
        if (e.target.tagName === 'INPUT' || e.target.tagName === 'SELECT') return;

        if (e.key === 'ArrowRight' || e.key === 'n') {
            if (state.selectedRoiIdx < state.roiIds.length - 1) selectRoi(state.selectedRoiIdx + 1);
        } else if (e.key === 'ArrowLeft' || e.key === 'p') {
            if (state.selectedRoiIdx > 0) selectRoi(state.selectedRoiIdx - 1);
        } else if (e.key === 'r') {
            handleRefit();
        }
    });
}

// ═══════════════════════════════════════════════════════════════════════════
// BATCH PROCESSING PANEL
// ═══════════════════════════════════════════════════════════════════════════

const batchState = {
    folder: null,
    running: false,
};

function initBatchPanel() {
    const terminal  = document.getElementById('batch-terminal');
    const folderEl  = document.getElementById('batch-folder-path');

    // Show batch panel from no-data screen
    document.getElementById('btn-show-batch').addEventListener('click', showBatchPanel);

    // Back to triage
    document.getElementById('btn-batch-back').addEventListener('click', showNoDataScreen);

    // Browse for folder
    document.getElementById('btn-batch-browse').addEventListener('click', async () => {
        const folder = await window.bolusAPI.openFolderDialog();
        if (folder) {
            batchState.folder = folder;
            folderEl.textContent = folder;
            // Enable action buttons
            document.getElementById('btn-batch-preflight').disabled = false;
            document.getElementById('btn-batch-prepare').disabled = false;
            document.getElementById('btn-batch-prepare-apply').disabled = false;
            document.getElementById('btn-batch-run').disabled = false;
        }
    });

    // Pre-flight
    document.getElementById('btn-batch-preflight').addEventListener('click', () => {
        runBatchPipeline(['--folder', batchState.folder, '--preflight']);
    });

    // Prepare (dry run)
    document.getElementById('btn-batch-prepare').addEventListener('click', () => {
        runBatchPipeline(['--folder', batchState.folder, '--prepare']);
    });

    // Prepare (apply)
    document.getElementById('btn-batch-prepare-apply').addEventListener('click', () => {
        runBatchPipeline(['--folder', batchState.folder, '--prepare', '--apply']);
    });

    // Full run
    document.getElementById('btn-batch-run').addEventListener('click', () => {
        const args = ['--folder', batchState.folder];
        if (document.getElementById('batch-opt-plot').checked) args.push('--plot');
        if (document.getElementById('batch-opt-verbose').checked) args.push('--verbose');
        runBatchPipeline(args);
    });

    // Stop
    document.getElementById('btn-batch-stop').addEventListener('click', async () => {
        await window.bolusAPI.killPipeline();
        appendBatchLine('\n— Pipeline cancelled by user —', 'warn');
        setBatchRunning(false);
    });

    // Clear
    document.getElementById('btn-batch-clear').addEventListener('click', () => {
        terminal.innerHTML = '<div class="batch-terminal-placeholder">Output cleared.</div>';
    });

    // Pipeline output listener
    window.bolusAPI.onPipelineOutput(({ stream, line }) => {
        const cls = classifyLine(line, stream);
        appendBatchLine(line, cls);
    });

    // Pipeline done listener
    window.bolusAPI.onPipelineDone(({ code, error }) => {
        if (code === 0) {
            appendBatchLine('\n✓ Pipeline completed successfully.', 'ok');
            playSound('hallelujah');
        } else if (error) {
            appendBatchLine(`\n✗ Pipeline error: ${error}`, 'error');
        } else {
            appendBatchLine(`\n✗ Pipeline exited with code ${code}`, 'error');
        }
        setBatchRunning(false);
    });
}

async function runBatchPipeline(args) {
    const terminal = document.getElementById('batch-terminal');
    terminal.innerHTML = '';  // Clear
    appendBatchLine(`▶ Running: bolus_tracking_cpp ${args.join(' ')}`, 'header');
    appendBatchLine('', 'info');

    setBatchRunning(true);
    const result = await window.bolusAPI.runPipeline(args);
    if (!result.ok) {
        appendBatchLine(`✗ Failed to start: ${result.error}`, 'error');
        setBatchRunning(false);
    }
}

function setBatchRunning(running) {
    batchState.running = running;
    const btns = ['btn-batch-preflight', 'btn-batch-prepare', 'btn-batch-prepare-apply', 'btn-batch-run'];
    btns.forEach(id => document.getElementById(id).disabled = running);
    const stopBtn = document.getElementById('btn-batch-stop');
    if (running) {
        stopBtn.classList.remove('hidden');
    } else {
        stopBtn.classList.add('hidden');
    }
}

function appendBatchLine(text, cls) {
    const terminal = document.getElementById('batch-terminal');
    // Remove placeholder if present
    const ph = terminal.querySelector('.batch-terminal-placeholder');
    if (ph) ph.remove();

    const line = document.createElement('div');
    if (cls) line.className = `batch-line-${cls}`;
    line.textContent = text;
    terminal.appendChild(line);

    // Auto-scroll to bottom
    terminal.scrollTop = terminal.scrollHeight;
}

function classifyLine(line, stream) {
    if (stream === 'stderr') return 'error';
    const lower = line.toLowerCase();
    if (lower.includes('error') || lower.includes('failed') || lower.includes('✗')) return 'error';
    if (lower.includes('warning') || lower.includes('⚠') || lower.includes('missing')) return 'warn';
    if (lower.includes('✓') || lower.includes('complete') || lower.includes('success') || lower.includes('pass')) return 'ok';
    if (lower.startsWith('===') || lower.startsWith('---') || lower.startsWith('>>>')) return 'header';
    return '';
}

// ═══════════════════════════════════════════════════════════════════════════
// INITIALIZATION
// ═══════════════════════════════════════════════════════════════════════════

async function init() {
    await initSounds();
    initLanguageSelector();
    initSqueakListeners();
    initBatchPanel();
    bindEvents();
    initMarkerDrag();
    runSplashScreen();

    // Ping server
    const ping = await serverCmd('ping');
    if (!ping.ok) {
        showToast('Warning: bolus_server not responding', 5000);
    }
}

// Start when DOM is ready
if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', init);
} else {
    init();
}
