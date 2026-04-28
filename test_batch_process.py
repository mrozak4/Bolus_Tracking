"""
Comprehensive tests for batch_process.py functions:
  - parse_metadata
  - get_mask_from_poly
  - find_triplets
"""

import os
import tempfile
import textwrap

import numpy as np
import pytest

from batch_process import parse_metadata, get_mask_from_poly, find_triplets


# ---------------------------------------------------------------------------
# parse_metadata
# ---------------------------------------------------------------------------

class TestParseMetadata:
    """Tests for the parse_metadata function."""

    def _write_meta(self, tmp_path, content):
        """Write content to a temporary .txt file and return its path."""
        path = tmp_path / "metadata.txt"
        path.write_text(content, encoding='utf-8')
        return str(path)

    def test_parses_standard_format(self, tmp_path):
        content = '"T Dimension"\t"300, 0.000 - 59.004 [s], Interval FreeRun"'
        path = self._write_meta(tmp_path, content)
        fr = parse_metadata(path)
        expected = round(300 / (59.004 - 0.0), 2)
        assert np.isclose(fr, expected, atol=0.01), f"frame rate {fr} != expected {expected}"

    def test_parses_integer_time_values(self, tmp_path):
        content = '"T Dimension"\t"600, 0.000 - 120.000 [s], Interval FreeRun"'
        path = self._write_meta(tmp_path, content)
        fr = parse_metadata(path)
        assert np.isclose(fr, 5.0, atol=0.01)

    def test_parses_non_zero_start_time(self, tmp_path):
        content = '"T Dimension"\t"100, 1.000 - 21.000 [s], Interval FreeRun"'
        path = self._write_meta(tmp_path, content)
        fr = parse_metadata(path)
        expected = round(100 / (21.0 - 1.0), 2)
        assert np.isclose(fr, expected, atol=0.01)

    def test_returns_float(self, tmp_path):
        content = '"T Dimension"\t"300, 0.000 - 60.000 [s], Interval FreeRun"'
        path = self._write_meta(tmp_path, content)
        fr = parse_metadata(path)
        assert isinstance(fr, float)

    def test_metadata_embedded_in_larger_file(self, tmp_path):
        content = textwrap.dedent("""\
            Some other metadata line
            "X Dimension"   "512, 0 - 511 [px]"
            "T Dimension"   "200, 0.000 - 39.800 [s], Interval FreeRun"
            More lines here
        """)
        path = self._write_meta(tmp_path, content)
        fr = parse_metadata(path)
        expected = round(200 / 39.8, 2)
        assert np.isclose(fr, expected, atol=0.01)

    def test_invalid_format_raises_value_error(self, tmp_path):
        content = 'No useful metadata here'
        path = self._write_meta(tmp_path, content)
        with pytest.raises(ValueError, match="Could not parse frame rate"):
            parse_metadata(path)

    def test_empty_file_raises_value_error(self, tmp_path):
        path = self._write_meta(tmp_path, '')
        with pytest.raises(ValueError):
            parse_metadata(path)

    def test_missing_t_dimension_raises_value_error(self, tmp_path):
        content = '"X Dimension"\t"512, 0 - 511 [px]"'
        path = self._write_meta(tmp_path, content)
        with pytest.raises(ValueError):
            parse_metadata(path)

    def test_frame_rate_rounded_to_two_decimals(self, tmp_path):
        # 300 frames over 59.004 s → irrational frame rate
        content = '"T Dimension"\t"300, 0.000 - 59.004 [s], Interval FreeRun"'
        path = self._write_meta(tmp_path, content)
        fr = parse_metadata(path)
        # Check that result has at most 2 decimal places
        assert fr == round(fr, 2)


# ---------------------------------------------------------------------------
# get_mask_from_poly
# ---------------------------------------------------------------------------

