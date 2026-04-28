function fwhm = calcFWHM(fitStart, timeToPeak, fitEnd)
% CALCFWHM Estimate full-width at half-maximum from bolus fit boundary times.
%
% This approximation is consistent with the BolusTrack convention and uses
% the user-selected fit start (X1), time to bolus peak (pk), and fit end
% (X2) to estimate FWHM:
%
%   HalfWidthU = (timeToPeak - fitStart) / 2
%   HalfWidthD = fitEnd - timeToPeak
%   fwhm       = (timeToPeak + HalfWidthD) - (fitStart + HalfWidthU)
%              = fitEnd - fitStart - (timeToPeak - fitStart) / 2
%
% INPUTS:
%   fitStart   - time of fit window start (seconds)
%   timeToPeak - time to bolus peak (seconds)
%   fitEnd     - time of fit window end (seconds)
%
% OUTPUT:
%   fwhm - estimated full-width at half-maximum (seconds)

HalfWidthU = (timeToPeak - fitStart) / 2;
HalfWidthD = fitEnd - timeToPeak;
fwhm = (timeToPeak + HalfWidthD) - (fitStart + HalfWidthU);

end
