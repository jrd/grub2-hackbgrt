/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2003,2007  Free Software Foundation, Inc.
 *  Copyright (C) 2003  NIIBE Yutaka <gniibe@m17n.org>
 *
 *  GRUB is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  GRUB is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with GRUB.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <grub/acpi.h>
#include <grub/charset.h>
#include <grub/dl.h>
#include <grub/efi/api.h>
#include <grub/efi/disk.h>
#include <grub/efi/efi.h>
#include <grub/efi/graphics_output.h>
#include <grub/err.h>
#include <grub/extcmd.h>
#include <grub/file.h>
#include <grub/i18n.h>
#include <grub/misc.h>
#include <grub/mm.h>
#include <grub/types.h>
#include <grub/video.h>
#include "config.h"
#include "types.h"

GRUB_MOD_LICENSE ("GPLv3+");


static grub_err_t
grub_video_gop_fill_mode_info (unsigned mode,
			       struct grub_efi_gop_mode_info* in,
			       struct grub_video_mode_info* out)
{
  out->mode_number = mode;
  out->number_of_colors = 256;
  out->width = in->width;
  out->height = in->height;
  out->mode_type = GRUB_VIDEO_MODE_TYPE_RGB;
  out->bytes_per_pixel = sizeof (struct grub_efi_gop_blt_pixel);
  out->bpp = out->bytes_per_pixel << 3;
  out->pitch = in->width * out->bytes_per_pixel;
  out->red_mask_size = 8;
  out->red_field_pos = 16;
  out->green_mask_size = 8;
  out->green_field_pos = 8;
  out->blue_mask_size = 8;
  out->blue_field_pos = 0;
  out->reserved_mask_size = 8;
  out->reserved_field_pos = 24;
  out->blit_format = GRUB_VIDEO_BLIT_FORMAT_BGRA_8888;
  out->mode_type |= (GRUB_VIDEO_MODE_TYPE_DOUBLE_BUFFERED | GRUB_VIDEO_MODE_TYPE_UPDATING_SWAP);
  return GRUB_ERR_NONE;
}

static int
grub_video_gop_iterate (struct grub_efi_gop* gop,
                        int (*hook) (const struct grub_video_mode_info* info, void* hook_arg),
                        void* hook_arg)
{
  for (unsigned mode = 0; mode < gop->mode->max_mode; mode++)
  {
    grub_efi_uintn_t size;
    grub_efi_status_t status;
    struct grub_efi_gop_mode_info* info = NULL;
    grub_err_t err;
    struct grub_video_mode_info mode_info;
    status = efi_call_4 (gop->query_mode, gop, mode, &size, &info);
    if (status)
    {
      info = 0;
      continue;
    }
    err = grub_video_gop_fill_mode_info (mode, info, &mode_info);
    if (err)
    {
      grub_errno = GRUB_ERR_NONE;
      continue;
    }
    if (hook (&mode_info, hook_arg))
      return 1;
  }
  return 0;
}

static int
check_protocol_hook (const struct grub_video_mode_info* info __attribute__ ((unused)),
                     void* hook_arg)
{
  int* have_usable_mode = hook_arg;
  *have_usable_mode = 1;
  return 1;
}

/**
 * Get the GOP (Graphics Output Protocol) pointer.
 */
static struct grub_efi_gop*
get_gop(void)
{
  static grub_efi_guid_t graphics_output_guid = GRUB_EFI_GOP_GUID;
  static grub_efi_handle_t gop_handle;
  static struct grub_efi_gop* gop;

  grub_efi_handle_t* handles;
  grub_efi_uintn_t num_handles, i;
  int have_usable_mode = 0;
  handles = grub_efi_locate_handle (GRUB_EFI_BY_PROTOCOL, &graphics_output_guid, NULL, &num_handles);
  if (!handles || num_handles == 0)
    return gop;
  for (i = 0; i < num_handles; i++)
  {
    gop_handle = handles[i];
    gop = grub_efi_open_protocol (gop_handle, &graphics_output_guid, GRUB_EFI_OPEN_PROTOCOL_GET_PROTOCOL);
    grub_video_gop_iterate (gop, check_protocol_hook, &have_usable_mode);
    if (have_usable_mode)
    {
      grub_free (handles);
      return gop;
    }
  }
  gop = 0;
  gop_handle = 0;
  return gop;
}

