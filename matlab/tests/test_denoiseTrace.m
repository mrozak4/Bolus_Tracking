classdef test_denoiseTrace < matlab.unittest.TestCase
% TEST_DENOISETRACE Unit tests for denoiseTrace.
%
% The algorithm replaces any sample that deviates from its local window
% median by more than threshSD standard deviations with the local median.
%
% Run from the repository root:
%   results = runtests('tests/test_denoiseTrace')

    methods (Test)

        % ------------------------------------------------------------------
        % No-change cases
        % ------------------------------------------------------------------

        function test_highThreshold_nothingDenoised(testCase)
            % An enormous threshold should leave every sample untouched,
            % even if a large spike is present.
            rng(42);
            rawTrace = randn(1, 50);
            rawTrace(25) = 999;
            result = denoiseTrace(rawTrace, 5, 1e9);
            testCase.verifyEqual(result, rawTrace, 'AbsTol', 1e-10);
        end

        function test_allConstant_noChange(testCase)
            % A perfectly flat signal: local SD = 0, and no sample deviates
            % from the local median (which equals itself).
            rawTrace = ones(1, 50) * 7;
            result = denoiseTrace(rawTrace, 5, 2.0);
            testCase.verifyEqual(result, rawTrace, 'AbsTol', 1e-10);
        end

        function test_outputSameSize(testCase)
            rawTrace = rand(1, 75);
            result = denoiseTrace(rawTrace, 5, 2.0);
            testCase.verifySize(result, size(rawTrace));
        end

        % ------------------------------------------------------------------
        % Spike suppression
        % ------------------------------------------------------------------

        function test_singleSpike_suppressed(testCase)
            % A large isolated spike in a flat background is replaced
            rawTrace = ones(1, 30);
            rawTrace(15) = 1000;
            result = denoiseTrace(rawTrace, 5, 2.0);
            testCase.verifyLessThan(result(15), 100);
        end

        function test_singleSpike_replacedWithLocalMedian(testCase)
            % The replaced value equals the local median (all background = 5)
            rawTrace = ones(1, 30) * 5;
            rawTrace(15) = 1000;
            result = denoiseTrace(rawTrace, 5, 2.0);
            testCase.verifyEqual(result(15), 5, 'AbsTol', 1e-10);
        end

        function test_multipleSpikes_allSuppressed(testCase)
            rawTrace = ones(1, 60) * 10;
            rawTrace([10, 30, 50]) = 5000;
            result = denoiseTrace(rawTrace, 5, 2.0);
            testCase.verifyLessThan(result(10), 1000);
            testCase.verifyLessThan(result(30), 1000);
            testCase.verifyLessThan(result(50), 1000);
        end

        function test_negativeSpike_suppressed(testCase)
            % Large negative outlier should also be replaced
            rawTrace = ones(1, 30) * 50;
            rawTrace(15) = -9000;
            result = denoiseTrace(rawTrace, 5, 2.0);
            testCase.verifyGreaterThan(result(15), -1000);
        end

        % ------------------------------------------------------------------
        % Threshold sensitivity
        % ------------------------------------------------------------------

        function test_lowThreshold_aggressiveSuppression(testCase)
            % Very tight threshold: many samples in Gaussian noise get replaced
            rng(7);
            rawTrace = randn(1, 100);
            result = denoiseTrace(rawTrace, 5, 0.01);
            nChanged = sum(abs(result - rawTrace) > 1e-10);
            testCase.verifyGreaterThan(nChanged, 50);
        end

        function test_stricterThreshold_moreReplacement(testCase)
            % Lowering the threshold replaces at least as many points
            rng(13);
            rawTrace = randn(1, 100);
            r_permissive = denoiseTrace(rawTrace, 5, 3.0);
            r_strict     = denoiseTrace(rawTrace, 5, 1.0);
            nChanged_permissive = sum(abs(r_permissive - rawTrace) > 1e-10);
            nChanged_strict     = sum(abs(r_strict     - rawTrace) > 1e-10);
            testCase.verifyGreaterThanOrEqual(nChanged_strict, nChanged_permissive);
        end

        % ------------------------------------------------------------------
        % Boundary handling
        % ------------------------------------------------------------------

        function test_spikeAtStart_suppressed(testCase)
            % First element has a smaller left-side window but still works
            rawTrace = ones(1, 20);
            rawTrace(1) = 5000;
            result = denoiseTrace(rawTrace, 5, 2.0);
            testCase.verifyLessThan(result(1), 1000);
        end

        function test_spikeAtEnd_suppressed(testCase)
            rawTrace = ones(1, 20);
            rawTrace(end) = 5000;
            result = denoiseTrace(rawTrace, 5, 2.0);
            testCase.verifyLessThan(result(end), 1000);
        end

        function test_halfWinOf1_minimalWindow(testCase)
            % halfWin=1 means only 1 neighbor on each side (2-sample window)
            rawTrace = [1 1 1000 1 1 1 1 1];
            result = denoiseTrace(rawTrace, 1, 2.0);
            testCase.verifyLessThan(result(3), 500);
        end

        % ------------------------------------------------------------------
        % Realistic bolus trace
        % ------------------------------------------------------------------

        function test_typicalBolusTrace_spikeRemoved(testCase)
            % Simulate a Gaussian bolus with one noise spike
            t = linspace(0, 60, 300);
            rawTrace = 500 * exp(-0.5 * ((t - 20) / 5).^2) + 200;
            rawTrace(50) = rawTrace(50) + 5000;    % inject spike
            result = denoiseTrace(rawTrace, 5, 2.0);
            % Spike should be substantially reduced
            testCase.verifyLessThan(result(50), rawTrace(50) * 0.5);
            % Non-spike samples should be largely unchanged
            nonSpikeIdx = setdiff(1:length(rawTrace), 50);
            maxDiff = max(abs(result(nonSpikeIdx) - rawTrace(nonSpikeIdx)));
            testCase.verifyLessThan(maxDiff, 50);   % <50 AU change on bolus
        end

        function test_cleanBolusTrace_highThreshold_unchanged(testCase)
            % A clean bolus trace with no outliers should be unchanged at a
            % very large threshold (denoising effectively disabled)
            t = linspace(0, 60, 300);
            rawTrace = 500 * exp(-0.5 * ((t - 20) / 5).^2) + 200;
            result = denoiseTrace(rawTrace, 5, 1e9);
            testCase.verifyEqual(result, rawTrace, 'AbsTol', 1e-10);
        end

    end

end
