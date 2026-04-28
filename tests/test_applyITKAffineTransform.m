classdef test_applyITKAffineTransform < matlab.unittest.TestCase
% TEST_APPLYITK Unit tests for applyITKAffineTransform.
%
% ITK convention tested:
%   output = A * (input - center) + center + translation
%
% Run from the repository root:
%   results = runtests('tests/test_applyITKAffineTransform')

    methods (Test)

        % ------------------------------------------------------------------
        % Identity / zero cases
        % ------------------------------------------------------------------

        function test_identityTransform_noChange(testCase)
            % Identity A, zero translation, zero center — vertices unchanged
            pos = [1 2; 3 4; 5 6];
            A           = eye(2);
            center      = [0; 0];
            translation = [0; 0];
            result = applyITKAffineTransform(pos, A, center, translation);
            testCase.verifyEqual(result, pos, 'AbsTol', 1e-10);
        end

        function test_identityA_withCenter_noChange(testCase)
            % Identity A with non-zero center still leaves positions unchanged
            % because A*(pt-c)+c+0 = pt-c+c = pt
            pos = [100 200; 300 400];
            A           = eye(2);
            center      = [512; 512];
            translation = [0; 0];
            result = applyITKAffineTransform(pos, A, center, translation);
            testCase.verifyEqual(result, pos, 'AbsTol', 1e-10);
        end

        % ------------------------------------------------------------------
        % Pure translation
        % ------------------------------------------------------------------

        function test_pureTranslation_positiveShift(testCase)
            pos = [10 20; 30 40];
            A           = eye(2);
            center      = [0; 0];
            translation = [5; -3];
            result = applyITKAffineTransform(pos, A, center, translation);
            testCase.verifyEqual(result, [15 17; 35 37], 'AbsTol', 1e-10);
        end

        function test_pureTranslation_withCenter(testCase)
            % Identity A: A*(pt-c)+c+t = pt+t regardless of center
            pos = [10 20];
            A           = eye(2);
            center      = [50; 50];
            translation = [3; 7];
            result = applyITKAffineTransform(pos, A, center, translation);
            testCase.verifyEqual(result, [13 27], 'AbsTol', 1e-10);
        end

        function test_pureTranslation_negativeValues(testCase)
            pos = [-10 -20; -5 10];
            A           = eye(2);
            center      = [0; 0];
            translation = [5; 5];
            result = applyITKAffineTransform(pos, A, center, translation);
            testCase.verifyEqual(result, [-5 -15; 0 15], 'AbsTol', 1e-10);
        end

        % ------------------------------------------------------------------
        % Rotation
        % ------------------------------------------------------------------

        function test_rotation90deg_aboutOrigin(testCase)
            % 90° CCW rotation: [1,0] -> [0,1]
            % A = [cos90 -sin90; sin90 cos90] = [0 -1; 1 0]
            pos = [1 0];
            A           = [0 -1; 1 0];
            center      = [0; 0];
            translation = [0; 0];
            result = applyITKAffineTransform(pos, A, center, translation);
            testCase.verifyEqual(result, [0 1], 'AbsTol', 1e-10);
        end

        function test_rotation90deg_aboutCenter(testCase)
            % 90° CCW about (5,5): point (5,0) -> (10,5)
            % A*(pt-c)+c: [0 -1;1 0]*[0;-5]+[5;5] = [5;0]+[5;5] = [10;5]
            pos = [5 0];
            A           = [0 -1; 1 0];
            center      = [5; 5];
            translation = [0; 0];
            result = applyITKAffineTransform(pos, A, center, translation);
            testCase.verifyEqual(result, [10 5], 'AbsTol', 1e-10);
        end

        function test_rotation180deg_aboutOrigin(testCase)
            % 180° rotation: [3,4] -> [-3,-4]
            A           = [-1 0; 0 -1];
            center      = [0; 0];
            translation = [0; 0];
            pos = [3 4];
            result = applyITKAffineTransform(pos, A, center, translation);
            testCase.verifyEqual(result, [-3 -4], 'AbsTol', 1e-10);
        end

        function test_smallRotationPlusTranslation(testCase)
            % Realistic registration: tiny rotation + small translation
            pos = [100 200; 150 250; 120 180];
            theta = 0.001;   % ~0.057 degrees
            A           = [cos(theta) -sin(theta); sin(theta) cos(theta)];
            center      = [512; 512];
            translation = [3; -1.5];
            result = applyITKAffineTransform(pos, A, center, translation);
            % With tiny rotation, result ≈ pos + translation
            testCase.verifyEqual(result(:, 1), pos(:, 1) + 3,    'AbsTol', 0.5);
            testCase.verifyEqual(result(:, 2), pos(:, 2) - 1.5,  'AbsTol', 0.5);
        end

        % ------------------------------------------------------------------
        % Scaling
        % ------------------------------------------------------------------

        function test_uniformScale_aboutOrigin(testCase)
            % Scale by 2: [3,4] -> [6,8]
            pos = [3 4; 6 8];
            A           = 2 * eye(2);
            center      = [0; 0];
            translation = [0; 0];
            result = applyITKAffineTransform(pos, A, center, translation);
            testCase.verifyEqual(result, [6 8; 12 16], 'AbsTol', 1e-10);
        end

        function test_scaleAboutCenter_centerUnchanged(testCase)
            % The fixed center point is invariant under scaling
            pos = [5 5];
            A           = 3 * eye(2);
            center      = [5; 5];
            translation = [0; 0];
            result = applyITKAffineTransform(pos, A, center, translation);
            testCase.verifyEqual(result, [5 5], 'AbsTol', 1e-10);
        end

        function test_scaleAboutCenter_offCenterPoint(testCase)
            % Scale 2× about (5,5): point (7,5) -> (9,5)
            % A*(pt-c)+c = 2*[2;0]+[5;5] = [9;5]
            pos = [7 5];
            A           = 2 * eye(2);
            center      = [5; 5];
            translation = [0; 0];
            result = applyITKAffineTransform(pos, A, center, translation);
            testCase.verifyEqual(result, [9 5], 'AbsTol', 1e-10);
        end

        % ------------------------------------------------------------------
        % Output shape
        % ------------------------------------------------------------------

        function test_singleVertex_returnsOneRow(testCase)
            pos = [100 200];
            A           = eye(2);
            center      = [0; 0];
            translation = [10; 20];
            result = applyITKAffineTransform(pos, A, center, translation);
            testCase.verifySize(result, [1, 2]);
            testCase.verifyEqual(result, [110 220], 'AbsTol', 1e-10);
        end

        function test_manyVertices_outputSizeMatchesInput(testCase)
            pos = rand(12, 2) * 512;
            A           = eye(2);
            center      = [0; 0];
            translation = [0; 0];
            result = applyITKAffineTransform(pos, A, center, translation);
            testCase.verifySize(result, [12, 2]);
        end

        % ------------------------------------------------------------------
        % Forward / inverse consistency
        % ------------------------------------------------------------------

        function test_rotationInverse_recoversOriginal(testCase)
            % Apply rotation then its inverse; should recover original positions
            pos = [100 200; 300 400; 50 75];
            theta       = 0.3;
            A           = [cos(theta) -sin(theta); sin(theta) cos(theta)];
            Ainv        = A';           % rotation inverse = transpose
            center      = [200; 200];
            translation = [10; -5];

            fwd = applyITKAffineTransform(pos, A, center, translation);

            % Inverse: pt = A^-1 * (fwd - center - translation) + center
            nVerts    = size(fwd, 1);
            recovered = zeros(nVerts, 2);
            for vv = 1:nVerts
                pt = fwd(vv, :)';
                recovered(vv, :) = (Ainv * (pt - center - translation) + center)';
            end

            testCase.verifyEqual(recovered, pos, 'AbsTol', 1e-10);
        end

        function test_twoTranslationsAdditive(testCase)
            % Applying two successive translations equals a single combined translation
            pos = [10 20; 30 40];
            A           = eye(2);
            center      = [0; 0];
            t1 = [3; 4];
            t2 = [7; -2];

            step1 = applyITKAffineTransform(pos, A, center, t1);
            step2 = applyITKAffineTransform(step1, A, center, t2);
            combined = applyITKAffineTransform(pos, A, center, t1 + t2);

            testCase.verifyEqual(step2, combined, 'AbsTol', 1e-10);
        end

    end

end
