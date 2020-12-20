#include <grub/bufio.h>
#include <grub/efi/efi.h>
#include <grub/file.h>
#include <grub/misc.h>
#include <grub/normal.h>
#include <grub/random.h>
#include <grub/types.h>
#include "config.h"


char* str_strip(const char* s);
char* str_str_after(const char* haystack, const char* needle);
grub_err_t hackbgrt_read_config_file_getline (char **line, void *data);
grub_err_t hackbgrt_parse_line (char* line, const char* esp_path, hackbgrt_config_t config);
void hackbgrt_set_bmp_with_random(hackbgrt_config_t config, const char* esp_path, int weight, enum hackbgrt_action action, int x, int y, const char* path);
int hackbgrt_parse_coordinate(const char* str, enum hackbgrt_action action);
void hackbgrt_read_config_image(const char* esp_path, hackbgrt_config_t config, const char* line);
void hackbgrt_read_config_resolution(hackbgrt_config_t config, const char* line);


/**
 * Strip spaces and tabs from the beginning/ending of a string.
 *
 * @param s The string.
 * @return new string pointer (should be freed).
 */
char*
str_strip(const char* s)
{
  grub_size_t len = grub_strlen (s);
  grub_size_t skip_start;
  grub_size_t skip_end;
  char* ret;

  for (skip_start = 0; s[0] && (s[0] == ' ' || s[0] == '\t'); skip_start++);
  for (skip_end = 0; len - skip_end > 0 && (s[len - skip_end] == ' ' || s[len - skip_end] == '\t'); skip_end++);
  ret = grub_zalloc (len - skip_start - skip_end + 1);
  grub_strncpy (ret, s + skip_start, len - skip_start - skip_end);
  return ret;
}

/**
 * Find the position after another string within a string.
 *
 * @param haystack The full text.
 * @param needle The string to look for.
 * @return new string pointer (should be freed).
 */
char*
str_str_after(const char* haystack, const char* needle)
{
  return (haystack = grub_strstr(haystack, needle)) ? grub_strdup(haystack + grub_strlen(needle)) : 0;
}


hackbgrt_config_t
hackbgrt_read_config (const char* esp_path, const char* config_path)
{
  grub_file_t rawfile, file;
  hackbgrt_config_t config;

  config = grub_zalloc (sizeof (*config));
  if (! config)
    return 0;
  /* Try to open the config file.  */
  rawfile = grub_file_open (config_path, GRUB_FILE_TYPE_CONFIG);
  if (! rawfile)
    return 0;
  file = grub_bufio_open (rawfile, 0);
  if (! file)
  {
    grub_file_close (rawfile);
    return 0;
  }
  while (1)
  {
    char *line;
    /* Print an error, if any.  */
    grub_print_error ();
    grub_errno = GRUB_ERR_NONE;
    if ((hackbgrt_read_config_file_getline (&line, file)) || (! line))
      break;
    hackbgrt_parse_line (line, esp_path, config);
    grub_free (line);
  }
  grub_dprintf ("hackbgrt", "config is read\n");
  grub_file_close (file);
  return config;
}

void
hackbgrt_free_config (hackbgrt_config_t config)
{
  if (config->image_path)
    grub_free (config->image_path);
  grub_free (config);
}

grub_err_t
hackbgrt_read_config_file_getline (char **line, void *data)
{
  grub_file_t file = data;
  while (1)
  {
    char *buf;
    *line = buf = grub_file_getline (file);
    if (! buf)
      return grub_errno;
    if (buf[0] == '#')
      grub_free (*line);
    else
      break;
  }
  return GRUB_ERR_NONE;
}

grub_err_t
hackbgrt_parse_line (char* line, const char* esp_path, hackbgrt_config_t config)
{
  char* strip_line = str_strip (line);
  if (strip_line[0])
  {
    grub_errno = GRUB_ERR_NONE;
    if (grub_strncmp(line, "image=", 6) == 0)
      hackbgrt_read_config_image(esp_path, config, strip_line + 6);
    else if (grub_strncmp(line, "resolution=", 11) == 0)
      hackbgrt_read_config_resolution(config, strip_line + 11);
    else
      grub_error (GRUB_ERR_READ_ERROR, "Unknown configuration directive: %s", line);
  }
  grub_free (strip_line);
  return grub_errno;
}

void
hackbgrt_set_bmp_with_random(hackbgrt_config_t config, const char* esp_path, int weight, enum hackbgrt_action action, int x, int y, const char* path)
{
  grub_uint32_t random;
  grub_uint32_t limit;
  grub_size_t esp_len;
  grub_size_t path_len;

  grub_crypto_get_random (&random, sizeof (grub_uint32_t));
  config->image_weight_sum += weight;
  limit = 0xfffffffful / config->image_weight_sum * weight;
  grub_dprintf("hackbgrt", "HackBGRT: weight %d, action %d, x %d, y %d, path %s, random = %08x, limit = %08x\n", weight, action, x, y, path, random, limit);
  if (!config->image_weight_sum || random <= limit)
  {
    config->action = action;
    esp_len = grub_strlen (esp_path);
    path_len = grub_strlen (path);
    config->image_path = grub_zalloc (esp_len + path_len + 1);
    grub_strncpy (config->image_path, esp_path, esp_len);
    grub_strncpy (config->image_path + esp_len, path, path_len);
    config->image_x = x;
    config->image_y = y;
  }
}

#define atoi_1(p) (*(p) - '0')

int
hackbgrt_parse_coordinate(const char* str, enum hackbgrt_action action)
{
  if (str && '0' <= str[0] && str[0] <= '9')
    return atoi_1(str);
  if ((str && grub_strncmp(str, "native", 6) == 0) || action == HACKBGRT_KEEP)
    return HACKBGRT_COORD_NATIVE;
  return HACKBGRT_COORD_AUTO;
}

void
hackbgrt_read_config_image(const char* esp_path, hackbgrt_config_t config, const char* line)
{
  const char* n = str_str_after(line, "n=");
  const char* x = str_str_after(line, "x=");
  const char* y = str_str_after(line, "y=");
  const char* path = str_str_after(line, "path=");
  enum hackbgrt_action action = HACKBGRT_KEEP;
  if (path)
    action = HACKBGRT_REPLACE;
  else if (grub_strstr(line, "remove"))
    action = HACKBGRT_REMOVE;
  else if (grub_strstr(line, "black"))
    action = HACKBGRT_REPLACE;
  else if (grub_strstr(line, "keep"))
    action = HACKBGRT_KEEP;
  else
  {
    grub_error(GRUB_ERR_READ_ERROR, "HackBGRT: Invalid image line: %s", line);
    return;
  }
  int weight = n && (!path || n < path) ? atoi_1(n) : 1;
  hackbgrt_set_bmp_with_random(config, esp_path, weight, action, hackbgrt_parse_coordinate(x, action), hackbgrt_parse_coordinate(y, action), path);
}

void
hackbgrt_read_config_resolution(hackbgrt_config_t config, const char* line)
{
  const char* x = line;
  const char* y = str_str_after(line, "x");
  if (x && *x && y && *y)
  {
    config->resolution_x = *x == '-' ? -(int) atoi_1(x + 1) : (int) atoi_1(x);
    config->resolution_y = *y == '-' ? -(int) atoi_1(y + 1) : (int) atoi_1(y);
  }
  else
    grub_error(GRUB_ERR_READ_ERROR, "HackBGRT: Invalid resolution line: %s", line);
}
