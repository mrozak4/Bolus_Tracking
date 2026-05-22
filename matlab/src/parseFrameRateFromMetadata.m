function calcFr = parseFrameRateFromMetadata(metaText)
% PARSEFRAMERATEFROMMETADATA Extract frame rate from Fluoview metadata text.
%
% Parses the "T Dimension" line from a Fluoview .txt metadata file and
% returns the frame rate in frames per second.  Returns NaN if the
% expected pattern is not found.
%
% The expected pattern is:
%   "T Dimension"   "N,   tStart - tEnd [s]"
%
% INPUT:
%   metaText - char array containing the full text of the metadata file
%
% OUTPUT:
%   calcFr - frame rate in fps, rounded to 2 decimal places; NaN if not found

tMatch = regexp(metaText, ...
    '"T Dimension"\s+"(\d+),\s+([\d.]+)\s*-\s*([\d.]+)\s*\[s\]', ...
    'tokens');

if ~isempty(tMatch)
    nFrames = str2double(tMatch{1}{1});
    tStart  = str2double(tMatch{1}{2});
    tEnd    = str2double(tMatch{1}{3});
    calcFr  = round(nFrames / (tEnd - tStart), 2);
else
    calcFr = NaN;
end

end
