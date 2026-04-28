function cleanTrace = denoiseTrace(rawTrace, halfWin, threshSD)
% DENOISETRACE Apply sliding-window outlier replacement to a 1D signal.
%
% For each sample, computes the median and standard deviation of a local
% window that includes halfWin neighbors on each side (excluding the
% sample itself).  If the sample deviates from the local median by more
% than threshSD standard deviations it is replaced by the local median.
%
% INPUTS:
%   rawTrace - 1D numeric row or column vector (the signal to denoise)
%   halfWin  - half-width of the local window in samples (integer >= 1)
%   threshSD - outlier threshold in standard deviations (e.g. 2.0)
%
% OUTPUT:
%   cleanTrace - denoised signal, same size as rawTrace

cleanTrace = rawTrace;
nPts = length(rawTrace);

for pp = 1:nPts
    wStart = max(1, pp - halfWin);
    wEnd   = min(nPts, pp + halfWin);

    % Collect all neighbors inside the window, excluding the current point
    localWindow = rawTrace([wStart:pp-1, pp+1:wEnd]);

    localMedian = median(localWindow);
    localSD     = std(localWindow);

    if abs(rawTrace(pp) - localMedian) > threshSD * localSD
        cleanTrace(pp) = localMedian;
    end
end

end
