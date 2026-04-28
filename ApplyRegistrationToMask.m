function ApplyRegistrationToMask
% APPLYREGISTRATIONTOMASK - Apply Visual Studio registration transforms
% to existing maskObj ROI files.
%
% The Visual Studio registration produces affine transform files
% (bolus1_shift.mat, bolus2_shift.mat) that were applied to the bolus
% TIFFs to register them to the XYZ volumetric stack. This script applies
% the SAME transform to the ROI vertex coordinates in a maskObj file,
% so the ROIs move with the image they were drawn on.
%
% USE CASE:
%   You drew ROIs on an UNREGISTERED bolus file. You then registered
%   the bolus to the XYZ stack, producing a shifted TIFF. The ROIs
%   no longer match the shifted TIFF. This script transforms the ROI
%   positions to match the registered image.
%
% The transform format is ITK AffineTransform_float_2_2:
%   [a00, a01, a10, a11, tx, ty] with a fixed center point.
%   output = A * (input - center) + center + translation
%
% NOTE: Some transforms include a small rotation component in addition
% to translation. This script handles the full affine transform, not
% just XY shift.
%
% USAGE:
%   1. Type ApplyRegistrationToMask in the command window
%   2. Select the maskObj .mat file to transform
%   3. Select the shift .mat file from Visual Studio (e.g., bolus1_shift.mat)
%   4. Optionally load a registered TIFF to verify
%   5. Save the transformed maskObj

%% Step 1: Load the maskObj
[maskFile, maskPath] = uigetfile('*.mat', 'Select the maskObj .mat file to transform');
if isequal(maskFile, 0); disp('Cancelled.'); return; end

maskData = load([maskPath maskFile]);

% Find the maskObj variable — it might be stored under different names
% or as the only polygon-type variable in the file
if isfield(maskData, 'maskObj')
    maskObj = maskData.maskObj;
else
    % Look for the variable that contains polygon objects or position structs
    fnames = fieldnames(maskData);
    found = false;
    for ff = 1:length(fnames)
        candidate = maskData.(fnames{ff});
        if isobject(candidate) || (isstruct(candidate) && (isfield(candidate,'poli') || isfield(candidate,'Position')))
            maskObj = candidate;
            found = true;
            disp(['Found maskObj stored as: ' fnames{ff}]);
            break
        end
    end
    if ~found
        error('Could not find a maskObj variable in the selected file.');
    end
end

% Extract positions from whichever format
nROI = length(maskObj);
allPos = cell(1, nROI);

for rr = 1:nROI
    if isstruct(maskObj) && isfield(maskObj, 'poli')
        allPos{rr} = maskObj(rr).poli.Position;
    elseif isstruct(maskObj) && isfield(maskObj, 'Position')
        allPos{rr} = maskObj(rr).Position;
    elseif isobject(maskObj)
        allPos{rr} = maskObj(rr).Position;
    else
        error(['Cannot extract position from ROI ' num2str(rr)]);
    end
end

disp(['Loaded ' num2str(nROI) ' ROIs from ' maskFile]);

%% Step 2: Load the registration transform
[shiftFile, shiftPath] = uigetfile('*.mat', 'Select the Visual Studio shift .mat file');
if isequal(shiftFile, 0); disp('Cancelled.'); return; end

shiftData = load([shiftPath shiftFile]);

if ~isfield(shiftData, 'AffineTransform_float_2_2') || ~isfield(shiftData, 'fixed')
    error('Selected file does not contain ITK affine transform data.');
end

% Parse the ITK affine transform
% Format: [a00, a01, a10, a11, tx, ty]
t = double(shiftData.AffineTransform_float_2_2(:));
center = double(shiftData.fixed(:));

A = [t(1), t(2); t(3), t(4)];   % 2x2 rotation/scale matrix
translation = [t(5); t(6)];      % translation vector
rotAngle = atan2d(t(2), t(1));   % rotation in degrees

disp(['Transform from ' shiftFile ':']);
disp(['  Translation: [' num2str(translation(1), '%.2f') ', ' num2str(translation(2), '%.2f') '] pixels']);
disp(['  Rotation: ' num2str(rotAngle, '%.4f') ' degrees']);
disp(['  Center: [' num2str(center(1), '%.2f') ', ' num2str(center(2), '%.2f') ']']);

if abs(rotAngle) > 0.01
    disp('  Note: This transform includes rotation. A simple XY shift would not capture this correctly.');
