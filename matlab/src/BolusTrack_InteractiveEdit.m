function BolusTrack

% GUI tool to analyze changes in signal intensity 
% (e.g. fluorescence) in a time-series. Through the panel, the user can
% load the data (.tiff files), import ROIs, input the Frame Rate, and
% estimate the initial fitting parameters using the cursor. The initial
% parameters will then be used to fit a gamma function through the data
% points. 

% Original author: Paolo Bazzigaluppi; January 2019.
% Modified: Adrienne Dorr, April 2026.
%   - Removed Select ROIs (use drawROI.m instead)
%   - Added Pop-out View for interactive per-ROI and per-vertex editing
%   - Added Save ROIs button to save adjusted ROI positions before fitting
%   - Added Load Metadata button to auto-populate frame rate from .txt file
%     (use the ORIGINAL unclipped metadata .txt, not a cropped version)
%   - Added optional robust temporal denoising with before/after toggle
%   - Each new trace defaults to RAW view (denoising is per-trace opt-in)
%   - Auto-saves progress every 5 fitted traces to prevent data loss
%   - Resume Session button to recover after a crash
%   - Fixed Import ROIs to handle both .poli and .Position mask formats

%% Set-up main figure and buttons

f = figure('numbertitle', 'off', ...
    'name', 'Bolus Tracking Analysis', ...
    'color','w',...
    'menubar','none', ...
    'toolbar','none', ...
    'resize', 'on', ...
    'tag','main', ...
    'renderer','painters', ...
    'position',[400 200 800 600]);
     
% Import and preview control
Ip = uicontrol;
Ip.Position =  [10 550 80 25];
Ip.String = 'Load Data';
Ip.Callback = @LoadButtonPushed;

% Load Metadata button (auto frame rate)
uicontrol('Position', [10 520 80 25], ...
    'String', 'Load Metadata', ...
    'Callback', @LoadMetadataButtonPushed);
    
% Set the Frame Rate
FrText = uicontrol ('Style','text','String','Frame Rate');
FrText.Position = [10 490 80 20];
persistent Fr
Fr = uicontrol('Style','edit');  
Fr.Position = [10 465 80 25];
Fr.Callback = @FrCall;

% Import ROIs control
uicontrol('Position', [10 425 80 25], ...
    'String', 'Import ROIs', ...
    'Callback', @ROIImportButtonPushed);

% Pop-out ROI view with interactive editing
uicontrol('Position', [10 395 80 25], ...
    'String', 'Pop-out View', ...
    'Callback', @PopOutButtonPushed);

% Save ROIs button — save adjusted ROI positions before fitting
uicontrol('Position', [10 365 80 25], ...
    'String', 'Save ROIs', ...
    'Callback', @SaveROIsButtonPushed);

% Show ROIs tc control
uicontrol('Position', [10 335 80 25], ...
    'String', 'Show ROIs tc', ...
    'Callback', @ROItcShowButtonPushed);

% Denoise threshold control
DnText = uicontrol('Style','text','String','Denoise (SD)');
DnText.Position = [10 305 80 20];
DnThresh = uicontrol('Style','edit','String','2.0');
DnThresh.Position = [10 280 80 25];
DnThresh.Callback = @DnThreshCall;

% Apply Denoise button
uicontrol('Position', [10 250 80 25], ...
    'String', 'Apply Denoise', ...
    'Callback', @ApplyDenoiseButtonPushed);

% Toggle Raw/Denoised button
uicontrol('Position', [10 220 80 25], ...
    'String', 'Toggle Raw', ...
    'Callback', @ToggleRawButtonPushed);

% Resume Session button
uicontrol('Position', [10 180 80 25], ...
    'String', 'Resume Session', ...
    'Callback', @ResumeSessionButtonPushed, ...
    'ForegroundColor', [0.8 0.2 0.2]);

% "save/next" button control
e = uicontrol;
e.Position = [615 510 120 25];
e.String = 'Save values/next trace';
e.Callback = @SaveValueButtonPushed;

% "gamma fit" button control
g = uicontrol;
g.String = 'Fit Gamma function';
g.Callback = @GammaFit;
g.Position = [615 550 120 25];

% Set the Experimental condition
ExpConCallText = uicontrol ('Style','text','String','Experiment:');
ExpConCallText.Position = [615 480 80 20];
ExpCon = uicontrol('Style','edit');  
ExpCon.Position = [690 480 40 25];
ExpCon.Callback = @ExpConCall;

% Set the Subject number
SubjIDCallText = uicontrol ('Style','text','String','Subject ID:');
SubjIDCallText.Position = [615 450 80 20];
SubjID = uicontrol('Style','edit');  
SubjID.Position = [690 450 40 25];
SubjID.Callback = @SubjIDCall;

% Vessel designation dropdown — set per vessel during fitting
VesTypeText = uicontrol('Style','text','String','Vessel Type:');
VesTypeText.Position = [615 420 60 20];
VesType = uicontrol('Style','popupmenu', ...
    'String', {'--','A (artery)','V (vein)','C (capillary)','U (unknown)'}, ...
    'Position', [680 420 60 25], ...
    'Callback', @VesTypeCall);

% "Export" button control
k = uicontrol;
k.String = 'Export results';
k.Callback = @ExportButtonPushed;
k.Position = [615 390 120 25];

% Clear Initial Values
k = uicontrol;
k.String = 'NaN initial Values';
k.Callback = @ClearInitValsButtonPushed;
k.Position = [630 310 100 25];

