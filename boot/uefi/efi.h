#ifndef ZENKU_EFI_H
#define ZENKU_EFI_H

/*
 * Minimal UEFI definitions used by the Zenku boot loader.
 *
 * The loader is compiled with clang for the x86_64-pc-win32 target and linked
 * as a PE32+ application, so this header stays independent of gnu-efi and of
 * the host C library. Only the protocols the loader actually needs are
 * declared here.
 */

#include <types.h>

#define EFIAPI __attribute__((ms_abi))
#define EFI_INLINE __attribute__((always_inline)) static inline

typedef uint64_t EFI_STATUS;
typedef uint64_t EFI_TPL;
typedef uint64_t EFI_PHYSICAL_ADDRESS;
typedef uint64_t EFI_VIRTUAL_ADDRESS;
typedef uint64_t EFI_LBA;
typedef uint64_t EFI_EVENT;
typedef uint64_t UINTN;
typedef uint32_t EFI_MEMORY_TYPE;
typedef uint16_t CHAR16;
typedef uint8_t BOOLEAN;
typedef void *EFI_HANDLE;

struct efi_system_table;

#define EFI_SUCCESS 0ull
#define EFI_ERROR(status) (((int64_t)(status)) < 0)
#define EFI_ERROR_BIT 0x8000000000000000ull

struct efi_guid {
    uint32_t data1;
    uint16_t data2;
    uint16_t data3;
    uint8_t data4[8];
} PACKED;

struct efi_table_header {
    uint64_t signature;
    uint32_t revision;
    uint32_t header_size;
    uint32_t crc32;
    uint32_t reserved;
} PACKED;

/* ------------------------------------------------------------------------- */
/* Simple text output                                                        */
/* ------------------------------------------------------------------------- */

struct efi_simple_text_output_mode {
    int32_t max_mode;
    int32_t mode;
    int32_t attribute;
    int32_t cursor_column;
    int32_t cursor_row;
    BOOLEAN cursor_visible;
} PACKED;

struct efi_simple_text_output {
    EFI_STATUS (EFIAPI *reset)(struct efi_simple_text_output *self,
                              BOOLEAN extended);
    EFI_STATUS (EFIAPI *output_string)(struct efi_simple_text_output *self,
                                      const CHAR16 *string);
    EFI_STATUS (EFIAPI *test_string)(struct efi_simple_text_output *self,
                                    const CHAR16 *string);
    EFI_STATUS (EFIAPI *query_mode)(struct efi_simple_text_output *self,
                                   UINTN mode, UINTN *columns, UINTN *rows);
    EFI_STATUS (EFIAPI *set_mode)(struct efi_simple_text_output *self,
                                 UINTN mode);
    EFI_STATUS (EFIAPI *set_attribute)(struct efi_simple_text_output *self,
                                      UINTN attribute);
    EFI_STATUS (EFIAPI *clear_screen)(struct efi_simple_text_output *self);
    EFI_STATUS (EFIAPI *set_cursor_position)(
        struct efi_simple_text_output *self, UINTN column, UINTN row);
    EFI_STATUS (EFIAPI *enable_cursor)(struct efi_simple_text_output *self,
                                      BOOLEAN visible);
    struct efi_simple_text_output_mode *mode;
};

/* ------------------------------------------------------------------------- */
/* Boot services                                                             */
/* ------------------------------------------------------------------------- */

enum efi_allocate_type {
    EFI_ALLOCATE_ANY_PAGES = 0,
    EFI_ALLOCATE_MAX_ADDRESS = 1,
    EFI_ALLOCATE_ADDRESS = 2,
};

enum efi_memory_type {
    EFI_RESERVED_MEMORY_TYPE = 0,
    EFI_LOADER_CODE,
    EFI_LOADER_DATA,
    EFI_BOOT_SERVICES_CODE,
    EFI_BOOT_SERVICES_DATA,
    EFI_RUNTIME_SERVICES_CODE,
    EFI_RUNTIME_SERVICES_DATA,
    EFI_CONVENTIONAL_MEMORY,
    EFI_UNUSABLE_MEMORY,
    EFI_ACPI_RECLAIM_MEMORY,
    EFI_ACPI_MEMORY_NVS,
    EFI_MEMORY_MAPPED_IO,
    EFI_MEMORY_MAPPED_IO_PORT_SPACE,
    EFI_PAL_CODE,
    EFI_PERSISTENT_MEMORY,
    EFI_UNACCEPTED_MEMORY,
};

