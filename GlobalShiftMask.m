function GlobalShiftMask
% GLOBALSHIFTMASK - GUI tool to globally shift all ROIs in a maskObj file.
%
% Provides a BolusTrack-like interface to:
%   1. Load a bolus TIFF to see the MIP
%   2. Import an existing maskObj to overlay ROIs on the MIP
%   3. Pop-out View to visualize, enter shift values, and verify
%   4. Save the shifted maskObj as a new .mat file
%
% The output is ONLY the shifted maskObj — no bolus fitting is performed.
% Import the saved file into BolusTrack_InteractiveEdit via "Import ROIs"
% for fine-tuning and/or bolus fitting.
%
% SHIFT VALUES:
%   If using Visual Studio registration output:
%       X shift = dx_B - dx_A
%       Y shift = dy_B - dy_A
%   Positive X = shift ROIs to the right
%   Positive Y = shift ROIs downward

%% Set-up main figure and buttons

f = figure('numbertitle', 'off', ...
    'name', 'Global Shift Mask Tool', ...
    'color','w',...
    'menubar','none', ...
    'toolbar','none', ...
    'resize', 'on', ...
    'tag','globalshift', ...
    'renderer','painters', ...
    'position',[400 200 700 500]);

% Load Data button
uicontrol('Position', [10 450 100 25], ...
    'String', 'Load Data', ...
    'Callback', @LoadButtonPushed);

% Import ROIs button
uicontrol('Position', [10 410 100 25], ...
    'String', 'Import ROIs', ...
    'Callback', @ImportROIButtonPushed);

% Pop-out View (with shift dialog)
uicontrol('Position', [10 370 100 25], ...
    'String', 'Pop-out View', ...
    'Callback', @PopOutButtonPushed);

% Save Shifted Mask button
uicontrol('Position', [10 330 100 25], ...
    'String', 'Save Mask', ...
    'Callback', @SaveButtonPushed);

% Instructions text
uicontrol('Style', 'text', ...
    'Position', [10 250 120 70], ...
    'String', {'1. Load Data', '2. Import ROIs', '3. Pop-out View', '   (enter shift)', '4. Save Mask'}, ...
    'HorizontalAlignment', 'left', ...
    'BackgroundColor', 'w');

% Axes for MIP display
ax1 = axes(f);
ax1.Units = 'pixels';
ax1.Position = [150 50 500 420];
xlabel('pixel')
ylabel('pixel')
colormap gray

% Scale to normalized units
set(findall(f, '-property', 'Units'), 'Units', 'norm')