% Clear all
k = uicontrol;
k.String = 'Clear All';
k.Callback = @ClearAllButtonPushed;
k.Position = [490 310 100 25];

% parameter 1 controls
text1 = uicontrol ('Style','text','String','Amplitude');
text1.Position = [490 550 60 20];
param1 = uicontrol('Style','edit');
param1.Position = [490 530 60 20];
param1.Callback = @param1In;    

% parameter 2 controls
text2 = uicontrol ('Style','text','String','Time to Peak');
text2.Position = [490 490 60 30];
param2 = uicontrol('Style','edit');
param2.Position = [490 470 60 20];
param2.Callback = @param2In;

% parameter 3 controls
text3 = uicontrol ('Style','text','String','Fit Start');
text3.Position = [490 430 60 20];
param3 = uicontrol('Style','edit');
param3.Position = [490 410 60 20];
param3.Callback = @param3In;

% parameter 4 controls
text4 = uicontrol ('Style','text','String','Fit End');
text4.Position = [490 370 60 20];
param4 = uicontrol('Style','edit');
param4.Position = [490 350 60 20];
param4.Callback = @param4In;

% parameter 5 controls
text5 = uicontrol ('Style','text','String','FWHM');
text5.Position = [600 370 60 20];
param5 = uicontrol('Style','edit');
param5.Position = [600 350 60 20];
param5.Callback = @param5In;

% parameter 6 controls
text6 = uicontrol ('Style','text','String','Baseline shift');
text6.Position = [670 370 60 30];
param6 = uicontrol('Style','edit');
param6.Position = [670 350 60 20];
param6.Callback = @param6In;

% preallocate plot space for time-series visualization
ax = axes(f);
ax.Units = 'pixels';
ax.Position = [160 350 250 220];
ax.XLabel.String = 'Time(s)';
ax.YLabel.String = 'Signal intensity (a.u.)';
assignin('base','ax',ax);

%% declare global variables
clear global idx
clear global fitOut
global idx
idx = 1;
global UpF
UpF = 20;
global fitOut
global denoiseSD
denoiseSD = 2.0;
global showingDenoised
showingDenoised = false;
global autoSavePath
autoSavePath = '';
global fittedCount
fittedCount = 0;
global vesDesignation
vesDesignation = {};

% scale to normalized unit
set(findall(f, '-property', 'Units'), 'Units', 'norm')

%% local functions 

    function DnThreshCall(~,~)
        denoiseSD = str2double(DnThresh.String);
        disp(['Denoise threshold set to ' num2str(denoiseSD) ' SD']);
    end

    function ClearInitValsButtonPushed(~,~)
        param1.String = NaN;
        param2.String = NaN;
        param3.String = NaN;
        param4.String = NaN;
        param5.String = NaN;
        param6.String = NaN;
    end

    function ClearAllButtonPushed(~,~)
        ax = evalin ('base','ax');
        ax1 = evalin('base','ax1');
        delete(ax)
        delete(ax1)
        clear global idx
        clear global fitOut 
        evalin('base', 'clearvars *')
    end

    function LoadMetadataButtonPushed(~,~)
        [metaFile, metaPath] = uigetfile({'*.txt','Text files'}, 'Select metadata .txt file (original, unclipped)');
        if isequal(metaFile, 0); return; end
        
        fid = fopen([metaPath metaFile], 'r');
        metaText = fread(fid, '*char')';
        fclose(fid);
        
        calcFr = parseFrameRateFromMetadata(metaText);
        
        if ~isnan(calcFr)
            Fr.String = num2str(calcFr);
            assignin('base', 'Fr', Fr.String);
            disp(['Frame rate auto-set to: ' num2str(calcFr) ' fps']);
        else
            disp('Could not parse frame rate from metadata file.');
            disp('Set frame rate manually.');
        end
    end

    function ROIImportButtonPushed(~,~)
    
    [fileNm,pathNm] = uigetfile;
    temp =  struct(load([pathNm fileNm]));
    
    nROI = length(temp.maskObj);
    
    for qq = 1:nROI
        
        if isstruct(temp.maskObj) && isfield(temp.maskObj, 'poli')
            thisPos = temp.maskObj(qq).poli.Position;
        elseif isstruct(temp.maskObj) && isfield(temp.maskObj, 'Position')
            thisPos = temp.maskObj(qq).Position;
        else
            thisPos = temp.maskObj(qq).Position;
        end
        
        if ~isempty(thisPos)
            NewROI(qq) = images.roi.Polygon(gca, 'Position', thisPos, ...
                'LineWidth', 0.5, 'InteractionsAllowed', 'translate');
        end
        
    end
           
    assignin('base','NewROI',NewROI)
    autoSavePath = pathNm;
    