struct efi_memory_descriptor {
    uint32_t type;
    uint32_t pad;
    EFI_PHYSICAL_ADDRESS physical_start;
    EFI_VIRTUAL_ADDRESS virtual_start;
    uint64_t number_of_pages;
    uint64_t attribute;
} PACKED;

struct efi_boot_services {
    struct efi_table_header hdr;

    EFI_TPL (EFIAPI *raise_tpl)(EFI_TPL new_tpl);
    void (EFIAPI *restore_tpl)(EFI_TPL old_tpl);

    EFI_STATUS (EFIAPI *allocate_pages)(enum efi_allocate_type type,
                                        EFI_MEMORY_TYPE memory_type,
                                        UINTN pages,
                                        EFI_PHYSICAL_ADDRESS *memory);
    EFI_STATUS (EFIAPI *free_pages)(EFI_PHYSICAL_ADDRESS memory,
                                   UINTN pages);
    EFI_STATUS (EFIAPI *get_memory_map)(UINTN *map_size,
                                        struct efi_memory_descriptor *map,
                                        UINTN *map_key,
                                        UINTN *descriptor_size,
                                        uint32_t *descriptor_version);
    EFI_STATUS (EFIAPI *allocate_pool)(EFI_MEMORY_TYPE pool_type,
                                       UINTN size, void **buffer);
    EFI_STATUS (EFIAPI *free_pool)(void *buffer);

    EFI_STATUS (EFIAPI *create_event)(uint32_t type, EFI_TPL notify_tpl,
                                      void *notify_function,
                                      void *notify_context,
                                      EFI_EVENT *event);
    EFI_STATUS (EFIAPI *set_timer)(EFI_EVENT event, uint32_t type,
                                   uint64_t trigger_time);
    EFI_STATUS (EFIAPI *wait_for_event)(UINTN number_of_events,
                                        EFI_EVENT *event, UINTN *index);
    EFI_STATUS (EFIAPI *signal_event)(EFI_EVENT event);
    EFI_STATUS (EFIAPI *close_event)(EFI_EVENT event);
    EFI_STATUS (EFIAPI *check_event)(EFI_EVENT event);

    EFI_STATUS (EFIAPI *install_protocol_interface)(EFI_HANDLE *handle,
                                                    struct efi_guid *protocol,
                                                    uint32_t interface_type,
                                                    void *interface);
    EFI_STATUS (EFIAPI *reinstall_protocol_interface)(EFI_HANDLE handle,
                                                      struct efi_guid *protocol,
                                                      void *old_interface,
                                                      void *new_interface);
    EFI_STATUS (EFIAPI *uninstall_protocol_interface)(EFI_HANDLE handle,
                                                      struct efi_guid *protocol,
                                                      void *interface);
    EFI_STATUS (EFIAPI *handle_protocol)(EFI_HANDLE handle,
                                         struct efi_guid *protocol,
                                         void **interface);
    void *reserved;
    EFI_STATUS (EFIAPI *register_protocol_notify)(struct efi_guid *protocol,
                                                  EFI_EVENT event,
                                                  void **registration);
    EFI_STATUS (EFIAPI *locate_handle)(uint32_t search_type,
                                       struct efi_guid *protocol,
                                       void *search_key,
                                       UINTN *buffer_size,
                                       EFI_HANDLE *buffer);
    EFI_STATUS (EFIAPI *locate_device_path)(struct efi_guid *protocol,
                                            void **device_path,
                                            EFI_HANDLE *device);
    EFI_STATUS (EFIAPI *install_configuration_table)(
        struct efi_guid *guid, void *table);

    EFI_STATUS (EFIAPI *load_image)(BOOLEAN boot_policy,
                                    EFI_HANDLE parent_image_handle,
                                    void *device_path, void *source_buffer,
                                    UINTN source_size, EFI_HANDLE *image_handle);
    EFI_STATUS (EFIAPI *start_image)(EFI_HANDLE image_handle,
                                     UINTN *exit_data_size,
                                     CHAR16 **exit_data);
    EFI_STATUS (EFIAPI *exit)(EFI_HANDLE image_handle,
                              EFI_STATUS exit_status,
                              UINTN exit_data_size, CHAR16 *exit_data);
    EFI_STATUS (EFIAPI *unload_image)(EFI_HANDLE image_handle);
    EFI_STATUS (EFIAPI *exit_boot_services)(EFI_HANDLE image_handle,
                                            UINTN map_key);

