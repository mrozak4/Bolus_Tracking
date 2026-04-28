classdef test_findMaskObjInData < matlab.unittest.TestCase
% TEST_FINDMASKOBJINDATA Unit tests for findMaskObjInData.
%
% Run from the repository root:
%   results = runtests('tests/test_findMaskObjInData')

    methods (Test)

        % ------------------------------------------------------------------
        % Direct 'maskObj' field — highest priority
        % ------------------------------------------------------------------

        function test_directMaskObjField_returned(testCase)
            maskData.maskObj = struct('Position', {[1 2; 3 4]});
            result = findMaskObjInData(maskData);
            testCase.verifyEqual(result.Position, [1 2; 3 4]);
        end

        function test_maskObjField_takesPrecedenceOverOtherFields(testCase)
            % 'maskObj' should win even if another matching field exists
            maskData.maskObj  = struct('Position', {[1 2; 3 4]});
            maskData.otherROI = struct('Position', {[99 99; 88 88]});
            result = findMaskObjInData(maskData);
            testCase.verifyEqual(result.Position, [1 2; 3 4]);
        end

        % ------------------------------------------------------------------
        % Fallback: struct with 'Position' field
        % ------------------------------------------------------------------

        function test_structWithPositionField_found(testCase)
            roi = struct('Position', {[5 6; 7 8; 9 10]});
            maskData.myROIs = roi;
            result = findMaskObjInData(maskData);
            testCase.verifyEqual(result.Position, [5 6; 7 8; 9 10]);
        end

        function test_structArrayWithPositionField_found(testCase)
            % Struct array (multiple ROIs) — isfield works on struct arrays
            for rr = 1:3
                roi_array(rr).Position = rr * ones(4, 2); %#ok<AGROW>
            end
            maskData.myMask = roi_array;
            result = findMaskObjInData(maskData);
            testCase.verifySize(result, [1, 3]);
        end

        % ------------------------------------------------------------------
        % Fallback: struct with 'poli' field (legacy drawROI format)
        % ------------------------------------------------------------------

        function test_structWithPoliField_found(testCase)
            roi.poli.Position = [1 2; 3 4];
            maskData.savedMask = roi;
            result = findMaskObjInData(maskData);
            testCase.verifyTrue(isfield(result, 'poli'));
        end

        % ------------------------------------------------------------------
        % Error cases
        % ------------------------------------------------------------------

        function test_noValidVariable_throwsError(testCase)
            maskData.someValue    = 42;
            maskData.anotherValue = 'hello';
            testCase.verifyError(@() findMaskObjInData(maskData), ...
                'BolusTrack:maskObjNotFound');
        end

        function test_emptyStruct_throwsError(testCase)
            maskData = struct();
            testCase.verifyError(@() findMaskObjInData(maskData), ...
                'BolusTrack:maskObjNotFound');
        end

        function test_numericFieldOnly_throwsError(testCase)
            maskData.x = [1 2 3];
            maskData.y = [4 5 6];
            testCase.verifyError(@() findMaskObjInData(maskData), ...
                'BolusTrack:maskObjNotFound');
        end

        function test_stringFieldOnly_throwsError(testCase)
            maskData.label   = 'test';
            maskData.subject = 'AB01';
            testCase.verifyError(@() findMaskObjInData(maskData), ...
                'BolusTrack:maskObjNotFound');
        end

        % ------------------------------------------------------------------
        % Mixed fields — correct variable found
        % ------------------------------------------------------------------

        function test_mixedFields_picksStructWithPosition(testCase)
            % Non-matching fields precede the matching one — still found
            maskData.rawData  = rand(10, 10);
            maskData.metadata = 'some text';
            maskData.roiData  = struct('Position', {[1 2; 3 4]});
            result = findMaskObjInData(maskData);
            testCase.verifyEqual(result.Position, [1 2; 3 4]);
        end

        function test_maskObjFieldWithPoliFormat(testCase)
            % 'maskObj' field may itself use the legacy poli format
            roi.poli.Position = [10 20; 30 40];
            maskData.maskObj = roi;
            result = findMaskObjInData(maskData);
            testCase.verifyTrue(isfield(result, 'poli'));
        end

    end

end
