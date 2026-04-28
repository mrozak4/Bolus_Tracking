function shiftedPos = applyGlobalShift(allPos, dx, dy)
% APPLYGLOBALSHIFT Apply a uniform XY pixel shift to all ROI positions.
%
% Adds a constant offset to every vertex of every ROI.  Positive dx moves
% ROIs to the right; positive dy moves them downward (image convention).
%
% INPUTS:
%   allPos - 1xN cell array of Mk×2 [x, y] vertex coordinate matrices
%   dx     - X shift in pixels (positive = right)
%   dy     - Y shift in pixels (positive = down)
%
% OUTPUT:
%   shiftedPos - 1xN cell array of shifted Mk×2 coordinate matrices

shiftedPos = allPos;
for rr = 1:length(allPos)
    shiftedPos{rr}(:, 1) = shiftedPos{rr}(:, 1) + dx;
    shiftedPos{rr}(:, 2) = shiftedPos{rr}(:, 2) + dy;
end

end
