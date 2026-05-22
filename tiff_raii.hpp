#ifndef TIFF_RAII_HPP
#define TIFF_RAII_HPP

#include <tiffio.h>
#include <memory>

/**
 * @brief Custom deleter for TIFF file pointer resource management.
 */
struct TIFFDeleter {
    void operator()(TIFF* tif) const {
        if (tif) {
            TIFFClose(tif);
        }
    }
};

using UniqueTIFF = std::unique_ptr<TIFF, TIFFDeleter>;

/**
 * @brief Custom deleter for LibTIFF scanline buffer memory allocations.
 */
struct TIFFBufferDeleter {
    void operator()(tdata_t buf) const {
        if (buf) {
            _TIFFfree(buf);
        }
    }
};

using UniqueTIFFBuffer = std::unique_ptr<void, TIFFBufferDeleter>;

#endif // TIFF_RAII_HPP
