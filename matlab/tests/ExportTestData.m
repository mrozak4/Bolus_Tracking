% ExportTestData.m
% Run this script in MATLAB *after* running BolusTrack and generating the time-courses.
% It exports the raw time-courses, the auto-estimated parameters, and any fitted curves
% for parity testing against the Python port.

try
    fitOut = evalin('base', 'fitOut');
    Fr = evalin('base', 'Fr');
catch
    error('fitOut or Fr not found. Please load a bolus in BolusTrack and click Show ROIs tc first.');
end

numToExport = min(10, length(fitOut));
exportData = struct();
exportData.Fr = str2double(Fr);

for i = 1:numToExport
    traceName = sprintf('ROI_%d', i);
    
    exportData.(traceName).yRaw = fitOut(i).yRaw;
    exportData.(traceName).tlRaw = fitOut(i).tlRaw;
    
    % Initial estimated params
    if isfield(fitOut(i), 'InitAmp')
        exportData.(traceName).InitAmp = fitOut(i).InitAmp;
        exportData.(traceName).InitT2p = fitOut(i).InitT2p;
        exportData.(traceName).InitFWHM = fitOut(i).InitFWHM;
        exportData.(traceName).InitM = fitOut(i).InitM;
    end
    
    % Fitted params
    if isfield(fitOut(i), 'beta') && ~isempty(fitOut(i).beta) && length(fitOut(i).beta) >= 4
        exportData.(traceName).beta = fitOut(i).beta;
        exportData.(traceName).AUC = fitOut(i).AUC;
        if isfield(fitOut(i), 'fitTr')
            exportData.(traceName).fitTr = fitOut(i).fitTr;
        end
    end
end

jsonStr = jsonencode(exportData);
fid = fopen('parity_test_data.json', 'w');
fwrite(fid, jsonStr, 'char');
fclose(fid);

disp('Exported parity_test_data.json successfully to current MATLAB directory.');
