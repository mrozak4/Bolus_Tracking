function transformedPos = applyITKAffineTransform(pos, A, center, translation)
% APPLYITK Apply an ITK AffineTransform_float_2_2 to 2D ROI vertex coordinates.
%
% ITK convention:
%   output = A * (input - center) + center + translation
%
% This is used by ApplyRegistrationToMask to move ROI vertex coordinates
% from the original (unregistered) image space into the registered image
% space, matching the same transform applied to the TIFF pixels.
%
% INPUTS:
%   pos         - Nx2 matrix of [x, y] vertex coordinates
%   A           - 2x2 rotation/scale matrix [a00 a01; a10 a11]
%   center      - 2x1 fixed center point [cx; cy] (ITK "fixed" parameter)
%   translation - 2x1 translation vector [tx; ty]
%
% OUTPUT:
%   transformedPos - Nx2 matrix of transformed [x, y] coordinates

nVerts = size(pos, 1);
transformedPos = zeros(nVerts, 2);

for vv = 1:nVerts
    pt = pos(vv, :)';            % column vector [x; y]
    newPt = A * (pt - center) + center + translation;
    transformedPos(vv, :) = newPt';
end

end
