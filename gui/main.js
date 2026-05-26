/**
 * Bolus Tracking Studio — Electron Main Process
 * Spawns bolus_server as a child process and bridges IPC to the renderer.
 * Memory-conscious: 256MB JS heap cap, no DevTools in production.
 */

const { app, BrowserWindow, ipcMain, dialog } = require('electron');
const { spawn } = require('child_process');
const path = require('path');
const fs = require('fs');
const os = require('os');

// ─── Globals ────────────────────────────────────────────────────────────────

let mainWindow = null;
let serverProcess = null;
let pendingRequests = new Map();  // id → { resolve, reject, timer }
let nextRequestId = 1;
let lineBuffer = '';             // accumulates partial JSON lines from stdout
let pipelineProcess = null;      // batch pipeline child process

// ─── Resolve Resource Paths ────────────────────────────────────────────────

function getResourcePath(relativePath) {
    // In packaged app: process.resourcesPath points to Resources/
    // In dev: use the project root
    if (app.isPackaged) {
        return path.join(process.resourcesPath, relativePath);
    }
    return path.join(__dirname, '..', relativePath);
}

function getServerBinaryPath() {
    const ext = process.platform === 'win32' ? '.exe' : '';
    if (app.isPackaged) {
        return path.join(process.resourcesPath, 'bin', `bolus_server${ext}`);
    }
    // Dev mode: look in build/ directory (or build/Release on Windows)
    const buildPath = path.join(__dirname, '..', 'build', `bolus_server${ext}`);
    if (fs.existsSync(buildPath)) return buildPath;
    const winPath = path.join(__dirname, '..', 'build', 'Release', `bolus_server${ext}`);
    if (fs.existsSync(winPath)) return winPath;
    
    // Fallback: check PATH
    return `bolus_server${ext}`;
}

function getPipelineBinaryPath() {
    const ext = process.platform === 'win32' ? '.exe' : '';
    if (app.isPackaged) {
        return path.join(process.resourcesPath, 'bin', `bolus_tracking_cpp${ext}`);
    }
    const buildPath = path.join(__dirname, '..', 'build', `bolus_tracking_cpp${ext}`);
    if (fs.existsSync(buildPath)) return buildPath;
    const winPath = path.join(__dirname, '..', 'build', 'Release', `bolus_tracking_cpp${ext}`);
    if (fs.existsSync(winPath)) return winPath;
    
    return `bolus_tracking_cpp${ext}`;
}

// ─── Server Process Management ──────────────────────────────────────────────

function startServer() {
    const serverPath = getServerBinaryPath();
    console.log(`[main] Starting bolus_server: ${serverPath}`);

    serverProcess = spawn(serverPath, [], {
        stdio: ['pipe', 'pipe', 'pipe'],
        env: { ...process.env }
    });

    serverProcess.stdout.on('data', (chunk) => {
        lineBuffer += chunk.toString();
        let newlineIdx;
        while ((newlineIdx = lineBuffer.indexOf('\n')) !== -1) {
            const line = lineBuffer.slice(0, newlineIdx).trim();
            lineBuffer = lineBuffer.slice(newlineIdx + 1);
            if (line.length === 0) continue;
            try {
                const response = JSON.parse(line);
                const pending = pendingRequests.get(response.id);
                if (pending) {
                    clearTimeout(pending.timer);
                    pendingRequests.delete(response.id);
                    pending.resolve(response);
                }
            } catch (e) {
                console.error('[main] Failed to parse server response:', line.slice(0, 200));
            }
        }
    });

    serverProcess.stderr.on('data', (chunk) => {
        const msg = chunk.toString().trim();
        if (msg) console.log(`[server stderr] ${msg}`);
    });

    serverProcess.on('error', (err) => {
        console.error('[main] Server process error:', err.message);
        // Reject all pending requests
        for (const [id, pending] of pendingRequests) {
            clearTimeout(pending.timer);
            pending.reject(new Error('Server process error: ' + err.message));
        }
        pendingRequests.clear();
    });

    serverProcess.on('exit', (code, signal) => {
        console.log(`[main] Server exited: code=${code}, signal=${signal}`);
        serverProcess = null;
        for (const [id, pending] of pendingRequests) {
            clearTimeout(pending.timer);
            pending.reject(new Error('Server process exited'));
        }
        pendingRequests.clear();
    });
}

