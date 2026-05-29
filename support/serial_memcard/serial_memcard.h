#pragma once

#include <stdint.h>
#include <stddef.h>

bool serial_memcard_enabled();
void serial_memcard_invalidate_scan_cache();
void serial_memcard_rescan_async();
bool serial_memcard_psx_slots_scanned();
uint8_t serial_memcard_psx_slot_mask();

bool serial_memcard_prepare_psx(uint8_t port, uint8_t slot, char* out_path, size_t out_len);
uint32_t serial_memcard_prepare_psx_async(uint8_t port, uint8_t slot, unsigned char index);
uint32_t serial_memcard_prepare_psx_ordinal_async(uint8_t ordinal, unsigned char index);
uint32_t serial_memcard_prepare_psx_auto_async(unsigned char index1, unsigned char index2);
bool serial_memcard_take_async_mount(unsigned char index, uint32_t token, char* out_path, size_t out_len, bool* done);
void serial_memcard_cancel_async_mount(unsigned char index);
bool serial_memcard_prepare_n64_cpak(uint8_t cpak_index, char* out_path, size_t out_len);
bool serial_memcard_prepare_n64_tpak(char* out_path, size_t out_len);
bool serial_memcard_prepare_n64_tpak_for_rom(const char* rom_path, char* out_path, size_t out_len);
bool serial_memcard_prepare_gb_save_for_rom(const char* rom_path, char* out_path, size_t out_len);
bool serial_memcard_prefetch_n64_cpak();

bool serial_memcard_attach_mount(const char* path, unsigned char index);
void serial_memcard_unmount(unsigned char index);
bool serial_memcard_take_mount_info(char* out, size_t out_len);
bool serial_memcard_take_mount_info_summary(char* out, size_t out_len);
bool serial_memcard_handle_write(unsigned char index, uint64_t offset,
                                     const uint8_t* data, uint32_t len);