end

    function PopOutButtonPushed(~,~)
    
    fileIn = evalin('base','fileIn');
    NewROI = evalin('base','NewROI');
    nROI = length(NewROI);
    
    fPop = figure('Name','ROI Editor - edit ROIs then click Apply Edits', ...
        'NumberTitle','off','Color','w','ToolBar','figure');
    imagesc(squeeze(max(fileIn,[],1)));
    colormap gray
    hold on
    axis image
    
    PopROI = cell(1, nROI);
    for rr = 1:nROI
        pos = NewROI(rr).Position;
        PopROI{rr} = images.roi.Polygon(gca,'Position',pos,'LineWidth',0.5,'InteractionsAllowed','all','Color','r');
        cx = mean(pos(:,1));
        cy = mean(pos(:,2));
        text(cx, cy, num2str(rr), 'Color', 'y', 'FontSize', 7, 'FontWeight', 'bold', 'HorizontalAlignment', 'center');
    end
    
    hold off
    title('Drag ROIs or individual vertices. Zoom with toolbar. Click Apply Edits when done.')
    
    uicontrol(fPop, 'Style', 'pushbutton', 'String', 'Apply Edits', ...
        'Position', [10 10 120 30], ...
        'Callback', @(~,~) applyPopOutEdits(PopROI));
    
    end

    function applyPopOutEdits(PopROI)
    
    nROI = length(PopROI);
    ax1 = evalin('base','ax1');
    
    try
        oldROI = evalin('base','NewROI');
        for rr = 1:length(oldROI)
            if isvalid(oldROI(rr))
                delete(oldROI(rr));
            end
        end
    catch
    end
    
    for rr = 1:nROI
        editedPos = PopROI{rr}.Position;
        NewROI(rr) = images.roi.Polygon(ax1, 'Position', editedPos, ...
            'LineWidth', 0.5, 'InteractionsAllowed', 'translate');
    end
    
    assignin('base','NewROI',NewROI);
    disp(['Applied edits for ' num2str(nROI) ' ROIs.']);
    disp('Click Save ROIs to save the adjusted positions, then Show ROIs tc to extract time-courses.');
    
    end

    function SaveROIsButtonPushed(~,~)
    % Save the current ROI positions as a maskObj .mat file.
    % Use this AFTER importing and adjusting ROIs (via Pop-out View
    % or manual dragging) and BEFORE clicking Show ROIs tc.
    % This ensures you have a saved copy of the finalized ROI
    % positions independent of the bolus fitting process.
    
    try
        NewROI = evalin('base','NewROI');
    catch
        errordlg('No ROIs loaded. Import ROIs first.');
        return
    end
    
    nROI = length(NewROI);
    
    % Build a clean struct array with .Position field
    clear maskObj
    for rr = 1:nROI
        maskObj(rr).Position = NewROI(rr).Position; %#ok<AGROW>
    end
    
    % Prompt for save location
    [saveFile, savePath] = uiputfile('*.mat', 'Save adjusted ROIs as', [autoSavePath 'adjusted_maskObj.mat']);
    if ~isequal(saveFile, 0)
        save([savePath saveFile], 'maskObj');
        disp(['Saved ' num2str(nROI) ' ROIs to: ' savePath saveFile]);
        disp('You can now click Show ROIs tc to begin fitting.');
    else
        disp('Save cancelled.');
    end
    
    end

    function ROItcShowButtonPushed(~,~)
    
    NewROI = evalin('base','NewROI');
    VesNum = length(NewROI);
    fileIn = evalin('base','fileIn');
    ImageIn = permute(fileIn,[2,3,1]);
    Fr = str2double(evalin('base','Fr'));
    
    for tt = 1 : VesNum
                
        mask(tt,:,:) = poly2mask(NewROI(tt).Position(:,1),NewROI(tt).Position(:,2), ...
            size(fileIn,2),size(fileIn,3));
        maskNum(tt,:,:) = mask(tt,:,:) * tt;
        xx(tt).c = NewROI(tt).Position(:,1);
        yy(tt).c = NewROI(tt).Position(:,2);
           
        fitOut(tt).yRaw = sum(sum(ImageIn .* squeeze(mask(tt,:,:)))) / sum(sum(squeeze(mask(tt,:,:))));
        fitOut(tt).yRawOriginal = fitOut(tt).yRaw;
        fitOut(tt).yDenoised = [];
        fitOut(tt).VNum = tt;
        fitOut(tt).denoiseSD = NaN;
        fitOut(tt).vesType = 'U';
        
        fitOut(tt).tlRaw = linspace(0,length(fitOut(tt).yRaw)/Fr,length(fitOut(tt).yRaw));
        
        fitOut(tt).tlUs = linspace(0,length(fitOut(tt).yRaw)/Fr,length(fitOut(tt).yRaw)*UpF);
        foo1 = fitOut(tt).tlRaw';
        foo2 = fitOut(tt).yRaw;
        fitOut(tt).y = spline(foo1,foo2,fitOut(tt).tlUs);
                
    end
    
    showingDenoised = false;
    fittedCount = 0;
    vesDesignation = cell(1, VesNum);
    
    plot(fitOut(1).tlUs,fitOut(1).y,'Parent',ax);
    ax.XLabel.String = 'Time(s)';
    ax.YLabel.String = 'Signal intensity (a.u.)';
    title(ax, ['ROI ' num2str(idx) ' — RAW']);
    dcm_obj = datacursormode(gcf); 
    set(dcm_obj,'Enable','on'); 
    
    assignin('base','maskNum',maskNum)
    assignin('base','maskObj',NewROI)
    
    autoEstimateParams();

    
