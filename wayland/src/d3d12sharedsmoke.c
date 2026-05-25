#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define COBJMACROS
#define INITGUID
#include <wsl/winadapter.h>
#include <directx/d3d12.h>
#include <directx/dxgiformat.h>
#undef interface

#include <wayland-client.h>

#include "xdg-shell-client-protocol.h"

#ifndef GENERIC_READ
#define GENERIC_READ 0x80000000L
#endif
#ifndef GENERIC_WRITE
#define GENERIC_WRITE 0x40000000L
#endif
#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0x100000
#endif
#ifndef FB_GPU_DXG_PRESENT_PROV_D3DKMT_HANDLES
#define FB_GPU_DXG_PRESENT_PROV_D3DKMT_HANDLES 0x0004
#endif
#ifndef FB_GPU_DXG_PRESENT_PROV_DIMENSIONS
#define FB_GPU_DXG_PRESENT_PROV_DIMENSIONS 0x0008
#endif
#ifndef FB_GPU_DXG_PRESENT_PROV_ADAPTER_LUID
#define FB_GPU_DXG_PRESENT_PROV_ADAPTER_LUID 0x0010
#endif

typedef uint32_t uint32;
typedef uint64_t uint64;

#define D3DKMT_ADAPTERS_MAX 64
#define D3D12_QAI_WSL_TYPE0_SIZE_ENV "D3D12SHAREDSMOKE_WSL_TYPE0_SIZE"
#define D3D12_QAI_WSL_ADVN_SIZE_ENV "D3D12SHAREDSMOKE_WSL_ADVN_PRIVATE_SIZE"

static uint32_t g_device_admission_wsl_type0_size;
static int g_device_admission_wsl_type0_size_set;

static int parse_runtime_u32(const char *text, uint32_t *value_out);

struct d3dkmthandle {
    union {
        struct {
            uint32 instance : 6;
            uint32 index : 24;
            uint32 unique : 2;
        };
        uint32 v;
    };
};

struct winluid {
    uint32 a;
    uint32 b;
};

static void format_winluid_text(char *buf, size_t buflen,
                                struct winluid luid)
{
    if (!buf || buflen == 0)
        return;
    /*
     * Text diagnostics use the same high:low order as dxgtrace and
     * dxgprobe's WDDM trace comparator. The in-memory UAPI fields remain
     * low/high as winluid.a/winluid.b.
     */
    snprintf(buf, buflen, "%08x:%08x", luid.b, luid.a);
}

struct d3dkmt_adapterinfo {
    struct d3dkmthandle adapter_handle;
    struct winluid adapter_luid;
    uint32 num_sources;
    uint32 present_move_regions_preferred;
};

struct d3dkmt_enumadapters2 {
    uint32 num_adapters;
    uint32 reserved;
    uint64 adapters;
};

struct d3dkmt_closeadapter {
    struct d3dkmthandle adapter_handle;
};

struct d3dkmt_openadapterfromluid {
    struct winluid adapter_luid;
    struct d3dkmthandle adapter_handle;
};

struct d3dkmt_queryadapterinfo {
    struct d3dkmthandle adapter;
    uint32 type;
    uint64 private_data;
    uint32 private_data_size;
};

struct d3dkmt_createdeviceflags {
    uint32 legacy_mode : 1;
    uint32 request_vSync : 1;
    uint32 disable_gpu_timeout : 1;
    uint32 gdi_device : 1;
    uint32 reserved : 28;
};

struct d3dkmt_createdevice {
    struct d3dkmthandle adapter;
    uint32 reserved3;
    struct d3dkmt_createdeviceflags flags;
    struct d3dkmthandle device;
    uint64 command_buffer;
    uint32 command_buffer_size;
    uint32 reserved;
    uint64 allocation_list;
    uint32 allocation_list_size;
    uint32 reserved1;
    uint64 patch_location_list;
    uint32 patch_location_list_size;
    uint32 reserved2;
};

struct d3dkmt_destroydevice {
    struct d3dkmthandle device;
};

struct d3dddi_synchronizationobject_flags {
    union {
        struct {
            uint32 shared : 1;
            uint32 nt_security_sharing : 1;
            uint32 cross_adapter : 1;
            uint32 top_of_pipeline : 1;
            uint32 no_signal : 1;
            uint32 no_wait : 1;
            uint32 no_signal_max_value_on_tdr : 1;
            uint32 no_gpu_access : 1;
            uint32 reserved : 23;
        };
        uint32 value;
    };
};

enum d3dddi_synchronizationobject_type {
    _D3DDDI_MONITORED_FENCE = 5,
};

struct d3dddi_synchronizationobjectinfo2 {
    enum d3dddi_synchronizationobject_type type;
    struct d3dddi_synchronizationobject_flags flags;
    union {
        struct {
            uint64 initial_fence_value;
            uint64 fence_cpu_virtual_address;
            uint64 fence_gpu_virtual_address;
            uint32 engine_affinity;
        } monitored_fence;
        uint64 reserved[8];
    };
    struct d3dkmthandle shared_handle;
};

struct d3dkmt_createsynchronizationobject2 {
    struct d3dkmthandle device;
    uint32 reserved;
    struct d3dddi_synchronizationobjectinfo2 info;
    struct d3dkmthandle sync_object;
    uint32 reserved1;
};

struct d3dkmt_createsyncfile {
    struct d3dkmthandle device;
    struct d3dkmthandle monitored_fence;
    uint64 fence_value;
    uint64 sync_file_handle;
};

struct d3dkmt_destroysynchronizationobject {
    struct d3dkmthandle sync_object;
};

struct d3dkmt_opensyncobjectfromnthandle2 {
    uint64 nt_handle;
    struct d3dkmthandle device;
    struct d3dddi_synchronizationobject_flags flags;
    struct d3dkmthandle sync_object;
    uint32 reserved1;
    union {
        struct {
            uint64 fence_value_cpu_va;
            uint64 fence_value_gpu_va;
            uint32 engine_affinity;
        } monitored_fence;
        uint64 reserved[8];
    };
};

struct d3dkmt_opensyncobjectfromsyncfile {
    uint64 sync_file_handle;
    struct d3dkmthandle device;
    struct d3dkmthandle syncobj;
    uint64 fence_value;
    uint64 fence_value_cpu_va;
    uint64 fence_value_gpu_va;
};

enum d3dkmt_standardallocationtype {
    _D3DKMT_STANDARDALLOCATIONTYPE_EXISTINGHEAP = 1,
    _D3DKMT_STANDARDALLOCATIONTYPE_CROSSADAPTER = 2,
};

struct d3dkmt_standardallocation_existingheap {
    uint64 size;
};

struct d3dkmt_createstandardallocationflags {
    union {
        struct {
            uint32 reserved : 32;
        };
        uint32 value;
    };
};

struct d3dkmt_createstandardallocation {
    enum d3dkmt_standardallocationtype type;
    uint32 reserved;
    struct d3dkmt_standardallocation_existingheap existing_heap_data;
    struct d3dkmt_createstandardallocationflags flags;
    uint32 reserved1;
};

struct d3dddi_allocationinfo2 {
    struct d3dkmthandle allocation;
    uint64 sysmem;
    uint64 priv_drv_data;
    uint32 priv_drv_data_size;
    uint32 vidpn_source_id;
    union {
        struct {
            uint32 primary : 1;
            uint32 stereo : 1;
            uint32 override_priority : 1;
            uint32 reserved : 29;
        };
        uint32 value;
    } flags;
    uint64 gpu_virtual_address;
    union {
        uint32 priority;
        uint64 unused;
    };
    uint64 reserved[5];
};

struct d3dkmt_createallocationflags {
    union {
        struct {
            uint32 create_resource : 1;
            uint32 create_shared : 1;
            uint32 non_secure : 1;
            uint32 create_protected : 1;
            uint32 restrict_shared_access : 1;
            uint32 existing_sysmem : 1;
            uint32 nt_security_sharing : 1;
            uint32 read_only : 1;
            uint32 create_write_combined : 1;
            uint32 create_cached : 1;
            uint32 swap_chain_back_buffer : 1;
            uint32 cross_adapter : 1;
            uint32 open_cross_adapter : 1;
            uint32 partial_shared_creation : 1;
            uint32 zeroed : 1;
            uint32 write_watch : 1;
            uint32 standard_allocation : 1;
            uint32 existing_section : 1;
            uint32 reserved : 14;
        };
        uint32 value;
    };
};

struct d3dkmt_createallocation {
    struct d3dkmthandle device;
    struct d3dkmthandle resource;
    struct d3dkmthandle global_share;
    uint32 reserved;
    uint64 private_runtime_data;
    uint32 private_runtime_data_size;
    uint32 reserved1;
    union {
        uint64 standard_allocation;
        uint64 priv_drv_data;
    };
    uint32 priv_drv_data_size;
    uint32 alloc_count;
    uint64 allocation_info;
    struct d3dkmt_createallocationflags flags;
    uint32 reserved2;
    uint64 private_runtime_resource_handle;
};

struct d3dddicb_destroyallocation2flags {
    union {
        struct {
            uint32 assume_not_in_use : 1;
            uint32 synchronous_destroy : 1;
            uint32 reserved : 29;
            uint32 system_use_only : 1;
        };
        uint32 value;
    };
};

struct d3dkmt_destroyallocation2 {
    struct d3dkmthandle device;
    struct d3dkmthandle resource;
    uint64 allocations;
    uint32 alloc_count;
    struct d3dddicb_destroyallocation2flags flags;
};

struct d3dkmt_queryresourceinfofromnthandle {
    struct d3dkmthandle device;
    uint32 reserved;
    uint64 nt_handle;
    uint64 private_runtime_data;
    uint32 private_runtime_data_size;
    uint32 total_priv_drv_data_size;
    uint32 resource_priv_drv_data_size;
    uint32 allocation_count;
};

struct d3dddi_openallocationinfo2 {
    struct d3dkmthandle allocation;
    uint64 priv_drv_data;
    uint32 priv_drv_data_size;
    uint64 gpu_va;
    uint64 reserved[6];
};

struct d3dkmt_openresourcefromnthandle {
    struct d3dkmthandle device;
    uint32 reserved;
    uint64 nt_handle;
    uint32 allocation_count;
    uint32 reserved1;
    uint64 open_alloc_info;
    int private_runtime_data_size;
    uint32 reserved2;
    uint64 private_runtime_data;
    uint32 resource_priv_drv_data_size;
    uint32 reserved3;
    uint64 resource_priv_drv_data;
    uint32 total_priv_drv_data_size;
    uint64 total_priv_drv_data;
    struct d3dkmthandle resource;
    struct d3dkmthandle keyed_mutex;
    uint64 keyed_mutex_private_data;
    uint32 keyed_mutex_private_data_size;
    struct d3dkmthandle sync_object;
};

struct d3dkmt_shareobjects {
    uint32 object_count;
    uint32 reserved;
    uint64 objects;
    uint64 object_attr;
    uint32 desired_access;
    uint32 reserved1;
    uint64 shared_handle;
};

#define LX_DXOPENADAPTERFROMLUID \
    _IOWR(0x47, 0x01, struct d3dkmt_openadapterfromluid)
#define LX_DXCREATEDEVICE \
    _IOWR(0x47, 0x02, struct d3dkmt_createdevice)
#define LX_DXCREATEALLOCATION \
    _IOWR(0x47, 0x06, struct d3dkmt_createallocation)
#define LX_DXQUERYADAPTERINFO \
    _IOWR(0x47, 0x09, struct d3dkmt_queryadapterinfo)
#define LX_DXENUMADAPTERS2 \
    _IOWR(0x47, 0x14, struct d3dkmt_enumadapters2)
#define LX_DXCLOSEADAPTER \
    _IOWR(0x47, 0x15, struct d3dkmt_closeadapter)
#define LX_DXDESTROYDEVICE \
    _IOWR(0x47, 0x19, struct d3dkmt_destroydevice)
#define LX_DXCREATESYNCHRONIZATIONOBJECT \
    _IOWR(0x47, 0x10, struct d3dkmt_createsynchronizationobject2)
#define LX_DXDESTROYSYNCHRONIZATIONOBJECT \
    _IOWR(0x47, 0x1d, struct d3dkmt_destroysynchronizationobject)
#define LX_DXDESTROYALLOCATION2 \
    _IOWR(0x47, 0x13, struct d3dkmt_destroyallocation2)
#define LX_DXSHAREOBJECTS \
    _IOWR(0x47, 0x3f, struct d3dkmt_shareobjects)
#define LX_DXOPENSYNCOBJECTFROMNTHANDLE2 \
    _IOWR(0x47, 0x40, struct d3dkmt_opensyncobjectfromnthandle2)
#define LX_DXCREATESYNCFILE \
    _IOWR(0x47, 0x45, struct d3dkmt_createsyncfile)
#define LX_DXOPENSYNCOBJECTFROMSYNCFILE \
    _IOWR(0x47, 0x47, struct d3dkmt_opensyncobjectfromsyncfile)
#define LX_DXQUERYRESOURCEINFOFROMNTHANDLE \
    _IOWR(0x47, 0x41, struct d3dkmt_queryresourceinfofromnthandle)
#define LX_DXOPENRESOURCEFROMNTHANDLE \
    _IOWR(0x47, 0x42, struct d3dkmt_openresourcefromnthandle)

struct app {
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_compositor *compositor;
    struct xdg_wm_base *wm_base;
    struct wl_proxy *gpu_manager;
    struct wl_surface *surface;
    struct xdg_surface *xdg_surface;
    struct xdg_toplevel *toplevel;
    struct wl_buffer *buffer;
    struct wl_callback *frame_callback;
    int configured;
    int frame_seen;
    int release_seen;
    int running;
    uint32_t gpu_manager_version;
};

struct d3d12_runtime {
    ID3D12Device *device;
    ID3D12CommandQueue *queue;
    ID3D12CommandAllocator *allocator;
    ID3D12GraphicsCommandList *command_list;
    ID3D12DescriptorHeap *rtv_heap;
    ID3D12Heap *heap;
    ID3D12Heap *opened_heap;
    ID3D12Resource *resource;
    ID3D12Resource *opened_resource;
    ID3D12Fence *fence;
    ID3D12Fence *opened_fence;
    HANDLE heap_handle;
    HANDLE resource_handle;
    HANDLE fence_handle;
    ID3D12Device *import_device;
    struct winluid adapter_luid;
    uint64_t fence_value;
    D3D12_FENCE_FLAGS diag_fence_flags;
    D3D12_RESOURCE_FLAGS diag_resource_flags;
    D3D12_HEAP_FLAGS diag_heap_flags;
    D3D12_RESOURCE_STATES diag_initial_state;
    int diag_app_sync_suppressed;
    int diag_fence_dup_no_cloexec;
    const char *diag_adapter_path;
};

struct d3d12_runtime_options {
    int render_target;
    int simultaneous;
    int touch_before_export;
    int initial_common;
    int clear_alpha_only;
    int omit_clear_value;
    int custom_heap_props;
    int skip_prealloc_info;
    int wsl_adapter_list;
    int precheck_dxg_luid;
    int fallback_dxg_luid_on_empty_wsl_list;
    int preexport_diagnostics;
    int reserve_low_va;
    int suppress_app_sync;
    int make_resident_before_export;
    int signal_before_export;
    int name_objects;
    int security_attrs;
    int placed_resource;
    int resource_cross_adapter;
    int export_heap_first;
    int heap_only;
    int wsl_parity;
    int export_fence_first;
    int fence_only;
    int fence_cross_adapter;
    int resource_only;
    int named_resource_handle;
    int named_fence_handle;
    int wsl_resource_shape;
    int wsl_resource_shape_direct;
    int wsl_resource_shape_shared_heap;
    int wsl_resource_shape_shared_heap_export_only;
    int wsl_success_shape_wsl_list_default_export_only;
    int wsl_success_shape_no_clear_value_export_only;
    int wsl_success_shape_initial_rt_export_only;
    int wsl_success_shape_reserve_low_va_export_only;
    int wsl_success_shape_resource_cross_adapter_export_only;
    int wsl_success_shape_prealloc_info_export_only;
    int wsl_resource_shape_no_heap_flags_export_only;
    int wsl_resource_shape_placed_shared_heap_export_only;
    int wsl_resource_shape_app_sync_export_only;
    int runtime_export_only;
    int runtime_import_contract;
    int runtime_import_independent_device;
    int runtime_import_fence_first;
    int runtime_import_fence_dup_no_cloexec;
    int runtime_import_fence_cross_adapter;
    int runtime_dxg_syncfile_acquire;
    int zero_heap_flags;
    int share_access_override;
    int direct_dxg_fence_runtime_device_override;
    int direct_dxg_fence_flags_override;
    uint32_t share_access;
    uint32_t direct_dxg_fence_runtime_device;
    uint32_t direct_dxg_fence_flags;
    const char *share_access_name;
    uint32_t width;
    uint32_t height;
};

static D3D12_HEAP_PROPERTIES
d3d12_default_heap_properties(ID3D12Device *device, int custom_heap_props)
{
    D3D12_HEAP_PROPERTIES props;

    if (custom_heap_props) {
#if !defined(_WIN32)
        return ID3D12Device_GetCustomHeapProperties(
            device, 0, D3D12_HEAP_TYPE_DEFAULT);
#else
        ID3D12Device_GetCustomHeapProperties(
            device, &props, 0, D3D12_HEAP_TYPE_DEFAULT);
        return props;
#endif
    }

    memset(&props, 0, sizeof(props));
    props.Type = D3D12_HEAP_TYPE_DEFAULT;
    props.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    props.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    props.CreationNodeMask = 1;
    props.VisibleNodeMask = 1;
    return props;
}

static uintptr_t align_up_uintptr(uintptr_t value, uintptr_t alignment)
{
    return (value + alignment - 1) & ~(alignment - 1);
}

static int handle_to_fd(HANDLE handle);

static void d3d12_runtime_reserve_low_va(void)
{
    const uintptr_t reserve_start = 0x10000000UL;
    const uintptr_t reserve_end = 0x100000000ULL;
    const size_t reserve_chunk = 16UL * 1024UL * 1024UL;
    long page_size_long = sysconf(_SC_PAGESIZE);
    uintptr_t page_size = page_size_long > 0 ? (uintptr_t)page_size_long : 4096;
    uintptr_t brk_addr = (uintptr_t)sbrk(0);
    uintptr_t brk_guard_addr = align_up_uintptr(brk_addr, page_size);
    void *brk_guard;
    size_t attempts = 0;
    size_t reserved = 0;
    size_t exists = 0;
    size_t failed = 0;
    int brk_guard_errno = 0;

    brk_guard = mmap((void *)brk_guard_addr, page_size, PROT_NONE,
                     MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE,
                     -1, 0);
    if (brk_guard == MAP_FAILED)
        brk_guard_errno = errno;

    for (uintptr_t addr = reserve_start; addr < reserve_end;
         addr += reserve_chunk) {
        void *mapped;

        attempts++;
        mapped = mmap((void *)addr, reserve_chunk, PROT_NONE,
                      MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE,
                      -1, 0);
        if (mapped != MAP_FAILED) {
            reserved += reserve_chunk;
        } else if (errno == EEXIST) {
            exists++;
        } else {
            failed++;
        }
    }

    printf("d3d12sharedsmoke: runtime low-va reserve brk=0x%lx brk_guard=0x%lx/%s brk_errno=%d range=0x%lx-0x%lx chunk=%lu attempts=%lu reserved=%luMiB exists=%lu failed=%lu\n",
           (unsigned long)brk_addr, (unsigned long)brk_guard_addr,
           brk_guard != MAP_FAILED ? "ok" : "fail", brk_guard_errno,
           (unsigned long)reserve_start, (unsigned long)reserve_end,
           (unsigned long)reserve_chunk, (unsigned long)attempts,
           (unsigned long)(reserved >> 20), (unsigned long)exists,
           (unsigned long)failed);
}

static void d3d12_print_compact_bytes(const char *case_label,
                                      const char *blob_label,
                                      size_t offset, const void *data,
                                      size_t size, size_t max_size)
{
    const unsigned char *bytes = (const unsigned char *)data;
    size_t count = size < max_size ? size : max_size;

    printf("d3d12sharedsmoke: runtime private-bytes label=%s blob=%s offset=0x%lx size=%lu bytes=",
           case_label ? case_label : "runtime_export",
           blob_label ? blob_label : "unknown", (unsigned long)offset,
           (unsigned long)size);
    for (size_t i = 0; i < count; i++)
        printf("%02x", bytes[i]);
    if (count < size)
        printf("...");
    printf("\n");
}

static void d3d12_print_handle_fd_diag(const char *label, const char *kind,
                                       HANDLE handle)
{
    int fd = handle_to_fd(handle);
    int fcntl_errno = 0;
    int fcntlfl_errno = 0;
    int fstat_errno = 0;
    int readlink_errno = 0;
    int fd_flags = -1;
    int fd_status_flags = -1;
    int readlink_ok = 0;
    char fd_path[64];
    char fd_target[256];
    const char *fd_dxg_hint = "not-char";
    struct stat st;
    int fstat_ok = 0;

    memset(&st, 0, sizeof(st));
    memset(fd_path, 0, sizeof(fd_path));
    memset(fd_target, 0, sizeof(fd_target));
    if (fd >= 0) {
        ssize_t link_len;

        fd_flags = fcntl(fd, F_GETFD);
        if (fd_flags < 0)
            fcntl_errno = errno;
        fd_status_flags = fcntl(fd, F_GETFL);
        if (fd_status_flags < 0)
            fcntlfl_errno = errno;
        if (fstat(fd, &st) == 0) {
            fstat_ok = 1;
        } else {
            fstat_errno = errno;
        }
        snprintf(fd_path, sizeof(fd_path), "/proc/self/fd/%d", fd);
        link_len = readlink(fd_path, fd_target, sizeof(fd_target) - 1);
        if (link_len >= 0) {
            fd_target[link_len] = '\0';
            readlink_ok = 1;
        } else {
            readlink_errno = errno;
        }
    }
    if (readlink_ok && strstr(fd_target, "dxgsyncobj")) {
        fd_dxg_hint = "anon-inode-dxgsyncobj";
    } else if (readlink_ok && strstr(fd_target, "dxgresource")) {
        fd_dxg_hint = "anon-inode-dxgresource";
    } else if (readlink_ok && strstr(fd_target, "anon_inode:dxg")) {
        fd_dxg_hint = "anon-inode-dxg";
    } else if (readlink_ok && strstr(fd_target, "/dev/dxg")) {
        fd_dxg_hint = "dev-dxg";
    } else if (fstat_ok && S_ISCHR(st.st_mode)) {
        fd_dxg_hint = "char-device";
    }
    printf("d3d12sharedsmoke: runtime handle-fd-diag label=%s kind=%s handle=%p fd=%d fd_fstatable=%u fd_dxg_hint=%s fcntl_flags=0x%x fcntl_errno=%d fcntl_status_flags=0x%x fcntl_status_errno=%d fstat_errno=%d mode=0%lo is_chr=%u dev=%lu ino=%lu fd_path=%s readlink_ok=%u readlink_errno=%d readlink_target=%s\n",
           label ? label : "runtime",
           kind ? kind : "unknown", handle, fd, fstat_ok,
           fd_dxg_hint, fd_flags, fcntl_errno, fd_status_flags,
           fcntlfl_errno,
           fstat_errno, fstat_ok ? (unsigned long)st.st_mode : 0,
           fstat_ok ? S_ISCHR(st.st_mode) : 0,
           fstat_ok ? (unsigned long)st.st_dev : 0,
           fstat_ok ? (unsigned long)st.st_ino : 0,
           fd >= 0 ? fd_path : "none", readlink_ok, readlink_errno,
           readlink_ok ? fd_target : "unavailable");
}

struct present_evidence {
    char run_id[128];
    char present_identity_compositor_run_id[128];
    char evidence_stage[64];
    char display_target_kind[64];
    char final_handoff_lane[96];
    char final_handoff_selected[96];
    char final_handoff_source[96];
    char final_handoff_intermediate[96];
    char final_handoff_destination[96];
    char wslg_user_display_transport[96];
    char wslg_user_display_helper_path[128];
    char wslg_user_display_dependency[128];
    char present_source_query_skipped_reason[96];
    int64_t mtime_ms;
    uint64_t counter;
    uint64_t starts;
    uint64_t copy_completes;
    uint64_t completes;
    uint64_t client_pid;
    uint64_t client_buffer_id;
    uint64_t manager_resource_id;
    uint64_t buffer_generation;
    uint64_t native_present_attempt_id;
    uint64_t native_present_completion_id;
    uint64_t client_native_present_attempts;
    uint64_t client_native_present_completions;
    uint64_t client_native_present_rejects;
    uint64_t resource_native_present_attempts;
    uint64_t resource_native_present_completions;
    uint64_t resource_native_present_rejects;
    uint64_t resource_generation_counter;
    uint64_t resource_generation_native_present_attempts;
    uint64_t resource_generation_native_present_completions;
    uint64_t resource_generation_native_present_rejects;
    uint64_t identity_counters_match_resource;
    uint64_t present_identity_client_pid;
    uint64_t present_identity_client_buffer_id;
    uint64_t present_identity_manager_resource_id;
    uint64_t present_identity_buffer_generation;
    uint64_t present_identity_attempt_id;
    uint64_t present_identity_completion_id;
    uint64_t present_identity_current_run_valid;
    uint64_t callback_release_same_frame_required;
    uint64_t resource;
    uint64_t allocation_count;
    uint64_t fence;
    uint64_t fence_target;
    uint64_t release_fence;
    uint64_t format;
    uint64_t display_handoff_implemented;
    uint64_t display_handoff_requires_kernel_host_protocol;
    uint64_t display_target_requires_kernel_host_protocol;
    uint64_t display_completion_correlated;
    uint64_t native_present_requirements_satisfied;
    uint64_t display_target_kind_raw;
    uint64_t display_target_kernel_reported;
    uint64_t runtime_created_d3d12_resource_present;
    uint64_t synthvid_sysmem_fallback_present;
    uint64_t existing_sysmem_is_d3d12_com_resource;
    uint64_t synthvid_sysmem_satisfies_native_present;
    uint64_t synthvid_dirty_rect_count;
    uint64_t synthvid_dirty_sequence;
    uint64_t dirty_rect_display_completion_correlated;
    uint64_t present_sequence_cpu_map_rejects_delta;
    uint64_t present_sequence_cpu_plane_rejects_delta;
    uint64_t no_cpu_map_no_readback_confirmed;
    uint64_t present_sequence_framebuffer_blit_used;
    uint64_t present_sequence_cpu_map_used;
    uint64_t present_sequence_cpu_readback_used;
    uint64_t present_sequence_cpu_copy_used;
    uint64_t present_sequence_software_dri_used;
    uint64_t software_dri_present_used;
    uint64_t framebuffer_blit_only;
    uint64_t phase2_bad_luid_rejected;
    uint64_t phase2_wrong_dimensions_rejected;
    uint64_t phase2_wrong_format_rejected;
    uint64_t phase2_missing_resource_fd_rejected;
    uint64_t phase2_missing_fence_fd_rejected;
    uint64_t phase2_stale_fence_rejected;
    uint64_t phase2_cpu_mappable_fallback_rejected;
    uint64_t phase2_import_only_satisfies_native_present;
    uint64_t phase2_open_only_satisfies_native_present;
    uint64_t phase2_gpu_copy_only_satisfies_native_present;
    uint64_t final_handoff_user_display_channel_selected;
    uint64_t wslg_user_display_lane_considered;
    uint64_t wslg_user_display_lane_selected;
    uint64_t wslg_user_display_channel_available;
    uint64_t wslg_user_display_host_ack_required;
    uint64_t wslg_user_display_host_ack_observed;
    uint64_t wslg_user_display_success;
    uint64_t final_handoff_existing_sysmem_allowed;
    uint64_t final_handoff_synthvid_dirty_only_allowed;
    uint64_t final_handoff_runtime_resource_required;
    uint64_t final_handoff_runtime_resource_observed;
    uint64_t final_handoff_kernel_abi_required;
    uint64_t final_handoff_kernel_abi_missing;
    uint64_t final_handoff_host_display_commit_success;
    uint64_t final_handoff_present_id;
    uint64_t final_handoff_completed;
    uint64_t final_handoff_dirty_rects;
    uint64_t final_handoff_dirty_sequence;
    uint64_t final_handoff_completion_correlated;
    uint64_t final_handoff_no_cpu_map_no_readback;
    uint64_t final_handoff_release_fence;
    uint64_t final_handoff_success;
    uint64_t dxg_present_source_register_attempts;
    uint64_t dxg_present_source_register_successes;
    uint64_t dxg_present_source_register_errno;
    uint64_t dxg_present_source;
    uint64_t dxg_present_source_commit_attempts;
    uint64_t dxg_present_source_commit_successes;
    uint64_t dxg_present_source_commit_errno;
    uint64_t dxg_present_source_commit_status;
    uint64_t dxg_present_source_commit_expected_eopnotsupp;
    uint64_t dxg_present_id;
    uint64_t dxg_present_completed;
    uint64_t buffer_present_source_register_attempts;
    uint64_t buffer_present_source_register_successes;
    uint64_t buffer_present_source_register_errno;
    uint64_t buffer_present_source;
    uint64_t buffer_present_source_commit_attempts;
    uint64_t buffer_present_source_commit_successes;
    uint64_t buffer_present_source_commit_errno;
    uint64_t buffer_present_source_commit_status;
    uint64_t buffer_present_source_commit_expected_eopnotsupp;
    uint64_t buffer_present_source_present_id;
    uint64_t buffer_present_source_completed;
    uint64_t buffer_present_source_query_attempts;
    uint64_t buffer_present_source_query_successes;
    uint64_t buffer_present_source_query_errno;
    uint64_t buffer_present_source_query_required;
    uint64_t buffer_present_source_query_skipped_commit_failed;
    uint64_t buffer_present_source_query_skipped_no_present_id;
    uint64_t buffer_present_source_query_attempted_after_commit_success;
    uint64_t buffer_present_source_query_kernel_missing;
    uint64_t present_source_registered;
    uint64_t present_source_query_attempted;
    uint64_t present_source_gpu_p_or_dda_transport_absent;
    uint64_t present_source_commit_rejected_eopnotsupp;
    uint64_t present_source_no_present_id_completed;
    uint64_t present_source_no_gpu_p_or_dda_display_bind;
    uint64_t present_source_no_display_handoff;
    uint64_t present_source_no_present_completion;
    uint64_t present_source_same_frame_callbacks_blocked;
    uint64_t present_source_same_frame_releases_blocked;
    uint64_t present_source_callback_blocked;
    uint64_t present_source_release_blocked;
    uint64_t present_source_adapter_luid_low;
    uint64_t present_source_adapter_luid_high;
    uint64_t present_source_provenance_flags;
    uint64_t present_source_register_flags;
    uint64_t descriptor_width;
    uint64_t descriptor_height;
    uint64_t descriptor_pitch;
    uint64_t descriptor_modifier;
    uint64_t descriptor_sample_count;
    uint64_t descriptor_dxg_fd;
    uint64_t descriptor_resource_fd;
    uint64_t descriptor_nt_shared_fd;
    uint64_t descriptor_device;
    uint64_t descriptor_resource;
    uint64_t descriptor_allocation0;
    uint64_t descriptor_allocation_count;
    uint64_t descriptor_format;
    uint64_t descriptor_total_private_size;
    uint64_t descriptor_matches_gpu_copy_resource;
    char descriptor_layout[32];
    char descriptor_luid[32];
    uint64_t buffer_present_source_completion_correlated;
    uint64_t buffer_release_observed;
    uint64_t buffer_release_resource;
    uint64_t buffer_release_present_sequence;
    uint64_t buffer_release_buffer_generation;
    uint64_t buffer_release_attempt_id;
    uint64_t buffer_release_completion_id;
    uint64_t buffer_release_present_id;
    uint64_t buffer_release_same_resource;
    uint64_t buffer_release_same_generation;
    uint64_t buffer_release_same_attempt;
    uint64_t buffer_release_same_present_id;
    uint64_t buffer_release_native_successes;
    uint64_t buffer_release_failclosed_unblocks;
    uint64_t frame_callback_observed;
    uint64_t frame_callback_resource;
    uint64_t frame_callback_present_sequence;
    uint64_t frame_callback_buffer_generation;
    uint64_t frame_callback_attempt_id;
    uint64_t frame_callback_completion_id;
    uint64_t frame_callback_present_id;
    uint64_t frame_callback_same_resource;
    uint64_t frame_callback_same_generation;
    uint64_t frame_callback_same_attempt;
    uint64_t frame_callback_same_present_id;
    uint64_t frame_callback_native_successes;
    uint64_t frame_callback_failclosed_unblocks;
    uint64_t callback_release_same_frame_observed;
    uint64_t callbacks_blocked;
    uint64_t releases_blocked;
    uint64_t failclosed_client_unblock_enabled;
    uint64_t failclosed_client_unblocked;
    uint64_t failclosed_client_unblock_no_native_present_credit;
    uint64_t failclosed_client_unblock_resource;
    uint64_t failclosed_client_unblock_sequence;
    uint64_t failclosed_client_unblock_buffer_generation;
    uint64_t failclosed_client_unblock_attempt_id;
    uint64_t failclosed_client_unblock_releases;
    uint64_t failclosed_client_unblock_callbacks;
    uint64_t cpu_readback;
    uint64_t cpu_mapping;
    uint64_t cpu_copy;
    struct winluid source_luid;
    struct winluid matched_luid;
    int found;
    int native_path;
    int rejected;
};

struct xv6_dxcore_adapter;
struct xv6_dxcore_adapter_factory;

struct xv6_dxcore_adapter_vtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(struct xv6_dxcore_adapter *,
                                                REFIID, void **);
    ULONG (STDMETHODCALLTYPE *AddRef)(struct xv6_dxcore_adapter *);
    ULONG (STDMETHODCALLTYPE *Release)(struct xv6_dxcore_adapter *);
    int (STDMETHODCALLTYPE *IsValid)(struct xv6_dxcore_adapter *);
    int (STDMETHODCALLTYPE *IsAttributeSupported)(struct xv6_dxcore_adapter *,
                                                  REFGUID);
    int (STDMETHODCALLTYPE *IsPropertySupported)(struct xv6_dxcore_adapter *,
                                                 uint32_t);
    HRESULT (STDMETHODCALLTYPE *GetProperty)(struct xv6_dxcore_adapter *,
                                             uint32_t, size_t, void *);
};

struct xv6_dxcore_adapter {
    const struct xv6_dxcore_adapter_vtbl *lpVtbl;
};

struct xv6_dxcore_adapter_list;

struct xv6_dxcore_adapter_list_vtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(
        struct xv6_dxcore_adapter_list *, REFIID, void **);
    ULONG (STDMETHODCALLTYPE *AddRef)(struct xv6_dxcore_adapter_list *);
    ULONG (STDMETHODCALLTYPE *Release)(struct xv6_dxcore_adapter_list *);
    HRESULT (STDMETHODCALLTYPE *GetAdapter)(struct xv6_dxcore_adapter_list *,
                                            uint32_t, REFIID, void **);
    uint32_t (STDMETHODCALLTYPE *GetAdapterCount)(
        struct xv6_dxcore_adapter_list *);
};

struct xv6_dxcore_adapter_list {
    const struct xv6_dxcore_adapter_list_vtbl *lpVtbl;
};

struct xv6_dxcore_adapter_factory_vtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(
        struct xv6_dxcore_adapter_factory *, REFIID, void **);
    ULONG (STDMETHODCALLTYPE *AddRef)(struct xv6_dxcore_adapter_factory *);
    ULONG (STDMETHODCALLTYPE *Release)(struct xv6_dxcore_adapter_factory *);
    HRESULT (STDMETHODCALLTYPE *CreateAdapterList)(
        struct xv6_dxcore_adapter_factory *, uint32_t, const GUID *, REFIID,
        void **);
    HRESULT (STDMETHODCALLTYPE *GetAdapterByLuid)(
        struct xv6_dxcore_adapter_factory *, const LUID *, REFIID, void **);
};

struct xv6_dxcore_adapter_factory {
    const struct xv6_dxcore_adapter_factory_vtbl *lpVtbl;
};

struct xv6_dxcore_hardware_id {
    uint32_t vendor_id;
    uint32_t device_id;
    uint32_t subsystem_id;
    uint32_t revision;
};

#define XV6_DXCORE_ADAPTER_PROPERTY_INSTANCE_LUID 0
#define XV6_DXCORE_ADAPTER_PROPERTY_HARDWARE_ID 3

static const GUID XV6_IID_IDXCoreAdapterFactory = {
    0x78ee5945, 0xc36e, 0x4b13,
    { 0xa6, 0x69, 0x00, 0x5d, 0xd1, 0x1c, 0x0f, 0x06 }
};

static const GUID XV6_IID_IDXCoreAdapterList = {
    0x526c7776, 0x40e9, 0x459b,
    { 0xb7, 0x11, 0xf3, 0x2a, 0xd7, 0x6d, 0xfc, 0x28 }
};

static const GUID XV6_IID_IDXCoreAdapter = {
    0xf0db4c7f, 0xfe5a, 0x42a2,
    { 0xbd, 0x62, 0xf2, 0xa6, 0xcf, 0x6f, 0xc8, 0x3e }
};

static const GUID XV6_DXCORE_ADAPTER_ATTRIBUTE_D3D12_GRAPHICS = {
    0x0c9ece4d, 0x2f6e, 0x4f01,
    { 0x8c, 0x96, 0xe8, 0x9e, 0x33, 0x1b, 0x47, 0xb1 }
};

HRESULT WINAPI DXCoreCreateAdapterFactory(REFIID riid, void **ppvFactory);

static const struct wl_interface *xv6_gpu_buffer_create_types[] = {
    &wl_buffer_interface, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL,
};

static const struct wl_message xv6_gpu_buffer_manager_requests[] = {
    { "create_buffer", "nuiiiu", xv6_gpu_buffer_create_types },
    { "create_buffer_with_fence", "nuiiiuh", xv6_gpu_buffer_create_types },
    { "create_d3d12_resource_buffer", "nhiiuuu",
      xv6_gpu_buffer_create_types },
    { "create_d3d12_resource_buffer_with_fence", "nhhiiuuu",
      xv6_gpu_buffer_create_types },
    { "create_d3d12_resource_buffer_luid", "nhuuiiuuu",
      xv6_gpu_buffer_create_types },
    { "create_d3d12_resource_buffer_with_fence_luid", "nhhuuiiuuu",
      xv6_gpu_buffer_create_types },
    { "create_d3d12_resource_buffer_with_fence_value_luid",
      "nhhuuuuiiuuu", xv6_gpu_buffer_create_types },
    { "create_d3d12_resource_buffer_with_fence_value_luid_run_id",
      "nhhuuuuiiuuus", xv6_gpu_buffer_create_types },
};

static const struct wl_interface xv6_gpu_buffer_manager_interface = {
    "xv6_gpu_buffer_manager",
    6,
    8,
    xv6_gpu_buffer_manager_requests,
    0,
    NULL,
};

static const char *d3d12_validation_run_id(void)
{
    const char *run_id = getenv("XV6_GPU_VALIDATE_RUN_ID");

    if (!run_id || !*run_id)
        run_id = getenv("XV6_WLCOMP_D3D12_RUN_ID");
    return (run_id && *run_id) ? run_id : "";
}

static struct wl_buffer *
xv6_gpu_buffer_manager_create_d3d12_resource_buffer_with_fence(
    struct wl_proxy *manager,
    int resource_fd,
    int fence_fd,
    struct winluid adapter_luid,
    int32_t width,
    int32_t height,
    uint32_t format,
    uint32_t allocation_count,
    uint32_t total_priv_size,
    uint64_t fence_value)
{
    if (wl_proxy_get_version(manager) >= 6) {
        return (struct wl_buffer *)wl_proxy_marshal_flags(
            manager, 7, &wl_buffer_interface, wl_proxy_get_version(manager),
            0, NULL, resource_fd, fence_fd, (uint32_t)fence_value,
            (uint32_t)(fence_value >> 32), adapter_luid.a, adapter_luid.b,
            width, height, format, allocation_count, total_priv_size,
            d3d12_validation_run_id());
    }
    if (wl_proxy_get_version(manager) >= 5) {
        return (struct wl_buffer *)wl_proxy_marshal_flags(
            manager, 6, &wl_buffer_interface, wl_proxy_get_version(manager),
            0, NULL, resource_fd, fence_fd, (uint32_t)fence_value,
            (uint32_t)(fence_value >> 32), adapter_luid.a, adapter_luid.b,
            width, height, format, allocation_count, total_priv_size);
    }
    if (wl_proxy_get_version(manager) >= 4) {
        return (struct wl_buffer *)wl_proxy_marshal_flags(
            manager, 5, &wl_buffer_interface, wl_proxy_get_version(manager),
            0, NULL, resource_fd, fence_fd, adapter_luid.a, adapter_luid.b,
            width, height, format, allocation_count, total_priv_size);
    }
    return (struct wl_buffer *)wl_proxy_marshal_flags(
        manager, 3, &wl_buffer_interface, wl_proxy_get_version(manager), 0,
        NULL, resource_fd, fence_fd, width, height, format,
        allocation_count, total_priv_size);
}

static struct wl_buffer *
xv6_gpu_buffer_manager_create_d3d12_resource_buffer_luid(
    struct wl_proxy *manager,
    int resource_fd,
    struct winluid adapter_luid,
    int32_t width,
    int32_t height,
    uint32_t format,
    uint32_t allocation_count,
    uint32_t total_priv_size)
{
    if (wl_proxy_get_version(manager) >= 4) {
        return (struct wl_buffer *)wl_proxy_marshal_flags(
            manager, 4, &wl_buffer_interface, wl_proxy_get_version(manager),
            0, NULL, resource_fd, adapter_luid.a, adapter_luid.b,
            width, height, format, allocation_count, total_priv_size);
    }
    return (struct wl_buffer *)wl_proxy_marshal_flags(
        manager, 2, &wl_buffer_interface, wl_proxy_get_version(manager), 0,
        NULL, resource_fd, width, height, format, allocation_count,
        total_priv_size);
}

static void timeout_handler(int sig)
{
    (void)sig;
    fprintf(stderr, "d3d12sharedsmoke: timeout\n");
    _exit(2);
}

static int64_t now_ms(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        return 0;
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static int handle_to_fd(HANDLE handle)
{
    return (int)(intptr_t)handle;
}

static void close_handle_fd(HANDLE *handle)
{
    int fd;

    if (!handle || !*handle)
        return;
    fd = handle_to_fd(*handle);
    if (fd >= 0)
        close(fd);
    *handle = NULL;
}

static int parse_counter_token(const char *buf, const char *key,
                               uint64_t *value_out)
{
    size_t key_len = strlen(key);
    const char *p = buf;
    int found = 0;

    while ((p = strstr(p, key)) != NULL) {
        char before = p == buf ? ' ' : p[-1];
        char *end;
        uint64_t value;

        if ((before >= 'a' && before <= 'z') ||
            (before >= 'A' && before <= 'Z') ||
            (before >= '0' && before <= '9') || before == '_') {
            p += key_len;
            continue;
        }
        p += key_len;
        if (*p != '=' && *p != ':')
            continue;
        p++;
        value = strtoull(p, &end, 0);
        if (end == p)
            continue;
        if (!found || value > *value_out)
            *value_out = value;
        found = 1;
    }
    return found;
}

static int parse_string_token(const char *buf, const char *key,
                              char *value_out, size_t value_size)
{
    size_t key_len = strlen(key);
    const char *p = buf;

    if (!value_out || value_size == 0)
        return 0;
    while ((p = strstr(p, key)) != NULL) {
        char before = p == buf ? ' ' : p[-1];
        const char *end;
        size_t len;

        if ((before >= 'a' && before <= 'z') ||
            (before >= 'A' && before <= 'Z') ||
            (before >= '0' && before <= '9') || before == '_') {
            p += key_len;
            continue;
        }
        p += key_len;
        if (*p != '=' && *p != ':')
            continue;
        p++;
        end = p;
        while (*end && *end != '\n' && *end != '\r')
            end++;
        len = (size_t)(end - p);
        if (len >= value_size)
            len = value_size - 1;
        memcpy(value_out, p, len);
        value_out[len] = '\0';
        return 1;
    }
    return 0;
}

static int parse_luid_token(const char *buf, const char *key,
                            struct winluid *luid_out)
{
    size_t key_len = strlen(key);
    const char *p = buf;
    unsigned high;
    unsigned low;

    while ((p = strstr(p, key)) != NULL) {
        char before = p == buf ? ' ' : p[-1];

        if ((before >= 'a' && before <= 'z') ||
            (before >= 'A' && before <= 'Z') ||
            (before >= '0' && before <= '9') || before == '_') {
            p += key_len;
            continue;
        }
        p += key_len;
        if (*p != '=' && *p != ':')
            continue;
        p++;
        if (sscanf(p, "%x:%x", &high, &low) == 2) {
            luid_out->a = low;
            luid_out->b = high;
            return 1;
        }
    }
    return 0;
}

static int read_present_evidence_file(const char *path, uint64_t *counter_out,
                                      int *rejected_out,
                                      struct present_evidence *evidence_out)
{
    static const char *gpu_complete_keys[] = {
        "d3d12_gpu_present_complete",
        "d3d12_gpu_present_completes",
        "d3d12_gpu_composite_complete",
        "d3d12_gpu_composite_completes",
        "d3d12_native_gpu_present_complete",
        "d3d12_native_gpu_presents",
        "d3d12_scanout_gpu_present_complete",
        "d3d12_scanout_gpu_presents",
    };
    static const char *reject_keys[] = {
        "d3d12_cpu_readback",
        "d3d12_cpu_mapping",
        "d3d12_cpu_copy",
        "d3d12_readback",
        "d3d12_native_present_unimplemented",
        "d3d12_present_state_only",
        "d3d12_present_fence_only",
        "d3d12_present_import_only",
        "d3d12_present_open_only",
        "d3d12_present_callback_only",
        "d3d12_present_release_only",
        "d3d12_phase2_bad_luid_rejected",
        "d3d12_phase2_wrong_dimensions_rejected",
        "d3d12_phase2_wrong_format_rejected",
        "d3d12_phase2_missing_resource_fd_rejected",
        "d3d12_phase2_missing_fence_fd_rejected",
        "d3d12_phase2_stale_fence_rejected",
        "d3d12_phase2_cpu_mappable_fallback_rejected",
        "callbacks_blocked",
        "releases_blocked",
    };
    char buf[16384];
    struct stat st;
    FILE *fp;
    size_t n;
    int found = 0;
    int evidence_file_found = 0;
    uint64_t evidence_marker = 0;

    memset(&st, 0, sizeof(st));
    if (stat(path, &st) != 0)
        return 0;
    fp = fopen(path, "r");
    if (!fp)
        return 0;
    n = fread(buf, 1, sizeof(buf) - 1, fp);
    fclose(fp);
    buf[n] = '\0';
    if (strstr(buf, "d3d12_present_path=") ||
        parse_counter_token(buf, "d3d12_evidence_generation",
                            &evidence_marker) ||
        parse_counter_token(buf, "d3d12_present_rejected",
                            &evidence_marker))
        evidence_file_found = 1;

    if (evidence_out) {
        memset(evidence_out, 0, sizeof(*evidence_out));
        evidence_out->mtime_ms = (int64_t)st.st_mtime * 1000;
        evidence_out->native_path =
            strstr(buf, "d3d12_present_path=d3d12-copy-to-fb-bo-display-present") != NULL ||
            strstr(buf, "d3d12_present_path=d3d12-dxg-present-source-display-handoff") != NULL;
        parse_string_token(buf, "d3d12_run_id", evidence_out->run_id,
                           sizeof(evidence_out->run_id));
        parse_string_token(buf, "d3d12_present_identity_compositor_run_id",
                           evidence_out->present_identity_compositor_run_id,
                           sizeof(evidence_out->present_identity_compositor_run_id));
        parse_string_token(buf, "d3d12_evidence_stage",
                           evidence_out->evidence_stage,
                           sizeof(evidence_out->evidence_stage));
        parse_string_token(buf, "d3d12_display_target_kind",
                           evidence_out->display_target_kind,
                           sizeof(evidence_out->display_target_kind));
        parse_string_token(buf, "d3d12_final_handoff_lane",
                           evidence_out->final_handoff_lane,
                           sizeof(evidence_out->final_handoff_lane));
        parse_string_token(buf, "d3d12_final_handoff_selected",
                           evidence_out->final_handoff_selected,
                           sizeof(evidence_out->final_handoff_selected));
        parse_string_token(buf, "d3d12_final_handoff_source",
                           evidence_out->final_handoff_source,
                           sizeof(evidence_out->final_handoff_source));
        parse_string_token(buf, "d3d12_final_handoff_intermediate",
                           evidence_out->final_handoff_intermediate,
                           sizeof(evidence_out->final_handoff_intermediate));
        parse_string_token(buf, "d3d12_final_handoff_destination",
                           evidence_out->final_handoff_destination,
                           sizeof(evidence_out->final_handoff_destination));
        parse_string_token(buf, "d3d12_wslg_user_display_transport",
                           evidence_out->wslg_user_display_transport,
                           sizeof(evidence_out->wslg_user_display_transport));
        parse_string_token(buf, "d3d12_wslg_user_display_helper_path",
                           evidence_out->wslg_user_display_helper_path,
                           sizeof(evidence_out->wslg_user_display_helper_path));
        parse_string_token(buf, "d3d12_wslg_user_display_service_path",
                           evidence_out->wslg_user_display_helper_path,
                           sizeof(evidence_out->wslg_user_display_helper_path));
        parse_string_token(buf, "d3d12_wslg_user_display_dependency",
                           evidence_out->wslg_user_display_dependency,
                           sizeof(evidence_out->wslg_user_display_dependency));
        parse_string_token(buf, "d3d12_present_source_query_skipped_reason",
                           evidence_out->present_source_query_skipped_reason,
                           sizeof(evidence_out->present_source_query_skipped_reason));
        parse_counter_token(buf, "d3d12_gpu_present_starts",
                            &evidence_out->starts);
        parse_counter_token(buf, "d3d12_gpu_copy_completes",
                            &evidence_out->copy_completes);
        parse_counter_token(buf, "d3d12_gpu_present_completes",
                            &evidence_out->completes);
        parse_counter_token(buf, "d3d12_client_pid",
                            &evidence_out->client_pid);
        parse_counter_token(buf, "d3d12_client_buffer_id",
                            &evidence_out->client_buffer_id);
        parse_counter_token(buf, "d3d12_manager_resource_id",
                            &evidence_out->manager_resource_id);
        parse_counter_token(buf, "d3d12_buffer_generation",
                            &evidence_out->buffer_generation);
        parse_counter_token(buf, "d3d12_native_present_attempt_id",
                            &evidence_out->native_present_attempt_id);
        parse_counter_token(buf, "d3d12_native_present_completion_id",
                            &evidence_out->native_present_completion_id);
        parse_counter_token(buf, "d3d12_client_native_present_attempts",
                            &evidence_out->client_native_present_attempts);
        parse_counter_token(buf, "d3d12_client_native_present_completions",
                            &evidence_out->client_native_present_completions);
        parse_counter_token(buf, "d3d12_client_native_present_rejects",
                            &evidence_out->client_native_present_rejects);
        parse_counter_token(buf, "d3d12_resource_native_present_attempts",
                            &evidence_out->resource_native_present_attempts);
        parse_counter_token(buf, "d3d12_resource_native_present_completions",
                            &evidence_out->resource_native_present_completions);
        parse_counter_token(buf, "d3d12_resource_native_present_rejects",
                            &evidence_out->resource_native_present_rejects);
        parse_counter_token(buf, "d3d12_resource_generation_counter",
                            &evidence_out->resource_generation_counter);
        parse_counter_token(
            buf, "d3d12_resource_generation_native_present_attempts",
            &evidence_out->resource_generation_native_present_attempts);
        parse_counter_token(
            buf, "d3d12_resource_generation_native_present_completions",
            &evidence_out->resource_generation_native_present_completions);
        parse_counter_token(
            buf, "d3d12_resource_generation_native_present_rejects",
            &evidence_out->resource_generation_native_present_rejects);
        parse_counter_token(buf, "d3d12_identity_counters_match_resource",
                            &evidence_out->identity_counters_match_resource);
        parse_counter_token(buf, "d3d12_present_identity_client_pid",
                            &evidence_out->present_identity_client_pid);
        parse_counter_token(buf, "d3d12_present_identity_client_buffer_id",
                            &evidence_out->present_identity_client_buffer_id);
        parse_counter_token(buf, "d3d12_present_identity_manager_resource_id",
                            &evidence_out->present_identity_manager_resource_id);
        parse_counter_token(buf, "d3d12_present_identity_buffer_generation",
                            &evidence_out->present_identity_buffer_generation);
        parse_counter_token(buf, "d3d12_present_identity_attempt_id",
                            &evidence_out->present_identity_attempt_id);
        parse_counter_token(buf, "d3d12_present_identity_completion_id",
                            &evidence_out->present_identity_completion_id);
        parse_counter_token(buf, "d3d12_present_identity_current_run_valid",
                            &evidence_out->present_identity_current_run_valid);
        parse_counter_token(buf, "d3d12_callback_release_same_frame_required",
                            &evidence_out->callback_release_same_frame_required);
        parse_counter_token(buf, "d3d12_present_resource",
                            &evidence_out->resource);
        parse_counter_token(buf, "d3d12_present_allocation_count",
                            &evidence_out->allocation_count);
        parse_counter_token(buf, "d3d12_present_fence",
                            &evidence_out->fence);
        parse_counter_token(buf, "d3d12_present_fence_target",
                            &evidence_out->fence_target);
        parse_counter_token(buf, "d3d12_present_release_fence",
                            &evidence_out->release_fence);
        parse_counter_token(buf, "d3d12_present_format",
                            &evidence_out->format);
        parse_counter_token(buf, "d3d12_display_handoff_implemented",
                            &evidence_out->display_handoff_implemented);
        parse_counter_token(
            buf, "d3d12_display_handoff_requires_kernel_host_protocol",
            &evidence_out->display_handoff_requires_kernel_host_protocol);
        if (!parse_counter_token(
                buf, "d3d12_display_target_requires_kernel_host_protocol",
                &evidence_out->display_target_requires_kernel_host_protocol)) {
            evidence_out->display_target_requires_kernel_host_protocol =
                evidence_out->display_handoff_requires_kernel_host_protocol;
        }
        parse_counter_token(buf, "d3d12_display_completion_correlated",
                            &evidence_out->display_completion_correlated);
        parse_counter_token(
            buf, "d3d12_native_present_requirements_satisfied",
            &evidence_out->native_present_requirements_satisfied);
        parse_counter_token(buf, "d3d12_display_target_kind_raw",
                            &evidence_out->display_target_kind_raw);
        parse_counter_token(buf, "d3d12_display_target_kernel_reported",
                            &evidence_out->display_target_kernel_reported);
        parse_counter_token(
            buf, "d3d12_runtime_created_d3d12_resource_present",
            &evidence_out->runtime_created_d3d12_resource_present);
        parse_counter_token(buf, "d3d12_synthvid_sysmem_fallback_present",
                            &evidence_out->synthvid_sysmem_fallback_present);
        parse_counter_token(buf, "d3d12_existing_sysmem_is_d3d12_com_resource",
                            &evidence_out->existing_sysmem_is_d3d12_com_resource);
        parse_counter_token(buf, "d3d12_synthvid_sysmem_satisfies_native_present",
                            &evidence_out->synthvid_sysmem_satisfies_native_present);
        parse_counter_token(buf, "d3d12_synthvid_dirty_rect_count",
                            &evidence_out->synthvid_dirty_rect_count);
        parse_counter_token(buf, "d3d12_synthvid_dirty_sequence",
                            &evidence_out->synthvid_dirty_sequence);
        parse_counter_token(
            buf, "d3d12_dirty_rect_display_completion_correlated",
            &evidence_out->dirty_rect_display_completion_correlated);
        parse_counter_token(buf, "d3d12_present_sequence_cpu_map_rejects_delta",
                            &evidence_out->present_sequence_cpu_map_rejects_delta);
        parse_counter_token(
            buf, "d3d12_present_sequence_cpu_plane_rejects_delta",
            &evidence_out->present_sequence_cpu_plane_rejects_delta);
        parse_counter_token(buf, "d3d12_no_cpu_map_no_readback_confirmed",
                            &evidence_out->no_cpu_map_no_readback_confirmed);
        parse_counter_token(buf, "d3d12_present_sequence_framebuffer_blit_used",
                            &evidence_out->present_sequence_framebuffer_blit_used);
        parse_counter_token(buf, "d3d12_present_sequence_cpu_map_used",
                            &evidence_out->present_sequence_cpu_map_used);
        parse_counter_token(buf, "d3d12_present_sequence_cpu_readback_used",
                            &evidence_out->present_sequence_cpu_readback_used);
        parse_counter_token(buf, "d3d12_present_sequence_cpu_copy_used",
                            &evidence_out->present_sequence_cpu_copy_used);
        parse_counter_token(buf, "d3d12_present_sequence_software_dri_used",
                            &evidence_out->present_sequence_software_dri_used);
        parse_counter_token(buf, "d3d12_software_dri_present_used",
                            &evidence_out->software_dri_present_used);
        parse_counter_token(buf, "d3d12_framebuffer_blit_only",
                            &evidence_out->framebuffer_blit_only);
        parse_counter_token(buf, "d3d12_phase2_bad_luid_rejected",
                            &evidence_out->phase2_bad_luid_rejected);
        parse_counter_token(buf, "d3d12_phase2_wrong_dimensions_rejected",
                            &evidence_out->phase2_wrong_dimensions_rejected);
        parse_counter_token(buf, "d3d12_phase2_wrong_format_rejected",
                            &evidence_out->phase2_wrong_format_rejected);
        parse_counter_token(buf, "d3d12_phase2_missing_resource_fd_rejected",
                            &evidence_out->phase2_missing_resource_fd_rejected);
        parse_counter_token(buf, "d3d12_phase2_missing_fence_fd_rejected",
                            &evidence_out->phase2_missing_fence_fd_rejected);
        parse_counter_token(buf, "d3d12_phase2_stale_fence_rejected",
                            &evidence_out->phase2_stale_fence_rejected);
        parse_counter_token(buf, "d3d12_phase2_cpu_mappable_fallback_rejected",
                            &evidence_out->phase2_cpu_mappable_fallback_rejected);
        parse_counter_token(
            buf, "d3d12_phase2_import_only_satisfies_native_present",
            &evidence_out->phase2_import_only_satisfies_native_present);
        parse_counter_token(
            buf, "d3d12_phase2_open_only_satisfies_native_present",
            &evidence_out->phase2_open_only_satisfies_native_present);
        parse_counter_token(
            buf, "d3d12_phase2_gpu_copy_only_satisfies_native_present",
            &evidence_out->phase2_gpu_copy_only_satisfies_native_present);
        parse_counter_token(
            buf, "d3d12_final_handoff_user_display_channel_selected",
            &evidence_out->final_handoff_user_display_channel_selected);
        parse_counter_token(buf, "d3d12_wslg_user_display_lane_considered",
                            &evidence_out->wslg_user_display_lane_considered);
        parse_counter_token(buf, "d3d12_wslg_user_display_lane_selected",
                            &evidence_out->wslg_user_display_lane_selected);
        parse_counter_token(buf, "d3d12_wslg_user_display_channel_available",
                            &evidence_out->wslg_user_display_channel_available);
        parse_counter_token(buf, "d3d12_wslg_user_display_host_ack_required",
                            &evidence_out->wslg_user_display_host_ack_required);
        parse_counter_token(buf, "d3d12_wslg_user_display_host_ack_observed",
                            &evidence_out->wslg_user_display_host_ack_observed);
        parse_counter_token(buf, "d3d12_wslg_user_display_success",
                            &evidence_out->wslg_user_display_success);
        parse_counter_token(buf, "d3d12_final_handoff_existing_sysmem_allowed",
                            &evidence_out->final_handoff_existing_sysmem_allowed);
        parse_counter_token(
            buf, "d3d12_final_handoff_synthvid_dirty_only_allowed",
            &evidence_out->final_handoff_synthvid_dirty_only_allowed);
        parse_counter_token(
            buf, "d3d12_final_handoff_runtime_resource_required",
            &evidence_out->final_handoff_runtime_resource_required);
        parse_counter_token(
            buf, "d3d12_final_handoff_runtime_resource_observed",
            &evidence_out->final_handoff_runtime_resource_observed);
        parse_counter_token(buf, "d3d12_final_handoff_kernel_abi_required",
                            &evidence_out->final_handoff_kernel_abi_required);
        parse_counter_token(buf, "d3d12_final_handoff_kernel_abi_missing",
                            &evidence_out->final_handoff_kernel_abi_missing);
        parse_counter_token(
            buf, "d3d12_final_handoff_host_display_commit_success",
            &evidence_out->final_handoff_host_display_commit_success);
        parse_counter_token(buf, "d3d12_final_handoff_present_id",
                            &evidence_out->final_handoff_present_id);
        parse_counter_token(buf, "d3d12_final_handoff_completed",
                            &evidence_out->final_handoff_completed);
        parse_counter_token(buf, "d3d12_final_handoff_dirty_rects",
                            &evidence_out->final_handoff_dirty_rects);
        parse_counter_token(buf, "d3d12_final_handoff_dirty_sequence",
                            &evidence_out->final_handoff_dirty_sequence);
        parse_counter_token(buf, "d3d12_final_handoff_completion_correlated",
                            &evidence_out->final_handoff_completion_correlated);
        parse_counter_token(
            buf, "d3d12_final_handoff_no_cpu_map_no_readback",
            &evidence_out->final_handoff_no_cpu_map_no_readback);
        parse_counter_token(buf, "d3d12_final_handoff_release_fence",
                            &evidence_out->final_handoff_release_fence);
        parse_counter_token(buf, "d3d12_final_handoff_success",
                            &evidence_out->final_handoff_success);
        parse_counter_token(
            buf, "d3d12_dxg_present_source_register_attempts",
            &evidence_out->dxg_present_source_register_attempts);
        parse_counter_token(
            buf, "d3d12_dxg_present_source_register_successes",
            &evidence_out->dxg_present_source_register_successes);
        parse_counter_token(
            buf, "d3d12_dxg_present_source_register_errno",
            &evidence_out->dxg_present_source_register_errno);
        parse_counter_token(buf, "d3d12_dxg_present_source",
                            &evidence_out->dxg_present_source);
        parse_counter_token(
            buf, "d3d12_dxg_present_source_commit_attempts",
            &evidence_out->dxg_present_source_commit_attempts);
        parse_counter_token(
            buf, "d3d12_dxg_present_source_commit_successes",
            &evidence_out->dxg_present_source_commit_successes);
        parse_counter_token(
            buf, "d3d12_dxg_present_source_commit_errno",
            &evidence_out->dxg_present_source_commit_errno);
        parse_counter_token(
            buf, "d3d12_dxg_present_source_commit_status",
            &evidence_out->dxg_present_source_commit_status);
        parse_counter_token(
            buf, "d3d12_dxg_present_source_commit_expected_eopnotsupp",
            &evidence_out->dxg_present_source_commit_expected_eopnotsupp);
        parse_counter_token(buf, "d3d12_dxg_present_id",
                            &evidence_out->dxg_present_id);
        parse_counter_token(buf, "d3d12_dxg_present_completed",
                            &evidence_out->dxg_present_completed);
        parse_counter_token(
            buf, "d3d12_present_source_buffer_register_attempts",
            &evidence_out->buffer_present_source_register_attempts);
        parse_counter_token(
            buf, "d3d12_present_source_buffer_register_successes",
            &evidence_out->buffer_present_source_register_successes);
        parse_counter_token(
            buf, "d3d12_present_source_buffer_register_errno",
            &evidence_out->buffer_present_source_register_errno);
        parse_counter_token(buf, "d3d12_present_source_buffer_source",
                            &evidence_out->buffer_present_source);
        parse_counter_token(
            buf, "d3d12_present_source_buffer_commit_attempts",
            &evidence_out->buffer_present_source_commit_attempts);
        parse_counter_token(
            buf, "d3d12_present_source_buffer_commit_successes",
            &evidence_out->buffer_present_source_commit_successes);
        parse_counter_token(
            buf, "d3d12_present_source_buffer_commit_errno",
            &evidence_out->buffer_present_source_commit_errno);
        parse_counter_token(
            buf, "d3d12_present_source_buffer_commit_status",
            &evidence_out->buffer_present_source_commit_status);
        parse_counter_token(
            buf, "d3d12_present_source_buffer_commit_expected_eopnotsupp",
            &evidence_out->buffer_present_source_commit_expected_eopnotsupp);
        parse_counter_token(
            buf, "d3d12_present_source_buffer_present_id",
            &evidence_out->buffer_present_source_present_id);
        parse_counter_token(
            buf, "d3d12_present_source_buffer_completed",
            &evidence_out->buffer_present_source_completed);
        parse_counter_token(
            buf, "d3d12_present_source_buffer_query_attempts",
            &evidence_out->buffer_present_source_query_attempts);
        parse_counter_token(
            buf, "d3d12_present_source_buffer_query_successes",
            &evidence_out->buffer_present_source_query_successes);
        parse_counter_token(
            buf, "d3d12_present_source_buffer_query_errno",
            &evidence_out->buffer_present_source_query_errno);
        parse_counter_token(
            buf, "d3d12_present_source_buffer_query_required",
            &evidence_out->buffer_present_source_query_required);
        parse_counter_token(
            buf, "d3d12_present_source_buffer_query_skipped_commit_failed",
            &evidence_out->buffer_present_source_query_skipped_commit_failed);
        parse_counter_token(
            buf, "d3d12_present_source_buffer_query_skipped_no_present_id",
            &evidence_out->buffer_present_source_query_skipped_no_present_id);
        parse_counter_token(
            buf, "d3d12_present_source_buffer_query_attempted_after_commit_success",
            &evidence_out->buffer_present_source_query_attempted_after_commit_success);
        parse_counter_token(
            buf, "d3d12_present_source_buffer_query_kernel_missing",
            &evidence_out->buffer_present_source_query_kernel_missing);
        parse_counter_token(buf, "d3d12_present_source_registered",
                            &evidence_out->present_source_registered);
        parse_counter_token(buf, "d3d12_present_source_query_attempted",
                            &evidence_out->present_source_query_attempted);
        parse_counter_token(buf,
                            "d3d12_present_source_gpu_p_or_dda_transport_absent",
                            &evidence_out->present_source_gpu_p_or_dda_transport_absent);
        parse_counter_token(buf, "d3d12_present_source_commit_rejected_eopnotsupp",
                            &evidence_out->present_source_commit_rejected_eopnotsupp);
        parse_counter_token(buf, "d3d12_present_source_no_present_id_completed",
                            &evidence_out->present_source_no_present_id_completed);
        parse_counter_token(
            buf, "d3d12_present_source_no_gpu_p_or_dda_display_bind",
            &evidence_out->present_source_no_gpu_p_or_dda_display_bind);
        parse_counter_token(buf, "d3d12_present_source_no_display_handoff",
                            &evidence_out->present_source_no_display_handoff);
        parse_counter_token(buf, "d3d12_present_source_no_present_completion",
                            &evidence_out->present_source_no_present_completion);
        parse_counter_token(
            buf, "d3d12_present_source_same_frame_callbacks_blocked",
            &evidence_out->present_source_same_frame_callbacks_blocked);
        parse_counter_token(
            buf, "d3d12_present_source_same_frame_releases_blocked",
            &evidence_out->present_source_same_frame_releases_blocked);
        parse_counter_token(buf, "d3d12_present_source_callback_blocked",
                            &evidence_out->present_source_callback_blocked);
        parse_counter_token(buf, "d3d12_present_source_release_blocked",
                            &evidence_out->present_source_release_blocked);
        parse_counter_token(buf, "d3d12_present_source_adapter_luid_low",
                            &evidence_out->present_source_adapter_luid_low);
        parse_counter_token(buf, "d3d12_present_source_adapter_luid_high",
                            &evidence_out->present_source_adapter_luid_high);
        parse_counter_token(buf, "d3d12_present_source_provenance_flags",
                            &evidence_out->present_source_provenance_flags);
        parse_counter_token(buf, "d3d12_present_source_register_flags",
                            &evidence_out->present_source_register_flags);
        parse_counter_token(buf, "d3d12_present_descriptor_width",
                            &evidence_out->descriptor_width);
        parse_counter_token(buf, "d3d12_present_descriptor_height",
                            &evidence_out->descriptor_height);
        parse_counter_token(buf, "d3d12_present_descriptor_pitch",
                            &evidence_out->descriptor_pitch);
        parse_string_token(buf, "d3d12_present_descriptor_layout",
                           evidence_out->descriptor_layout,
                           sizeof(evidence_out->descriptor_layout));
        parse_counter_token(buf, "d3d12_present_descriptor_modifier",
                            &evidence_out->descriptor_modifier);
        parse_counter_token(buf, "d3d12_present_descriptor_sample_count",
                            &evidence_out->descriptor_sample_count);
        parse_counter_token(buf, "d3d12_present_descriptor_dxg_fd",
                            &evidence_out->descriptor_dxg_fd);
        parse_counter_token(buf, "d3d12_present_descriptor_resource_fd",
                            &evidence_out->descriptor_resource_fd);
        parse_counter_token(buf, "d3d12_present_descriptor_nt_shared_fd",
                            &evidence_out->descriptor_nt_shared_fd);
        parse_counter_token(buf, "d3d12_present_descriptor_device",
                            &evidence_out->descriptor_device);
        parse_counter_token(buf, "d3d12_present_descriptor_resource",
                            &evidence_out->descriptor_resource);
        parse_counter_token(buf, "d3d12_present_descriptor_allocation0",
                            &evidence_out->descriptor_allocation0);
        parse_counter_token(buf, "d3d12_present_descriptor_allocation_count",
                            &evidence_out->descriptor_allocation_count);
        parse_counter_token(buf, "d3d12_present_descriptor_format",
                            &evidence_out->descriptor_format);
        parse_counter_token(buf, "d3d12_present_descriptor_total_private_size",
                            &evidence_out->descriptor_total_private_size);
        parse_string_token(buf, "d3d12_present_descriptor_luid",
                           evidence_out->descriptor_luid,
                           sizeof(evidence_out->descriptor_luid));
        parse_counter_token(
            buf, "d3d12_present_descriptor_matches_gpu_copy_resource",
            &evidence_out->descriptor_matches_gpu_copy_resource);
        parse_counter_token(
            buf, "d3d12_present_source_buffer_completion_correlated",
            &evidence_out->buffer_present_source_completion_correlated);
        parse_counter_token(buf, "d3d12_buffer_release_observed",
                            &evidence_out->buffer_release_observed);
        parse_counter_token(buf, "d3d12_buffer_release_resource",
                            &evidence_out->buffer_release_resource);
        parse_counter_token(buf, "d3d12_buffer_release_present_sequence",
                            &evidence_out->buffer_release_present_sequence);
        parse_counter_token(buf, "d3d12_buffer_release_buffer_generation",
                            &evidence_out->buffer_release_buffer_generation);
        parse_counter_token(buf, "d3d12_buffer_release_attempt_id",
                            &evidence_out->buffer_release_attempt_id);
        parse_counter_token(buf, "d3d12_buffer_release_completion_id",
                            &evidence_out->buffer_release_completion_id);
        parse_counter_token(buf, "d3d12_buffer_release_present_id",
                            &evidence_out->buffer_release_present_id);
        parse_counter_token(buf, "d3d12_buffer_release_same_resource",
                            &evidence_out->buffer_release_same_resource);
        parse_counter_token(buf, "d3d12_buffer_release_same_generation",
                            &evidence_out->buffer_release_same_generation);
        parse_counter_token(buf, "d3d12_buffer_release_same_attempt",
                            &evidence_out->buffer_release_same_attempt);
        parse_counter_token(buf, "d3d12_buffer_release_same_present_id",
                            &evidence_out->buffer_release_same_present_id);
        parse_counter_token(buf, "d3d12_buffer_release_native_successes",
                            &evidence_out->buffer_release_native_successes);
        parse_counter_token(buf, "d3d12_buffer_release_failclosed_unblocks",
                            &evidence_out->buffer_release_failclosed_unblocks);
        parse_counter_token(buf, "d3d12_frame_callback_observed",
                            &evidence_out->frame_callback_observed);
        parse_counter_token(buf, "d3d12_frame_callback_resource",
                            &evidence_out->frame_callback_resource);
        parse_counter_token(buf, "d3d12_frame_callback_present_sequence",
                            &evidence_out->frame_callback_present_sequence);
        parse_counter_token(buf, "d3d12_frame_callback_buffer_generation",
                            &evidence_out->frame_callback_buffer_generation);
        parse_counter_token(buf, "d3d12_frame_callback_attempt_id",
                            &evidence_out->frame_callback_attempt_id);
        parse_counter_token(buf, "d3d12_frame_callback_completion_id",
                            &evidence_out->frame_callback_completion_id);
        parse_counter_token(buf, "d3d12_frame_callback_present_id",
                            &evidence_out->frame_callback_present_id);
        parse_counter_token(buf, "d3d12_frame_callback_same_resource",
                            &evidence_out->frame_callback_same_resource);
        parse_counter_token(buf, "d3d12_frame_callback_same_generation",
                            &evidence_out->frame_callback_same_generation);
        parse_counter_token(buf, "d3d12_frame_callback_same_attempt",
                            &evidence_out->frame_callback_same_attempt);
        parse_counter_token(buf, "d3d12_frame_callback_same_present_id",
                            &evidence_out->frame_callback_same_present_id);
        parse_counter_token(buf, "d3d12_frame_callback_native_successes",
                            &evidence_out->frame_callback_native_successes);
        parse_counter_token(buf, "d3d12_frame_callback_failclosed_unblocks",
                            &evidence_out->frame_callback_failclosed_unblocks);
        parse_counter_token(buf, "d3d12_callback_release_same_frame_observed",
                            &evidence_out->callback_release_same_frame_observed);
        parse_counter_token(buf, "callbacks_blocked",
                            &evidence_out->callbacks_blocked);
        parse_counter_token(buf, "releases_blocked",
                            &evidence_out->releases_blocked);
        parse_counter_token(buf, "d3d12_failclosed_client_unblock_enabled",
                            &evidence_out->failclosed_client_unblock_enabled);
        parse_counter_token(buf, "d3d12_failclosed_client_unblocked",
                            &evidence_out->failclosed_client_unblocked);
        parse_counter_token(
            buf, "d3d12_failclosed_client_unblock_no_native_present_credit",
            &evidence_out->failclosed_client_unblock_no_native_present_credit);
        parse_counter_token(buf, "d3d12_failclosed_client_unblock_resource",
                            &evidence_out->failclosed_client_unblock_resource);
        parse_counter_token(buf, "d3d12_failclosed_client_unblock_sequence",
                            &evidence_out->failclosed_client_unblock_sequence);
        parse_counter_token(
            buf, "d3d12_failclosed_client_unblock_buffer_generation",
            &evidence_out->failclosed_client_unblock_buffer_generation);
        parse_counter_token(buf, "d3d12_failclosed_client_unblock_attempt_id",
                            &evidence_out->failclosed_client_unblock_attempt_id);
        parse_counter_token(buf, "d3d12_failclosed_client_unblock_releases",
                            &evidence_out->failclosed_client_unblock_releases);
        parse_counter_token(buf, "d3d12_failclosed_client_unblock_callbacks",
                            &evidence_out->failclosed_client_unblock_callbacks);
        parse_counter_token(buf, "d3d12_cpu_readback",
                            &evidence_out->cpu_readback);
        parse_counter_token(buf, "d3d12_cpu_mapping",
                            &evidence_out->cpu_mapping);
        parse_counter_token(buf, "d3d12_cpu_copy",
                            &evidence_out->cpu_copy);
        parse_luid_token(buf, "d3d12_present_luid",
                         &evidence_out->source_luid);
        parse_luid_token(buf, "d3d12_present_matched_luid",
                         &evidence_out->matched_luid);
    }

    for (size_t i = 0;
         i < sizeof(gpu_complete_keys) / sizeof(gpu_complete_keys[0]); i++) {
        uint64_t value = 0;

        if (parse_counter_token(buf, gpu_complete_keys[i], &value)) {
            if (!found || value > *counter_out)
                *counter_out = value;
            if (evidence_out && value > evidence_out->counter)
                evidence_out->counter = value;
            found = 1;
        }
    }
    for (size_t i = 0; i < sizeof(reject_keys) / sizeof(reject_keys[0]);
         i++) {
        uint64_t value = 0;

        if (parse_counter_token(buf, reject_keys[i], &value) && value != 0) {
            *rejected_out = 1;
            if (evidence_out)
                evidence_out->rejected = 1;
        }
    }
    if (evidence_out)
        evidence_out->found = found || evidence_file_found;
    return found || evidence_file_found;
}

static int read_fresh_native_present_evidence(struct present_evidence *evidence)
{
    uint64_t counter = 0;
    int rejected = 0;
    const char *override = getenv("D3D12SHAREDSMOKE_PRESENT_EVIDENCE");
    const char *path = override && override[0] ? override :
        "/tmp/wlcomp-d3d12-present";

    if (!evidence) {
        errno = EINVAL;
        return 0;
    }
    if (!read_present_evidence_file(path, &counter, &rejected, evidence))
        return 0;
    evidence->rejected |= rejected;
    return 1;
}

static int validate_native_present_evidence(
    const struct present_evidence *evidence,
    const struct winluid *expected_luid,
    int64_t minimum_mtime_ms)
{
    if (!evidence || !expected_luid || !evidence->found ||
        (evidence->mtime_ms != 0 &&
         evidence->mtime_ms + 1000 < minimum_mtime_ms) ||
        evidence->rejected || !evidence->native_path ||
        evidence->counter == 0 || evidence->run_id[0] == '\0' ||
        evidence->present_identity_compositor_run_id[0] == '\0' ||
        evidence->starts == 0 ||
        evidence->copy_completes == 0 || evidence->completes == 0 ||
        evidence->client_pid == 0 ||
        evidence->client_buffer_id == 0 ||
        evidence->manager_resource_id == 0 ||
        evidence->buffer_generation == 0 ||
        evidence->native_present_attempt_id == 0 ||
        evidence->native_present_completion_id == 0 ||
        evidence->client_native_present_attempts == 0 ||
        evidence->client_native_present_completions == 0 ||
        evidence->resource_native_present_attempts == 0 ||
        evidence->resource_native_present_completions == 0 ||
        evidence->resource_generation_counter == 0 ||
        evidence->resource_generation_native_present_attempts == 0 ||
        evidence->resource_generation_native_present_completions == 0 ||
        evidence->identity_counters_match_resource == 0 ||
        evidence->present_identity_client_pid == 0 ||
        evidence->present_identity_client_buffer_id == 0 ||
        evidence->present_identity_manager_resource_id == 0 ||
        evidence->present_identity_buffer_generation == 0 ||
        evidence->present_identity_attempt_id == 0 ||
        evidence->present_identity_completion_id == 0 ||
        evidence->present_identity_current_run_valid == 0 ||
        evidence->callback_release_same_frame_required == 0 ||
        evidence->resource == 0 || evidence->allocation_count == 0 ||
        evidence->descriptor_width == 0 ||
        evidence->descriptor_height == 0 ||
        evidence->descriptor_pitch < evidence->descriptor_width * 4 ||
        strcmp(evidence->descriptor_layout, "linear") != 0 ||
        evidence->descriptor_modifier != 0 ||
        evidence->descriptor_sample_count != 1 ||
        evidence->descriptor_dxg_fd == 0 ||
        evidence->descriptor_resource_fd == 0 ||
        evidence->descriptor_nt_shared_fd !=
            evidence->descriptor_resource_fd ||
        evidence->descriptor_device == 0 ||
        evidence->descriptor_resource != evidence->resource ||
        evidence->descriptor_allocation0 == 0 ||
        evidence->descriptor_allocation_count != evidence->allocation_count ||
        evidence->descriptor_format != evidence->format ||
        evidence->descriptor_total_private_size == 0 ||
        evidence->descriptor_luid[0] == '\0' ||
        evidence->descriptor_matches_gpu_copy_resource == 0 ||
        evidence->fence == 0 || evidence->fence_target != 1 ||
        evidence->release_fence == 0 ||
        evidence->format != WL_SHM_FORMAT_ARGB8888 ||
        evidence->display_handoff_implemented == 0 ||
        evidence->display_target_kind[0] == '\0' ||
        strcmp(evidence->display_target_kind,
               "runtime-created-d3d12-resource") != 0 ||
        evidence->display_handoff_requires_kernel_host_protocol != 0 ||
        evidence->display_target_requires_kernel_host_protocol != 0 ||
        evidence->display_completion_correlated == 0 ||
        evidence->native_present_requirements_satisfied == 0 ||
        evidence->display_target_kind_raw == 0 ||
        evidence->display_target_kernel_reported == 0 ||
        evidence->runtime_created_d3d12_resource_present == 0 ||
        evidence->synthvid_sysmem_fallback_present != 0 ||
        evidence->existing_sysmem_is_d3d12_com_resource != 0 ||
        evidence->synthvid_sysmem_satisfies_native_present != 0 ||
        evidence->synthvid_dirty_rect_count == 0 ||
        evidence->synthvid_dirty_sequence == 0 ||
        evidence->dirty_rect_display_completion_correlated == 0 ||
        evidence->present_sequence_cpu_map_rejects_delta != 0 ||
        evidence->present_sequence_cpu_plane_rejects_delta != 0 ||
        evidence->no_cpu_map_no_readback_confirmed == 0 ||
        evidence->present_sequence_framebuffer_blit_used != 0 ||
        evidence->present_sequence_cpu_map_used != 0 ||
        evidence->present_sequence_cpu_readback_used != 0 ||
        evidence->present_sequence_cpu_copy_used != 0 ||
        evidence->present_sequence_software_dri_used != 0 ||
        evidence->software_dri_present_used != 0 ||
        evidence->framebuffer_blit_only != 0 ||
        evidence->phase2_bad_luid_rejected != 0 ||
        evidence->phase2_wrong_dimensions_rejected != 0 ||
        evidence->phase2_wrong_format_rejected != 0 ||
        evidence->phase2_missing_resource_fd_rejected != 0 ||
        evidence->phase2_missing_fence_fd_rejected != 0 ||
        evidence->phase2_stale_fence_rejected != 0 ||
        evidence->phase2_cpu_mappable_fallback_rejected != 0 ||
        evidence->phase2_import_only_satisfies_native_present != 0 ||
        evidence->phase2_open_only_satisfies_native_present != 0 ||
        evidence->phase2_gpu_copy_only_satisfies_native_present != 0 ||
        strcmp(evidence->final_handoff_lane,
               "runtime-created-d3d12-resource-through-gpu-p-or-dda") != 0 ||
        (strcmp(evidence->final_handoff_selected,
                "dxg-resource-scanout-bind") != 0 &&
         strcmp(evidence->final_handoff_selected,
                "gpu-p-dxg-resource-scanout-bind") != 0) ||
        strcmp(evidence->final_handoff_source,
               "runtime-created-d3d12-resource") != 0 ||
        strcmp(evidence->final_handoff_intermediate,
               "compositor-owned-d3d12-texture") != 0 ||
        strcmp(evidence->final_handoff_destination,
               "host-display-channel") != 0 ||
        evidence->final_handoff_user_display_channel_selected != 0 ||
        evidence->final_handoff_existing_sysmem_allowed != 0 ||
        evidence->final_handoff_synthvid_dirty_only_allowed != 0 ||
        evidence->final_handoff_runtime_resource_required == 0 ||
        evidence->final_handoff_runtime_resource_observed == 0 ||
        evidence->final_handoff_kernel_abi_required == 0 ||
        evidence->final_handoff_kernel_abi_missing != 0 ||
        evidence->final_handoff_host_display_commit_success == 0 ||
        evidence->final_handoff_present_id == 0 ||
        evidence->final_handoff_completed == 0 ||
        evidence->final_handoff_dirty_rects == 0 ||
        evidence->final_handoff_dirty_sequence == 0 ||
        evidence->final_handoff_completion_correlated == 0 ||
        evidence->final_handoff_no_cpu_map_no_readback == 0 ||
        evidence->final_handoff_release_fence == 0 ||
        evidence->final_handoff_success == 0 ||
        evidence->dxg_present_source_register_attempts == 0 ||
        evidence->dxg_present_source_register_successes == 0 ||
        evidence->dxg_present_source_register_errno != 0 ||
        evidence->dxg_present_source == 0 ||
        evidence->dxg_present_source_commit_attempts == 0 ||
        evidence->dxg_present_source_commit_successes == 0 ||
        evidence->dxg_present_source_commit_errno != 0 ||
        evidence->dxg_present_source_commit_status != 0 ||
        evidence->dxg_present_source_commit_expected_eopnotsupp != 0 ||
        evidence->dxg_present_id == 0 ||
        evidence->dxg_present_completed == 0 ||
        evidence->buffer_present_source_register_attempts == 0 ||
        evidence->buffer_present_source_register_successes == 0 ||
        evidence->buffer_present_source_register_errno != 0 ||
        evidence->buffer_present_source == 0 ||
        evidence->buffer_present_source_commit_attempts == 0 ||
        evidence->buffer_present_source_commit_successes == 0 ||
        evidence->buffer_present_source_commit_errno != 0 ||
        evidence->buffer_present_source_commit_status != 0 ||
        evidence->buffer_present_source_commit_expected_eopnotsupp != 0 ||
        evidence->buffer_present_source_present_id == 0 ||
        evidence->buffer_present_source_completed == 0 ||
        evidence->buffer_present_source_completion_correlated == 0 ||
        evidence->buffer_release_observed == 0 ||
        evidence->buffer_release_resource == 0 ||
        evidence->buffer_release_present_sequence == 0 ||
        evidence->buffer_release_buffer_generation == 0 ||
        evidence->buffer_release_attempt_id == 0 ||
        evidence->buffer_release_completion_id == 0 ||
        evidence->buffer_release_present_id == 0 ||
        evidence->buffer_release_same_resource == 0 ||
        evidence->buffer_release_same_generation == 0 ||
        evidence->buffer_release_same_attempt == 0 ||
        evidence->buffer_release_same_present_id == 0 ||
        evidence->buffer_release_native_successes == 0 ||
        evidence->buffer_release_failclosed_unblocks != 0 ||
        evidence->frame_callback_observed == 0 ||
        evidence->frame_callback_resource == 0 ||
        evidence->frame_callback_present_sequence == 0 ||
        evidence->frame_callback_buffer_generation == 0 ||
        evidence->frame_callback_attempt_id == 0 ||
        evidence->frame_callback_completion_id == 0 ||
        evidence->frame_callback_present_id == 0 ||
        evidence->frame_callback_same_resource == 0 ||
        evidence->frame_callback_same_generation == 0 ||
        evidence->frame_callback_same_attempt == 0 ||
        evidence->frame_callback_same_present_id == 0 ||
        evidence->frame_callback_native_successes == 0 ||
        evidence->frame_callback_failclosed_unblocks != 0 ||
        evidence->callback_release_same_frame_observed == 0 ||
        evidence->cpu_readback != 0 || evidence->cpu_mapping != 0 ||
        evidence->cpu_copy != 0)
        return -1;
    if (evidence->dxg_present_completed < evidence->dxg_present_id ||
        evidence->buffer_present_source_completed <
            evidence->buffer_present_source_present_id)
        return -1;
    if (evidence->final_handoff_completed <
            evidence->final_handoff_present_id ||
        evidence->final_handoff_dirty_sequence >
            evidence->final_handoff_completed ||
        evidence->final_handoff_release_fence !=
            evidence->final_handoff_completed)
        return -1;
    if (evidence->buffer_release_resource != evidence->resource ||
        evidence->frame_callback_resource != evidence->resource)
        return -1;
    if (evidence->resource_generation_counter !=
            evidence->buffer_generation ||
        evidence->resource_native_present_attempts >
            evidence->client_native_present_attempts ||
        evidence->resource_native_present_completions >
            evidence->client_native_present_completions ||
        evidence->resource_native_present_rejects >
            evidence->client_native_present_rejects ||
        evidence->resource_native_present_completions >
            evidence->resource_native_present_attempts ||
        evidence->resource_native_present_rejects >
            evidence->resource_native_present_attempts ||
        evidence->client_native_present_completions >
            evidence->client_native_present_attempts)
        return -1;
    if (evidence->resource_generation_native_present_attempts !=
            evidence->resource_native_present_attempts ||
        evidence->resource_generation_native_present_completions !=
            evidence->resource_native_present_completions ||
        evidence->resource_generation_native_present_rejects !=
            evidence->resource_native_present_rejects)
        return -1;
    if (strcmp(evidence->present_identity_compositor_run_id,
               evidence->run_id) != 0)
        return -1;
    if (evidence->present_identity_client_pid != evidence->client_pid ||
        evidence->present_identity_client_buffer_id !=
            evidence->client_buffer_id ||
        evidence->present_identity_manager_resource_id !=
            evidence->manager_resource_id ||
        evidence->present_identity_buffer_generation !=
            evidence->buffer_generation ||
        evidence->present_identity_attempt_id !=
            evidence->native_present_attempt_id ||
        evidence->present_identity_completion_id !=
            evidence->native_present_completion_id)
        return -1;
    if (evidence->buffer_release_buffer_generation !=
            evidence->buffer_generation ||
        evidence->buffer_release_attempt_id !=
            evidence->native_present_attempt_id ||
        evidence->buffer_release_completion_id !=
            evidence->native_present_completion_id ||
        evidence->buffer_release_present_id !=
            evidence->buffer_present_source_present_id ||
        evidence->frame_callback_buffer_generation !=
            evidence->buffer_generation ||
        evidence->frame_callback_attempt_id !=
            evidence->native_present_attempt_id ||
        evidence->frame_callback_completion_id !=
            evidence->native_present_completion_id ||
        evidence->frame_callback_present_id !=
            evidence->buffer_present_source_present_id)
        return -1;
    if (evidence->buffer_release_present_sequence !=
            evidence->frame_callback_present_sequence)
        return -1;
    if (evidence->completes > evidence->starts ||
        evidence->completes > evidence->copy_completes)
        return -1;
    if (evidence->source_luid.a == 0 && evidence->source_luid.b == 0)
        return -1;
    if (evidence->source_luid.a != evidence->matched_luid.a ||
        evidence->source_luid.b != evidence->matched_luid.b)
        return -1;
    if (evidence->source_luid.a != expected_luid->a ||
        evidence->source_luid.b != expected_luid->b)
        return -1;
    return 0;
}

static int present_evidence_has_current_run_identity(
    const struct present_evidence *evidence,
    const struct winluid *expected_luid,
    int64_t minimum_mtime_ms)
{
    if (!evidence || !expected_luid || !evidence->found ||
        (evidence->mtime_ms != 0 &&
         evidence->mtime_ms + 1000 < minimum_mtime_ms) ||
        evidence->run_id[0] == '\0' ||
        evidence->present_identity_compositor_run_id[0] == '\0' ||
        strcmp(evidence->present_identity_compositor_run_id,
               evidence->run_id) != 0 ||
        evidence->present_identity_current_run_valid == 0 ||
        evidence->client_pid == 0 ||
        evidence->client_buffer_id == 0 ||
        evidence->manager_resource_id == 0 ||
        evidence->buffer_generation == 0 ||
        evidence->native_present_attempt_id == 0 ||
        evidence->client_native_present_attempts == 0 ||
        evidence->resource_native_present_attempts == 0 ||
        evidence->resource_generation_counter != evidence->buffer_generation ||
        evidence->resource_generation_native_present_attempts !=
            evidence->resource_native_present_attempts ||
        evidence->resource_generation_native_present_completions !=
            evidence->resource_native_present_completions ||
        evidence->resource_generation_native_present_rejects !=
            evidence->resource_native_present_rejects ||
        evidence->identity_counters_match_resource == 0 ||
        evidence->resource_native_present_attempts >
            evidence->client_native_present_attempts ||
        evidence->resource_native_present_completions >
            evidence->client_native_present_completions ||
        evidence->resource_native_present_rejects >
            evidence->client_native_present_rejects ||
        evidence->present_identity_client_pid != evidence->client_pid ||
        evidence->present_identity_client_buffer_id !=
            evidence->client_buffer_id ||
        evidence->present_identity_manager_resource_id !=
            evidence->manager_resource_id ||
        evidence->present_identity_buffer_generation !=
            evidence->buffer_generation ||
        evidence->present_identity_attempt_id !=
            evidence->native_present_attempt_id ||
        evidence->present_identity_completion_id !=
            evidence->native_present_completion_id)
        return 0;
    if (evidence->source_luid.a == 0 && evidence->source_luid.b == 0)
        return 0;
    if (evidence->source_luid.a != evidence->matched_luid.a ||
        evidence->source_luid.b != evidence->matched_luid.b)
        return 0;
    if (evidence->source_luid.a != expected_luid->a ||
        evidence->source_luid.b != expected_luid->b)
        return 0;
    return 1;
}

static int present_evidence_is_terminal_fail_closed(
    const struct present_evidence *evidence,
    const struct winluid *expected_luid,
    int64_t minimum_mtime_ms)
{
    int failclosed_client_unblocked;

    if (!present_evidence_has_current_run_identity(
            evidence, expected_luid, minimum_mtime_ms))
        return 0;
    failclosed_client_unblocked =
        evidence->failclosed_client_unblocked != 0 &&
        evidence->failclosed_client_unblock_no_native_present_credit != 0;
    if (!evidence->rejected ||
        strcmp(evidence->evidence_stage, "present_rejected") != 0 ||
        evidence->starts == 0 ||
        evidence->copy_completes == 0 ||
        evidence->completes != 0 ||
        evidence->resource == 0 ||
        evidence->allocation_count == 0 ||
        evidence->descriptor_width == 0 ||
        evidence->descriptor_height == 0 ||
        evidence->descriptor_pitch < evidence->descriptor_width * 4 ||
        strcmp(evidence->descriptor_layout, "linear") != 0 ||
        evidence->descriptor_modifier != 0 ||
        evidence->descriptor_sample_count != 1 ||
        evidence->descriptor_dxg_fd == 0 ||
        evidence->descriptor_resource_fd == 0 ||
        evidence->descriptor_nt_shared_fd !=
            evidence->descriptor_resource_fd ||
        evidence->descriptor_device == 0 ||
        evidence->descriptor_resource != evidence->resource ||
        evidence->descriptor_allocation0 == 0 ||
        evidence->descriptor_allocation_count != evidence->allocation_count ||
        evidence->descriptor_format != evidence->format ||
        evidence->descriptor_total_private_size == 0 ||
        evidence->descriptor_luid[0] == '\0' ||
        evidence->descriptor_matches_gpu_copy_resource == 0 ||
        evidence->fence == 0 ||
        evidence->fence_target == 0 ||
        evidence->dxg_present_source_register_attempts == 0 ||
        evidence->dxg_present_source_register_successes == 0 ||
        evidence->dxg_present_source_register_errno != 0 ||
        evidence->dxg_present_source == 0 ||
        evidence->dxg_present_source_commit_attempts == 0 ||
        evidence->dxg_present_source_commit_successes != 0 ||
        evidence->dxg_present_source_commit_errno != EOPNOTSUPP ||
        evidence->dxg_present_source_commit_status != EOPNOTSUPP ||
        evidence->dxg_present_source_commit_expected_eopnotsupp == 0 ||
        evidence->dxg_present_id != 0 ||
        evidence->dxg_present_completed != 0 ||
        evidence->buffer_present_source_register_attempts == 0 ||
        evidence->buffer_present_source_register_successes == 0 ||
        evidence->buffer_present_source_register_errno != 0 ||
        evidence->buffer_present_source == 0 ||
        evidence->buffer_present_source_commit_attempts == 0 ||
        evidence->buffer_present_source_commit_successes != 0 ||
        evidence->buffer_present_source_commit_errno != EOPNOTSUPP ||
        evidence->buffer_present_source_commit_status != EOPNOTSUPP ||
        evidence->buffer_present_source_commit_expected_eopnotsupp == 0 ||
        evidence->buffer_present_source_present_id != 0 ||
        evidence->buffer_present_source_completed != 0 ||
        evidence->buffer_present_source_query_attempts != 0 ||
        evidence->buffer_present_source_query_successes != 0 ||
        evidence->buffer_present_source_query_required != 0 ||
        evidence->buffer_present_source_query_skipped_commit_failed == 0 ||
        evidence->buffer_present_source_query_skipped_no_present_id != 0 ||
        evidence->buffer_present_source_query_attempted_after_commit_success != 0 ||
        evidence->buffer_present_source_query_kernel_missing != 0 ||
        evidence->present_source_registered == 0 ||
        evidence->present_source_query_attempted != 0 ||
        strcmp(evidence->present_source_query_skipped_reason,
               "commit-failed") != 0 ||
        evidence->present_source_gpu_p_or_dda_transport_absent == 0 ||
        evidence->present_source_commit_rejected_eopnotsupp == 0 ||
        evidence->present_source_no_present_id_completed == 0 ||
        evidence->present_source_no_gpu_p_or_dda_display_bind == 0 ||
        evidence->present_source_no_display_handoff == 0 ||
        evidence->present_source_no_present_completion == 0 ||
        (!failclosed_client_unblocked &&
         evidence->present_source_same_frame_callbacks_blocked == 0) ||
        (!failclosed_client_unblocked &&
         evidence->present_source_same_frame_releases_blocked == 0) ||
        (!failclosed_client_unblocked &&
         evidence->present_source_callback_blocked == 0) ||
        (!failclosed_client_unblocked &&
         evidence->present_source_release_blocked == 0) ||
        evidence->present_source_register_flags != 0 ||
        evidence->present_source_adapter_luid_low != expected_luid->a ||
        evidence->present_source_adapter_luid_high != expected_luid->b ||
        (evidence->present_source_provenance_flags &
         FB_GPU_DXG_PRESENT_PROV_D3DKMT_HANDLES) == 0 ||
        (evidence->present_source_provenance_flags &
         FB_GPU_DXG_PRESENT_PROV_DIMENSIONS) == 0 ||
        (evidence->present_source_provenance_flags &
         FB_GPU_DXG_PRESENT_PROV_ADAPTER_LUID) == 0 ||
        evidence->buffer_present_source_completion_correlated != 0 ||
        evidence->display_handoff_implemented != 0 ||
        evidence->display_completion_correlated != 0 ||
        evidence->native_present_requirements_satisfied != 0 ||
        evidence->buffer_release_native_successes != 0 ||
        evidence->frame_callback_native_successes != 0 ||
        evidence->phase2_import_only_satisfies_native_present != 0 ||
        evidence->phase2_open_only_satisfies_native_present != 0 ||
        evidence->phase2_gpu_copy_only_satisfies_native_present != 0 ||
        evidence->present_sequence_framebuffer_blit_used != 0 ||
        evidence->present_sequence_cpu_map_used != 0 ||
        evidence->present_sequence_cpu_readback_used != 0 ||
        evidence->present_sequence_cpu_copy_used != 0 ||
        evidence->present_sequence_software_dri_used != 0 ||
        evidence->software_dri_present_used != 0 ||
        evidence->framebuffer_blit_only != 0 ||
        evidence->present_sequence_cpu_map_rejects_delta != 0 ||
        evidence->present_sequence_cpu_plane_rejects_delta != 0 ||
        evidence->no_cpu_map_no_readback_confirmed == 0 ||
        (!failclosed_client_unblocked && evidence->callbacks_blocked == 0) ||
        (!failclosed_client_unblocked && evidence->releases_blocked == 0) ||
        (!failclosed_client_unblocked &&
         evidence->frame_callback_observed != 0) ||
        (!failclosed_client_unblocked &&
         evidence->buffer_release_observed != 0) ||
        evidence->cpu_readback != 0 ||
        evidence->cpu_mapping != 0 ||
        evidence->cpu_copy != 0)
        return 0;
    if (failclosed_client_unblocked) {
        if (evidence->failclosed_client_unblock_resource != evidence->resource ||
            evidence->failclosed_client_unblock_buffer_generation !=
                evidence->buffer_generation ||
            evidence->failclosed_client_unblock_attempt_id !=
                evidence->native_present_attempt_id ||
            evidence->native_present_completion_id != 0 ||
            evidence->buffer_release_completion_id != 0 ||
            evidence->frame_callback_completion_id != 0 ||
            evidence->buffer_release_present_id != 0 ||
            evidence->frame_callback_present_id != 0 ||
            evidence->buffer_release_failclosed_unblocks !=
                evidence->failclosed_client_unblock_releases ||
            evidence->frame_callback_failclosed_unblocks !=
                evidence->failclosed_client_unblock_callbacks ||
            evidence->callback_release_same_frame_observed != 0 ||
            evidence->native_present_requirements_satisfied != 0 ||
            evidence->completes != 0)
            return 0;
    }
    return 1;
}

static void print_present_evidence_matrix(
    const char *label, const char *path, const struct present_evidence *e,
    int valid)
{
    char source_luid[32];
    char matched_luid[32];

    format_winluid_text(source_luid, sizeof(source_luid),
                        e ? e->source_luid : (struct winluid){ 0, 0 });
    format_winluid_text(matched_luid, sizeof(matched_luid),
                        e ? e->matched_luid : (struct winluid){ 0, 0 });
    printf("d3d12sharedsmoke: present-evidence-matrix label=%s path=%s status=%s found=%u rejected=%u native_path=%u counter=%lu starts=%lu copy=%lu completes=%lu client_attempts=%lu client_completions=%lu client_rejects=%lu resource_attempts=%lu resource_completions=%lu resource_rejects=%lu generation_attempts=%lu generation_completions=%lu generation_rejects=%lu present_id=%lu completed=%lu buffer_present_id=%lu buffer_completed=%lu callbacks=%lu callback_native_successes=%lu callback_failclosed=%lu callback_resource=0x%lx callback_sequence=%lu releases=%lu release_native_successes=%lu release_failclosed=%lu release_resource=0x%lx release_sequence=%lu release_fence=%lu descriptor=%lux%lu pitch=%lu layout=%s sample_count=%lu dxg_fd=%lu resource_fd=%lu nt_fd=%lu device=0x%lx desc_resource=0x%lx allocation0=0x%lx desc_allocations=%lu desc_format=0x%lx total_priv=%lu desc_luid=%s desc_matches_copy=%lu handoff=%lu target_kind=%s display_correlated=%lu requirements=%lu phase2_bad_luid=%lu phase2_wrong_dimensions=%lu phase2_wrong_format=%lu phase2_missing_resource_fd=%lu phase2_missing_fence_fd=%lu phase2_stale_fence=%lu phase2_cpu_mappable_fallback=%lu cpu_readback=%lu cpu_mapping=%lu cpu_copy=%lu source_luid=%s matched_luid=%s\n",
           label ? label : "present-evidence", path ? path : "none",
           valid ? "PASS" : "FAIL",
           e && e->found, e && e->rejected, e && e->native_path,
           (unsigned long)(e ? e->counter : 0),
           (unsigned long)(e ? e->starts : 0),
           (unsigned long)(e ? e->copy_completes : 0),
           (unsigned long)(e ? e->completes : 0),
           (unsigned long)(e ? e->client_native_present_attempts : 0),
           (unsigned long)(e ? e->client_native_present_completions : 0),
           (unsigned long)(e ? e->client_native_present_rejects : 0),
           (unsigned long)(e ? e->resource_native_present_attempts : 0),
           (unsigned long)(e ? e->resource_native_present_completions : 0),
           (unsigned long)(e ? e->resource_native_present_rejects : 0),
           (unsigned long)(e ?
               e->resource_generation_native_present_attempts : 0),
           (unsigned long)(e ?
               e->resource_generation_native_present_completions : 0),
           (unsigned long)(e ?
               e->resource_generation_native_present_rejects : 0),
           (unsigned long)(e ? e->dxg_present_id : 0),
           (unsigned long)(e ? e->dxg_present_completed : 0),
           (unsigned long)(e ? e->buffer_present_source_present_id : 0),
           (unsigned long)(e ? e->buffer_present_source_completed : 0),
           (unsigned long)(e ? e->frame_callback_observed : 0),
           (unsigned long)(e ? e->frame_callback_native_successes : 0),
           (unsigned long)(e ? e->frame_callback_failclosed_unblocks : 0),
           (unsigned long)(e ? e->frame_callback_resource : 0),
           (unsigned long)(e ? e->frame_callback_present_sequence : 0),
           (unsigned long)(e ? e->buffer_release_observed : 0),
           (unsigned long)(e ? e->buffer_release_native_successes : 0),
           (unsigned long)(e ? e->buffer_release_failclosed_unblocks : 0),
           (unsigned long)(e ? e->buffer_release_resource : 0),
           (unsigned long)(e ? e->buffer_release_present_sequence : 0),
           (unsigned long)(e ? e->release_fence : 0),
           (unsigned long)(e ? e->descriptor_width : 0),
           (unsigned long)(e ? e->descriptor_height : 0),
           (unsigned long)(e ? e->descriptor_pitch : 0),
           e && e->descriptor_layout[0] ? e->descriptor_layout : "none",
           (unsigned long)(e ? e->descriptor_sample_count : 0),
           (unsigned long)(e ? e->descriptor_dxg_fd : 0),
           (unsigned long)(e ? e->descriptor_resource_fd : 0),
           (unsigned long)(e ? e->descriptor_nt_shared_fd : 0),
           (unsigned long)(e ? e->descriptor_device : 0),
           (unsigned long)(e ? e->descriptor_resource : 0),
           (unsigned long)(e ? e->descriptor_allocation0 : 0),
           (unsigned long)(e ? e->descriptor_allocation_count : 0),
           (unsigned long)(e ? e->descriptor_format : 0),
           (unsigned long)(e ? e->descriptor_total_private_size : 0),
           e && e->descriptor_luid[0] ? e->descriptor_luid : "none",
           (unsigned long)(e ? e->descriptor_matches_gpu_copy_resource : 0),
           (unsigned long)(e ? e->display_handoff_implemented : 0),
           e && e->display_target_kind[0] ? e->display_target_kind : "none",
           (unsigned long)(e ? e->display_completion_correlated : 0),
           (unsigned long)(e ? e->native_present_requirements_satisfied : 0),
           (unsigned long)(e ? e->phase2_bad_luid_rejected : 0),
           (unsigned long)(e ? e->phase2_wrong_dimensions_rejected : 0),
           (unsigned long)(e ? e->phase2_wrong_format_rejected : 0),
           (unsigned long)(e ? e->phase2_missing_resource_fd_rejected : 0),
           (unsigned long)(e ? e->phase2_missing_fence_fd_rejected : 0),
           (unsigned long)(e ? e->phase2_stale_fence_rejected : 0),
           (unsigned long)(e ? e->phase2_cpu_mappable_fallback_rejected : 0),
           (unsigned long)(e ? e->cpu_readback : 0),
           (unsigned long)(e ? e->cpu_mapping : 0),
           (unsigned long)(e ? e->cpu_copy : 0),
           source_luid, matched_luid);
    printf("d3d12sharedsmoke: final-handoff-evidence label=%s lane=%s selected=%s source=%s intermediate=%s destination=%s success=%lu kernel_abi_missing=%lu host_commit=%lu runtime_resource=%lu present_id=%lu completed=%lu dirty_rects=%lu dirty_sequence=%lu correlated=%lu no_cpu=%lu release_fence=%lu existing_sysmem_allowed=%lu synthvid_dirty_only_allowed=%lu wslg_lane_considered=%lu wslg_lane_selected=%lu wslg_channel_available=%lu wslg_transport=%s wslg_service=%s wslg_ack_required=%lu wslg_ack_observed=%lu wslg_success=%lu\n",
           label ? label : "present-evidence",
           e && e->final_handoff_lane[0] ? e->final_handoff_lane : "none",
           e && e->final_handoff_selected[0] ?
               e->final_handoff_selected : "none",
           e && e->final_handoff_source[0] ?
               e->final_handoff_source : "none",
           e && e->final_handoff_intermediate[0] ?
               e->final_handoff_intermediate : "none",
           e && e->final_handoff_destination[0] ?
               e->final_handoff_destination : "none",
           (unsigned long)(e ? e->final_handoff_success : 0),
           (unsigned long)(e ? e->final_handoff_kernel_abi_missing : 0),
           (unsigned long)(e ?
               e->final_handoff_host_display_commit_success : 0),
           (unsigned long)(e ?
               e->final_handoff_runtime_resource_observed : 0),
           (unsigned long)(e ? e->final_handoff_present_id : 0),
           (unsigned long)(e ? e->final_handoff_completed : 0),
           (unsigned long)(e ? e->final_handoff_dirty_rects : 0),
           (unsigned long)(e ? e->final_handoff_dirty_sequence : 0),
           (unsigned long)(e ? e->final_handoff_completion_correlated : 0),
           (unsigned long)(e ?
               e->final_handoff_no_cpu_map_no_readback : 0),
           (unsigned long)(e ? e->final_handoff_release_fence : 0),
           (unsigned long)(e ?
               e->final_handoff_existing_sysmem_allowed : 0),
           (unsigned long)(e ?
               e->final_handoff_synthvid_dirty_only_allowed : 0),
           (unsigned long)(e ? e->wslg_user_display_lane_considered : 0),
           (unsigned long)(e ? e->wslg_user_display_lane_selected : 0),
           (unsigned long)(e ? e->wslg_user_display_channel_available : 0),
           e && e->wslg_user_display_transport[0] ?
               e->wslg_user_display_transport : "none",
           e && e->wslg_user_display_helper_path[0] ?
               e->wslg_user_display_helper_path : "none",
           (unsigned long)(e ? e->wslg_user_display_host_ack_required : 0),
           (unsigned long)(e ? e->wslg_user_display_host_ack_observed : 0),
           (unsigned long)(e ? e->wslg_user_display_success : 0));
}

static int run_present_evidence_validate(const char *path)
{
    struct present_evidence evidence;
    struct winluid expected_luid;
    uint64_t counter = 0;
    int rejected = 0;
    int found;
    int valid;

    memset(&evidence, 0, sizeof(evidence));
    found = read_present_evidence_file(path, &counter, &rejected, &evidence);
    expected_luid = evidence.source_luid;
    valid = found &&
        validate_native_present_evidence(&evidence, &expected_luid, 0) == 0;
    print_present_evidence_matrix("external", path, &evidence, valid);
    return valid ? 0 : 1;
}

static int write_present_evidence_fixture(const char *path, const char *body)
{
    FILE *fp;

    fp = fopen(path, "w");
    if (!fp)
        return -1;
    fputs(body, fp);
    fclose(fp);
    return 0;
}

static int run_present_evidence_selftest(void)
{
    static const char positive[] =
        "d3d12_present_path=d3d12-dxg-present-source-display-handoff\n"
        "d3d12_run_id=selftest\n"
        "d3d12_present_identity_compositor_run_id=selftest\n"
        "d3d12_display_target_kind=runtime-created-d3d12-resource\n"
        "d3d12_gpu_present_starts=1\n"
        "d3d12_gpu_copy_completes=1\n"
        "d3d12_gpu_present_completes=1\n"
        "d3d12_client_pid=100\n"
        "d3d12_client_buffer_id=10\n"
        "d3d12_manager_resource_id=11\n"
        "d3d12_buffer_generation=12\n"
        "d3d12_native_present_attempt_id=3\n"
        "d3d12_native_present_completion_id=4\n"
        "d3d12_present_identity_client_pid=100\n"
        "d3d12_present_identity_client_buffer_id=10\n"
        "d3d12_present_identity_manager_resource_id=11\n"
        "d3d12_present_identity_buffer_generation=12\n"
        "d3d12_present_identity_attempt_id=3\n"
        "d3d12_present_identity_completion_id=4\n"
        "d3d12_present_identity_current_run_valid=1\n"
        "d3d12_callback_release_same_frame_required=1\n"
        "d3d12_present_resource=0x1234\n"
        "d3d12_present_allocation_count=1\n"
        "d3d12_present_fence=0x5678\n"
        "d3d12_present_fence_target=1\n"
        "d3d12_present_release_fence=7\n"
        "d3d12_present_format=0\n"
        "d3d12_present_descriptor_width=256\n"
        "d3d12_present_descriptor_height=256\n"
        "d3d12_present_descriptor_pitch=1024\n"
        "d3d12_present_descriptor_layout=linear\n"
        "d3d12_present_descriptor_modifier=0\n"
        "d3d12_present_descriptor_sample_count=1\n"
        "d3d12_present_descriptor_dxg_fd=4\n"
        "d3d12_present_descriptor_resource_fd=23\n"
        "d3d12_present_descriptor_nt_shared_fd=23\n"
        "d3d12_present_descriptor_device=0x42\n"
        "d3d12_present_descriptor_resource=0x1234\n"
        "d3d12_present_descriptor_allocation0=0x4321\n"
        "d3d12_present_descriptor_allocation_count=1\n"
        "d3d12_present_descriptor_format=0\n"
        "d3d12_present_descriptor_total_private_size=594\n"
        "d3d12_present_descriptor_luid=00000002:00000001\n"
        "d3d12_present_descriptor_matches_gpu_copy_resource=1\n"
        "d3d12_display_handoff_implemented=1\n"
        "d3d12_display_handoff_requires_kernel_host_protocol=0\n"
        "d3d12_display_target_requires_kernel_host_protocol=0\n"
        "d3d12_display_completion_correlated=1\n"
        "d3d12_native_present_requirements_satisfied=1\n"
        "d3d12_display_target_kind_raw=2\n"
        "d3d12_display_target_kernel_reported=1\n"
        "d3d12_runtime_created_d3d12_resource_present=1\n"
        "d3d12_synthvid_sysmem_fallback_present=0\n"
        "d3d12_existing_sysmem_is_d3d12_com_resource=0\n"
        "d3d12_synthvid_sysmem_satisfies_native_present=0\n"
        "d3d12_synthvid_dirty_rect_count=1\n"
        "d3d12_synthvid_dirty_sequence=5\n"
        "d3d12_dirty_rect_display_completion_correlated=1\n"
        "d3d12_present_sequence_cpu_map_rejects_delta=0\n"
        "d3d12_present_sequence_cpu_plane_rejects_delta=0\n"
        "d3d12_no_cpu_map_no_readback_confirmed=1\n"
        "d3d12_present_sequence_framebuffer_blit_used=0\n"
        "d3d12_present_sequence_cpu_map_used=0\n"
        "d3d12_present_sequence_cpu_readback_used=0\n"
        "d3d12_present_sequence_cpu_copy_used=0\n"
        "d3d12_present_sequence_software_dri_used=0\n"
        "d3d12_software_dri_present_used=0\n"
        "d3d12_framebuffer_blit_only=0\n"
        "d3d12_final_handoff_lane=runtime-created-d3d12-resource-through-gpu-p-or-dda\n"
        "d3d12_final_handoff_selected=dxg-resource-scanout-bind\n"
        "d3d12_final_handoff_source=runtime-created-d3d12-resource\n"
        "d3d12_final_handoff_intermediate=compositor-owned-d3d12-texture\n"
        "d3d12_final_handoff_destination=host-display-channel\n"
        "d3d12_final_handoff_user_display_channel_selected=0\n"
        "d3d12_wslg_user_display_lane_considered=1\n"
        "d3d12_wslg_user_display_lane_selected=0\n"
        "d3d12_wslg_user_display_channel_available=0\n"
        "d3d12_wslg_user_display_transport=none\n"
        "d3d12_wslg_user_display_service_path=none\n"
        "d3d12_wslg_user_display_helper_path=none\n"
        "d3d12_wslg_user_display_host_ack_required=1\n"
        "d3d12_wslg_user_display_host_ack_observed=0\n"
        "d3d12_wslg_user_display_success=0\n"
        "d3d12_wslg_user_display_dependency=represented-gpu-p-or-dda-display-transport\n"
        "d3d12_final_handoff_existing_sysmem_allowed=0\n"
        "d3d12_final_handoff_synthvid_dirty_only_allowed=0\n"
        "d3d12_final_handoff_runtime_resource_required=1\n"
        "d3d12_final_handoff_runtime_resource_observed=1\n"
        "d3d12_final_handoff_kernel_abi_required=1\n"
        "d3d12_final_handoff_kernel_abi=FB_GPU_DXG_PRESENT_SOURCE_COMMIT/runtime-resource-scanout\n"
        "d3d12_final_handoff_kernel_abi_missing=0\n"
        "d3d12_final_handoff_host_display_commit_success=1\n"
        "d3d12_final_handoff_present_id=5\n"
        "d3d12_final_handoff_completed=5\n"
        "d3d12_final_handoff_dirty_rects=1\n"
        "d3d12_final_handoff_dirty_sequence=5\n"
        "d3d12_final_handoff_completion_correlated=1\n"
        "d3d12_final_handoff_no_cpu_map_no_readback=1\n"
        "d3d12_final_handoff_release_fence=5\n"
        "d3d12_final_handoff_callbacks_release_gate=native-display-completion\n"
        "d3d12_final_handoff_success=1\n"
        "d3d12_dxg_present_source_register_attempts=1\n"
        "d3d12_dxg_present_source_register_successes=1\n"
        "d3d12_dxg_present_source_register_errno=0\n"
        "d3d12_dxg_present_source=0x99\n"
        "d3d12_dxg_present_source_commit_attempts=1\n"
        "d3d12_dxg_present_source_commit_successes=1\n"
        "d3d12_dxg_present_source_commit_errno=0\n"
        "d3d12_dxg_present_source_commit_status=0\n"
        "d3d12_dxg_present_source_commit_expected_eopnotsupp=0\n"
        "d3d12_dxg_present_id=5\n"
        "d3d12_dxg_present_completed=5\n"
        "d3d12_present_source_buffer_register_attempts=1\n"
        "d3d12_present_source_buffer_register_successes=1\n"
        "d3d12_present_source_buffer_register_errno=0\n"
        "d3d12_present_source_buffer_source=0x88\n"
        "d3d12_present_source_buffer_commit_attempts=1\n"
        "d3d12_present_source_buffer_commit_successes=1\n"
        "d3d12_present_source_buffer_commit_errno=0\n"
        "d3d12_present_source_buffer_commit_status=0\n"
        "d3d12_present_source_buffer_commit_expected_eopnotsupp=0\n"
        "d3d12_present_source_buffer_present_id=5\n"
        "d3d12_present_source_buffer_completed=5\n"
        "d3d12_present_source_buffer_completion_correlated=1\n"
        "d3d12_buffer_release_observed=1\n"
        "d3d12_buffer_release_resource=0x1234\n"
        "d3d12_buffer_release_present_sequence=5\n"
        "d3d12_buffer_release_buffer_generation=12\n"
        "d3d12_buffer_release_attempt_id=3\n"
        "d3d12_buffer_release_completion_id=4\n"
        "d3d12_buffer_release_present_id=5\n"
        "d3d12_buffer_release_same_resource=1\n"
        "d3d12_buffer_release_same_generation=1\n"
        "d3d12_buffer_release_same_attempt=1\n"
        "d3d12_buffer_release_same_present_id=1\n"
        "d3d12_frame_callback_observed=1\n"
        "d3d12_frame_callback_resource=0x1234\n"
        "d3d12_frame_callback_present_sequence=5\n"
        "d3d12_frame_callback_buffer_generation=12\n"
        "d3d12_frame_callback_attempt_id=3\n"
        "d3d12_frame_callback_completion_id=4\n"
        "d3d12_frame_callback_present_id=5\n"
        "d3d12_frame_callback_same_resource=1\n"
        "d3d12_frame_callback_same_generation=1\n"
        "d3d12_frame_callback_same_attempt=1\n"
        "d3d12_frame_callback_same_present_id=1\n"
        "d3d12_callback_release_same_frame_observed=1\n"
        "d3d12_buffer_release_native_successes=1\n"
        "d3d12_frame_callback_native_successes=1\n"
        "d3d12_buffer_release_failclosed_unblocks=0\n"
        "d3d12_frame_callback_failclosed_unblocks=0\n"
        "d3d12_cpu_readback=0\n"
        "d3d12_cpu_mapping=0\n"
        "d3d12_cpu_copy=0\n"
        "d3d12_present_luid=00000002:00000001\n"
        "d3d12_present_matched_luid=00000002:00000001\n";
    static const char identity_ok[] =
        "d3d12_client_native_present_attempts=1\n"
        "d3d12_client_native_present_completions=1\n"
        "d3d12_client_native_present_rejects=0\n"
        "d3d12_resource_native_present_attempts=1\n"
        "d3d12_resource_native_present_completions=1\n"
        "d3d12_resource_native_present_rejects=0\n"
        "d3d12_resource_generation_counter=12\n"
        "d3d12_resource_generation_native_present_attempts=1\n"
        "d3d12_resource_generation_native_present_completions=1\n"
        "d3d12_resource_generation_native_present_rejects=0\n"
        "d3d12_identity_counters_match_resource=1\n";
    static const char identity_missing_client_attempts[] =
        "d3d12_client_native_present_completions=1\n"
        "d3d12_client_native_present_rejects=0\n"
        "d3d12_resource_native_present_attempts=1\n"
        "d3d12_resource_native_present_completions=1\n"
        "d3d12_resource_native_present_rejects=0\n"
        "d3d12_resource_generation_counter=12\n"
        "d3d12_resource_generation_native_present_attempts=1\n"
        "d3d12_resource_generation_native_present_completions=1\n"
        "d3d12_resource_generation_native_present_rejects=0\n"
        "d3d12_identity_counters_match_resource=1\n";
    static const char identity_missing_resource_attempts[] =
        "d3d12_client_native_present_attempts=1\n"
        "d3d12_client_native_present_completions=1\n"
        "d3d12_client_native_present_rejects=0\n"
        "d3d12_resource_native_present_completions=1\n"
        "d3d12_resource_native_present_rejects=0\n"
        "d3d12_resource_generation_counter=12\n"
        "d3d12_resource_generation_native_present_attempts=1\n"
        "d3d12_resource_generation_native_present_completions=1\n"
        "d3d12_resource_generation_native_present_rejects=0\n"
        "d3d12_identity_counters_match_resource=1\n";
    static const char identity_mismatch_flag[] =
        "d3d12_client_native_present_attempts=1\n"
        "d3d12_client_native_present_completions=1\n"
        "d3d12_client_native_present_rejects=0\n"
        "d3d12_resource_native_present_attempts=1\n"
        "d3d12_resource_native_present_completions=1\n"
        "d3d12_resource_native_present_rejects=0\n"
        "d3d12_resource_generation_counter=12\n"
        "d3d12_resource_generation_native_present_attempts=1\n"
        "d3d12_resource_generation_native_present_completions=1\n"
        "d3d12_resource_generation_native_present_rejects=0\n"
        "d3d12_identity_counters_match_resource=0\n";
    static const char identity_stale_generation[] =
        "d3d12_client_native_present_attempts=1\n"
        "d3d12_client_native_present_completions=1\n"
        "d3d12_client_native_present_rejects=0\n"
        "d3d12_resource_native_present_attempts=1\n"
        "d3d12_resource_native_present_completions=1\n"
        "d3d12_resource_native_present_rejects=0\n"
        "d3d12_resource_generation_counter=13\n"
        "d3d12_resource_generation_native_present_attempts=1\n"
        "d3d12_resource_generation_native_present_completions=1\n"
        "d3d12_resource_generation_native_present_rejects=0\n"
        "d3d12_identity_counters_match_resource=1\n";
    static const char identity_resource_completion_other_client[] =
        "d3d12_client_native_present_attempts=1\n"
        "d3d12_client_native_present_completions=1\n"
        "d3d12_client_native_present_rejects=0\n"
        "d3d12_resource_native_present_attempts=1\n"
        "d3d12_resource_native_present_completions=2\n"
        "d3d12_resource_native_present_rejects=0\n"
        "d3d12_resource_generation_counter=12\n"
        "d3d12_resource_generation_native_present_attempts=1\n"
        "d3d12_resource_generation_native_present_completions=2\n"
        "d3d12_resource_generation_native_present_rejects=0\n"
        "d3d12_identity_counters_match_resource=1\n";
    struct {
        const char *label;
        const char *identity;
        const char *extra;
        int expect_pass;
    } cases[] = {
        { "positive", NULL, "", 1 },
        { "import-only-rejected", NULL, "d3d12_present_import_only=1\n", 0 },
        { "open-only-rejected", NULL, "d3d12_present_open_only=1\n", 0 },
        { "copy-only-rejected", NULL, "d3d12_cpu_copy=1\n", 0 },
        { "fail-closed-rejected", NULL,
          "d3d12_native_present_unimplemented=1\n", 0 },
        { "bad-luid-rejected",
          NULL, "d3d12_phase2_bad_luid_rejected=1\n", 0 },
        { "wrong-dimensions-rejected",
          NULL, "d3d12_phase2_wrong_dimensions_rejected=1\n", 0 },
        { "wrong-format-rejected",
          NULL, "d3d12_phase2_wrong_format_rejected=1\n", 0 },
        { "missing-resource-fd-rejected",
          NULL, "d3d12_phase2_missing_resource_fd_rejected=1\n", 0 },
        { "missing-fence-fd-rejected",
          NULL, "d3d12_phase2_missing_fence_fd_rejected=1\n", 0 },
        { "stale-fence-rejected",
          NULL, "d3d12_phase2_stale_fence_rejected=1\n", 0 },
        { "cpu-mappable-fallback-rejected",
          NULL, "d3d12_phase2_cpu_mappable_fallback_rejected=1\n", 0 },
        { "incomplete-present-id",
          NULL,
          "d3d12_dxg_present_id=6\n"
          "d3d12_present_source_buffer_present_id=6\n", 0 },
        { "missing-client-counter",
          identity_missing_client_attempts, "", 0 },
        { "missing-resource-counter",
          identity_missing_resource_attempts, "", 0 },
        { "stale-resource-generation",
          identity_stale_generation, "", 0 },
        { "resource-counter-not-matched",
          identity_mismatch_flag, "", 0 },
        { "resource-completion-from-other-client",
          identity_resource_completion_other_client, "", 0 },
        { "native-success-has-failclosed-release",
          NULL, "d3d12_buffer_release_failclosed_unblocks=1\n", 0 },
        { "native-success-has-failclosed-callback",
          NULL, "d3d12_frame_callback_failclosed_unblocks=1\n", 0 },
    };
    int failures = 0;

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        char path[128];
        char body[16384];
        struct present_evidence evidence;
        struct winluid expected_luid;
        uint64_t counter = 0;
        int rejected = 0;
        int found;
        int valid;

        snprintf(path, sizeof(path),
                 "/tmp/d3d12sharedsmoke-present-evidence-%ld-%lu.txt",
                 (long)getpid(), (unsigned long)i);
        snprintf(body, sizeof(body), "%s%s%s",
                 cases[i].identity ? cases[i].identity : identity_ok,
                 cases[i].extra, positive);
        if (write_present_evidence_fixture(path, body) != 0) {
            printf("d3d12sharedsmoke: present-evidence-selftest label=%s status=FAIL reason=write_failed path=%s\n",
                   cases[i].label, path);
            failures++;
            continue;
        }
        memset(&evidence, 0, sizeof(evidence));
        found = read_present_evidence_file(path, &counter, &rejected,
                                           &evidence);
        expected_luid = evidence.source_luid;
        valid = found &&
            validate_native_present_evidence(&evidence, &expected_luid, 0) == 0;
        print_present_evidence_matrix(cases[i].label, path, &evidence, valid);
        printf("d3d12sharedsmoke: present-evidence-selftest label=%s expected=%s actual=%s status=%s\n",
               cases[i].label, cases[i].expect_pass ? "PASS" : "FAIL",
               valid ? "PASS" : "FAIL",
               valid == cases[i].expect_pass ? "PASS" : "FAIL");
        if (valid != cases[i].expect_pass)
            failures++;
        unlink(path);
    }
    printf("d3d12sharedsmoke: present-evidence-selftest summary failures=%d\n",
           failures);
    return failures == 0 ? 0 : 1;
}

static int run_runtime_negative_submission_bundle(const char *self_path)
{
    struct negative_case {
        const char *name;
        const char *expect;
        int runtime_submission;
        const char *args[6];
    };
    static const struct negative_case cases[] = {
        {
            "bad-luid",
            "compositor-reject",
            1,
            { "--runtime", "--runtime-dxg-syncfile-acquire", "--bad-luid",
              NULL },
        },
        {
            "bad-dimensions",
            "compositor-reject",
            1,
            { "--runtime", "--runtime-dxg-syncfile-acquire",
              "--bad-dimensions", NULL },
        },
        {
            "bad-format",
            "compositor-reject",
            1,
            { "--runtime", "--runtime-dxg-syncfile-acquire", "--bad-format",
              NULL },
        },
        {
            "missing-fence",
            "compositor-reject",
            1,
            { "--runtime", "--runtime-dxg-syncfile-acquire",
              "--missing-fence", NULL },
        },
        {
            "stale-fence",
            "compositor-reject",
            1,
            { "--runtime", "--runtime-dxg-syncfile-acquire", "--stale-fence",
              NULL },
        },
        {
            "cpu-fallback",
            "present-evidence-selftest-rejects-cpu-fallback",
            0,
            { "--present-evidence-selftest", NULL },
        },
    };
    int failures = 0;

    if (!self_path || !*self_path)
        self_path = "d3d12sharedsmoke";

    printf("d3d12sharedsmoke: runtime-negative-submission-matrix begin cases=%lu cpu_fallback_runtime_submission=0 cpu_fallback_reason=no-compositor-cpu-fallback-submission-path-for-d3d12-shared-buffers\n",
           (unsigned long)(sizeof(cases) / sizeof(cases[0])));
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        char *child_argv[8];
        pid_t pid;
        int status = 0;
        int pass = 0;
        int argc = 0;

        child_argv[argc++] = (char *)self_path;
        for (int j = 0; cases[i].args[j] && argc < 7; j++)
            child_argv[argc++] = (char *)cases[i].args[j];
        child_argv[argc] = NULL;

        fflush(NULL);
        pid = fork();
        if (pid == 0) {
            execvp(self_path, child_argv);
            _exit(127);
        }
        if (pid < 0) {
            failures++;
            printf("d3d12sharedsmoke: runtime-negative-submission-matrix case=%s runtime_submission=%d expect=%s status=FAIL reason=fork_failed errno=%d (%s)\n",
                   cases[i].name, cases[i].runtime_submission,
                   cases[i].expect, errno, strerror(errno));
            continue;
        }
        if (waitpid(pid, &status, 0) < 0) {
            failures++;
            printf("d3d12sharedsmoke: runtime-negative-submission-matrix case=%s runtime_submission=%d expect=%s status=FAIL reason=waitpid_failed errno=%d (%s)\n",
                   cases[i].name, cases[i].runtime_submission,
                   cases[i].expect, errno, strerror(errno));
            continue;
        }

        pass = WIFEXITED(status) && WEXITSTATUS(status) == 0;
        if (!pass)
            failures++;
        printf("d3d12sharedsmoke: runtime-negative-submission-matrix case=%s runtime_submission=%d expect=%s command=\"%s",
               cases[i].name, cases[i].runtime_submission,
               cases[i].expect, self_path);
        for (int j = 0; cases[i].args[j]; j++)
            printf(" %s", cases[i].args[j]);
        printf("\" exit_kind=%s exit_status=%d signal=%d status=%s\n",
               WIFEXITED(status) ? "exited" :
               (WIFSIGNALED(status) ? "signaled" : "other"),
               WIFEXITED(status) ? WEXITSTATUS(status) : -1,
               WIFSIGNALED(status) ? WTERMSIG(status) : 0,
               pass ? "PASS" : "FAIL");
    }
    printf("d3d12sharedsmoke: runtime-negative-submission-matrix summary pass=%lu fail=%d status=%s native_present_claim=0\n",
           (unsigned long)(sizeof(cases) / sizeof(cases[0]) -
                           (size_t)failures),
           failures, failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}

static void xdg_wm_base_ping(void *data, struct xdg_wm_base *wm_base,
                             uint32_t serial)
{
    (void)data;
    xdg_wm_base_pong(wm_base, serial);
}

static const struct xdg_wm_base_listener wm_base_listener = {
    .ping = xdg_wm_base_ping,
};

static void buffer_release(void *data, struct wl_buffer *buffer)
{
    struct app *app = data;

    (void)buffer;
    app->release_seen = 1;
    app->running = 0;
}

static const struct wl_buffer_listener buffer_listener = {
    .release = buffer_release,
};

static void frame_done(void *data, struct wl_callback *callback,
                       uint32_t callback_data)
{
    struct app *app = data;

    (void)callback_data;
    if (app->frame_callback == callback)
        app->frame_callback = NULL;
    wl_callback_destroy(callback);
    app->frame_seen = 1;
}

static const struct wl_callback_listener frame_listener = {
    .done = frame_done,
};

static void registry_global(void *data, struct wl_registry *registry,
                            uint32_t name, const char *interface,
                            uint32_t version)
{
    struct app *app = data;

    if (strcmp(interface, wl_compositor_interface.name) == 0) {
        app->compositor = wl_registry_bind(
            registry, name, &wl_compositor_interface, version < 4 ? version : 4);
    } else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
        app->wm_base = wl_registry_bind(
            registry, name, &xdg_wm_base_interface, version < 2 ? version : 2);
        xdg_wm_base_add_listener(app->wm_base, &wm_base_listener, app);
    } else if (strcmp(interface, xv6_gpu_buffer_manager_interface.name) == 0) {
        app->gpu_manager_version = version < 6 ? version : 6;
        app->gpu_manager = wl_registry_bind(
            registry, name, &xv6_gpu_buffer_manager_interface,
            app->gpu_manager_version);
    }
}

static void registry_global_remove(void *data, struct wl_registry *registry,
                                   uint32_t name)
{
    (void)data;
    (void)registry;
    (void)name;
}

static const struct wl_registry_listener registry_listener = {
    .global = registry_global,
    .global_remove = registry_global_remove,
};

static int open_dxg_device_for_luid(int *fd_out,
                                    struct d3dkmthandle *adapter_out,
                                    struct d3dkmthandle *device_out,
                                    struct winluid *luid_out,
                                    const struct winluid *required_luid)
{
    struct d3dkmt_enumadapters2 enum2;
    struct d3dkmt_adapterinfo adapters[D3DKMT_ADAPTERS_MAX];
    uint32_t adapter_count;
    uint32_t first_usable = D3DKMT_ADAPTERS_MAX;
    int fd = open("/dev/dxg", O_RDWR);
    int saved_errno = ENODEV;

    if (fd < 0)
        return -1;

    memset(&enum2, 0, sizeof(enum2));
    memset(adapters, 0, sizeof(adapters));
    enum2.num_adapters = D3DKMT_ADAPTERS_MAX;
    enum2.adapters = (uint64)adapters;
    if (ioctl(fd, LX_DXENUMADAPTERS2, &enum2) < 0 ||
        enum2.num_adapters == 0) {
        saved_errno = errno ? errno : ENODEV;
        fprintf(stderr,
                "d3d12sharedsmoke: enum adapter records failed count=%u requested=%u errno=%d (%s)\n",
                enum2.num_adapters, D3DKMT_ADAPTERS_MAX,
                saved_errno, strerror(saved_errno));
        close(fd);
        errno = saved_errno;
        return -1;
    }
    adapter_count = enum2.num_adapters;
    if (adapter_count > D3DKMT_ADAPTERS_MAX)
        adapter_count = D3DKMT_ADAPTERS_MAX;
    for (uint32_t i = 0; i < adapter_count; i++) {
        if (adapters[i].adapter_handle.v != 0) {
            first_usable = i;
            break;
        }
    }
    if (first_usable == D3DKMT_ADAPTERS_MAX) {
        fprintf(stderr,
                "d3d12sharedsmoke: enum adapter records contained no usable handle count=%u requested=%u\n",
                adapter_count, D3DKMT_ADAPTERS_MAX);
        close(fd);
        errno = ENODEV;
        return -1;
    }
    fprintf(stderr,
            "d3d12sharedsmoke: enum adapters2 layout=list-first requested=%u count=%u first_index=%u\n",
            D3DKMT_ADAPTERS_MAX, enum2.num_adapters, first_usable);

    for (uint32_t i = 0; i < adapter_count; i++) {
        struct d3dkmt_openadapterfromluid open_luid;
        struct d3dkmt_createdevice create_device;

        if (required_luid &&
            (adapters[i].adapter_luid.a != required_luid->a ||
             adapters[i].adapter_luid.b != required_luid->b))
            continue;

        memset(&open_luid, 0, sizeof(open_luid));
        open_luid.adapter_luid = adapters[i].adapter_luid;
        if (ioctl(fd, LX_DXOPENADAPTERFROMLUID, &open_luid) < 0 ||
            open_luid.adapter_handle.v == 0)
            continue;

        memset(&create_device, 0, sizeof(create_device));
        create_device.adapter = open_luid.adapter_handle;
        if (ioctl(fd, LX_DXCREATEDEVICE, &create_device) == 0 &&
            create_device.device.v != 0) {
            *fd_out = fd;
            *adapter_out = open_luid.adapter_handle;
            *device_out = create_device.device;
            if (luid_out)
                *luid_out = adapters[i].adapter_luid;
            return 0;
        } else {
            struct d3dkmt_closeadapter close_adapter;

            memset(&close_adapter, 0, sizeof(close_adapter));
            close_adapter.adapter_handle = open_luid.adapter_handle;
            ioctl(fd, LX_DXCLOSEADAPTER, &close_adapter);
        }
    }

    if (required_luid) {
        char required_luid_text[32];

        format_winluid_text(required_luid_text, sizeof(required_luid_text),
                            *required_luid);
        fprintf(stderr,
                "d3d12sharedsmoke: no DXG adapter matched required LUID %s low=0x%08x high=0x%08x count=%u\n",
                required_luid_text, required_luid->a, required_luid->b,
                adapter_count);
    } else {
        fprintf(stderr,
                "d3d12sharedsmoke: no DXG adapter could create a device count=%u\n",
                adapter_count);
    }
    close(fd);
    errno = ENODEV;
    return -1;
}

static int open_first_dxg_device(int *fd_out, struct d3dkmthandle *adapter_out,
                                 struct d3dkmthandle *device_out,
                                 struct winluid *luid_out)
{
    return open_dxg_device_for_luid(fd_out, adapter_out, device_out,
                                    luid_out, NULL);
}

static int query_first_dxg_adapter_luid(struct winluid *luid_out)
{
    struct d3dkmt_enumadapters2 enum2;
    struct d3dkmt_adapterinfo adapters[D3DKMT_ADAPTERS_MAX];
    uint32_t listed_count;
    int fd;
    int rc;
    int saved_errno = ENODEV;

    if (!luid_out) {
        errno = EINVAL;
        return -1;
    }
    memset(luid_out, 0, sizeof(*luid_out));

    fd = open("/dev/dxg", O_RDWR);
    if (fd < 0) {
        fprintf(stderr,
                "d3d12sharedsmoke: open /dev/dxg for adapter LUID failed errno=%d (%s)\n",
                errno, strerror(errno));
        return -1;
    }

    memset(&enum2, 0, sizeof(enum2));
    memset(adapters, 0, sizeof(adapters));
    enum2.num_adapters = D3DKMT_ADAPTERS_MAX;
    enum2.adapters = (uint64)adapters;
    rc = ioctl(fd, LX_DXENUMADAPTERS2, &enum2);
    if (rc < 0 || enum2.num_adapters == 0) {
        saved_errno = errno ? errno : ENODEV;
        fprintf(stderr,
                "d3d12sharedsmoke: enum adapter records failed rc=%d count=%u errno=%d (%s)\n",
                rc, enum2.num_adapters, saved_errno,
                strerror(saved_errno));
        close(fd);
        errno = saved_errno;
        return -1;
    }
    listed_count = enum2.num_adapters;
    if (listed_count > D3DKMT_ADAPTERS_MAX) {
        fprintf(stderr,
                "d3d12sharedsmoke: enum adapter list returned count %u above local cap %u, clamping scan\n",
                listed_count, D3DKMT_ADAPTERS_MAX);
        listed_count = D3DKMT_ADAPTERS_MAX;
    }

    for (uint32_t i = 0; i < listed_count; i++) {
        char luid_text[32];

        format_winluid_text(luid_text, sizeof(luid_text),
                            adapters[i].adapter_luid);
        fprintf(stderr,
                "d3d12sharedsmoke: dxg adapter[%u] handle=0x%x luid=%s low=0x%08x high=0x%08x sources=%u\n",
                i, adapters[i].adapter_handle.v, luid_text,
                adapters[i].adapter_luid.a, adapters[i].adapter_luid.b,
                adapters[i].num_sources);
        if (adapters[i].adapter_luid.a || adapters[i].adapter_luid.b) {
            *luid_out = adapters[i].adapter_luid;
            close(fd);
            return 0;
        }
    }

    fprintf(stderr,
            "d3d12sharedsmoke: enum adapter records contained no nonzero LUID first_handle=0x%x count=%u requested=%u\n",
            adapters[0].adapter_handle.v, listed_count, D3DKMT_ADAPTERS_MAX);
    close(fd);
    errno = ENODEV;
    return -1;
}

static int query_first_dxg_adapter_luid_quiet(struct winluid *luid_out)
{
    struct d3dkmt_enumadapters2 enum2;
    struct d3dkmt_adapterinfo adapters[D3DKMT_ADAPTERS_MAX];
    uint32_t listed_count;
    int fd;
    int rc;
    int saved_errno = ENODEV;

    if (!luid_out) {
        errno = EINVAL;
        return -1;
    }
    memset(luid_out, 0, sizeof(*luid_out));

    fd = open("/dev/dxg", O_RDWR);
    if (fd < 0)
        return -1;

    memset(&enum2, 0, sizeof(enum2));
    memset(adapters, 0, sizeof(adapters));
    enum2.num_adapters = D3DKMT_ADAPTERS_MAX;
    enum2.adapters = (uint64)adapters;
    rc = ioctl(fd, LX_DXENUMADAPTERS2, &enum2);
    if (rc < 0 || enum2.num_adapters == 0) {
        saved_errno = errno ? errno : ENODEV;
        close(fd);
        errno = saved_errno;
        return -1;
    }
    listed_count = enum2.num_adapters;
    if (listed_count > D3DKMT_ADAPTERS_MAX)
        listed_count = D3DKMT_ADAPTERS_MAX;

    for (uint32_t i = 0; i < listed_count; i++) {
        if (adapters[i].adapter_luid.a || adapters[i].adapter_luid.b) {
            *luid_out = adapters[i].adapter_luid;
            close(fd);
            return 0;
        }
    }

    close(fd);
    errno = ENODEV;
    return -1;
}

static void close_dxg_device(int fd, struct d3dkmthandle adapter,
                             struct d3dkmthandle device)
{
    struct d3dkmt_destroydevice destroy_device;
    struct d3dkmt_closeadapter close_adapter;

    if (fd < 0)
        return;
    if (device.v != 0) {
        memset(&destroy_device, 0, sizeof(destroy_device));
        destroy_device.device = device;
        ioctl(fd, LX_DXDESTROYDEVICE, &destroy_device);
    }
    if (adapter.v != 0) {
        memset(&close_adapter, 0, sizeof(close_adapter));
        close_adapter.adapter_handle = adapter;
        ioctl(fd, LX_DXCLOSEADAPTER, &close_adapter);
    }
    close(fd);
}

static int create_shared_dxg_resource(int fd, struct d3dkmthandle device,
                                      uint32_t bytes,
                                      int *shared_fd_out,
                                      struct d3dkmthandle *resource_out,
                                      struct d3dkmt_queryresourceinfofromnthandle *query_out)
{
    struct d3dddi_allocationinfo2 allocation_info;
    struct d3dkmt_createallocation create_allocation;
    struct d3dkmt_createstandardallocation standard_allocation;
    struct d3dkmt_shareobjects share_objects;
    struct d3dkmthandle objects[1];
    uint64 shared_handle = 0;

    memset(&allocation_info, 0, sizeof(allocation_info));
    memset(&create_allocation, 0, sizeof(create_allocation));
    memset(&standard_allocation, 0, sizeof(standard_allocation));
    create_allocation.device = device;
    create_allocation.alloc_count = 1;
    create_allocation.allocation_info = (uint64)&allocation_info;
    standard_allocation.type = _D3DKMT_STANDARDALLOCATIONTYPE_CROSSADAPTER;
    standard_allocation.existing_heap_data.size = bytes;
    create_allocation.standard_allocation = (uint64)&standard_allocation;
    create_allocation.flags.create_resource = 1;
    create_allocation.flags.create_shared = 1;
    create_allocation.flags.nt_security_sharing = 1;
    create_allocation.flags.cross_adapter = 1;
    create_allocation.flags.standard_allocation = 1;
    if (ioctl(fd, LX_DXCREATEALLOCATION, &create_allocation) < 0 ||
        create_allocation.resource.v == 0 ||
        allocation_info.allocation.v == 0) {
        fprintf(stderr,
                "d3d12sharedsmoke: createallocation failed resource=0x%x allocation=0x%x errno=%d\n",
                create_allocation.resource.v, allocation_info.allocation.v,
                errno);
        return -1;
    }

    memset(&share_objects, 0, sizeof(share_objects));
    objects[0] = create_allocation.resource;
    share_objects.object_count = 1;
    share_objects.objects = (uint64)objects;
    share_objects.shared_handle = (uint64)&shared_handle;
    if (ioctl(fd, LX_DXSHAREOBJECTS, &share_objects) < 0 ||
        shared_handle == 0) {
        struct d3dkmt_destroyallocation2 destroy_allocation;

        fprintf(stderr,
                "d3d12sharedsmoke: shareobjects failed resource=0x%x errno=%d\n",
                create_allocation.resource.v, errno);
        memset(&destroy_allocation, 0, sizeof(destroy_allocation));
        destroy_allocation.device = device;
        destroy_allocation.resource = create_allocation.resource;
        destroy_allocation.flags.assume_not_in_use = 1;
        ioctl(fd, LX_DXDESTROYALLOCATION2, &destroy_allocation);
        return -1;
    }

    memset(query_out, 0, sizeof(*query_out));
    query_out->device = device;
    query_out->nt_handle = shared_handle;
    if (ioctl(fd, LX_DXQUERYRESOURCEINFOFROMNTHANDLE, query_out) < 0 ||
        query_out->allocation_count == 0 ||
        query_out->total_priv_drv_data_size == 0) {
        struct d3dkmt_destroyallocation2 destroy_allocation;

        fprintf(stderr,
                "d3d12sharedsmoke: query shared resource failed allocations=%u total_priv=%u errno=%d\n",
                query_out->allocation_count, query_out->total_priv_drv_data_size,
                errno);
        close((int)shared_handle);
        memset(&destroy_allocation, 0, sizeof(destroy_allocation));
        destroy_allocation.device = device;
        destroy_allocation.resource = create_allocation.resource;
        destroy_allocation.flags.assume_not_in_use = 1;
        ioctl(fd, LX_DXDESTROYALLOCATION2, &destroy_allocation);
        return -1;
    }

    *shared_fd_out = (int)shared_handle;
    *resource_out = create_allocation.resource;
    printf("d3d12sharedsmoke: shared resource=0x%x fd=%d allocations=%u total_priv=%u\n",
           resource_out->v, *shared_fd_out, query_out->allocation_count,
           query_out->total_priv_drv_data_size);
    return 0;
}

static int buffer_has_nonzero_byte(const void *buffer, size_t size)
{
    const uint8_t *bytes = (const uint8_t *)buffer;

    for (size_t i = 0; i < size; i++) {
        if (bytes[i] != 0)
            return 1;
    }
    return 0;
}

static int validate_direct_open_shared_resource(
    int fd, struct d3dkmthandle device, int shared_fd,
    const struct d3dkmt_queryresourceinfofromnthandle *query)
{
    struct d3dkmt_openresourcefromnthandle open_resource;
    struct d3dkmt_destroyallocation2 destroy_allocation;
    struct d3dddi_openallocationinfo2 *open_alloc = NULL;
    void *runtime_data = NULL;
    void *resource_data = NULL;
    void *total_data = NULL;
    uint32_t allocation_count = query->allocation_count;
    uintptr_t total_base;
    uintptr_t total_end;
    uintptr_t first_offset = 0;
    uint32_t first_size = 0;
    int copied_priv = 0;
    int ret = -1;

    memset(&open_resource, 0, sizeof(open_resource));
    if (allocation_count == 0 || allocation_count > 64 ||
        query->total_priv_drv_data_size == 0) {
        fprintf(stderr,
                "d3d12sharedsmoke: direct open shared resource invalid query allocations=%u total_priv=%u\n",
                allocation_count, query->total_priv_drv_data_size);
        return -1;
    }

    open_alloc = calloc(allocation_count, sizeof(open_alloc[0]));
    if (query->private_runtime_data_size != 0)
        runtime_data = calloc(1, query->private_runtime_data_size);
    if (query->resource_priv_drv_data_size != 0)
        resource_data = calloc(1, query->resource_priv_drv_data_size);
    total_data = calloc(1, query->total_priv_drv_data_size);
    if (!open_alloc ||
        (query->private_runtime_data_size != 0 && !runtime_data) ||
        (query->resource_priv_drv_data_size != 0 && !resource_data) ||
        !total_data) {
        fprintf(stderr,
                "d3d12sharedsmoke: direct open shared resource alloc failed allocations=%u runtime=%u resource_priv=%u total_priv=%u\n",
                allocation_count, query->private_runtime_data_size,
                query->resource_priv_drv_data_size,
                query->total_priv_drv_data_size);
        goto out;
    }

    open_resource.device = device;
    open_resource.nt_handle = (uint64)(uint32_t)shared_fd;
    open_resource.allocation_count = allocation_count;
    open_resource.open_alloc_info = (uint64)open_alloc;
    open_resource.private_runtime_data_size =
        (int)query->private_runtime_data_size;
    open_resource.private_runtime_data = (uint64)runtime_data;
    open_resource.resource_priv_drv_data_size =
        query->resource_priv_drv_data_size;
    open_resource.resource_priv_drv_data = (uint64)resource_data;
    open_resource.total_priv_drv_data_size =
        query->total_priv_drv_data_size;
    open_resource.total_priv_drv_data = (uint64)total_data;
    if (ioctl(fd, LX_DXOPENRESOURCEFROMNTHANDLE, &open_resource) < 0 ||
        open_resource.resource.v == 0) {
        fprintf(stderr,
                "d3d12sharedsmoke: direct open shared resource failed fd=%d resource=0x%x allocations=%u total_priv=%u errno=%d\n",
                shared_fd, open_resource.resource.v, allocation_count,
                query->total_priv_drv_data_size, errno);
        goto out;
    }

    total_base = (uintptr_t)total_data;
    total_end = total_base + query->total_priv_drv_data_size;
    for (uint32_t i = 0; i < allocation_count; i++) {
        uintptr_t priv = (uintptr_t)open_alloc[i].priv_drv_data;
        uintptr_t priv_end;

        if (open_alloc[i].allocation.v == 0 ||
            open_alloc[i].priv_drv_data_size == 0 ||
            priv < total_base || priv > total_end) {
            fprintf(stderr,
                    "d3d12sharedsmoke: direct open shared resource allocation metadata invalid index=%u allocation=0x%x priv=0x%lx size=%u total_priv=%u\n",
                    i, open_alloc[i].allocation.v,
                    (unsigned long)priv, open_alloc[i].priv_drv_data_size,
                    query->total_priv_drv_data_size);
            goto out;
        }
        priv_end = priv + open_alloc[i].priv_drv_data_size;
        if (priv_end < priv || priv_end > total_end) {
            fprintf(stderr,
                    "d3d12sharedsmoke: direct open shared resource private blob out of range index=%u offset=%lu size=%u total_priv=%u\n",
                    i, (unsigned long)(priv - total_base),
                    open_alloc[i].priv_drv_data_size,
                    query->total_priv_drv_data_size);
            goto out;
        }
        if (!buffer_has_nonzero_byte((const void *)priv,
                                     open_alloc[i].priv_drv_data_size)) {
            fprintf(stderr,
                    "d3d12sharedsmoke: direct open shared resource private blob empty index=%u offset=%lu size=%u\n",
                    i, (unsigned long)(priv - total_base),
                    open_alloc[i].priv_drv_data_size);
            goto out;
        }
        if (i == 0) {
            first_offset = priv - total_base;
            first_size = open_alloc[i].priv_drv_data_size;
        }
        copied_priv = 1;
    }

    printf("d3d12sharedsmoke: direct open shared resource ok resource=0x%x allocations=%u total_priv=%u first_allocation=0x%x first_priv_offset=%lu first_priv_size=%u copied_priv=%d\n",
           open_resource.resource.v, allocation_count,
           query->total_priv_drv_data_size, open_alloc[0].allocation.v,
           (unsigned long)first_offset, first_size, copied_priv);
    ret = 0;

out:
    if (open_resource.resource.v != 0) {
        memset(&destroy_allocation, 0, sizeof(destroy_allocation));
        destroy_allocation.device = device;
        destroy_allocation.resource = open_resource.resource;
        destroy_allocation.flags.assume_not_in_use = 1;
        if (ioctl(fd, LX_DXDESTROYALLOCATION2, &destroy_allocation) < 0) {
            fprintf(stderr,
                    "d3d12sharedsmoke: direct open shared resource destroy failed resource=0x%x errno=%d\n",
                    open_resource.resource.v, errno);
            ret = -1;
        }
    }
    free(open_alloc);
    free(runtime_data);
    free(resource_data);
    free(total_data);
    return ret;
}

static int validate_direct_open_shared_fence(
    int fd, struct d3dkmthandle device, int fence_fd,
    uint32_t flags, const char *device_role,
    struct d3dkmthandle *sync_out)
{
    struct d3dkmt_opensyncobjectfromnthandle2 open_sync;
    int ret = -1;

    if (sync_out)
        memset(sync_out, 0, sizeof(*sync_out));
    if (fd < 0 || device.v == 0 || fence_fd < 0) {
        errno = EINVAL;
        printf("d3d12sharedsmoke: runtime direct-dxg-fence-import-result device_role=%s attempted=0 ok=0 reason=invalid_args fd=%d device=0x%x fence_fd=%d errno=%d sync=0x0 fence_cpu=0x0 fence_gpu=0x0 flags=0x%x shared=%u nt_security=%u no_signal=%u\n",
               device_role ? device_role : "unknown", fd, device.v,
               fence_fd, errno, flags, flags & 1, (flags >> 1) & 1,
               (flags >> 4) & 1);
        return -1;
    }

    memset(&open_sync, 0, sizeof(open_sync));
    open_sync.device = device;
    open_sync.nt_handle = (uint64)(uint32_t)fence_fd;
    open_sync.flags.value = flags;
    open_sync.monitored_fence.engine_affinity = 0;
    if (ioctl(fd, LX_DXOPENSYNCOBJECTFROMNTHANDLE2, &open_sync) < 0) {
        int ioctl_errno = errno ? errno : EIO;

        printf("d3d12sharedsmoke: runtime direct-dxg-fence-import-result device_role=%s attempted=1 ok=0 reason=ioctl_failed ioctl=LX_DXOPENSYNCOBJECTFROMNTHANDLE2 fd=%d device=0x%x fence_fd=%d errno=%d (%s) sync=0x%x fence_cpu=0x%lx fence_gpu=0x%lx flags=0x%x shared=%u nt_security=%u no_signal=%u\n",
               device_role ? device_role : "unknown", fd, device.v,
               fence_fd, ioctl_errno, strerror(ioctl_errno),
               open_sync.sync_object.v,
               (unsigned long)open_sync.monitored_fence.fence_value_cpu_va,
               (unsigned long)open_sync.monitored_fence.fence_value_gpu_va,
               open_sync.flags.value, open_sync.flags.shared,
               open_sync.flags.nt_security_sharing,
               open_sync.flags.no_signal);
        errno = ioctl_errno;
        return -1;
    }

    if (open_sync.sync_object.v == 0 ||
        open_sync.monitored_fence.fence_value_cpu_va == 0 ||
        open_sync.monitored_fence.fence_value_gpu_va == 0) {
        printf("d3d12sharedsmoke: runtime direct-dxg-fence-import-result device_role=%s attempted=1 ok=0 reason=incomplete_result ioctl=LX_DXOPENSYNCOBJECTFROMNTHANDLE2 fd=%d device=0x%x fence_fd=%d errno=0 sync=0x%x fence_cpu=0x%lx fence_gpu=0x%lx flags=0x%x shared=%u nt_security=%u no_signal=%u\n",
               device_role ? device_role : "unknown", fd, device.v,
               fence_fd, open_sync.sync_object.v,
               (unsigned long)open_sync.monitored_fence.fence_value_cpu_va,
               (unsigned long)open_sync.monitored_fence.fence_value_gpu_va,
               open_sync.flags.value, open_sync.flags.shared,
               open_sync.flags.nt_security_sharing,
               open_sync.flags.no_signal);
        errno = EIO;
        goto out_destroy;
    }

    if (sync_out)
        *sync_out = open_sync.sync_object;
    printf("d3d12sharedsmoke: runtime direct-dxg-fence-import-result device_role=%s attempted=1 ok=1 reason=ok ioctl=LX_DXOPENSYNCOBJECTFROMNTHANDLE2 fd=%d device=0x%x fence_fd=%d errno=0 sync=0x%x fence_cpu=0x%lx fence_gpu=0x%lx flags=0x%x shared=%u nt_security=%u no_signal=%u\n",
           device_role ? device_role : "unknown", fd, device.v, fence_fd,
           open_sync.sync_object.v,
           (unsigned long)open_sync.monitored_fence.fence_value_cpu_va,
           (unsigned long)open_sync.monitored_fence.fence_value_gpu_va,
           open_sync.flags.value, open_sync.flags.shared,
           open_sync.flags.nt_security_sharing,
           open_sync.flags.no_signal);
    ret = 0;

out_destroy:
    if (open_sync.sync_object.v != 0) {
        struct d3dkmt_destroysynchronizationobject destroy_sync;

        memset(&destroy_sync, 0, sizeof(destroy_sync));
        destroy_sync.sync_object = open_sync.sync_object;
        if (ioctl(fd, LX_DXDESTROYSYNCHRONIZATIONOBJECT,
                  &destroy_sync) < 0) {
            int destroy_errno = errno ? errno : EIO;

            printf("d3d12sharedsmoke: runtime direct-dxg-fence-import-destroy sync=0x%x errno=%d (%s)\n",
                   open_sync.sync_object.v, destroy_errno,
                   strerror(destroy_errno));
        }
    }
    return ret;
}

static int create_shared_dxg_fence(int fd, struct d3dkmthandle device,
                                   int *shared_fd_out,
                                   struct d3dkmthandle *sync_out)
{
    struct d3dkmt_createsynchronizationobject2 create_sync;
    struct d3dkmt_shareobjects share_objects;
    struct d3dkmthandle objects[1];
    uint64 shared_handle = 0;

    memset(&create_sync, 0, sizeof(create_sync));
    create_sync.device = device;
    create_sync.info.type = _D3DDDI_MONITORED_FENCE;
    create_sync.info.flags.shared = 1;
    create_sync.info.flags.nt_security_sharing = 1;
    create_sync.info.monitored_fence.initial_fence_value = 1;
    if (ioctl(fd, LX_DXCREATESYNCHRONIZATIONOBJECT, &create_sync) < 0 ||
        create_sync.sync_object.v == 0) {
        fprintf(stderr,
                "d3d12sharedsmoke: create monitored fence failed sync=0x%x errno=%d\n",
                create_sync.sync_object.v, errno);
        return -1;
    }

    memset(&share_objects, 0, sizeof(share_objects));
    objects[0] = create_sync.sync_object;
    share_objects.object_count = 1;
    share_objects.objects = (uint64)objects;
    share_objects.shared_handle = (uint64)&shared_handle;
    if (ioctl(fd, LX_DXSHAREOBJECTS, &share_objects) < 0 ||
        shared_handle == 0) {
        struct d3dkmt_destroysynchronizationobject destroy_sync;

        fprintf(stderr,
                "d3d12sharedsmoke: share monitored fence failed sync=0x%x errno=%d\n",
                create_sync.sync_object.v, errno);
        memset(&destroy_sync, 0, sizeof(destroy_sync));
        destroy_sync.sync_object = create_sync.sync_object;
        ioctl(fd, LX_DXDESTROYSYNCHRONIZATIONOBJECT, &destroy_sync);
        return -1;
    }

    *shared_fd_out = (int)shared_handle;
    *sync_out = create_sync.sync_object;
    printf("d3d12sharedsmoke: shared fence=0x%x fd=%d cpu=0x%lx gpu=0x%lx\n",
           sync_out->v, *shared_fd_out,
           create_sync.info.monitored_fence.fence_cpu_virtual_address,
           create_sync.info.monitored_fence.fence_gpu_virtual_address);
    return 0;
}

static int create_dxg_syncfile_acquire(int fd, struct d3dkmthandle device,
                                       int *sync_file_fd_out,
                                       struct d3dkmthandle *sync_out)
{
    struct d3dkmt_createsynchronizationobject2 create_sync;
    struct d3dkmt_createsyncfile create_sync_file;

    memset(&create_sync, 0, sizeof(create_sync));
    create_sync.device = device;
    create_sync.info.type = _D3DDDI_MONITORED_FENCE;
    create_sync.info.flags.shared = 1;
    create_sync.info.flags.nt_security_sharing = 1;
    create_sync.info.monitored_fence.initial_fence_value = 1;
    if (ioctl(fd, LX_DXCREATESYNCHRONIZATIONOBJECT, &create_sync) < 0 ||
        create_sync.sync_object.v == 0) {
        fprintf(stderr,
                "d3d12sharedsmoke: create sync-file monitored fence failed sync=0x%x errno=%d\n",
                create_sync.sync_object.v, errno);
        return -1;
    }

    memset(&create_sync_file, 0, sizeof(create_sync_file));
    create_sync_file.device = device;
    create_sync_file.monitored_fence = create_sync.sync_object;
    create_sync_file.fence_value = 1;
    if (ioctl(fd, LX_DXCREATESYNCFILE, &create_sync_file) < 0 ||
        create_sync_file.sync_file_handle == 0) {
        struct d3dkmt_destroysynchronizationobject destroy_sync;

        fprintf(stderr,
                "d3d12sharedsmoke: create DXG sync-file acquire failed sync=0x%x fd=%lu errno=%d\n",
                create_sync.sync_object.v,
                (unsigned long)create_sync_file.sync_file_handle,
                errno);
        memset(&destroy_sync, 0, sizeof(destroy_sync));
        destroy_sync.sync_object = create_sync.sync_object;
        ioctl(fd, LX_DXDESTROYSYNCHRONIZATIONOBJECT, &destroy_sync);
        return -1;
    }

    *sync_file_fd_out = (int)create_sync_file.sync_file_handle;
    *sync_out = create_sync.sync_object;
    printf("d3d12sharedsmoke: dxg sync-file acquire fd=%d sync=0x%x target=%lu cpu=0x%lx gpu=0x%lx\n",
           *sync_file_fd_out, sync_out->v,
           (unsigned long)create_sync_file.fence_value,
           create_sync.info.monitored_fence.fence_cpu_virtual_address,
           create_sync.info.monitored_fence.fence_gpu_virtual_address);
    return 0;
}

static int validate_direct_open_dxg_syncfile(
    int fd, struct d3dkmthandle device, int sync_file_fd,
    const char *device_role, struct d3dkmthandle *sync_out)
{
    struct d3dkmt_opensyncobjectfromsyncfile open_sync;
    int saved_errno;

    memset(&open_sync, 0, sizeof(open_sync));
    open_sync.device = device;
    open_sync.sync_file_handle = (uint64)(uint32_t)sync_file_fd;
    if (ioctl(fd, LX_DXOPENSYNCOBJECTFROMSYNCFILE, &open_sync) < 0) {
        saved_errno = errno ? errno : EIO;
        printf("d3d12sharedsmoke: runtime dxg-syncfile-import-result device_role=%s attempted=1 ok=0 reason=ioctl_failed ioctl=LX_DXOPENSYNCOBJECTFROMSYNCFILE fd=%d device=0x%x sync_file_fd=%d errno=%d (%s) sync=0x%x target=%lu fence_cpu=0x%lx fence_gpu=0x%lx\n",
               device_role ? device_role : "unknown", fd, device.v,
               sync_file_fd, saved_errno, strerror(saved_errno),
               open_sync.syncobj.v, (unsigned long)open_sync.fence_value,
               (unsigned long)open_sync.fence_value_cpu_va,
               (unsigned long)open_sync.fence_value_gpu_va);
        errno = saved_errno;
        return -1;
    }
    if (open_sync.syncobj.v == 0 || open_sync.fence_value != 1 ||
        open_sync.fence_value_cpu_va == 0 ||
        open_sync.fence_value_gpu_va == 0) {
        saved_errno = EINVAL;
        printf("d3d12sharedsmoke: runtime dxg-syncfile-import-result device_role=%s attempted=1 ok=0 reason=incomplete_result ioctl=LX_DXOPENSYNCOBJECTFROMSYNCFILE fd=%d device=0x%x sync_file_fd=%d errno=0 sync=0x%x target=%lu fence_cpu=0x%lx fence_gpu=0x%lx\n",
               device_role ? device_role : "unknown", fd, device.v,
               sync_file_fd, open_sync.syncobj.v,
               (unsigned long)open_sync.fence_value,
               (unsigned long)open_sync.fence_value_cpu_va,
               (unsigned long)open_sync.fence_value_gpu_va);
        if (open_sync.syncobj.v != 0) {
            struct d3dkmt_destroysynchronizationobject destroy_sync;

            memset(&destroy_sync, 0, sizeof(destroy_sync));
            destroy_sync.sync_object = open_sync.syncobj;
            ioctl(fd, LX_DXDESTROYSYNCHRONIZATIONOBJECT, &destroy_sync);
        }
        errno = saved_errno;
        return -1;
    }

    if (sync_out)
        *sync_out = open_sync.syncobj;
    printf("d3d12sharedsmoke: runtime dxg-syncfile-import-result device_role=%s attempted=1 ok=1 reason=ok ioctl=LX_DXOPENSYNCOBJECTFROMSYNCFILE fd=%d device=0x%x sync_file_fd=%d errno=0 sync=0x%x target=%lu fence_cpu=0x%lx fence_gpu=0x%lx\n",
           device_role ? device_role : "unknown", fd, device.v,
           sync_file_fd, open_sync.syncobj.v,
           (unsigned long)open_sync.fence_value,
           (unsigned long)open_sync.fence_value_cpu_va,
           (unsigned long)open_sync.fence_value_gpu_va);
    {
        struct d3dkmt_destroysynchronizationobject destroy_sync;

        memset(&destroy_sync, 0, sizeof(destroy_sync));
        destroy_sync.sync_object = open_sync.syncobj;
        ioctl(fd, LX_DXDESTROYSYNCHRONIZATIONOBJECT, &destroy_sync);
    }
    return 0;
}

static int query_shared_resource_info(int fd, struct d3dkmthandle device,
                                      int shared_fd,
                                      struct d3dkmt_queryresourceinfofromnthandle *query_out)
{
    memset(query_out, 0, sizeof(*query_out));
    query_out->device = device;
    query_out->nt_handle = (uint64)(uint32_t)shared_fd;
    if (ioctl(fd, LX_DXQUERYRESOURCEINFOFROMNTHANDLE, query_out) < 0 ||
        query_out->allocation_count == 0 ||
        query_out->total_priv_drv_data_size == 0) {
        fprintf(stderr,
                "d3d12sharedsmoke: query shared resource failed fd=%d allocations=%u total_priv=%u errno=%d\n",
                shared_fd, query_out->allocation_count,
                query_out->total_priv_drv_data_size, errno);
        return -1;
    }
    return 0;
}

static void d3d12_runtime_destroy(struct d3d12_runtime *rt)
{
    if (!rt)
        return;
    if (rt->opened_fence)
        ID3D12Fence_Release(rt->opened_fence);
    if (rt->import_device)
        ID3D12Device_Release(rt->import_device);
    if (rt->fence)
        ID3D12Fence_Release(rt->fence);
    if (rt->opened_resource)
        ID3D12Resource_Release(rt->opened_resource);
    if (rt->resource)
        ID3D12Resource_Release(rt->resource);
    if (rt->opened_heap)
        ID3D12Heap_Release(rt->opened_heap);
    if (rt->heap)
        ID3D12Heap_Release(rt->heap);
    if (rt->rtv_heap)
        ID3D12DescriptorHeap_Release(rt->rtv_heap);
    if (rt->command_list)
        ID3D12GraphicsCommandList_Release(rt->command_list);
    if (rt->allocator)
        ID3D12CommandAllocator_Release(rt->allocator);
    if (rt->queue)
        ID3D12CommandQueue_Release(rt->queue);
    if (rt->device)
        ID3D12Device_Release(rt->device);
    close_handle_fd(&rt->heap_handle);
    close_handle_fd(&rt->resource_handle);
    close_handle_fd(&rt->fence_handle);
    memset(rt, 0, sizeof(*rt));
}

static int wait_for_fence_value(ID3D12Fence *fence, uint64_t value)
{
    int64_t deadline = now_ms() + 3000;

    while (now_ms() < deadline) {
        uint64_t completed = ID3D12Fence_GetCompletedValue(fence);

        if (completed >= value)
            return 0;
        usleep(10000);
    }
    return -1;
}

static void d3d12_runtime_print_phase_result(
    const char *label, const char *stage, const char *handle_kind,
    int create_device_success, int create_shared_handle_reached,
    HRESULT hr, HRESULT device_reason, HANDLE handle,
    struct winluid adapter_luid, const char *present_claim);
static void d3d12_report_dxcore_admission_without_dxg_luid(
    const char *label, const char *source, int dxg_errno);

static int d3d12_create_device_for_dxg_luid(struct winluid *luid_out,
                                            ID3D12Device **device_out,
                                            const char *diag_label)
{
    struct winluid dxg_luid;
    struct xv6_dxcore_adapter_factory *factory = NULL;
    struct xv6_dxcore_adapter *dxcore_adapter = NULL;
    LUID luid;
    char luid_text[32];
    HRESULT hr;
    int ret = -1;

    if (device_out)
        *device_out = NULL;
    if (!device_out) {
        errno = EINVAL;
        return -1;
    }

    memset(&dxg_luid, 0, sizeof(dxg_luid));
    if (query_first_dxg_adapter_luid(&dxg_luid) != 0) {
        int dxg_errno = errno ? errno : ENODEV;

        printf("d3d12sharedsmoke: dxg-luid-discovery source=runtime-direct path=/dev/dxg ioctl=LX_DXENUMADAPTERS2 result=failed errno=%d (%s) fallback=dxcore-admission-report-only\n",
               dxg_errno, strerror(dxg_errno));
        d3d12_runtime_print_phase_result(
            diag_label, "userspace-admission", "none", 0, 0,
            (HRESULT)0x80004005, 0, NULL, dxg_luid, "export_only");
        d3d12_report_dxcore_admission_without_dxg_luid(
            diag_label, "runtime-direct", dxg_errno);
        fprintf(stderr,
                "d3d12sharedsmoke: could not obtain DXG adapter LUID for D3D12 runtime device\n");
        return -1;
    }
    format_winluid_text(luid_text, sizeof(luid_text), dxg_luid);
    fprintf(stderr,
            "d3d12sharedsmoke: selecting D3D12 adapter by DXG LUID %s low=0x%08x high=0x%08x\n",
            luid_text, dxg_luid.a, dxg_luid.b);

    hr = DXCoreCreateAdapterFactory(&XV6_IID_IDXCoreAdapterFactory,
                                    (void **)&factory);
    if (FAILED(hr) || !factory) {
        d3d12_runtime_print_phase_result(
            diag_label, "userspace-admission", "none", 0, 0, hr, 0,
            NULL, dxg_luid, "export_only");
        fprintf(stderr,
                "d3d12sharedsmoke: DXCoreCreateAdapterFactory failed hr=0x%lx\n",
                (unsigned long)hr);
        return -1;
    }

    memset(&luid, 0, sizeof(luid));
    luid.LowPart = dxg_luid.a;
    luid.HighPart = (LONG)dxg_luid.b;
    hr = factory->lpVtbl->GetAdapterByLuid(
        factory, &luid, &XV6_IID_IDXCoreAdapter, (void **)&dxcore_adapter);
    if (FAILED(hr) || !dxcore_adapter) {
        d3d12_runtime_print_phase_result(
            diag_label, "userspace-admission", "none", 0, 0, hr, 0,
            NULL, dxg_luid, "export_only");
        fprintf(stderr,
                "d3d12sharedsmoke: DXCore GetAdapterByLuid luid=%s low=0x%08x high=0x%08x failed hr=0x%lx adapter=%p\n",
                luid_text, dxg_luid.a, dxg_luid.b, (unsigned long)hr,
                (void *)dxcore_adapter);
        goto out;
    }

    hr = D3D12CreateDevice((IUnknown *)dxcore_adapter,
                           D3D_FEATURE_LEVEL_11_0,
                           &IID_ID3D12Device, (void **)device_out);
    if (FAILED(hr) || !*device_out) {
        d3d12_runtime_print_phase_result(
            diag_label, "create-device", "none", 0, 0, hr, 0, NULL,
            dxg_luid, "export_only");
        fprintf(stderr,
                "d3d12sharedsmoke: D3D12CreateDevice(dxcore luid=%s low=0x%08x high=0x%08x) failed hr=0x%lx\n",
                luid_text, dxg_luid.a, dxg_luid.b,
                (unsigned long)hr);
        goto out;
    }

    if (luid_out)
        *luid_out = dxg_luid;
    d3d12_runtime_print_phase_result(
        diag_label, "create-device", "none", 1, 0, hr, 0, NULL,
        dxg_luid, "export_only");
    ret = 0;

out:
    if (dxcore_adapter)
        dxcore_adapter->lpVtbl->Release(dxcore_adapter);
    if (factory)
        factory->lpVtbl->Release(factory);
    return ret;
}

static int d3d12_create_device_from_wsl_adapter_list(
    struct winluid *luid_out, ID3D12Device **device_out,
    int precheck_dxg_luid, int query_device_luid, int *empty_list_out,
    const char *diag_label)
{
    const GUID attrs[] = { XV6_DXCORE_ADAPTER_ATTRIBUTE_D3D12_GRAPHICS };
    struct xv6_dxcore_adapter_factory *factory = NULL;
    struct xv6_dxcore_adapter_list *list = NULL;
    struct xv6_dxcore_adapter *selected = NULL;
    struct winluid dxg_luid;
    struct winluid selected_winluid;
    char dxg_luid_text[32];
    const char *selected_reason = "none";
    uint32_t count;
    HRESULT hr;
    int have_dxg_luid;
    int saw_candidate_luid = 0;
    int ret = -1;

    if (device_out)
        *device_out = NULL;
    if (empty_list_out)
        *empty_list_out = 0;
    if (!device_out) {
        errno = EINVAL;
        return -1;
    }

    memset(&dxg_luid, 0, sizeof(dxg_luid));
    memset(&selected_winluid, 0, sizeof(selected_winluid));
    have_dxg_luid = 0;
    if (precheck_dxg_luid) {
        if (query_first_dxg_adapter_luid(&dxg_luid) == 0) {
            have_dxg_luid = 1;
        } else {
            int dxg_errno = errno ? errno : ENODEV;

            printf("d3d12sharedsmoke: dxg-luid-discovery source=runtime-wsl-list path=/dev/dxg ioctl=LX_DXENUMADAPTERS2 result=failed errno=%d (%s) fallback=dxcore-admission-report-only\n",
                   dxg_errno, strerror(dxg_errno));
            d3d12_runtime_print_phase_result(
                diag_label, "userspace-admission", "none", 0, 0,
                (HRESULT)0x80004005, 0, NULL, dxg_luid, "export_only");
            d3d12_report_dxcore_admission_without_dxg_luid(
                diag_label, "runtime-wsl-list", dxg_errno);
            return -1;
        }
    }
    format_winluid_text(dxg_luid_text, sizeof(dxg_luid_text), dxg_luid);
    printf("d3d12sharedsmoke: WSL-list dxg_precheck=%u dxg_luid_available=%u dxg_luid=%s low=0x%08x high=0x%08x\n",
           precheck_dxg_luid, have_dxg_luid,
           have_dxg_luid ? dxg_luid_text : "none",
           dxg_luid.a, dxg_luid.b);

    hr = DXCoreCreateAdapterFactory(&XV6_IID_IDXCoreAdapterFactory,
                                    (void **)&factory);
    if (FAILED(hr) || !factory) {
        d3d12_runtime_print_phase_result(
            diag_label, "userspace-admission", "none", 0, 0, hr, 0,
            NULL, dxg_luid, "export_only");
        fprintf(stderr,
                "d3d12sharedsmoke: WSL-list DXCoreCreateAdapterFactory failed hr=0x%lx\n",
                (unsigned long)hr);
        return -1;
    }

    hr = factory->lpVtbl->CreateAdapterList(
        factory, 1, attrs, &XV6_IID_IDXCoreAdapterList, (void **)&list);
    if (FAILED(hr) || !list) {
        d3d12_runtime_print_phase_result(
            diag_label, "userspace-admission", "none", 0, 0, hr, 0,
            NULL, dxg_luid, "export_only");
        fprintf(stderr,
                "d3d12sharedsmoke: WSL-list CreateAdapterList failed hr=0x%lx list=%p\n",
                (unsigned long)hr, (void *)list);
        goto out;
    }

    count = list->lpVtbl->GetAdapterCount(list);
    printf("d3d12sharedsmoke: WSL-list adapter count=%u\n", count);
    if (count == 0) {
        if (empty_list_out)
            *empty_list_out = 1;
        printf("d3d12sharedsmoke: wsl-list count=0\n");
    }
    for (uint32_t i = 0; i < count; i++) {
        struct xv6_dxcore_adapter *candidate = NULL;
        struct xv6_dxcore_hardware_id hwid;
        LUID candidate_luid;
        struct winluid candidate_winluid;
        char candidate_luid_text[32];
        int have_hwid = 0;
        int have_candidate_luid = 0;
        int dxg_luid_match = 0;
        int vendor_valid = 0;
        int hwid_shifted = 0;
        int candidate_is_nvidia = 0;

        hr = list->lpVtbl->GetAdapter(list, i, &XV6_IID_IDXCoreAdapter,
                                      (void **)&candidate);
        if (FAILED(hr) || !candidate)
            continue;

        memset(&candidate_luid, 0, sizeof(candidate_luid));
        memset(&candidate_winluid, 0, sizeof(candidate_winluid));
        if (precheck_dxg_luid && candidate->lpVtbl->GetProperty &&
            SUCCEEDED(candidate->lpVtbl->GetProperty(
                candidate, XV6_DXCORE_ADAPTER_PROPERTY_INSTANCE_LUID,
                sizeof(candidate_luid), &candidate_luid))) {
            candidate_winluid.a = (uint32_t)candidate_luid.LowPart;
            candidate_winluid.b = (uint32_t)candidate_luid.HighPart;
            have_candidate_luid = 1;
            saw_candidate_luid = 1;
        }
        format_winluid_text(candidate_luid_text, sizeof(candidate_luid_text),
                            candidate_winluid);
        if (have_dxg_luid && have_candidate_luid &&
            candidate_winluid.a == dxg_luid.a &&
            candidate_winluid.b == dxg_luid.b)
            dxg_luid_match = 1;

        memset(&hwid, 0, sizeof(hwid));
        if (candidate->lpVtbl->GetProperty &&
            SUCCEEDED(candidate->lpVtbl->GetProperty(
                candidate, XV6_DXCORE_ADAPTER_PROPERTY_HARDWARE_ID,
                sizeof(hwid), &hwid)))
            have_hwid = 1;
        vendor_valid = have_hwid && hwid.vendor_id != 0 &&
            hwid.vendor_id <= 0xffff;
        hwid_shifted = have_hwid && !vendor_valid &&
            (((hwid.vendor_id & 0xffff) == 0 && (hwid.vendor_id >> 16) != 0) ||
             ((hwid.device_id & 0xffff) == 0 && (hwid.device_id >> 16) != 0));
        candidate_is_nvidia = vendor_valid && hwid.vendor_id == 0x10de;

        printf("d3d12sharedsmoke: WSL-list adapter[%u] luid=%s have_luid=%u dxg_match=%u vendor=0x%08x device=0x%08x hwid=%u hwid_shifted=%u vendor_valid=%u\n",
               i, have_candidate_luid ? candidate_luid_text : "none",
               have_candidate_luid, dxg_luid_match,
               have_hwid ? hwid.vendor_id : 0,
               have_hwid ? hwid.device_id : 0, have_hwid, hwid_shifted,
               vendor_valid);
        if (dxg_luid_match) {
            if (selected)
                selected->lpVtbl->Release(selected);
            selected = candidate;
            selected_winluid = candidate_winluid;
            selected_reason = "dxg_luid";
            break;
        }
        if (!precheck_dxg_luid || !have_dxg_luid ||
            !have_candidate_luid) {
            if (candidate_is_nvidia) {
                if (selected)
                    selected->lpVtbl->Release(selected);
                selected = candidate;
                selected_winluid = candidate_winluid;
                selected_reason = "valid_vendor";
                break;
            }
            if (!selected) {
                selected = candidate;
                selected_winluid = candidate_winluid;
                selected_reason = have_dxg_luid ?
                    "first_no_luid" : "first_no_dxg_luid";
            } else {
                candidate->lpVtbl->Release(candidate);
            }
            continue;
        }
        candidate->lpVtbl->Release(candidate);
    }

    if (selected && precheck_dxg_luid && have_dxg_luid &&
        saw_candidate_luid &&
        strcmp(selected_reason, "dxg_luid") != 0) {
        selected->lpVtbl->Release(selected);
        selected = NULL;
        fprintf(stderr,
                "d3d12sharedsmoke: WSL-list no adapter matched dxg LUID %s; refusing silent cross-adapter selection\n",
                dxg_luid_text);
    }
    if (!selected) {
        d3d12_runtime_print_phase_result(
            diag_label, "userspace-admission", "none", 0, 0,
            (HRESULT)0x80004005, 0, NULL, dxg_luid, "export_only");
        if (count == 0)
            printf("d3d12sharedsmoke: WSL-list found no D3D12 adapter count=0\n");
        else
            fprintf(stderr,
                    "d3d12sharedsmoke: WSL-list found no D3D12 adapter\n");
        goto out;
    }
    printf("d3d12sharedsmoke: WSL-list selected adapter reason=%s\n",
           selected_reason);

    hr = D3D12CreateDevice((IUnknown *)selected, D3D_FEATURE_LEVEL_11_0,
                           &IID_ID3D12Device, (void **)device_out);
    if (FAILED(hr) || !*device_out) {
        d3d12_runtime_print_phase_result(
            diag_label, "create-device", "none", 0, 0, hr, 0, NULL,
            have_dxg_luid ? dxg_luid : selected_winluid, "export_only");
        fprintf(stderr,
                "d3d12sharedsmoke: WSL-list D3D12CreateDevice failed hr=0x%lx device=%p\n",
                (unsigned long)hr, device_out ? (void *)*device_out : NULL);
        goto out;
    }

    if (luid_out && query_device_luid) {
        LUID luid = ID3D12Device_GetAdapterLuid(*device_out);

        luid_out->a = (uint32_t)luid.LowPart;
        luid_out->b = (uint32_t)luid.HighPart;
        d3d12_runtime_print_phase_result(
            diag_label, "create-device", "none", 1, 0, hr, 0, NULL,
            *luid_out, "export_only");
    } else if (luid_out && have_dxg_luid &&
               strcmp(selected_reason, "dxg_luid") == 0) {
        *luid_out = dxg_luid;
        d3d12_runtime_print_phase_result(
            diag_label, "create-device", "none", 1, 0, hr, 0, NULL,
            *luid_out, "export_only");
    } else {
        d3d12_runtime_print_phase_result(
            diag_label, "create-device", "none", 1, 0, hr, 0, NULL,
            have_dxg_luid ? dxg_luid : selected_winluid, "export_only");
    }
    ret = 0;

out:
    if (selected)
        selected->lpVtbl->Release(selected);
    if (list)
        list->lpVtbl->Release(list);
    if (factory)
        factory->lpVtbl->Release(factory);
    return ret;
}

static int winluid_equal(struct winluid a, struct winluid b)
{
    return a.a == b.a && a.b == b.b;
}

struct device_admission_qai_probe_result {
    int attempted;
    int skipped;
    int rc;
    int err;
    uint32 requested_size;
    uint32 result_size;
};

struct device_admission_qai_route_summary {
    int have_luid;
    struct winluid luid;
    int device_luid_match;
    struct device_admission_qai_probe_result generic_type55;
    struct device_admission_qai_probe_result generic_type0;
    struct device_admission_qai_probe_result generic_type27;
    struct device_admission_qai_probe_result wsl_type55;
    struct device_admission_qai_probe_result wsl_type0;
    struct device_admission_qai_probe_result wsl_type27;
};

static const char *qai_probe_status(
    const struct device_admission_qai_probe_result *result)
{
    if (!result || !result->attempted)
        return "NA";
    if (result->skipped)
        return "SKIP";
    return result->rc < 0 ? "FAIL" : "PASS";
}

static int qai_probe_einval(
    const struct device_admission_qai_probe_result *result)
{
    return result && result->attempted && !result->skipped &&
        result->rc < 0 && result->err == EINVAL;
}

static void device_admission_qai_record_probe(
    struct device_admission_qai_route_summary *summary, const char *probe_class,
    uint32 type, const struct device_admission_qai_probe_result *result)
{
    struct device_admission_qai_probe_result *slot = NULL;

    if (!summary || !probe_class || !result)
        return;
    if (strcmp(probe_class, "generic") == 0) {
        if (type == 55)
            slot = &summary->generic_type55;
        else if (type == 0)
            slot = &summary->generic_type0;
        else if (type == 27)
            slot = &summary->generic_type27;
    } else if (strcmp(probe_class, "wsl-shaped") == 0) {
        if (type == 55)
            slot = &summary->wsl_type55;
        else if (type == 0)
            slot = &summary->wsl_type0;
        else if (type == 27)
            slot = &summary->wsl_type27;
    }
    if (slot)
        *slot = *result;
}

static void print_qai_probe_summary_fields(
    const char *prefix,
    const struct device_admission_qai_probe_result *result)
{
    printf(" %s_status=%s %s_req=%u %s_result=%u %s_rc=%d %s_errno=%d",
           prefix, qai_probe_status(result),
           prefix, result && result->attempted ? result->requested_size : 0,
           prefix, result && result->attempted ? result->result_size : 0,
           prefix, result && result->attempted ? result->rc : -1,
           prefix, result && result->attempted ? result->err : 0);
}

static void print_device_admission_qai_equivalence_summary(
    struct winluid dxg_luid,
    const struct device_admission_qai_route_summary *direct,
    const struct device_admission_qai_route_summary *list)
{
    char dxg_luid_text[32];
    char direct_luid_text[32];
    char list_luid_text[32];
    int route_luid_match = direct && list && direct->have_luid &&
        list->have_luid && winluid_equal(direct->luid, list->luid);

    format_winluid_text(dxg_luid_text, sizeof(dxg_luid_text), dxg_luid);
    format_winluid_text(direct_luid_text, sizeof(direct_luid_text),
                        direct ? direct->luid : dxg_luid);
    format_winluid_text(list_luid_text, sizeof(list_luid_text),
                        list ? list->luid : dxg_luid);

    printf("d3d12sharedsmoke: qai-equivalence-summary dxg_luid=%s direct_luid=%s direct_luid_match=%u list_luid=%s list_luid_match=%u direct_list_luid_match=%u direct_generic_type55_einval=%u list_generic_type55_einval=%u direct_generic_type0_einval=%u list_generic_type0_einval=%u direct_generic_type27_einval=%u list_generic_type27_einval=%u",
           dxg_luid_text,
           direct && direct->have_luid ? direct_luid_text : "none",
           direct ? direct->device_luid_match : 0,
           list && list->have_luid ? list_luid_text : "none",
           list ? list->device_luid_match : 0, route_luid_match,
           qai_probe_einval(direct ? &direct->generic_type55 : NULL),
           qai_probe_einval(list ? &list->generic_type55 : NULL),
           qai_probe_einval(direct ? &direct->generic_type0 : NULL),
           qai_probe_einval(list ? &list->generic_type0 : NULL),
           qai_probe_einval(direct ? &direct->generic_type27 : NULL),
           qai_probe_einval(list ? &list->generic_type27 : NULL));
    print_qai_probe_summary_fields("direct_generic_type55",
                                   direct ? &direct->generic_type55 : NULL);
    print_qai_probe_summary_fields("direct_wsl_type55",
                                   direct ? &direct->wsl_type55 : NULL);
    print_qai_probe_summary_fields("direct_generic_type27",
                                   direct ? &direct->generic_type27 : NULL);
    print_qai_probe_summary_fields("direct_wsl_type27",
                                   direct ? &direct->wsl_type27 : NULL);
    print_qai_probe_summary_fields("direct_generic_type0",
                                   direct ? &direct->generic_type0 : NULL);
    print_qai_probe_summary_fields("direct_wsl_type0",
                                   direct ? &direct->wsl_type0 : NULL);
    print_qai_probe_summary_fields("list_generic_type55",
                                   list ? &list->generic_type55 : NULL);
    print_qai_probe_summary_fields("list_wsl_type55",
                                   list ? &list->wsl_type55 : NULL);
    print_qai_probe_summary_fields("list_generic_type27",
                                   list ? &list->generic_type27 : NULL);
    print_qai_probe_summary_fields("list_wsl_type27",
                                   list ? &list->wsl_type27 : NULL);
    print_qai_probe_summary_fields("list_generic_type0",
                                   list ? &list->generic_type0 : NULL);
    print_qai_probe_summary_fields("list_wsl_type0",
                                   list ? &list->wsl_type0 : NULL);
    printf("\n");
}

static void print_qai_head16(const unsigned char *data, size_t size)
{
    size_t count = size < 16 ? size : 16;

    for (size_t i = 0; i < count; i++)
        printf("%02x", data[i]);
    if (count == 0)
        printf("none");
}

static struct device_admission_qai_probe_result print_device_admission_qai_probe(
    int fd, struct d3dkmthandle adapter, const char *route,
    struct winluid query_luid, struct winluid dxg_luid, const char *probe_class,
    uint32 type, const char *name, uint32 requested_size)
{
    struct device_admission_qai_probe_result result;
    struct d3dkmt_queryadapterinfo query;
    unsigned char *data = NULL;
    uint32 result_size = requested_size;
    int rc = -1;
    int saved_errno = 0;
    char query_luid_text[32];

    memset(&result, 0, sizeof(result));
    result.attempted = 1;
    result.rc = -1;
    result.requested_size = requested_size;
    result.result_size = 0;
    format_winluid_text(query_luid_text, sizeof(query_luid_text),
                        query_luid);
    if (requested_size != 0) {
        data = calloc(1, requested_size);
        if (!data) {
            result.err = errno ? errno : ENOMEM;
            printf("d3d12sharedsmoke: device-admission-qai-probe class=%s route=%s luid=%s matched_dxg_luid=%u type=%u name=%s requested_size=%u result_size=0 rc=-1 errno=%d status=ALLOC_FAIL head=none\n",
                   probe_class, route, query_luid_text,
                   winluid_equal(query_luid, dxg_luid), type, name,
                   requested_size, result.err);
            return result;
        }
    }

    memset(&query, 0, sizeof(query));
    query.adapter = adapter;
    query.type = type;
    query.private_data = (uint64)data;
    query.private_data_size = requested_size;
    rc = ioctl(fd, LX_DXQUERYADAPTERINFO, &query);
    result_size = query.private_data_size;
    result.rc = rc;
    result.result_size = result_size;
    if (rc < 0)
        saved_errno = errno ? errno : EIO;
    result.err = saved_errno;

    printf("d3d12sharedsmoke: device-admission-qai-probe class=%s route=%s luid=%s matched_dxg_luid=%u type=%u name=%s requested_size=%u result_size=%u rc=%d errno=%d status=%s head=",
           probe_class, route, query_luid_text,
           winluid_equal(query_luid, dxg_luid), type, name,
           requested_size, result_size, rc, saved_errno,
           rc < 0 ? "FAIL" : "PASS");
    print_qai_head16(data, rc < 0 ? 0 :
                     (result_size < requested_size ? result_size :
                      requested_size));
    printf("\n");

    if (data)
        free(data);
    return result;
}

static int device_admission_wsl_type0_size(uint32_t *size_out,
                                           const char **source_out,
                                           const char **reason_out)
{
    const char *env;

    if (!size_out || !source_out || !reason_out)
        return 0;
    if (g_device_admission_wsl_type0_size_set) {
        *size_out = g_device_admission_wsl_type0_size;
        *source_out = "cli";
        *reason_out = "ok";
        return 1;
    }
    env = getenv(D3D12_QAI_WSL_TYPE0_SIZE_ENV);
    if (!env || env[0] == '\0')
        env = getenv(D3D12_QAI_WSL_ADVN_SIZE_ENV);
    if (!env || env[0] == '\0') {
        *size_out = 0;
        *source_out = "none";
        *reason_out = "missing-cache";
        return 0;
    }
    if (parse_runtime_u32(env, size_out) != 0 || *size_out == 0) {
        *size_out = 0;
        *source_out = "env";
        *reason_out = "bad-cache-value";
        return 0;
    }
    *source_out = "env";
    *reason_out = "ok";
    return 1;
}

static void print_device_admission_wsl_shaped_qai(
    int fd, struct d3dkmthandle adapter, const char *route,
    struct winluid query_luid, struct winluid dxg_luid,
    struct device_admission_qai_route_summary *summary)
{
    uint32_t type0_size = 0;
    const char *type0_source = "none";
    const char *type0_reason = "missing-cache";
    char query_luid_text[32];
    struct device_admission_qai_probe_result result;

    result = print_device_admission_qai_probe(
        fd, adapter, route, query_luid, dxg_luid,
        "wsl-shaped", 55, "type55", 4);
    device_admission_qai_record_probe(summary, "wsl-shaped", 55, &result);
    result = print_device_admission_qai_probe(
        fd, adapter, route, query_luid, dxg_luid,
        "wsl-shaped", 27, "type27", 4);
    device_admission_qai_record_probe(summary, "wsl-shaped", 27, &result);
    if (device_admission_wsl_type0_size(&type0_size, &type0_source,
                                        &type0_reason)) {
        result = print_device_admission_qai_probe(
            fd, adapter, route, query_luid, dxg_luid, "wsl-shaped", 0,
            "umdriverprivate", type0_size);
        device_admission_qai_record_probe(summary, "wsl-shaped", 0, &result);
        return;
    }

    format_winluid_text(query_luid_text, sizeof(query_luid_text),
                        query_luid);
    printf("d3d12sharedsmoke: device-admission-qai-probe class=wsl-shaped route=%s luid=%s matched_dxg_luid=%u type=0 name=umdriverprivate requested_size=0 result_size=0 rc=-1 errno=0 status=SKIP reason=%s cache_source=%s cache_env=%s,%s head=none\n",
           route, query_luid_text, winluid_equal(query_luid, dxg_luid),
           type0_reason, type0_source, D3D12_QAI_WSL_TYPE0_SIZE_ENV,
           D3D12_QAI_WSL_ADVN_SIZE_ENV);
    memset(&result, 0, sizeof(result));
    result.attempted = 1;
    result.skipped = 1;
    result.rc = -1;
    device_admission_qai_record_probe(summary, "wsl-shaped", 0, &result);
}

static void print_device_admission_qai_hints(const char *path,
                                             struct winluid selected_luid,
                                             int have_selected_luid,
                                             struct winluid dxg_luid,
                                             struct device_admission_qai_route_summary *summary)
{
    static const struct {
        uint32 type;
        uint32 size;
        const char *name;
    } probes[] = {
        { 1, 524, "umdrivername" },
        { 55, 4096, "type55" },
        { 0, 8192, "umdriverprivate" },
        { 27, 256, "type27" },
        { 48, 4096, "queryregistry" },
        { 15, 4, "adaptertype" },
        { 30, 4, "physicaladaptercount" },
        { 31, 28, "type31" },
        { 57, 4, "adaptertype-render" },
    };
    struct d3dkmt_openadapterfromluid open_luid;
    struct d3dkmt_closeadapter close_adapter;
    struct winluid query_luid = have_selected_luid ? selected_luid : dxg_luid;
    char query_luid_text[32];
    int fd;

    if (summary) {
        memset(summary, 0, sizeof(*summary));
        summary->have_luid = have_selected_luid;
        summary->luid = query_luid;
        summary->device_luid_match = winluid_equal(query_luid, dxg_luid);
    }
    format_winluid_text(query_luid_text, sizeof(query_luid_text),
                        query_luid);
    fd = open("/dev/dxg", O_RDWR);
    if (fd < 0) {
        printf("d3d12sharedsmoke: device-admission-qai path=%s luid=%s open_dxg=FAIL errno=%d (%s)\n",
               path, query_luid_text, errno, strerror(errno));
        return;
    }

    memset(&open_luid, 0, sizeof(open_luid));
    open_luid.adapter_luid = query_luid;
    if (ioctl(fd, LX_DXOPENADAPTERFROMLUID, &open_luid) < 0 ||
        open_luid.adapter_handle.v == 0) {
        int saved_errno = errno ? errno : ENODEV;

        printf("d3d12sharedsmoke: device-admission-qai path=%s luid=%s low=0x%08x high=0x%08x open_adapter=FAIL errno=%d (%s)\n",
               path, query_luid_text, query_luid.a, query_luid.b,
               saved_errno, strerror(saved_errno));
        close(fd);
        return;
    }

    printf("d3d12sharedsmoke: device-admission-qai path=%s luid=%s low=0x%08x high=0x%08x matched_dxg_luid=%u adapter=0x%x open_adapter=PASS\n",
           path, query_luid_text, query_luid.a, query_luid.b,
           winluid_equal(query_luid, dxg_luid), open_luid.adapter_handle.v);

    for (size_t i = 0; i < sizeof(probes) / sizeof(probes[0]); i++) {
        unsigned char data[8192];
        struct d3dkmt_queryadapterinfo query;
        uint32 size = probes[i].size;
        int rc;
        int saved_errno = 0;
        struct device_admission_qai_probe_result probe_result;

        if (size > sizeof(data))
            size = sizeof(data);
        memset(data, 0, sizeof(data));
        memset(&query, 0, sizeof(query));
        query.adapter = open_luid.adapter_handle;
        query.type = probes[i].type;
        query.private_data = (uint64)data;
        query.private_data_size = size;
        rc = ioctl(fd, LX_DXQUERYADAPTERINFO, &query);
        if (rc < 0)
            saved_errno = errno ? errno : EIO;

        printf("d3d12sharedsmoke: device-admission-qai path=%s type=%u name=%s rc=%d errno=%d size=%u head=",
               path, probes[i].type, probes[i].name, rc, saved_errno, size);
        print_qai_head16(data, rc < 0 ? 0 : size);
        printf("\n");
        probe_result = print_device_admission_qai_probe(
            fd, open_luid.adapter_handle, path, query_luid, dxg_luid,
            "generic", probes[i].type, probes[i].name, size);
        device_admission_qai_record_probe(summary, "generic",
                                          probes[i].type, &probe_result);
    }
    print_device_admission_wsl_shaped_qai(fd, open_luid.adapter_handle,
                                          path, query_luid, dxg_luid,
                                          summary);

    memset(&close_adapter, 0, sizeof(close_adapter));
    close_adapter.adapter_handle = open_luid.adapter_handle;
    ioctl(fd, LX_DXCLOSEADAPTER, &close_adapter);
    close(fd);
}

static void print_device_admission_result(const char *path, HRESULT adapter_hr,
                                          HRESULT create_hr,
                                          void *adapter,
                                          int have_selected_luid,
                                          struct winluid selected_luid,
                                          int have_device_luid,
                                          struct winluid device_luid,
                                          struct winluid dxg_luid)
{
    char selected_luid_text[32];
    char device_luid_text[32];
    int device_luid_match = have_device_luid &&
        winluid_equal(device_luid, dxg_luid);

    format_winluid_text(selected_luid_text, sizeof(selected_luid_text),
                        selected_luid);
    format_winluid_text(device_luid_text, sizeof(device_luid_text),
                        device_luid);
    printf("d3d12sharedsmoke: device-admission path=%s adapter_hr=0x%lx create_hr=0x%lx adapter_present=%u adapter=%p selected_luid=%s low=0x%08x high=0x%08x device_luid=%s device_luid_match=%u\n",
           path, (unsigned long)adapter_hr, (unsigned long)create_hr,
           adapter ? 1 : 0, adapter,
           have_selected_luid ? selected_luid_text : "none",
           have_selected_luid ? selected_luid.a : 0,
           have_selected_luid ? selected_luid.b : 0,
           have_device_luid ? device_luid_text : "none",
           device_luid_match);
}

static void d3d12_device_admission_try_direct(
    struct xv6_dxcore_adapter_factory *factory, struct winluid dxg_luid,
    struct device_admission_qai_route_summary *summary)
{
    struct xv6_dxcore_adapter *adapter = NULL;
    ID3D12Device *device = NULL;
    LUID luid;
    HRESULT adapter_hr;
    HRESULT create_hr = (HRESULT)0x80004005;
    struct winluid device_luid;
    int have_device_luid = 0;

    memset(&luid, 0, sizeof(luid));
    memset(&device_luid, 0, sizeof(device_luid));
    luid.LowPart = dxg_luid.a;
    luid.HighPart = (LONG)dxg_luid.b;

    adapter_hr = factory->lpVtbl->GetAdapterByLuid(
        factory, &luid, &XV6_IID_IDXCoreAdapter, (void **)&adapter);
    if (SUCCEEDED(adapter_hr) && adapter) {
        create_hr = D3D12CreateDevice((IUnknown *)adapter,
                                      D3D_FEATURE_LEVEL_11_0,
                                      &IID_ID3D12Device, (void **)&device);
        if (SUCCEEDED(create_hr) && device) {
            LUID got = ID3D12Device_GetAdapterLuid(device);

            device_luid.a = (uint32_t)got.LowPart;
            device_luid.b = (uint32_t)got.HighPart;
            have_device_luid = 1;
        }
    }

    print_device_admission_result("direct-get-adapter-by-luid", adapter_hr,
                                  create_hr, adapter, 1, dxg_luid,
                                  have_device_luid, device_luid, dxg_luid);
    print_device_admission_qai_hints("direct-get-adapter-by-luid",
                                     dxg_luid, 1, dxg_luid, summary);

    if (device)
        ID3D12Device_Release(device);
    if (adapter)
        adapter->lpVtbl->Release(adapter);
}

static void d3d12_device_admission_try_wsl_list(
    struct xv6_dxcore_adapter_factory *factory, struct winluid dxg_luid,
    struct device_admission_qai_route_summary *summary)
{
    const GUID attrs[] = { XV6_DXCORE_ADAPTER_ATTRIBUTE_D3D12_GRAPHICS };
    struct xv6_dxcore_adapter_list *list = NULL;
    struct xv6_dxcore_adapter *selected = NULL;
    ID3D12Device *device = NULL;
    HRESULT adapter_hr;
    HRESULT create_hr = (HRESULT)0x80004005;
    uint32_t count = 0;
    struct winluid selected_luid;
    struct winluid device_luid;
    int have_selected_luid = 0;
    int have_device_luid = 0;

    memset(&selected_luid, 0, sizeof(selected_luid));
    memset(&device_luid, 0, sizeof(device_luid));

    adapter_hr = factory->lpVtbl->CreateAdapterList(
        factory, 1, attrs, &XV6_IID_IDXCoreAdapterList, (void **)&list);
    if (SUCCEEDED(adapter_hr) && list) {
        count = list->lpVtbl->GetAdapterCount(list);
        for (uint32_t i = 0; i < count; i++) {
            struct xv6_dxcore_adapter *candidate = NULL;
            LUID candidate_luid;
            struct winluid candidate_winluid;
            int have_candidate_luid = 0;
            HRESULT get_hr;

            get_hr = list->lpVtbl->GetAdapter(list, i,
                                              &XV6_IID_IDXCoreAdapter,
                                              (void **)&candidate);
            if (FAILED(get_hr) || !candidate)
                continue;

            memset(&candidate_luid, 0, sizeof(candidate_luid));
            memset(&candidate_winluid, 0, sizeof(candidate_winluid));
            if (candidate->lpVtbl->GetProperty &&
                SUCCEEDED(candidate->lpVtbl->GetProperty(
                    candidate, XV6_DXCORE_ADAPTER_PROPERTY_INSTANCE_LUID,
                    sizeof(candidate_luid), &candidate_luid))) {
                candidate_winluid.a = (uint32_t)candidate_luid.LowPart;
                candidate_winluid.b = (uint32_t)candidate_luid.HighPart;
                have_candidate_luid = 1;
            }

            if (!selected ||
                (have_candidate_luid &&
                 winluid_equal(candidate_winluid, dxg_luid))) {
                if (selected)
                    selected->lpVtbl->Release(selected);
                selected = candidate;
                if (have_candidate_luid) {
                    selected_luid = candidate_winluid;
                    have_selected_luid = 1;
                } else {
                    memset(&selected_luid, 0, sizeof(selected_luid));
                    have_selected_luid = 0;
                }
                if (have_candidate_luid &&
                    winluid_equal(candidate_winluid, dxg_luid))
                    break;
                continue;
            }
            candidate->lpVtbl->Release(candidate);
        }
    }

    if (selected) {
        create_hr = D3D12CreateDevice((IUnknown *)selected,
                                      D3D_FEATURE_LEVEL_11_0,
                                      &IID_ID3D12Device, (void **)&device);
        if (SUCCEEDED(create_hr) && device) {
            LUID got = ID3D12Device_GetAdapterLuid(device);

            device_luid.a = (uint32_t)got.LowPart;
            device_luid.b = (uint32_t)got.HighPart;
            have_device_luid = 1;
        }
    }

    print_device_admission_result("create-adapter-list-d3d12-graphics",
                                  adapter_hr, create_hr, selected,
                                  have_selected_luid, selected_luid,
                                  have_device_luid, device_luid, dxg_luid);
    print_device_admission_qai_hints("create-adapter-list-d3d12-graphics",
                                     selected_luid, have_selected_luid,
                                     dxg_luid, summary);

    if (device)
        ID3D12Device_Release(device);
    if (selected)
        selected->lpVtbl->Release(selected);
    if (list)
        list->lpVtbl->Release(list);
}

static int run_d3d12_device_admission_matrix(void)
{
    struct xv6_dxcore_adapter_factory *factory = NULL;
    struct device_admission_qai_route_summary direct_qai;
    struct device_admission_qai_route_summary list_qai;
    struct winluid dxg_luid;
    char dxg_luid_text[32];
    HRESULT hr;

    if (query_first_dxg_adapter_luid_quiet(&dxg_luid) != 0) {
        int dxg_errno = errno ? errno : ENODEV;

        printf("d3d12sharedsmoke: dxg-luid-discovery source=device-admission-matrix path=/dev/dxg ioctl=LX_DXENUMADAPTERS2 result=failed errno=%d (%s) fallback=dxcore-admission-report-only\n",
               dxg_errno, strerror(dxg_errno));
        d3d12_report_dxcore_admission_without_dxg_luid(
            "device_admission_matrix", "device-admission-matrix",
            dxg_errno);
        fprintf(stderr,
                "d3d12sharedsmoke: device-admission could not obtain first DXG adapter LUID errno=%d (%s)\n",
                dxg_errno, strerror(dxg_errno));
        return 1;
    }
    format_winluid_text(dxg_luid_text, sizeof(dxg_luid_text), dxg_luid);
    printf("d3d12sharedsmoke: device-admission dxg_luid=%s low=0x%08x high=0x%08x feature_level=0x%x iid=ID3D12Device\n",
           dxg_luid_text, dxg_luid.a, dxg_luid.b, D3D_FEATURE_LEVEL_11_0);

    hr = DXCoreCreateAdapterFactory(&XV6_IID_IDXCoreAdapterFactory,
                                    (void **)&factory);
    if (FAILED(hr) || !factory) {
        fprintf(stderr,
                "d3d12sharedsmoke: device-admission DXCoreCreateAdapterFactory failed hr=0x%lx factory=%p\n",
                (unsigned long)hr, (void *)factory);
        return 1;
    }

    memset(&direct_qai, 0, sizeof(direct_qai));
    memset(&list_qai, 0, sizeof(list_qai));
    d3d12_device_admission_try_direct(factory, dxg_luid, &direct_qai);
    d3d12_device_admission_try_wsl_list(factory, dxg_luid, &list_qai);
    print_device_admission_qai_equivalence_summary(dxg_luid, &direct_qai,
                                                   &list_qai);

    factory->lpVtbl->Release(factory);
    return 0;
}

static int d3d12_runtime_postcheck_dxg_luid(const char *phase,
                                            struct winluid runtime_luid)
{
    struct winluid dxg_luid;
    char runtime_luid_text[32];
    char dxg_luid_text[32];
    int match;

    memset(&dxg_luid, 0, sizeof(dxg_luid));
    format_winluid_text(runtime_luid_text, sizeof(runtime_luid_text),
                        runtime_luid);
    if (query_first_dxg_adapter_luid(&dxg_luid) != 0) {
        printf("d3d12sharedsmoke: runtime post-export dxg LUID check phase=%s unavailable runtime=%s\n",
               phase ? phase : "normal", runtime_luid_text);
        return 0;
    }

    format_winluid_text(dxg_luid_text, sizeof(dxg_luid_text), dxg_luid);
    match = runtime_luid.a == dxg_luid.a && runtime_luid.b == dxg_luid.b;
    printf("d3d12sharedsmoke: runtime post-export dxg LUID check phase=%s runtime=%s dxg=%s match=%u\n",
           phase ? phase : "normal", runtime_luid_text, dxg_luid_text,
           match);
    if (!match) {
        fprintf(stderr,
                "d3d12sharedsmoke: runtime adapter LUID mismatch after export phase=%s runtime=%s dxg=%s\n",
                phase ? phase : "normal", runtime_luid_text, dxg_luid_text);
        return -1;
    }
    return 0;
}

static void d3d12_runtime_record_device_luid(struct d3d12_runtime *rt)
{
    LUID luid;

    if (!rt || !rt->device)
        return;
    luid = ID3D12Device_GetAdapterLuid(rt->device);
    rt->adapter_luid.a = (uint32_t)luid.LowPart;
    rt->adapter_luid.b = (uint32_t)luid.HighPart;
}

static const char *d3d12_runtime_shape_name(
    const struct d3d12_runtime_options *opts)
{
    if (opts && opts->render_target)
        return "render_target";
    if (opts && opts->simultaneous)
        return "simultaneous";
    return "plain";
}

static const char *d3d12_runtime_export_label(
    const struct d3d12_runtime_options *opts)
{
    if (opts && opts->wsl_resource_shape_app_sync_export_only)
        return "phase1_wsl_resource_shape_app_sync_export_only";
    if (opts && opts->runtime_import_contract) {
        if (opts->runtime_import_independent_device)
            return "phase1_wsl_success_shape_import_contract_independent_device";
        if (opts->runtime_import_fence_first)
            return "phase1_wsl_success_shape_import_contract_fence_first";
        if (opts->runtime_import_fence_dup_no_cloexec)
            return "phase1_wsl_success_shape_import_contract_fence_dup_no_cloexec";
        if (opts->runtime_import_fence_cross_adapter)
            return "phase1_wsl_success_shape_import_contract_fence_cross_adapter";
        return "phase1_wsl_success_shape_import_contract";
    }
    if (opts && opts->wsl_success_shape_wsl_list_default_export_only)
        return "phase1_wsl_success_shape_wsl_list_default_export_only";
    if (opts && opts->wsl_success_shape_no_clear_value_export_only)
        return "phase1_wsl_success_shape_no_clear_value_export_only";
    if (opts && opts->wsl_success_shape_initial_rt_export_only)
        return "phase1_wsl_success_shape_initial_rt_export_only";
    if (opts && opts->wsl_success_shape_reserve_low_va_export_only)
        return "phase1_wsl_success_shape_reserve_low_va_export_only";
    if (opts && opts->wsl_success_shape_resource_cross_adapter_export_only)
        return "phase1_wsl_success_shape_resource_cross_adapter_export_only";
    if (opts && opts->wsl_success_shape_prealloc_info_export_only)
        return "phase1_wsl_success_shape_prealloc_info_export_only";
    if (opts && opts->wsl_resource_shape_shared_heap_export_only)
        return "phase1_wsl_success_shape_direct_export_only";
    if (opts && opts->wsl_resource_shape_no_heap_flags_export_only)
        return "phase1_wsl_success_shape_no_heap_flags_export_only";
    if (opts && opts->wsl_resource_shape_placed_shared_heap_export_only)
        return "phase1_wsl_success_shape_placed_shared_heap_export_only";
    if (opts && opts->wsl_resource_shape_shared_heap)
        return "phase1_wsl_resource_shape_shared_heap";
    if (opts && opts->wsl_resource_shape_direct)
        return "phase1_wsl_resource_shape_direct";
    if (opts && opts->wsl_resource_shape)
        return "phase1_wsl_resource_shape";
    if (opts && opts->fence_only)
        return "phase1e_fence_only";
    if (opts && opts->heap_only)
        return "phase1e_placed_shared_heap";
    if (opts && opts->export_heap_first)
        return "phase1e_heap_first";
    if (opts && opts->make_resident_before_export)
        return "phase1e_make_resident_before_export";
    if (opts && opts->placed_resource)
        return "phase1e_placed_resource";
    return "phase1a_committed_resource";
}

static void d3d12_runtime_print_export_result(
    const char *label, const char *phase, const char *handle_kind,
    int create_shared_handle_reached, HRESULT hr, HRESULT device_reason,
    HANDLE handle, struct winluid adapter_luid,
    D3D12_RESOURCE_FLAGS resource_flags, D3D12_HEAP_FLAGS heap_flags,
    D3D12_RESOURCE_STATES initial_state, const char *app_sync,
    const char *adapter_path, int present_attempted, int import_attempted)
{
    char adapter_luid_text[32];

    format_winluid_text(adapter_luid_text, sizeof(adapter_luid_text),
                        adapter_luid);
    printf("d3d12sharedsmoke: runtime export-result label=%s phase=%s handle_kind=%s create_shared_handle_reached=%u nt_share_expected=%u hr=0x%lx device_reason=0x%lx handle=%p fd=%d resource_flags=0x%x heap_flags=0x%x initial_state=%u app_sync=%s adapter_path=%s present_attempted=%u import_attempted=%u adapter_luid=%s low=0x%08x high=0x%08x present_claim=export_only\n",
           label ? label : "runtime_export",
           phase ? phase : "normal",
           handle_kind ? handle_kind : "unknown",
           create_shared_handle_reached,
           create_shared_handle_reached,
           (unsigned long)hr, (unsigned long)device_reason, handle,
           handle ? handle_to_fd(handle) : -1,
           (unsigned)resource_flags, (unsigned)heap_flags,
           (unsigned)initial_state, app_sync ? app_sync : "unknown",
           adapter_path ? adapter_path : "unknown", present_attempted,
           import_attempted,
           adapter_luid_text, adapter_luid.a, adapter_luid.b);
}

static void d3d12_runtime_print_phase_result(
    const char *label, const char *stage, const char *handle_kind,
    int create_device_success, int create_shared_handle_reached,
    HRESULT hr, HRESULT device_reason, HANDLE handle,
    struct winluid adapter_luid, const char *present_claim)
{
    char adapter_luid_text[32];

    format_winluid_text(adapter_luid_text, sizeof(adapter_luid_text),
                        adapter_luid);
    printf("d3d12sharedsmoke: runtime phase-result label=%s stage=%s handle_kind=%s create_device_success=%u create_shared_handle_reached=%u hr=0x%lx device_reason=0x%lx handle=%p fd=%d adapter_luid=%s low=0x%08x high=0x%08x present_claim=%s\n",
           label ? label : "runtime",
           stage ? stage : "unknown",
           handle_kind ? handle_kind : "none",
           create_device_success, create_shared_handle_reached,
           (unsigned long)hr, (unsigned long)device_reason, handle,
           handle ? handle_to_fd(handle) : -1,
           adapter_luid_text, adapter_luid.a, adapter_luid.b,
           present_claim ? present_claim : "export_only");
}

static void d3d12_report_dxcore_admission_without_dxg_luid(
    const char *label, const char *source, int dxg_errno)
{
    const GUID attrs[] = { XV6_DXCORE_ADAPTER_ATTRIBUTE_D3D12_GRAPHICS };
    struct xv6_dxcore_adapter_factory *factory = NULL;
    struct xv6_dxcore_adapter_list *list = NULL;
    HRESULT factory_hr;
    HRESULT list_hr = (HRESULT)0x80004005;
    uint32_t count = 0;

    factory_hr = DXCoreCreateAdapterFactory(&XV6_IID_IDXCoreAdapterFactory,
                                            (void **)&factory);
    if (FAILED(factory_hr) || !factory) {
        printf("d3d12sharedsmoke: dxcore-admission-fallback source=%s label=%s dxg_enum_errno=%d factory_hr=0x%lx list_hr=0x%lx count=0 runtime_export_allowed=0 present_claim=none\n",
               source ? source : "unknown", label ? label : "runtime",
               dxg_errno, (unsigned long)factory_hr,
               (unsigned long)list_hr);
        if (factory)
            factory->lpVtbl->Release(factory);
        return;
    }

    list_hr = factory->lpVtbl->CreateAdapterList(
        factory, 1, attrs, &XV6_IID_IDXCoreAdapterList, (void **)&list);
    if (SUCCEEDED(list_hr) && list)
        count = list->lpVtbl->GetAdapterCount(list);
    printf("d3d12sharedsmoke: dxcore-admission-fallback source=%s label=%s dxg_enum_errno=%d factory_hr=0x%lx list_hr=0x%lx count=%u runtime_export_allowed=0 present_claim=none\n",
           source ? source : "unknown", label ? label : "runtime",
           dxg_errno, (unsigned long)factory_hr, (unsigned long)list_hr,
           count);

    if (SUCCEEDED(list_hr) && list) {
        for (uint32_t i = 0; i < count; i++) {
            struct xv6_dxcore_adapter *adapter = NULL;
            struct xv6_dxcore_adapter *direct_adapter = NULL;
            ID3D12Device *list_device = NULL;
            ID3D12Device *direct_device = NULL;
            LUID property_luid;
            LUID device_luid;
            struct winluid property_winluid;
            struct winluid list_device_winluid;
            struct winluid direct_device_winluid;
            char property_luid_text[32];
            char list_device_luid_text[32];
            char direct_device_luid_text[32];
            HRESULT adapter_hr;
            HRESULT property_hr = (HRESULT)0x80004005;
            HRESULT list_create_hr = (HRESULT)0x80004005;
            HRESULT direct_hr = (HRESULT)0x80004005;
            HRESULT direct_create_hr = (HRESULT)0x80004005;
            int have_property_luid = 0;
            int have_list_device_luid = 0;
            int have_direct_device_luid = 0;

            memset(&property_luid, 0, sizeof(property_luid));
            memset(&device_luid, 0, sizeof(device_luid));
            memset(&property_winluid, 0, sizeof(property_winluid));
            memset(&list_device_winluid, 0, sizeof(list_device_winluid));
            memset(&direct_device_winluid, 0, sizeof(direct_device_winluid));

            adapter_hr = list->lpVtbl->GetAdapter(
                list, i, &XV6_IID_IDXCoreAdapter, (void **)&adapter);
            if (SUCCEEDED(adapter_hr) && adapter) {
                if (adapter->lpVtbl->GetProperty) {
                    property_hr = adapter->lpVtbl->GetProperty(
                        adapter, XV6_DXCORE_ADAPTER_PROPERTY_INSTANCE_LUID,
                        sizeof(property_luid), &property_luid);
                    if (SUCCEEDED(property_hr)) {
                        property_winluid.a = (uint32_t)property_luid.LowPart;
                        property_winluid.b = (uint32_t)property_luid.HighPart;
                        have_property_luid = 1;
                    }
                }
                list_create_hr = D3D12CreateDevice(
                    (IUnknown *)adapter, D3D_FEATURE_LEVEL_11_0,
                    &IID_ID3D12Device, (void **)&list_device);
                if (SUCCEEDED(list_create_hr) && list_device) {
                    device_luid = ID3D12Device_GetAdapterLuid(list_device);
                    list_device_winluid.a = (uint32_t)device_luid.LowPart;
                    list_device_winluid.b = (uint32_t)device_luid.HighPart;
                    have_list_device_luid = 1;
                }
            }

            if (have_property_luid) {
                direct_hr = factory->lpVtbl->GetAdapterByLuid(
                    factory, &property_luid, &XV6_IID_IDXCoreAdapter,
                    (void **)&direct_adapter);
                if (SUCCEEDED(direct_hr) && direct_adapter) {
                    direct_create_hr = D3D12CreateDevice(
                        (IUnknown *)direct_adapter, D3D_FEATURE_LEVEL_11_0,
                        &IID_ID3D12Device, (void **)&direct_device);
                    if (SUCCEEDED(direct_create_hr) && direct_device) {
                        device_luid =
                            ID3D12Device_GetAdapterLuid(direct_device);
                        direct_device_winluid.a =
                            (uint32_t)device_luid.LowPart;
                        direct_device_winluid.b =
                            (uint32_t)device_luid.HighPart;
                        have_direct_device_luid = 1;
                    }
                }
            }

            format_winluid_text(property_luid_text,
                                sizeof(property_luid_text),
                                property_winluid);
            format_winluid_text(list_device_luid_text,
                                sizeof(list_device_luid_text),
                                list_device_winluid);
            format_winluid_text(direct_device_luid_text,
                                sizeof(direct_device_luid_text),
                                direct_device_winluid);
            printf("d3d12sharedsmoke: dxcore-admission-fallback-adapter source=%s label=%s index=%u dxg_enum_errno=%d adapter_hr=0x%lx property_hr=0x%lx have_property_luid=%u property_luid=%s list_create_hr=0x%lx list_create_device_success=%u list_device_luid=%s direct_get_by_property_luid_hr=0x%lx direct_create_hr=0x%lx direct_create_device_success=%u direct_device_luid=%s runtime_export_allowed=0 present_claim=none\n",
                   source ? source : "unknown", label ? label : "runtime",
                   i, dxg_errno, (unsigned long)adapter_hr,
                   (unsigned long)property_hr, have_property_luid,
                   have_property_luid ? property_luid_text : "none",
                   (unsigned long)list_create_hr,
                   (SUCCEEDED(list_create_hr) && list_device) ? 1 : 0,
                   have_list_device_luid ? list_device_luid_text : "none",
                   (unsigned long)direct_hr,
                   (unsigned long)direct_create_hr,
                   (SUCCEEDED(direct_create_hr) && direct_device) ? 1 : 0,
                   have_direct_device_luid ? direct_device_luid_text : "none");

            if (direct_device)
                ID3D12Device_Release(direct_device);
            if (direct_adapter)
                direct_adapter->lpVtbl->Release(direct_adapter);
            if (list_device)
                ID3D12Device_Release(list_device);
            if (adapter)
                adapter->lpVtbl->Release(adapter);
        }
    }

    if (list)
        list->lpVtbl->Release(list);
    if (factory)
        factory->lpVtbl->Release(factory);
}

static int parse_runtime_size(const char *text, uint32_t *width,
                              uint32_t *height)
{
    unsigned int parsed_width;
    unsigned int parsed_height;
    char extra;

    if (!text || !width || !height)
        return -1;
    if (sscanf(text, "%ux%u%c", &parsed_width, &parsed_height, &extra) != 2)
        return -1;
    if (parsed_width == 0 || parsed_height == 0)
        return -1;
    if (parsed_width > 16384 || parsed_height > 16384)
        return -1;
    *width = parsed_width;
    *height = parsed_height;
    return 0;
}

static int parse_runtime_share_access(const char *text, uint32_t *access,
                                      const char **name)
{
    char *end;
    unsigned long value;

    if (!text || !access || !name)
        return -1;
    if (strcmp(text, "generic-all") == 0 || strcmp(text, "all") == 0 ||
        strcmp(text, "default") == 0) {
        *access = GENERIC_ALL;
        *name = "generic-all";
        return 0;
    }
    if (strcmp(text, "zero") == 0 || strcmp(text, "none") == 0) {
        *access = 0;
        *name = "zero";
        return 0;
    }
    if (strcmp(text, "read") == 0) {
        *access = GENERIC_READ;
        *name = "read";
        return 0;
    }
    if (strcmp(text, "write") == 0) {
        *access = GENERIC_WRITE;
        *name = "write";
        return 0;
    }
    if (strcmp(text, "readwrite") == 0) {
        *access = GENERIC_READ | GENERIC_WRITE;
        *name = "readwrite";
        return 0;
    }
    errno = 0;
    value = strtoul(text, &end, 0);
    if (errno || end == text || *end != '\0' || value > 0xffffffffUL)
        return -1;
    *access = (uint32_t)value;
    *name = "custom";
    return 0;
}

static int parse_runtime_u32(const char *text, uint32_t *value_out)
{
    char *end;
    unsigned long value;

    if (!text || !value_out)
        return -1;
    errno = 0;
    value = strtoul(text, &end, 0);
    if (errno || end == text || *end != '\0' || value > 0xffffffffUL)
        return -1;
    *value_out = (uint32_t)value;
    return 0;
}

static void apply_runtime_wsl_parity(struct d3d12_runtime_options *opts,
                                     int runtime_size_explicit,
                                     int runtime_touch_explicit,
                                     int runtime_app_sync_explicit)
{
    opts->wsl_parity = 1;
    opts->render_target = 1;
    opts->simultaneous = 0;
    opts->initial_common = 1;
    opts->clear_alpha_only = 1;
    opts->skip_prealloc_info = 1;
    opts->wsl_adapter_list = 1;
    if (!runtime_app_sync_explicit)
        opts->suppress_app_sync = 1;
    if (!runtime_touch_explicit)
        opts->touch_before_export = 0;
    if (!runtime_size_explicit) {
        opts->width = 640;
        opts->height = 480;
    }
}

static void apply_runtime_wsl_resource_shape(
    struct d3d12_runtime_options *opts,
    int runtime_size_explicit,
    int runtime_touch_explicit,
    int runtime_app_sync_explicit)
{
    apply_runtime_wsl_parity(opts, runtime_size_explicit,
                             runtime_touch_explicit,
                             runtime_app_sync_explicit);
    opts->wsl_resource_shape = 1;
    opts->zero_heap_flags = 1;
    opts->skip_prealloc_info = 0;
    opts->precheck_dxg_luid = 1;
    opts->fallback_dxg_luid_on_empty_wsl_list = 0;
}

static void apply_runtime_wsl_resource_shape_direct(
    struct d3d12_runtime_options *opts,
    int runtime_size_explicit,
    int runtime_touch_explicit,
    int runtime_app_sync_explicit)
{
    apply_runtime_wsl_resource_shape(opts, runtime_size_explicit,
                                     runtime_touch_explicit,
                                     runtime_app_sync_explicit);
    opts->wsl_resource_shape_direct = 1;
    opts->wsl_adapter_list = 0;
    opts->fallback_dxg_luid_on_empty_wsl_list = 0;
}

static void apply_runtime_wsl_resource_shape_shared_heap(
    struct d3d12_runtime_options *opts,
    int runtime_size_explicit,
    int runtime_touch_explicit,
    int runtime_app_sync_explicit)
{
    apply_runtime_wsl_resource_shape_direct(opts, runtime_size_explicit,
                                            runtime_touch_explicit,
                                            runtime_app_sync_explicit);
    opts->wsl_resource_shape_shared_heap = 1;
    opts->zero_heap_flags = 0;
}

static void apply_runtime_wsl_success_shape_direct_export_only(
    struct d3d12_runtime_options *opts,
    int runtime_size_explicit,
    int runtime_touch_explicit,
    int runtime_app_sync_explicit)
{
    apply_runtime_wsl_resource_shape_shared_heap(
        opts, runtime_size_explicit, runtime_touch_explicit,
        runtime_app_sync_explicit);
    opts->wsl_resource_shape_shared_heap_export_only = 1;
    opts->resource_only = 1;
    opts->runtime_export_only = 1;
    opts->skip_prealloc_info = 1;
    opts->preexport_diagnostics = 1;
}

static void apply_runtime_wsl_success_shape_import_contract(
    struct d3d12_runtime_options *opts,
    int runtime_size_explicit,
    int runtime_touch_explicit)
{
    apply_runtime_wsl_resource_shape_shared_heap(
        opts, runtime_size_explicit, runtime_touch_explicit, 1);
    opts->wsl_resource_shape_shared_heap_export_only = 1;
    opts->runtime_import_contract = 1;
    opts->resource_only = 0;
    opts->runtime_export_only = 0;
    opts->suppress_app_sync = 0;
    opts->skip_prealloc_info = 1;
    opts->preexport_diagnostics = 1;
}

static void apply_runtime_wsl_success_shape_import_contract_independent(
    struct d3d12_runtime_options *opts,
    int runtime_size_explicit,
    int runtime_touch_explicit)
{
    apply_runtime_wsl_success_shape_import_contract(
        opts, runtime_size_explicit, runtime_touch_explicit);
    opts->runtime_import_independent_device = 1;
}

static void apply_runtime_wsl_success_shape_import_contract_fence_first(
    struct d3d12_runtime_options *opts,
    int runtime_size_explicit,
    int runtime_touch_explicit)
{
    apply_runtime_wsl_success_shape_import_contract(
        opts, runtime_size_explicit, runtime_touch_explicit);
    opts->runtime_import_fence_first = 1;
    opts->export_fence_first = 1;
}

static void apply_runtime_wsl_success_shape_import_contract_fence_dup(
    struct d3d12_runtime_options *opts,
    int runtime_size_explicit,
    int runtime_touch_explicit)
{
    apply_runtime_wsl_success_shape_import_contract(
        opts, runtime_size_explicit, runtime_touch_explicit);
    opts->runtime_import_fence_dup_no_cloexec = 1;
}

static void apply_runtime_wsl_success_shape_import_contract_fence_cross_adapter(
    struct d3d12_runtime_options *opts,
    int runtime_size_explicit,
    int runtime_touch_explicit)
{
    apply_runtime_wsl_success_shape_import_contract(
        opts, runtime_size_explicit, runtime_touch_explicit);
    opts->runtime_import_fence_cross_adapter = 1;
    opts->fence_cross_adapter = 1;
}

static void apply_runtime_wsl_success_shape_wsl_list_default_export_only(
    struct d3d12_runtime_options *opts,
    int runtime_size_explicit,
    int runtime_touch_explicit,
    int runtime_app_sync_explicit)
{
    apply_runtime_wsl_resource_shape(opts, runtime_size_explicit,
                                     runtime_touch_explicit,
                                     runtime_app_sync_explicit);
    opts->wsl_success_shape_wsl_list_default_export_only = 1;
    opts->wsl_resource_shape_shared_heap = 1;
    opts->zero_heap_flags = 0;
    opts->resource_only = 1;
    opts->runtime_export_only = 1;
    opts->skip_prealloc_info = 1;
    opts->preexport_diagnostics = 1;
    opts->wsl_adapter_list = 1;
    opts->precheck_dxg_luid = 0;
    opts->fallback_dxg_luid_on_empty_wsl_list = 0;
}

static void apply_runtime_wsl_success_shape_no_clear_value_export_only(
    struct d3d12_runtime_options *opts,
    int runtime_size_explicit,
    int runtime_touch_explicit,
    int runtime_app_sync_explicit)
{
    apply_runtime_wsl_success_shape_direct_export_only(
        opts, runtime_size_explicit, runtime_touch_explicit,
        runtime_app_sync_explicit);
    opts->wsl_success_shape_no_clear_value_export_only = 1;
    opts->omit_clear_value = 1;
}

static void apply_runtime_wsl_success_shape_initial_rt_export_only(
    struct d3d12_runtime_options *opts,
    int runtime_size_explicit,
    int runtime_touch_explicit,
    int runtime_app_sync_explicit)
{
    apply_runtime_wsl_success_shape_direct_export_only(
        opts, runtime_size_explicit, runtime_touch_explicit,
        runtime_app_sync_explicit);
    opts->wsl_success_shape_initial_rt_export_only = 1;
    opts->initial_common = 0;
}

static void apply_runtime_wsl_success_shape_reserve_low_va_export_only(
    struct d3d12_runtime_options *opts,
    int runtime_size_explicit,
    int runtime_touch_explicit,
    int runtime_app_sync_explicit)
{
    apply_runtime_wsl_success_shape_direct_export_only(
        opts, runtime_size_explicit, runtime_touch_explicit,
        runtime_app_sync_explicit);
    opts->wsl_success_shape_reserve_low_va_export_only = 1;
    opts->reserve_low_va = 1;
}

static void apply_runtime_wsl_success_shape_resource_cross_adapter_export_only(
    struct d3d12_runtime_options *opts,
    int runtime_size_explicit,
    int runtime_touch_explicit,
    int runtime_app_sync_explicit)
{
    apply_runtime_wsl_success_shape_direct_export_only(
        opts, runtime_size_explicit, runtime_touch_explicit,
        runtime_app_sync_explicit);
    opts->wsl_success_shape_resource_cross_adapter_export_only = 1;
    opts->resource_cross_adapter = 1;
}

static void apply_runtime_wsl_success_shape_prealloc_info_export_only(
    struct d3d12_runtime_options *opts,
    int runtime_size_explicit,
    int runtime_touch_explicit,
    int runtime_app_sync_explicit)
{
    apply_runtime_wsl_success_shape_direct_export_only(
        opts, runtime_size_explicit, runtime_touch_explicit,
        runtime_app_sync_explicit);
    opts->wsl_success_shape_prealloc_info_export_only = 1;
    opts->skip_prealloc_info = 0;
}

static void apply_runtime_wsl_success_shape_no_heap_flags_export_only(
    struct d3d12_runtime_options *opts,
    int runtime_size_explicit,
    int runtime_touch_explicit,
    int runtime_app_sync_explicit)
{
    apply_runtime_wsl_resource_shape_direct(opts, runtime_size_explicit,
                                            runtime_touch_explicit,
                                            runtime_app_sync_explicit);
    opts->wsl_resource_shape_no_heap_flags_export_only = 1;
    opts->resource_only = 1;
    opts->runtime_export_only = 1;
    opts->skip_prealloc_info = 1;
    opts->preexport_diagnostics = 1;
}

static void apply_runtime_wsl_success_shape_placed_shared_heap_export_only(
    struct d3d12_runtime_options *opts,
    int runtime_size_explicit,
    int runtime_touch_explicit,
    int runtime_app_sync_explicit)
{
    apply_runtime_wsl_resource_shape_shared_heap(
        opts, runtime_size_explicit, runtime_touch_explicit,
        runtime_app_sync_explicit);
    opts->wsl_resource_shape_placed_shared_heap_export_only = 1;
    opts->placed_resource = 1;
    opts->resource_only = 1;
    opts->runtime_export_only = 1;
    opts->preexport_diagnostics = 1;
}

static void apply_runtime_wsl_resource_shape_app_sync_export_only(
    struct d3d12_runtime_options *opts,
    int runtime_size_explicit,
    int runtime_touch_explicit,
    int runtime_app_sync_explicit)
{
    (void)runtime_app_sync_explicit;
    apply_runtime_wsl_resource_shape_direct(opts, runtime_size_explicit,
                                            runtime_touch_explicit, 1);
    opts->wsl_resource_shape_app_sync_export_only = 1;
    opts->suppress_app_sync = 0;
    opts->runtime_export_only = 1;
}

static int apply_runtime_case(struct d3d12_runtime_options *opts,
                              const char *name,
                              int runtime_size_explicit,
                              int runtime_touch_explicit,
                              int runtime_app_sync_explicit)
{
    if (strcmp(name, "wsl") == 0) {
        apply_runtime_wsl_parity(opts, runtime_size_explicit,
                                 runtime_touch_explicit,
                                 runtime_app_sync_explicit);
        opts->precheck_dxg_luid = 1;
        opts->fallback_dxg_luid_on_empty_wsl_list = 1;
    } else if (strcmp(name, "wslshape") == 0 ||
               strcmp(name, "wsl-resource-shape") == 0) {
        apply_runtime_wsl_resource_shape(opts, runtime_size_explicit,
                                         runtime_touch_explicit,
                                         runtime_app_sync_explicit);
    } else if (strcmp(name, "wslshape-direct") == 0 ||
               strcmp(name, "wsl-resource-shape-direct") == 0) {
        apply_runtime_wsl_resource_shape_direct(opts, runtime_size_explicit,
                                                runtime_touch_explicit,
                                                runtime_app_sync_explicit);
    } else if (strcmp(name, "wslshape-sharedheap") == 0 ||
               strcmp(name, "wsl-resource-shape-shared-heap") == 0) {
        apply_runtime_wsl_resource_shape_shared_heap(
            opts, runtime_size_explicit, runtime_touch_explicit,
            runtime_app_sync_explicit);
    } else if (strcmp(name, "wsl-success-shape-direct") == 0 ||
               strcmp(name, "wsl-success-shape-direct-export-only") == 0) {
        apply_runtime_wsl_success_shape_direct_export_only(
            opts, runtime_size_explicit, runtime_touch_explicit,
            runtime_app_sync_explicit);
    } else if (strcmp(name, "import") == 0 ||
               strcmp(name, "wsl-success-shape-import-contract") == 0) {
        apply_runtime_wsl_success_shape_import_contract(
            opts, runtime_size_explicit, runtime_touch_explicit);
    } else if (strcmp(name, "wsl-success-shape-import-contract-independent-device") == 0) {
        apply_runtime_wsl_success_shape_import_contract_independent(
            opts, runtime_size_explicit, runtime_touch_explicit);
    } else if (strcmp(name, "wsl-success-shape-import-contract-fence-first") == 0) {
        apply_runtime_wsl_success_shape_import_contract_fence_first(
            opts, runtime_size_explicit, runtime_touch_explicit);
    } else if (strcmp(name, "wsl-success-shape-import-contract-fence-dup") == 0 ||
               strcmp(name, "wsl-success-shape-import-contract-fence-dup-no-cloexec") == 0) {
        apply_runtime_wsl_success_shape_import_contract_fence_dup(
            opts, runtime_size_explicit, runtime_touch_explicit);
    } else if (strcmp(name, "wsl-success-shape-import-contract-fence-cross-adapter") == 0) {
        apply_runtime_wsl_success_shape_import_contract_fence_cross_adapter(
            opts, runtime_size_explicit, runtime_touch_explicit);
    } else if (strcmp(name, "wsl-success-shape-wsl-list-default") == 0 ||
               strcmp(name, "wsl-success-shape-wsl-list-default-export-only") == 0) {
        apply_runtime_wsl_success_shape_wsl_list_default_export_only(
            opts, runtime_size_explicit, runtime_touch_explicit,
            runtime_app_sync_explicit);
    } else if (strcmp(name, "wsl-success-shape-no-clear-value") == 0 ||
               strcmp(name, "wsl-success-shape-no-clear-value-export-only") == 0) {
        apply_runtime_wsl_success_shape_no_clear_value_export_only(
            opts, runtime_size_explicit, runtime_touch_explicit,
            runtime_app_sync_explicit);
    } else if (strcmp(name, "wsl-success-shape-initial-rt") == 0 ||
               strcmp(name, "wsl-success-shape-initial-rt-export-only") == 0) {
        apply_runtime_wsl_success_shape_initial_rt_export_only(
            opts, runtime_size_explicit, runtime_touch_explicit,
            runtime_app_sync_explicit);
    } else if (strcmp(name, "wsl-success-shape-reserve-low-va") == 0 ||
               strcmp(name, "wsl-success-shape-reserve-low-va-export-only") == 0) {
        apply_runtime_wsl_success_shape_reserve_low_va_export_only(
            opts, runtime_size_explicit, runtime_touch_explicit,
            runtime_app_sync_explicit);
    } else if (strcmp(name, "wsl-success-shape-resource-cross-adapter") == 0 ||
               strcmp(name, "wsl-success-shape-resource-cross-adapter-export-only") == 0) {
        apply_runtime_wsl_success_shape_resource_cross_adapter_export_only(
            opts, runtime_size_explicit, runtime_touch_explicit,
            runtime_app_sync_explicit);
    } else if (strcmp(name, "wsl-success-shape-prealloc-info") == 0 ||
               strcmp(name, "wsl-success-shape-prealloc-info-export-only") == 0) {
        apply_runtime_wsl_success_shape_prealloc_info_export_only(
            opts, runtime_size_explicit, runtime_touch_explicit,
            runtime_app_sync_explicit);
    } else if (strcmp(name, "wsl-success-shape-no-heap-flags") == 0 ||
               strcmp(name, "wsl-success-shape-no-heap-flags-export-only") == 0) {
        apply_runtime_wsl_success_shape_no_heap_flags_export_only(
            opts, runtime_size_explicit, runtime_touch_explicit,
            runtime_app_sync_explicit);
    } else if (strcmp(name, "wsl-success-shape-placed-shared-heap") == 0 ||
               strcmp(name, "wsl-success-shape-placed-shared-heap-export-only") == 0) {
        apply_runtime_wsl_success_shape_placed_shared_heap_export_only(
            opts, runtime_size_explicit, runtime_touch_explicit,
            runtime_app_sync_explicit);
    } else if (strcmp(name, "wslshape-appsync-export") == 0 ||
               strcmp(name, "wsl-resource-shape-app-sync-export-only") == 0) {
        apply_runtime_wsl_resource_shape_app_sync_export_only(
            opts, runtime_size_explicit, runtime_touch_explicit,
            runtime_app_sync_explicit);
    } else if (strcmp(name, "mr") == 0) {
        apply_runtime_wsl_parity(opts, runtime_size_explicit,
                                 runtime_touch_explicit,
                                 runtime_app_sync_explicit);
        opts->make_resident_before_export = 1;
    } else if (strcmp(name, "cross") == 0) {
        apply_runtime_wsl_parity(opts, runtime_size_explicit,
                                 runtime_touch_explicit,
                                 runtime_app_sync_explicit);
        opts->resource_cross_adapter = 1;
    } else if (strcmp(name, "placed") == 0) {
        apply_runtime_wsl_parity(opts, runtime_size_explicit,
                                 runtime_touch_explicit,
                                 runtime_app_sync_explicit);
        opts->placed_resource = 1;
    } else if (strcmp(name, "heap") == 0) {
        apply_runtime_wsl_parity(opts, runtime_size_explicit,
                                 runtime_touch_explicit,
                                 runtime_app_sync_explicit);
        opts->placed_resource = 1;
        opts->heap_only = 1;
    } else if (strcmp(name, "heapfirst") == 0) {
        apply_runtime_wsl_parity(opts, runtime_size_explicit,
                                 runtime_touch_explicit,
                                 runtime_app_sync_explicit);
        opts->placed_resource = 1;
        opts->export_heap_first = 1;
    } else if (strcmp(name, "fence") == 0) {
        opts->fence_only = 1;
    } else {
        return -1;
    }

    return 0;
}

static int d3d12_runtime_clear_render_target(
    struct d3d12_runtime *rt, D3D12_RESOURCE_STATES state_before_clear)
{
    D3D12_DESCRIPTOR_HEAP_DESC heap_desc;
    D3D12_CPU_DESCRIPTOR_HANDLE rtv;
    D3D12_RESOURCE_BARRIER barrier;
    ID3D12CommandList *lists[1];
    float clear_color[4] = { 0.125f, 0.25f, 0.5f, 1.0f };
    HRESULT hr;

    memset(&heap_desc, 0, sizeof(heap_desc));
    heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    heap_desc.NumDescriptors = 1;
    heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    hr = ID3D12Device_CreateDescriptorHeap(rt->device, &heap_desc,
                                           &IID_ID3D12DescriptorHeap,
                                           (void **)&rt->rtv_heap);
    if (FAILED(hr) || !rt->rtv_heap) {
        fprintf(stderr,
                "d3d12sharedsmoke: CreateDescriptorHeap(RTV) failed hr=0x%lx\n",
                (unsigned long)hr);
        return -1;
    }

    rtv = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(rt->rtv_heap);
    ID3D12Device_CreateRenderTargetView(rt->device, rt->resource, NULL, rtv);

    hr = ID3D12Device_CreateCommandAllocator(
        rt->device, D3D12_COMMAND_LIST_TYPE_DIRECT,
        &IID_ID3D12CommandAllocator, (void **)&rt->allocator);
    if (FAILED(hr) || !rt->allocator) {
        fprintf(stderr,
                "d3d12sharedsmoke: CreateCommandAllocator failed hr=0x%lx\n",
                (unsigned long)hr);
        return -1;
    }

    hr = ID3D12Device_CreateCommandList(
        rt->device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, rt->allocator, NULL,
        &IID_ID3D12GraphicsCommandList, (void **)&rt->command_list);
    if (FAILED(hr) || !rt->command_list) {
        fprintf(stderr,
                "d3d12sharedsmoke: CreateCommandList failed hr=0x%lx\n",
                (unsigned long)hr);
        return -1;
    }

    if (state_before_clear != D3D12_RESOURCE_STATE_RENDER_TARGET) {
        memset(&barrier, 0, sizeof(barrier));
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        barrier.Transition.pResource = rt->resource;
        barrier.Transition.Subresource =
            D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = state_before_clear;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        ID3D12GraphicsCommandList_ResourceBarrier(rt->command_list, 1,
                                                  &barrier);
    }

    ID3D12GraphicsCommandList_ClearRenderTargetView(rt->command_list, rtv,
                                                    clear_color, 0, NULL);
    memset(&barrier, 0, sizeof(barrier));
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource = rt->resource;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
    ID3D12GraphicsCommandList_ResourceBarrier(rt->command_list, 1, &barrier);

    hr = ID3D12GraphicsCommandList_Close(rt->command_list);
    if (FAILED(hr)) {
        fprintf(stderr,
                "d3d12sharedsmoke: Close clear command list failed hr=0x%lx\n",
                (unsigned long)hr);
        return -1;
    }

    lists[0] = (ID3D12CommandList *)rt->command_list;
    ID3D12CommandQueue_ExecuteCommandLists(rt->queue, 1, lists);
    rt->fence_value++;
    hr = ID3D12CommandQueue_Signal(rt->queue, rt->fence, rt->fence_value);
    if (FAILED(hr)) {
        fprintf(stderr,
                "d3d12sharedsmoke: command queue Signal after clear failed hr=0x%lx\n",
                (unsigned long)hr);
        return -1;
    }
    if (wait_for_fence_value(rt->fence, rt->fence_value) != 0) {
        fprintf(stderr,
                "d3d12sharedsmoke: clear fence wait failed target=%lu completed=%lu\n",
                (unsigned long)rt->fence_value,
                (unsigned long)ID3D12Fence_GetCompletedValue(rt->fence));
        return -1;
    }

    printf("d3d12sharedsmoke: runtime render target cleared target=%lu color=%g,%g,%g,%g state_before=%u state_after=COMMON\n",
           (unsigned long)rt->fence_value, clear_color[0], clear_color[1],
           clear_color[2], clear_color[3], (unsigned)state_before_clear);
    return 0;
}

static int d3d12_runtime_export_open_fence(
    struct d3d12_runtime *rt, const SECURITY_ATTRIBUTES *security_attrs_ptr,
    LPCWSTR fence_handle_name, const char *fence_handle_name_label,
    const char *phase, const char *diag_label, ID3D12Device *open_device,
    const char *open_device_label, D3D12_FENCE_FLAGS fence_flags,
    int skip_d3d12_open)
{
    HRESULT hr;
    HRESULT reason = 0;
    int open_errno;
    ID3D12Device *target_device = open_device ? open_device : rt->device;

    if (rt->opened_fence)
        return 0;
    if (rt->fence_handle && skip_d3d12_open) {
        printf("d3d12sharedsmoke: runtime OpenSharedHandle(fence) skipped phase=%s reason=dxg-syncfile-acquire existing-export uses pure-dxg-acquire-fd fd=%d acquire_sync=dxg-syncfile-acquire d3d12_fence_fd_used=0 d3d12_fence_open_expected_fail=1 native_present_claim=0\n",
               phase ? phase : "normal", handle_to_fd(rt->fence_handle));
        return 0;
    }

    printf("d3d12sharedsmoke: runtime CreateSharedHandle(fence) args phase=%s object=%p attrs=%p access=GENERIC_ALL/0x%lx name=%s/%p\n",
           phase ? phase : "normal", (void *)rt->fence,
           (const void *)security_attrs_ptr, (unsigned long)GENERIC_ALL,
           fence_handle_name_label ? fence_handle_name_label : "null",
           (const void *)fence_handle_name);
    hr = ID3D12Device_CreateSharedHandle(
        rt->device, (ID3D12DeviceChild *)rt->fence, security_attrs_ptr,
        GENERIC_ALL, fence_handle_name, &rt->fence_handle);
    if (FAILED(hr) || !rt->fence_handle) {
        reason = ID3D12Device_GetDeviceRemovedReason(rt->device);

        fprintf(stderr,
                "d3d12sharedsmoke: CreateSharedHandle(fence) failed phase=%s hr=0x%lx device_reason=0x%lx handle=%p\n",
                phase ? phase : "normal", (unsigned long)hr,
                (unsigned long)reason, rt->fence_handle);
        d3d12_runtime_print_export_result(diag_label, phase, "fence", 1,
                                          hr, reason, rt->fence_handle,
                                          rt->adapter_luid, 0, 0, 0,
                                          "n/a", "n/a", 0, 0);
        return -1;
    }
    d3d12_runtime_print_export_result(diag_label, phase, "fence", 1,
                                      hr, reason, rt->fence_handle,
                                      rt->adapter_luid, 0, 0, 0,
                                      "n/a", "n/a", 0, 0);
    printf("d3d12sharedsmoke: runtime CreateSharedHandle(fence) ok phase=%s fd=%d handle=%p flags=SHARED\n",
           phase ? phase : "normal", handle_to_fd(rt->fence_handle),
           rt->fence_handle);
    d3d12_print_handle_fd_diag(diag_label, "fence", rt->fence_handle);
    if (rt->diag_fence_dup_no_cloexec) {
        int old_fd = handle_to_fd(rt->fence_handle);
        int dup_fd;
        int dup_errno = 0;
        int setfd_errno = 0;
        int flags_before = old_fd >= 0 ? fcntl(old_fd, F_GETFD) : -1;
        int flags_after = -1;

        errno = 0;
        dup_fd = old_fd >= 0 ? fcntl(old_fd, F_DUPFD, 0) : -1;
        if (dup_fd < 0)
            dup_errno = errno;
        printf("d3d12sharedsmoke: runtime fence-dup-no-cloexec request phase=%s old_handle=%p old_fd=%d old_fcntl_flags=0x%x dup_fd=%d dup_errno=%d (%s)\n",
               phase ? phase : "normal", rt->fence_handle, old_fd,
               flags_before, dup_fd, dup_errno,
               dup_errno ? strerror(dup_errno) : "ok");
        if (dup_fd < 0)
            return -1;
        errno = 0;
        if (fcntl(dup_fd, F_SETFD, 0) != 0)
            setfd_errno = errno;
        flags_after = fcntl(dup_fd, F_GETFD);
        close_handle_fd(&rt->fence_handle);
        rt->fence_handle = (HANDLE)(intptr_t)dup_fd;
        printf("d3d12sharedsmoke: runtime fence-dup-no-cloexec result phase=%s new_handle=%p new_fd=%d new_fcntl_flags=0x%x setfd_errno=%d (%s)\n",
               phase ? phase : "normal", rt->fence_handle, dup_fd,
               flags_after, setfd_errno,
               setfd_errno ? strerror(setfd_errno) : "ok");
        d3d12_print_handle_fd_diag(diag_label, "fence-dup-no-cloexec",
                                   rt->fence_handle);
    }
    if (skip_d3d12_open) {
        printf("d3d12sharedsmoke: runtime OpenSharedHandle(fence) skipped phase=%s reason=dxg-syncfile-acquire uses pure-dxg-acquire-fd fd=%d acquire_sync=dxg-syncfile-acquire d3d12_fence_fd_used=0 d3d12_fence_open_expected_fail=1 native_present_claim=0\n",
               phase ? phase : "normal", handle_to_fd(rt->fence_handle));
        return 0;
    }
    printf("d3d12sharedsmoke: runtime OpenSharedHandle(fence) args phase=%s api=ID3D12Device_OpenSharedHandle open_device=%s handle=%p fd=%d iid=ID3D12Fence fence_flags=0x%x shared=%u cross_adapter=%u non_monitored=%u\n",
           phase ? phase : "normal",
           open_device_label ? open_device_label : "same_device",
           rt->fence_handle,
           handle_to_fd(rt->fence_handle), (unsigned)fence_flags,
           (fence_flags & D3D12_FENCE_FLAG_SHARED) != 0,
           (fence_flags & D3D12_FENCE_FLAG_SHARED_CROSS_ADAPTER) != 0,
           (fence_flags & D3D12_FENCE_FLAG_NON_MONITORED) != 0);
    errno = 0;
    hr = ID3D12Device_OpenSharedHandle(target_device, rt->fence_handle,
                                       &IID_ID3D12Fence,
                                       (void **)&rt->opened_fence);
    open_errno = errno;

    if (FAILED(hr) || !rt->opened_fence) {
        reason = ID3D12Device_GetDeviceRemovedReason(target_device);
        fprintf(stderr,
                "d3d12sharedsmoke: OpenSharedHandle(fence) failed phase=%s api=ID3D12Device_OpenSharedHandle open_device=%s handle=%p fd=%d iid=ID3D12Fence fence_flags=0x%x shared=%u cross_adapter=%u non_monitored=%u hr=0x%lx errno=%d (%s) device_reason=0x%lx object=%p object_returned=%u completed=n/a\n",
                phase ? phase : "normal",
                open_device_label ? open_device_label : "same_device",
                rt->fence_handle, handle_to_fd(rt->fence_handle),
                (unsigned)fence_flags,
                (fence_flags & D3D12_FENCE_FLAG_SHARED) != 0,
                (fence_flags & D3D12_FENCE_FLAG_SHARED_CROSS_ADAPTER) != 0,
                (fence_flags & D3D12_FENCE_FLAG_NON_MONITORED) != 0,
                (unsigned long)hr, open_errno,
                open_errno ? strerror(open_errno) : "ok",
                (unsigned long)reason, (void *)rt->opened_fence,
                rt->opened_fence != NULL);
        return -1;
    }
    printf("d3d12sharedsmoke: runtime OpenSharedHandle(fence) result phase=%s hr=0x%lx errno=%d (%s) device_reason=0x%lx object=%p object_returned=1 completed=%lu\n",
           phase ? phase : "normal", (unsigned long)hr,
           open_errno, open_errno ? strerror(open_errno) : "ok",
           (unsigned long)ID3D12Device_GetDeviceRemovedReason(target_device),
           (void *)rt->opened_fence,
           (unsigned long)ID3D12Fence_GetCompletedValue(rt->opened_fence));
    if (ID3D12Fence_GetCompletedValue(rt->opened_fence) < rt->fence_value) {
        fprintf(stderr,
                "d3d12sharedsmoke: opened fence initial value phase=%s is %lu, expected at least %lu\n",
                phase ? phase : "normal",
                (unsigned long)ID3D12Fence_GetCompletedValue(rt->opened_fence),
                (unsigned long)rt->fence_value);
        return -1;
    }
    printf("d3d12sharedsmoke: runtime OpenSharedHandle(fence) ok phase=%s completed=%lu expected_at_least=%lu\n",
           phase ? phase : "normal",
           (unsigned long)ID3D12Fence_GetCompletedValue(rt->opened_fence),
           (unsigned long)rt->fence_value);
    return 0;
}

static int d3d12_runtime_export_open_heap(
    struct d3d12_runtime *rt, const SECURITY_ATTRIBUTES *security_attrs_ptr,
    DWORD share_access, const char *share_access_name,
    const char *diag_label)
{
    HRESULT hr;
    HRESULT reason = 0;

    if (!rt || !rt->device || !rt->heap) {
        fprintf(stderr,
                "d3d12sharedsmoke: runtime heap export requested but no explicit heap exists; use --runtime-placed-resource\n");
        if (rt)
            d3d12_runtime_print_export_result(diag_label, "heap", "heap", 0,
                                              (HRESULT)0x80004005, 0, NULL,
                                              rt->adapter_luid, 0, 0, 0,
                                              "n/a", "n/a", 0, 0);
        return -1;
    }
    if (rt->opened_heap)
        return 0;

    printf("d3d12sharedsmoke: runtime CreateSharedHandle(heap) args object=heap heap=%p resource=%p attrs=%p access=%s/0x%lx name=null/(nil)\n",
           (void *)rt->heap, (void *)rt->resource,
           (const void *)security_attrs_ptr,
           share_access_name ? share_access_name : "custom",
           (unsigned long)share_access);
    hr = ID3D12Device_CreateSharedHandle(
        rt->device, (ID3D12DeviceChild *)rt->heap, security_attrs_ptr,
        share_access, NULL, &rt->heap_handle);
    if (FAILED(hr) || !rt->heap_handle) {
        reason = ID3D12Device_GetDeviceRemovedReason(rt->device);

        fprintf(stderr,
                "d3d12sharedsmoke: CreateSharedHandle(heap) failed hr=0x%lx device_reason=0x%lx handle=%p\n",
                (unsigned long)hr, (unsigned long)reason, rt->heap_handle);
        d3d12_runtime_print_export_result(diag_label, "heap", "heap", 1,
                                          hr, reason, rt->heap_handle,
                                          rt->adapter_luid, 0, 0, 0,
                                          "n/a", "n/a", 0, 0);
        return -1;
    }
    d3d12_runtime_print_export_result(diag_label, "heap", "heap", 1,
                                      hr, reason, rt->heap_handle,
                                      rt->adapter_luid, 0, 0, 0,
                                      "n/a", "n/a", 0, 0);
    printf("d3d12sharedsmoke: runtime CreateSharedHandle(heap) ok fd=%d handle=%p access=%s/0x%lx\n",
           handle_to_fd(rt->heap_handle), rt->heap_handle,
           share_access_name ? share_access_name : "custom",
           (unsigned long)share_access);
    hr = ID3D12Device_OpenSharedHandle(rt->device, rt->heap_handle,
                                       &IID_ID3D12Heap,
                                       (void **)&rt->opened_heap);
    if (FAILED(hr) || !rt->opened_heap) {
        fprintf(stderr,
                "d3d12sharedsmoke: OpenSharedHandle(heap) failed hr=0x%lx\n",
                (unsigned long)hr);
        return -1;
    }
    printf("d3d12sharedsmoke: runtime OpenSharedHandle(heap) ok heap=%p\n",
           (void *)rt->opened_heap);
    return 0;
}

static int d3d12_runtime_create_shared_fence(
    struct d3d12_runtime *rt, uint64_t initial_value, D3D12_FENCE_FLAGS flags,
    const struct d3d12_runtime_options *opts)
{
    HRESULT hr;

    printf("d3d12sharedsmoke: runtime CreateFence args initial=%lu flags=0x%x shared=%u cross_adapter=%u non_monitored=%u\n",
           (unsigned long)initial_value, (unsigned)flags,
           (flags & D3D12_FENCE_FLAG_SHARED) != 0,
           (flags & D3D12_FENCE_FLAG_SHARED_CROSS_ADAPTER) != 0,
           (flags & D3D12_FENCE_FLAG_NON_MONITORED) != 0);
    hr = ID3D12Device_CreateFence(rt->device, initial_value, flags,
                                  &IID_ID3D12Fence, (void **)&rt->fence);
    if (FAILED(hr) || !rt->fence) {
        HRESULT reason = ID3D12Device_GetDeviceRemovedReason(rt->device);

        fprintf(stderr,
                "d3d12sharedsmoke: CreateFence failed hr=0x%lx device_reason=0x%lx initial=%lu flags=0x%x fence=%p\n",
                (unsigned long)hr, (unsigned long)reason,
                (unsigned long)initial_value, (unsigned)flags, rt->fence);
        d3d12_runtime_print_export_result(
            d3d12_runtime_export_label(opts), "create-fence", "fence", 0,
            hr, reason, NULL, rt->adapter_luid, 0, 0, 0,
            "n/a", "n/a", 0, 0);
        return -1;
    }
    rt->fence_value = initial_value;
    printf("d3d12sharedsmoke: runtime CreateFence ok fence=%p requested_flags=0x%x completed=%lu\n",
           (void *)rt->fence, (unsigned)flags,
           (unsigned long)ID3D12Fence_GetCompletedValue(rt->fence));
    if (opts && opts->name_objects) {
        hr = ID3D12Fence_SetName(rt->fence, L"xv6-d3d12sharedsmoke-fence");
        printf("d3d12sharedsmoke: runtime fence SetName hr=0x%lx\n",
               (unsigned long)hr);
    }
    return 0;
}

static int d3d12_runtime_create(struct d3d12_runtime *rt,
                                const struct d3d12_runtime_options *opts)
{
    D3D12_COMMAND_QUEUE_DESC queue_desc;
    D3D12_HEAP_PROPERTIES heap_props;
    D3D12_HEAP_PROPERTIES actual_heap_props;
    D3D12_HEAP_DESC heap_desc;
    D3D12_RESOURCE_ALLOCATION_INFO alloc_info;
    D3D12_RESOURCE_DESC resource_desc;
    D3D12_RESOURCE_DESC opened_desc;
    D3D12_CLEAR_VALUE clear_value;
    D3D12_HEAP_FLAGS actual_heap_flags;
    D3D12_HEAP_FLAGS requested_heap_flags = D3D12_HEAP_FLAG_SHARED;
    D3D12_RESOURCE_STATES initial_state = D3D12_RESOURCE_STATE_COMMON;
    D3D12_CLEAR_VALUE *clear_value_ptr = NULL;
    D3D12_RESOURCE_FLAGS resource_flags = D3D12_RESOURCE_FLAG_NONE;
    SECURITY_ATTRIBUTES security_attrs;
    const SECURITY_ATTRIBUTES *security_attrs_ptr = NULL;
    LPCWSTR resource_handle_name = NULL;
    LPCWSTR fence_handle_name = NULL;
    const char *resource_handle_name_label = "null";
    const char *fence_handle_name_label = "null";
    DWORD resource_share_access = GENERIC_ALL;
    const char *resource_share_access_name = "generic-all";
    D3D12_FENCE_FLAGS fence_flags = D3D12_FENCE_FLAG_SHARED;
    uint64_t fence_initial_value = 0;
    int touch_clears_resource;
    struct winluid selected_luid;
    HRESULT heap_hr;
    HRESULT hr;
    int prealloc_info_queried;
    int app_sync_suppressed;
    int preexport_diagnostics;
    uintptr_t resource_va;
    uint64_t resource_high_token;
    const char *export_label = d3d12_runtime_export_label(opts);
    const char *adapter_select = opts && opts->wsl_adapter_list ?
        "wsl_list" : "dxg_luid";

    memset(rt, 0, sizeof(*rt));
    memset(&selected_luid, 0, sizeof(selected_luid));
    if (opts && opts->fence_cross_adapter)
        fence_flags |= D3D12_FENCE_FLAG_SHARED_CROSS_ADAPTER;
    rt->diag_fence_flags = fence_flags;
    rt->diag_fence_dup_no_cloexec =
        opts && opts->runtime_import_fence_dup_no_cloexec;
    rt->diag_adapter_path = adapter_select;
    preexport_diagnostics =
        !opts || !opts->wsl_parity || opts->preexport_diagnostics;
    printf("d3d12sharedsmoke: runtime option-plan label=%s adapter_select=%s direct_admission=%u wsl_adapter_list=%u precheck_dxg_luid=%u fallback_on_empty_wsl_list=%u runtime_import_contract=%u runtime_import_independent_device=%u runtime_import_fence_first=%u runtime_import_fence_dup_no_cloexec=%u runtime_import_fence_cross_adapter=%u export_fence_first=%u fence_flags=0x%x fence_shared=%u fence_cross_adapter=%u\n",
           export_label, adapter_select,
           opts && !opts->wsl_adapter_list,
           opts ? opts->wsl_adapter_list : 0,
           opts ? opts->precheck_dxg_luid : 0,
           opts ? opts->fallback_dxg_luid_on_empty_wsl_list : 0,
           opts ? opts->runtime_import_contract : 0,
           opts ? opts->runtime_import_independent_device : 0,
           opts ? opts->runtime_import_fence_first : 0,
           opts ? opts->runtime_import_fence_dup_no_cloexec : 0,
           opts ? opts->runtime_import_fence_cross_adapter : 0,
           opts ? opts->export_fence_first : 0,
           (unsigned)fence_flags,
           (fence_flags & D3D12_FENCE_FLAG_SHARED) != 0,
           (fence_flags & D3D12_FENCE_FLAG_SHARED_CROSS_ADAPTER) != 0);
    if (opts && opts->wsl_adapter_list) {
        int empty_wsl_adapter_list = 0;

        if (d3d12_create_device_from_wsl_adapter_list(&selected_luid,
                                                      &rt->device,
                                                      opts->precheck_dxg_luid,
                                                      preexport_diagnostics,
                                                      &empty_wsl_adapter_list,
                                                      export_label) != 0) {
            if (!empty_wsl_adapter_list ||
                !opts->fallback_dxg_luid_on_empty_wsl_list)
                return -1;
            printf("d3d12sharedsmoke: dxg-luid fallback attempted reason=wsl-list-empty\n");
            if (d3d12_create_device_for_dxg_luid(&selected_luid,
                                                 &rt->device,
                                                 export_label) != 0) {
                fprintf(stderr,
                        "d3d12sharedsmoke: dxg-luid fallback failed reason=wsl-list-empty\n");
                return -1;
            }
            adapter_select = "dxg_luid_fallback";
            printf("d3d12sharedsmoke: dxg-luid fallback succeeded reason=wsl-list-empty\n");
        }
    } else {
        if (d3d12_create_device_for_dxg_luid(&selected_luid,
                                             &rt->device,
                                             export_label) != 0)
            return -1;
    }

    if (preexport_diagnostics) {
        char runtime_luid_text[32];
        LUID device_luid;

        device_luid = ID3D12Device_GetAdapterLuid(rt->device);
        rt->adapter_luid.a = (uint32_t)device_luid.LowPart;
        rt->adapter_luid.b = (uint32_t)device_luid.HighPart;
        if ((selected_luid.a || selected_luid.b) &&
            (selected_luid.a != rt->adapter_luid.a ||
             selected_luid.b != rt->adapter_luid.b)) {
            char selected_text[32];
            char device_text[32];

            format_winluid_text(selected_text, sizeof(selected_text),
                                selected_luid);
            format_winluid_text(device_text, sizeof(device_text),
                                rt->adapter_luid);
            fprintf(stderr,
                    "d3d12sharedsmoke: runtime device LUID mismatch dxg=%s d3d12=%s dxg_low=0x%08x dxg_high=0x%08x d3d12_low=0x%08x d3d12_high=0x%08x\n",
                    selected_text, device_text, selected_luid.a,
                    selected_luid.b, rt->adapter_luid.a,
                    rt->adapter_luid.b);
            return -1;
        }

        format_winluid_text(runtime_luid_text, sizeof(runtime_luid_text),
                            rt->adapter_luid);
        printf("d3d12sharedsmoke: runtime device luid=%s low=0x%08x high=0x%08x nodes=%u adapter_select=%s\n",
               runtime_luid_text, rt->adapter_luid.a, rt->adapter_luid.b,
               ID3D12Device_GetNodeCount(rt->device), adapter_select);
    } else {
        rt->adapter_luid = selected_luid;
        printf("d3d12sharedsmoke: runtime device diagnostics deferred adapter_select=%s preexport_diagnostics=0\n",
               adapter_select);
    }

    memset(&queue_desc, 0, sizeof(queue_desc));
    queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    queue_desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
    queue_desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    app_sync_suppressed = opts && (opts->resource_only ||
                                   opts->suppress_app_sync);

    if (!app_sync_suppressed) {
        hr = ID3D12Device_CreateCommandQueue(rt->device, &queue_desc,
                                             &IID_ID3D12CommandQueue,
                                             (void **)&rt->queue);
        if (FAILED(hr) || !rt->queue) {
            fprintf(stderr,
                    "d3d12sharedsmoke: CreateCommandQueue failed hr=0x%lx\n",
                    (unsigned long)hr);
            return -1;
        }
    }
    if (opts && opts->security_attrs) {
        memset(&security_attrs, 0, sizeof(security_attrs));
        security_attrs.nLength = sizeof(security_attrs);
        security_attrs.lpSecurityDescriptor = NULL;
        security_attrs.bInheritHandle = 0;
        security_attrs_ptr = &security_attrs;
    }
    if (opts && opts->named_resource_handle) {
        resource_handle_name = L"xv6-d3d12sharedsmoke-resource";
        resource_handle_name_label = "xv6-d3d12sharedsmoke-resource";
    }
    if (opts && opts->named_fence_handle) {
        fence_handle_name = L"xv6-d3d12sharedsmoke-fence";
        fence_handle_name_label = "xv6-d3d12sharedsmoke-fence";
    }
    if (opts && opts->fence_only) {
        printf("d3d12sharedsmoke: runtime fence-only mode wsl_parity=%u security_attrs=%u cross_adapter=%u fence_name=%s\n",
               opts->wsl_parity, opts->security_attrs,
               opts->fence_cross_adapter, fence_handle_name_label);
        if (d3d12_runtime_create_shared_fence(rt, fence_initial_value,
                                             fence_flags, opts) != 0)
            return -1;
        if (d3d12_runtime_export_open_fence(rt, security_attrs_ptr,
                                            fence_handle_name,
                                            fence_handle_name_label,
                                            "fence-only",
                                            export_label, rt->device,
                                            "same_device", fence_flags,
                                            0) != 0)
            return -1;
        rt->fence_value++;
        hr = ID3D12CommandQueue_Signal(rt->queue, rt->fence,
                                       rt->fence_value);
        if (FAILED(hr)) {
            fprintf(stderr,
                    "d3d12sharedsmoke: fence-only Signal failed hr=0x%lx\n",
                    (unsigned long)hr);
            return -1;
        }
        if (wait_for_fence_value(rt->opened_fence, rt->fence_value) != 0) {
            fprintf(stderr,
                    "d3d12sharedsmoke: fence-only wait failed target=%lu completed=%lu\n",
                    (unsigned long)rt->fence_value,
                    (unsigned long)ID3D12Fence_GetCompletedValue(rt->opened_fence));
            return -1;
        }
        printf("d3d12sharedsmoke: runtime fence-only ok fd=%d target=%lu completed=%lu\n",
               handle_to_fd(rt->fence_handle),
               (unsigned long)rt->fence_value,
               (unsigned long)ID3D12Fence_GetCompletedValue(rt->opened_fence));
        return 0;
    }

    heap_props = d3d12_default_heap_properties(
        rt->device, opts && opts->custom_heap_props);

    memset(&resource_desc, 0, sizeof(resource_desc));
    resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resource_desc.Width = opts && opts->width ? opts->width : 256;
    resource_desc.Height = opts && opts->height ? opts->height : 256;
    resource_desc.DepthOrArraySize = 1;
    resource_desc.MipLevels = 1;
    resource_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    resource_desc.SampleDesc.Count = 1;
    resource_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    if (!opts || opts->simultaneous)
        resource_flags |= D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS;
    if (opts && opts->render_target) {
        resource_flags &= ~D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS;
        resource_flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        initial_state = opts->initial_common ?
            D3D12_RESOURCE_STATE_COMMON : D3D12_RESOURCE_STATE_RENDER_TARGET;
        memset(&clear_value, 0, sizeof(clear_value));
        clear_value.Format = resource_desc.Format;
        if (!opts || !opts->clear_alpha_only) {
            clear_value.Color[0] = 0.125f;
            clear_value.Color[1] = 0.25f;
            clear_value.Color[2] = 0.5f;
        }
        clear_value.Color[3] = 1.0f;
        if (!opts || !opts->omit_clear_value)
            clear_value_ptr = &clear_value;
    }
    if (opts && opts->resource_cross_adapter) {
        resource_flags |= D3D12_RESOURCE_FLAG_ALLOW_CROSS_ADAPTER;
        requested_heap_flags |= D3D12_HEAP_FLAG_SHARED_CROSS_ADAPTER;
    }
    touch_clears_resource =
        opts && opts->touch_before_export && opts->render_target;
    resource_desc.Flags = resource_flags;
    memset(&alloc_info, 0, sizeof(alloc_info));
    prealloc_info_queried =
        !opts || !opts->skip_prealloc_info || opts->placed_resource;
    if (prealloc_info_queried)
        alloc_info = ID3D12Device_GetResourceAllocationInfo(rt->device, 0, 1,
                                                            &resource_desc);
    if (opts && opts->placed_resource && opts->render_target)
        requested_heap_flags |= D3D12_HEAP_FLAG_ALLOW_ONLY_RT_DS_TEXTURES;
    if (opts && opts->zero_heap_flags && !opts->placed_resource)
        requested_heap_flags = D3D12_HEAP_FLAG_NONE;
    if (opts && opts->share_access_override) {
        resource_share_access = (DWORD)opts->share_access;
        resource_share_access_name = opts->share_access_name ?
            opts->share_access_name : "custom";
    }
    rt->diag_resource_flags = resource_desc.Flags;
    rt->diag_heap_flags = requested_heap_flags;
    rt->diag_initial_state = initial_state;
    rt->diag_app_sync_suppressed = app_sync_suppressed;
    rt->diag_adapter_path = adapter_select;

    printf("d3d12sharedsmoke: runtime shape=%s label=%s size=%lux%u creation=%s wsl_parity=%u wsl_resource_shape=%u wsl_resource_shape_direct=%u wsl_resource_shape_shared_heap=%u wsl_resource_shape_shared_heap_export_only=%u wsl_success_shape_wsl_list_default_export_only=%u wsl_success_shape_no_clear_value_export_only=%u wsl_success_shape_initial_rt_export_only=%u wsl_success_shape_reserve_low_va_export_only=%u wsl_success_shape_resource_cross_adapter_export_only=%u wsl_success_shape_prealloc_info_export_only=%u wsl_resource_shape_no_heap_flags_export_only=%u wsl_resource_shape_placed_shared_heap_export_only=%u wsl_resource_shape_app_sync_export_only=%u resource_only=%u runtime_export_only=%u runtime_import_contract=%u runtime_import_fence_first=%u runtime_import_fence_dup_no_cloexec=%u runtime_import_fence_cross_adapter=%u app_sync=%s simultaneous=%u touch_requested=%u cleared_before_export=%u initial_common=%u clear_alpha_only=%u omit_clear_value=%u custom_heap_props=%u preexport_diag=%u prealloc_info=%u wsl_adapter_list=%u reserve_low_va=%u make_resident=%u signal_before_export=%u export_fence_first=%u fence_cross_adapter=%u resource_cross_adapter=%u export_heap_first=%u heap_only=%u zero_heap_flags=%u name_objects=%u security_attrs=%u resource_name=%s fence_name=%s share_access=%s/0x%lx heap_flags=0x%x resource_flags=0x%x initial_state=%u clear_value=%u clear_ptr=%p clear_size=%lu alloc=%llu align=%llu\n",
           d3d12_runtime_shape_name(opts), export_label,
           (unsigned long)resource_desc.Width, resource_desc.Height,
           opts && opts->placed_resource ? "placed" : "committed",
           opts ? opts->wsl_parity : 0,
           opts ? opts->wsl_resource_shape : 0,
           opts ? opts->wsl_resource_shape_direct : 0,
           opts ? opts->wsl_resource_shape_shared_heap : 0,
           opts ? opts->wsl_resource_shape_shared_heap_export_only : 0,
           opts ? opts->wsl_success_shape_wsl_list_default_export_only : 0,
           opts ? opts->wsl_success_shape_no_clear_value_export_only : 0,
           opts ? opts->wsl_success_shape_initial_rt_export_only : 0,
           opts ? opts->wsl_success_shape_reserve_low_va_export_only : 0,
           opts ? opts->wsl_success_shape_resource_cross_adapter_export_only : 0,
           opts ? opts->wsl_success_shape_prealloc_info_export_only : 0,
           opts ? opts->wsl_resource_shape_no_heap_flags_export_only : 0,
           opts ? opts->wsl_resource_shape_placed_shared_heap_export_only : 0,
           opts ? opts->wsl_resource_shape_app_sync_export_only : 0,
           opts ? opts->resource_only : 0,
           opts ? opts->runtime_export_only : 0,
           opts ? opts->runtime_import_contract : 0,
           opts ? opts->runtime_import_fence_first : 0,
           opts ? opts->runtime_import_fence_dup_no_cloexec : 0,
           opts ? opts->runtime_import_fence_cross_adapter : 0,
           app_sync_suppressed ? "suppressed" : "enabled",
           opts ? opts->simultaneous : 1,
           opts ? opts->touch_before_export : 0, touch_clears_resource,
           opts ? opts->initial_common : 0,
           opts ? opts->clear_alpha_only : 0,
           opts ? opts->omit_clear_value : 0,
           opts ? opts->custom_heap_props : 0,
           preexport_diagnostics,
           prealloc_info_queried,
           opts ? opts->wsl_adapter_list : 0,
           opts ? opts->reserve_low_va : 0,
           opts ? opts->make_resident_before_export : 0,
           opts ? opts->signal_before_export : 0,
           opts ? opts->export_fence_first : 0,
           opts ? opts->fence_cross_adapter : 0,
           opts ? opts->resource_cross_adapter : 0,
           opts ? opts->export_heap_first : 0,
           opts ? opts->heap_only : 0,
           opts ? opts->zero_heap_flags : 0,
           opts ? opts->name_objects : 0,
           opts ? opts->security_attrs : 0,
           resource_handle_name_label,
           fence_handle_name_label,
           resource_share_access_name,
           (unsigned long)resource_share_access,
           (unsigned)requested_heap_flags,
           (unsigned)resource_desc.Flags, (unsigned)initial_state,
           clear_value_ptr != NULL, (void *)clear_value_ptr,
           (unsigned long)sizeof(clear_value),
           (unsigned long long)alloc_info.SizeInBytes,
           (unsigned long long)alloc_info.Alignment);
    printf("d3d12sharedsmoke: runtime resource desc dim=%u align=%lu width=%lu height=%u depth=%u mips=%u fmt=%u samples=%u quality=%u layout=%u flags=0x%x heap_type=%u cpu_page=%u mem_pool=%u node=%u visible=%u\n",
           (unsigned)resource_desc.Dimension,
           (unsigned long)resource_desc.Alignment,
           (unsigned long)resource_desc.Width, resource_desc.Height,
           resource_desc.DepthOrArraySize, resource_desc.MipLevels,
           (unsigned)resource_desc.Format, resource_desc.SampleDesc.Count,
           resource_desc.SampleDesc.Quality,
           (unsigned)resource_desc.Layout, (unsigned)resource_desc.Flags,
           (unsigned)heap_props.Type, (unsigned)heap_props.CPUPageProperty,
           (unsigned)heap_props.MemoryPoolPreference,
           heap_props.CreationNodeMask, heap_props.VisibleNodeMask);
    if (clear_value_ptr) {
        printf("d3d12sharedsmoke: runtime clear value fmt=%u color=%g,%g,%g,%g\n",
               (unsigned)clear_value_ptr->Format, clear_value_ptr->Color[0],
               clear_value_ptr->Color[1], clear_value_ptr->Color[2],
               clear_value_ptr->Color[3]);
    }
    if (opts && (opts->preexport_diagnostics || opts->runtime_export_only ||
                 opts->wsl_resource_shape)) {
        uint32_t clear_value_addr_low = clear_value_ptr ?
            (uint32_t)(uintptr_t)clear_value_ptr : 0;
        uint32_t clear_value_addr_high = clear_value_ptr ?
            (uint32_t)((uintptr_t)clear_value_ptr >> 32) : 0;
        uint32_t resource_desc_addr_low =
            (uint32_t)(uintptr_t)&resource_desc;
        uint32_t resource_desc_addr_high =
            (uint32_t)((uintptr_t)&resource_desc >> 32);

        printf("d3d12sharedsmoke: runtime create-descs label=%s creation=%s committed_heap_flags=0x%x heap_desc_size=%llu heap_desc_align=%llu heap_desc_flags=0x%x heap_type=%u heap_cpu_page=%u heap_mem_pool=%u heap_node=%u heap_visible=%u resource_dim=%u resource_align=%lu resource_width=%lu resource_height=%u resource_depth=%u resource_mips=%u resource_format=%u resource_samples=%u resource_quality=%u resource_layout=%u resource_flags=0x%x initial_state=%u clear_value=%u prealloc_info=%u alloc=%llu align=%llu\n",
               export_label,
               opts && opts->placed_resource ? "placed" : "committed",
               (unsigned)requested_heap_flags,
               opts && opts->placed_resource ?
                   (unsigned long long)alloc_info.SizeInBytes : 0ULL,
               opts && opts->placed_resource ?
                   (unsigned long long)alloc_info.Alignment : 0ULL,
               opts && opts->placed_resource ?
                   (unsigned)requested_heap_flags : 0,
               (unsigned)heap_props.Type,
               (unsigned)heap_props.CPUPageProperty,
               (unsigned)heap_props.MemoryPoolPreference,
               heap_props.CreationNodeMask, heap_props.VisibleNodeMask,
               (unsigned)resource_desc.Dimension,
               (unsigned long)resource_desc.Alignment,
               (unsigned long)resource_desc.Width, resource_desc.Height,
               resource_desc.DepthOrArraySize, resource_desc.MipLevels,
               (unsigned)resource_desc.Format,
               resource_desc.SampleDesc.Count,
               resource_desc.SampleDesc.Quality,
               (unsigned)resource_desc.Layout, (unsigned)resource_desc.Flags,
               (unsigned)initial_state, clear_value_ptr != NULL,
               prealloc_info_queried,
               (unsigned long long)alloc_info.SizeInBytes,
               (unsigned long long)alloc_info.Alignment);
        printf("d3d12sharedsmoke: runtime private-offset-inputs label=%s q38_candidate_resource_hi=0x00000000 d3c_candidate_resource_hi32=0x00000000 clear_ptr_low=0x%08x clear_ptr_high=0x%08x desc_ptr_low=0x%08x desc_ptr_high=0x%08x clear_value_present=%u initial_state=%u resource_flags=0x%x heap_flags=0x%x\n",
               export_label, clear_value_addr_low, clear_value_addr_high,
               resource_desc_addr_low, resource_desc_addr_high,
               clear_value_ptr != NULL, (unsigned)initial_state,
               (unsigned)resource_desc.Flags, (unsigned)requested_heap_flags);
        d3d12_print_compact_bytes(export_label, "resource_desc", 0,
                                  &resource_desc, sizeof(resource_desc), 64);
        d3d12_print_compact_bytes(export_label, "heap_props", 0,
                                  &heap_props, sizeof(heap_props), 48);
        if (clear_value_ptr)
            d3d12_print_compact_bytes(export_label, "clear_value", 0,
                                      clear_value_ptr, sizeof(*clear_value_ptr),
                                      sizeof(*clear_value_ptr));
    }

    if (opts && opts->placed_resource) {
        memset(&heap_desc, 0, sizeof(heap_desc));
        heap_desc.SizeInBytes = alloc_info.SizeInBytes;
        heap_desc.Properties = heap_props;
        heap_desc.Alignment = alloc_info.Alignment;
        heap_desc.Flags = requested_heap_flags;
        hr = ID3D12Device_CreateHeap(rt->device, &heap_desc, &IID_ID3D12Heap,
                                     (void **)&rt->heap);
        if (FAILED(hr) || !rt->heap) {
            fprintf(stderr,
                    "d3d12sharedsmoke: CreateHeap failed hr=0x%lx size=%llu align=%llu flags=0x%x\n",
                    (unsigned long)hr,
                    (unsigned long long)heap_desc.SizeInBytes,
                    (unsigned long long)heap_desc.Alignment,
                    (unsigned)heap_desc.Flags);
            d3d12_runtime_print_export_result(export_label, "create-heap",
                                              "heap", 0, hr, 0, NULL,
                                              rt->adapter_luid,
                                              resource_desc.Flags,
                                              requested_heap_flags,
                                              initial_state,
                                              app_sync_suppressed ?
                                                  "suppressed" : "enabled",
                                              adapter_select, 0, 0);
            d3d12_runtime_print_phase_result(
                export_label, "qai-type0-create-heap", "heap", 1, 0,
                hr, 0, NULL, rt->adapter_luid, "export_only");
            return -1;
        }
        hr = ID3D12Device_CreatePlacedResource(
            rt->device, rt->heap, 0, &resource_desc, initial_state,
            clear_value_ptr, &IID_ID3D12Resource, (void **)&rt->resource);
    } else {
        hr = ID3D12Device_CreateCommittedResource(
            rt->device, &heap_props, requested_heap_flags, &resource_desc,
            initial_state, clear_value_ptr, &IID_ID3D12Resource,
            (void **)&rt->resource);
    }
    if (FAILED(hr) || !rt->resource) {
        fprintf(stderr,
                "d3d12sharedsmoke: runtime resource create failed hr=0x%lx shape=%s size=%lux%u creation=%s resource_flags=0x%x initial_state=%u\n",
                (unsigned long)hr, d3d12_runtime_shape_name(opts),
                (unsigned long)resource_desc.Width, resource_desc.Height,
                opts && opts->placed_resource ? "placed" : "committed",
                (unsigned)resource_desc.Flags, (unsigned)initial_state);
        d3d12_runtime_print_export_result(export_label, "create-resource",
                                          "resource", 0, hr, 0, NULL,
                                          rt->adapter_luid,
                                          resource_desc.Flags,
                                          requested_heap_flags,
                                          initial_state,
                                          app_sync_suppressed ?
                                              "suppressed" : "enabled",
                                          adapter_select, 0, 0);
        d3d12_runtime_print_phase_result(
            export_label, "qai-type0-create-resource", "resource", 1, 0,
            hr, 0, NULL, rt->adapter_luid, "export_only");
        return -1;
    }
    d3d12_runtime_print_phase_result(
        export_label, "qai-type0-create-resource", "resource", 1, 0,
        hr, 0, NULL, rt->adapter_luid, "export_only");
    memset(&actual_heap_props, 0, sizeof(actual_heap_props));
    actual_heap_flags = D3D12_HEAP_FLAG_NONE;
    heap_hr = 0;
    if (preexport_diagnostics)
        heap_hr = ID3D12Resource_GetHeapProperties(rt->resource,
                                                   &actual_heap_props,
                                                   &actual_heap_flags);
    if (opts && opts->name_objects) {
        hr = ID3D12Resource_SetName(rt->resource,
                                    L"xv6-d3d12sharedsmoke-resource");
        printf("d3d12sharedsmoke: runtime resource SetName hr=0x%lx\n",
               (unsigned long)hr);
    }

    resource_va = (uintptr_t)rt->resource;
    resource_high_token = ((uint64_t)(resource_va >> 32)) << 32;
    printf("d3d12sharedsmoke: runtime resource create shape=%s size=%lux%u creation=%s heap_flags=0x%x preexport_diag=%u reserve_low_va=%u actual_heap_hr=0x%lx actual_heap_flags=0x%x heap_type=%u cpu_page=%u mem_pool=%u resource_flags=0x%x layout=%u format=%u initial_state=%u alloc=%llu align=%llu\n",
           d3d12_runtime_shape_name(opts),
           (unsigned long)resource_desc.Width, resource_desc.Height,
           opts && opts->placed_resource ? "placed" : "committed",
           (unsigned)requested_heap_flags, preexport_diagnostics,
           opts ? opts->reserve_low_va : 0,
           (unsigned long)heap_hr,
           (unsigned)actual_heap_flags, (unsigned)actual_heap_props.Type,
           (unsigned)actual_heap_props.CPUPageProperty,
           (unsigned)actual_heap_props.MemoryPoolPreference,
           (unsigned)resource_desc.Flags, (unsigned)resource_desc.Layout,
           (unsigned)resource_desc.Format, (unsigned)initial_state,
           (unsigned long long)alloc_info.SizeInBytes,
           (unsigned long long)alloc_info.Alignment);
    printf("d3d12sharedsmoke: runtime resource pointer object=%p va_high32=0x%lx q38_token=0x%lx below4g=%u reserve_low_va=%u\n",
           (void *)rt->resource, (unsigned long)(resource_va >> 32),
           (unsigned long)resource_high_token,
           resource_va < 0x100000000ULL,
           opts ? opts->reserve_low_va : 0);
    if (opts && (opts->preexport_diagnostics || opts->runtime_export_only ||
                 opts->wsl_resource_shape)) {
        printf("d3d12sharedsmoke: runtime private-offset-resource label=%s q38_candidate_resource_hi_token=0x%lx d3c_candidate_resource_hi32=0x%08x resource_ptr_low=0x%08x resource_ptr_high=0x%08x below4g=%u reserve_low_va=%u\n",
               export_label, (unsigned long)resource_high_token,
               (unsigned)(resource_va >> 32), (unsigned)resource_va,
               (unsigned)(resource_va >> 32), resource_va < 0x100000000ULL,
               opts ? opts->reserve_low_va : 0);
    }

    if (!app_sync_suppressed) {
        if (d3d12_runtime_create_shared_fence(rt, fence_initial_value,
                                              fence_flags, opts) != 0)
            return -1;
    }
    if (!app_sync_suppressed && touch_clears_resource &&
        d3d12_runtime_clear_render_target(rt, initial_state) != 0)
        return -1;

    if (opts && opts->make_resident_before_export) {
        ID3D12Pageable *pageable = (ID3D12Pageable *)rt->resource;

        hr = ID3D12Device_MakeResident(rt->device, 1, &pageable);
        if (FAILED(hr)) {
            HRESULT reason = ID3D12Device_GetDeviceRemovedReason(rt->device);

            fprintf(stderr,
                    "d3d12sharedsmoke: MakeResident before export failed hr=0x%lx device_reason=0x%lx\n",
                    (unsigned long)hr, (unsigned long)reason);
            d3d12_runtime_print_export_result(
                export_label, "make-resident-before-export", "resource", 0,
                hr, reason, NULL, rt->adapter_luid, resource_desc.Flags,
                requested_heap_flags, initial_state,
                app_sync_suppressed ? "suppressed" : "enabled",
                adapter_select, 0, 0);
            return -1;
        }
        d3d12_runtime_print_export_result(
            export_label, "make-resident-before-export", "resource", 0,
            hr, 0, NULL, rt->adapter_luid, resource_desc.Flags,
            requested_heap_flags, initial_state,
            app_sync_suppressed ? "suppressed" : "enabled",
            adapter_select, 0, 0);
        printf("d3d12sharedsmoke: runtime MakeResident before export ok\n");
    }

    if (opts && !app_sync_suppressed && opts->signal_before_export) {
        rt->fence_value++;
        hr = ID3D12CommandQueue_Signal(rt->queue, rt->fence,
                                       rt->fence_value);
        if (FAILED(hr)) {
            fprintf(stderr,
                    "d3d12sharedsmoke: command queue Signal before export failed hr=0x%lx\n",
                    (unsigned long)hr);
            return -1;
        }
        if (wait_for_fence_value(rt->fence, rt->fence_value) != 0) {
            fprintf(stderr,
                    "d3d12sharedsmoke: pre-export fence wait failed target=%lu completed=%lu\n",
                    (unsigned long)rt->fence_value,
                    (unsigned long)ID3D12Fence_GetCompletedValue(rt->fence));
            return -1;
        }
        printf("d3d12sharedsmoke: runtime pre-export fence signal/wait target=%lu\n",
               (unsigned long)rt->fence_value);
    }

    if (opts && !app_sync_suppressed && opts->export_fence_first &&
        d3d12_runtime_export_open_fence(rt, security_attrs_ptr,
                                        fence_handle_name,
                                        fence_handle_name_label,
                                        "before-resource",
                                        export_label, rt->device,
                                        "same_device", fence_flags,
                                        opts && opts->runtime_dxg_syncfile_acquire) != 0) {
        if (!(opts->runtime_import_contract && rt->fence_handle))
            return -1;
        printf("d3d12sharedsmoke: runtime D3D12 fence OpenSharedHandle failed before resource; continuing import-contract for direct DXG fence import fence_fd=%d\n",
               handle_to_fd(rt->fence_handle));
    }

    if (opts && (opts->export_heap_first || opts->heap_only)) {
        if (d3d12_runtime_export_open_heap(rt, security_attrs_ptr,
                                           resource_share_access,
                                           resource_share_access_name,
                                           export_label) != 0)
            return -1;
        if (opts->heap_only) {
            printf("d3d12sharedsmoke: runtime heap-only validation ok fd=%d\n",
                   handle_to_fd(rt->heap_handle));
            return 0;
        }
    }

    if (opts && (opts->wsl_resource_shape || preexport_diagnostics ||
                 opts->share_access_override || opts->security_attrs ||
                 opts->make_resident_before_export ||
                 opts->signal_before_export || opts->export_heap_first)) {
        printf("d3d12sharedsmoke: runtime pre-export-metadata label=%s handle_kind=resource nt_share_expected=1 creation=%s object=%p heap=%p fence=%p resource_flags=0x%x heap_flags=0x%x actual_heap_hr=0x%lx actual_heap_flags=0x%x actual_heap_type=%u actual_heap_cpu_page=%u actual_heap_mem_pool=%u initial_state=%u app_sync=%s adapter_path=%s security_attrs=%u resource_name=%s share_access=%s/0x%lx desc_dim=%u width=%lu height=%u fmt=%u layout=%u clear_value=%u clear_ptr=%p prealloc_info=%u alloc=%llu align=%llu make_resident=%u signal_before_export=%u export_heap_first=%u resource_only=%u runtime_export_only=%u present_attempted=0 import_attempted=0\n",
               export_label,
               opts && opts->placed_resource ? "placed" : "committed",
               (void *)rt->resource, (void *)rt->heap,
               (void *)rt->fence, (unsigned)resource_desc.Flags,
               (unsigned)requested_heap_flags, (unsigned long)heap_hr,
               (unsigned)actual_heap_flags,
               (unsigned)actual_heap_props.Type,
               (unsigned)actual_heap_props.CPUPageProperty,
               (unsigned)actual_heap_props.MemoryPoolPreference,
               (unsigned)initial_state,
               app_sync_suppressed ? "suppressed" : "enabled",
               adapter_select, opts ? opts->security_attrs : 0,
               resource_handle_name_label, resource_share_access_name,
               (unsigned long)resource_share_access,
               (unsigned)resource_desc.Dimension,
               (unsigned long)resource_desc.Width, resource_desc.Height,
               (unsigned)resource_desc.Format, (unsigned)resource_desc.Layout,
               clear_value_ptr != NULL, (void *)clear_value_ptr,
               prealloc_info_queried,
               (unsigned long long)alloc_info.SizeInBytes,
               (unsigned long long)alloc_info.Alignment,
               opts ? opts->make_resident_before_export : 0,
               opts ? opts->signal_before_export : 0,
               opts ? opts->export_heap_first : 0,
               opts ? opts->resource_only : 0,
               opts ? opts->runtime_export_only : 0);
    }
    printf("d3d12sharedsmoke: runtime CreateSharedHandle(resource) args object=%p attrs=%p access=%s/0x%lx name=%s/%p\n",
           (void *)rt->resource, (const void *)security_attrs_ptr,
           resource_share_access_name, (unsigned long)resource_share_access,
           resource_handle_name_label, (const void *)resource_handle_name);
    hr = ID3D12Device_CreateSharedHandle(rt->device, (ID3D12DeviceChild *)rt->resource,
                                         security_attrs_ptr,
                                         resource_share_access,
                                         resource_handle_name,
                                         &rt->resource_handle);
    if (FAILED(hr) || !rt->resource_handle) {
        HRESULT reason = ID3D12Device_GetDeviceRemovedReason(rt->device);

        fprintf(stderr,
                "d3d12sharedsmoke: CreateSharedHandle(resource) failed hr=0x%lx device_reason=0x%lx handle=%p shape=%s size=%lux%u creation=%s app_sync=%s touch_requested=%u cleared_before_export=%u initial_common=%u clear_alpha_only=%u prealloc_info=%u adapter_select=%s make_resident=%u signal_before_export=%u name_objects=%u security_attrs=%u share_access=%s/0x%lx heap_flags=0x%x resource_flags=0x%x initial_state=%u alloc=%llu align=%llu\n",
                (unsigned long)hr, (unsigned long)reason,
                rt->resource_handle, d3d12_runtime_shape_name(opts),
                (unsigned long)resource_desc.Width, resource_desc.Height,
                opts && opts->placed_resource ? "placed" : "committed",
                app_sync_suppressed ? "suppressed" : "enabled",
                opts ? opts->touch_before_export : 0, touch_clears_resource,
                opts ? opts->initial_common : 0,
                opts ? opts->clear_alpha_only : 0,
                prealloc_info_queried,
                adapter_select,
                opts ? opts->make_resident_before_export : 0,
                opts ? opts->signal_before_export : 0,
                opts ? opts->name_objects : 0,
                opts ? opts->security_attrs : 0,
                resource_share_access_name,
                (unsigned long)resource_share_access,
                (unsigned)requested_heap_flags,
                (unsigned)resource_desc.Flags, (unsigned)initial_state,
                (unsigned long long)alloc_info.SizeInBytes,
                (unsigned long long)alloc_info.Alignment);
        d3d12_runtime_print_export_result(export_label, "resource",
                                          "resource", 1, hr, reason,
                                          rt->resource_handle,
                                          rt->adapter_luid,
                                          resource_desc.Flags,
                                          requested_heap_flags,
                                          initial_state,
                                          app_sync_suppressed ?
                                              "suppressed" : "enabled",
                                          adapter_select, 0, 0);
        if (opts && opts->wsl_adapter_list) {
            d3d12_runtime_record_device_luid(rt);
            d3d12_runtime_postcheck_dxg_luid("resource-export-failed",
                                             rt->adapter_luid);
        }
        return -1;
    }
    d3d12_runtime_print_export_result(export_label, "resource", "resource",
                                      1, hr, 0, rt->resource_handle,
                                      rt->adapter_luid, resource_desc.Flags,
                                      requested_heap_flags, initial_state,
                                      app_sync_suppressed ?
                                          "suppressed" : "enabled",
                                      adapter_select, 0, 0);
    printf("d3d12sharedsmoke: runtime CreateSharedHandle(resource) ok fd=%d handle=%p access=%s/0x%lx name=%s\n",
           handle_to_fd(rt->resource_handle), rt->resource_handle,
           resource_share_access_name, (unsigned long)resource_share_access,
           resource_handle_name_label);
    d3d12_print_handle_fd_diag(export_label, "resource", rt->resource_handle);
    if (opts && opts->runtime_export_only) {
        printf("d3d12sharedsmoke: runtime export-only stop label=%s handle_kind=resource fd=%d present_attempted=0 import_attempted=0\n",
               export_label, handle_to_fd(rt->resource_handle));
        return 0;
    }
    if (opts && opts->runtime_import_independent_device) {
        if (d3d12_create_device_for_dxg_luid(&rt->adapter_luid,
                                             &rt->import_device,
                                             export_label) != 0)
            return -1;
        printf("d3d12sharedsmoke: runtime independent import device ok device=%p\n",
               (void *)rt->import_device);
    }
    printf("d3d12sharedsmoke: runtime OpenSharedHandle(resource) args api=ID3D12Device_OpenSharedHandle open_device=%s handle=%p fd=%d iid=ID3D12Resource\n",
           rt->import_device ? "independent_device" : "same_device",
           rt->resource_handle, handle_to_fd(rt->resource_handle));
    hr = ID3D12Device_OpenSharedHandle(rt->import_device ? rt->import_device :
                                           rt->device,
                                       rt->resource_handle,
                                       &IID_ID3D12Resource,
                                       (void **)&rt->opened_resource);
    if (FAILED(hr) || !rt->opened_resource) {
        fprintf(stderr,
                "d3d12sharedsmoke: OpenSharedHandle(resource) failed hr=0x%lx\n",
                (unsigned long)hr);
        d3d12_runtime_print_export_result(
            export_label, "resource-import", "resource", 1, hr, 0,
            rt->resource_handle, rt->adapter_luid, resource_desc.Flags,
            requested_heap_flags, initial_state,
            app_sync_suppressed ? "suppressed" : "enabled",
            adapter_select, 0, 1);
        return -1;
    }
    d3d12_runtime_print_export_result(
        export_label, "resource-import", "resource", 1, hr, 0,
        rt->resource_handle, rt->adapter_luid, resource_desc.Flags,
        requested_heap_flags, initial_state,
        app_sync_suppressed ? "suppressed" : "enabled",
        adapter_select, 0, 1);
    opened_desc = ID3D12Resource_GetDesc(rt->opened_resource);
    printf("d3d12sharedsmoke: runtime OpenSharedHandle(resource) ok desc=%lux%u fmt=%u flags=0x%x layout=%u samples=%u\n",
           (unsigned long)opened_desc.Width, opened_desc.Height,
           (unsigned)opened_desc.Format, (unsigned)opened_desc.Flags,
           (unsigned)opened_desc.Layout, opened_desc.SampleDesc.Count);
    if (opened_desc.Dimension != resource_desc.Dimension ||
        opened_desc.Width != resource_desc.Width ||
        opened_desc.Height != resource_desc.Height ||
        opened_desc.DepthOrArraySize != resource_desc.DepthOrArraySize ||
        opened_desc.MipLevels != resource_desc.MipLevels ||
        opened_desc.Format != resource_desc.Format ||
        opened_desc.SampleDesc.Count != resource_desc.SampleDesc.Count) {
        fprintf(stderr,
                "d3d12sharedsmoke: opened resource desc mismatch dim=%u width=%lu height=%u fmt=%u samples=%u\n",
                (unsigned)opened_desc.Dimension,
                (unsigned long)opened_desc.Width,
                opened_desc.Height, (unsigned)opened_desc.Format,
                opened_desc.SampleDesc.Count);
        return -1;
    }

    if (opts && opts->wsl_adapter_list) {
        d3d12_runtime_record_device_luid(rt);
        if (d3d12_runtime_postcheck_dxg_luid("resource-export-ok",
                                             rt->adapter_luid) != 0)
            return -1;
    }

    if (app_sync_suppressed) {
        printf("d3d12sharedsmoke: runtime resource-only ok fd=%d desc=%lux%u fmt=%u flags=0x%x layout=%u app_sync=%s\n",
               handle_to_fd(rt->resource_handle),
               (unsigned long)opened_desc.Width, opened_desc.Height,
               (unsigned)opened_desc.Format, (unsigned)opened_desc.Flags,
               (unsigned)opened_desc.Layout,
               app_sync_suppressed ? "suppressed" : "enabled");
        return 0;
    }

    if (d3d12_runtime_export_open_fence(rt, security_attrs_ptr,
                                        fence_handle_name,
                                        fence_handle_name_label,
                                        opts && opts->export_fence_first ?
                                            "after-resource" : "normal",
                                        export_label,
                                        rt->import_device ? rt->import_device :
                                            rt->device,
                                        rt->import_device ?
                                            "independent_device" :
                                            "same_device",
                                        fence_flags,
                                        opts && opts->runtime_dxg_syncfile_acquire) != 0) {
        if (opts && opts->runtime_import_contract && rt->fence_handle) {
            printf("d3d12sharedsmoke: runtime D3D12 fence OpenSharedHandle failed; continuing import-contract for direct DXG fence import fence_fd=%d\n",
                   handle_to_fd(rt->fence_handle));
            return 0;
        }
        return -1;
    }
    if (opts && opts->runtime_dxg_syncfile_acquire) {
        printf("d3d12sharedsmoke: runtime dxg-syncfile-acquire D3D12 fence export retained but direct import bypassed resource_fd=%d d3d12_fence_fd=%d acquire_sync=dxg-syncfile-acquire d3d12_fence_fd_used=0 present_claim=requires-compositor-completion native_present_claim=0\n",
               handle_to_fd(rt->resource_handle),
               handle_to_fd(rt->fence_handle));
        return 0;
    }
    rt->fence_value++;
    hr = ID3D12CommandQueue_Signal(rt->queue, rt->fence, rt->fence_value);
    if (FAILED(hr)) {
        fprintf(stderr,
                "d3d12sharedsmoke: command queue Signal failed hr=0x%lx\n",
                (unsigned long)hr);
        return -1;
    }
    printf("d3d12sharedsmoke: runtime fence signal queued target=%lu opened_completed_before_wait=%lu\n",
           (unsigned long)rt->fence_value,
           (unsigned long)ID3D12Fence_GetCompletedValue(rt->opened_fence));
    if (wait_for_fence_value(rt->opened_fence, rt->fence_value) != 0) {
        fprintf(stderr,
                "d3d12sharedsmoke: opened fence did not reach value %lu completed=%lu\n",
                (unsigned long)rt->fence_value,
                (unsigned long)ID3D12Fence_GetCompletedValue(rt->opened_fence));
        return -1;
    }

    printf("d3d12sharedsmoke: runtime shared resource fd=%d desc=%lux%u fmt=%u fence_fd=%d completed=%lu\n",
           handle_to_fd(rt->resource_handle),
           (unsigned long)opened_desc.Width, opened_desc.Height,
           opened_desc.Format, handle_to_fd(rt->fence_handle),
           (unsigned long)ID3D12Fence_GetCompletedValue(rt->opened_fence));
    return 0;
}

static int d3d12_runtime_print_import_contract_result(
    const char *label, const struct d3d12_runtime *rt,
    int dxg_query_attempted, int dxg_query_ok,
    const struct d3dkmt_queryresourceinfofromnthandle *query,
    const char *dxg_query_reason, int dxg_errno,
    int direct_dxg_fence_import_attempted,
    int direct_dxg_fence_import_ok,
    const char *direct_dxg_fence_import_reason,
    int direct_dxg_fence_import_errno,
    struct d3dkmthandle imported_resource_device,
    int runtime_device_import_attempted,
    int runtime_device_import_ok,
    const char *runtime_device_import_reason,
    int runtime_device_import_errno,
    struct d3dkmthandle runtime_device_override)
{
    int resource_export_ok = rt && rt->resource_handle != NULL;
    int resource_import_ok = rt && rt->opened_resource != NULL;
    int fence_export_ok = rt && rt->fence_handle != NULL;
    int fence_import_ok = rt && rt->opened_fence != NULL;
    int fence_wait_ok = fence_import_ok &&
        ID3D12Fence_GetCompletedValue(rt->opened_fence) >= rt->fence_value;
    int pass = 0;
    int fail = 0;

    pass += resource_export_ok;
    pass += resource_import_ok;
    pass += fence_export_ok;
    pass += fence_import_ok;
    pass += fence_wait_ok;
    pass += dxg_query_ok;
    pass += direct_dxg_fence_import_ok;
    if (runtime_device_import_attempted)
        pass += runtime_device_import_ok;
    fail += !resource_export_ok;
    fail += !resource_import_ok;
    fail += !fence_export_ok;
    fail += !fence_import_ok;
    fail += !fence_wait_ok;
    fail += !dxg_query_ok;
    fail += !direct_dxg_fence_import_ok;
    if (runtime_device_import_attempted)
        fail += !runtime_device_import_ok;

    printf("d3d12sharedsmoke: runtime import-contract-result label=%s resource_export=%s resource_import=%s fence_export=%s d3d12_fence_import=%s fence_wait=%s dxg_query=%s dxg_query_attempted=%u dxg_query_reason=%s dxg_errno=%d direct_dxg_fence_import=%s direct_dxg_fence_import_attempted=%u direct_dxg_fence_import_reason=%s direct_dxg_fence_import_errno=%d direct_dxg_fence_import_device=0x%x runtime_device_fence_import=%s runtime_device_fence_import_attempted=%u runtime_device_fence_import_reason=%s runtime_device_fence_import_errno=%d runtime_device_override=0x%x pass=%u fail=%u resource_fd=%d fence_fd=%d fence_flags=0x%x fence_shared=%u fence_cross_adapter=%u fence_non_monitored=%u fence_dup_no_cloexec=%u import_device=%s fence_value=%lu completed=%lu allocations=%u total_priv=%u present_attempted=0\n",
           label ? label : "runtime_import_contract",
           resource_export_ok ? "PASS" : "FAIL",
           resource_import_ok ? "PASS" : "FAIL",
           fence_export_ok ? "PASS" : "FAIL",
           fence_import_ok ? "PASS" : "FAIL",
           fence_wait_ok ? "PASS" : "FAIL",
           dxg_query_ok ? "PASS" : "FAIL",
           dxg_query_attempted,
           dxg_query_reason ? dxg_query_reason : "unknown", dxg_errno,
           direct_dxg_fence_import_ok ? "PASS" : "FAIL",
           direct_dxg_fence_import_attempted,
           direct_dxg_fence_import_reason ?
               direct_dxg_fence_import_reason : "unknown",
           direct_dxg_fence_import_errno,
           imported_resource_device.v,
           runtime_device_import_ok ? "PASS" : "FAIL",
           runtime_device_import_attempted,
           runtime_device_import_reason ?
               runtime_device_import_reason : "not_configured",
           runtime_device_import_errno,
           runtime_device_override.v,
           pass, fail,
           resource_export_ok ? handle_to_fd(rt->resource_handle) : -1,
           fence_export_ok ? handle_to_fd(rt->fence_handle) : -1,
           rt ? (unsigned)rt->diag_fence_flags : 0,
           rt ? ((rt->diag_fence_flags & D3D12_FENCE_FLAG_SHARED) != 0) : 0,
           rt ? ((rt->diag_fence_flags &
                  D3D12_FENCE_FLAG_SHARED_CROSS_ADAPTER) != 0) : 0,
           rt ? ((rt->diag_fence_flags &
                  D3D12_FENCE_FLAG_NON_MONITORED) != 0) : 0,
           rt ? rt->diag_fence_dup_no_cloexec : 0,
           rt && rt->import_device ? "independent_device" : "same_device",
           rt ? (unsigned long)rt->fence_value : 0,
           fence_import_ok ?
               (unsigned long)ID3D12Fence_GetCompletedValue(rt->opened_fence) :
               0,
           query ? query->allocation_count : 0,
           query ? query->total_priv_drv_data_size : 0);
    return fail == 0 ? 0 : -1;
}

int main(int argc, char **argv)
{
    struct app app;
    struct d3dkmthandle adapter;
    struct d3dkmthandle device;
    struct d3dkmthandle resource;
    struct d3dkmthandle sync_object;
    struct winluid adapter_luid;
    struct winluid presented_luid;
    struct d3dkmt_queryresourceinfofromnthandle query;
    struct d3d12_runtime runtime;
    int dxg_fd = -1;
    int shared_fd = -1;
    int fence_fd = -1;
    int buffer_fd = -1;
    int buffer_fence_fd = -1;
    int fence_fd_owned = 0;
    int expect_luid_reject = getenv("D3D12SHAREDSMOKE_BAD_LUID") != NULL;
    int use_runtime = getenv("D3D12SHAREDSMOKE_RUNTIME") != NULL;
    int require_present = 0;
    int allow_failclosed_present = 0;
    int ret = 1;
    int dxg_only = 0;
    int device_admission_matrix = 0;
    int runtime_size_explicit = 0;
    int runtime_touch_explicit = 0;
    int runtime_app_sync_explicit = 0;
    int expect_bad_dimensions_reject = 0;
    int expect_bad_format_reject = 0;
    int expect_missing_fence_reject = 0;
    int expect_stale_fence_reject = 0;
    uint32_t buffer_width = 256;
    uint32_t buffer_height = 256;
    uint32_t buffer_format = WL_SHM_FORMAT_ARGB8888;
    uint64_t submitted_fence_value = 1;
    uint64_t present_before = 0;
    uint64_t present_after = 0;
    int64_t present_commit_ms = 0;
    int present_rejected = 0;
    struct present_evidence present_evidence;
    struct d3d12_runtime_options runtime_opts;
    const char *wayland_commit_state = "not_attempted";
    const char *wayland_commit_reason = "startup";
    const char *present_evidence_state = "not_checked";
    const char *acquire_fd_kind = NULL;
    int auto_runtime_dxg_syncfile_acquire = 0;
    int present_evidence_selftest = 0;
    int runtime_negative_submission_bundle = 0;
    const char *present_evidence_validate_path = NULL;

    memset(&runtime_opts, 0, sizeof(runtime_opts));
    runtime_opts.simultaneous = 1;
    runtime_opts.width = 256;
    runtime_opts.height = 256;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--require-present") == 0) {
            require_present = 1;
        } else if (strcmp(argv[i], "--allow-failclosed-present") == 0) {
            require_present = 1;
            allow_failclosed_present = 1;
        } else if (strcmp(argv[i], "--present-evidence-selftest") == 0) {
            present_evidence_selftest = 1;
        } else if (strcmp(argv[i], "--runtime-negative-submissions") == 0 ||
                   strcmp(argv[i], "--runtime-negative-bundle") == 0) {
            runtime_negative_submission_bundle = 1;
        } else if (strncmp(argv[i], "--present-evidence-validate=",
                           sizeof("--present-evidence-validate=") - 1) == 0) {
            present_evidence_validate_path =
                argv[i] + sizeof("--present-evidence-validate=") - 1;
        } else if (strcmp(argv[i], "--present-evidence-validate") == 0 &&
                   i + 1 < argc) {
            present_evidence_validate_path = argv[++i];
        } else if (strcmp(argv[i], "--device-admission-matrix") == 0) {
            device_admission_matrix = 1;
        } else if (strncmp(argv[i], "--device-admission-wsl-type0-size=",
                           sizeof("--device-admission-wsl-type0-size=") - 1) == 0 ||
                   strncmp(argv[i], "--wsl-qai-type0-size=",
                           sizeof("--wsl-qai-type0-size=") - 1) == 0) {
            const char *value_arg = strchr(argv[i], '=');

            device_admission_matrix = 1;
            if (value_arg)
                value_arg++;
            if (!value_arg ||
                parse_runtime_u32(value_arg,
                                  &g_device_admission_wsl_type0_size) != 0 ||
                g_device_admission_wsl_type0_size == 0) {
                fprintf(stderr,
                        "d3d12sharedsmoke: invalid WSL QAI type0 size '%s'\n",
                        value_arg ? value_arg : "");
                return 2;
            }
            g_device_admission_wsl_type0_size_set = 1;
        } else if (strcmp(argv[i], "--runtime") == 0) {
            use_runtime = 1;
        } else if (strcmp(argv[i], "--runtime-wsl-parity") == 0) {
            use_runtime = 1;
            apply_runtime_wsl_parity(&runtime_opts, runtime_size_explicit,
                                     runtime_touch_explicit,
                                     runtime_app_sync_explicit);
        } else if (strncmp(argv[i], "--case=", sizeof("--case=") - 1) == 0) {
            const char *case_name = argv[i] + sizeof("--case=") - 1;

            if (strcmp(case_name, "dxg") == 0 || strcmp(case_name, "raw") == 0) {
                use_runtime = 0;
                dxg_only = 1;
            } else if (apply_runtime_case(&runtime_opts, case_name,
                                          runtime_size_explicit,
                                          runtime_touch_explicit,
                                          runtime_app_sync_explicit) == 0) {
                use_runtime = 1;
            } else {
                fprintf(stderr,
                        "d3d12sharedsmoke: invalid runtime case '%s'\n",
                        case_name);
                return 2;
            }
        } else if (strcmp(argv[i], "--runtime-render-target") == 0) {
            use_runtime = 1;
            runtime_opts.render_target = 1;
            runtime_opts.simultaneous = 0;
            if (!runtime_touch_explicit)
                runtime_opts.touch_before_export = 1;
            if (!runtime_size_explicit) {
                runtime_opts.width = 640;
                runtime_opts.height = 480;
            }
        } else if (strncmp(argv[i], "--runtime-size=",
                           sizeof("--runtime-size=") - 1) == 0) {
            use_runtime = 1;
            if (parse_runtime_size(argv[i] + sizeof("--runtime-size=") - 1,
                                   &runtime_opts.width,
                                   &runtime_opts.height) != 0) {
                fprintf(stderr,
                        "d3d12sharedsmoke: invalid runtime size '%s'\n",
                        argv[i] + sizeof("--runtime-size=") - 1);
                return 2;
            }
            runtime_size_explicit = 1;
        } else if (strcmp(argv[i], "--runtime-640x480") == 0) {
            use_runtime = 1;
            runtime_opts.width = 640;
            runtime_opts.height = 480;
            runtime_size_explicit = 1;
        } else if (strcmp(argv[i], "--runtime-no-simultaneous") == 0) {
            use_runtime = 1;
            runtime_opts.simultaneous = 0;
        } else if (strcmp(argv[i], "--runtime-touch-before-export") == 0) {
            use_runtime = 1;
            runtime_opts.touch_before_export = 1;
            runtime_touch_explicit = 1;
        } else if (strcmp(argv[i], "--runtime-no-clear") == 0) {
            use_runtime = 1;
            runtime_opts.touch_before_export = 0;
            runtime_touch_explicit = 1;
        } else if (strcmp(argv[i], "--runtime-initial-common") == 0) {
            use_runtime = 1;
            runtime_opts.initial_common = 1;
        } else if (strcmp(argv[i], "--runtime-clear-alpha-only") == 0) {
            use_runtime = 1;
            runtime_opts.clear_alpha_only = 1;
        } else if (strcmp(argv[i], "--runtime-custom-heap-props") == 0) {
            use_runtime = 1;
            runtime_opts.custom_heap_props = 1;
        } else if (strcmp(argv[i], "--runtime-skip-prealloc-info") == 0) {
            use_runtime = 1;
            runtime_opts.skip_prealloc_info = 1;
        } else if (strcmp(argv[i], "--runtime-wsl-adapter-list") == 0) {
            use_runtime = 1;
            runtime_opts.wsl_adapter_list = 1;
        } else if (strcmp(argv[i], "--runtime-wsl-resource-shape") == 0) {
            use_runtime = 1;
            apply_runtime_wsl_resource_shape(&runtime_opts,
                                             runtime_size_explicit,
                                             runtime_touch_explicit,
                                             runtime_app_sync_explicit);
        } else if (strcmp(argv[i], "--runtime-wsl-resource-shape-direct") == 0) {
            use_runtime = 1;
            apply_runtime_wsl_resource_shape_direct(
                &runtime_opts, runtime_size_explicit,
                runtime_touch_explicit, runtime_app_sync_explicit);
        } else if (strcmp(argv[i], "--runtime-wsl-resource-shape-shared-heap") == 0) {
            use_runtime = 1;
            apply_runtime_wsl_resource_shape_shared_heap(
                &runtime_opts, runtime_size_explicit,
                runtime_touch_explicit, runtime_app_sync_explicit);
        } else if (strcmp(argv[i], "--runtime-wsl-success-shape-direct") == 0) {
            use_runtime = 1;
            apply_runtime_wsl_success_shape_direct_export_only(
                &runtime_opts, runtime_size_explicit,
                runtime_touch_explicit, runtime_app_sync_explicit);
        } else if (strcmp(argv[i], "--runtime-wsl-success-shape-import-contract") == 0) {
            use_runtime = 1;
            apply_runtime_wsl_success_shape_import_contract(
                &runtime_opts, runtime_size_explicit,
                runtime_touch_explicit);
        } else if (strcmp(argv[i], "--runtime-wsl-success-shape-import-contract-independent-device") == 0) {
            use_runtime = 1;
            apply_runtime_wsl_success_shape_import_contract_independent(
                &runtime_opts, runtime_size_explicit,
                runtime_touch_explicit);
        } else if (strcmp(argv[i], "--runtime-wsl-success-shape-import-contract-fence-first") == 0) {
            use_runtime = 1;
            apply_runtime_wsl_success_shape_import_contract_fence_first(
                &runtime_opts, runtime_size_explicit,
                runtime_touch_explicit);
        } else if (strcmp(argv[i], "--runtime-wsl-success-shape-import-contract-fence-dup") == 0 ||
                   strcmp(argv[i], "--runtime-wsl-success-shape-import-contract-fence-dup-no-cloexec") == 0) {
            use_runtime = 1;
            apply_runtime_wsl_success_shape_import_contract_fence_dup(
                &runtime_opts, runtime_size_explicit,
                runtime_touch_explicit);
        } else if (strcmp(argv[i], "--runtime-wsl-success-shape-import-contract-fence-cross-adapter") == 0) {
            use_runtime = 1;
            apply_runtime_wsl_success_shape_import_contract_fence_cross_adapter(
                &runtime_opts, runtime_size_explicit,
                runtime_touch_explicit);
        } else if (strcmp(argv[i], "--runtime-wsl-success-shape-wsl-list-default") == 0) {
            use_runtime = 1;
            apply_runtime_wsl_success_shape_wsl_list_default_export_only(
                &runtime_opts, runtime_size_explicit,
                runtime_touch_explicit, runtime_app_sync_explicit);
        } else if (strcmp(argv[i], "--runtime-wsl-success-shape-no-clear-value") == 0) {
            use_runtime = 1;
            apply_runtime_wsl_success_shape_no_clear_value_export_only(
                &runtime_opts, runtime_size_explicit,
                runtime_touch_explicit, runtime_app_sync_explicit);
        } else if (strcmp(argv[i], "--runtime-wsl-success-shape-initial-rt") == 0) {
            use_runtime = 1;
            apply_runtime_wsl_success_shape_initial_rt_export_only(
                &runtime_opts, runtime_size_explicit,
                runtime_touch_explicit, runtime_app_sync_explicit);
        } else if (strcmp(argv[i], "--runtime-wsl-success-shape-reserve-low-va") == 0) {
            use_runtime = 1;
            apply_runtime_wsl_success_shape_reserve_low_va_export_only(
                &runtime_opts, runtime_size_explicit,
                runtime_touch_explicit, runtime_app_sync_explicit);
        } else if (strcmp(argv[i], "--runtime-wsl-success-shape-resource-cross-adapter") == 0) {
            use_runtime = 1;
            apply_runtime_wsl_success_shape_resource_cross_adapter_export_only(
                &runtime_opts, runtime_size_explicit,
                runtime_touch_explicit, runtime_app_sync_explicit);
        } else if (strcmp(argv[i], "--runtime-wsl-success-shape-prealloc-info") == 0) {
            use_runtime = 1;
            apply_runtime_wsl_success_shape_prealloc_info_export_only(
                &runtime_opts, runtime_size_explicit,
                runtime_touch_explicit, runtime_app_sync_explicit);
        } else if (strcmp(argv[i], "--runtime-wsl-success-shape-no-heap-flags") == 0) {
            use_runtime = 1;
            apply_runtime_wsl_success_shape_no_heap_flags_export_only(
                &runtime_opts, runtime_size_explicit,
                runtime_touch_explicit, runtime_app_sync_explicit);
        } else if (strcmp(argv[i], "--runtime-wsl-success-shape-placed-shared-heap") == 0) {
            use_runtime = 1;
            apply_runtime_wsl_success_shape_placed_shared_heap_export_only(
                &runtime_opts, runtime_size_explicit,
                runtime_touch_explicit, runtime_app_sync_explicit);
        } else if (strcmp(argv[i], "--runtime-wsl-resource-shape-app-sync-export-only") == 0) {
            use_runtime = 1;
            apply_runtime_wsl_resource_shape_app_sync_export_only(
                &runtime_opts, runtime_size_explicit,
                runtime_touch_explicit, runtime_app_sync_explicit);
        } else if (strcmp(argv[i], "--runtime-precheck-dxg-luid") == 0) {
            use_runtime = 1;
            runtime_opts.precheck_dxg_luid = 1;
        } else if (strcmp(argv[i], "--runtime-preexport-diagnostics") == 0) {
            use_runtime = 1;
            runtime_opts.preexport_diagnostics = 1;
        } else if (strcmp(argv[i], "--runtime-reserve-low-va") == 0) {
            use_runtime = 1;
            runtime_opts.reserve_low_va = 1;
        } else if (strcmp(argv[i], "--runtime-app-sync") == 0) {
            use_runtime = 1;
            runtime_opts.suppress_app_sync = 0;
            runtime_app_sync_explicit = 1;
        } else if (strcmp(argv[i], "--runtime-make-resident-before-export") == 0) {
            use_runtime = 1;
            runtime_opts.make_resident_before_export = 1;
        } else if (strcmp(argv[i], "--runtime-signal-before-export") == 0) {
            use_runtime = 1;
            runtime_opts.signal_before_export = 1;
        } else if (strcmp(argv[i], "--runtime-export-fence-first") == 0) {
            use_runtime = 1;
            runtime_opts.export_fence_first = 1;
        } else if (strcmp(argv[i], "--runtime-dxg-syncfile-acquire") == 0) {
            use_runtime = 1;
            runtime_opts.runtime_dxg_syncfile_acquire = 1;
        } else if (strcmp(argv[i], "--runtime-fence-only") == 0) {
            use_runtime = 1;
            runtime_opts.fence_only = 1;
        } else if (strcmp(argv[i], "--runtime-fence-cross-adapter") == 0) {
            use_runtime = 1;
            runtime_opts.fence_cross_adapter = 1;
        } else if (strcmp(argv[i], "--runtime-resource-cross-adapter") == 0) {
            use_runtime = 1;
            runtime_opts.resource_cross_adapter = 1;
        } else if (strcmp(argv[i], "--runtime-export-heap-first") == 0) {
            use_runtime = 1;
            runtime_opts.export_heap_first = 1;
        } else if (strcmp(argv[i], "--runtime-heap-only") == 0) {
            use_runtime = 1;
            runtime_opts.heap_only = 1;
            runtime_opts.placed_resource = 1;
        } else if (strcmp(argv[i], "--runtime-resource-only") == 0) {
            use_runtime = 1;
            runtime_opts.resource_only = 1;
        } else if (strcmp(argv[i], "--runtime-named-handles") == 0) {
            use_runtime = 1;
            runtime_opts.named_resource_handle = 1;
            runtime_opts.named_fence_handle = 1;
        } else if (strcmp(argv[i], "--runtime-named-resource-handle") == 0) {
            use_runtime = 1;
            runtime_opts.named_resource_handle = 1;
        } else if (strcmp(argv[i], "--runtime-named-fence-handle") == 0) {
            use_runtime = 1;
            runtime_opts.named_fence_handle = 1;
        } else if (strcmp(argv[i], "--runtime-name-objects") == 0) {
            use_runtime = 1;
            runtime_opts.name_objects = 1;
        } else if (strcmp(argv[i], "--runtime-security-attrs") == 0) {
            use_runtime = 1;
            runtime_opts.security_attrs = 1;
        } else if (strncmp(argv[i], "--runtime-share-access=",
                           sizeof("--runtime-share-access=") - 1) == 0) {
            const char *access_arg =
                argv[i] + sizeof("--runtime-share-access=") - 1;

            use_runtime = 1;
            if (parse_runtime_share_access(access_arg,
                                           &runtime_opts.share_access,
                                           &runtime_opts.share_access_name) !=
                0) {
                fprintf(stderr,
                        "d3d12sharedsmoke: invalid runtime share access '%s'\n",
                        access_arg);
                return 2;
            }
            runtime_opts.share_access_override = 1;
        } else if (strcmp(argv[i], "--runtime-placed-resource") == 0) {
            use_runtime = 1;
            runtime_opts.placed_resource = 1;
        } else if (strncmp(argv[i], "--dxgdev",
                           sizeof("--dxgdev") - 1) == 0 ||
                   strncmp(argv[i], "--runtime-direct-dxg-fence-device",
                           sizeof("--runtime-direct-dxg-fence-device") - 1) == 0 ||
                   strncmp(argv[i], "--runtime-dxg-fence-runtime-device",
                           sizeof("--runtime-dxg-fence-runtime-device") - 1) == 0) {
            const char *value_arg = strchr(argv[i], '=');

            use_runtime = 1;
            if (value_arg) {
                value_arg++;
            } else if (i + 1 < argc) {
                value_arg = argv[++i];
            }
            if (!value_arg ||
                parse_runtime_u32(value_arg,
                                  &runtime_opts.direct_dxg_fence_runtime_device) != 0) {
                fprintf(stderr,
                        "d3d12sharedsmoke: invalid runtime direct dxg fence device '%s'\n",
                        value_arg ? value_arg : "");
                return 2;
            }
            runtime_opts.direct_dxg_fence_runtime_device_override = 1;
        } else if (strncmp(argv[i], "--dxgflags",
                           sizeof("--dxgflags") - 1) == 0 ||
                   strncmp(argv[i], "--runtime-direct-dxg-fence-flags",
                           sizeof("--runtime-direct-dxg-fence-flags") - 1) == 0) {
            const char *value_arg = strchr(argv[i], '=');

            use_runtime = 1;
            if (value_arg) {
                value_arg++;
            } else if (i + 1 < argc) {
                value_arg = argv[++i];
            }
            if (!value_arg ||
                parse_runtime_u32(value_arg,
                                  &runtime_opts.direct_dxg_fence_flags) != 0) {
                fprintf(stderr,
                        "d3d12sharedsmoke: invalid runtime direct dxg fence flags '%s'\n",
                        value_arg ? value_arg : "");
                return 2;
            }
            runtime_opts.direct_dxg_fence_flags_override = 1;
        } else if (strcmp(argv[i], "--bad-luid") == 0) {
            expect_luid_reject = 1;
        } else if (strcmp(argv[i], "--bad-dimensions") == 0 ||
                   strcmp(argv[i], "--wrong-dimensions") == 0) {
            expect_bad_dimensions_reject = 1;
        } else if (strcmp(argv[i], "--bad-format") == 0 ||
                   strcmp(argv[i], "--wrong-format") == 0) {
            expect_bad_format_reject = 1;
        } else if (strcmp(argv[i], "--missing-fence") == 0 ||
                   strcmp(argv[i], "--missing-fence-fd") == 0) {
            expect_missing_fence_reject = 1;
        } else if (strcmp(argv[i], "--stale-fence") == 0 ||
                   strcmp(argv[i], "--stale-fence-target") == 0) {
            expect_stale_fence_reject = 1;
        } else {
            fprintf(stderr,
                    "usage: %s [--case=...] [--runtime] [--device-admission-matrix] [--present-evidence-selftest] [--present-evidence-validate=PATH] [--runtime-negative-submissions] [--require-present] [--allow-failclosed-present] [--bad-luid] [--bad-dimensions] [--bad-format] [--missing-fence] [--stale-fence]\n",
                    argv[0]);
            return 2;
        }
    }

    if (runtime_negative_submission_bundle)
        return run_runtime_negative_submission_bundle(argv[0]);
    if (present_evidence_selftest)
        return run_present_evidence_selftest();
    if (present_evidence_validate_path)
        return run_present_evidence_validate(present_evidence_validate_path);
    if (device_admission_matrix)
        return run_d3d12_device_admission_matrix();
    if (use_runtime && require_present &&
        !runtime_opts.runtime_dxg_syncfile_acquire) {
        runtime_opts.runtime_dxg_syncfile_acquire = 1;
        auto_runtime_dxg_syncfile_acquire = 1;
        printf("d3d12sharedsmoke: runtime-present acquire path auto-selected acquire_sync=dxg-syncfile-acquire reason=d3d12-fence-open-wsl-reproducible-failure d3d12_fence_open_required=0 fail_closed=1\n");
        printf("d3d12sharedsmoke: d3d12_fence_sharing_policy_matrix "
               "decision=dxg_syncfile_acquire direct_d3d12_open_required=0 "
               "direct_d3d12_open_expected_fail=1 "
               "dxg_syncfile_acquire_required=1 "
               "same_adapter_wsl_trace=/tmp/xv6-wsl-probe/mesaglfeature-nvidia-live.trace "
               "same_adapter_trace_required=1 "
               "native_present_claim=0 opengl_submit_credit=0 status=PASS\n");
    }

    signal(SIGALRM, timeout_handler);
    alarm(use_runtime && require_present ? 60 : 10);
    memset(&app, 0, sizeof(app));
    memset(&adapter, 0, sizeof(adapter));
    memset(&device, 0, sizeof(device));
    memset(&resource, 0, sizeof(resource));
    memset(&sync_object, 0, sizeof(sync_object));
    memset(&adapter_luid, 0, sizeof(adapter_luid));
    memset(&presented_luid, 0, sizeof(presented_luid));
    memset(&runtime, 0, sizeof(runtime));
    memset(&present_evidence, 0, sizeof(present_evidence));

    if (use_runtime) {
        const char *runtime_label = d3d12_runtime_export_label(&runtime_opts);

        if (runtime_opts.reserve_low_va)
            d3d12_runtime_reserve_low_va();
        if (d3d12_runtime_create(&runtime, &runtime_opts) != 0) {
            wayland_commit_reason = "runtime_create_failed";
            if (runtime_opts.runtime_import_contract)
                d3d12_runtime_print_import_contract_result(
                    runtime_label, &runtime, 0, 0, NULL,
                    "runtime_create_failed", 0, 0, 0,
                    "runtime_create_failed", 0, device, 0, 0,
                    "runtime_create_failed", 0, device);
            goto out;
        }
        if (runtime_opts.runtime_import_contract) {
            int dxg_query_ok = 0;
            int dxg_query_attempted = 0;
            int dxg_query_errno = 0;
            int direct_dxg_fence_import_ok = 0;
            int direct_dxg_fence_import_attempted = 0;
            int direct_dxg_fence_import_errno = 0;
            int runtime_device_import_ok = 0;
            int runtime_device_import_attempted = 0;
            int runtime_device_import_errno = 0;
            struct d3dkmthandle runtime_device_override;
            uint32_t direct_dxg_fence_flags = 0x13;
            const char *dxg_query_reason = "ok";
            const char *direct_dxg_fence_import_reason = "ok";
            const char *runtime_device_import_reason = "not_configured";

            memset(&runtime_device_override, 0, sizeof(runtime_device_override));
            adapter_luid = runtime.adapter_luid;
            shared_fd = handle_to_fd(runtime.resource_handle);
            fence_fd = handle_to_fd(runtime.fence_handle);
            if (runtime_opts.direct_dxg_fence_runtime_device_override)
                runtime_device_override.v =
                    runtime_opts.direct_dxg_fence_runtime_device;
            if (runtime_opts.direct_dxg_fence_flags_override)
                direct_dxg_fence_flags =
                    runtime_opts.direct_dxg_fence_flags;
            if (open_dxg_device_for_luid(&dxg_fd, &adapter, &device,
                                         NULL, &adapter_luid) != 0) {
                dxg_query_reason = "open_dxg_device_for_luid_failed";
                dxg_query_errno = errno;
                direct_dxg_fence_import_reason =
                    "open_dxg_device_for_luid_failed";
                direct_dxg_fence_import_errno = errno;
                runtime_device_import_reason =
                    "open_dxg_device_for_luid_failed";
                runtime_device_import_errno = errno;
            } else {
                printf("d3d12sharedsmoke: runtime direct-dxg-fence-import-plan resource_fd=%d fence_fd=%d imported_resource_device=0x%x runtime_device_override=0x%x runtime_device_override_present=%u dxg_fd=%d adapter=0x%x flags=0x%x shared=%u nt_security=%u no_signal=%u\n",
                       shared_fd, fence_fd, device.v,
                       runtime_device_override.v,
                       runtime_opts.direct_dxg_fence_runtime_device_override,
                       dxg_fd, adapter.v, direct_dxg_fence_flags,
                       direct_dxg_fence_flags & 1,
                       (direct_dxg_fence_flags >> 1) & 1,
                       (direct_dxg_fence_flags >> 4) & 1);
                if (shared_fd < 0) {
                    dxg_query_reason = "resource_fd_invalid";
                } else {
                    dxg_query_attempted = 1;
                    if (query_shared_resource_info(dxg_fd, device,
                                                   shared_fd, &query) == 0) {
                        dxg_query_ok = 1;
                    } else {
                        dxg_query_reason =
                            "query_shared_resource_info_failed";
                        dxg_query_errno = errno;
                    }
                }
                if (fence_fd < 0) {
                    direct_dxg_fence_import_reason = "fence_fd_invalid";
                } else {
                    struct d3dkmthandle direct_sync;

                    direct_dxg_fence_import_attempted = 1;
                    memset(&direct_sync, 0, sizeof(direct_sync));
                    if (validate_direct_open_shared_fence(
                            dxg_fd, device, fence_fd,
                            direct_dxg_fence_flags,
                            "imported_resource_device",
                            &direct_sync) == 0) {
                        direct_dxg_fence_import_ok = 1;
                    } else {
                        direct_dxg_fence_import_reason =
                            "direct_dxg_fence_import_failed";
                        direct_dxg_fence_import_errno = errno;
                    }
                    if (runtime_opts.direct_dxg_fence_runtime_device_override) {
                        struct d3dkmthandle runtime_sync;

                        runtime_device_import_attempted = 1;
                        memset(&runtime_sync, 0, sizeof(runtime_sync));
                        if (validate_direct_open_shared_fence(
                                dxg_fd, runtime_device_override, fence_fd,
                                direct_dxg_fence_flags,
                                "runtime_device_override",
                                &runtime_sync) == 0) {
                            runtime_device_import_ok = 1;
                            runtime_device_import_reason = "ok";
                        } else {
                            runtime_device_import_reason =
                                "runtime_device_fence_import_failed";
                            runtime_device_import_errno = errno;
                        }
                    }
                }
            }
            if (d3d12_runtime_print_import_contract_result(
                    runtime_label, &runtime, dxg_query_attempted,
                    dxg_query_ok, dxg_query_ok ? &query : NULL,
                    dxg_query_reason, dxg_query_errno,
                    direct_dxg_fence_import_attempted,
                    direct_dxg_fence_import_ok,
                    direct_dxg_fence_import_reason,
                    direct_dxg_fence_import_errno, device,
                    runtime_device_import_attempted,
                    runtime_device_import_ok,
                    runtime_device_import_reason,
                    runtime_device_import_errno,
                    runtime_device_override) == 0)
                ret = 0;
            goto out;
        }
        if (runtime_opts.fence_only) {
            printf("d3d12sharedsmoke: runtime fence-only validation ok\n");
            ret = 0;
            goto out;
        }
        if (runtime_opts.heap_only) {
            printf("d3d12sharedsmoke: runtime heap-only validation ok\n");
            ret = 0;
            goto out;
        }
        if (runtime_opts.runtime_export_only) {
            printf("d3d12sharedsmoke: runtime export-only validation ok label=%s app_sync=%s resource_fd=%d fence_fd=%d\n",
                   runtime_label,
                   runtime.diag_app_sync_suppressed ? "suppressed" :
                       "enabled",
                   handle_to_fd(runtime.resource_handle),
                   handle_to_fd(runtime.fence_handle));
            if (require_present) {
                wayland_commit_reason =
                    "runtime_export_only_cannot_present";
                fprintf(stderr,
                        "d3d12sharedsmoke: require-present requested but runtime export-only mode cannot present\n");
                goto out;
            }
            ret = 0;
            goto out;
        }
        if (runtime_opts.resource_only || runtime_opts.suppress_app_sync) {
            printf("d3d12sharedsmoke: runtime resource-only validation ok app_sync=%s\n",
                   runtime_opts.suppress_app_sync ? "suppressed" : "enabled");
            if (require_present) {
                wayland_commit_reason =
                    "runtime_app_sync_suppressed_cannot_present";
                fprintf(stderr,
                        "d3d12sharedsmoke: require-present requested but runtime app sync is suppressed; export-only shape cannot present\n");
                goto out;
            }
            ret = 0;
            goto out;
        }
        if (require_present) {
            d3d12_runtime_print_phase_result(
                runtime_label, "present-requested", "resource",
                runtime.device != NULL, runtime.resource_handle != NULL,
                0, 0, runtime.resource_handle, runtime.adapter_luid,
                "present_requested");
            d3d12_runtime_print_export_result(
                runtime_label, "present-requested", "resource",
                runtime.resource_handle != NULL, 0, 0,
                runtime.resource_handle, runtime.adapter_luid,
                runtime.diag_resource_flags, runtime.diag_heap_flags,
                runtime.diag_initial_state,
                runtime.diag_app_sync_suppressed ? "suppressed" : "enabled",
                runtime.diag_adapter_path, 1,
                runtime.opened_resource != NULL);
        }
        adapter_luid = runtime.adapter_luid;
        if (open_dxg_device_for_luid(&dxg_fd, &adapter, &device,
                                     NULL, &adapter_luid) < 0) {
            char luid_text[32];

            wayland_commit_reason = "open_dxg_device_for_runtime_luid_failed";
            format_winluid_text(luid_text, sizeof(luid_text), adapter_luid);
            fprintf(stderr,
                    "d3d12sharedsmoke: dxg device unavailable for runtime luid=%s low=0x%08x high=0x%08x\n",
                    luid_text, adapter_luid.a, adapter_luid.b);
            goto out;
        }
        shared_fd = handle_to_fd(runtime.resource_handle);
        fence_fd = handle_to_fd(runtime.fence_handle);
        buffer_width = runtime_opts.width;
        buffer_height = runtime_opts.height;
        if (query_shared_resource_info(dxg_fd, device, shared_fd, &query) != 0) {
            wayland_commit_reason = "runtime_query_shared_resource_failed";
            goto out;
        }
        printf("d3d12sharedsmoke: runtime query adapter=0x%x allocations=%u total_priv=%u\n",
               adapter.v, query.allocation_count,
               query.total_priv_drv_data_size);
        printf("d3d12sharedsmoke: runtime-present contract resource_export=%s resource_open=%s fence_export=%s d3d12_fence_open=%s d3d12_fence_open_expected_fail=%u acquire_sync=%s auto_acquire=%u resource_fd=%d d3d12_fence_fd=%d wayland_commit_attempted=0\n",
               runtime.resource_handle ? "PASS" : "FAIL",
               runtime.opened_resource ? "PASS" : "FAIL",
               runtime.fence_handle ? "PASS" : "FAIL",
               runtime_opts.runtime_dxg_syncfile_acquire ?
                   "BYPASSED_PURE_DXG_ACQUIRE" :
                   (runtime.opened_fence ? "PASS" : "FAIL"),
               runtime_opts.runtime_dxg_syncfile_acquire ? 1 : 0,
               runtime_opts.runtime_dxg_syncfile_acquire ?
                   "dxg-syncfile-acquire" : "legacy-d3d12-fence-fd",
               auto_runtime_dxg_syncfile_acquire,
               shared_fd, fence_fd);
        if (runtime_opts.runtime_dxg_syncfile_acquire) {
            struct d3dkmthandle acquire_import;
            int acquire_errno;

            memset(&acquire_import, 0, sizeof(acquire_import));
            fence_fd = -1;
            if (create_dxg_syncfile_acquire(dxg_fd, device, &fence_fd,
                                            &sync_object) < 0) {
                acquire_errno = errno ? errno : EIO;
                wayland_commit_reason = "dxg_syncfile_acquire_export_failed";
                fprintf(stderr,
                        "d3d12sharedsmoke: dxg-syncfile-acquire export failed fail_closed=1 acquire_sync=dxg-syncfile-acquire errno=%d (%s) present_attempted=0 native_present_claim=0\n",
                        acquire_errno, strerror(acquire_errno));
                goto out;
            }
            fence_fd_owned = 1;
            printf("d3d12sharedsmoke: dxg-syncfile-acquire export ok acquire_sync=dxg-syncfile-acquire acquire_fd_kind=dxg-shared-sync-fd resource_fd=%d acquire_fd=%d acquire_sync=0x%x acquire_target=1 d3d12_fence_fd_used=0 present_claim=requires-compositor-completion native_present_claim=0\n",
                   shared_fd, fence_fd, sync_object.v);
            if (validate_direct_open_dxg_syncfile(
                    dxg_fd, device, fence_fd,
                    "dxg_syncfile_acquire_validate",
                    &acquire_import) != 0) {
                acquire_errno = errno ? errno : EIO;
                wayland_commit_reason =
                    "dxg_syncfile_acquire_import_validation_failed";
                fprintf(stderr,
                        "d3d12sharedsmoke: dxg-syncfile-acquire import validation failed fail_closed=1 acquire_sync=dxg-syncfile-acquire fd=%d errno=%d (%s) present_attempted=0 native_present_claim=0\n",
                        fence_fd, acquire_errno, strerror(acquire_errno));
                goto out;
            }
            printf("d3d12sharedsmoke: dxg-syncfile-acquire import validation ok acquire_sync=dxg-syncfile-acquire acquire_fd_kind=dxg-shared-sync-fd fd=%d target=1 kernel_dxg_sync_import=PASS d3d12_fence_fd_used=0\n",
                   fence_fd);
            printf("d3d12sharedsmoke: d3d12_fence_sharing_validation_matrix "
                   "decision=dxg_syncfile_acquire direct_d3d12_fence_fd_used=0 "
                   "dxg_syncfile_export=PASS dxg_syncfile_import=PASS "
                   "acquire_target=1 same_adapter_luid=PASS "
                   "same_adapter_wsl_trace=/tmp/xv6-wsl-probe/mesaglfeature-nvidia-live.trace "
                   "present_claim=requires-compositor-completion "
                   "native_present_claim=0 opengl_submit_credit=0 status=PASS\n");
        }
    } else {
        if (open_first_dxg_device(&dxg_fd, &adapter, &device,
                                  &adapter_luid) < 0) {
            fprintf(stderr, "d3d12sharedsmoke: dxg device unavailable\n");
            goto out;
        }
        if (create_shared_dxg_resource(dxg_fd, device, 256 * 256 * 4,
                                       &shared_fd, &resource, &query) < 0)
            goto out;
        if (validate_direct_open_shared_resource(dxg_fd, device, shared_fd,
                                                 &query) < 0)
            goto out;
        if (create_shared_dxg_fence(dxg_fd, device, &fence_fd,
                                    &sync_object) < 0)
            goto out;
        if (dxg_only) {
            printf("d3d12sharedsmoke: raw DXG shared resource/fence validation ok resource=0x%x fd=%d allocations=%u total_priv=%u fence=0x%x fence_fd=%d\n",
                   resource.v, shared_fd, query.allocation_count,
                   query.total_priv_drv_data_size, sync_object.v, fence_fd);
            ret = 0;
            goto out;
        }
    }
    {
        char source_luid_text[32];

        format_winluid_text(source_luid_text, sizeof(source_luid_text),
                            adapter_luid);
        printf("d3d12sharedsmoke: source adapter=0x%x luid=%s low=0x%08x high=0x%08x mode=%s\n",
               adapter.v, source_luid_text, adapter_luid.a, adapter_luid.b,
               use_runtime ? "d3d12-runtime" : "dxg-ioctl");
    }
    if (expect_bad_dimensions_reject) {
        buffer_width++;
        printf("d3d12sharedsmoke: negative submit enabled kind=wrong-dimensions submitted_size=%ux%u expected_compositor_reject=1 native_present_claim=0\n",
               buffer_width, buffer_height);
    }
    if (expect_bad_format_reject) {
        buffer_format = WL_SHM_FORMAT_XRGB8888;
        printf("d3d12sharedsmoke: negative submit enabled kind=wrong-format submitted_format=0x%x expected_compositor_reject=1 native_present_claim=0\n",
               buffer_format);
    }
    if (expect_missing_fence_reject) {
        printf("d3d12sharedsmoke: negative submit enabled kind=missing-fence-fd expected_compositor_reject=1 native_present_claim=0\n");
    }
    if (expect_stale_fence_reject) {
        submitted_fence_value = 0;
        printf("d3d12sharedsmoke: negative submit enabled kind=stale-fence-target submitted_target=%lu expected_compositor_reject=1 native_present_claim=0\n",
               (unsigned long)submitted_fence_value);
    }
    presented_luid = adapter_luid;
    if (expect_luid_reject) {
        presented_luid.a ^= 0x1;
        {
            char mismatch_luid_text[32];

            format_winluid_text(mismatch_luid_text,
                                sizeof(mismatch_luid_text), presented_luid);
            printf("d3d12sharedsmoke: sending mismatched luid=%s low=0x%08x high=0x%08x\n",
                   mismatch_luid_text, presented_luid.a, presented_luid.b);
        }
    }

    wayland_commit_reason = "before_wayland_connect";
    if (!getenv("XDG_RUNTIME_DIR")) {
        setenv("XDG_RUNTIME_DIR", "/tmp", 1);
        printf("d3d12sharedsmoke: wayland env defaulted name=XDG_RUNTIME_DIR value=/tmp\n");
    }
    if (!getenv("WAYLAND_DISPLAY")) {
        setenv("WAYLAND_DISPLAY", "wayland-0", 1);
        printf("d3d12sharedsmoke: wayland env defaulted name=WAYLAND_DISPLAY value=wayland-0\n");
    }

    app.display = wl_display_connect(NULL);
    if (!app.display) {
        wayland_commit_reason = "wl_display_connect_failed";
        fprintf(stderr, "d3d12sharedsmoke: wl_display_connect failed\n");
        goto out;
    }
    app.registry = wl_display_get_registry(app.display);
    wl_registry_add_listener(app.registry, &registry_listener, &app);
    wl_display_roundtrip(app.display);
    if (!app.compositor || !app.gpu_manager ||
        wl_proxy_get_version(app.gpu_manager) < 3) {
        wayland_commit_reason = "missing_compositor_or_gpu_manager_v3";
        fprintf(stderr,
                "d3d12sharedsmoke: missing compositor/gpu-manager-v3\n");
        goto out;
    }
    if (app.gpu_manager_version >= 6)
        printf("d3d12sharedsmoke: using gpu-manager-v6 run-id fence-value path\n");
    else if (app.gpu_manager_version >= 5)
        printf("d3d12sharedsmoke: using gpu-manager-v5 fence-value path\n");
    else if (app.gpu_manager_version >= 4)
        printf("d3d12sharedsmoke: using gpu-manager-v4 adapter-luid path\n");

    app.surface = wl_compositor_create_surface(app.compositor);
    if (!app.surface) {
        wayland_commit_reason = "create_surface_failed";
        fprintf(stderr, "d3d12sharedsmoke: create surface failed\n");
        goto out;
    }

    buffer_fd = dup(shared_fd);
    if (!expect_missing_fence_reject)
        buffer_fence_fd = dup(fence_fd);
    if (buffer_fd < 0 ||
        (!expect_missing_fence_reject && buffer_fence_fd < 0)) {
        wayland_commit_reason = "dup_resource_or_acquire_fd_failed";
        goto out;
    }
    acquire_fd_kind = expect_missing_fence_reject ?
        "missing-fence-fd" :
        (runtime_opts.runtime_dxg_syncfile_acquire ?
         "dxg-shared-sync-fd" : "d3d12-fence-nt-fd");
    printf("d3d12sharedsmoke: wayland D3D12 submit plan resource_fd=%d acquire_fd=%d acquire_target=%lu acquire_sync=%s acquire_fd_kind=%s wl_format=0x%x expected_dxgi_format=%u d3d12_fence_fd_used=%d present_claim=requires-compositor-completion native_present_claim=0 require_present=%d negative_bad_luid=%d negative_bad_dimensions=%d negative_bad_format=%d negative_missing_fence=%d negative_stale_fence=%d\n",
           shared_fd, fence_fd,
           (unsigned long)submitted_fence_value,
           runtime_opts.runtime_dxg_syncfile_acquire ?
               "dxg-syncfile-acquire" : "legacy-d3d12-fence-fd",
           acquire_fd_kind, buffer_format, DXGI_FORMAT_B8G8R8A8_UNORM,
           (runtime_opts.runtime_dxg_syncfile_acquire ||
            expect_missing_fence_reject) ? 0 : 1,
           require_present, expect_luid_reject,
           expect_bad_dimensions_reject, expect_bad_format_reject,
           expect_missing_fence_reject, expect_stale_fence_reject);
    if (expect_missing_fence_reject) {
        app.buffer =
            xv6_gpu_buffer_manager_create_d3d12_resource_buffer_luid(
                app.gpu_manager, buffer_fd, presented_luid,
                buffer_width, buffer_height, buffer_format,
                query.allocation_count, query.total_priv_drv_data_size);
    } else {
        app.buffer =
            xv6_gpu_buffer_manager_create_d3d12_resource_buffer_with_fence(
                app.gpu_manager, buffer_fd, buffer_fence_fd, presented_luid,
                buffer_width, buffer_height, buffer_format,
                query.allocation_count,
                query.total_priv_drv_data_size, submitted_fence_value);
    }
    buffer_fd = -1;
    buffer_fence_fd = -1;
    if (!app.buffer) {
        wayland_commit_reason = "create_d3d12_wl_buffer_failed";
        fprintf(stderr, "d3d12sharedsmoke: create D3D12 wl_buffer failed\n");
        goto out;
    }
    wl_buffer_add_listener(app.buffer, &buffer_listener, &app);
    if (require_present) {
        int stale_present_existed =
            access("/tmp/wlcomp-d3d12-present", F_OK) == 0;

        if (unlink("/tmp/wlcomp-d3d12-present") != 0 && errno != ENOENT) {
            wayland_commit_reason = "stale_present_evidence_unlink_failed";
            fprintf(stderr,
                    "d3d12sharedsmoke: could not remove stale present evidence errno=%d\n",
                    errno);
            goto out;
        }
        printf("d3d12sharedsmoke: stale present evidence cleared path=/tmp/wlcomp-d3d12-present existed=%d\n",
               stale_present_existed);
        present_before = 0;
        present_rejected = 0;
        app.frame_callback = wl_surface_frame(app.surface);
        if (!app.frame_callback) {
            wayland_commit_reason = "frame_callback_create_failed";
            fprintf(stderr, "d3d12sharedsmoke: frame callback failed\n");
            goto out;
        }
        wl_callback_add_listener(app.frame_callback, &frame_listener, &app);
    }
    wl_surface_attach(app.surface, app.buffer, 0, 0);
    wl_surface_damage(app.surface, 0, 0, buffer_width, buffer_height);
    present_commit_ms = now_ms();
    wayland_commit_state = "attempted";
    wayland_commit_reason = "wl_surface_commit";
    printf("d3d12sharedsmoke: wayland commit attempted resource_fd=%d acquire_fd=%d require_present=%d acquire_sync=%s\n",
           shared_fd, fence_fd, require_present,
           runtime_opts.runtime_dxg_syncfile_acquire ?
               "dxg-syncfile-acquire" : "legacy-d3d12-fence-fd");
    wl_surface_commit(app.surface);

    if (wl_display_roundtrip(app.display) < 0 ||
        wl_display_roundtrip(app.display) < 0) {
        if (expect_luid_reject || expect_bad_dimensions_reject ||
            expect_bad_format_reject || expect_missing_fence_reject ||
            expect_stale_fence_reject) {
            const char *kind = expect_luid_reject ? "bad_luid" :
                (expect_bad_dimensions_reject ? "wrong_dimensions" :
                 (expect_bad_format_reject ? "wrong_format" :
                  (expect_missing_fence_reject ? "missing_fence_fd" :
                   "stale_fence_target")));

            wayland_commit_state = "rejected_expected_negative_contract";
            wayland_commit_reason = kind;
            printf("d3d12sharedsmoke: compositor rejected expected negative D3D12 contract kind=%s\n",
                   kind);
            ret = 0;
            goto out;
        }
        wayland_commit_state = "failed";
        wayland_commit_reason = "compositor_rejected_commit";
        fprintf(stderr, "d3d12sharedsmoke: compositor rejected commit\n");
        goto out;
    }

    if (expect_luid_reject || expect_bad_dimensions_reject ||
        expect_bad_format_reject || expect_missing_fence_reject ||
        expect_stale_fence_reject) {
        const char *kind = expect_luid_reject ? "bad_luid" :
            (expect_bad_dimensions_reject ? "wrong_dimensions" :
             (expect_bad_format_reject ? "wrong_format" :
              (expect_missing_fence_reject ? "missing_fence_fd" :
               "stale_fence_target")));

        wayland_commit_state = "failed";
        wayland_commit_reason = "negative_contract_unexpectedly_accepted";
        fprintf(stderr,
                "d3d12sharedsmoke: negative D3D12 contract unexpectedly accepted kind=%s\n",
                kind);
        goto out;
    }
    wayland_commit_state = "accepted";
    wayland_commit_reason = "commit_roundtrip_ok";

    if (require_present) {
        int terminal_present_seen = 0;
        int terminal_fail_closed = 0;
        int native_present_valid = 0;

        alarm(45);
        for (int i = 0; i < 450; i++) {
            if (read_fresh_native_present_evidence(&present_evidence)) {
                present_after = present_evidence.counter;
                present_rejected = present_evidence.rejected;
                if (present_after > present_before &&
                    validate_native_present_evidence(&present_evidence,
                                                     &adapter_luid,
                                                     present_commit_ms) == 0) {
                    native_present_valid = 1;
                    terminal_present_seen = 1;
                    break;
                }
                if (present_evidence_is_terminal_fail_closed(
                        &present_evidence, &adapter_luid,
                        present_commit_ms)) {
                    terminal_fail_closed = 1;
                    terminal_present_seen = 1;
                    break;
                }
            }
            if (wl_display_dispatch_pending(app.display) < 0)
                break;
            wl_display_flush(app.display);
            usleep(100000);
        }
        if (!terminal_present_seen &&
            !read_fresh_native_present_evidence(&present_evidence)) {
            present_evidence_state = "missing";
            wayland_commit_reason = "present_evidence_file_missing";
            fprintf(stderr,
                    "d3d12sharedsmoke: GPU present evidence file missing or unreadable before=%lu\n",
                    (unsigned long)present_before);
            goto out;
        }
        present_after = present_evidence.counter;
        present_rejected = present_evidence.rejected;
        if (!terminal_present_seen) {
            if (present_after > present_before &&
                validate_native_present_evidence(&present_evidence,
                                                 &adapter_luid,
                                                 present_commit_ms) == 0) {
                native_present_valid = 1;
                terminal_present_seen = 1;
            } else if (present_evidence_is_terminal_fail_closed(
                           &present_evidence, &adapter_luid,
                           present_commit_ms)) {
                terminal_fail_closed = 1;
                terminal_present_seen = 1;
            }
        }
        present_evidence_state = "present";
        if (!native_present_valid) {
            char source_luid_text[32];
            char matched_luid_text[32];

            present_evidence_state = terminal_fail_closed ?
                "present_fail_closed_terminal" :
                "present_intermediate_timeout";
            wayland_commit_reason = terminal_fail_closed ?
                "present_fail_closed_terminal" :
                "present_evidence_terminal_timeout";
            format_winluid_text(source_luid_text, sizeof(source_luid_text),
                                present_evidence.source_luid);
            format_winluid_text(matched_luid_text, sizeof(matched_luid_text),
                                present_evidence.matched_luid);
            fprintf(stderr,
                    "d3d12sharedsmoke: GPU present evidence terminal wait result before=%lu after=%lu terminal=%d fail_closed=%d rejected=%d stage=%s path=%d run_id=%s client_buffer_id=%lu manager_resource_id=%lu buffer_generation=%lu display_target_kind=%s handoff=%lu handoff_requires_kernel=%lu target_requires_kernel=%lu display_correlated=%lu native_requirements=%lu dxg_reg=%lu/%lu reg_errno=%lu source=0x%lx dxg_commit=%lu/%lu commit_errno=%lu commit_status=%lu expected_eopnotsupp=%lu present_id=%lu completed=%lu buffer_reg=%lu/%lu buffer_reg_errno=%lu buffer_source=0x%lx buffer_commit=%lu/%lu buffer_commit_errno=%lu buffer_commit_status=%lu buffer_expected_eopnotsupp=%lu buffer_present_id=%lu buffer_completed=%lu buffer_query=%lu/%lu buffer_query_errno=%lu buffer_query_required=%lu buffer_query_skipped_commit_failed=%lu buffer_query_skipped_no_present_id=%lu buffer_query_after_commit_success=%lu buffer_query_kernel_missing=%lu source_registered=%lu source_query_attempted=%lu source_query_skip_reason=%s gpu_p_or_dda_transport_absent=%lu commit_rejected_eopnotsupp=%lu no_present_id_completed=%lu no_gpu_p_or_dda_display_bind=%lu no_display_handoff=%lu no_present_completion=%lu same_frame_callbacks_blocked=%lu same_frame_releases_blocked=%lu callback_blocked=%lu release_blocked=%lu source_adapter_luid=%08lx:%08lx source_provenance=0x%lx source_register_flags=0x%lx buffer_correlated=%lu callbacks=%lu callback_resource=0x%lx callback_sequence=%lu releases=%lu release_resource=0x%lx release_sequence=%lu mtime_ms=%ld min_mtime_ms=%ld starts=%lu copy=%lu completes=%lu resource=0x%lx allocations=%lu fence=0x%lx target=%lu release=%lu fmt=0x%lx cpu_readback=%lu cpu_mapping=%lu cpu_copy=%lu source_luid=%s matched_luid=%s\n",
                    (unsigned long)present_before,
                    (unsigned long)present_after, terminal_present_seen,
                    terminal_fail_closed, present_rejected,
                    present_evidence.evidence_stage[0] ?
                    present_evidence.evidence_stage : "none",
                    present_evidence.native_path,
                    present_evidence.run_id[0] ? present_evidence.run_id :
                    "none",
                    (unsigned long)present_evidence.client_buffer_id,
                    (unsigned long)present_evidence.manager_resource_id,
                    (unsigned long)present_evidence.buffer_generation,
                    present_evidence.display_target_kind[0] ?
                    present_evidence.display_target_kind : "none",
                    (unsigned long)present_evidence.display_handoff_implemented,
                    (unsigned long)present_evidence.
                        display_handoff_requires_kernel_host_protocol,
                    (unsigned long)present_evidence.
                        display_target_requires_kernel_host_protocol,
                    (unsigned long)present_evidence.
                        display_completion_correlated,
                    (unsigned long)present_evidence.
                        native_present_requirements_satisfied,
                    (unsigned long)present_evidence.
                        dxg_present_source_register_attempts,
                    (unsigned long)present_evidence.
                        dxg_present_source_register_successes,
                    (unsigned long)present_evidence.
                        dxg_present_source_register_errno,
                    (unsigned long)present_evidence.dxg_present_source,
                    (unsigned long)present_evidence.
                        dxg_present_source_commit_attempts,
                    (unsigned long)present_evidence.
                        dxg_present_source_commit_successes,
                    (unsigned long)present_evidence.
                        dxg_present_source_commit_errno,
                    (unsigned long)present_evidence.
                        dxg_present_source_commit_status,
                    (unsigned long)present_evidence.
                        dxg_present_source_commit_expected_eopnotsupp,
                    (unsigned long)present_evidence.dxg_present_id,
                    (unsigned long)present_evidence.dxg_present_completed,
                    (unsigned long)present_evidence.
                        buffer_present_source_register_attempts,
                    (unsigned long)present_evidence.
                        buffer_present_source_register_successes,
                    (unsigned long)present_evidence.
                        buffer_present_source_register_errno,
                    (unsigned long)present_evidence.buffer_present_source,
                    (unsigned long)present_evidence.
                        buffer_present_source_commit_attempts,
                    (unsigned long)present_evidence.
                        buffer_present_source_commit_successes,
                    (unsigned long)present_evidence.
                        buffer_present_source_commit_errno,
                    (unsigned long)present_evidence.
                        buffer_present_source_commit_status,
                    (unsigned long)present_evidence.
                        buffer_present_source_commit_expected_eopnotsupp,
                    (unsigned long)present_evidence.
                        buffer_present_source_present_id,
                    (unsigned long)present_evidence.
                        buffer_present_source_completed,
                    (unsigned long)present_evidence.
                        buffer_present_source_query_attempts,
                    (unsigned long)present_evidence.
                        buffer_present_source_query_successes,
                    (unsigned long)present_evidence.
                        buffer_present_source_query_errno,
                    (unsigned long)present_evidence.
                        buffer_present_source_query_required,
                    (unsigned long)present_evidence.
                        buffer_present_source_query_skipped_commit_failed,
                    (unsigned long)present_evidence.
                        buffer_present_source_query_skipped_no_present_id,
                    (unsigned long)present_evidence.
                        buffer_present_source_query_attempted_after_commit_success,
                    (unsigned long)present_evidence.
                        buffer_present_source_query_kernel_missing,
                    (unsigned long)present_evidence.present_source_registered,
                    (unsigned long)present_evidence.present_source_query_attempted,
                    present_evidence.present_source_query_skipped_reason[0] ?
                    present_evidence.present_source_query_skipped_reason :
                    "none",
                    (unsigned long)present_evidence.
                        present_source_gpu_p_or_dda_transport_absent,
                    (unsigned long)present_evidence.
                        present_source_commit_rejected_eopnotsupp,
                    (unsigned long)present_evidence.
                        present_source_no_present_id_completed,
                    (unsigned long)present_evidence.
                        present_source_no_gpu_p_or_dda_display_bind,
                    (unsigned long)present_evidence.
                        present_source_no_display_handoff,
                    (unsigned long)present_evidence.
                        present_source_no_present_completion,
                    (unsigned long)present_evidence.
                        present_source_same_frame_callbacks_blocked,
                    (unsigned long)present_evidence.
                        present_source_same_frame_releases_blocked,
                    (unsigned long)present_evidence.
                        present_source_callback_blocked,
                    (unsigned long)present_evidence.
                        present_source_release_blocked,
                    (unsigned long)present_evidence.
                        present_source_adapter_luid_high,
                    (unsigned long)present_evidence.
                        present_source_adapter_luid_low,
                    (unsigned long)present_evidence.
                        present_source_provenance_flags,
                    (unsigned long)present_evidence.
                        present_source_register_flags,
                    (unsigned long)present_evidence.
                        buffer_present_source_completion_correlated,
                    (unsigned long)present_evidence.
                        frame_callback_observed,
                    (unsigned long)present_evidence.
                        frame_callback_resource,
                    (unsigned long)present_evidence.
                        frame_callback_present_sequence,
                    (unsigned long)present_evidence.
                        buffer_release_observed,
                    (unsigned long)present_evidence.
                        buffer_release_resource,
                    (unsigned long)present_evidence.
                        buffer_release_present_sequence,
                    (long)present_evidence.mtime_ms,
                    (long)present_commit_ms,
                    (unsigned long)present_evidence.starts,
                    (unsigned long)present_evidence.copy_completes,
                    (unsigned long)present_evidence.completes,
                    (unsigned long)present_evidence.resource,
                    (unsigned long)present_evidence.allocation_count,
                    (unsigned long)present_evidence.fence,
                    (unsigned long)present_evidence.fence_target,
                    (unsigned long)present_evidence.release_fence,
                    (unsigned long)present_evidence.format,
                    (unsigned long)present_evidence.cpu_readback,
                    (unsigned long)present_evidence.cpu_mapping,
                    (unsigned long)present_evidence.cpu_copy,
                    source_luid_text, matched_luid_text);
            fprintf(stderr,
                    "d3d12sharedsmoke: phase2 negative evidence bad_luid=%lu wrong_dimensions=%lu wrong_format=%lu missing_resource_fd=%lu missing_fence_fd=%lu stale_fence=%lu cpu_mappable_fallback=%lu import_only_satisfies=%lu open_only_satisfies=%lu gpu_copy_only_satisfies=%lu\n",
                    (unsigned long)present_evidence.phase2_bad_luid_rejected,
                    (unsigned long)present_evidence.
                        phase2_wrong_dimensions_rejected,
                    (unsigned long)present_evidence.
                        phase2_wrong_format_rejected,
                    (unsigned long)present_evidence.
                        phase2_missing_resource_fd_rejected,
                    (unsigned long)present_evidence.
                        phase2_missing_fence_fd_rejected,
                    (unsigned long)present_evidence.
                        phase2_stale_fence_rejected,
                    (unsigned long)present_evidence.
                        phase2_cpu_mappable_fallback_rejected,
                    (unsigned long)present_evidence.
                        phase2_import_only_satisfies_native_present,
                    (unsigned long)present_evidence.
                        phase2_open_only_satisfies_native_present,
                    (unsigned long)present_evidence.
                        phase2_gpu_copy_only_satisfies_native_present);
            if (terminal_fail_closed && allow_failclosed_present) {
                printf("d3d12sharedsmoke: d3d12_wayland_resource_buffer_runtime_matrix resource_export=PASS resource_open=PASS dxg_syncfile_export=PASS dxg_syncfile_import=PASS same_adapter_luid=PASS compositor_import=PASS present_source_register=PASS present_commit=failclosed-eopnotsupp terminal_failclosed=PASS native_present_claim=0 opengl_submit_credit=0 status=PASS\n");
                ret = 0;
                goto out;
            }
            goto out;
        }
        present_evidence_state = "present_ok";
        wayland_commit_reason = "present_validation_ok";
        {
            char source_luid_text[32];
            char matched_luid_text[32];

            format_winluid_text(source_luid_text, sizeof(source_luid_text),
                                present_evidence.source_luid);
            format_winluid_text(matched_luid_text, sizeof(matched_luid_text),
                                present_evidence.matched_luid);
            printf("d3d12sharedsmoke: native present evidence ok path=d3d12-dxg-present-source-display-handoff run_id=%s client_buffer_id=%lu manager_resource_id=%lu buffer_generation=%lu display_target_kind=%s dxg_present_source=0x%lx present_id=%lu completed=%lu buffer_present_id=%lu buffer_completed=%lu callbacks=%lu callback_resource=0x%lx callback_sequence=%lu releases=%lu release_resource=0x%lx release_sequence=%lu mtime_ms=%ld min_mtime_ms=%ld starts=%lu copy=%lu completes=%lu resource=0x%lx allocations=%lu fence=0x%lx target=%lu release=%lu fmt=0x%lx source_luid=%s matched_luid=%s no_cpu_readback=1\n",
                   present_evidence.run_id[0] ? present_evidence.run_id :
                   "none",
                   (unsigned long)present_evidence.client_buffer_id,
                   (unsigned long)present_evidence.manager_resource_id,
                   (unsigned long)present_evidence.buffer_generation,
                   present_evidence.display_target_kind[0] ?
                   present_evidence.display_target_kind : "unknown",
                   (unsigned long)present_evidence.dxg_present_source,
                   (unsigned long)present_evidence.dxg_present_id,
                   (unsigned long)present_evidence.dxg_present_completed,
                   (unsigned long)present_evidence.
                       buffer_present_source_present_id,
                   (unsigned long)present_evidence.
                       buffer_present_source_completed,
                   (unsigned long)present_evidence.frame_callback_observed,
                   (unsigned long)present_evidence.frame_callback_resource,
                   (unsigned long)present_evidence.
                       frame_callback_present_sequence,
                   (unsigned long)present_evidence.buffer_release_observed,
                   (unsigned long)present_evidence.buffer_release_resource,
                   (unsigned long)present_evidence.
                       buffer_release_present_sequence,
                   (long)present_evidence.mtime_ms,
                   (long)present_commit_ms,
                   (unsigned long)present_evidence.starts,
                   (unsigned long)present_evidence.copy_completes,
                   (unsigned long)present_evidence.completes,
                   (unsigned long)present_evidence.resource,
                   (unsigned long)present_evidence.allocation_count,
                   (unsigned long)present_evidence.fence,
                   (unsigned long)present_evidence.fence_target,
                   (unsigned long)present_evidence.release_fence,
                   (unsigned long)present_evidence.format,
                   source_luid_text, matched_luid_text);
        }
        printf("d3d12sharedsmoke: present validation ok frame=%d release=%d gpu_present=%lu->%lu\n",
               app.frame_seen, app.release_seen,
               (unsigned long)present_before, (unsigned long)present_after);
        if (use_runtime) {
            d3d12_runtime_print_phase_result(
                d3d12_runtime_export_label(&runtime_opts), "present-success",
                "resource", runtime.device != NULL,
                runtime.resource_handle != NULL, 0, 0,
                runtime.resource_handle, runtime.adapter_luid,
                "present_success");
            d3d12_runtime_print_export_result(
                d3d12_runtime_export_label(&runtime_opts), "present-success",
                "resource", runtime.resource_handle != NULL, 0, 0,
                runtime.resource_handle, runtime.adapter_luid,
                runtime.diag_resource_flags, runtime.diag_heap_flags,
                runtime.diag_initial_state,
                runtime.diag_app_sync_suppressed ? "suppressed" : "enabled",
                runtime.diag_adapter_path, 1,
                runtime.opened_resource != NULL);
        }
        printf("d3d12sharedsmoke: shared-surface OpenGL-submit contract ok mode=%s allocations=%u total_priv=%u fence_value=1\n",
               use_runtime ? "d3d12-runtime" : "dxg-ioctl",
               query.allocation_count, query.total_priv_drv_data_size);
    }

    printf("d3d12sharedsmoke: committed D3D12 shared resource buffer release=%d\n",
           app.release_seen);
    if (require_present)
        wayland_commit_reason = "success";
    ret = 0;

out:
    if (use_runtime && require_present) {
        printf("d3d12sharedsmoke: runtime-present-control-flow wayland_commit_attempted=%u wayland_commit_state=%s reason=%s present_evidence=%s evidence_file=/tmp/wlcomp-d3d12-present resource_export=%s resource_open=%s fence_export=%s d3d12_fence_open=%s acquire_sync=%s auto_acquire=%u allow_failclosed=%u strict_pass=%u\n",
               strcmp(wayland_commit_state, "not_attempted") != 0,
               wayland_commit_state, wayland_commit_reason,
               present_evidence_state,
               runtime.resource_handle ? "PASS" : "FAIL",
               runtime.opened_resource ? "PASS" : "FAIL",
               runtime.fence_handle ? "PASS" : "FAIL",
               runtime_opts.runtime_dxg_syncfile_acquire ?
                   "BYPASSED_EXPECTED_FAIL" :
                   (runtime.opened_fence ? "PASS" : "FAIL"),
               runtime_opts.runtime_dxg_syncfile_acquire ?
                   "dxg-syncfile-acquire" : "legacy-d3d12-fence-fd",
               auto_runtime_dxg_syncfile_acquire, allow_failclosed_present,
               ret == 0);
    }
    if (ret != 0 && use_runtime && require_present) {
        HANDLE failed_handle = runtime.resource_handle ?
            runtime.resource_handle :
            (runtime.heap_handle ? runtime.heap_handle : runtime.fence_handle);
        const char *failed_kind = runtime.resource_handle ? "resource" :
            (runtime.heap_handle ? "heap" :
             (runtime.fence_handle ? "fence" : "none"));

        d3d12_runtime_print_phase_result(
            d3d12_runtime_export_label(&runtime_opts),
            "present-failed-before-success", failed_kind,
            runtime.device != NULL, failed_handle != NULL,
            (HRESULT)0x80004005, 0, failed_handle, runtime.adapter_luid,
            "present_failed");
        d3d12_runtime_print_export_result(
            d3d12_runtime_export_label(&runtime_opts),
            "present-failed-before-success", failed_kind,
            failed_handle != NULL, (HRESULT)0x80004005, 0, failed_handle,
            runtime.adapter_luid, runtime.diag_resource_flags,
            runtime.diag_heap_flags, runtime.diag_initial_state,
            runtime.diag_app_sync_suppressed ? "suppressed" : "enabled",
            runtime.diag_adapter_path, app.buffer != NULL,
            runtime.opened_resource != NULL);
    }
    if (buffer_fd >= 0)
        close(buffer_fd);
    if (buffer_fence_fd >= 0)
        close(buffer_fence_fd);
    if (app.frame_callback)
        wl_callback_destroy(app.frame_callback);
    if (app.buffer)
        wl_buffer_destroy(app.buffer);
    if (app.toplevel)
        xdg_toplevel_destroy(app.toplevel);
    if (app.xdg_surface)
        xdg_surface_destroy(app.xdg_surface);
    if (app.surface)
        wl_surface_destroy(app.surface);
    if (app.gpu_manager)
        wl_proxy_destroy(app.gpu_manager);
    if (app.wm_base)
        xdg_wm_base_destroy(app.wm_base);
    if (app.compositor)
        wl_compositor_destroy(app.compositor);
    if (app.registry)
        wl_registry_destroy(app.registry);
    if (app.display)
        wl_display_disconnect(app.display);
    if (!use_runtime && shared_fd >= 0)
        close(shared_fd);
    if ((!use_runtime || fence_fd_owned) && fence_fd >= 0)
        close(fence_fd);
    if (sync_object.v != 0) {
        struct d3dkmt_destroysynchronizationobject destroy_sync;

        memset(&destroy_sync, 0, sizeof(destroy_sync));
        destroy_sync.sync_object = sync_object;
        ioctl(dxg_fd, LX_DXDESTROYSYNCHRONIZATIONOBJECT, &destroy_sync);
    }
    if (resource.v != 0) {
        struct d3dkmt_destroyallocation2 destroy_allocation;

        memset(&destroy_allocation, 0, sizeof(destroy_allocation));
        destroy_allocation.device = device;
        destroy_allocation.resource = resource;
        destroy_allocation.flags.assume_not_in_use = 1;
        ioctl(dxg_fd, LX_DXDESTROYALLOCATION2, &destroy_allocation);
    }
    close_dxg_device(dxg_fd, adapter, device);
    d3d12_runtime_destroy(&runtime);
    return ret;
}
