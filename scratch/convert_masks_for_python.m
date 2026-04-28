% Convert all MATLAB mask files for Python compatibility
disp('Searching for maskObj files...');
maskFiles = dir('**/*MaskObj*.mat');
maskFiles2 = dir('**/*maskObj*.mat');
allFiles = [maskFiles; maskFiles2];

% Remove duplicates
[~, uniqueIdx] = unique(cellfun(@(x) fullfile(x.folder, x.name), num2cell(allFiles), 'UniformOutput', false));
allFiles = allFiles(uniqueIdx);

count = 0;
for i = 1:length(allFiles)
    filePath = fullfile(allFiles(i).folder, allFiles(i).name);
    
    % Don't process our parity or adjusted files
    if contains(filePath, 'adjusted') || contains(filePath, 'parity')
        continue;
    end
    
    try
        data = load(filePath);
        if isfield(data, 'maskObj')
            oldMasks = data.maskObj;
            clear newMaskObj
            nROI = length(oldMasks);
            valid = false;
            
            for j = 1:nROI
                % Handle various old formats (struct with poli, direct object, etc)
                if isstruct(oldMasks) && isfield(oldMasks, 'poli') && ~isempty(oldMasks(j).poli)
                    newMaskObj(j).Position = oldMasks(j).poli.Position;
                    valid = true;
                elseif isstruct(oldMasks) && isfield(oldMasks, 'Position')
                    newMaskObj(j).Position = oldMasks(j).Position;
                    valid = true;
                elseif isprop(oldMasks(j), 'Position') || isfield(oldMasks(j), 'Position')
                    newMaskObj(j).Position = oldMasks(j).Position;
                    valid = true;
                end
            end
            
            if valid
                maskObj = newMaskObj;
                [fPath, fName, fExt] = fileparts(filePath);
                newFileName = ['adjusted_' fName fExt];
                newFilePath = fullfile(fPath, newFileName);
                save(newFilePath, 'maskObj', '-v7');
                fprintf('Converted: %s -> %s\n', allFiles(i).name, newFileName);
                count = count + 1;
            else
                fprintf('Skipped: %s (Could not extract Position data)\n', allFiles(i).name);
            end
        end
    catch ME
        fprintf('Failed on %s: %s\n', allFiles(i).name, ME.message);
    end
end

fprintf('\nDone! Converted %d mask files for Python compatibility.\n', count);
