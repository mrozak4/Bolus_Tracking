function convert_masks_for_python(target_folder)
    if nargin < 1 || isempty(target_folder)
        % Find project root directory
        scriptFolder = fileparts(mfilename('fullpath'));
        projectFolder = fileparts(scriptFolder);
        if isempty(projectFolder), projectFolder = pwd; end
        target_folder = projectFolder;
    end

    fprintf('Searching for maskObj files in: %s...\n', target_folder);

    maskFiles1 = dir(fullfile(target_folder, '**/*MaskObj*.mat'));
    maskFiles2 = dir(fullfile(target_folder, '**/*maskObj*.mat'));
    maskFiles3 = dir(fullfile(target_folder, '**/*Mask*bj*.mat'));
    maskFiles4 = dir(fullfile(target_folder, '**/*mask*bj*.mat'));
    allFiles = [maskFiles1; maskFiles2; maskFiles3; maskFiles4];

    if isempty(allFiles)
        disp('No mask files found.');
        return;
    end

    % Remove duplicates
    [~, uniqueIdx] = unique(cellfun(@(x) fullfile(x.folder, x.name), num2cell(allFiles), 'UniformOutput', false));
    allFiles = allFiles(uniqueIdx);

    count = 0;
    for i = 1:length(allFiles)
        filePath = fullfile(allFiles(i).folder, allFiles(i).name);
        
        % Don't process our own output adjusted files or parity files
        if strncmp(allFiles(i).name, 'adjusted_', 9) || contains(filePath, 'parity')
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
                    
                    % Export TXT version for pure C++ pipeline
                    txtFileName = [fName '_rois.txt'];
                    txtFilePath = fullfile(fPath, txtFileName);
                    fid = fopen(txtFilePath, 'w');
                    if fid ~= -1
                        fprintf(fid, '%d\n', length(maskObj));
                        for j = 1:length(maskObj)
                            pos = maskObj(j).Position;
                            fprintf(fid, '%d %d\n', j-1, size(pos, 1));
                            for k = 1:size(pos, 1)
                                fprintf(fid, '%f %f\n', pos(k, 1), pos(k, 2));
                            end
                        end
                        fclose(fid);
                        fprintf('Exported TXT for C++: %s\n', txtFileName);
                    end
                    
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
end