/**
 * Create a new XSDT with the given number of entries.
 *
 * @param xsdt0 The old XSDT.
 * @param entries The number of SDT entries.
 * @return Pointer to a new XSDT.
 */
static struct grub_acpi_table_header*
create_xsdt(struct grub_acpi_table_header* xsdt0, grub_efi_uintn_t entries)
{
  struct grub_acpi_table_header* xsdt = 0;

  grub_efi_uint32_t xsdt_len = sizeof (struct grub_acpi_table_header) + entries * sizeof (grub_efi_uint64_t);
  grub_efi_status_t status = efi_call_3 (grub_efi_system_table->boot_services->allocate_pool, GRUB_EFI_ACPI_RECLAIM_MEMORY, xsdt_len, (void**) &xsdt);
  if (status)
  {
    grub_printf("HackBGRT: Failed to allocate memory for XSDT.\n");
    return 0;
  }
  grub_memset(xsdt, 0, xsdt_len);
  grub_memcpy(xsdt, xsdt0, grub_min(xsdt0->length, xsdt_len));
  xsdt->length = xsdt_len;
  xsdt->checksum = 0;
  return xsdt;
}

#define OEMID_TO_CHAR_LIST(oemid) oemid[0], oemid[1], oemid[2], oemid[3], oemid[4], oemid[5]
#define ACPI_TABLE_TO_CHAR_LIST(sign) sign[0], sign[1], sign[2], sign[3]

/**
 * Update the ACPI tables as needed for the desired BGRT change.
 *
 * If action is REMOVE, all BGRT entries will be removed.
 * If action is KEEP, the first BGRT entry will be returned.
 * If action is REPLACE, the given BGRT entry will be stored in each XSDT.
 *
 * @param action The intended action.
 * @param bgrt The BGRT, if action is REPLACE.
 * @return Pointer to the BGRT, or 0 if not found (or destroyed).
 */
