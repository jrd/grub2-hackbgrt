#pragma once

#include <grub/acpi.h>
#include <grub/types.h>

#pragma pack(push, 1)

/** BGRT structure */
struct grub_acpi_bgrt
{
    struct grub_acpi_table_header header;
    grub_uint16_t version;
    grub_uint8_t  status;
    grub_uint8_t  image_type;
    grub_uint64_t image_address;
    grub_uint32_t image_offset_x;
    grub_uint32_t image_offset_y;
} GRUB_PACKED;
typedef struct grub_acpi_bgrt* grub_acpi_bgrt_t;

#define BGRT_MAGIC          "BGRT"
#define BGRT_MAGIC_SIZE     (sizeof (BGRT_MAGIC) - 1)
#define BGRT_HEADER_SIZE    sizeof (struct grub_acpi_bgrt) // 56
#define BGRT_VERSION        1
#define BGRT_STATUS_INVALID 0 // not displayed
#define BGRT_STATUS_VALID   1 // displayed
#define BGRT_IMAGE_TYPE_BMP 0

/** Bitmap file header */
// https://en.wikipedia.org/wiki/BMP_file_format
// all int are in little endian format
struct bitmap_header {
    grub_uint16_t signature; // BM
    grub_uint32_t size; // total file size
    grub_uint32_t unused; // 0
    grub_uint32_t pixel_data_offset; // always 54 here = sizeof (bitmap_header)
    grub_uint32_t dib_header_size; // always 40 here
    grub_uint32_t width;
    grub_uint32_t height;
    grub_uint16_t planes; // always 1 for bgrt
    grub_uint16_t bpp; // always 24 for bgrt
    grub_uint32_t compression; // 0 = no compression
    grub_uint32_t data_size; // pixels raw size
    grub_uint32_t ppm_horiz; // pixel per metters horizontal (2835 = 75 DPI)
    grub_uint32_t ppm_vert; // pixel per metters vertical (2835 = 75 DPI)
    grub_uint32_t palette_colors; // number of colors in palette (0 = no palette)
    grub_uint32_t important_colors; // number of important colors (0 = all important)
} GRUB_PACKED;
typedef struct bitmap_header* bitmap_header_t;

#define BMP_MAGIC             "BM"
#define BMP_MAGIC_SIZE        (sizeof (BMP_MAGIC) - 1)
#define BMP_PIXEL_DATA_OFFSET sizeof (struct bitmap_header) // 54
#define BMP_DIB_HEADER_SIZE   40
#define BMP_888_BPP           24
#define BMP_NO_COMPRESSION    0
#define BMP_72_DPI            2835
#define BMP_NO_PALETTE        0

struct bitmap {
    struct bitmap_header header;
    grub_uint8_t* pixels;
} GRUB_PACKED;
typedef struct bitmap* bitmap_t;

inline grub_uint32_t
get_bitmap_pixels_size (grub_uint32_t width, grub_uint32_t height)
{
    // each row is padded to 4 bytes
    // each pixel is 3 bytes (24 bpp)
    return (BMP_888_BPP / 8 * width + width % 4) * height;
}

inline grub_uint32_t
get_bitmap_total_size (grub_uint32_t width, grub_uint32_t height)
{
    return BMP_PIXEL_DATA_OFFSET + get_bitmap_pixels_size (width, height);
}

/**
 * Verify the checksums of an ACPI RSDP version 2.
 *
 * @param data Pointer to the table.
 * @return 1 if the checksum is correct, 0 otherwise.
 */
extern int verify_acpi_rsdp2_checksums(void* data);

/**
 * Set the correct checksums of an ACPI RSDP version 2.
 *
 * @param data Pointer to the table.
 */
extern void set_acpi_rsdp2_checksums(void* data);

/**
 * Verify the checksum of an ACPI SDT.
 *
 * @param data Pointer to the table.
 * @return 1 if the checksum is correct, 0 otherwise.
 */
extern int verify_acpi_sdt_checksum(void* data);

/**
 * Set the correct checksum for an ACPI SDT.
 *
 * @param data Pointer to the table.
 */
extern void set_acpi_sdt_checksum(void* data);

#pragma pack(pop)