    EFI_STATUS (EFIAPI *get_next_monotonic_count)(uint64_t *count);
    EFI_STATUS (EFIAPI *stall)(UINTN microseconds);
    EFI_STATUS (EFIAPI *set_watchdog_timer)(UINTN timeout,
                                            uint64_t watchdog_code,
                                            UINTN data_size,
                                            CHAR16 *watchdog_data);

    EFI_STATUS (EFIAPI *connect_controller)(EFI_HANDLE controller,
                                            void *driver_image_handle,
                                            void *remaining_device_path,
                                            BOOLEAN recursive);
    EFI_STATUS (EFIAPI *disconnect_controller)(EFI_HANDLE controller,
                                               EFI_HANDLE driver_image_handle,
                                               EFI_HANDLE child_handle);

    EFI_STATUS (EFIAPI *open_protocol)(EFI_HANDLE handle,
                                       struct efi_guid *protocol,
                                       void **interface, EFI_HANDLE agent,
                                       EFI_HANDLE controller,
                                       uint32_t attributes);
    EFI_STATUS (EFIAPI *close_protocol)(EFI_HANDLE handle,
                                        struct efi_guid *protocol,
                                        EFI_HANDLE agent,
                                        EFI_HANDLE controller);
    EFI_STATUS (EFIAPI *open_protocol_information)(EFI_HANDLE handle,
                                                   struct efi_guid *protocol,
                                                   void **entry_buffer,
                                                   UINTN *entry_count);

    EFI_STATUS (EFIAPI *protocols_per_handle)(EFI_HANDLE handle,
                                              struct efi_guid ***protocols,
                                              UINTN *protocol_count);
    EFI_STATUS (EFIAPI *locate_handle_buffer)(uint32_t search_type,
                                              struct efi_guid *protocol,
                                              void *search_key,
                                              UINTN *handle_count,
                                              EFI_HANDLE **handles);
    EFI_STATUS (EFIAPI *locate_protocol)(struct efi_guid *protocol,
                                         void *registration, void **interface);
    EFI_STATUS (EFIAPI *install_multiple_protocol_interfaces)(
        EFI_HANDLE *handle, ...);
    EFI_STATUS (EFIAPI *uninstall_multiple_protocol_interfaces)(
        EFI_HANDLE handle, ...);

    EFI_STATUS (EFIAPI *calculate_crc32)(const void *data, UINTN size,
                                         uint32_t *crc32);
    void (EFIAPI *copy_mem)(void *destination, const void *source, UINTN length);
    void (EFIAPI *set_mem)(void *buffer, UINTN size, uint8_t value);
    EFI_STATUS (EFIAPI *create_event_ex)(uint32_t type, EFI_TPL notify_tpl,
                                         void *notify_function,
                                         void *notify_context,
                                         const struct efi_guid *event_group,
                                         EFI_EVENT *event);
};

/* ------------------------------------------------------------------------- */
/* Loaded image, simple file system, graphics output                         */
/* ------------------------------------------------------------------------- */

struct efi_loaded_image {
    uint32_t revision;
    EFI_HANDLE parent_handle;
    struct efi_system_table *system_table;
    EFI_HANDLE device_handle;
    void *file_path;
    void *reserved;
    uint32_t load_options_size;
    void *load_options;
    void *image_base;
    uint64_t image_size;
    EFI_MEMORY_TYPE image_code_type;
    EFI_MEMORY_TYPE image_data_type;
    EFI_STATUS (EFIAPI *unload)(EFI_HANDLE image_handle);
};

typedef struct efi_file efi_file;

struct efi_file {
    uint64_t revision;
    EFI_STATUS (EFIAPI *open)(efi_file *self, efi_file **new_handle,
                              const CHAR16 *file_name, uint64_t open_mode,
                              uint64_t attributes);
    EFI_STATUS (EFIAPI *close)(efi_file *self);
    EFI_STATUS (EFIAPI *delete)(efi_file *self);
    EFI_STATUS (EFIAPI *read)(efi_file *self, UINTN *buffer_size,
                              void *buffer);
    EFI_STATUS (EFIAPI *write)(efi_file *self, UINTN *buffer_size,
                               const void *buffer);
    EFI_STATUS (EFIAPI *get_position)(efi_file *self, uint64_t *position);
    EFI_STATUS (EFIAPI *set_position)(efi_file *self, uint64_t position);
    EFI_STATUS (EFIAPI *get_info)(efi_file *self, struct efi_guid *info_type,
                                  UINTN *buffer_size, void *buffer);
    EFI_STATUS (EFIAPI *set_info)(efi_file *self, struct efi_guid *info_type,
                                  UINTN buffer_size, const void *buffer);
    EFI_STATUS (EFIAPI *flush)(efi_file *self);
};