static grub_acpi_bgrt_t
handle_acpi_tables(enum hackbgrt_action action, grub_acpi_bgrt_t bgrt)
{
  static grub_efi_packed_guid_t acpi20_guid = GRUB_EFI_ACPI_20_TABLE_GUID;
  struct grub_acpi_rsdp_v20* rsdp;
  struct grub_acpi_table_header* xsdt;

  for (unsigned i = 0; i < grub_efi_system_table->num_table_entries; i++)
  {
    grub_efi_packed_guid_t *guid = &grub_efi_system_table->configuration_table[i].vendor_guid;
    if (grub_memcmp (guid, &acpi20_guid, sizeof (grub_efi_packed_guid_t)) != 0)
      continue;
    // read RSDP version 2.0 https://wiki.osdev.org/RSDP
    rsdp = (struct grub_acpi_rsdp_v20*) grub_efi_system_table->configuration_table[i].vendor_table;
    if (grub_memcmp (rsdp->rsdpv1.signature, GRUB_RSDP_SIGNATURE, GRUB_RSDP_SIGNATURE_SIZE) != 0 || rsdp->rsdpv1.revision < 2 || !verify_acpi_rsdp2_checksums (rsdp))
      continue;
    grub_dprintf ("hackbgrt", "RSDP: revision = %d, OEM ID = %c%c%c%c%c%c\n", rsdp->rsdpv1.revision, OEMID_TO_CHAR_LIST (rsdp->rsdpv1.oemid));
    // Read XSDT https://wiki.osdev.org/XSDT
    xsdt = (struct grub_acpi_table_header*) (grub_efi_uintn_t) rsdp->xsdt_addr;
    if (!xsdt)
    {
      grub_dprintf ("hackbgrt", "* XSDT: missing\n");
      continue;
    }
    if (grub_memcmp(xsdt->signature, "XSDT", 4) != 0)
    {
      grub_dprintf ("hackbgrt", "* XSDT: bad signature\n");
      continue;
    }
    if (action == HACKBGRT_KEEP && !verify_acpi_sdt_checksum (xsdt))
    {
      grub_dprintf ("hackbgrt", "* XSDT: bad checksum\n");
      continue;
    }
    grub_efi_uint64_t* entry_arr = (grub_efi_uint64_t*) &xsdt[1];
    grub_efi_uint32_t entry_arr_length = (xsdt->length - sizeof (*xsdt)) / sizeof (grub_efi_uint64_t);
    grub_dprintf ("hackbgrt", "* XSDT: OEM ID = %c%c%c%c%c%c, entry count = %d\n", OEMID_TO_CHAR_LIST (xsdt->oemid), entry_arr_length);
    unsigned bgrt_count = 0;
    for (unsigned j = 0; j < entry_arr_length; j++)
    {
      struct grub_acpi_table_header* entry = (struct grub_acpi_table_header*) ((grub_efi_uintn_t) entry_arr[j]);
      if (grub_memcmp(entry->signature, "BGRT", 4) != 0)
        continue;
      grub_dprintf ("hackbgrt", " - ACPI table: %c%c%c%c, revision = %d, OEM ID = %c%c%c%c%c%c\n", ACPI_TABLE_TO_CHAR_LIST (entry->signature), entry->revision, OEMID_TO_CHAR_LIST (entry->oemid));
      switch (action)
      {
        case HACKBGRT_KEEP:
          if (!bgrt)
          {
            grub_dprintf ("hackbgrt", " -> Returning first BGRT.\n");
            bgrt = (grub_acpi_bgrt_t) entry;
          }
          break;
        case HACKBGRT_REMOVE:
          grub_dprintf ("hackbgrt", " -> Deleting BGRT (entry %d).\n", j);
          for (unsigned k = j + 1; k < entry_arr_length; k++)
            entry_arr[k - 1] = entry_arr[k];
          entry_arr_length--;
          entry_arr[entry_arr_length] = 0;
          xsdt->length -= sizeof (entry_arr[0]);
          j--;
          break;
        case HACKBGRT_REPLACE:
          grub_dprintf ("hackbgrt", " -> Replacing BGRT (entry %d).\n", j);
          entry_arr[j] = (grub_efi_uintn_t) bgrt;
      }
      bgrt_count += 1;
    }
    if (!bgrt_count && action == HACKBGRT_REPLACE && bgrt)
    {
      grub_dprintf ("hackbgrt", " - Adding missing BGRT.\n");
      xsdt = create_xsdt(xsdt, entry_arr_length + 1);
      entry_arr = (grub_efi_uint64_t*) &xsdt[1];
      entry_arr[entry_arr_length++] = (grub_efi_uintn_t) bgrt;
      rsdp->xsdt_addr = (grub_efi_uintn_t) xsdt;
      set_acpi_rsdp2_checksums(rsdp);
    }
    set_acpi_sdt_checksum(xsdt);
  }
  return bgrt;
}

/**
 * Load a bitmap or generate a black one.
 *
 * @param path The bitmap path; NULL for a black bitmap.
 * @return The loaded bitmap, or 0 if not available.
 */