end   

    function ResumeSessionButtonPushed(~,~)
    
    [resumeFile, resumePath] = uigetfile('*.mat', 'Select autosave_progress.mat');
    if isequal(resumeFile, 0); disp('Cancelled.'); return; end
    
    savedData = load([resumePath resumeFile]);
    
    if ~isfield(savedData, 'fitOut') || ~isfield(savedData, 'idx')
        errordlg('Selected file does not contain auto-save data (fitOut and idx).');
        return
    end
    
    savedFitOut = savedData.fitOut;
    savedIdx = savedData.idx;
    
    if isempty(fitOut)
        errordlg('Click Show ROIs tc first to generate time-courses, then Resume Session.');
        return
    end
    
    if length(fitOut) ~= length(savedFitOut)
        warndlg(['Auto-save has ' num2str(length(savedFitOut)) ' ROIs but current session has ' ...
            num2str(length(fitOut)) '. Proceeding — check that you loaded the correct maskObj.']);
    end
    
    resumeFrom = 0;
    for rr = 1:min(length(savedFitOut), length(fitOut))
        if isfield(savedFitOut(rr), 'mse') && ~isempty(savedFitOut(rr).mse)
            fitFields = {'InitAmp','InitT2p','InitFWHM','InitM', ...
                         'beta','res','J','covb','mse','CI', ...
                         'fitTr','fitTrN','AUC','AUCn', ...
                         'OnT','TTm','TTlb','TThb', ...
                         'tr2pl','rawDs','fittedOn'};
            for ff = 1:length(fitFields)
                fname = fitFields{ff};
                if isfield(savedFitOut(rr), fname)
                    fitOut(rr).(fname) = savedFitOut(rr).(fname);
                end
            end
            resumeFrom = rr;
        end
    end
    
    if resumeFrom == 0
        disp('No completed fits found in auto-save file.');
        return
    end
    
    idx = resumeFrom + 1;
    fittedCount = resumeFrom;
    
    if idx <= length(fitOut)
        showingDenoised = false;
        rawTrace = fitOut(idx).yRawOriginal;
        foo1 = fitOut(idx).tlRaw';
        fitOut(idx).yRaw = rawTrace;
        fitOut(idx).y = spline(foo1, rawTrace, fitOut(idx).tlUs);
        
        plot(fitOut(idx).tlUs, fitOut(idx).y, 'Parent', ax);
        ax.XLabel.String = 'Time(s)';
        ax.YLabel.String = 'Signal intensity (a.u.)';
        title(ax, ['ROI ' num2str(idx) ' — RAW']);
        dcm_obj = datacursormode(gcf); 
        set(dcm_obj,'Enable','on');
        
        disp(['Restored ' num2str(resumeFrom) ' completed fits.']);
        disp(['Resuming at ROI ' num2str(idx) ' of ' num2str(length(fitOut)) '.']);
    else
        disp(['All ' num2str(resumeFrom) ' traces were already fitted. Ready to Export.']);
    end
    
    if idx <= length(fitOut)
        autoEstimateParams();
    end
    
    end

    function ApplyDenoiseButtonPushed(~,~)
    
    Fr = str2double(evalin('base','Fr'));
    halfWin = 5; % 5 frames each side (~2s at 5fps) — preserves fast arteriolar bolus shape
    
    cleanTrace = denoiseTrace(fitOut(idx).yRawOriginal, halfWin, denoiseSD);
    
    fitOut(idx).yDenoised = cleanTrace;
    
    fitOut(idx).yRaw = cleanTrace;
    foo1 = fitOut(idx).tlRaw';
    fitOut(idx).y = spline(foo1, cleanTrace, fitOut(idx).tlUs);
    
    showingDenoised = true;
    
    plot(fitOut(idx).tlUs, fitOut(idx).y, 'Parent', ax);
    ax.XLabel.String = 'Time(s)';
    ax.YLabel.String = 'Signal intensity (a.u.)';
    title(ax, ['ROI ' num2str(idx) ' — DENOISED (' num2str(denoiseSD) ' SD)']);
    dcm_obj = datacursormode(gcf); 
    set(dcm_obj,'Enable','on');
    
    disp(['Denoising applied to ROI ' num2str(idx) ' at ' num2str(denoiseSD) ' SD.']);
    
    autoEstimateParams();
    
    end

    function ToggleRawButtonPushed(~,~)
    
    Fr = str2double(evalin('base','Fr'));
    
    if showingDenoised
        rawTrace = fitOut(idx).yRawOriginal;
        foo1 = fitOut(idx).tlRaw';
        yDisp = spline(foo1, rawTrace, fitOut(idx).tlUs);
        
        plot(fitOut(idx).tlUs, yDisp, 'Parent', ax);
        title(ax, ['ROI ' num2str(idx) ' — RAW']);
        showingDenoised = false;
        
        fitOut(idx).yRaw = rawTrace;
        fitOut(idx).y = yDisp;
    else
        if ~isempty(fitOut(idx).yDenoised)
            cleanTrace = fitOut(idx).yDenoised;
            foo1 = fitOut(idx).tlRaw';
            yDisp = spline(foo1, cleanTrace, fitOut(idx).tlUs);
            
            plot(fitOut(idx).tlUs, yDisp, 'Parent', ax);
            title(ax, ['ROI ' num2str(idx) ' — DENOISED (' num2str(denoiseSD) ' SD)']);
            showingDenoised = true;
            
            fitOut(idx).yRaw = cleanTrace;
            fitOut(idx).y = yDisp;
        else
            disp('No denoised data for this trace. Click Apply Denoise first.');
        end
    end
    
    ax.YLabel.String = 'Signal intensity (a.u.)';
    dcm_obj = datacursormode(gcf); 
    set(dcm_obj,'Enable','on');
    
    autoEstimateParams();
    
    end

    function SaveValueButtonPushed(~,~)
    
    % Check for negative FWHM before saving
    if isfield(fitOut(idx), 'beta') && ~isempty(fitOut(idx).beta) && ~all(isnan(fitOut(idx).beta))
        if fitOut(idx).beta(3) <= 0
            answer = questdlg(sprintf('Vessel %d has negative FWHM (%.3f). Save anyway?', ...
                idx, fitOut(idx).beta(3)), ...
                'Fit quality warning', ...
                'Save as-is', 'Skip (NaN)', 'Go back and refit', 'Go back and refit');
            switch answer
                case 'Skip (NaN)'
                    fitOut(idx).beta = [NaN, NaN, NaN, NaN];
                    fitOut(idx).AUC = NaN;
                    fitOut(idx).AUCn = NaN;
                    fitOut(idx).OnT = NaN;
                    fitOut(idx).TTm = NaN;
                    fitOut(idx).TTlb = NaN;
                    fitOut(idx).TThb = NaN;
                case 'Go back and refit'
                    return;
                case 'Save as-is'
                    % proceed normally
            end
        end
    end
    
    fittedCount = fittedCount + 1;
    
    % Reset vessel type dropdown for next vessel
    VesType.Value = 1;  % reset to '--'
    
    if mod(fittedCount, 5) == 0
        try
            mask = evalin('base','mask');
            maskNum = evalin('base','maskNum');
            autoFile = [autoSavePath 'autosave_progress.mat'];
            save(autoFile, 'fitOut', 'mask', 'maskNum', 'idx');
            disp(['Auto-saved progress (' num2str(fittedCount) ' traces fitted) to: ' autoFile]);
        catch
            disp('Auto-save failed (check path and permissions).');
        end
    end
    
    if idx+1 <= length(fitOut)
        
        idx = idx + 1;
        
        rawTrace = fitOut(idx).yRawOriginal;
        foo1 = fitOut(idx).tlRaw';
        fitOut(idx).yRaw = rawTrace;
        fitOut(idx).y = spline(foo1, rawTrace, fitOut(idx).tlUs);
        showingDenoised = false;
        
        plot(fitOut(idx).tlUs, fitOut(idx).y, 'Parent', ax);
        ax.XLabel.String = 'Time(s)';
        ax.YLabel.String = 'Signal intensity (a.u.)';
        title(ax, ['ROI ' num2str(idx) ' — RAW']);
        dcm_obj = datacursormode(gcf); 
        set(dcm_obj,'Enable','on'); 
        
        autoEstimateParams();
                        
    elseif idx+1 > length(fitOut)
        
        plot(NaN,'Parent',ax);
        assignin('base','idx',idx);
        ax.XLabel.String = 'Time(s)';
        ax.YLabel.String = 'Signal intensity (a.u.)';
        
        % Ensure all OnT fields are numeric (not empty [])
        for oo = 1 : length(fitOut)
            if isempty(fitOut(oo).OnT), fitOut(oo).OnT = NaN; end
        end
        
        OnTzero = min([fitOut.OnT]); 
        for oo = 1 : length(fitOut)
            if isnan(fitOut(oo).OnT)
                fitOut(oo).OnTSc = NaN;
            else
                fitOut(oo).OnTSc = fitOut(oo).OnT - OnTzero;
            end
        end
        
        assignin('base','fitOut',fitOut);
        
        try
            mask = evalin('base','mask');
            maskNum = evalin('base','maskNum');
            autoFile = [autoSavePath 'autosave_progress.mat'];
            save(autoFile, 'fitOut', 'mask', 'maskNum', 'idx');
            disp(['Final auto-save complete (' num2str(fittedCount) ' traces fitted).']);
        catch
            disp('Final auto-save failed.');
        end

    end

