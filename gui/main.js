/**
 * ⚠️  DEPRECATED — THIS ELECTRON GUI IS NO LONGER MAINTAINED  ⚠️
 *
 * The Electron-based Bolus Tracking Studio has been replaced by a native
 * C++ desktop application built with Dear ImGui, ImPlot, and GLFW.
 *
 * To use the current app:
 *   - Download the DMG from GitHub Releases, OR
 *   - Build from source: cmake --build . --target bolus_tracking_gui
 *
 * This file is retained for historical reference only.
 * The original source code has been removed to prevent JavaScript
 * syntax errors when Electron tries to parse it.
 */

const { app, dialog } = require('electron');

app.whenReady().then(() => {
    dialog.showErrorBox(
        '⚠️ Deprecated — Use Native App Instead',
        'This Electron GUI is DEPRECATED and no longer works.\n\n' +
        'It has been replaced by the native C++ Bolus Tracking Studio app.\n\n' +
        'To get the current app:\n' +
        '• Download the DMG from GitHub Releases\n' +
        '• Or build from source:\n' +
        '    mkdir build && cd build\n' +
        '    cmake .. -DCMAKE_BUILD_TYPE=Release\n' +
        '    cmake --build . --target bolus_tracking_gui -j8\n' +
        '    open "Bolus Tracking Studio.app"\n\n' +
        'This app will now exit.'
    );
    app.quit();
});