static bitmap_t load_bmp(const char* path)
{
  bitmap_t bmp = 0;
  grub_efi_status_t status;
  grub_file_t file;

  if (!path)
  {
    grub_uint32_t bitmap_size = get_bitmap_total_size (1, 1);
    status = efi_call_3 (grub_efi_system_table->boot_services->allocate_pool, GRUB_EFI_BOOT_SERVICES_DATA, bitmap_size, (void**) &bmp);
    if (status)
    {
      grub_error(GRUB_ERR_READ_ERROR, "HackBGRT: Failed to allocate a blank BMP!\n");
      grub_efi_system_table->boot_services->stall(1000000); // 1 sec pause
    }
    else
    {
      grub_memcpy(&bmp->header.signature, BMP_MAGIC, BMP_MAGIC_SIZE);
      bmp->header.size = bitmap_size;
      bmp->header.unused = 0;
      bmp->header.pixel_data_offset = BMP_PIXEL_DATA_OFFSET;
      bmp->header.dib_header_size = BMP_DIB_HEADER_SIZE;
      bmp->header.width = 1;
      bmp->header.height = 1;
      bmp->header.planes = 1;
      bmp->header.bpp = BMP_888_BPP;
      bmp->header.compression = BMP_NO_COMPRESSION;
      bmp->header.data_size = get_bitmap_pixels_size (1, 1);
      bmp->header.ppm_horiz = BMP_72_DPI;
      bmp->header.ppm_vert = BMP_72_DPI;
      bmp->header.palette_colors = BMP_NO_PALETTE;
      bmp->header.important_colors = BMP_NO_PALETTE;
      grub_memcpy(bmp->pixels,
          "\x00\x00\x00" // 1 black pixel (RGB 888)
          "\x00", // DWORD row padding
          4
      );
    }
  }
  else
  {
    grub_dprintf ("hackbgrt", "HackBGRT: Loading %s.\n", path);
    file = grub_file_open(path, GRUB_FILE_TYPE_PIXMAP);
    if (!file)
    {
      grub_error(GRUB_ERR_READ_ERROR, "HackBGRT: Failed to load BMP (%s)!\n", path);
      grub_efi_system_table->boot_services->stall(1000000); // 1 sec pause
    }
    else
    {
      struct bitmap_header header;
      grub_dprintf ("hackbgrt", "file %s opened\n", path);
      if (grub_file_read (file, &header, sizeof (header)) != sizeof (header))
      {
        grub_file_close(file);
        grub_error(GRUB_ERR_READ_ERROR, "HackBGRT: Failed to load BMP (%s)!\n", path);
        grub_efi_system_table->boot_services->stall(1000000); // 1 sec pause
      }
      else
      {
        grub_dprintf ("hackbgrt", "header of %s read\n", path);
        grub_dprintf ("hackbgrt", "signature %s, pixel_data_offset=%d, dib_header_size=%d, planes=%d, bpp=%d, compression=%d, palette_colors=%d, important_colors=%d\n",
                grub_memcmp(&header.signature, BMP_MAGIC, BMP_MAGIC_SIZE) == 0 ? "ok" : "ko",
                header.pixel_data_offset,
                header.dib_header_size,
                header.planes,
                header.bpp,
                header.compression,
                header.palette_colors,
                header.important_colors
                );
        if (grub_memcmp(&header.signature, BMP_MAGIC, BMP_MAGIC_SIZE) != 0
            || header.pixel_data_offset != BMP_PIXEL_DATA_OFFSET
            || header.dib_header_size != BMP_DIB_HEADER_SIZE
            || header.planes != 1
            || header.bpp != BMP_888_BPP
            || header.compression != BMP_NO_COMPRESSION
            || header.palette_colors != BMP_NO_PALETTE
            || header.important_colors != BMP_NO_PALETTE)
        {
          grub_file_close(file);
          grub_error(GRUB_ERR_READ_ERROR, "HackBGRT: Failed to load BMP, not supported format (%s)!\n", path);
          grub_efi_system_table->boot_services->stall(1000000); // 1 sec pause
        }
        else
        {
          grub_dprintf ("hackbgrt", "header of %s OK (bitmap size = %d)\n", path, header.size);
          status = efi_call_3 (grub_efi_system_table->boot_services->allocate_pool, GRUB_EFI_BOOT_SERVICES_DATA, header.size, (void**) &bmp);
          if (status)
          {
            grub_file_close(file);
            grub_error(GRUB_ERR_READ_ERROR, "HackBGRT: Failed to allocate memory for BMP!\n");
            grub_efi_system_table->boot_services->stall(1000000); // 1 sec pause
          }
          else
          {
            grub_dprintf ("hackbgrt", "EFI memory allocated for bitmap\n");
            grub_memcpy(&bmp->header, &header, sizeof (header));
            grub_dprintf ("hackbgrt", "EFI bitmap header copied\n");
            grub_uint32_t pixels_size = header.data_size;
            if (grub_file_read (file, &bmp->pixels, pixels_size) != pixels_size)
            {
              grub_file_close(file);
              grub_error(GRUB_ERR_READ_ERROR, "HackBGRT: Failed to load BMP (%s)!\n", path);
              grub_efi_system_table->boot_services->stall(1000000); // 1 sec pause
            }
            else
            {
              grub_dprintf ("hackbgrt", "EFI bitmap pixels (%d) copied\n", pixels_size);
              grub_file_close(file);
            }
          }
        }
      }
    }
  }
  grub_print_error ();
  grub_dprintf ("hackbgrt", "EFI bitmap = %p\n", bmp);
  return bmp;
}

