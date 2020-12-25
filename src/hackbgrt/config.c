#include <grub/bufio.h>
#include <grub/efi/efi.h>
#include <grub/file.h>
#include <grub/misc.h>
#include <grub/normal.h>
#include <grub/random.h>
#include <grub/types.h>
#include "config.h"


grub_err_t hackbgrt_parse_param (const char* param, const char* esp_path, hackbgrt_config_t config, int* image_weight_sum_p);
char** hackbgrt_strsplit (const char* s, const char separator);
char* hackbgrt_strsep (char** stringp, const char separator);
int hackbgrt_parse_coordinate(const char* str, enum hackbgrt_action action);
void hackbgrt_set_config_with_random(const char* esp_path, hackbgrt_config_t config, enum hackbgrt_action action, const char* path, int x, int y, int weight, int* weight_sum_p);


hackbgrt_config_t
hackbgrt_read_config (const char* esp_path, const char* params[], const grub_size_t params_count)
{
  const char* param;
  hackbgrt_config_t config = grub_zalloc (sizeof (struct hackbgrt_config));
  if (! config)
    return 0;
  int image_weight_sum = 0;
  for (grub_size_t i = 0; i < params_count; i++)
  {
    param = params[i];
    hackbgrt_parse_param (param, esp_path, config, &image_weight_sum);
    /* Print an error, if any.  */
    grub_print_error ();
  }
  grub_dprintf ("hackbgrt", "config is read\n");
  return config;
}

grub_err_t
hackbgrt_parse_param (const char* param, const char* esp_path, hackbgrt_config_t config, int* image_weight_sum_p)
{
  int action = HACKBGRT_REPLACE;
  char* image_path = NULL;
  char* image_x_str = NULL;
  int image_x = HACKBGRT_COORD_AUTO;
  char* image_y_str = NULL;
  int image_y = HACKBGRT_COORD_AUTO;
  char* image_weight_str = NULL;
  int image_weight = 1;

  grub_errno = GRUB_ERR_NONE;
  char** var_values = hackbgrt_strsplit (param, ',');
  for (char* var_value = *var_values; var_value; var_value += sizeof (char*))
  {
    char* var = hackbgrt_strsep (&var_value, '=');
    char* value = var_value;
    if (!value)
    {
      grub_error (GRUB_ERR_READ_ERROR, "No variable=value defined in parameter: %s", param);
      break;
    }
    if (grub_strcmp(var, "image") == 0 && !image_path)
      image_path = value;
    else if (grub_strcmp(var, "x") == 0 && !image_x_str)
      image_x_str = value;
    else if (grub_strcmp(var, "y") == 0 && !image_y_str)
      image_y_str = value;
    else if (grub_strcmp(var, "weight") == 0 && !image_weight_str)
      image_weight_str = value;
    else
    {
      grub_error (GRUB_ERR_READ_ERROR, "Unknown variable in parameter: %s", param);
      break;
    }
  }
  if (grub_errno != GRUB_ERR_NONE)
    goto fail;
  if (image_path == NULL)
  {
    grub_error (GRUB_ERR_READ_ERROR, "image variable should be defined in parameter: %s", param);
    goto fail;
  }
  else
  {
    if (grub_strcmp(image_path, "keep") == 0)
    {
      action = HACKBGRT_KEEP;
      image_x = HACKBGRT_COORD_KEEP;
      image_y = HACKBGRT_COORD_KEEP;
    }
    else if (grub_strcmp(image_path, "remove") == 0)
    {
      action = HACKBGRT_REMOVE;
      image_x = 0;
      image_y = 0;
    }
    else if (grub_strncmp(image_path, "/", 1) != 0)
    {
      grub_error (GRUB_ERR_READ_ERROR, "image variable should define a BMP image path or 'keep' or 'remove': %s", param);
      goto fail;
    }
  }
  if (image_x_str != NULL)
    image_x = hackbgrt_parse_coordinate (image_x_str, action);
  if (image_y_str != NULL)
    image_y = hackbgrt_parse_coordinate (image_y_str, action);
  if (image_weight_str != NULL)
    image_weight = (int) grub_strtoul (image_weight_str, 0, 10);
  hackbgrt_set_config_with_random(esp_path, config, action, image_path, image_x, image_y, image_weight, image_weight_sum_p);
  goto succeed;
fail:
  grub_print_error ();
succeed:
  for (char* var_value = *var_values; var_value; var_value += sizeof (char*))
    grub_free (var_value);
  grub_free (var_values);
  return grub_errno;
}

char**
hackbgrt_strsplit (const char* s, const char separator)
{
  grub_size_t count = 2; // n + 1 intervals, last one is null
  char** ret;

  for (char* c = (char*) s; *c; c++)
    if (*c == separator)
      count++;
  ret = (char**) grub_malloc (sizeof (char*) * count);
  char** elem_p = ret;
  for (char* part = grub_strdup (s); part;)
  {
    char* elem = hackbgrt_strsep (&part, separator);
    if (elem)
    {
      *elem_p = elem;
      elem_p += sizeof (char*);
    }
  }
  *elem_p = NULL;
  return ret;
}

char*
hackbgrt_strsep (char** stringp, const char separator)
{
  char* begin = *stringp;
  if (begin == NULL)
    return NULL;
  char* end = grub_strchr (begin, separator);
  if (*end)
  {
    *end++ = '\0';
    *stringp = end;
  }
  else
    *stringp = NULL;
  return begin;
}

int
hackbgrt_parse_coordinate(const char* str, enum hackbgrt_action action)
{
  if (str && '1' <= str[0] && str[0] <= '9')
    return (int) grub_strtoul (str, 0, 10);
  if ((str && grub_strncmp(str, "keep", 4) == 0) || action == HACKBGRT_KEEP)
    return HACKBGRT_COORD_KEEP;
  return HACKBGRT_COORD_AUTO;
}

void
hackbgrt_set_config_with_random(const char* esp_path, hackbgrt_config_t config, enum hackbgrt_action action, const char* path, int x, int y, int weight, int* weight_sum_p)
{
  grub_uint32_t random;
  grub_uint32_t limit;
  grub_size_t esp_len;
  grub_size_t path_len;
  int weight_sum = *weight_sum_p;

  grub_crypto_get_random (&random, sizeof (grub_uint32_t));
  weight_sum += weight;
  limit = 0xfffffffful / weight_sum * weight;
  grub_dprintf("hackbgrt", "HackBGRT: action %d, path %s, x %d, y %d, weight %d, random = %08x, limit = %08x\n", action, path, x, y, weight, random, limit);
  if (!weight_sum || random <= limit)
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

void
hackbgrt_free_config (hackbgrt_config_t config)
{
  if (config->image_path)
    grub_free (config->image_path);
  grub_free (config);
}