function sendServerCommand(action, params, timeoutMs = 120000) {
    return new Promise((resolve, reject) => {
        if (!serverProcess || !serverProcess.stdin.writable) {
            return reject(new Error('Server not running'));
        }
        const id = nextRequestId++;
        const msg = JSON.stringify({ id, action, params }) + '\n';

        const timer = setTimeout(() => {
            pendingRequests.delete(id);
            reject(new Error(`Server timeout for action: ${action} (${timeoutMs}ms)`));
        }, timeoutMs);

        pendingRequests.set(id, { resolve, reject, timer });

        try {
            serverProcess.stdin.write(msg);
        } catch (e) {
            clearTimeout(timer);
            pendingRequests.delete(id);
            reject(new Error('Failed to write to server: ' + e.message));
        }
    });
}

function stopServer() {
    if (serverProcess) {
        try {
            serverProcess.stdin.end();
            serverProcess.kill('SIGTERM');
        } catch (e) { /* ignore */ }
        serverProcess = null;
    }
    for (const [id, pending] of pendingRequests) {
        clearTimeout(pending.timer);
        pending.reject(new Error('Shutting down'));
    }
    pendingRequests.clear();
}

// ─── Window Creation ────────────────────────────────────────────────────────

function createWindow() {
    const isDev = process.argv.includes('--dev');

    mainWindow = new BrowserWindow({
        width: 1400,
        height: 900,
        minWidth: 1000,
        minHeight: 700,
        title: 'Bolus Tracking Studio',
        backgroundColor: '#131316',
        show: false,  // Show after ready-to-show to avoid flash
        webPreferences: {
            preload: path.join(__dirname, 'preload.js'),
            nodeIntegration: false,
            contextIsolation: true,
            sandbox: false  // needed for preload to access path/fs
        }
    });

    mainWindow.loadFile(path.join(__dirname, 'index.html'));

    mainWindow.once('ready-to-show', () => {
        mainWindow.show();
    });

    if (isDev) {
        mainWindow.webContents.openDevTools({ mode: 'detach' });
    }

    mainWindow.on('closed', () => {
        mainWindow = null;
    });
}

// ─── IPC Handlers (renderer ↔ main ↔ bolus_server) ─────────────────────────