/**
 * Select the correct coordinate (manual, automatic, native)
 *
 * @param value The configured coordinate value; has special values for automatic and native.
 * @param automatic The automatically calculated alternative.
 * @param native The original coordinate.
 * @see enum hackbgrt_coordinate
 */
static int select_coordinate(int value, int automatic, int native) {
  if (value == HACKBGRT_COORD_AUTO)
    return automatic;
  else if (value == HACKBGRT_COORD_NATIVE)
    return native;
  return value;
}

/**
 * The main logic for BGRT modification.
 *
 * @param config The hack BGRT config.
 */
static void
hack_bgrt(hackbgrt_config_t config)
{
  // REMOVE: simply delete all BGRT entries.
  if (config->action == HACKBGRT_REMOVE)
  {
    grub_dprintf ("hackbgrt", "Remove old BGRT.\n");
    handle_acpi_tables(HACKBGRT_REMOVE, 0);
    return;
  }
  grub_dprintf ("hackbgrt", "Get old BGRT.\n");
  grub_acpi_bgrt_t bgrt = handle_acpi_tables(HACKBGRT_KEEP, 0);
  bitmap_t old_bmp = 0;
  int old_x = 0, old_y = 0;
  if (bgrt && verify_acpi_sdt_checksum(bgrt) == 0)
  {
    grub_dprintf ("hackbgrt", "Get old Bitmap and position.\n");
    old_bmp = (bitmap_t) bgrt->image_address;
    old_x = bgrt->image_offset_x;
    old_y = bgrt->image_offset_y;
  }
  // Missing BGRT?
  if (!bgrt)
  {
    // Keep missing = do nothing.
    if (config->action == HACKBGRT_KEEP)
      return;
    grub_dprintf ("hackbgrt", "Allocate new BGRT because there was no old one.\n");
    grub_efi_status_t status = efi_call_3 (grub_efi_system_table->boot_services->allocate_pool, GRUB_EFI_ACPI_RECLAIM_MEMORY, sizeof (*bgrt), (void**) &bgrt);
    if (status)
    {
      grub_error(GRUB_ERR_READ_ERROR, "HackBGRT: Failed to allocate memory for BGRT.\n");
      return;
    }
  }
  grub_dprintf ("hackbgrt", "Clear BGRT, fill new values.\n");
  grub_memcpy(bgrt->header.signature, BGRT_MAGIC, BGRT_MAGIC_SIZE);
  bgrt->header.length = BGRT_HEADER_SIZE;
  bgrt->header.revision = 0;
  grub_memcpy(bgrt->header.oemid, "GRUB_2", 6);
  grub_memcpy(bgrt->header.oemtable, "HackBGRT", 8);
  bgrt->header.oemrev = 1;
  grub_memcpy(bgrt->header.creator_id, "ACPI", 4);
  bgrt->header.creator_rev = 20201214; // 2020-12-14
  bgrt->version = BGRT_VERSION;
  bgrt->status = BGRT_STATUS_VALID;
  bgrt->image_type = BGRT_IMAGE_TYPE_BMP;
  set_acpi_sdt_checksum(bgrt);
  grub_dprintf ("hackbgrt", "Get the bitmap.\n");
  bitmap_t new_bmp = old_bmp;
  if (config->action == HACKBGRT_REPLACE)
  {
    grub_dprintf ("hackbgrt", "Load BMP %s.\n", config->image_path);
    new_bmp = load_bmp(config->image_path);
  }
  if (!new_bmp)
  {
    grub_dprintf ("hackbgrt", "No bitmap, no need for BGRT.\n");
    handle_acpi_tables(HACKBGRT_REMOVE, 0);
    return;
  }
  grub_dprintf ("hackbgrt", "Address new bitmap into BGRT structure.\n");
  bgrt->image_address = (grub_uint64_t) new_bmp;
  // Calculate the automatically centered position for the image.
  int auto_x = 0, auto_y = 0;
  struct grub_efi_gop* gop = get_gop();
  if (gop)
  {
    grub_dprintf ("hackbgrt", "Compute new bitmap position using GOP info.\n");
    auto_x = grub_max(0, ((int) gop->mode->info->width - (int) new_bmp->header.width) / 2);
    auto_y = grub_max(0, ((int) gop->mode->info->height * 2/3 - (int) new_bmp->header.height) / 2);
  }
  else if (old_bmp)
  {
    grub_dprintf ("hackbgrt", "Compute new bitmap position using old bitmap info.\n");
    auto_x = grub_max(0, old_x + ((int) old_bmp->header.width - (int) new_bmp->header.width) / 2);
    auto_y = grub_max(0, old_y + ((int) old_bmp->header.height - (int) new_bmp->header.height) / 2);
  }
  grub_dprintf ("hackbgrt", "Set the bitmap position (manual, automatic, original) into BGRT structure.\n");
  bgrt->image_offset_x = select_coordinate(config->image_x, auto_x, old_x);
  bgrt->image_offset_y = select_coordinate(config->image_y, auto_y, old_y);
  set_acpi_sdt_checksum(bgrt);
  grub_dprintf ("hackbgrt", "Store this BGRT (%d x %d).\n", (int) bgrt->image_offset_x, (int) bgrt->image_offset_y);
  handle_acpi_tables(HACKBGRT_REPLACE, bgrt);
}