struct efi_simple_file_system {
    uint64_t revision;
    EFI_STATUS (EFIAPI *open_volume)(struct efi_simple_file_system *self,
                                     efi_file **root);
};

struct efi_file_info {
    uint64_t size;
    uint64_t file_size;
    uint64_t physical_size;
    uint8_t create_time[16];
    uint8_t last_access_time[16];
    uint8_t modification_time[16];
    uint64_t attribute;
    CHAR16 file_name[1];
} PACKED;

struct efi_pixel_bitmask {
    uint32_t red_mask;
    uint32_t green_mask;
    uint32_t blue_mask;
    uint32_t reserved_mask;
} PACKED;

struct efi_gop_mode_info {
    uint32_t version;
    uint32_t horizontal_resolution;
    uint32_t vertical_resolution;
    uint32_t pixel_format;
    struct efi_pixel_bitmask pixel_information;
    uint32_t pixels_per_scan_line;
} PACKED;

enum efi_gop_pixel_format {
    EFI_PIXEL_RGB_RESERVED_8BIT_PER_COLOR = 0,
    EFI_PIXEL_BGR_RESERVED_8BIT_PER_COLOR = 1,
    EFI_PIXEL_BIT_MASK = 2,
    EFI_PIXEL_BLT_ONLY = 3,
};

struct efi_gop_mode {
    uint32_t max_mode;
    uint32_t mode;
    struct efi_gop_mode_info *info;
    UINTN size_of_info;
    EFI_PHYSICAL_ADDRESS frame_buffer_base;
    UINTN frame_buffer_size;
};

struct efi_graphics_output {
    EFI_STATUS (EFIAPI *query_mode)(struct efi_graphics_output *self,
                                    uint32_t mode, UINTN *size_of_info,
                                    struct efi_gop_mode_info **info);
    EFI_STATUS (EFIAPI *set_mode)(struct efi_graphics_output *self,
                                  uint32_t mode);
    EFI_STATUS (EFIAPI *blt)(struct efi_graphics_output *self, void *blt_buffer,
                             uint32_t blt_operation, UINTN source_x,
                             UINTN source_y, UINTN destination_x,
                             UINTN destination_y, UINTN width, UINTN height,
                             UINTN delta);
    struct efi_gop_mode *mode;
};

/* ------------------------------------------------------------------------- */
/* System table and protocol GUIDs                                           */
/* ------------------------------------------------------------------------- */

struct efi_system_table {
    struct efi_table_header hdr;
    CHAR16 *firmware_vendor;
    uint32_t firmware_revision;
    EFI_HANDLE console_in_handle;
    void *con_in;
    EFI_HANDLE console_out_handle;
    struct efi_simple_text_output *con_out;
    EFI_HANDLE standard_error_handle;
    struct efi_simple_text_output *std_err;
    void *runtime_services;
    struct efi_boot_services *boot_services;
    UINTN number_of_table_entries;
    void *configuration_table;
};

#define EFI_LOADED_IMAGE_PROTOCOL_GUID \
    { 0x5B1B31A1, 0x9562, 0x11d2, \
      { 0x8E, 0x3F, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B } }

#define EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID \
    { 0x964E5B22, 0x6459, 0x11d2, \
      { 0x8E, 0x39, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B } }

#define EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID \
    { 0x9042A9DE, 0x23DC, 0x4A38, \
      { 0x96, 0xFB, 0x7A, 0xDE, 0xD0, 0x80, 0x51, 0x6A } }

#define EFI_FILE_INFO_GUID \
    { 0x09576E92, 0x6D3F, 0x11d2, \
      { 0x8E, 0x39, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B } }

#define EFI_FILE_MODE_READ 0x0000000000000001ull

/* Copy an ASCII string into UTF-16 storage. Returns the character count. */
EFI_INLINE UINTN efi_ascii_to_utf16(const char *source, CHAR16 *destination,
                                    UINTN capacity)
{
    UINTN i = 0;

    while (source[i] && i + 1 < capacity) {
        destination[i] = (CHAR16)(uint8_t)source[i];
        i++;
    }
    destination[i] = 0;
    return i;
}

#endif /* ZENKU_EFI_H */
