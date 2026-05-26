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
        await loadLocale(sel.value);
    });
}

async function loadLocale(code) {
    const data = await window.bolusAPI.loadLocale(code);
    if (data) {
        state.tr = data;
        applyLocale();
    }
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
        'section-actions-title': t.title_manual_override,
        'manual-override-desc': t.text_manual_override_desc,
        'label-denoise': t.label_denoise_strength,
        'btn-refit': t.btn_refit,
        'btn-override': t.btn_override,
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
        'label-crop-range': t.text_visual_crop_range,
    };

    for (const [id, text] of Object.entries(map)) {
        const el = document.getElementById(id);
        if (el && text) el.textContent = text;
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
}

function showMainContent() {
    document.getElementById('no-data-screen').classList.add('hidden');
    document.getElementById('main-content').classList.remove('hidden');
    document.getElementById('preflight-panel').classList.add('hidden');
}

function showPreflight() {
    document.getElementById('no-data-screen').classList.add('hidden');
    document.getElementById('main-content').classList.add('hidden');
    document.getElementById('preflight-panel').classList.remove('hidden');
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
        pfResults.innerHTML = `<div class="preflight-error">❌ Failed to scan folder: ${scanResp.error}</div>`;
        return;
    }

    const scan = scanResp.data;

    // If scan_folder doesn't exist yet, fall back to direct loading
    if (!scan) {
        pfResults.innerHTML = '<div class="preflight-warn">⚠ Pre-flight scan not available. Loading directly...</div>';
        await directLoad(folderPath);
        return;
    }

    // Display pre-flight results
    let html = '';
    for (const ds of scan.datasets || []) {
        const icon = ds.ready ? '✓' : '✗';
        const cls = ds.ready ? 'preflight-ok' : 'preflight-error';
        html += `<div class="${cls}">${icon} ${ds.name}: ${ds.status}</div>`;
    }
    if (scan.warnings) {
        for (const w of scan.warnings) {
            html += `<div class="preflight-warn">⚠ ${w}</div>`;
        }
    }
    pfResults.innerHTML = html || '<div class="preflight-ok">✓ Ready to process</div>';

    const btnProceed = document.getElementById('btn-preflight-proceed');
    btnProceed.disabled = false;
    btnProceed.onclick = () => directLoad(folderPath);
}

async function directLoad(folderPath) {
    showToast('Loading data...');

    // Load TIFF
    const tiffResp = await serverCmd('load_tiff', { path: folderPath });
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
    const roiResp = await serverCmd('load_rois', { path: folderPath });
    if (!roiResp.ok) {
        showToast('Failed to load ROIs: ' + (roiResp.error || 'unknown'));
        showNoDataScreen();
        return;
    }

    // Load CSV if exists
    const csvResp = await serverCmd('load_csv', { path: folderPath });
    if (csvResp.ok) {
        state.csvPath = csvResp.data.path || '';
        state.roiRecords = csvResp.data.records || [];
    }

    // Parse framerate
    const frResp = await serverCmd('parse_framerate', { path: folderPath });
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
        const rec = state.roiRecords[idx] || {};
        const qcFlag = rec.qc_flag || '—';

        const item = document.createElement('div');
        item.className = 'roi-item';
        item.dataset.index = idx;

        const badgeClass = `qc-${qcFlag.toLowerCase()}`;

        item.innerHTML = `
            <div>
                <span class="roi-item-id">ROI ${roiId}</span>
                <span class="roi-item-info">${rec.ves_type || ''}</span>
            </div>
            <span class="qc-badge ${badgeClass}">${qcFlag}</span>
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
        else if (mode === 'flagged' && ['FAIL','WARN','REVIEW'].includes(qc)) state.filteredIndices.push(i);
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
    const total = state.roiIds.length;
    const active = state.filteredIndices.length;
    const manual = state.roiRecords.filter(r => r && r.fit_source === 'manual').length;
    document.getElementById('sidebar-counts').textContent = `ROIs: ${total} | Active: ${active} | Manual: ${manual}`;
    document.getElementById('val-roi-count').textContent = total;
    document.getElementById('val-flagged').textContent = state.roiRecords.filter(r => r && ['FAIL','WARN'].includes((r.qc_flag||'').toUpperCase())).length;
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

    // Get trace data (lightweight — just for params, C++ renders the plot)
    const traceResp = await serverCmd('get_trace', { roi_index: idx });
    if (!traceResp.ok) {
        showToast('Failed to get trace: ' + traceResp.error);
        return;
    }
    const trace = traceResp.data;

    // Update status header
    const rec = state.roiRecords[idx] || {};
    const qc = rec.qc_flag || '—';
    const src = rec.fit_source || '—';
    document.getElementById('roi-status-text').textContent =
        `Signal Time Series (SU) - ROI #${trace.roi_id} | Status: ${qc} (Source: ${src})`;

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
    const resp = await serverCmd('render_plot', {
        roi_index: idx,
        crop_min: state.cropMin,
        crop_max: state.cropMax,
        width: 900,
        height: 400,
        theme: 'dark',
    });

    if (resp.ok && resp.data && resp.data.svg) {
        document.getElementById('svg-plot').innerHTML = resp.data.svg;
    } else {
        // Fallback: show a placeholder if render_plot not yet implemented
        document.getElementById('svg-plot').innerHTML =
            `<div style="padding:40px;text-align:center;color:#99948c;">
                Plot will appear here once render_plot is implemented in bolus_server.
                <br>ROI index: ${idx}
            </div>`;
    }
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
    if (resp.ok) {
        if (resp.data) state.roiRecords[state.selectedRoiIdx] = resp.data;
        await selectRoi(state.selectedRoiIdx);
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

async function handleForcePass() {
    if (state.selectedRoiIdx < 0) return;
    const resp = await serverCmd('override_pass', { roi_index: state.selectedRoiIdx });
    if (resp.ok) {
        if (resp.data) state.roiRecords[state.selectedRoiIdx] = resp.data;
        await selectRoi(state.selectedRoiIdx);
        buildRoiList();
        showToast('Forced PASS');
    }
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
    document.getElementById('btn-override').addEventListener('click', handleForcePass);
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
// INITIALIZATION
// ═══════════════════════════════════════════════════════════════════════════

async function init() {
    await initSounds();
    initLanguageSelector();
    initSqueakListeners();
    bindEvents();
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