static grub_err_t
grub_cmd_hackbgrt (grub_extcmd_context_t ctxt __attribute__ ((unused)),
                   int argc,
		   char* argv[])
{
  const char* base_config_path = "/EFI/HackBGRT/config.txt";
  grub_size_t base_len;
  grub_size_t esp_arg_len;
  char* config_path;
  hackbgrt_config_t config;

  if (argc == 0)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, N_("EFI system partition (ESP) expected"));
  esp_arg_len = grub_strlen (argv[0]);
  if (esp_arg_len < 5 || argv[0][0] != '(' || argv[0][esp_arg_len - 1] != ')')
  {
    return grub_error (GRUB_ERR_BAD_ARGUMENT, N_("format (hd0,gpt1) expected"));
  }
  base_len = grub_strlen (base_config_path);
  config_path = grub_zalloc (esp_arg_len + base_len + 1);
  grub_strncpy (config_path, argv[0], esp_arg_len);
  grub_strncpy (config_path + esp_arg_len, base_config_path, base_len);
  grub_dprintf ("hackbgrt", "Hack BGRT config file=%s\n", config_path);
  config = hackbgrt_read_config (argv[0], config_path);
  if (! config)
  {
    grub_print_error ();
    goto fail;
  }
  grub_dprintf ("hackbgrt", "starting hack\n");
  hack_bgrt(config);
  grub_print_error ();
  grub_dprintf ("hackbgrt", "ending hack\n");
fail:
  grub_dprintf ("hackbgrt", "free some memory\n");
  if (config_path)
    grub_free (config_path);
  if (config)
    hackbgrt_free_config (config);
  grub_dprintf ("hackbgrt", "end of my code\n");
  return grub_errno;
}

static grub_extcmd_t cmd;

GRUB_MOD_INIT(hackbgrt)
{
  cmd = grub_register_extcmd (
      "hackbgrt",
      grub_cmd_hackbgrt,
      GRUB_COMMAND_FLAG_BLOCKS,
      N_("EFI_PARTITION"),
      N_("Change the BGRT image."),
      0
  );
}

GRUB_MOD_FINI(hackbgrt)
{
  grub_unregister_extcmd (cmd);
}