end

    function autoEstimateParams(~,~)
        % Automatically estimate the initial parameters for the current trace
        % and populate the GUI fields.
        
        Fr = str2double(evalin('base','Fr'));
        tr = fitOut(idx).y; % Upsampled trace
        t_us = fitOut(idx).tlUs; % Upsampled time vector
        
        % 1. Baseline: Median of the first 2 seconds (or 10% of trace)
        nBaseFrames = min(round(2 * Fr * UpF), round(length(tr) * 0.1));
        if nBaseFrames < 1, nBaseFrames = 1; end
        baseline = median(tr(1:nBaseFrames));
        sdBase = std(tr(1:nBaseFrames));
        
        % Ignore points for boundary spline overshoot
        ignorePoints = min(round(3.0 * Fr * UpF), round(length(tr) * 0.05));
        validEnd = length(tr) - ignorePoints;
        if validEnd < 1, validEnd = length(tr); end
        
        % 2. Steepest Rise to Peak Detection
        smoothWinRise = round(1.0 * Fr * UpF);
        if smoothWinRise < 1, smoothWinRise = 1; end
        smoothedRise = smoothdata(tr, 'movmean', smoothWinRise);
        derivRise = diff(smoothedRise);
        derivRise(end+1) = derivRise(end); % Keep same length
        
        [~, riseIdx] = max(derivRise(1:validEnd));
        
        searchWin = round(8.0 * Fr * UpF);
        peakSearchEnd = min(validEnd, riseIdx + searchWin);
        [maxVal, maxIdxRel] = max(tr(riseIdx:peakSearchEnd));
        maxIdx = maxIdxRel + riseIdx - 1;
        amp = maxVal - baseline;
        
        % 3. Walk backward for Onset
        thresh = baseline + max(3 * sdBase, 0.05 * amp);
        belowThreshCandidates = find(tr(1:maxIdx) < thresh);
        if isempty(belowThreshCandidates)
            startIdx = 1;
        else
            startIdx = belowThreshCandidates(end);
        end
        tStart = t_us(startIdx);
        startAmp = tr(startIdx);
        
        t2p = t_us(maxIdx) - tStart;
        t2p = max(t2p, 0.01);
        
        % 4. Fit End (Offset): Hybrid local-minimum capped end detection
        smoothWinEnd = round(0.8 * Fr * UpF);
        if smoothWinEnd < 1, smoothWinEnd = 1; end
        smoothedEnd = smoothdata(tr, 'movmean', smoothWinEnd);
        derivEnd = diff(smoothedEnd);
        derivEnd(end+1) = derivEnd(end);
        
        % 4a. Find local minimum to cap search window
        downslopeCandidates = find(derivEnd(maxIdx:validEnd) < 0);
        if isempty(downslopeCandidates)
            localMinIdx = validEnd;
        else
            downslopeStart = downslopeCandidates(1) + maxIdx - 1;
            nonDecayingCandidates = find(derivEnd(downslopeStart:validEnd) >= 0);
            if isempty(nonDecayingCandidates)
                localMinIdx = validEnd;
            else
                localMinIdx = nonDecayingCandidates(1) + downslopeStart - 1;
            end
        end
        if localMinIdx <= maxIdx
            localMinIdx = validEnd;
        end
        
        % 4b. Thresholding in capped window [maxIdx, localMinIdx]
        smoothWinEndBaseline = round(1.0 * Fr * UpF);
        if smoothWinEndBaseline < 1, smoothWinEndBaseline = 1; end
        smoothedEndBaseline = smoothdata(tr, 'movmean', smoothWinEndBaseline);
        nEndFrames = min(round(2 * Fr * UpF), round(validEnd * 0.1));
        if nEndFrames < 1, nEndFrames = 1; end
        
        endBaseline = median(smoothedEndBaseline(validEnd-nEndFrames+1:validEnd));
        endSdBase = std(smoothedEndBaseline(validEnd-nEndFrames+1:validEnd));
        
        endThresh = endBaseline + max(3 * endSdBase, 0.03 * amp);
        if endSdBase == 0 || endThresh >= maxVal
            endThresh = endBaseline + 0.1 * amp;
        end
        
        aboveEndThreshCandidates = find(smoothedEnd(maxIdx:localMinIdx) > endThresh);
        if isempty(aboveEndThreshCandidates)
            endIdx = localMinIdx;
        else
            endIdx = aboveEndThreshCandidates(end) + maxIdx - 1;
        end
        if endIdx >= validEnd
            endIdx = validEnd;
        end
        tEnd = t_us(endIdx);
        endAmp = tr(endIdx);
        
        % 5. FWHM
        halfMax = baseline + 0.5 * amp;
        idxUp = find(tr(1:maxIdx) >= halfMax, 1, 'first');
        idxDown = find(tr(maxIdx:end) <= halfMax, 1, 'first');
        if isempty(idxUp), idxUp = startIdx; end
        if isempty(idxDown)
            tDown = tEnd;
        else
            tDown = t_us(idxDown + maxIdx - 1);
        end
        tUp = t_us(idxUp);
        fwhm = tDown - tUp;
        if fwhm <= 0, fwhm = 0.5; end % Fallback
        
        % 6. Baseline shift (absolute baseline)
        bslnShift = baseline;
        
        % Populate GUI fields
        param1.String = num2str(amp);
        param2.String = num2str(t2p);
        param3.String = num2str(tStart);
        param4.String = num2str(tEnd);
        param5.String = num2str(fwhm);
        param6.String = num2str(bslnShift);
        
        % Update base workspace variables used by manual click callbacks
        assignin('base','param1',param1.String);
        assignin('base','param2',param2.String);
        assignin('base','param3',param3.String);
        assignin('base','param4',param4.String);
        assignin('base','param5',param5.String);
        assignin('base','param6',param6.String);
        assignin('base','FitStAmp',startAmp);
        assignin('base','FitEndAmp',endAmp);
        
        % Plot the automated clicks on the existing axis
        ax = evalin('base','ax');
        hold(ax, 'on');
        % Peak (Red)
        plot(ax, t2p, maxVal, 'r*', 'MarkerSize', 10, 'LineWidth', 1.5);
        % Start (Green)
        plot(ax, tStart, startAmp, 'g*', 'MarkerSize', 10, 'LineWidth', 1.5);
        % End (Blue)
        plot(ax, tEnd, endAmp, 'b*', 'MarkerSize', 10, 'LineWidth', 1.5);
        % FWHM (Black line)
        plot(ax, [tUp tDown], [halfMax halfMax], 'k-', 'LineWidth', 2);
        hold(ax, 'off');
        
        % Save visualization to a folder
        try
            visFolder = fullfile(autoSavePath, 'auto_clicks');
            if ~exist(visFolder, 'dir')
                mkdir(visFolder);
            end
            exportgraphics(ax, fullfile(visFolder, sprintf('ROI_%03d_autoclicks.png', idx)), 'Resolution', 150);
        catch
            % Fallback
        end
        
        disp(['Auto-estimated params for ROI ' num2str(idx)]);
    end

    function GammaFit(~,~)
    
    Fr = str2double(evalin('base','Fr'));
   
    st = round(Fr*UpF*str2double(param3.String));
    en = round(Fr*UpF*str2double(param4.String));
     
    tr = fitOut(idx).y(st:en);
    
    options = statset('RobustWgtFun','cauchy');
    
    xxx = linspace(0,length(tr)/(Fr*UpF),length(tr));
    
    paramsInit = [str2double(param1.String)...
        (str2double(param2.String)- (st/(Fr*UpF)))...
        (str2double(param5.String))...
        str2double(param6.String)];
    
    fitOut(idx).InitAmp = str2double(param1.String);
    fitOut(idx).InitT2p = str2double(param2.String)- (st/(Fr*UpF));
    fitOut(idx).InitFWHM = str2double(param5.String);
    fitOut(idx).InitM = str2double(param6.String);
    
    [fitOut(idx).beta,fitOut(idx).res,fitOut(idx).J,fitOut(idx).covb,fitOut(idx).mse] = ...
        nlinfit(xxx,tr,@gammaFun,paramsInit,options);
    
    fitOut(idx).CI = nlparci(fitOut(idx).beta,fitOut(idx).res,'covar',fitOut(idx).covb);
    
    fitOut(idx).fitTr = feval(@gammaFun,fitOut(idx).beta,xxx);
    fitOut(idx).fitTrN = ((fitOut(idx).fitTr - min(fitOut(idx).fitTr)) / ...
        (max(fitOut(idx).fitTr) - min(fitOut(idx).fitTr)));
    fitOut(idx).AUC = trapz(fitOut(idx).fitTr);
    fitOut(idx).AUCn = trapz(fitOut(idx).fitTrN);
    
    fitOut(idx).OnT = (find((diff(find(fitOut(idx).fitTrN' < 0.1))) == 1, 1, 'last') + 1) / (Fr*UpF);
    fitOut(idx).TTm = abs(fitOut(idx).beta(2) -  fitOut(idx).OnT);
    fitOut(idx).TTlb = abs(fitOut(idx).CI(2,1) -  fitOut(idx).OnT);
    fitOut(idx).TThb = abs(fitOut(idx).CI(2,2) - fitOut(idx).OnT);
    
    if showingDenoised
        fitOut(idx).fittedOn = ['denoised_' num2str(denoiseSD) 'SD'];
        fitOut(idx).denoiseSD = denoiseSD;
    else
        fitOut(idx).fittedOn = 'raw';
        fitOut(idx).denoiseSD = NaN;
    end
    
    % Store vessel designation if set
    if length(vesDesignation) >= idx && ~isempty(vesDesignation{idx}) && ~strcmp(vesDesignation{idx}, '--')
        fitOut(idx).vesType = vesDesignation{idx};
    else
        fitOut(idx).vesType = 'U';
    end
    
    % Flag negative or zero FWHM
    if fitOut(idx).beta(3) <= 0
        warning('BolusTrack:negativeFWHM', ...
            'Vessel %d: Fitted FWHM is %.3f (negative or zero). Fit is unreliable.', ...
            idx, fitOut(idx).beta(3));
        hold on
        text(fitOut(idx).beta(2) + (st/(Fr*UpF)), max(fitOut(idx).y) * 0.8, ...
            'NEGATIVE FWHM', ...
            'Color', 'r', 'FontSize', 10, 'FontWeight', 'bold', ...
            'Parent', ax);
        hold off
    end
    
    % Flag empty transit times (replace [] with NaN)
    if isempty(fitOut(idx).OnT), fitOut(idx).OnT = NaN; end
    if isempty(fitOut(idx).TTm), fitOut(idx).TTm = NaN; end
    if isempty(fitOut(idx).TTlb), fitOut(idx).TTlb = NaN; end
    if isempty(fitOut(idx).TThb), fitOut(idx).TThb = NaN; end
    
    fitOut(idx).tr2pl = NaN(size(fitOut(idx).y));
    fitOut(idx).tr2pl(1,st:en) = fitOut(idx).fitTr;
    
    fitOut(idx).rawDs = fitOut(idx).y(st:en);
    
    hold on
    plot(fitOut(idx).tlUs,fitOut(idx).tr2pl,'Parent',ax);
    ax.XLabel.String = 'Time(s)';
    ax.YLabel.String = 'Signal intensity (a.u.)';
    hold off
    
    disp('fitting ok')

end

    function param1In(~,~)
    dcm_obj = datacursormode(gcf);
    c_info = getCursorInfo(dcm_obj);
    param1.String = c_info.Position(2);
    assignin('base','param1',param1.String);
end

    function param2In(~,~)
    dcm_obj = datacursormode(gcf);
    c_info = getCursorInfo(dcm_obj);
    param2.String = c_info.Position(1);
    assignin('base','param2',param2.String);
end

    function param3In(~,~)
    dcm_obj = datacursormode(gcf);
    c_info = getCursorInfo(dcm_obj);
    param3.String = c_info.Position(1);
    assignin('base','param3',param3.String); 
    assignin('base','FitStAmp',c_info.Position(2));
end

    function param4In(~,~)
    dcm_obj = datacursormode(gcf);
    c_info = getCursorInfo(dcm_obj);
    param4.String = c_info.Position(1);
    assignin('base','param4',param4.String);
    assignin('base','FitEndAmp',c_info.Position(2));
end

    function param5In(~,~)
    X1 = str2double(evalin('base','param3'));
    X2 = str2double(evalin('base','param4'));
    pk = str2double(evalin('base','param2'));
    fwhm = calcFWHM(X1, pk, X2);
    param5.String = fwhm;
    assignin('base','param5',param5.String);
end

    function param6In(~,~)
    FitStAmp = (evalin('base','FitStAmp'));
    param6.String = FitStAmp;
    assignin('base','param6',param6.String);
end

    function ExportButtonPushed (~,~)
    
    fitOut = evalin('base','fitOut');
    ExpCon = evalin('base','ExpCon');
    subj_num = str2double(evalin('base','subj_num'));
    mask = evalin('base','mask');
    
    emptyIndex = find(arrayfun(@(fitOut) isempty(fitOut.mse),fitOut));
    fName = fieldnames(fitOut);
    
    idxNaN = find(~ismember(fName,'beta'));
    idxNaN = idxNaN(idxNaN>5);
    
    for nn = 1 : length(emptyIndex)
        
        fitOut(emptyIndex(nn)).(fName{find(ismember(fName,'beta'))}) = [NaN,NaN,NaN,NaN]; %#ok<*FNDSB>
        
        for ii = 1 : length(idxNaN)
            if strcmp(fName{idxNaN(ii)}, 'vesType')
                fitOut(emptyIndex(nn)).vesType = 'U';
            elseif strcmp(fName{idxNaN(ii)}, 'fittedOn')
                fitOut(emptyIndex(nn)).fittedOn = 'skipped';
            else
                fitOut(emptyIndex(nn)).(fName{idxNaN(ii)}) = NaN;
            end
        end
    end

    [TheFile,ThePath] = uiputfile('*.csv');
          
    if  exist([ThePath TheFile], 'file') == 0
        
        ff = fopen([ThePath TheFile], 'w');
        
        fprintf(ff,'%s %3s %3s %3s %3s %3s %3s %3s %3s %3s %3s %3s %3s %3s %3s %3s %3s %3s %3s %3s',...
            'subj_num,', 'ves_num,','exp,','InitAmp,','InitiT2p,','InitiFWHM,','InitM,','F_Amp,',...
            'F_T2p,','F_FWHM,','F_M,','AUC,','AUCn,','TTlb,','TTm,','TThb,','OnTSc,','ROI size,','DenoiseSD,','VesType,');
        
        fprintf(ff,'\n');
        
        for jj = 1 : length(fitOut)
            
            % Ensure no empty transit/onset values slip through as blanks
            if isempty(fitOut(jj).TTlb), fitOut(jj).TTlb = NaN; end
            if isempty(fitOut(jj).TTm),  fitOut(jj).TTm  = NaN; end
            if isempty(fitOut(jj).TThb), fitOut(jj).TThb = NaN; end
            if ~isfield(fitOut(jj), 'OnTSc') || isempty(fitOut(jj).OnTSc)
                fitOut(jj).OnTSc = NaN;
            end
            
            % Get denoise SD (NaN if not denoised or field missing)
            if isfield(fitOut(jj), 'denoiseSD') && ~isempty(fitOut(jj).denoiseSD)
                thisDenoiseSD = fitOut(jj).denoiseSD;
            else
                thisDenoiseSD = NaN;
            end
            
            % Get vessel type (U if not set)
            if isfield(fitOut(jj), 'vesType') && ~isempty(fitOut(jj).vesType)
                thisVesType = fitOut(jj).vesType;
            else
                thisVesType = 'U';
            end
            
            fprintf(ff,'%d,%d,%s,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,%d,%.1f,%s,\n', ...
                subj_num, jj, ExpCon, fitOut(jj).InitAmp, fitOut(jj).InitT2p, fitOut(jj).InitFWHM, fitOut(jj).InitM, ...
                fitOut(jj).beta(1), fitOut(jj).beta(2), fitOut(jj).beta(3), fitOut(jj).beta(4), ...
                fitOut(jj).AUC, fitOut(jj).AUCn, fitOut(jj).TTlb, fitOut(jj).TTm, fitOut(jj).TThb, ...  
                fitOut(jj).OnTSc, length(nonzeros(mask(jj,:,:))), thisDenoiseSD, thisVesType);
            
        end
        
        fclose(ff);
    end
    
    saveName = [TheFile(1:end-4) '.mat'];
    saveNameMaskObj = [TheFile(1:end-4) '_MaskObj.mat'];
    mask = evalin('base','mask');
    maskNum = evalin('base','maskNum');
    maskObj = evalin('base','maskObj');
    save([ThePath saveName], 'fitOut','mask','maskNum');
    save([ThePath saveNameMaskObj], 'maskObj');
    
    try
        autoFile = [autoSavePath 'autosave_progress.mat'];
        if exist(autoFile, 'file')
            delete(autoFile);
            disp('Auto-save file cleaned up after successful export.');
        end
    catch
    end
    
end            

    function FrCall(~,~)
        assignin('base','Fr',Fr.String);
    end

    function LoadButtonPushed(~,~)
    
    [fileInName,pathIn] = uigetfile({'*.*'},[],'/media/bazzi/paolo/');
    tiff_info = imfinfo([pathIn,fileInName]);
    
    fileIn = NaN(length(tiff_info),tiff_info(1).Height, tiff_info(1).Width);
    for jj = 1 : length(tiff_info); fileIn(jj,:,:) = imread([pathIn,fileInName], jj); end   
    
    krnSize= [3 3];
    for tt = 1 : size(fileIn,1); fileIn(tt,:,:) = medfilt2(squeeze(fileIn(tt,:,:)),krnSize); end
   
    ax1 = subplot(2,2,3:4);
    imagesc(squeeze(max(max(fileIn,2))),'Parent',ax1)
    assignin('base','ax1',ax1);
    hold on
    xlabel('pixel')
    ylabel('pixel')
    colormap gray
    
    assignin('base','fileIn',fileIn);
    autoSavePath = pathIn;
    
end

    function ExpConCall(~,~)
        assignin('base','ExpCon',ExpCon.String);
    end

    function SubjIDCall(~,~)
        assignin('base','subj_num',SubjID.String);
    end

    function VesTypeCall(~,~)
        typeOptions = {'--','A','V','C','U'};
        selectedIdx = VesType.Value;
        vesDesignation{idx} = typeOptions{selectedIdx};
    end
    
end 
