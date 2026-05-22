function results = runTests()
% RUNTESTS Run all BolusTrack unit tests and report a summary.
%
% Run from the repository root directory:
%   addpath(genpath('.'));
%   cd tests;
%   results = runTests();
%
% Or, from anywhere on the MATLAB path:
%   results = runtests('tests')

suite  = testsuite('tests');
runner = matlab.unittest.TestRunner.withTextOutput();
results = runner.run(suite);

nPassed = sum([results.Passed]);
nFailed = sum([results.Failed]);
nTotal  = length(results);

fprintf('\n=== BolusTrack Test Summary ===\n');
fprintf('  Total:  %d\n', nTotal);
fprintf('  Passed: %d\n', nPassed);
fprintf('  Failed: %d\n', nFailed);

if nFailed > 0
    failedTests = results([results.Failed]);
    fprintf('\nFailed tests:\n');
    for ii = 1:length(failedTests)
        fprintf('  - %s\n', failedTests(ii).Name);
    end
end

end