%% Local functions

    function LoadButtonPushed(~,~)
        % Load a bolus TIFF and display the MIP
        [fileInName, pathIn] = uigetfile({'*.tif;*.tiff','TIFF files'}, 'Select bolus TIFF');
        if isequal(fileInName, 0); return; end
        
        disp('Loading TIFF...');
        tiff_info = imfinfo([pathIn, fileInName]);
        fileIn = NaN(length(tiff_info), tiff_info(1).Height, tiff_info(1).Width);
        for jj = 1:length(tiff_info)
            fileIn(jj,:,:) = imread([pathIn, fileInName], jj);
        end
        
        % Apply median filter
        krnSize = [3 3];
        for tt = 1:size(fileIn,1)
            fileIn(tt,:,:) = medfilt2(squeeze(fileIn(tt,:,:)), krnSize);
        end
        
        % Display MIP
        MIP = squeeze(max(fileIn, [], 1));
        imagesc(MIP, 'Parent', ax1);
        colormap gray
        hold(ax1, 'on')
        
        % Store in base workspace
        assignin('base', 'GSM_fileIn', fileIn);
        assignin('base', 'GSM_MIP', MIP);
        disp(['Loaded: ' fileInName ' (' num2str(length(tiff_info)) ' frames)']);
    end

    function ImportROIButtonPushed(~,~)
        % Load a maskObj file and overlay ROIs on the MIP
        [fileNm, pathNm] = uigetfile('*.mat', 'Select maskObj file');
        if isequal(fileNm, 0); return; end
        
        temp = load([pathNm fileNm]);
        if ~isfield(temp, 'maskObj')
            errordlg('Selected file does not contain a maskObj variable.');
            return
        end
        
        maskObj = temp.maskObj;
        
        % Extract positions from whichever format
        nROI = length(maskObj);
        allPos = cell(1, nROI);
        
        for rr = 1:nROI
            if isstruct(maskObj) && isfield(maskObj, 'poli')
                allPos{rr} = maskObj(rr).poli.Position;
            elseif isstruct(maskObj) && isfield(maskObj, 'Position')
                allPos{rr} = maskObj(rr).Position;
            else
                allPos{rr} = maskObj(rr).Position;
            end
        end
        
        % Store positions and source info in base workspace
        assignin('base', 'GSM_allPos', allPos);
        assignin('base', 'GSM_nROI', nROI);
        assignin('base', 'GSM_maskPath', pathNm);
        assignin('base', 'GSM_maskFile', fileNm);
        
        % Overlay on MIP as thin red lines
        hold(ax1, 'on')
        for rr = 1:nROI
            pos = allPos{rr};
            plot([pos(:,1); pos(1,1)], [pos(:,2); pos(1,2)], 'r-', 'LineWidth', 0.5, 'Parent', ax1);
            cx = mean(pos(:,1));
            cy = mean(pos(:,2));
            text(cx, cy, num2str(rr), 'Color', 'y', 'FontSize', 7, 'FontWeight', 'bold', ...
                'HorizontalAlignment', 'center', 'Parent', ax1);
        end
        
        disp(['Loaded ' num2str(nROI) ' ROIs from ' fileNm]);
    end

    function PopOutButtonPushed(~,~)
        % Open a full-size pop-out showing the MIP with ROIs.
        % Ask for shift values, apply them, and display the result.
        % This lets you see the BEFORE (in the main window) and
        % AFTER (in the pop-out) side by side.
        
        % Check that data has been loaded
        try
            allPos = evalin('base', 'GSM_allPos');
            nROI = evalin('base', 'GSM_nROI');
        catch
            errordlg('Import ROIs first.');
            return
        end
        
        % Check if MIP is available
        hasImage = true;
        try
            MIP = evalin('base', 'GSM_MIP');
        catch
            hasImage = false;
        end
        
        % Ask for shift values
        shiftInput = inputdlg({'X shift (pixels, positive = right):', ...
                                'Y shift (pixels, positive = down):'}, ...
            'Global ROI Shift', 1, {'0', '0'});
        
        if isempty(shiftInput); return; end
        
        dx = str2double(shiftInput{1});
        dy = str2double(shiftInput{2});
        
        % Apply shift to a COPY of the positions
        shiftedPos = allPos;
        for rr = 1:nROI
            shiftedPos{rr}(:,1) = shiftedPos{rr}(:,1) + dx;
            shiftedPos{rr}(:,2) = shiftedPos{rr}(:,2) + dy;
        end
        
        % Store shifted positions in base workspace
        assignin('base', 'GSM_shiftedPos', shiftedPos);
        assignin('base', 'GSM_dx', dx);
        assignin('base', 'GSM_dy', dy);
        
        % Display in pop-out
        figure('Name', ['Shifted ROIs (X=' num2str(dx) ' Y=' num2str(dy) ')'], ...
            'NumberTitle', 'off', 'Color', 'w', 'ToolBar', 'figure');
        
        if hasImage
            imagesc(MIP);
            colormap gray
        end
        
        hold on
        axis image
        
        for rr = 1:nROI
            pos = shiftedPos{rr};
            plot([pos(:,1); pos(1,1)], [pos(:,2); pos(1,2)], 'r-', 'LineWidth', 0.5);
            cx = mean(pos(:,1));
            cy = mean(pos(:,2));
            text(cx, cy, num2str(rr), 'Color', 'y', 'FontSize', 7, 'FontWeight', 'bold', ...
                'HorizontalAlignment', 'center');
        end
        
        hold off
        title(['Shifted (X=' num2str(dx) ' Y=' num2str(dy) ')  |  Zoom/pan to verify, then Save Mask in main window'])
        
        disp(['Shift applied: X = ' num2str(dx) ', Y = ' num2str(dy)]);
        disp('If happy, click Save Mask in the main window.');
        disp('If not, click Pop-out View again with different values.');
    end

    function SaveButtonPushed(~,~)
        % Save the shifted positions as a new maskObj .mat file
        
        try
            shiftedPos = evalin('base', 'GSM_shiftedPos');
            dx = evalin('base', 'GSM_dx');
            dy = evalin('base', 'GSM_dy');
            origPath = evalin('base', 'GSM_maskPath');
            origFile = evalin('base', 'GSM_maskFile');
        catch
            errordlg('Use Pop-out View to apply a shift first.');
            return
        end
        
        nROI = length(shiftedPos);
        
        % Build a struct array with .Position field
        % This is the format BolusTrack Import ROIs expects
        clear maskObj
        for rr = 1:nROI
            maskObj(rr).Position = shiftedPos{rr}; %#ok<AGROW>
        end
        
        % Default filename
        [~, baseName, ~] = fileparts(origFile);
        defaultName = [baseName '_shifted_X' num2str(dx) '_Y' num2str(dy) '.mat'];
        
        [saveFile, savePath] = uiputfile('*.mat', 'Save shifted maskObj as', [origPath defaultName]);
        if ~isequal(saveFile, 0)
            save([savePath saveFile], 'maskObj');
            disp(['Saved: ' savePath saveFile]);
            disp('Import this file into BolusTrack via Import ROIs.');
        else
            disp('Save cancelled.');
        end
    end

end
