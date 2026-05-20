import os
import re
import glob
import warnings
import numpy as np
import pandas as pd
import scipy.io as sio
import tifffile
from scipy.interpolate import interp1d
from scipy.ndimage import gaussian_filter1d
from scipy.optimize import curve_fit

import tkinter as tk
from tkinter import ttk, filedialog, messagebox
import matplotlib.pyplot as plt
from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg, NavigationToolbar2Tk

from bolus_tracking import denoise_trace, auto_estimate_params, fit_bolus, gamma_fun
from batch_process import find_triplets, get_mask_from_poly, parse_metadata

warnings.filterwarnings("ignore")

class BolusTrackingGUI:
    def __init__(self, root):
        self.root = root
        self.root.title("Capillary Bolus Tracking & Gamma Curve Fitting Studio")
        self.root.geometry("1300x850")
        
        # Configure grid weight
        self.root.grid_columnconfigure(1, weight=1)
        self.root.grid_rowconfigure(0, weight=1)
        
        # Style configuration
        self.style = ttk.Style()
        self.style.theme_use('clam')
        
        # Color palette
        self.bg_color = "#f5f6f8"
        self.primary_color = "#2c3e50"
        self.accent_color = "#3498db"
        
        self.root.configure(bg=self.bg_color)
        
        # Internal state variables
        self.current_folder = ""
        self.triplets = []
        self.current_triplet = None
        self.tiff_stack = None
        self.mask_objs = []
        self.current_roi_idx = 0
        self.frame_rate = 1.0
        self.up_factor = 20
        
        # Current ROI trace data
        self.mfi_raw = None
        self.mfi_denoised = None
        self.y_us = None
        self.tl_raw = None
        self.tl_us = None
        self.sd_base = 0
        self.clicks = None
        self.fit_params = None  # [amp, t2p, fwhm, baseline]
        
        # Fit results table for current dataset
        self.dataset_results = [] # list of dicts
        
        # Click mode: 'onset', 'peak', 'end', 'baseline', or None
        self.click_mode = None
        
        # Create UI layout
        self.create_widgets()
        
        # Auto-load current folder if it contains any datasets
        self.load_folder(os.getcwd())

    def create_widgets(self):
        # ---------------- Left Panel (Controls) ----------------
        left_panel = tk.Frame(self.root, bg=self.bg_color, width=320, padx=10, pady=10)
        left_panel.grid(row=0, column=0, sticky="ns")
        left_panel.grid_propagate(False)
        
        # Folder Selection
        btn_folder = tk.Button(left_panel, text="📁 Open Subject Folder", font=("Helvetica", 11, "bold"),
                               bg=self.primary_color, fg="white", activebackground=self.accent_color,
                               command=self.select_folder_dialog)
        btn_folder.pack(fill="x", pady=(0, 10))
        
        lbl_folder_frame = ttk.LabelFrame(left_panel, text="Selected Folder")
        lbl_folder_frame.pack(fill="x", pady=5)
        self.lbl_folder_path = ttk.Label(lbl_folder_frame, text="No folder selected", wraplength=280, foreground="#7f8c8d")
        self.lbl_folder_path.pack(fill="x", padx=5, pady=5)
        
        # Triplet/Dataset Selector
        lbl_dataset_frame = ttk.LabelFrame(left_panel, text="1. Select Dataset")
        lbl_dataset_frame.pack(fill="x", pady=5)
        self.cb_dataset = ttk.Combobox(lbl_dataset_frame, state="readonly")
        self.cb_dataset.pack(fill="x", padx=5, pady=5)
        self.cb_dataset.bind("<<ComboboxSelected>>", self.on_dataset_selected)
        
        # ROI Selector
        lbl_roi_frame = ttk.LabelFrame(left_panel, text="2. Select Capillary ROI")
        lbl_roi_frame.pack(fill="x", pady=5)
        self.cb_roi = ttk.Combobox(lbl_roi_frame, state="readonly")
        self.cb_roi.pack(fill="x", padx=5, pady=5)
        self.cb_roi.bind("<<ComboboxSelected>>", self.on_roi_selected)
        
        # Interactive click tool buttons
        lbl_click_frame = ttk.LabelFrame(left_panel, text="3. Adjust Markers (Click Plot)")
        lbl_click_frame.pack(fill="x", pady=5)
        
        grid_clicks = tk.Frame(lbl_click_frame, bg=self.bg_color)
        grid_clicks.pack(fill="x", padx=5, pady=5)
        
        self.btn_click_onset = tk.Button(grid_clicks, text="Set Onset ⌖", bg="#e67e22", fg="white", command=lambda: self.set_click_mode("onset"))
        self.btn_click_onset.grid(row=0, column=0, padx=2, pady=2, sticky="ew")
        
        self.btn_click_peak = tk.Button(grid_clicks, text="Set Peak ⌖", bg="#9b59b6", fg="white", command=lambda: self.set_click_mode("peak"))
        self.btn_click_peak.grid(row=0, column=1, padx=2, pady=2, sticky="ew")
        
        self.btn_click_end = tk.Button(grid_clicks, text="Set End ⌖", bg="#e74c3c", fg="white", command=lambda: self.set_click_mode("end"))
        self.btn_click_end.grid(row=1, column=0, padx=2, pady=2, sticky="ew")
        
        self.btn_click_base = tk.Button(grid_clicks, text="Set Baseline ⌖", bg="#27ae60", fg="white", command=lambda: self.set_click_mode("baseline"))
        self.btn_click_base.grid(row=1, column=1, padx=2, pady=2, sticky="ew")
        
        grid_clicks.grid_columnconfigure(0, weight=1)
        grid_clicks.grid_columnconfigure(1, weight=1)
        
        # Parameter overrides
        lbl_params_frame = ttk.LabelFrame(left_panel, text="4. Fit Parameters")
        lbl_params_frame.pack(fill="x", pady=5)
        
        grid_params = tk.Frame(lbl_params_frame, bg=self.bg_color)
        grid_params.pack(fill="x", padx=5, pady=5)
        
        # Labels and Entry boxes for editing
        ttk.Label(grid_params, text="Amplitude:").grid(row=0, column=0, sticky="w", pady=2)
        self.ent_amp = ttk.Entry(grid_params, width=10)
        self.ent_amp.grid(row=0, column=1, pady=2, sticky="e")
        
        ttk.Label(grid_params, text="T2P (s):").grid(row=1, column=0, sticky="w", pady=2)
        self.ent_t2p = ttk.Entry(grid_params, width=10)
        self.ent_t2p.grid(row=1, column=1, pady=2, sticky="e")
        
        ttk.Label(grid_params, text="FWHM (s):").grid(row=2, column=0, sticky="w", pady=2)
        self.ent_fwhm = ttk.Entry(grid_params, width=10)
        self.ent_fwhm.grid(row=2, column=1, pady=2, sticky="e")
        
        ttk.Label(grid_params, text="Baseline:").grid(row=3, column=0, sticky="w", pady=2)
        self.ent_base = ttk.Entry(grid_params, width=10)
        self.ent_base.grid(row=3, column=1, pady=2, sticky="e")
        
        ttk.Label(grid_params, text="Onset (s):").grid(row=4, column=0, sticky="w", pady=2)
        self.ent_onset_t = ttk.Entry(grid_params, width=10)
        self.ent_onset_t.grid(row=4, column=1, pady=2, sticky="e")
        
        ttk.Label(grid_params, text="End (s):").grid(row=5, column=0, sticky="w", pady=2)
        self.ent_end_t = ttk.Entry(grid_params, width=10)
        self.ent_end_t.grid(row=5, column=1, pady=2, sticky="e")
        
        grid_params.grid_columnconfigure(1, weight=1)
        
        # Vessel Designation
        lbl_vessel_frame = ttk.LabelFrame(left_panel, text="5. Vessel Designation")
        lbl_vessel_frame.pack(fill="x", pady=5)
        self.cb_vessel = ttk.Combobox(lbl_vessel_frame, state="readonly", values=["Unknown (U)", "Artery (A)", "Vein (V)", "Capillary (C)"])
        self.cb_vessel.set("Unknown (U)")
        self.cb_vessel.pack(fill="x", padx=5, pady=5)
        
        # Run / Save Actions
        self.btn_run_fit = tk.Button(left_panel, text="⚡ Run Gamma Fit", font=("Helvetica", 11, "bold"),
                                     bg="#2980b9", fg="white", activebackground="#3498db",
                                     command=self.run_fit_action)
        self.btn_run_fit.pack(fill="x", pady=(10, 5))
        
        self.btn_auto_est = tk.Button(left_panel, text="↺ Reset to Auto-Heuristics", font=("Helvetica", 9),
                                      bg="#95a5a6", fg="white", activebackground="#7f8c8d",
                                      command=self.reset_to_auto)
        self.btn_auto_est.pack(fill="x", pady=2)
        
        self.btn_save_export = tk.Button(left_panel, text="💾 Save & Export Results", font=("Helvetica", 11, "bold"),
                                         bg="#27ae60", fg="white", activebackground="#2ecc71",
                                         command=self.save_export_action)
        self.btn_save_export.pack(fill="x", pady=(10, 0))
        
        # Status/Instruction label at bottom of panel
        self.lbl_status = ttk.Label(left_panel, text="Ready", font=("Helvetica", 10, "italic"), foreground="#34495e", wraplength=280)
        self.lbl_status.pack(side="bottom", fill="x", pady=5)
        
        # ---------------- Right Panel (Plot/Visualization) ----------------
        right_panel = tk.Frame(self.root, bg="white")
        right_panel.grid(row=0, column=1, sticky="nsew")
        right_panel.grid_rowconfigure(0, weight=1)
        right_panel.grid_columnconfigure(0, weight=1)
        
        # Matplotlib plot setup
        self.fig, self.ax = plt.subplots(figsize=(10, 6), dpi=100)
        self.canvas = FigureCanvasTkAgg(self.fig, master=right_panel)
        self.canvas_widget = self.canvas.get_tk_widget()
        self.canvas_widget.grid(row=0, column=0, sticky="nsew")
        
        # Navigation toolbar
        toolbar_frame = tk.Frame(right_panel, bg="white")
        toolbar_frame.grid(row=1, column=0, sticky="ew")
        self.toolbar = NavigationToolbar2Tk(self.canvas, toolbar_frame)
        self.toolbar.update()
        
        # Connect to mouse clicks
        self.canvas.mpl_connect("button_press_event", self.on_plot_click)

    def set_click_mode(self, mode):
        self.click_mode = mode
        modes_colors = {
            'onset': ('Onset Marker', '#e67e22'),
            'peak': ('Peak Marker', '#9b59b6'),
            'end': ('End Marker', '#e74c3c'),
            'baseline': ('Baseline Marker', '#27ae60')
        }
        name, color = modes_colors[mode]
        self.lbl_status.config(text=f"Click on the plot to place: {name}", foreground=color)
        self.root.config(cursor="crosshair")

    def on_plot_click(self, event):
        if not self.click_mode or event.inaxes != self.ax:
            return
        
        x = event.xdata
        y = event.ydata
        
        if self.click_mode == "onset":
            self.ent_onset_t.delete(0, tk.END)
            self.ent_onset_t.insert(0, f"{x:.3f}")
        elif self.click_mode == "peak":
            self.ent_t2p.delete(0, tk.END)
            # T2P in entries is Time to Peak relative to onset
            onset_t = float(self.ent_onset_t.get() or 0)
            t2p_rel = max(0.01, x - onset_t)
            self.ent_t2p.insert(0, f"{t2p_rel:.3f}")
            self.ent_amp.delete(0, tk.END)
            base = float(self.ent_base.get() or 0)
            self.ent_amp.insert(0, f"{max(0.1, y - base):.3f}")
        elif self.click_mode == "end":
            self.ent_end_t.delete(0, tk.END)
            self.ent_end_t.insert(0, f"{x:.3f}")
        elif self.click_mode == "baseline":
            self.ent_base.delete(0, tk.END)
            self.ent_base.insert(0, f"{y:.3f}")
            
        self.click_mode = None
        self.lbl_status.config(text="Ready", foreground="#34495e")
        self.root.config(cursor="")
        
        # Instantly run fit with updated interactive parameters
        self.run_fit_action()

    def select_folder_dialog(self):
        folder = filedialog.askdirectory(initialdir=os.getcwd())
        if folder:
            self.load_folder(folder)

    def load_folder(self, folder):
        self.current_folder = folder
        self.lbl_folder_path.config(text=folder)
        
        # Detect triplets
        self.triplets = find_triplets(folder)
        if not self.triplets:
            self.cb_dataset.configure(values=[])
            self.cb_dataset.set("")
            self.cb_roi.configure(values=[])
            self.cb_roi.set("")
            self.lbl_status.config(text="No datasets found in this folder.", foreground="#e74c3c")
            return
            
        display_names = [os.path.basename(t[0]) for t in self.triplets]
        self.cb_dataset.configure(values=display_names)
        self.cb_dataset.current(0)
        self.on_dataset_selected(None)

    def on_dataset_selected(self, event):
        idx = self.cb_dataset.current()
        if idx < 0:
            return
            
        self.current_triplet = self.triplets[idx]
        tiff_path, mask_path, meta_path = self.current_triplet
        
        self.lbl_status.config(text="Loading dataset files...", foreground=self.accent_color)
        self.root.update()
        
        try:
            # Read metadata
            self.frame_rate = parse_metadata(meta_path)
            
            # Read TIFF stack
            self.tiff_stack = tifffile.imread(tiff_path)
            
            # Read maskObj
            mat_data = sio.loadmat(mask_path, struct_as_record=False, squeeze_me=True)
            if 'maskObj' in mat_data:
                self.mask_objs = mat_data['maskObj']
                if not isinstance(self.mask_objs, np.ndarray):
                    self.mask_objs = np.array([self.mask_objs])
            else:
                raise ValueError("No maskObj found in .mat mask file.")
                
            # Initialize results CSV file path
            self.out_csv = tiff_path.replace('.tif', '_results.csv')
            if os.path.exists(self.out_csv):
                # Load existing results CSV to keep user overrides
                df_existing = pd.read_csv(self.out_csv)
                self.dataset_results = df_existing.to_dict('records')
            else:
                self.dataset_results = []
                
            # Populate Combobox values for ROIs
            roi_list = [f"ROI {i+1}" for i in range(len(self.mask_objs))]
            self.cb_roi.configure(values=roi_list)
            self.cb_roi.current(0)
            self.on_roi_selected(None)
            
        except Exception as e:
            messagebox.showerror("Error Loading Dataset", str(e))
            self.lbl_status.config(text="Failed to load dataset.", foreground="#e74c3c")

    def on_roi_selected(self, event):
        self.current_roi_idx = self.cb_roi.current()
        if self.current_roi_idx < 0:
            return
            
        obj = self.mask_objs[self.current_roi_idx]
        if hasattr(obj, 'poli'):
            pos = obj.poli.Position
        elif hasattr(obj, 'Position'):
            pos = obj.Position
        else:
            messagebox.showerror("Error", "Invalid ROI structure: missing Position.")
            return
            
        if len(pos) < 3:
            messagebox.showerror("Error", "ROI has too few points to build a polygon.")
            return
            
        self.lbl_status.config(text=f"Extracting ROI {self.current_roi_idx + 1} trace...", foreground=self.accent_color)
        self.root.update()
        
        # 1. MFI Trace Extraction
        mask = get_mask_from_poly(pos, self.tiff_stack.shape[1:])
        self.mfi_raw = np.array([np.mean(frame[mask]) for frame in self.tiff_stack])
        self.mfi_denoised = denoise_trace(self.mfi_raw)
        
        # 2. Upsampling
        self.tl_raw = np.arange(len(self.mfi_raw)) / self.frame_rate
        self.tl_us = np.arange(len(self.mfi_raw) * self.up_factor) / (self.frame_rate * self.up_factor)
        
        spline_interp = interp1d(self.tl_raw, self.mfi_denoised, kind='cubic', fill_value='extrapolate')
        self.y_us = spline_interp(self.tl_us)
        
        # Check if we already have results in memory for this ROI
        roi_record = None
        for record in self.dataset_results:
            if int(record.get('ROI', 0)) == self.current_roi_idx + 1:
                roi_record = record
                break
                
        if roi_record:
            # Populate entry boxes from saved record
            self.ent_amp.delete(0, tk.END)
            self.ent_amp.insert(0, f"{roi_record.get('InitAmp', 0.0):.3f}")
            
            self.ent_t2p.delete(0, tk.END)
            self.ent_t2p.insert(0, f"{roi_record.get('InitT2p', 0.0):.3f}")
            
            self.ent_fwhm.delete(0, tk.END)
            self.ent_fwhm.insert(0, f"{roi_record.get('InitFWHM', 0.0):.3f}")
            
            self.ent_base.delete(0, tk.END)
            self.ent_base.insert(0, f"{roi_record.get('InitM', 0.0):.3f}")
            
            self.ent_onset_t.delete(0, tk.END)
            self.ent_onset_t.insert(0, f"{roi_record.get('Click2_Onset_T', 0.0):.3f}")
            
            self.ent_end_t.delete(0, tk.END)
            self.ent_end_t.insert(0, f"{roi_record.get('Click4_End_T', 0.0):.3f}")
            
            self.cb_vessel.set(roi_record.get('VesType', 'Unknown (U)'))
            
            # Load fit variables
            self.fit_params = [
                roi_record.get('F_Amp', np.nan),
                roi_record.get('F_T2p', np.nan),
                roi_record.get('F_FWHM', np.nan),
                roi_record.get('F_M', np.nan)
            ]
            self.sd_base = 0 # reset
            
            self.run_fit_action() # update GUI plot
        else:
            self.reset_to_auto()

    def reset_to_auto(self):
        # Run automatic heuristic estimates
        init_params, start_idx, end_idx, self.sd_base, self.clicks = auto_estimate_params(
            self.y_us, self.tl_us, self.frame_rate, self.up_factor
        )
        
        # Populate entries
        self.ent_amp.delete(0, tk.END)
        self.ent_amp.insert(0, f"{init_params[0]:.3f}")
        
        self.ent_t2p.delete(0, tk.END)
        self.ent_t2p.insert(0, f"{init_params[1]:.3f}")
        
        self.ent_fwhm.delete(0, tk.END)
        self.ent_fwhm.insert(0, f"{init_params[2]:.3f}")
        
        self.ent_base.delete(0, tk.END)
        self.ent_base.insert(0, f"{init_params[3]:.3f}")
        
        self.ent_onset_t.delete(0, tk.END)
        self.ent_onset_t.insert(0, f"{self.clicks['onset'][0]:.3f}")
        
        self.ent_end_t.delete(0, tk.END)
        self.ent_end_t.insert(0, f"{self.clicks['end'][0]:.3f}")
        
        self.cb_vessel.set("Unknown (U)")
        self.fit_params = None
        
        # Run the fit with the auto parameters
        self.run_fit_action()

    def run_fit_action(self):
        try:
            # Parse inputs from Entry widgets
            amp = float(self.ent_amp.get())
            t2p = float(self.ent_t2p.get())
            fwhm = float(self.ent_fwhm.get())
            base = float(self.ent_base.get())
            onset_t = float(self.ent_onset_t.get())
            end_t = float(self.ent_end_t.get())
            
            # Find closest indices in time vector
            start_idx = np.argmin(np.abs(self.tl_us - onset_t))
            end_idx = np.argmin(np.abs(self.tl_us - end_t))
            
            # Reconstruct initial params array
            init_params = [amp, t2p, fwhm, base]
            
            # Setup fit bounds and fit window
            t_fit = self.tl_us[start_idx:end_idx] - onset_t
            y_fit = self.y_us[start_idx:end_idx]
            
            # Run two-pass fit
            popt, pcov = fit_bolus(t_fit, y_fit, init_params, self.sd_base)
            self.fit_params = popt
            
            # Update plot
            self.update_plot(onset_t, end_t, start_idx, end_idx)
            
            if popt is not None and not np.isnan(popt).any():
                self.lbl_status.config(
                    text=f"Fit Successful!\nFitted Amplitude: {popt[0]:.2f}\nT2P: {popt[1]:.2f}s, FWHM: {popt[2]:.2f}s, Base: {popt[3]:.2f}",
                    foreground="#27ae60"
                )
            else:
                self.lbl_status.config(text="Fitting failed or diverged.", foreground="#e74c3c")
                
        except ValueError as e:
            self.lbl_status.config(text=f"Invalid parameter values: {e}", foreground="#e74c3c")

    def update_plot(self, onset_t, end_t, start_idx, end_idx):
        self.ax.clear()
        
        # Plot data points
        self.ax.plot(self.tl_raw, self.mfi_raw, 'o', color='#95a5a6', markersize=3, label='Raw Data', alpha=0.5)
        self.ax.plot(self.tl_raw, self.mfi_denoised, '+', color='#2ecc71', markersize=6, label='Denoised Data', alpha=0.7)
        self.ax.plot(self.tl_us, self.y_us, '--', color='#3498db', alpha=0.5, label='Spline Upsampled')
        
        # Place onset and end vertical lines
        self.ax.axvline(onset_t, color="#e67e22", linestyle=":", label=f"Onset ({onset_t:.2f}s)")
        self.ax.axvline(end_t, color="#e74c3c", linestyle=":", label=f"End ({end_t:.2f}s)")
        
        # Get values from entry boxes
        amp = float(self.ent_amp.get() or 0)
        t2p = float(self.ent_t2p.get() or 0)
        base = float(self.ent_base.get() or 0)
        
        # Plot initial guess markers
        self.ax.plot(onset_t, base, 'co', markersize=9, label='Onset Init')
        self.ax.plot(onset_t + t2p, base + amp, 'mo', markersize=9, label='Peak Init')
        
        # Plot the fit curve if successful
        if self.fit_params is not None and not np.isnan(self.fit_params).any():
            t_plot = self.tl_us[0:end_idx] - onset_t
            y_fit = gamma_fun(t_plot, *self.fit_params)
            self.ax.plot(self.tl_us[0:end_idx], y_fit, 'r-', linewidth=2.5, label='Gamma Fit')
            self.ax.plot(self.tl_us[:start_idx], np.full(start_idx, self.fit_params[3]), 'r-', linewidth=2.5)
            
            title_str = (f"ROI {self.current_roi_idx + 1} Interactive Fit\n"
                         f"Amp={self.fit_params[0]:.2f}, T2p={self.fit_params[1]:.2f}s, "
                         f"FWHM={self.fit_params[2]:.2f}s, Base={self.fit_params[3]:.2f}")
            self.ax.set_title(title_str, fontdict={'fontsize': 11, 'weight': 'bold'})
        else:
            self.ax.set_title(f"ROI {self.current_roi_idx + 1} Interactive Estimator", fontdict={'fontsize': 11, 'weight': 'bold'})
            
        self.ax.set_xlabel("Time (seconds)")
        self.ax.set_ylabel("Fluorescence Intensity")
        self.ax.legend(loc="upper right", fontsize=8)
        self.ax.grid(True, linestyle="--", alpha=0.3)
        
        self.fig.tight_layout()
        self.canvas.draw()

    def save_export_action(self):
        if self.current_roi_idx < 0:
            return
            
        try:
            # Parse parameters to save
            amp_init = float(self.ent_amp.get())
            t2p_init = float(self.ent_t2p.get())
            fwhm_init = float(self.ent_fwhm.get())
            base_init = float(self.ent_base.get())
            onset_t = float(self.ent_onset_t.get())
            end_t = float(self.ent_end_t.get())
            
            ves_type_full = self.cb_vessel.get()
            ves_type = "U"
            if "Artery" in ves_type_full:
                ves_type = "A"
            elif "Vein" in ves_type_full:
                ves_type = "V"
            elif "Capillary" in ves_type_full:
                ves_type = "C"
                
            tiff_path = self.current_triplet[0]
            subj_match = re.search(r'(?:subject[_-]?)(\d+)', tiff_path, re.IGNORECASE)
            if not subj_match:
                subj_match = re.search(r'\b\d{4}\b', tiff_path)
            subj_num = int(subj_match.group(1)) if subj_match else 0
            exp = os.path.splitext(os.path.basename(tiff_path))[0]
            
            pos = self.mask_objs[self.current_roi_idx]
            if hasattr(pos, 'poli'):
                pos_verts = pos.poli.Position
            elif hasattr(pos, 'Position'):
                pos_verts = pos.Position
            else:
                pos_verts = []
                
            if len(pos_verts) >= 3:
                mask = get_mask_from_poly(pos_verts, self.tiff_stack.shape[1:])
                roi_size = int(np.sum(mask))
            else:
                roi_size = 0
                
            denoise_rms = float(np.sqrt(np.mean((self.mfi_raw - self.mfi_denoised) ** 2)))
            
            # Setup record
            record = {
                'ROI': self.current_roi_idx + 1,
                'SubjNum': subj_num,
                'Exp': exp,
                'InitAmp': amp_init,
                'InitT2p': t2p_init,
                'InitFWHM': fwhm_init,
                'InitM': base_init,
                'InitSNR': base_init / self.sd_base if self.sd_base > 0 else np.nan,
                'InitCNR': amp_init / self.sd_base if self.sd_base > 0 else np.nan,
                'Click1_Start_T': self.tl_us[0],
                'Click2_Onset_T': onset_t,
                'Click3_Peak_T': onset_t + t2p_init,
                'Click4_End_T': end_t,
            }
            
            # Calculate fitted parameters and stats
            auc = np.nan
            aucn = np.nan
            ttlb = np.nan
            ttm = np.nan
            tthb = np.nan
            ont = np.nan
            
            if self.fit_params is not None and not np.isnan(self.fit_params).any():
                f_amp, f_t2p, f_fwhm, f_m = self.fit_params
                
                # Evaluate fit over t_fit
                start_idx = np.argmin(np.abs(self.tl_us - onset_t))
                end_idx = np.argmin(np.abs(self.tl_us - end_t))
                t_fit = self.tl_us[start_idx:end_idx] - onset_t
                
                alpha = ((f_t2p ** 2) / (f_fwhm ** 2)) * 8.0 * np.log(2.0)
                beta_param = ((f_fwhm ** 2) / f_t2p) / (8.0 * np.log(2.0))
                
                y_fit_model = np.zeros_like(t_fit)
                for idx_t, t_val in enumerate(t_fit):
                    if t_val > 0:
                        y_fit_model[idx_t] = f_m + f_amp * (t_val / f_t2p)**alpha * np.exp(-(t_val - f_t2p) / beta_param)
                    else:
                        y_fit_model[idx_t] = f_m
                        
                # Trapezoidal numerical integration
                auc = float(np.sum(y_fit_model) - (y_fit_model[0] + y_fit_model[-1]) / 2.0)
                
                min_y = np.min(y_fit_model)
                max_y = np.max(y_fit_model)
                range_y = max_y - min_y
                y_fit_model_n = (y_fit_model - min_y) / range_y if range_y > 0 else np.zeros_like(y_fit_model)
                aucn = float(np.sum(y_fit_model_n) - (y_fit_model_n[0] + y_fit_model_n[-1]) / 2.0)
                
                # OnT
                I = np.where(y_fit_model_n < 0.1)[0]
                onset_idx = 0
                if len(I) > 0:
                    diffs = np.diff(I)
                    contig_idxs = np.where(diffs == 1)[0]
                    if len(contig_idxs) > 0:
                        last_idx = contig_idxs[-1]
                        onset_idx = I[last_idx] + 1
                    else:
                        onset_idx = I[0]
                ont = float(onset_idx / (self.frame_rate * self.up_factor))
                ttm = float(abs(f_t2p - ont))
                
                # Standard errors
                popt, pcov = fit_bolus(t_fit, self.y_us[start_idx:end_idx], [amp_init, t2p_init, fwhm_init, base_init], self.sd_base)
                se_t2p = 0.0
                if pcov is not None and not np.isinf(pcov).any():
                    se = np.sqrt(np.diag(pcov))
                    if len(se) > 1:
                        se_t2p = se[1]
                ci_lower = f_t2p - 1.96 * se_t2p
                ci_upper = f_t2p + 1.96 * se_t2p
                ttlb = float(abs(ci_lower - ont))
                tthb = float(abs(ci_upper - ont))
                
                record.update({
                    'F_Amp': f_amp,
                    'F_T2p': f_t2p,
                    'F_FWHM': f_fwhm,
                    'F_M': f_m,
                    'F_SNR': f_m / self.sd_base if self.sd_base > 0 else np.nan,
                    'F_CNR': f_amp / self.sd_base if self.sd_base > 0 else np.nan,
                    'AUC': auc,
                    'AUCn': aucn,
                    'TTlb': ttlb,
                    'TTm': ttm,
                    'TThb': tthb,
                    'OnT': ont,
                    'OnTSc': np.nan,  # will be computed across scan
                    'ROISize': roi_size,
                    'Denoise_RMS': denoise_rms,
                    'VesType': ves_type
                })
            else:
                record.update({
                    'F_Amp': np.nan, 'F_T2p': np.nan, 'F_FWHM': np.nan, 'F_M': np.nan, 'F_SNR': np.nan, 'F_CNR': np.nan,
                    'AUC': np.nan, 'AUCn': np.nan, 'TTlb': np.nan, 'TTm': np.nan, 'TThb': np.nan, 'OnT': np.nan, 'OnTSc': np.nan,
                    'ROISize': roi_size, 'Denoise_RMS': denoise_rms, 'VesType': ves_type
                })
                
            # Replace or add to dataset results
            existing_idx = -1
            for k, rec in enumerate(self.dataset_results):
                if int(rec.get('ROI', 0)) == self.current_roi_idx + 1:
                    existing_idx = k
                    break
                    
            if existing_idx >= 0:
                self.dataset_results[existing_idx] = record
            else:
                self.dataset_results.append(record)
                
            # Calculate OnTSc (Onset time in Scan) relative to minimum OnT
            valid_onts = [r['OnT'] for r in self.dataset_results if 'OnT' in r and not np.isnan(r['OnT'])]
            if len(valid_onts) > 0:
                min_ont = np.min(valid_onts)
                for r in self.dataset_results:
                    if 'OnT' in r and not np.isnan(r['OnT']):
                        r['OnTSc'] = r['OnT'] - min_ont
                        
            # Save results list as CSV
            df = pd.DataFrame(self.dataset_results)
            # Sort by ROI number
            df = df.sort_values(by="ROI").reset_index(drop=True)
            
            # Reorder columns to match standard batch format
            cols_order = [
                'ROI', 'SubjNum', 'Exp', 'InitAmp', 'InitT2p', 'InitFWHM', 'InitM', 'InitSNR', 'InitCNR',
                'Click1_Start_T', 'Click2_Onset_T', 'Click3_Peak_T', 'Click4_End_T',
                'F_Amp', 'F_T2p', 'F_FWHM', 'F_M', 'F_SNR', 'F_CNR',
                'AUC', 'AUCn', 'TTlb', 'TTm', 'TThb', 'OnT', 'OnTSc',
                'ROISize', 'Denoise_RMS', 'VesType'
            ]
            for col in cols_order:
                if col not in df.columns:
                    df[col] = np.nan
            df = df[cols_order]
            
            df.to_csv(self.out_csv, index=False)
            
            # Save screenshot of the fit to the plots/ folder
            plots_dir = os.path.join(os.path.dirname(self.out_csv), "plots")
            os.makedirs(plots_dir, exist_ok=True)
            base_name = os.path.basename(self.current_triplet[0]).replace('.tif', '')
            plot_path = os.path.join(plots_dir, f"{base_name}_ROI_{self.current_roi_idx + 1}_fit.png")
            self.fig.savefig(plot_path, dpi=150, bbox_inches='tight')
            
            self.lbl_status.config(text=f"Saved CSV & Plot to plots/ folder!", foreground="#27ae60")
            messagebox.showinfo("Success", f"Fitted parameters for ROI {self.current_roi_idx+1} successfully exported!\nResults saved to:\n{self.out_csv}")
            
        except Exception as e:
            messagebox.showerror("Export Error", str(e))

if __name__ == "__main__":
    root = tk.Tk()
    app = BolusTrackingGUI(root)
    root.mainloop()
