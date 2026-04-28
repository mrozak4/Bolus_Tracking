function maskObj = findMaskObjInData(maskData)
% FINDMASKOBJINDATA Find the maskObj variable in data loaded from a .mat file.
%
% Checks for a field named 'maskObj' first.  If not found, searches for
% any variable that is a MATLAB object or a struct with a 'poli' or
% 'Position' field — both are valid maskObj formats used across BolusTrack,
% drawROI, GlobalShiftMask, and ApplyRegistrationToMask.
%
% INPUT:
%   maskData - struct returned by load() on a maskObj .mat file
%
% OUTPUT:
%   maskObj - the found mask variable (struct array or object array)
%
% ERROR:
%   Throws 'BolusTrack:maskObjNotFound' if no maskObj can be identified.

if isfield(maskData, 'maskObj')
    maskObj = maskData.maskObj;
    return
end

fnames = fieldnames(maskData);
for ff = 1:length(fnames)
    candidate = maskData.(fnames{ff});
    if isobject(candidate) || ...
       (isstruct(candidate) && ...
        (isfield(candidate, 'poli') || isfield(candidate, 'Position')))
        maskObj = candidate;
        disp(['Found maskObj stored as: ' fnames{ff}]);
        return
    end
end

error('BolusTrack:maskObjNotFound', ...
    'Could not find a maskObj variable in the selected file.');

end