class TestGetMaskFromPoly:
    """Tests for the get_mask_from_poly function."""

    def test_output_shape_matches_image_shape(self):
        poly = np.array([[5, 5], [15, 5], [15, 15], [5, 15]], dtype=float)
        mask = get_mask_from_poly(poly, (30, 30))
        assert mask.shape == (30, 30)

    def test_output_dtype_is_bool(self):
        poly = np.array([[5, 5], [15, 5], [15, 15], [5, 15]], dtype=float)
        mask = get_mask_from_poly(poly, (30, 30))
        assert mask.dtype == bool

    def test_interior_pixels_are_true(self):
        # Square from (10,10) to (20,20)
        poly = np.array([[10, 10], [20, 10], [20, 20], [10, 20]], dtype=float)
        mask = get_mask_from_poly(poly, (30, 30))
        assert mask[15, 15], "Interior pixel should be True"

    def test_exterior_pixels_are_false(self):
        poly = np.array([[10, 10], [20, 10], [20, 20], [10, 20]], dtype=float)
        mask = get_mask_from_poly(poly, (30, 30))
        assert not mask[0, 0], "Exterior pixel should be False"
        assert not mask[29, 29], "Exterior pixel should be False"

    def test_non_zero_pixels_present(self):
        poly = np.array([[5, 5], [25, 5], [25, 25], [5, 25]], dtype=float)
        mask = get_mask_from_poly(poly, (40, 40))
        assert mask.sum() > 0, "Mask should contain True pixels"

    def test_triangle_polygon(self):
        # Right triangle with vertices at (0,0), (10,0), (5,10)
        poly = np.array([[0, 0], [10, 0], [5, 10]], dtype=float)
        mask = get_mask_from_poly(poly, (15, 15))
        assert mask.sum() > 0

    def test_small_polygon(self):
        """Minimum 3-vertex polygon should not crash."""
        poly = np.array([[1, 1], [3, 1], [2, 3]], dtype=float)
        mask = get_mask_from_poly(poly, (10, 10))
        assert mask.shape == (10, 10)

    def test_full_image_polygon(self):
        """Polygon covering entire image should fill most of the mask."""
        shape = (20, 20)
        poly = np.array([[0, 0], [19, 0], [19, 19], [0, 19]], dtype=float)
        mask = get_mask_from_poly(poly, shape)
        assert mask.sum() > 0.9 * shape[0] * shape[1]

    def test_integer_vertices_accepted(self):
        """Integer vertex arrays should work without errors."""
        poly = np.array([[5, 5], [15, 5], [15, 15], [5, 15]])
        mask = get_mask_from_poly(poly, (20, 20))
        assert mask.shape == (20, 20)

    def test_non_square_image(self):
        poly = np.array([[10, 5], [30, 5], [30, 20], [10, 20]], dtype=float)
        mask = get_mask_from_poly(poly, (40, 50))
        assert mask.shape == (40, 50)
        assert mask.sum() > 0


# ---------------------------------------------------------------------------
# find_triplets
# ---------------------------------------------------------------------------