end

%% Step 3: Apply the affine transform to all ROI positions
% ITK convention: output = A * (input - center) + center + translation
% But we need to be careful about coordinate conventions.
% In the TIFF image, the Visual Studio transform maps:
%   registered_position = A * (original_position - center) + center + translation
% So to move ROI coordinates from the original image space to the
% registered image space, we apply the same forward transform.

for rr = 1:nROI
    pos = allPos{rr}; % Nx2: [x, y] columns
    
    % Apply affine transform to each vertex
    nVerts = size(pos, 1);
    transformedPos = zeros(nVerts, 2);
    
    for vv = 1:nVerts
        pt = pos(vv, :)'; % column vector [x; y]
        % Forward transform: A * (pt - center) + center + translation
        newPt = A * (pt - center) + center + translation;
        transformedPos(vv, :) = newPt';
    end
    
    allPos{rr} = transformedPos;
end

disp('Transform applied to all ROI positions.');

%% Step 4: Optionally verify on a registered TIFF
answer = questdlg('Load a registered TIFF to verify ROI placement?', ...
    'Verify', 'Yes', 'No', 'Yes');

hasImage = false;
if strcmp(answer, 'Yes')
    [tifName, tifPath] = uigetfile({'*.tif;*.tiff', 'TIFF files'}, 'Select registered bolus TIFF or MIP for verification');
    if ~isequal(tifName, 0)
        tiff_info = imfinfo([tifPath tifName]);
        if length(tiff_info) == 1
            % Single image (MIP)
            MIP = imread([tifPath tifName]);
        else
            % Stack — make MIP
            disp('Loading TIFF stack (this may take a moment)...');
            fileIn = NaN(length(tiff_info), tiff_info(1).Height, tiff_info(1).Width);
            for jj = 1:length(tiff_info)
                fileIn(jj,:,:) = imread([tifPath tifName], jj);
            end
            MIP = squeeze(max(fileIn, [], 1));
        end
        hasImage = true;
        disp('Image loaded.');
    end
end

%% Step 5: Display verification
fFig = figure('Name', ['Transformed ROIs (' shiftFile ')'], ...
    'NumberTitle', 'off', 'Color', 'w', 'ToolBar', 'figure');

if hasImage
    imagesc(MIP);
    colormap gray
end

hold on
axis image

for rr = 1:nROI
    pos = allPos{rr};
    plot([pos(:,1); pos(1,1)], [pos(:,2); pos(1,2)], 'r-', 'LineWidth', 0.5);
    cx = mean(pos(:,1));
    cy = mean(pos(:,2));
    text(cx, cy, num2str(rr), 'Color', 'y', 'FontSize', 7, 'FontWeight', 'bold', ...
        'HorizontalAlignment', 'center');
end

hold off

if abs(rotAngle) > 0.01
    title(['Transformed (tx=' num2str(translation(1),'%.1f') ' ty=' num2str(translation(2),'%.1f') ' rot=' num2str(rotAngle,'%.2f') '\circ)']);
else
    title(['Transformed (tx=' num2str(translation(1),'%.1f') ' ty=' num2str(translation(2),'%.1f') ')']);
end

%% Step 6: Save
uicontrol(fFig, 'Style', 'pushbutton', 'String', 'Save Transformed Mask', ...
    'Position', [10 10 170 30], ...
    'Callback', @(~,~) saveTransformedMask(allPos, maskPath, maskFile, shiftFile));

end

function saveTransformedMask(allPos, origPath, origFile, shiftFile)
% Save as a clean struct array with .Position field

nROI = length(allPos);
clear maskObj
for rr = 1:nROI
    maskObj(rr).Position = allPos{rr}; %#ok<AGROW>
end

[~, shiftBase, ~] = fileparts(shiftFile);
[~, maskBase, ~] = fileparts(origFile);
defaultName = [maskBase '_registered_' shiftBase '.mat'];

[saveFile, savePath] = uiputfile('*.mat', 'Save transformed maskObj as', [origPath defaultName]);
if ~isequal(saveFile, 0)
    save([savePath saveFile], 'maskObj');
    disp(['Saved: ' savePath saveFile]);
    disp('Import this file into BolusTrack via Import ROIs.');
    disp('If additional fine-tuning is needed, use GlobalShiftMask or the Pop-out View.');
else
    disp('Save cancelled.');
end

end
