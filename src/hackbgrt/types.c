#include "types.h"
#include "grub/acpi.h"

int
verify_acpi_rsdp2_checksums (void* data)
{
  struct grub_acpi_rsdp_v20* rsdp = (struct grub_acpi_rsdp_v20*) data;
  return grub_byte_checksum(data, sizeof (rsdp->rsdpv1)) == 0 && grub_byte_checksum(data, rsdp->length) == 0;
}

void
set_acpi_rsdp2_checksums (void* data)
{
  struct grub_acpi_rsdp_v20* rsdp = (struct grub_acpi_rsdp_v20*) data;
  rsdp->rsdpv1.checksum = -grub_byte_checksum(data, sizeof (rsdp->rsdpv1));
  rsdp->checksum = -grub_byte_checksum(data, rsdp->length);
}

int
verify_acpi_sdt_checksum (void* data)
{
  struct grub_acpi_table_header* acpi_table_header = (struct grub_acpi_table_header*) data;
  return grub_byte_checksum(data, acpi_table_header->length) == 0;
}

void
set_acpi_sdt_checksum (void* data)
{
  struct grub_acpi_table_header* acpi_table_header = (struct grub_acpi_table_header*) data;
  acpi_table_header->checksum = -grub_byte_checksum(data, acpi_table_header->length);
}
