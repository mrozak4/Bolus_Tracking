classdef test_calcFWHM < matlab.unittest.TestCase
% TEST_CALCFWHM Unit tests for calcFWHM.
%
% The BolusTrack FWHM approximation:
%   HalfWidthU = (timeToPeak - fitStart) / 2
%   HalfWidthD = fitEnd - timeToPeak
%   fwhm = (timeToPeak + HalfWidthD) - (fitStart + HalfWidthU)
%         = fitEnd - fitStart - (timeToPeak - fitStart) / 2
%
% Run from the repository root:
%   results = runtests('tests/test_calcFWHM')

    methods (Test)

        % ------------------------------------------------------------------
        % Basic formula verification
        % ------------------------------------------------------------------

        function test_symmetricPeak_midpointOfWindow(testCase)
            % fitStart=0, peak=5, fitEnd=10
            % HalfWidthU=2.5, HalfWidthD=5 => fwhm=7.5
            testCase.verifyEqual(calcFWHM(0, 5, 10), 7.5, 'AbsTol', 1e-10);
        end

        function test_formulaEquivalence(testCase)
            % Verify result equals the simplified form:
            %   fwhm = fitEnd - fitStart - (timeToPeak - fitStart) / 2
            fitStart   = 3.5;
            timeToPeak = 8.2;
            fitEnd     = 25.7;
            result   = calcFWHM(fitStart, timeToPeak, fitEnd);
            expected = fitEnd - fitStart - (timeToPeak - fitStart) / 2;
            testCase.verifyEqual(result, expected, 'AbsTol', 1e-10);
        end

        % ------------------------------------------------------------------
        % Boundary / edge positions of the peak
        % ------------------------------------------------------------------

        function test_peakAtFitStart(testCase)
            % timeToPeak == fitStart => HalfWidthU=0, fwhm = fitEnd - fitStart
            result = calcFWHM(0, 0, 10);
            testCase.verifyEqual(result, 10, 'AbsTol', 1e-10);
        end

        function test_peakAtFitEnd(testCase)
            % timeToPeak == fitEnd => HalfWidthD=0, fwhm = fitEnd - (fitStart + HalfWidthU)
            %                      = fitEnd - fitStart - (fitEnd - fitStart)/2
            %                      = (fitEnd - fitStart) / 2
            result = calcFWHM(0, 10, 10);
            testCase.verifyEqual(result, 5, 'AbsTol', 1e-10);
        end

        % ------------------------------------------------------------------
        % Typical bolus parameters
        % ------------------------------------------------------------------

        function test_typicalBolus_startAt10_peak20_end50(testCase)
            % fwhm = 50 - 10 - (20-10)/2 = 40 - 5 = 35
            testCase.verifyEqual(calcFWHM(10, 20, 50), 35, 'AbsTol', 1e-10);
        end

        function test_typicalBolus_zeroBased(testCase)
            % fitStart=0, peak=15, fitEnd=40
            % fwhm = 40 - 0 - 15/2 = 40 - 7.5 = 32.5
            testCase.verifyEqual(calcFWHM(0, 15, 40), 32.5, 'AbsTol', 1e-10);
        end

        function test_nonZeroFitStart(testCase)
            % fitStart=2, peak=5, fitEnd=12
            % fwhm = 12 - 2 - (5-2)/2 = 10 - 1.5 = 8.5
            testCase.verifyEqual(calcFWHM(2, 5, 12), 8.5, 'AbsTol', 1e-10);
        end

        % ------------------------------------------------------------------
        % Scale / magnitude
        % ------------------------------------------------------------------

        function test_subSecondWindow(testCase)
            % fitStart=0, peak=0.5, fitEnd=1
            % fwhm = 1 - 0 - 0.5/2 = 0.75
            testCase.verifyEqual(calcFWHM(0, 0.5, 1), 0.75, 'AbsTol', 1e-10);
        end

        function test_largeValues_minutes(testCase)
            % fitStart=60, peak=90, fitEnd=180
            % fwhm = 180 - 60 - (90-60)/2 = 120 - 15 = 105
            testCase.verifyEqual(calcFWHM(60, 90, 180), 105, 'AbsTol', 1e-10);
        end

        % ------------------------------------------------------------------
        % Monotonicity: moving peak later reduces FWHM
        % ------------------------------------------------------------------

        function test_laterPeak_smallerFWHM(testCase)
            % With fixed fitStart and fitEnd, a later peak gives smaller fwhm
            % because HalfWidthU grows while HalfWidthD shrinks faster
            fwhm_early = calcFWHM(0, 3, 10);
            fwhm_late  = calcFWHM(0, 7, 10);
            testCase.verifyGreaterThan(fwhm_early, fwhm_late);
        end

        % ------------------------------------------------------------------
        % Output type
        % ------------------------------------------------------------------

        function test_outputIsScalarDouble(testCase)
            result = calcFWHM(0, 5, 10);
            testCase.verifyClass(result, 'double');
            testCase.verifySize(result, [1, 1]);
        end

    end

end
