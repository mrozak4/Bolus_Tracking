classdef test_applyGlobalShift < matlab.unittest.TestCase
% TEST_APPLYGLOBALSHIFT Unit tests for applyGlobalShift.
%
% Run from the repository root:
%   results = runtests('tests/test_applyGlobalShift')

    methods (Test)

        % ------------------------------------------------------------------
        % Basic shift directions
        % ------------------------------------------------------------------

        function test_positiveXandY(testCase)
            allPos = {[10 20; 30 40]};
            result = applyGlobalShift(allPos, 5, 3);
            testCase.verifyEqual(result{1}, [15 23; 35 43], 'AbsTol', 1e-10);
        end

        function test_negativeXandY(testCase)
            allPos = {[10 20; 30 40]};
            result = applyGlobalShift(allPos, -5, -3);
            testCase.verifyEqual(result{1}, [5 17; 25 37], 'AbsTol', 1e-10);
        end

        function test_xOnlyShift_yUnchanged(testCase)
            allPos = {[10 20; 30 40]};
            result = applyGlobalShift(allPos, 7, 0);
            testCase.verifyEqual(result{1}(:, 1), [17; 37], 'AbsTol', 1e-10);
            testCase.verifyEqual(result{1}(:, 2), [20; 40], 'AbsTol', 1e-10);
        end

        function test_yOnlyShift_xUnchanged(testCase)
            allPos = {[10 20; 30 40]};
            result = applyGlobalShift(allPos, 0, -8);
            testCase.verifyEqual(result{1}(:, 1), [10; 30], 'AbsTol', 1e-10);
            testCase.verifyEqual(result{1}(:, 2), [12; 32], 'AbsTol', 1e-10);
        end

        % ------------------------------------------------------------------
        % Zero shift
        % ------------------------------------------------------------------

        function test_zeroShift_noChange(testCase)
            allPos = {[10 20; 30 40], [5 15; 25 35]};
            result = applyGlobalShift(allPos, 0, 0);
            testCase.verifyEqual(result{1}, allPos{1}, 'AbsTol', 1e-10);
            testCase.verifyEqual(result{2}, allPos{2}, 'AbsTol', 1e-10);
        end

        % ------------------------------------------------------------------
        % Multiple ROIs
        % ------------------------------------------------------------------

        function test_multipleROIs_allShifted(testCase)
            allPos = {[1 2; 3 4], [10 20; 30 40], [5 5; 6 6]};
            result = applyGlobalShift(allPos, 2, -1);
            testCase.verifyEqual(result{1}, [3 1; 5 3],   'AbsTol', 1e-10);
            testCase.verifyEqual(result{2}, [12 19; 32 39], 'AbsTol', 1e-10);
            testCase.verifyEqual(result{3}, [7 4; 8 5],   'AbsTol', 1e-10);
        end

        function test_singleROI_singleVertex(testCase)
            allPos = {[100 200]};
            result = applyGlobalShift(allPos, -10, 15);
            testCase.verifyEqual(result{1}, [90 215], 'AbsTol', 1e-10);
        end

        % ------------------------------------------------------------------
        % Fractional and large shifts
        % ------------------------------------------------------------------

        function test_fractionalShift(testCase)
            allPos = {[10 20]};
            result = applyGlobalShift(allPos, 0.5, -0.75);
            testCase.verifyEqual(result{1}, [10.5 19.25], 'AbsTol', 1e-10);
        end

        function test_largeShift(testCase)
            allPos = {[50 50; 100 100]};
            result = applyGlobalShift(allPos, 1000, -500);
            testCase.verifyEqual(result{1}, [1050 -450; 1100 -400], 'AbsTol', 1e-10);
        end

        % ------------------------------------------------------------------
        % Input immutability
        % ------------------------------------------------------------------

        function test_doesNotModifyInputCell(testCase)
            pos1   = [10 20; 30 40];
            allPos = {pos1};
            applyGlobalShift(allPos, 5, 5);
            testCase.verifyEqual(allPos{1}, pos1, 'AbsTol', 1e-10);
        end

        % ------------------------------------------------------------------
        % Output structure
        % ------------------------------------------------------------------

        function test_outputCellSameLength(testCase)
            allPos = {rand(4,2), rand(6,2), rand(3,2)};
            result = applyGlobalShift(allPos, 1, 1);
            testCase.verifyNumElements(result, 3);
        end

        function test_outputVertexCountPreserved(testCase)
            allPos = {rand(4, 2), rand(7, 2)};
            result = applyGlobalShift(allPos, 3, -2);
            testCase.verifySize(result{1}, [4, 2]);
            testCase.verifySize(result{2}, [7, 2]);
        end

        % ------------------------------------------------------------------
        % Symmetry / inverse
        % ------------------------------------------------------------------

        function test_shiftThenUnshift_recoversOriginal(testCase)
            allPos = {[10 20; 30 40; 15 25]};
            shifted   = applyGlobalShift(allPos, 7, -3);
            recovered = applyGlobalShift(shifted, -7, 3);
            testCase.verifyEqual(recovered{1}, allPos{1}, 'AbsTol', 1e-10);
        end

        function test_twoShiftsAdditive(testCase)
            % Two successive shifts equal a single combined shift
            allPos = {[10 20; 30 40]};
            step1    = applyGlobalShift(allPos,  3,  5);
            step2    = applyGlobalShift(step1,  -1,  2);
            combined = applyGlobalShift(allPos,  2,  7);
            testCase.verifyEqual(step2{1}, combined{1}, 'AbsTol', 1e-10);
        end

    end

end
