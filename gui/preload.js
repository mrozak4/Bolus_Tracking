const { contextBridge, ipcRenderer } = require('electron');

contextBridge.exposeInMainWorld('bolusAPI', {
    /**
     * Send a command to bolus_server and await JSON response.
     * @param {string} action - Command name (e.g. 'load_tiff', 'get_trace', 'render_plot')
     * @param {object} params - Command parameters
     * @returns {Promise<object>} Server response { ok, data?, error? }
     */
    sendCommand: (action, params) => {
        return ipcRenderer.invoke('server:command', action, params);
    },

    /** Open native folder picker. Returns path string or null. */
    openFolderDialog: () => ipcRenderer.invoke('dialog:openFolder'),

    /** Open native file picker. Returns path string or null. */
    openFileDialog: () => ipcRenderer.invoke('dialog:openFile'),

    /** Resolve a resource path (fonts, icons, etc). */
    getResourcePath: (rel) => ipcRenderer.invoke('app:getResourcePath', rel),

    /** Get absolute path to a sound file. */
    getSoundPath: (filename) => ipcRenderer.invoke('app:getSoundPath', filename),

    /** Load a locale JSON file by language code. Returns parsed object or null. */
    loadLocale: (langCode) => ipcRenderer.invoke('app:loadLocale', langCode),

    // ── Batch Pipeline API ──────────────────────────────────────────────────

    /**
     * Run the batch pipeline (bolus_tracking_cpp) with given CLI args.
     * @param {string[]} args - CLI arguments (e.g. ['--folder', '/path', '--plot'])
     * @returns {Promise<{ok: boolean, error?: string}>}
     */
    runPipeline: (args) => ipcRenderer.invoke('pipeline:run', args),

    /** Kill a running batch pipeline process. */
    killPipeline: () => ipcRenderer.invoke('pipeline:kill'),

    /**
     * Register a callback for pipeline stdout/stderr output lines.
     * @param {function({stream: string, line: string})} callback
     */
    onPipelineOutput: (callback) => {
        ipcRenderer.on('pipeline:output', (event, data) => callback(data));
    },

    /**
     * Register a callback for pipeline completion.
     * @param {function({code: number, signal?: string, error?: string})} callback
     */
    onPipelineDone: (callback) => {
        ipcRenderer.on('pipeline:done', (event, data) => callback(data));
    }
});
