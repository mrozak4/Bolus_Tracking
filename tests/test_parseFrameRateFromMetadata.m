classdef test_parseFrameRateFromMetadata < matlab.unittest.TestCase
% TEST_PARSEFRAMERATEFROMMETADATA Unit tests for parseFrameRateFromMetadata.
%
% Run from the repository root:
%   results = runtests('tests/test_parseFrameRateFromMetadata')

    methods (Test)

        % ------------------------------------------------------------------
        % Valid inputs — correct frame rate returned
        % ------------------------------------------------------------------

        function test_5fps_100framesOver20s(testCase)
            % 100 frames / 20 s = 5.00 fps
            meta = '"T Dimension"	"100,	0.00 - 20.00 [s]"';
            testCase.verifyEqual(parseFrameRateFromMetadata(meta), 5.0, 'AbsTol', 0.01);
        end

        function test_1fps_60framesOver60s(testCase)
            meta = '"T Dimension"	"60,	0.00 - 60.00 [s]"';
            testCase.verifyEqual(parseFrameRateFromMetadata(meta), 1.0, 'AbsTol', 0.01);
        end

        function test_2fps_200framesOver100s(testCase)
            meta = '"T Dimension"	"200,	0.00 - 100.00 [s]"';
            testCase.verifyEqual(parseFrameRateFromMetadata(meta), 2.0, 'AbsTol', 0.01);
        end

        function test_5fps_embeddedInLargerText(testCase)
            % The pattern appears in the middle of a multi-line metadata file
            meta = [newline 'Some header info' newline ...
                '"T Dimension"' sprintf('\t') '"300,	0.00 - 60.00 [s]"' newline ...
                'Some other field	value' newline];
            testCase.verifyEqual(parseFrameRateFromMetadata(meta), 5.0, 'AbsTol', 0.01);
        end

        function test_roundingApplied(testCase)
            % 100 / 33 = 3.0303... -> rounded to 3.03
            meta = '"T Dimension"	"100,	0.00 - 33.00 [s]"';
            expected = round(100 / 33, 2);
            testCase.verifyEqual(parseFrameRateFromMetadata(meta), expected, 'AbsTol', 0.001);
        end

        function test_nonZeroStartTime_durationUsed(testCase)
            % Duration = tEnd - tStart = 30 - 10 = 20 s; 100/20 = 5 fps
            meta = '"T Dimension"	"100,	10.00 - 30.00 [s]"';
            expected = round(100 / (30 - 10), 2);
            testCase.verifyEqual(parseFrameRateFromMetadata(meta), expected, 'AbsTol', 0.001);
        end

        function test_highFrameRate_50fps(testCase)
            % 1500 frames over 30 s = 50 fps
            meta = '"T Dimension"	"1500,	0.00 - 30.00 [s]"';
            testCase.verifyEqual(parseFrameRateFromMetadata(meta), 50.0, 'AbsTol', 0.01);
        end

        % ------------------------------------------------------------------
        % Invalid / missing inputs — NaN returned
        % ------------------------------------------------------------------

        function test_emptyString_returnsNaN(testCase)
            testCase.verifyTrue(isnan(parseFrameRateFromMetadata('')));
        end

        function test_unrelatedText_returnsNaN(testCase)
            testCase.verifyTrue(isnan(parseFrameRateFromMetadata('Some unrelated text')));
        end

        function test_wrongUnit_ms_returnsNaN(testCase)
            % Unit is [ms] not [s] — should not match
            meta = '"T Dimension"	"100,	0.00 - 20.00 [ms]"';
            testCase.verifyTrue(isnan(parseFrameRateFromMetadata(meta)));
        end

        function test_missingQuotes_returnsNaN(testCase)
            % Pattern requires quoted fields
            meta = 'T Dimension 100 0.00 - 20.00 [s]';
            testCase.verifyTrue(isnan(parseFrameRateFromMetadata(meta)));
        end

        % ------------------------------------------------------------------
        % Output type
        % ------------------------------------------------------------------

        function test_outputIsScalarDouble(testCase)
            meta = '"T Dimension"	"50,	0.00 - 10.00 [s]"';
            result = parseFrameRateFromMetadata(meta);
            testCase.verifyClass(result, 'double');
            testCase.verifySize(result, [1, 1]);
        end

        function test_nanOutputIsScalarDouble(testCase)
            result = parseFrameRateFromMetadata('no match here');
            testCase.verifyClass(result, 'double');
            testCase.verifySize(result, [1, 1]);
            testCase.verifyTrue(isnan(result));
        end

    end

end
