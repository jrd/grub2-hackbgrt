#pragma once

/**
 * Possible actions to perform on the BGRT.
 */
enum hackbgrt_action
{
  HACKBGRT_KEEP = 0,
  HACKBGRT_REPLACE,
  HACKBGRT_REMOVE
};

/**
 * Special values for the image coordinates.
 * @see struct hackbgrt_config
 */
enum hackbgrt_coordinate
{
  HACKBGRT_COORD_AUTO = 0x10000001,
  HACKBGRT_COORD_KEEP = 0x10000002
};

/**
 * The configuration.
 */
struct hackbgrt_config
{
  enum hackbgrt_action action;
  char* image_path;
  int image_x;
  int image_y;
};

typedef struct hackbgrt_config* hackbgrt_config_t;

/**
 * Read a configuration file.
 *
 * @param esp_path ESP path like (hd0,gpt1).
 * @param params configuration parameters.
 * @param params_count number of parameters.
 * @return the read configuration or 0 if error.
 */
extern hackbgrt_config_t
hackbgrt_read_config (const char* esp_path, const char* params[], const grub_size_t params_count);

extern void
hackbgrt_free_config (hackbgrt_config_t config);