class TestFindTriplets:
    """Tests for the find_triplets function."""

    def _make_triplet(self, folder, name):
        """Create a minimal TIFF, MAT, and TXT triplet for a given bolus name."""
        tif_path = os.path.join(folder, f"{name}.tif")
        mat_path = os.path.join(folder, f"adjusted_{name}.mat")
        txt_path = os.path.join(folder, f"{name}.txt")
        for p in (tif_path, mat_path, txt_path):
            with open(p, 'w') as f:
                f.write('placeholder')
        return tif_path, mat_path, txt_path

    def test_empty_folder_returns_empty_list(self):
        with tempfile.TemporaryDirectory() as folder:
            result = find_triplets(folder)
        assert result == []

    def test_single_complete_triplet_found(self):
        with tempfile.TemporaryDirectory() as folder:
            self._make_triplet(folder, 'bolus1_baseline')
            result = find_triplets(folder)
        assert len(result) == 1

    def test_triplet_contains_correct_paths(self):
        with tempfile.TemporaryDirectory() as folder:
            tif, mat, txt = self._make_triplet(folder, 'bolus2_co2')
            result = find_triplets(folder)
        assert len(result) == 1
        found_tif, found_mat, found_txt = result[0]
        assert found_tif == tif
        assert found_mat == mat
        assert found_txt == txt

    def test_multiple_triplets_found(self):
        with tempfile.TemporaryDirectory() as folder:
            self._make_triplet(folder, 'bolus1_baseline')
            self._make_triplet(folder, 'bolus2_co2')
            self._make_triplet(folder, 'bolus3_hypoxia')
            result = find_triplets(folder)
        assert len(result) == 3

    def test_incomplete_triplet_not_returned(self):
        """A TIFF without matching MAT or TXT should not appear in results."""
        with tempfile.TemporaryDirectory() as folder:
            # Only create TIFF, no MAT or TXT
            tif_path = os.path.join(folder, 'bolus1_baseline.tif')
            with open(tif_path, 'w') as f:
                f.write('placeholder')
            result = find_triplets(folder)
        assert result == []

    def test_mip_tiff_excluded(self):
        """TIFFs inside a 'mips' directory should be excluded."""
        with tempfile.TemporaryDirectory() as folder:
            mip_dir = os.path.join(folder, 'mips')
            os.makedirs(mip_dir)
            # Place TIFF in mips dir, but MAT and TXT in root
            tif_path = os.path.join(mip_dir, 'bolus1_baseline.tif')
            mat_path = os.path.join(folder, 'adjusted_bolus1_baseline.mat')
            txt_path = os.path.join(folder, 'bolus1_baseline.txt')
            for p in (tif_path, mat_path, txt_path):
                with open(p, 'w') as f:
                    f.write('placeholder')
            result = find_triplets(folder)
        assert result == []

    def test_results_tiff_excluded(self):
        """TIFFs whose path contains 'results' should be excluded."""
        with tempfile.TemporaryDirectory() as folder:
            results_dir = os.path.join(folder, 'results')
            os.makedirs(results_dir)
            tif_path = os.path.join(results_dir, 'bolus1_baseline.tif')
            mat_path = os.path.join(folder, 'adjusted_bolus1_baseline.mat')
            txt_path = os.path.join(folder, 'bolus1_baseline.txt')
            for p in (tif_path, mat_path, txt_path):
                with open(p, 'w') as f:
                    f.write('placeholder')
            result = find_triplets(folder)
        assert result == []

    def test_max_prefixed_tiff_excluded(self):
        """TIFFs with 'max_' prefix should be excluded."""
        with tempfile.TemporaryDirectory() as folder:
            tif_path = os.path.join(folder, 'max_bolus1_baseline.tif')
            mat_path = os.path.join(folder, 'adjusted_bolus1_baseline.mat')
            txt_path = os.path.join(folder, 'bolus1_baseline.txt')
            for p in (tif_path, mat_path, txt_path):
                with open(p, 'w') as f:
                    f.write('placeholder')
            result = find_triplets(folder)
        assert result == []

    def test_tiff_without_bolus_pattern_excluded(self):
        """TIFFs that don't match the bolus naming pattern should be excluded."""
        with tempfile.TemporaryDirectory() as folder:
            tif_path = os.path.join(folder, 'random_image.tif')
            mat_path = os.path.join(folder, 'adjusted_random_image.mat')
            txt_path = os.path.join(folder, 'random_image.txt')
            for p in (tif_path, mat_path, txt_path):
                with open(p, 'w') as f:
                    f.write('placeholder')
            result = find_triplets(folder)
        assert result == []

    def test_returns_list(self):
        with tempfile.TemporaryDirectory() as folder:
            result = find_triplets(folder)
        assert isinstance(result, list)

    def test_nested_subdirectory_triplets_found(self):
        """Triplets in subdirectories should be found via recursive glob."""
        with tempfile.TemporaryDirectory() as folder:
            sub = os.path.join(folder, 'session1')
            os.makedirs(sub)
            self._make_triplet(sub, 'bolus1_baseline')
            result = find_triplets(folder)
        assert len(result) == 1