function setupIPC() {
    // Generic command passthrough to bolus_server
    ipcMain.handle('server:command', async (event, action, params) => {
        try {
            // Longer timeout for compute-heavy operations
            const heavyOps = ['compute_traces', 'run_fit', 'load_tiff', 'render_plot'];
            const timeout = heavyOps.includes(action) ? 300000 : 60000;
            return await sendServerCommand(action, params || {}, timeout);
        } catch (e) {
            return { ok: false, error: e.message };
        }
    });

    // Native folder picker
    ipcMain.handle('dialog:openFolder', async () => {
        const result = await dialog.showOpenDialog(mainWindow, {
            properties: ['openDirectory'],
            title: 'Select Subject Data Folder'
        });
        if (result.canceled || result.filePaths.length === 0) return null;
        return result.filePaths[0];
    });

    // Native file picker
    ipcMain.handle('dialog:openFile', async () => {
        const result = await dialog.showOpenDialog(mainWindow, {
            properties: ['openFile'],
            title: 'Select Data File',
            filters: [
                { name: 'All Supported', extensions: ['tif', 'tiff', 'csv', 'txt', 'mat'] },
                { name: 'TIFF Images', extensions: ['tif', 'tiff'] },
                { name: 'CSV Files', extensions: ['csv'] },
                { name: 'All Files', extensions: ['*'] }
            ]
        });
        if (result.canceled || result.filePaths.length === 0) return null;
        return result.filePaths[0];
    });

    // Resource path resolver
    ipcMain.handle('app:getResourcePath', (event, rel) => {
        return getResourcePath(rel);
    });

    // Sound file path helper
    ipcMain.handle('app:getSoundPath', (event, filename) => {
        if (app.isPackaged) {
            return path.join(process.resourcesPath, 'sounds', filename);
        }
        return path.join(__dirname, '..', 'resources', filename);
    });

    // ── Batch Pipeline Operations ──────────────────────────────────────────

    /**
     * Start the batch pipeline (bolus_tracking_cpp) with given args.
     * Streams stdout/stderr lines back to the renderer via 'pipeline:output' events.
     * Returns immediately with { ok: true } or { ok: false, error }.
     */
    ipcMain.handle('pipeline:run', async (event, args) => {
        if (pipelineProcess) {
            return { ok: false, error: 'Pipeline is already running' };
        }

        const binPath = getPipelineBinaryPath();
        if (!fs.existsSync(binPath)) {
            return { ok: false, error: `Pipeline binary not found: ${binPath}` };
        }

        console.log(`[main] Starting pipeline: ${binPath} ${args.join(' ')}`);

        try {
            pipelineProcess = spawn(binPath, args, {
                stdio: ['ignore', 'pipe', 'pipe'],
                env: { ...process.env }
            });
        } catch (e) {
            pipelineProcess = null;
            return { ok: false, error: e.message };
        }

        let stdoutBuf = '';
        pipelineProcess.stdout.on('data', (chunk) => {
            stdoutBuf += chunk.toString();
            let idx;
            while ((idx = stdoutBuf.indexOf('\n')) !== -1) {
                const line = stdoutBuf.slice(0, idx);
                stdoutBuf = stdoutBuf.slice(idx + 1);
                if (mainWindow && !mainWindow.isDestroyed()) {
                    mainWindow.webContents.send('pipeline:output', { stream: 'stdout', line });
                }
            }
        });

        let stderrBuf = '';
        pipelineProcess.stderr.on('data', (chunk) => {
            stderrBuf += chunk.toString();
            let idx;
            while ((idx = stderrBuf.indexOf('\n')) !== -1) {
                const line = stderrBuf.slice(0, idx);
                stderrBuf = stderrBuf.slice(idx + 1);
                if (mainWindow && !mainWindow.isDestroyed()) {
                    mainWindow.webContents.send('pipeline:output', { stream: 'stderr', line });
                }
            }
        });

        pipelineProcess.on('exit', (code, signal) => {
            console.log(`[main] Pipeline exited: code=${code}, signal=${signal}`);
            // Flush remaining buffer
            if (stdoutBuf.trim() && mainWindow && !mainWindow.isDestroyed()) {
                mainWindow.webContents.send('pipeline:output', { stream: 'stdout', line: stdoutBuf.trim() });
            }
            if (stderrBuf.trim() && mainWindow && !mainWindow.isDestroyed()) {
                mainWindow.webContents.send('pipeline:output', { stream: 'stderr', line: stderrBuf.trim() });
            }
            pipelineProcess = null;
            if (mainWindow && !mainWindow.isDestroyed()) {
                mainWindow.webContents.send('pipeline:done', { code, signal });
            }
        });

        pipelineProcess.on('error', (err) => {
            console.error('[main] Pipeline error:', err.message);
            pipelineProcess = null;
            if (mainWindow && !mainWindow.isDestroyed()) {
                mainWindow.webContents.send('pipeline:done', { code: -1, error: err.message });
            }
        });

        return { ok: true };
    });

    /** Kill a running pipeline process. */
    ipcMain.handle('pipeline:kill', async () => {
        if (pipelineProcess) {
            try {
                pipelineProcess.kill('SIGTERM');
            } catch (e) { /* ignore */ }
            pipelineProcess = null;
            return { ok: true };
        }
        return { ok: false, error: 'No pipeline running' };
    });

    // Locale file loader
    ipcMain.handle('app:loadLocale', async (event, langCode) => {
        const localePath = path.join(__dirname, 'locales', `${langCode}.json`);
        try {
            const data = fs.readFileSync(localePath, 'utf-8');
            return JSON.parse(data);
        } catch (e) {
            console.error(`[main] Failed to load locale ${langCode}:`, e.message);
            return null;
        }
    });
}

// ─── App Lifecycle ──────────────────────────────────────────────────────────

app.whenReady().then(() => {
    startServer();
    setupIPC();
    createWindow();
});

app.on('window-all-closed', () => {
    stopServer();
    app.quit();
});

app.on('before-quit', () => {
    stopServer();
});

app.on('activate', () => {
    if (BrowserWindow.getAllWindows().length === 0) {
        createWindow();
    }
});
