/* ── xv6_gpu_buffer_manager ──────────────────────────────────────── */

#define COBJMACROS
#define INITGUID
#include <wsl/winadapter.h>
#include <directx/d3d12.h>
#include <directx/dxgiformat.h>
#undef interface

#ifndef FB_GPU_DXG_PRESENT_SOURCE_REGISTER
#define FB_GPU_DXG_PRESENT_SOURCE_REGISTER 0x462F
#endif
#ifndef FB_GPU_DXG_PRESENT_SOURCE_COMMIT
#define FB_GPU_DXG_PRESENT_SOURCE_COMMIT   0x4630
#endif
#ifndef FB_GPU_DXG_PRESENT_SOURCE_QUERY
#define FB_GPU_DXG_PRESENT_SOURCE_QUERY    0x4631
#endif
#ifndef FB_GPU_DXG_PRESENT_F_WAIT_SYNC
#define FB_GPU_DXG_PRESENT_F_WAIT_SYNC 0x1
#endif
#ifndef FB_GPU_DXG_DISPLAY_TARGET_NONE
#define FB_GPU_DXG_DISPLAY_TARGET_NONE 0
#endif
#ifndef FB_GPU_DXG_DISPLAY_TARGET_SYNTHVID_VRAM_D3D12_EXISTING_SYSMEM
#define FB_GPU_DXG_DISPLAY_TARGET_SYNTHVID_VRAM_D3D12_EXISTING_SYSMEM 1
#endif
#ifndef FB_GPU_DXG_DISPLAY_TARGET_RUNTIME_D3D12_RESOURCE
#define FB_GPU_DXG_DISPLAY_TARGET_RUNTIME_D3D12_RESOURCE 2
#endif
#ifndef FB_GPU_DXG_DISPLAY_TARGET_DDA_NOUVEAU_SCANOUT
#define FB_GPU_DXG_DISPLAY_TARGET_DDA_NOUVEAU_SCANOUT 3
#endif
#ifndef FB_GPU_DXG_PRESENT_MISSING_NONE
#define FB_GPU_DXG_PRESENT_MISSING_NONE 0
#endif
#ifndef FB_GPU_DXG_PRESENT_MISSING_SCANOUT_BIND
#define FB_GPU_DXG_PRESENT_MISSING_SCANOUT_BIND 1
#endif
#ifndef FB_GPU_DXG_PRESENT_PROV_DXG_FD
#define FB_GPU_DXG_PRESENT_PROV_DXG_FD 0x0001
#endif
#ifndef FB_GPU_DXG_PRESENT_PROV_RESOURCE_FD
#define FB_GPU_DXG_PRESENT_PROV_RESOURCE_FD 0x0002
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
#ifndef XV6_D3D12_DISPLAY_TARGET_SYNTHVID_VRAM_D3D12_EXISTING_SYSMEM
#define XV6_D3D12_DISPLAY_TARGET_SYNTHVID_VRAM_D3D12_EXISTING_SYSMEM \
    FB_GPU_DXG_DISPLAY_TARGET_SYNTHVID_VRAM_D3D12_EXISTING_SYSMEM
#endif
#ifndef XV6_D3D12_DISPLAY_TARGET_RUNTIME_CREATED_D3D12_RESOURCE
#define XV6_D3D12_DISPLAY_TARGET_RUNTIME_CREATED_D3D12_RESOURCE \
    FB_GPU_DXG_DISPLAY_TARGET_RUNTIME_D3D12_RESOURCE
#endif
#ifndef DRM_FORMAT_ARGB8888
#define DRM_FORMAT_ARGB8888 0x34325241u
#endif
#ifndef DRM_FORMAT_XRGB8888
#define DRM_FORMAT_XRGB8888 0x34325258u
#endif
#ifndef DRM_FORMAT_MOD_LINEAR
#define DRM_FORMAT_MOD_LINEAR 0
#endif

struct fb_gpu_dxg_present_source_register {
    int32_t  dxg_fd;
    int32_t  resource_fd;
    uint32_t device;
    uint32_t resource;
    uint32_t allocation;
    uint32_t allocation_count;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t format;
    uint64_t modifier;
    uint64_t reserved0;
    /*
     * Keep flags/present_source at the original stable prefix offsets.
     * The LUID/provenance fields below are an append-only diagnostic tail.
     */
    uint32_t flags;
    uint32_t present_source;
    uint32_t adapter_luid_low;
    uint32_t adapter_luid_high;
    uint32_t provenance_flags;
    uint32_t reserved1;
};

struct fb_gpu_dxg_present_source_commit {
    uint32_t present_source;
    uint32_t flags;
    uint32_t sync_object;
    uint32_t reserved;
    uint64_t fence_value;
    uint64_t present_id;
    uint64_t completed;
};

struct fb_gpu_dxg_present_source_query {
    uint32_t present_source;
    uint32_t flags;
    uint32_t display_target_kind;
    uint32_t source_live;
    uint64_t present_id;
    uint64_t completed;
    uint64_t host_handoff_missing;
    uint64_t requires_host_protocol;
    uint64_t missing_host_abi;
    uint64_t helper_contract_version;
    uint64_t helper_required_metadata;
    uint64_t helper_transport;
    uint64_t helper_transport_present;
    uint64_t helper_operation;
    uint64_t helper_lifetime;
    uint64_t helper_requires_completion;
    uint32_t device;
    uint32_t resource;
    uint32_t allocation;
    uint32_t allocation_count;
    uint32_t sync_object;
    uint32_t last_flags;
    uint64_t fence_value;
    uint64_t last_ret;
    int32_t dxg_fd;
    int32_t resource_fd;
    uint32_t provenance_flags;
    uint32_t selected_lane;
    uint32_t adapter_luid_low;
    uint32_t adapter_luid_high;
    uint32_t adapter_identity;
    uint32_t helper_block_reason;
    uint64_t host_candidates;
    uint64_t host_rejects;
};

static const struct wl_interface *xv6_gpu_buffer_create_types[] = {
    &wl_buffer_interface,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
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

struct d3dkmthandle {
    union {
        struct {
            uint32_t instance : 6;
            uint32_t index : 24;
            uint32_t unique : 2;
        };
        uint32_t v;
    };
};

struct winluid {
    uint32_t a;
    uint32_t b;
};

static void xv6_format_winluid_text(char *buf, size_t buflen,
                                    struct winluid luid)
{
    if (!buf || buflen == 0)
        return;
    /*
     * Diagnostic text uses high:low ordering to match dxgtrace/dxgprobe
     * WDDM trace comparisons. UAPI fields remain low/high in a/b.
     */
    snprintf(buf, buflen, "%08x:%08x", luid.b, luid.a);
}

struct d3dkmt_adapterinfo {
    struct d3dkmthandle adapter_handle;
    struct winluid adapter_luid;
    uint32_t num_sources;
    uint32_t present_move_regions_preferred;
};

struct d3dkmt_enumadapters2 {
    uint32_t num_adapters;
    uint32_t reserved;
    uint64_t adapters;
};

struct d3dkmt_closeadapter {
    struct d3dkmthandle adapter_handle;
};

struct d3dkmt_openadapterfromluid {
    struct winluid adapter_luid;
    struct d3dkmthandle adapter_handle;
};

struct d3dkmt_createdeviceflags {
    uint32_t legacy_mode : 1;
    uint32_t request_vSync : 1;
    uint32_t disable_gpu_timeout : 1;
    uint32_t gdi_device : 1;
    uint32_t reserved : 28;
};

struct d3dkmt_createdevice {
    struct d3dkmthandle adapter;
    uint32_t reserved3;
    struct d3dkmt_createdeviceflags flags;
    struct d3dkmthandle device;
    uint64_t command_buffer;
    uint32_t command_buffer_size;
    uint32_t reserved;
    uint64_t allocation_list;
    uint32_t allocation_list_size;
    uint32_t reserved1;
    uint64_t patch_location_list;
    uint32_t patch_location_list_size;
    uint32_t reserved2;
};

struct d3dkmt_destroydevice {
    struct d3dkmthandle device;
};

struct d3dddi_synchronizationobject_flags {
    union {
        struct {
            uint32_t shared : 1;
            uint32_t nt_security_sharing : 1;
            uint32_t cross_adapter : 1;
            uint32_t top_of_pipeline : 1;
            uint32_t no_signal : 1;
            uint32_t no_wait : 1;
            uint32_t no_signal_max_value_on_tdr : 1;
            uint32_t no_gpu_access : 1;
            uint32_t reserved : 23;
        };
        uint32_t value;
    };
};

struct d3dkmt_destroysynchronizationobject {
    struct d3dkmthandle sync_object;
};

struct d3dddi_waitforsynchronizationobjectfromcpu_flags {
    union {
        struct {
            uint32_t wait_any : 1;
            uint32_t reserved : 31;
        };
        uint32_t value;
    };
};

struct d3dkmt_waitforsynchronizationobjectfromcpu {
    struct d3dkmthandle device;
    uint32_t object_count;
    uint64_t objects;
    uint64_t fence_values;
    uint64_t async_event;
    struct d3dddi_waitforsynchronizationobjectfromcpu_flags flags;
};

struct d3dkmt_opensyncobjectfromnthandle2 {
    uint64_t nt_handle;
    struct d3dkmthandle device;
    struct d3dddi_synchronizationobject_flags flags;
    struct d3dkmthandle sync_object;
    uint32_t reserved1;
    union {
        struct {
            uint64_t fence_value_cpu_va;
            uint64_t fence_value_gpu_va;
            uint32_t engine_affinity;
        } monitored_fence;
        uint64_t reserved[8];
    };
};

struct d3dkmt_opensyncobjectfromsyncfile {
    uint64_t sync_file_handle;
    struct d3dkmthandle device;
    struct d3dkmthandle syncobj;
    uint64_t fence_value;
    uint64_t fence_value_cpu_va;
    uint64_t fence_value_gpu_va;
};

struct d3dddicb_destroyallocation2flags {
    union {
        struct {
            uint32_t assume_not_in_use : 1;
            uint32_t synchronous_destroy : 1;
            uint32_t reserved : 29;
            uint32_t system_use_only : 1;
        };
        uint32_t value;
    };
};

struct d3dkmt_destroyallocation2 {
    struct d3dkmthandle device;
    struct d3dkmthandle resource;
    uint64_t allocations;
    uint32_t alloc_count;
    struct d3dddicb_destroyallocation2flags flags;
};

struct d3dddi_openallocationinfo2 {
    struct d3dkmthandle allocation;
    uint64_t priv_drv_data;
    uint32_t priv_drv_data_size;
    uint64_t gpu_va;
    uint64_t reserved[6];
};

struct d3dkmt_openresourcefromnthandle {
    struct d3dkmthandle device;
    uint32_t reserved;
    uint64_t nt_handle;
    uint32_t allocation_count;
    uint32_t reserved1;
    uint64_t open_alloc_info;
    int32_t private_runtime_data_size;
    uint32_t reserved2;
    uint64_t private_runtime_data;
    uint32_t resource_priv_drv_data_size;
    uint32_t reserved3;
    uint64_t resource_priv_drv_data;
    uint32_t total_priv_drv_data_size;
    uint64_t total_priv_drv_data;
    struct d3dkmthandle resource;
    struct d3dkmthandle keyed_mutex;
    uint64_t keyed_mutex_private_data;
    uint32_t keyed_mutex_private_data_size;
    struct d3dkmthandle sync_object;
};

struct d3dkmt_queryresourceinfofromnthandle {
    struct d3dkmthandle device;
    uint32_t reserved;
    uint64_t nt_handle;
    uint64_t private_runtime_data;
    uint32_t private_runtime_data_size;
    uint32_t total_priv_drv_data_size;
    uint32_t resource_priv_drv_data_size;
    uint32_t allocation_count;
};

#define XV6_D3DKMT_ADAPTERS_MAX 64
#define LX_DXOPENADAPTERFROMLUID \
    _IOWR(0x47, 0x01, struct d3dkmt_openadapterfromluid)
#define LX_DXCREATEDEVICE \
    _IOWR(0x47, 0x02, struct d3dkmt_createdevice)
#define LX_DXENUMADAPTERS2 \
    _IOWR(0x47, 0x14, struct d3dkmt_enumadapters2)
#define LX_DXCLOSEADAPTER \
    _IOWR(0x47, 0x15, struct d3dkmt_closeadapter)
#define LX_DXDESTROYDEVICE \
    _IOWR(0x47, 0x19, struct d3dkmt_destroydevice)
#define LX_DXDESTROYSYNCHRONIZATIONOBJECT \
    _IOWR(0x47, 0x1d, struct d3dkmt_destroysynchronizationobject)
#define LX_DXDESTROYALLOCATION2 \
    _IOWR(0x47, 0x13, struct d3dkmt_destroyallocation2)
#define LX_DXQUERYRESOURCEINFOFROMNTHANDLE \
    _IOWR(0x47, 0x41, struct d3dkmt_queryresourceinfofromnthandle)
#define LX_DXOPENRESOURCEFROMNTHANDLE \
    _IOWR(0x47, 0x42, struct d3dkmt_openresourcefromnthandle)
#define LX_DXOPENSYNCOBJECTFROMNTHANDLE2 \
    _IOWR(0x47, 0x40, struct d3dkmt_opensyncobjectfromnthandle2)
#define LX_DXOPENSYNCOBJECTFROMSYNCFILE \
    _IOWR(0x47, 0x47, struct d3dkmt_opensyncobjectfromsyncfile)
#define LX_DXWAITFORSYNCHRONIZATIONOBJECTFROMCPU \
    _IOWR(0x47, 0x3a, struct d3dkmt_waitforsynchronizationobjectfromcpu)

#define XV6_D3D12_PRESENT_EVIDENCE_PATH "/tmp/wlcomp-d3d12-present"

static char g_d3d12_present_missing[384];
static uint64_t g_d3d12_resource_submit_count;
static uint64_t g_d3d12_resource_protocol_accept_count;
static uint64_t g_d3d12_native_import_attempt_count;
static uint64_t g_d3d12_native_import_complete_count;
static uint64_t g_d3d12_resource_import_attempt_count;
static uint64_t g_d3d12_resource_import_success_count;
static uint64_t g_d3d12_fence_import_attempt_count;
static uint64_t g_d3d12_fence_import_success_count;
static uint64_t g_d3d12_syncfile_fence_import_attempt_count;
static uint64_t g_d3d12_syncfile_fence_import_success_count;
static uint64_t g_d3d12_fence_target_observed_count;
static uint64_t g_d3d12_framebuffer_fallback_count;
static uint64_t g_d3d12_buffer_release_observed_count;
static uint64_t g_d3d12_frame_callback_observed_count;
static uint64_t g_d3d12_buffer_release_native_success_count;
static uint64_t g_d3d12_frame_callback_native_success_count;
static uint64_t g_d3d12_failclosed_client_unblock_count;
static uint64_t g_d3d12_failclosed_client_unblock_resource;
static uint64_t g_d3d12_failclosed_client_unblock_sequence;
static uint64_t g_d3d12_failclosed_client_unblock_buffer_generation;
static uint64_t g_d3d12_failclosed_client_unblock_attempt_id;
static uint64_t g_d3d12_failclosed_client_unblock_release_count;
static uint64_t g_d3d12_failclosed_client_unblock_callback_count;
static uint64_t g_d3d12_native_present_attempt_count;
static uint64_t g_d3d12_native_present_completion_count;
static uint64_t g_d3d12_next_buffer_generation;
static uint64_t g_d3d12_last_buffer_generation;
static uint64_t g_d3d12_last_native_present_attempt_id;
static uint64_t g_d3d12_last_native_present_completion_id;
static pid_t g_d3d12_last_client_pid;
static uint32_t g_d3d12_last_client_buffer_id;
static uint32_t g_d3d12_last_manager_resource_id;
static char g_d3d12_last_client_run_id[96];
static uint32_t g_d3d12_buffer_release_resource;
static uint64_t g_d3d12_buffer_release_present_sequence;
static uint64_t g_d3d12_buffer_release_buffer_generation;
static uint64_t g_d3d12_buffer_release_attempt_id;
static uint64_t g_d3d12_buffer_release_completion_id;
static uint64_t g_d3d12_buffer_release_present_id;
static uint32_t g_d3d12_frame_callback_resource;
static uint64_t g_d3d12_frame_callback_present_sequence;
static uint64_t g_d3d12_frame_callback_buffer_generation;
static uint64_t g_d3d12_frame_callback_attempt_id;
static uint64_t g_d3d12_frame_callback_completion_id;
static uint64_t g_d3d12_frame_callback_present_id;
static uint64_t g_d3d12_evidence_generation;
static uint64_t g_d3d12_fb_heap_open_attempt_count;
static uint64_t g_d3d12_fb_heap_open_success_count;
static uint64_t g_d3d12_fb_heap_open_notimpl_count;
static uint64_t g_d3d12_fb_heap_open_cached_reject_count;
static uint64_t g_d3d12_compositor_target_create_attempt_count;
static uint64_t g_d3d12_compositor_target_create_success_count;
static uint64_t g_d3d12_compositor_gpu_copy_submit_count;
static uint64_t g_d3d12_compositor_gpu_copy_wait_count;
static uint64_t g_d3d12_compositor_copy_device_ptr;
static uint64_t g_d3d12_compositor_copy_queue_ptr;
static uint64_t g_d3d12_compositor_copy_cmdlist_ptr;
static uint64_t g_d3d12_dxg_present_register_attempt_count;
static uint64_t g_d3d12_dxg_present_register_success_count;
static uint64_t g_d3d12_dxg_present_commit_attempt_count;
static uint64_t g_d3d12_dxg_present_commit_success_count;
static int g_d3d12_dxg_present_register_errno;
static int g_d3d12_dxg_present_commit_errno;
static uint32_t g_d3d12_dxg_present_source;
static uint64_t g_d3d12_dxg_present_id;
static uint64_t g_d3d12_dxg_present_completed;
static uint64_t g_d3d12_phase2_bad_luid_reject_count;
static uint64_t g_d3d12_phase2_wrong_dimensions_reject_count;
static uint64_t g_d3d12_phase2_wrong_format_reject_count;
static uint64_t g_d3d12_phase2_missing_resource_fd_reject_count;
static uint64_t g_d3d12_phase2_missing_fence_fd_reject_count;
static uint64_t g_d3d12_phase2_stale_fence_reject_count;
static int g_d3d12_phase2_bad_luid_rejected_current;
static int g_d3d12_phase2_wrong_dimensions_rejected_current;
static int g_d3d12_phase2_wrong_format_rejected_current;
static int g_d3d12_phase2_missing_resource_fd_rejected_current;
static int g_d3d12_phase2_missing_fence_fd_rejected_current;
static int g_d3d12_phase2_stale_fence_rejected_current;
static int g_d3d12_phase2_cpu_mappable_fallback_rejected_current;

#define XV6_D3D12_IDENTITY_COUNTER_RECORDS 64

struct xv6_d3d12_identity_counter_record {
    char run_id[96];
    pid_t client_pid;
    uint32_t client_buffer_id;
    uint32_t manager_resource_id;
    uint32_t resource;
    uint64_t buffer_generation;
    uint64_t client_attempts;
    uint64_t client_completions;
    uint64_t client_rejects;
    uint64_t resource_attempts;
    uint64_t resource_completions;
    uint64_t resource_rejects;
    uint64_t last_seen;
};

static struct xv6_d3d12_identity_counter_record
    g_d3d12_identity_counters[XV6_D3D12_IDENTITY_COUNTER_RECORDS];
static uint64_t g_d3d12_identity_counter_clock;

struct xv6_d3d12_present_contract;
struct xv6_d3d12_present_result;

static void xv6_d3d12_write_stage_evidence(const char *stage, int err);
static void xv6_d3d12_write_present_evidence(
    const struct xv6_d3d12_present_contract *present,
    const struct xv6_d3d12_present_result *result,
    int rejected,
    int err);
static void xv6_d3d12_drain_failclosed_client(
    struct wlcomp_buffer *buf, const char *reason);

struct xv6_d3d12_present_rect {
    int32_t x;
    int32_t y;
    uint32_t w;
    uint32_t h;
};

struct xv6_d3d12_present_contract {
    uint32_t adapter;
    uint32_t device;
    uint32_t resource;
    uint32_t allocation_count;
    const uint32_t *allocations;
    uint32_t fence;
    uint64_t fence_target;
    uint64_t fence_current;
    uint32_t format;
    uint32_t source_luid_a;
    uint32_t source_luid_b;
    int source_luid_valid;
    uint32_t matched_luid_a;
    uint32_t matched_luid_b;
    uint32_t descriptor_width;
    uint32_t descriptor_height;
    uint32_t descriptor_pitch;
    uint64_t descriptor_modifier;
    uint32_t descriptor_sample_count;
    int descriptor_dxg_fd;
    int descriptor_resource_fd;
    int wait_confirmed;
    uint32_t total_priv_size;
    pid_t client_pid;
    uint32_t client_buffer_id;
    uint32_t manager_resource_id;
    uint64_t buffer_generation;
    uint64_t native_present_attempt_id;
    uint64_t native_present_completion_id;
    uint64_t framebuffer_blit_attempts_at_present;
    uint64_t framebuffer_blit_successes_at_present;
    uint64_t cpu_map_rejects_at_present;
    uint64_t cpu_plane_rejects_at_present;
    uint64_t present_source_register_attempts;
    uint64_t present_source_register_successes;
    int present_source_register_errno;
    uint32_t present_source;
    uint32_t present_source_register_flags;
    uint32_t present_source_adapter_luid_low;
    uint32_t present_source_adapter_luid_high;
    uint32_t present_source_provenance_flags;
    uint64_t present_source_commit_attempts;
    uint64_t present_source_commit_successes;
    int present_source_commit_errno;
    int present_source_commit_status;
    int present_source_commit_expected_eopnotsupp;
    uint64_t present_source_present_id;
    uint64_t present_source_completed;
    uint64_t present_source_query_attempts;
    uint64_t present_source_query_successes;
    int present_source_query_errno;
    uint32_t display_target_kind;
    uint32_t dirty_rect_count;
    uint64_t dirty_sequence;
    int present_source_completion_correlated;
    struct xv6_d3d12_present_rect dst;
    struct xv6_d3d12_present_rect src;
};

struct xv6_d3d12_present_result {
    uint64_t start_sequence;
    uint64_t copy_sequence;
    uint64_t complete_sequence;
    uint64_t release_fence;
};

static const char *xv6_d3d12_present_run_id(void)
{
    static char run_id[96];
    const char *env;

    if (g_d3d12_last_client_run_id[0])
        return g_d3d12_last_client_run_id;
    env = getenv("XV6_WLCOMP_D3D12_RUN_ID");
    if (env && *env) {
        snprintf(run_id, sizeof(run_id), "%s", env);
        return run_id;
    }
    if (!run_id[0])
        snprintf(run_id, sizeof(run_id), "wlcomp-%d-%lu", (int)getpid(),
                 (unsigned long)get_time_us());
    return run_id;
}

static struct xv6_d3d12_identity_counter_record *
xv6_d3d12_get_identity_counter_record(const struct wlcomp_buffer *buf,
                                      int create)
{
    struct xv6_d3d12_identity_counter_record *empty = NULL;
    const char *run_id;

    if (!buf || !buf->is_d3d12_shared)
        return NULL;
    run_id = buf->d3d12_client_run_id[0] ?
        buf->d3d12_client_run_id : xv6_d3d12_present_run_id();
    for (size_t i = 0; i < XV6_D3D12_IDENTITY_COUNTER_RECORDS; i++) {
        struct xv6_d3d12_identity_counter_record *rec =
            &g_d3d12_identity_counters[i];

        if (rec->last_seen == 0) {
            if (!empty)
                empty = rec;
            continue;
        }
        if (rec->client_pid == buf->d3d12_client_pid &&
            rec->client_buffer_id == buf->d3d12_client_buffer_id &&
            rec->manager_resource_id == buf->d3d12_manager_resource_id &&
            rec->resource == buf->d3d12_opened_resource &&
            rec->buffer_generation == buf->d3d12_buffer_generation &&
            strcmp(rec->run_id, run_id) == 0)
            return rec;
    }
    if (!create)
        return NULL;
    if (!empty) {
        uint64_t oldest = UINT64_MAX;

        empty = &g_d3d12_identity_counters[0];
        for (size_t i = 0; i < XV6_D3D12_IDENTITY_COUNTER_RECORDS; i++) {
            struct xv6_d3d12_identity_counter_record *rec =
                &g_d3d12_identity_counters[i];

            if (rec->last_seen < oldest) {
                oldest = rec->last_seen;
                empty = rec;
            }
        }
    }
    memset(empty, 0, sizeof(*empty));
    snprintf(empty->run_id, sizeof(empty->run_id), "%s", run_id);
    empty->client_pid = buf->d3d12_client_pid;
    empty->client_buffer_id = buf->d3d12_client_buffer_id;
    empty->manager_resource_id = buf->d3d12_manager_resource_id;
    empty->resource = buf->d3d12_opened_resource;
    empty->buffer_generation = buf->d3d12_buffer_generation;
    empty->last_seen = ++g_d3d12_identity_counter_clock;
    return empty;
}

static void xv6_d3d12_note_identity_present_attempt(
    struct wlcomp_buffer *buf)
{
    struct xv6_d3d12_identity_counter_record *rec;

    rec = xv6_d3d12_get_identity_counter_record(buf, 1);
    if (!rec)
        return;
    rec->client_attempts++;
    rec->resource_attempts++;
    rec->last_seen = ++g_d3d12_identity_counter_clock;
}

static void xv6_d3d12_note_identity_present_completion(
    struct wlcomp_buffer *buf)
{
    struct xv6_d3d12_identity_counter_record *rec;

    rec = xv6_d3d12_get_identity_counter_record(buf, 1);
    if (!rec)
        return;
    rec->client_completions++;
    rec->resource_completions++;
    rec->last_seen = ++g_d3d12_identity_counter_clock;
}

static void xv6_d3d12_note_identity_present_reject(
    struct wlcomp_buffer *buf)
{
    struct xv6_d3d12_identity_counter_record *rec;

    if (!buf || buf->d3d12_native_present_reject_recorded ||
        buf->d3d12_native_present_completion_id != 0 ||
        buf->d3d12_native_present_attempt_id == 0)
        return;
    rec = xv6_d3d12_get_identity_counter_record(buf, 1);
    if (!rec)
        return;
    rec->client_rejects++;
    rec->resource_rejects++;
    rec->last_seen = ++g_d3d12_identity_counter_clock;
    buf->d3d12_native_present_reject_recorded = 1;
}

static int xv6_d3d12_failclosed_unblocks_clients(void)
{
    static int initialized;
    static int enabled = 1;
    const char *env;

    if (initialized)
        return enabled;
    initialized = 1;
    env = getenv("XV6_WLCOMP_D3D12_FAILCLOSED_UNBLOCK");
    if (env && *env)
        enabled = atoi(env) != 0;
    enabled = cmdline_int_value("wlcomp_d3d12_failclosed_unblock",
                                enabled) != 0;
    return enabled;
}

static void xv6_d3d12_unblock_failclosed_client(
    struct wlcomp_buffer *buf, const char *reason)
{
    static int logs;

    if (!buf || !buf->is_d3d12_shared || buf->d3d12_present_complete ||
        buf->d3d12_failclosed_client_unblocked ||
        !xv6_d3d12_failclosed_unblocks_clients())
        return;

    buf->d3d12_failclosed_client_unblocked = 1;
    buf->d3d12_present_evidence_pending = 0;
    buf->d3d12_present_sequence = get_time_us();
    buf->d3d12_present_fence_value = 0;
    g_d3d12_failclosed_client_unblock_count++;
    g_d3d12_failclosed_client_unblock_resource =
        buf->d3d12_opened_resource;
    g_d3d12_failclosed_client_unblock_sequence =
        buf->d3d12_present_sequence;
    g_d3d12_failclosed_client_unblock_buffer_generation =
        buf->d3d12_buffer_generation;
    g_d3d12_failclosed_client_unblock_attempt_id =
        buf->d3d12_native_present_attempt_id;
    if (logs < 16) {
        fprintf(stderr,
                "wlcomp: d3d12 fail-closed client unblocked no_native_present_credit=1 reason=%s resource=0x%x attempts=%lu copies=%lu native_completions=%lu backend_opengl_submit=0\n",
                reason ? reason : "unknown", buf->d3d12_opened_resource,
                (unsigned long)g_d3d12_native_present_attempt_count,
                (unsigned long)g_d3d12_present_copy_complete_count,
                (unsigned long)g_d3d12_native_present_completion_count);
        logs++;
    }
    xv6_d3d12_drain_failclosed_client(buf, reason);
}

static void xv6_d3d12_set_last_client_run_id(const char *client_run_id)
{
    size_t i;
    size_t out = 0;

    g_d3d12_last_client_run_id[0] = '\0';
    if (!client_run_id)
        return;
    for (i = 0; client_run_id[i] &&
         out + 1 < sizeof(g_d3d12_last_client_run_id); i++) {
        unsigned char ch = (unsigned char)client_run_id[i];

        g_d3d12_last_client_run_id[out++] =
            (ch <= ' ' || ch == 0x7f) ? '_' : (char)ch;
    }
    g_d3d12_last_client_run_id[out] = '\0';
}

static const char *xv6_d3d12_display_target_kind_name(uint32_t kind)
{
    switch (kind) {
    case FB_GPU_DXG_DISPLAY_TARGET_NONE:
        return "none";
    case XV6_D3D12_DISPLAY_TARGET_SYNTHVID_VRAM_D3D12_EXISTING_SYSMEM:
        return "synthvid-vram-d3d12-existing-sysmem";
    case XV6_D3D12_DISPLAY_TARGET_RUNTIME_CREATED_D3D12_RESOURCE:
        return "runtime-created-d3d12-resource";
    default:
        return "kernel-reported-unknown";
    }
}

static int xv6_d3d12_display_target_is_runtime_resource(uint32_t kind)
{
    return kind == XV6_D3D12_DISPLAY_TARGET_RUNTIME_CREATED_D3D12_RESOURCE;
}

static int xv6_d3d12_display_target_is_synthvid_sysmem(uint32_t kind)
{
    return kind ==
        XV6_D3D12_DISPLAY_TARGET_SYNTHVID_VRAM_D3D12_EXISTING_SYSMEM;
}

static uint64_t xv6_d3d12_counter_delta(uint64_t current, uint64_t baseline)
{
    return current >= baseline ? current - baseline : 0;
}

enum xv6_d3d12_phase2_negative_kind {
    XV6_D3D12_NEG_BAD_LUID,
    XV6_D3D12_NEG_WRONG_DIMENSIONS,
    XV6_D3D12_NEG_WRONG_FORMAT,
    XV6_D3D12_NEG_MISSING_RESOURCE_FD,
    XV6_D3D12_NEG_MISSING_FENCE_FD,
    XV6_D3D12_NEG_STALE_FENCE,
};

static void xv6_d3d12_reset_phase2_negative_current(void)
{
    g_d3d12_phase2_bad_luid_rejected_current = 0;
    g_d3d12_phase2_wrong_dimensions_rejected_current = 0;
    g_d3d12_phase2_wrong_format_rejected_current = 0;
    g_d3d12_phase2_missing_resource_fd_rejected_current = 0;
    g_d3d12_phase2_missing_fence_fd_rejected_current = 0;
    g_d3d12_phase2_stale_fence_rejected_current = 0;
    g_d3d12_phase2_cpu_mappable_fallback_rejected_current = 0;
}

static void xv6_d3d12_note_phase2_negative(
    enum xv6_d3d12_phase2_negative_kind kind)
{
    switch (kind) {
    case XV6_D3D12_NEG_BAD_LUID:
        g_d3d12_phase2_bad_luid_rejected_current = 1;
        g_d3d12_phase2_bad_luid_reject_count++;
        break;
    case XV6_D3D12_NEG_WRONG_DIMENSIONS:
        g_d3d12_phase2_wrong_dimensions_rejected_current = 1;
        g_d3d12_phase2_wrong_dimensions_reject_count++;
        break;
    case XV6_D3D12_NEG_WRONG_FORMAT:
        g_d3d12_phase2_wrong_format_rejected_current = 1;
        g_d3d12_phase2_wrong_format_reject_count++;
        break;
    case XV6_D3D12_NEG_MISSING_RESOURCE_FD:
        g_d3d12_phase2_missing_resource_fd_rejected_current = 1;
        g_d3d12_phase2_missing_resource_fd_reject_count++;
        break;
    case XV6_D3D12_NEG_MISSING_FENCE_FD:
        g_d3d12_phase2_missing_fence_fd_rejected_current = 1;
        g_d3d12_phase2_missing_fence_fd_reject_count++;
        break;
    case XV6_D3D12_NEG_STALE_FENCE:
        g_d3d12_phase2_stale_fence_rejected_current = 1;
        g_d3d12_phase2_stale_fence_reject_count++;
        break;
    }
}

#define XV6_D3D12_PRESENT_SOURCE_RECORDS 64

struct xv6_d3d12_present_source_record {
    const struct wlcomp_buffer *buf;
    pid_t client_pid;
    uint32_t client_buffer_id;
    uint32_t manager_resource_id;
    uint64_t buffer_generation;
    uint64_t register_attempts;
    uint64_t register_successes;
    int register_errno;
    uint32_t present_source;
    uint32_t register_flags;
    uint32_t adapter_luid_low;
    uint32_t adapter_luid_high;
    uint32_t provenance_flags;
    uint64_t commit_attempts;
    uint64_t commit_successes;
    int commit_errno;
    int commit_status;
    int commit_expected_eopnotsupp;
    uint64_t present_id;
    uint64_t completed;
    uint64_t query_attempts;
    uint64_t query_successes;
    int query_errno;
    uint32_t display_target_kind;
    uint32_t dirty_rect_count;
    uint64_t dirty_sequence;
    int completion_correlated;
};

static struct xv6_d3d12_present_source_record
    g_d3d12_present_source_records[XV6_D3D12_PRESENT_SOURCE_RECORDS];

static struct xv6_d3d12_present_source_record *
xv6_d3d12_get_present_source_record(const struct wlcomp_buffer *buf, int create)
{
    struct xv6_d3d12_present_source_record *empty = NULL;

    if (!buf)
        return NULL;
    for (size_t i = 0; i < XV6_D3D12_PRESENT_SOURCE_RECORDS; i++) {
        struct xv6_d3d12_present_source_record *rec =
            &g_d3d12_present_source_records[i];

        if (!rec->buf) {
            if (!empty)
                empty = rec;
            continue;
        }
        if (rec->buf == buf &&
            rec->buffer_generation == buf->d3d12_buffer_generation)
            return rec;
    }
    if (!create)
        return NULL;
    if (!empty) {
        uint64_t oldest_generation = UINT64_MAX;

        empty = &g_d3d12_present_source_records[0];
        for (size_t i = 0; i < XV6_D3D12_PRESENT_SOURCE_RECORDS; i++) {
            struct xv6_d3d12_present_source_record *rec =
                &g_d3d12_present_source_records[i];

            if (rec->buffer_generation < oldest_generation) {
                oldest_generation = rec->buffer_generation;
                empty = rec;
            }
        }
    }
    memset(empty, 0, sizeof(*empty));
    empty->buf = buf;
    empty->client_pid = buf->d3d12_client_pid;
    empty->client_buffer_id = buf->d3d12_client_buffer_id;
    empty->manager_resource_id = buf->d3d12_manager_resource_id;
    empty->buffer_generation = buf->d3d12_buffer_generation;
    return empty;
}

static void xv6_d3d12_reset_present_source_record(
    const struct wlcomp_buffer *buf)
{
    struct xv6_d3d12_present_source_record *rec;

    rec = xv6_d3d12_get_present_source_record(buf, 1);
    if (!rec)
        return;
    rec->register_attempts = 0;
    rec->register_successes = 0;
    rec->register_errno = 0;
    rec->present_source = 0;
    rec->commit_attempts = 0;
    rec->commit_successes = 0;
    rec->commit_errno = 0;
    rec->commit_status = 0;
    rec->commit_expected_eopnotsupp = 0;
    rec->present_id = 0;
    rec->completed = 0;
    rec->query_attempts = 0;
    rec->query_successes = 0;
    rec->query_errno = 0;
    rec->display_target_kind = FB_GPU_DXG_DISPLAY_TARGET_NONE;
    rec->dirty_rect_count = 0;
    rec->dirty_sequence = 0;
    rec->completion_correlated = 0;
}

static void xv6_d3d12_set_last_identity_from_buffer(
    const struct wlcomp_buffer *buf)
{
    if (!buf)
        return;
    g_d3d12_last_client_pid = buf->d3d12_client_pid;
    g_d3d12_last_client_buffer_id = buf->d3d12_client_buffer_id;
    g_d3d12_last_manager_resource_id = buf->d3d12_manager_resource_id;
    xv6_d3d12_set_last_client_run_id(buf->d3d12_client_run_id);
    g_d3d12_last_buffer_generation = buf->d3d12_buffer_generation;
    if (buf->d3d12_native_present_attempt_id)
        g_d3d12_last_native_present_attempt_id =
            buf->d3d12_native_present_attempt_id;
    if (buf->d3d12_native_present_completion_id)
        g_d3d12_last_native_present_completion_id =
            buf->d3d12_native_present_completion_id;
}

static void xv6_d3d12_fill_present_identity(
    struct xv6_d3d12_present_contract *present,
    const struct wlcomp_buffer *buf)
{
    struct xv6_d3d12_present_source_record *source_record;

    if (!present || !buf)
        return;
    present->client_pid = buf->d3d12_client_pid;
    present->client_buffer_id = buf->d3d12_client_buffer_id;
    present->manager_resource_id = buf->d3d12_manager_resource_id;
    present->buffer_generation = buf->d3d12_buffer_generation;
    present->native_present_attempt_id = buf->d3d12_native_present_attempt_id;
    present->native_present_completion_id =
        buf->d3d12_native_present_completion_id;
    present->descriptor_width = buf->width > 0 ? (uint32_t)buf->width : 0;
    present->descriptor_height = buf->height > 0 ? (uint32_t)buf->height : 0;
    present->descriptor_pitch =
        present->descriptor_width != 0 ? present->descriptor_width * 4U : 0;
    present->descriptor_modifier = DRM_FORMAT_MOD_LINEAR;
    present->descriptor_sample_count = 1;
    present->descriptor_dxg_fd = buf->d3d12_dxg_fd;
    present->descriptor_resource_fd = buf->d3d12_resource_fd;
    present->framebuffer_blit_attempts_at_present =
        buf->d3d12_fb_blit_attempts_at_present;
    present->framebuffer_blit_successes_at_present =
        buf->d3d12_fb_blit_successes_at_present;
    present->cpu_map_rejects_at_present =
        buf->d3d12_cpu_map_rejects_at_present;
    present->cpu_plane_rejects_at_present =
        buf->d3d12_cpu_plane_rejects_at_present;
    source_record = xv6_d3d12_get_present_source_record(buf, 0);
    if (source_record) {
        present->present_source_register_attempts =
            source_record->register_attempts;
        present->present_source_register_successes =
            source_record->register_successes;
        present->present_source_register_errno =
            source_record->register_errno;
        present->present_source = source_record->present_source;
        present->present_source_register_flags =
            source_record->register_flags;
        present->present_source_adapter_luid_low =
            source_record->adapter_luid_low;
        present->present_source_adapter_luid_high =
            source_record->adapter_luid_high;
        present->present_source_provenance_flags =
            source_record->provenance_flags;
        present->present_source_commit_attempts =
            source_record->commit_attempts;
        present->present_source_commit_successes =
            source_record->commit_successes;
        present->present_source_commit_errno = source_record->commit_errno;
        present->present_source_commit_status = source_record->commit_status;
        present->present_source_commit_expected_eopnotsupp =
            source_record->commit_expected_eopnotsupp;
        present->present_source_present_id = source_record->present_id;
        present->present_source_completed = source_record->completed;
        present->present_source_query_attempts =
            source_record->query_attempts;
        present->present_source_query_successes =
            source_record->query_successes;
        present->present_source_query_errno = source_record->query_errno;
        present->display_target_kind = source_record->display_target_kind;
        present->dirty_rect_count = source_record->dirty_rect_count;
        present->dirty_sequence = source_record->dirty_sequence;
        present->present_source_completion_correlated =
            source_record->completion_correlated;
    }
    xv6_d3d12_set_last_identity_from_buffer(buf);
}

static void xv6_d3d12_write_phase2_negative_evidence(FILE *fp)
{
    if (!fp)
        return;
    fprintf(fp,
            "d3d12_phase2_bad_luid_rejected=%d\n"
            "d3d12_phase2_bad_luid_rejects=%lu\n"
            "d3d12_phase2_wrong_dimensions_rejected=%d\n"
            "d3d12_phase2_wrong_dimensions_rejects=%lu\n"
            "d3d12_phase2_wrong_format_rejected=%d\n"
            "d3d12_phase2_wrong_format_rejects=%lu\n"
            "d3d12_phase2_missing_resource_fd_rejected=%d\n"
            "d3d12_phase2_missing_resource_fd_rejects=%lu\n"
            "d3d12_phase2_missing_fence_fd_rejected=%d\n"
            "d3d12_phase2_missing_fence_fd_rejects=%lu\n"
            "d3d12_phase2_stale_fence_rejected=%d\n"
            "d3d12_phase2_stale_fence_rejects=%lu\n"
            "d3d12_phase2_cpu_mappable_fallback_rejected=%d\n"
            "d3d12_phase2_cpu_mappable_fallback_rejects=%lu\n"
            "d3d12_phase2_import_only_satisfies_native_present=0\n"
            "d3d12_phase2_open_only_satisfies_native_present=0\n"
            "d3d12_phase2_gpu_copy_only_satisfies_native_present=0\n"
            "d3d12_phase2_callbacks_require_native_present_complete=1\n"
            "d3d12_phase2_releases_require_native_present_complete=1\n",
            g_d3d12_phase2_bad_luid_rejected_current,
            (unsigned long)g_d3d12_phase2_bad_luid_reject_count,
            g_d3d12_phase2_wrong_dimensions_rejected_current,
            (unsigned long)g_d3d12_phase2_wrong_dimensions_reject_count,
            g_d3d12_phase2_wrong_format_rejected_current,
            (unsigned long)g_d3d12_phase2_wrong_format_reject_count,
            g_d3d12_phase2_missing_resource_fd_rejected_current,
            (unsigned long)g_d3d12_phase2_missing_resource_fd_reject_count,
            g_d3d12_phase2_missing_fence_fd_rejected_current,
            (unsigned long)g_d3d12_phase2_missing_fence_fd_reject_count,
            g_d3d12_phase2_stale_fence_rejected_current,
            (unsigned long)g_d3d12_phase2_stale_fence_reject_count,
            g_d3d12_phase2_cpu_mappable_fallback_rejected_current,
            (unsigned long)(g_d3d12_cpu_map_reject_count +
                            g_d3d12_cpu_plane_reject_count));
}

static void xv6_d3d12_write_final_handoff_evidence(
    FILE *fp,
    const struct xv6_d3d12_present_contract *present,
    const struct xv6_d3d12_present_result *result,
    int rejected)
{
    int runtime_target = present &&
        xv6_d3d12_display_target_is_runtime_resource(
            present->display_target_kind);
    int host_commit_success = runtime_target &&
        present->present_source_commit_attempts != 0 &&
        present->present_source_commit_successes != 0 &&
        present->present_source_commit_errno == 0 &&
        present->present_source_commit_status == 0 &&
        present->present_source_present_id != 0;
    int dirty_correlated = present &&
        present->dirty_rect_count != 0 &&
        present->dirty_sequence != 0 &&
        present->present_source_completed >= present->dirty_sequence;
    int completion_correlated = host_commit_success &&
        present->present_source_completed >=
            present->present_source_present_id &&
        present->present_source_completion_correlated &&
        dirty_correlated &&
        result && result->release_fence != 0;
    int no_cpu_readback = !present ||
        (xv6_d3d12_counter_delta(g_d3d12_cpu_map_reject_count,
                                 present->cpu_map_rejects_at_present) == 0 &&
         xv6_d3d12_counter_delta(g_d3d12_cpu_plane_reject_count,
                                 present->cpu_plane_rejects_at_present) == 0);
    int final_success = !rejected && runtime_target && host_commit_success &&
        completion_correlated && no_cpu_readback;

    if (!fp)
        return;
    fprintf(fp,
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
            "d3d12_final_handoff_runtime_resource_observed=%d\n"
            "d3d12_final_handoff_kernel_abi_required=1\n"
            "d3d12_final_handoff_kernel_abi=FB_GPU_DXG_PRESENT_SOURCE_COMMIT/runtime-resource-scanout\n"
            "d3d12_final_handoff_kernel_abi_missing=%d\n"
            "d3d12_final_handoff_host_display_commit_success=%d\n"
            "d3d12_final_handoff_present_id=%lu\n"
            "d3d12_final_handoff_completed=%lu\n"
            "d3d12_final_handoff_dirty_rects=%u\n"
            "d3d12_final_handoff_dirty_sequence=%lu\n"
            "d3d12_final_handoff_completion_correlated=%d\n"
            "d3d12_final_handoff_no_cpu_map_no_readback=%d\n"
            "d3d12_final_handoff_release_fence=%lu\n"
            "d3d12_final_handoff_callbacks_release_gate=native-display-completion\n"
            "d3d12_final_handoff_success=%d\n",
            runtime_target,
            host_commit_success ? 0 : 1,
            host_commit_success,
            (unsigned long)(present ? present->present_source_present_id : 0),
            (unsigned long)(present ? present->present_source_completed : 0),
            present ? present->dirty_rect_count : 0,
            (unsigned long)(present ? present->dirty_sequence : 0),
            completion_correlated,
            no_cpu_readback,
            (unsigned long)(result ? result->release_fence : 0),
            final_success);
}

struct xv6_gpu_buffer_manager_impl {
    void (*create_buffer)(struct wl_client *client,
                          struct wl_resource *resource,
                          uint32_t id,
                          uint32_t handle,
                          int32_t width,
                          int32_t height,
                          int32_t stride,
                          uint32_t format);
    void (*create_buffer_with_fence)(struct wl_client *client,
                                     struct wl_resource *resource,
                                     uint32_t id,
                                     uint32_t handle,
                                     int32_t width,
                                     int32_t height,
                                     int32_t stride,
                                     uint32_t format,
                                     int32_t acquire_fence_fd);
    void (*create_d3d12_resource_buffer)(struct wl_client *client,
                                         struct wl_resource *resource,
                                         uint32_t id,
                                         int32_t resource_fd,
                                         int32_t width,
                                         int32_t height,
                                         uint32_t format,
                                         uint32_t allocation_count,
                                         uint32_t total_priv_size);
    void (*create_d3d12_resource_buffer_with_fence)(struct wl_client *client,
                                                    struct wl_resource *resource,
                                                    uint32_t id,
                                                    int32_t resource_fd,
                                                    int32_t fence_fd,
                                                    int32_t width,
                                                    int32_t height,
                                                    uint32_t format,
                                                    uint32_t allocation_count,
                                                    uint32_t total_priv_size);
    void (*create_d3d12_resource_buffer_luid)(struct wl_client *client,
                                              struct wl_resource *resource,
                                              uint32_t id,
                                              int32_t resource_fd,
                                              uint32_t luid_a,
                                              uint32_t luid_b,
                                              int32_t width,
                                              int32_t height,
                                              uint32_t format,
                                              uint32_t allocation_count,
                                              uint32_t total_priv_size);
    void (*create_d3d12_resource_buffer_with_fence_luid)(
        struct wl_client *client,
        struct wl_resource *resource,
        uint32_t id,
        int32_t resource_fd,
        int32_t fence_fd,
        uint32_t luid_a,
        uint32_t luid_b,
        int32_t width,
        int32_t height,
        uint32_t format,
        uint32_t allocation_count,
        uint32_t total_priv_size);
    void (*create_d3d12_resource_buffer_with_fence_value_luid)(
        struct wl_client *client,
        struct wl_resource *resource,
        uint32_t id,
        int32_t resource_fd,
        int32_t fence_fd,
        uint32_t fence_value_low,
        uint32_t fence_value_high,
        uint32_t luid_a,
        uint32_t luid_b,
        int32_t width,
        int32_t height,
        uint32_t format,
        uint32_t allocation_count,
        uint32_t total_priv_size);
    void (*create_d3d12_resource_buffer_with_fence_value_luid_run_id)(
        struct wl_client *client,
        struct wl_resource *resource,
        uint32_t id,
        int32_t resource_fd,
        int32_t fence_fd,
        uint32_t fence_value_low,
        uint32_t fence_value_high,
        uint32_t luid_a,
        uint32_t luid_b,
        int32_t width,
        int32_t height,
        uint32_t format,
        uint32_t allocation_count,
        uint32_t total_priv_size,
        const char *client_run_id);
};

static void xv6_gpu_create_buffer_common(struct wl_client *client,
                                         struct wl_resource *resource,
                                         uint32_t id,
                                         uint32_t handle,
                                         int32_t width,
                                         int32_t height,
                                         int32_t stride,
                                         uint32_t format,
                                         int32_t acquire_fence_fd)
{
    struct fb_gpu_bo_import import;
    struct wlcomp_buffer *buf;
    struct wl_resource *buf_res;

    if (handle == 0 || width <= 0 || height <= 0 || stride <= 0 ||
        (format != WL_SHM_FORMAT_ARGB8888 &&
         format != WL_SHM_FORMAT_XRGB8888)) {
        if (acquire_fence_fd >= 0)
            close(acquire_fence_fd);
        wl_resource_post_error(resource, 0, "invalid xv6 GPU buffer");
        return;
    }

    memset(&import, 0, sizeof(import));
    import.handle = handle;
    if (ioctl(g_fb_fd, FB_GPU_BO_IMPORT, &import) < 0) {
        if (acquire_fence_fd >= 0)
            close(acquire_fence_fd);
        wl_resource_post_error(resource, 0, "FB_GPU_BO_IMPORT failed");
        return;
    }
    if ((int32_t)import.width != width ||
        (int32_t)import.height != height ||
        (int32_t)import.pitch != stride ||
        import.addr == 0 || import.size == 0) {
        munmap((void *)import.addr, (size_t)import.size);
        if (acquire_fence_fd >= 0)
            close(acquire_fence_fd);
        wl_resource_post_error(resource, 0, "xv6 GPU buffer metadata mismatch");
        return;
    }

    buf = calloc(1, sizeof(*buf));
    if (!buf) {
        munmap((void *)import.addr, (size_t)import.size);
        if (acquire_fence_fd >= 0)
            close(acquire_fence_fd);
        wl_resource_post_no_memory(resource);
        return;
    }
    buf->is_gpu_bo = 1;
    buf->gpu_addr = import.addr;
    buf->gpu_size = import.size;
    buf->gpu_handle = handle;
    buf->plane_gpu_addr[0] = import.addr;
    buf->plane_gpu_size[0] = import.size;
    buf->plane_gpu_handle[0] = handle;
    buf->acquire_fence_fd = acquire_fence_fd;
    buf->acquire_wait_start_ms = 0;
    buf->width = width;
    buf->height = height;
    buf->stride = stride;
    buf->plane_count = 1;
    buf->plane_stride[0] = stride;
    buf->plane_offset[0] = 0;
    buf->format = format;

    buf_res = wl_resource_create(client, &wl_buffer_interface, 1, id);
    if (!buf_res) {
        buffer_free(buf);
        wl_resource_post_no_memory(resource);
        return;
    }
    buf->resource = buf_res;
    wl_resource_set_implementation(buf_res, &buffer_impl, buf,
                                   buffer_destroy_handler);
    {
        pid_t client_pid = 0;
        uid_t client_uid = 0;
        gid_t client_gid = 0;
        wl_client_get_credentials(client, &client_pid, &client_uid,
                                  &client_gid);
        fprintf(stderr,
                "wlcomp: xv6gpu buffer pid=%d size=%dx%d stride=%d fmt=0x%x handle=%u\n",
                client_pid, width, height, stride, format, handle);
    }
}

static void xv6_d3d12_count_identity_present_progress(
    const struct xv6_d3d12_present_contract *present,
    uint64_t *client_attempts,
    uint64_t *client_completions,
    uint64_t *client_rejects,
    uint64_t *resource_attempts,
    uint64_t *resource_completions,
    uint64_t *resource_rejects)
{
    const char *run_id;

    if (client_attempts)
        *client_attempts = 0;
    if (client_completions)
        *client_completions = 0;
    if (client_rejects)
        *client_rejects = 0;
    if (resource_attempts)
        *resource_attempts = 0;
    if (resource_completions)
        *resource_completions = 0;
    if (resource_rejects)
        *resource_rejects = 0;
    if (!present || present->client_pid == 0)
        return;
    run_id = xv6_d3d12_present_run_id();

    for (size_t i = 0; i < XV6_D3D12_IDENTITY_COUNTER_RECORDS; i++) {
        struct xv6_d3d12_identity_counter_record *rec =
            &g_d3d12_identity_counters[i];
        int same_client;
        int same_resource_generation;

        if (rec->last_seen == 0)
            continue;
        same_client =
            rec->client_pid == present->client_pid &&
            strcmp(rec->run_id, run_id) == 0;
        same_resource_generation =
            same_client &&
            present->resource != 0 &&
            rec->resource == present->resource &&
            rec->buffer_generation == present->buffer_generation;
        if (same_client) {
            if (client_attempts)
                *client_attempts += rec->client_attempts;
            if (client_completions)
                *client_completions += rec->client_completions;
            if (client_rejects)
                *client_rejects += rec->client_rejects;
        }
        if (same_resource_generation) {
            if (resource_attempts)
                *resource_attempts += rec->resource_attempts;
            if (resource_completions)
                *resource_completions += rec->resource_completions;
            if (resource_rejects)
                *resource_rejects += rec->resource_rejects;
        }
    }
}

static void xv6_gpu_create_buffer(struct wl_client *client,
                                  struct wl_resource *resource,
                                  uint32_t id,
                                  uint32_t handle,
                                  int32_t width,
                                  int32_t height,
                                  int32_t stride,
                                  uint32_t format)
{
    xv6_gpu_create_buffer_common(client, resource, id, handle, width, height,
                                 stride, format, -1);
}

static void xv6_gpu_create_buffer_with_fence(struct wl_client *client,
                                             struct wl_resource *resource,
                                             uint32_t id,
                                             uint32_t handle,
                                             int32_t width,
                                             int32_t height,
                                             int32_t stride,
                                             uint32_t format,
                                             int32_t acquire_fence_fd)
{
    xv6_gpu_create_buffer_common(client, resource, id, handle, width, height,
                                 stride, format, acquire_fence_fd);
}

static void xv6_d3d12_close_device(int dxg_fd, uint32_t adapter,
                                   uint32_t device)
{
    struct d3dkmt_destroydevice destroy_device;
    struct d3dkmt_closeadapter close_adapter;

    if (dxg_fd < 0)
        return;
    if (device != 0) {
        memset(&destroy_device, 0, sizeof(destroy_device));
        destroy_device.device.v = device;
        ioctl(dxg_fd, LX_DXDESTROYDEVICE, &destroy_device);
    }
    if (adapter != 0) {
        memset(&close_adapter, 0, sizeof(close_adapter));
        close_adapter.adapter_handle.v = adapter;
        ioctl(dxg_fd, LX_DXCLOSEADAPTER, &close_adapter);
    }
    close(dxg_fd);
}

static void xv6_d3d12_close_buffer_import(struct wlcomp_buffer *buf)
{
    struct d3dkmt_destroyallocation2 destroy_allocation;
    struct d3dkmt_destroysynchronizationobject destroy_sync;

    if (!buf || buf->d3d12_dxg_fd < 0)
        return;
    if (buf->d3d12_opened_fence != 0) {
        memset(&destroy_sync, 0, sizeof(destroy_sync));
        destroy_sync.sync_object.v = buf->d3d12_opened_fence;
        ioctl(buf->d3d12_dxg_fd, LX_DXDESTROYSYNCHRONIZATIONOBJECT,
              &destroy_sync);
    }
    if (buf->d3d12_opened_resource != 0 && buf->d3d12_device != 0) {
        memset(&destroy_allocation, 0, sizeof(destroy_allocation));
        destroy_allocation.device.v = buf->d3d12_device;
        destroy_allocation.resource.v = buf->d3d12_opened_resource;
        destroy_allocation.flags.assume_not_in_use = 1;
        ioctl(buf->d3d12_dxg_fd, LX_DXDESTROYALLOCATION2,
              &destroy_allocation);
    }
    xv6_d3d12_close_device(buf->d3d12_dxg_fd, buf->d3d12_adapter,
                           buf->d3d12_device);
    buf->d3d12_dxg_fd = -1;
    buf->d3d12_adapter = 0;
    buf->d3d12_device = 0;
    buf->d3d12_opened_resource = 0;
    buf->d3d12_opened_fence = 0;
    buf->d3d12_opened_allocation_count = 0;
    memset(buf->d3d12_opened_allocations, 0,
           sizeof(buf->d3d12_opened_allocations));
    buf->d3d12_fence_cpu_va = 0;
    buf->d3d12_fence_gpu_va = 0;
    buf->d3d12_fence_imported_from_syncfile = 0;
}

static int xv6_d3d12_poll_fence_ready(struct wlcomp_buffer *buf,
                                      uint64_t *current_out)
{
    volatile uint64_t *fence;
    uint64_t current;

    if (current_out)
        *current_out = 0;
    if (!buf || !buf->is_d3d12_shared || !buf->d3d12_fence_value_valid ||
        buf->d3d12_fence_cpu_va == 0)
        return 0;

    fence = (volatile uint64_t *)buf->d3d12_fence_cpu_va;
    current = *fence;
    if (current_out)
        *current_out = current;
    return current >= buf->d3d12_fence_value;
}

static int xv6_d3d12_confirm_fence_wait(struct wlcomp_buffer *buf)
{
    struct d3dkmt_waitforsynchronizationobjectfromcpu wait;
    struct d3dkmthandle objects[1];
    uint64_t values[1];

    if (!buf || buf->d3d12_dxg_fd < 0 || buf->d3d12_device == 0 ||
        buf->d3d12_opened_fence == 0 || !buf->d3d12_fence_value_valid)
        return -1;

    memset(&wait, 0, sizeof(wait));
    memset(objects, 0, sizeof(objects));
    objects[0].v = buf->d3d12_opened_fence;
    values[0] = buf->d3d12_fence_value;
    wait.device.v = buf->d3d12_device;
    wait.object_count = 1;
    wait.objects = (uint64_t)objects;
    wait.fence_values = (uint64_t)values;

    if (ioctl(buf->d3d12_dxg_fd, LX_DXWAITFORSYNCHRONIZATIONOBJECTFROMCPU,
              &wait) != 0)
        return -1;

    buf->d3d12_present_wait_confirmed = 1;
    return 0;
}

static int xv6_d3d12_import_fence_confirmed(struct wlcomp_buffer *buf,
                                            uint64_t *current_out)
{
    uint64_t current = 0;

    if (current_out)
        *current_out = 0;
    if (!buf || !buf->is_d3d12_shared)
        return 0;
    if (!buf->d3d12_fence_value_valid) {
        static int no_fence_logs;

        if (no_fence_logs < 8) {
            fprintf(stderr,
                    "wlcomp: d3d12 native present blocked: missing explicit monitored fence resource=0x%x\n",
                    buf->d3d12_opened_resource);
            no_fence_logs++;
        }
        return 0;
    }
    if (!xv6_d3d12_poll_fence_ready(buf, &current))
        return 0;
    if (current_out)
        *current_out = current;
    if (current >= buf->d3d12_fence_value &&
        !buf->d3d12_present_wait_confirmed) {
        g_d3d12_fence_target_observed_count++;
        fprintf(stderr,
                "wlcomp: d3d12 fence target observed d3d12_fence_target_observed=%lu resource=0x%x fence=0x%x target=%lu current=%lu\n",
                (unsigned long)g_d3d12_fence_target_observed_count,
                buf->d3d12_opened_resource, buf->d3d12_opened_fence,
                (unsigned long)buf->d3d12_fence_value,
                (unsigned long)current);
        xv6_d3d12_write_stage_evidence("fence_target_observed", 0);
    }
    if (!buf->d3d12_present_wait_confirmed &&
        xv6_d3d12_confirm_fence_wait(buf) != 0) {
        static int wait_fail_logs;

        if (wait_fail_logs < 8) {
            fprintf(stderr,
                    "wlcomp: d3d12 native present fence wait failed resource=0x%x fence=0x%x target=%lu current=%lu errno=%d (%s)\n",
                    buf->d3d12_opened_resource, buf->d3d12_opened_fence,
                    (unsigned long)buf->d3d12_fence_value,
                    (unsigned long)current, errno, strerror(errno));
            wait_fail_logs++;
        }
        return 0;
    }
    return 1;
}

struct xv6_d3d12_loader {
    int initialized;
    void *d3d12_mod;
    void *dxcore_mod;
    PFN_D3D12_CREATE_DEVICE create_device;
    HRESULT (WINAPI *create_adapter_factory)(REFIID, void **);
    char missing[160];
};

struct xv6_dxcore_adapter;
struct xv6_dxcore_adapter_factory;

struct xv6_dxcore_adapter_vtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(struct xv6_dxcore_adapter *,
                                                REFIID, void **);
    ULONG (STDMETHODCALLTYPE *AddRef)(struct xv6_dxcore_adapter *);
    ULONG (STDMETHODCALLTYPE *Release)(struct xv6_dxcore_adapter *);
};

struct xv6_dxcore_adapter {
    const struct xv6_dxcore_adapter_vtbl *lpVtbl;
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

static const GUID XV6_IID_IDXCoreAdapterFactory = {
    0x78ee5945, 0xc36e, 0x4b13,
    { 0xa6, 0x69, 0x00, 0x5d, 0xd1, 0x1c, 0x0f, 0x06 }
};

static const GUID XV6_IID_IDXCoreAdapter = {
    0xf0db4c7f, 0xfe5a, 0x42a2,
    { 0xbd, 0x62, 0xf2, 0xa6, 0xcf, 0x6f, 0xc8, 0x3e }
};

static struct xv6_d3d12_loader g_d3d12_loader;

static int xv6_d3d12_present_same_luid(
    const struct xv6_d3d12_present_contract *present)
{
    return present && present->source_luid_valid &&
           (present->source_luid_a != 0 || present->source_luid_b != 0) &&
           present->source_luid_a == present->matched_luid_a &&
           present->source_luid_b == present->matched_luid_b;
}

static void xv6_d3d12_set_missing(const char *text)
{
    if (!text)
        text = "unknown";
    snprintf(g_d3d12_present_missing, sizeof(g_d3d12_present_missing),
             "%s", text);
}

static DXGI_FORMAT xv6_d3d12_expected_resource_format(uint32_t wl_format)
{
    if (wl_format == WL_SHM_FORMAT_ARGB8888)
        return DXGI_FORMAT_B8G8R8A8_UNORM;
    if (wl_format == WL_SHM_FORMAT_XRGB8888)
        return DXGI_FORMAT_B8G8R8X8_UNORM;
    return DXGI_FORMAT_UNKNOWN;
}

static uint32_t xv6_d3d12_present_source_format(uint32_t wl_format)
{
    if (wl_format == WL_SHM_FORMAT_ARGB8888)
        return DRM_FORMAT_ARGB8888;
    if (wl_format == WL_SHM_FORMAT_XRGB8888)
        return DRM_FORMAT_XRGB8888;
    return 0;
}

static void xv6_d3d12_write_stage_evidence(const char *stage, int err)
{
    FILE *fp = fopen(XV6_D3D12_PRESENT_EVIDENCE_PATH, "w");
    uint64_t evidence_time_us = get_time_us();

    if (!fp)
        return;
    if (!stage)
        stage = "unknown";
    g_d3d12_evidence_generation++;
    fprintf(fp,
            "d3d12_present_evidence_time_us=%lu\n"
            "d3d12_evidence_generation=%lu\n"
            "d3d12_evidence_stage=%s\n"
            "d3d12_present_rejected=%d\n"
            "d3d12_native_present_unimplemented=1\n"
            "d3d12_present_path=import-observability\n"
            "d3d12_gpu_present_complete=0\n"
            "d3d12_gpu_present_completes=0\n"
            "d3d12_import_evidence_required=1\n"
            "d3d12_shared_resource_import_required=1\n"
            "d3d12_fence_import_required=1\n"
            "d3d12_display_completion_required=1\n"
            "d3d12_display_completion_correlated=0\n"
            "d3d12_native_present_requirements_satisfied=0\n"
            "d3d12_copy_target=compositor-owned-d3d12-texture\n"
            "d3d12_compositor_target_create_attempts=%lu\n"
            "d3d12_compositor_target_create_successes=%lu\n"
            "d3d12_compositor_gpu_copy_submits=%lu\n"
            "d3d12_compositor_gpu_copy_waits=%lu\n"
            "d3d12_display_handoff_implemented=0\n"
            "d3d12_display_target_kind=none\n"
            "d3d12_d3d12_copy_target_presentable=0\n"
            "d3d12_fb_bo_as_d3d12_heap_supported=0\n"
            "d3d12_fb_bo_as_d3d12_shared_resource_supported=0\n"
            "d3d12_row_major_cpu_visible_present_target_used=0\n"
            "d3d12_readback_present_target_used=0\n"
            "d3d12_display_handoff_requires_kernel_host_protocol=1\n"
            "d3d12_display_target_requires_kernel_host_protocol=1\n"
            "d3d12_dxg_present_source_register_attempts=%lu\n"
            "d3d12_dxg_present_source_register_successes=%lu\n"
            "d3d12_dxg_present_source_register_errno=%d\n"
            "d3d12_dxg_present_source=0x%x\n"
            "d3d12_dxg_present_source_commit_attempts=%lu\n"
            "d3d12_dxg_present_source_commit_successes=%lu\n"
            "d3d12_dxg_present_source_commit_errno=%d\n"
            "d3d12_dxg_present_source_commit_status=%d\n"
            "d3d12_dxg_present_source_commit_expected_eopnotsupp=1\n"
            "d3d12_dxg_present_id=%lu\n"
            "d3d12_dxg_present_completed=%lu\n"
            "d3d12_fb_heap_open_path=not-used\n"
            "d3d12_fb_heap_open_supported=%d\n"
            "d3d12_fb_heap_open_attempts=%lu\n"
            "d3d12_fb_heap_open_successes=%lu\n"
            "d3d12_fb_heap_open_notimpl=%lu\n"
            "d3d12_fb_heap_open_cached_rejects=%lu\n"
            "d3d12_acquire_sync_path=dxg-monitored-fence-cpu-wait\n"
            "d3d12_acquire_sync_d3d12_fence_open_used=0\n"
            "d3d12_acquire_sync_wait_confirmed=0\n"
            "d3d12_gpu_present_rejects=%lu\n"
            "d3d12_context_gpu_present_completes=%lu\n"
            "d3d12_present_errno=%d\n"
            "d3d12_present_missing=%s\n"
            "d3d12_resource_submissions=%lu\n"
            "d3d12_protocol_accepts=%lu\n"
            "d3d12_native_import_attempts=%lu\n"
            "d3d12_native_import_completes=%lu\n"
            "d3d12_resource_import_attempts=%lu\n"
            "d3d12_resource_import_successes=%lu\n"
            "d3d12_fence_import_attempts=%lu\n"
            "d3d12_fence_import_successes=%lu\n"
            "d3d12_syncfile_fence_import_attempts=%lu\n"
            "d3d12_syncfile_fence_import_successes=%lu\n"
            "d3d12_fence_target_observed=%lu\n"
            "d3d12_gpu_copy_composite_starts=%lu\n"
            "d3d12_gpu_copy_composite_completes=%lu\n"
            "d3d12_framebuffer_fallback_used=0\n"
            "d3d12_framebuffer_fallback_count=%lu\n"
            "d3d12_buffer_release_observed=%lu\n"
            "d3d12_buffer_release_resource=0x%x\n"
            "d3d12_buffer_release_present_sequence=%lu\n"
            "d3d12_frame_callback_observed=%lu\n"
            "d3d12_frame_callback_resource=0x%x\n"
            "d3d12_frame_callback_present_sequence=%lu\n"
            "d3d12_cpu_readback=0\n"
            "d3d12_cpu_mapping=0\n"
            "d3d12_cpu_copy=0\n"
            "d3d12_no_readback=1\n",
            (unsigned long)evidence_time_us,
            (unsigned long)g_d3d12_evidence_generation, stage,
            err ? 1 : 0,
            (unsigned long)g_d3d12_compositor_target_create_attempt_count,
            (unsigned long)g_d3d12_compositor_target_create_success_count,
            (unsigned long)g_d3d12_compositor_gpu_copy_submit_count,
            (unsigned long)g_d3d12_compositor_gpu_copy_wait_count,
            (unsigned long)g_d3d12_dxg_present_register_attempt_count,
            (unsigned long)g_d3d12_dxg_present_register_success_count,
            g_d3d12_dxg_present_register_errno,
            g_d3d12_dxg_present_source,
            (unsigned long)g_d3d12_dxg_present_commit_attempt_count,
            (unsigned long)g_d3d12_dxg_present_commit_success_count,
            g_d3d12_dxg_present_commit_errno,
            g_d3d12_dxg_present_commit_errno,
            (unsigned long)g_d3d12_dxg_present_id,
            (unsigned long)g_d3d12_dxg_present_completed,
            0,
            (unsigned long)g_d3d12_fb_heap_open_attempt_count,
            (unsigned long)g_d3d12_fb_heap_open_success_count,
            (unsigned long)g_d3d12_fb_heap_open_notimpl_count,
            (unsigned long)g_d3d12_fb_heap_open_cached_reject_count,
            (unsigned long)g_d3d12_present_reject_count,
            (unsigned long)g_d3d12_present_display_complete_count,
            err, g_d3d12_present_missing[0] ? g_d3d12_present_missing :
            "none",
            (unsigned long)g_d3d12_resource_submit_count,
            (unsigned long)g_d3d12_resource_protocol_accept_count,
            (unsigned long)g_d3d12_native_import_attempt_count,
            (unsigned long)g_d3d12_native_import_complete_count,
            (unsigned long)g_d3d12_resource_import_attempt_count,
            (unsigned long)g_d3d12_resource_import_success_count,
            (unsigned long)g_d3d12_fence_import_attempt_count,
            (unsigned long)g_d3d12_fence_import_success_count,
            (unsigned long)g_d3d12_syncfile_fence_import_attempt_count,
            (unsigned long)g_d3d12_syncfile_fence_import_success_count,
            (unsigned long)g_d3d12_fence_target_observed_count,
            (unsigned long)g_d3d12_present_start_count,
            (unsigned long)g_d3d12_present_copy_complete_count,
            (unsigned long)g_d3d12_framebuffer_fallback_count,
            (unsigned long)g_d3d12_buffer_release_observed_count,
            g_d3d12_buffer_release_resource,
            (unsigned long)g_d3d12_buffer_release_present_sequence,
            (unsigned long)g_d3d12_frame_callback_observed_count,
            g_d3d12_frame_callback_resource,
            (unsigned long)g_d3d12_frame_callback_present_sequence);
    fprintf(fp,
            "d3d12_run_id=%s\n"
            "d3d12_client_pid=%d\n"
            "d3d12_client_buffer_id=%u\n"
            "d3d12_manager_resource_id=%u\n"
            "d3d12_buffer_generation=%lu\n"
            "d3d12_native_present_attempts=%lu\n"
            "d3d12_native_present_completions=0\n"
            "d3d12_context_native_present_completions=%lu\n"
            "d3d12_native_present_attempt_id=%lu\n"
            "d3d12_native_present_completion_id=0\n"
            "d3d12_context_native_present_completion_id=%lu\n"
            "d3d12_framebuffer_blit_attempts=%lu\n"
            "d3d12_framebuffer_blit_successes=%lu\n"
            "d3d12_present_sequence_framebuffer_blit_used=0\n"
            "d3d12_present_sequence_cpu_map_used=0\n"
            "d3d12_present_sequence_cpu_readback_used=0\n"
            "d3d12_present_sequence_cpu_copy_used=0\n"
            "d3d12_present_sequence_software_dri_used=0\n"
            "d3d12_software_dri_present_used=0\n"
            "d3d12_dri_software_present_used=0\n"
            "d3d12_framebuffer_blit_only=0\n"
            "d3d12_fps_evidence_only=0\n",
            xv6_d3d12_present_run_id(), (int)g_d3d12_last_client_pid,
            g_d3d12_last_client_buffer_id,
            g_d3d12_last_manager_resource_id,
            (unsigned long)g_d3d12_last_buffer_generation,
            (unsigned long)g_d3d12_native_present_attempt_count,
            (unsigned long)g_d3d12_native_present_completion_count,
            (unsigned long)g_d3d12_last_native_present_attempt_id,
            (unsigned long)g_d3d12_last_native_present_completion_id,
            (unsigned long)g_fb_blit_present_attempt_count,
            (unsigned long)g_fb_blit_present_success_count);
    fprintf(fp,
            "d3d12_present_identity_compositor_run_id=%s\n"
            "d3d12_present_identity_client_pid=%d\n"
            "d3d12_present_identity_client_buffer_id=%u\n"
            "d3d12_present_identity_manager_resource_id=%u\n"
            "d3d12_present_identity_buffer_generation=%lu\n"
            "d3d12_present_identity_attempt_id=%lu\n"
            "d3d12_present_identity_completion_id=0\n"
            "d3d12_present_identity_current_run_valid=%d\n"
            "d3d12_callback_release_same_frame_required=1\n"
            "d3d12_buffer_release_buffer_generation=%lu\n"
            "d3d12_buffer_release_attempt_id=%lu\n"
            "d3d12_buffer_release_completion_id=%lu\n"
            "d3d12_buffer_release_present_id=%lu\n"
            "d3d12_buffer_release_same_resource=0\n"
            "d3d12_buffer_release_same_generation=0\n"
            "d3d12_buffer_release_same_attempt=0\n"
            "d3d12_buffer_release_same_present_id=0\n"
            "d3d12_frame_callback_buffer_generation=%lu\n"
            "d3d12_frame_callback_attempt_id=%lu\n"
            "d3d12_frame_callback_completion_id=%lu\n"
            "d3d12_frame_callback_present_id=%lu\n"
            "d3d12_frame_callback_same_resource=0\n"
            "d3d12_frame_callback_same_generation=0\n"
            "d3d12_frame_callback_same_attempt=0\n"
            "d3d12_frame_callback_same_present_id=0\n"
            "d3d12_callback_release_same_frame_observed=0\n",
            xv6_d3d12_present_run_id(), (int)g_d3d12_last_client_pid,
            g_d3d12_last_client_buffer_id,
            g_d3d12_last_manager_resource_id,
            (unsigned long)g_d3d12_last_buffer_generation,
            (unsigned long)g_d3d12_last_native_present_attempt_id,
            g_d3d12_last_client_pid != 0 &&
                g_d3d12_last_client_buffer_id != 0 &&
                g_d3d12_last_manager_resource_id != 0 &&
                g_d3d12_last_buffer_generation != 0 &&
                g_d3d12_last_native_present_attempt_id != 0,
            (unsigned long)g_d3d12_buffer_release_buffer_generation,
            (unsigned long)g_d3d12_buffer_release_attempt_id,
            (unsigned long)g_d3d12_buffer_release_completion_id,
            (unsigned long)g_d3d12_buffer_release_present_id,
            (unsigned long)g_d3d12_frame_callback_buffer_generation,
            (unsigned long)g_d3d12_frame_callback_attempt_id,
            (unsigned long)g_d3d12_frame_callback_completion_id,
            (unsigned long)g_d3d12_frame_callback_present_id);
    xv6_d3d12_write_final_handoff_evidence(fp, NULL, NULL, 1);
    xv6_d3d12_write_phase2_negative_evidence(fp);
    fclose(fp);
}

static void xv6_d3d12_observe_buffer_release(struct wlcomp_buffer *buf)
{
    struct xv6_d3d12_present_contract present;
    struct xv6_d3d12_present_result result;
    struct xv6_d3d12_present_source_record *source_record;
    int failclosed_unblock;
    int err;

    if (!buf || !buf->is_d3d12_shared)
        return;
    failclosed_unblock =
        buf->d3d12_failclosed_client_unblocked &&
        !buf->d3d12_present_complete;
    if (!buf->d3d12_present_complete && !failclosed_unblock)
        return;

    source_record = xv6_d3d12_get_present_source_record(buf, 0);
    g_d3d12_buffer_release_observed_count++;
    g_d3d12_buffer_release_resource = buf->d3d12_opened_resource;
    g_d3d12_buffer_release_present_sequence = buf->d3d12_present_sequence;
    g_d3d12_buffer_release_buffer_generation =
        buf->d3d12_buffer_generation;
    g_d3d12_buffer_release_attempt_id =
        buf->d3d12_native_present_attempt_id;
    g_d3d12_buffer_release_completion_id =
        buf->d3d12_native_present_completion_id;
    g_d3d12_buffer_release_present_id =
        source_record ? source_record->present_id : 0;
    memset(&present, 0, sizeof(present));
    memset(&result, 0, sizeof(result));
    present.adapter = buf->d3d12_adapter;
    present.device = buf->d3d12_device;
    present.resource = buf->d3d12_opened_resource;
    present.allocation_count = buf->d3d12_opened_allocation_count;
    present.allocations = buf->d3d12_opened_allocations;
    present.fence = buf->d3d12_opened_fence;
    present.fence_target = buf->d3d12_fence_value;
    present.format = buf->format;
    present.source_luid_a = buf->d3d12_source_luid_a;
    present.source_luid_b = buf->d3d12_source_luid_b;
    present.source_luid_valid = buf->d3d12_source_luid_valid;
    present.matched_luid_a = buf->d3d12_matched_luid_a;
    present.matched_luid_b = buf->d3d12_matched_luid_b;
    present.wait_confirmed = buf->d3d12_present_wait_confirmed;
    present.total_priv_size = buf->d3d12_total_priv_size;
    present.dst.x = buf->d3d12_present_dst_x;
    present.dst.y = buf->d3d12_present_dst_y;
    present.dst.w = buf->d3d12_present_dst_w;
    present.dst.h = buf->d3d12_present_dst_h;
    present.src.x = buf->d3d12_present_src_x;
    present.src.y = buf->d3d12_present_src_y;
    present.src.w = buf->d3d12_present_src_w;
    present.src.h = buf->d3d12_present_src_h;
    xv6_d3d12_fill_present_identity(&present, buf);
    result.start_sequence = buf->d3d12_gpu_present_start_time;
    result.copy_sequence = buf->d3d12_gpu_copy_complete_time;
    result.complete_sequence =
        failclosed_unblock ? 0 : buf->d3d12_present_sequence;
    result.release_fence =
        failclosed_unblock ? 0 : buf->d3d12_present_fence_value;
    if (failclosed_unblock) {
        g_d3d12_failclosed_client_unblock_release_count++;
        err = source_record && source_record->commit_errno ?
            source_record->commit_errno : EOPNOTSUPP;
        xv6_d3d12_write_present_evidence(&present, &result, 1, err);
        fprintf(stderr,
                "wlcomp: d3d12 fail-closed buffer release observed no_native_present_credit=1 releases=%lu resource=0x%x sequence=%lu commit_errno=%d backend_opengl_submit=0\n",
                (unsigned long)g_d3d12_failclosed_client_unblock_release_count,
                buf->d3d12_opened_resource,
                (unsigned long)buf->d3d12_present_sequence, err);
        return;
    }
    g_d3d12_buffer_release_native_success_count++;
    xv6_d3d12_write_present_evidence(&present, &result, 0, 0);
    fprintf(stderr,
            "wlcomp: d3d12 buffer release observed d3d12_buffer_release_observed=%lu resource=0x%x present_sequence=%lu evidence_generation=%lu\n",
            (unsigned long)g_d3d12_buffer_release_observed_count,
            buf->d3d12_opened_resource,
            (unsigned long)buf->d3d12_present_sequence,
            (unsigned long)g_d3d12_evidence_generation);
}

static void xv6_d3d12_observe_frame_callback(struct wlcomp_surface *surf,
                                             uint32_t now)
{
    struct xv6_d3d12_present_contract present;
    struct xv6_d3d12_present_result result;
    struct xv6_d3d12_present_source_record *source_record;
    struct wlcomp_buffer *buf;
    int failclosed_unblock;
    int err;

    if (!surf)
        return;
    buf = surf->committed_buf;
    if (!buf || !buf->is_d3d12_shared)
        return;
    failclosed_unblock =
        buf->d3d12_failclosed_client_unblocked &&
        !buf->d3d12_present_complete;
    if (!buf->d3d12_present_complete && !failclosed_unblock)
        return;

    source_record = xv6_d3d12_get_present_source_record(buf, 0);
    g_d3d12_frame_callback_observed_count++;
    g_d3d12_frame_callback_resource = buf->d3d12_opened_resource;
    g_d3d12_frame_callback_present_sequence = buf->d3d12_present_sequence;
    g_d3d12_frame_callback_buffer_generation =
        buf->d3d12_buffer_generation;
    g_d3d12_frame_callback_attempt_id =
        buf->d3d12_native_present_attempt_id;
    g_d3d12_frame_callback_completion_id =
        buf->d3d12_native_present_completion_id;
    g_d3d12_frame_callback_present_id =
        source_record ? source_record->present_id : 0;
    memset(&present, 0, sizeof(present));
    memset(&result, 0, sizeof(result));
    present.adapter = buf->d3d12_adapter;
    present.device = buf->d3d12_device;
    present.resource = buf->d3d12_opened_resource;
    present.allocation_count = buf->d3d12_opened_allocation_count;
    present.allocations = buf->d3d12_opened_allocations;
    present.fence = buf->d3d12_opened_fence;
    present.fence_target = buf->d3d12_fence_value;
    present.format = buf->format;
    present.source_luid_a = buf->d3d12_source_luid_a;
    present.source_luid_b = buf->d3d12_source_luid_b;
    present.source_luid_valid = buf->d3d12_source_luid_valid;
    present.matched_luid_a = buf->d3d12_matched_luid_a;
    present.matched_luid_b = buf->d3d12_matched_luid_b;
    present.wait_confirmed = buf->d3d12_present_wait_confirmed;
    present.total_priv_size = buf->d3d12_total_priv_size;
    present.dst.x = buf->d3d12_present_dst_x;
    present.dst.y = buf->d3d12_present_dst_y;
    present.dst.w = buf->d3d12_present_dst_w;
    present.dst.h = buf->d3d12_present_dst_h;
    present.src.x = buf->d3d12_present_src_x;
    present.src.y = buf->d3d12_present_src_y;
    present.src.w = buf->d3d12_present_src_w;
    present.src.h = buf->d3d12_present_src_h;
    xv6_d3d12_fill_present_identity(&present, buf);
    result.start_sequence = buf->d3d12_gpu_present_start_time;
    result.copy_sequence = buf->d3d12_gpu_copy_complete_time;
    result.complete_sequence =
        failclosed_unblock ? 0 : buf->d3d12_present_sequence;
    result.release_fence =
        failclosed_unblock ? 0 : buf->d3d12_present_fence_value;
    if (failclosed_unblock) {
        g_d3d12_failclosed_client_unblock_callback_count++;
        err = source_record && source_record->commit_errno ?
            source_record->commit_errno : EOPNOTSUPP;
        xv6_d3d12_write_present_evidence(&present, &result, 1, err);
        fprintf(stderr,
                "wlcomp: d3d12 fail-closed frame callback observed no_native_present_credit=1 callbacks=%lu resource=0x%x sequence=%lu callback_time=%u commit_errno=%d backend_opengl_submit=0\n",
                (unsigned long)g_d3d12_failclosed_client_unblock_callback_count,
                buf->d3d12_opened_resource,
                (unsigned long)buf->d3d12_present_sequence, now, err);
        return;
    }
    g_d3d12_frame_callback_native_success_count++;
    xv6_d3d12_write_present_evidence(&present, &result, 0, 0);
    fprintf(stderr,
            "wlcomp: d3d12 frame callback observed d3d12_frame_callback_observed=%lu resource=0x%x present_sequence=%lu callback_time=%u evidence_generation=%lu\n",
            (unsigned long)g_d3d12_frame_callback_observed_count,
            buf->d3d12_opened_resource,
            (unsigned long)buf->d3d12_present_sequence, now,
            (unsigned long)g_d3d12_evidence_generation);
}

static void xv6_d3d12_drain_failclosed_client(struct wlcomp_buffer *buf,
                                              const char *reason)
{
    struct wlcomp_surface *surf;
    uint32_t now;
    int pending_releases_before = 0;
    int pending_callbacks_before = 0;
    int pending_releases_after = 0;
    int pending_callbacks_after = 0;
    int releases_queued = 0;
    int callbacks_sent = 0;

    if (!buf || !buf->is_d3d12_shared ||
        !buf->d3d12_failclosed_client_unblocked ||
        buf->d3d12_present_complete)
        return;

    now = get_time_ms();
    wl_list_for_each(surf, &g_surfaces, link) {
        struct wlcomp_frame_callback *cb;

        if (surf->committed_buf != buf)
            continue;
        if (!surf->buffer_released && buf->acquire_fence_fd < 0)
            pending_releases_before++;
        wl_list_for_each(cb, &surf->frame_callbacks, link)
            pending_callbacks_before++;
    }
    wl_list_for_each(surf, &g_surfaces, link) {
        if (surf->committed_buf != buf)
            continue;
        if (!surf->buffer_released && buf->acquire_fence_fd < 0) {
            queue_buffer_release(buf);
            surf->buffer_released = 1;
            releases_queued++;
        }
        {
            struct wlcomp_frame_callback *cb;
            struct wlcomp_frame_callback *tmp;

            wl_list_for_each_safe(cb, tmp, &surf->frame_callbacks, link) {
                xv6_d3d12_observe_frame_callback(surf, now);
                wl_callback_send_done(cb->resource, now);
                wl_resource_destroy(cb->resource);
                wl_list_remove(&cb->link);
                free(cb);
                callbacks_sent++;
            }
        }
    }
    flush_buffer_releases_after_present();
    wl_list_for_each(surf, &g_surfaces, link) {
        struct wlcomp_frame_callback *cb;

        if (surf->committed_buf != buf)
            continue;
        if (!surf->buffer_released && buf->acquire_fence_fd < 0)
            pending_releases_after++;
        wl_list_for_each(cb, &surf->frame_callbacks, link)
            pending_callbacks_after++;
    }
    if (g_display)
        wl_display_flush_clients(g_display);
    fprintf(stderr,
            "wlcomp: d3d12 fail-closed client drain no_native_present_credit=1 reason=%s resource=0x%x releases_queued=%d callbacks_sent=%d backend_opengl_submit=0\n",
            reason ? reason : "unknown", buf->d3d12_opened_resource,
            releases_queued, callbacks_sent);
    fprintf(stderr,
            "wlcomp: d3d12_failclosed_lifecycle_matrix reason=%s resource=0x%x releases_before=%d callbacks_before=%d releases_queued=%d callbacks_sent=%d pending_release_after=%d pending_callbacks_after=%d native_present_claim=0 opengl_submit_credit=0 status=%s\n",
            reason ? reason : "unknown", buf->d3d12_opened_resource,
            pending_releases_before, pending_callbacks_before,
            releases_queued, callbacks_sent, pending_releases_after,
            pending_callbacks_after,
            pending_releases_after == 0 && pending_callbacks_after == 0 ?
            "PASS" : "FAIL");
}

static int xv6_d3d12_load_runtime(void)
{
    if (g_d3d12_loader.initialized)
        return g_d3d12_loader.create_device != NULL ? 0 : -1;

    g_d3d12_loader.initialized = 1;
    g_d3d12_loader.d3d12_mod = dlopen("libd3d12.so", RTLD_NOW | RTLD_LOCAL);
    if (!g_d3d12_loader.d3d12_mod)
        g_d3d12_loader.d3d12_mod =
            dlopen("/lib/libd3d12.so", RTLD_NOW | RTLD_LOCAL);
    if (!g_d3d12_loader.d3d12_mod) {
        const char *err = dlerror();
        snprintf(g_d3d12_loader.missing, sizeof(g_d3d12_loader.missing),
                 "dlopen(libd3d12.so): %s", err ? err : "unknown");
        xv6_d3d12_set_missing(g_d3d12_loader.missing);
        return -1;
    }

    g_d3d12_loader.create_device =
        (PFN_D3D12_CREATE_DEVICE)dlsym(g_d3d12_loader.d3d12_mod,
                                       "D3D12CreateDevice");
    if (!g_d3d12_loader.create_device) {
        const char *err = dlerror();
        snprintf(g_d3d12_loader.missing, sizeof(g_d3d12_loader.missing),
                 "dlsym(D3D12CreateDevice): %s", err ? err : "unknown");
        xv6_d3d12_set_missing(g_d3d12_loader.missing);
        return -1;
    }

    /*
     * DXCore's public header is C++-only in this tree, so the C compositor uses
     * a tiny COM vtable subset above to select the same adapter LUID as DXG.
     */
    g_d3d12_loader.dxcore_mod = dlopen("libdxcore.so", RTLD_NOW | RTLD_LOCAL);
    if (!g_d3d12_loader.dxcore_mod)
        g_d3d12_loader.dxcore_mod =
            dlopen("/lib/libdxcore.so", RTLD_NOW | RTLD_LOCAL);
    if (g_d3d12_loader.dxcore_mod) {
        g_d3d12_loader.create_adapter_factory =
            (HRESULT (WINAPI *)(REFIID, void **))dlsym(
                g_d3d12_loader.dxcore_mod, "DXCoreCreateAdapterFactory");
    }
    return 0;
}

static int xv6_d3d12_create_device_for_luid(uint32_t luid_a,
                                            uint32_t luid_b,
                                            ID3D12Device **device_out)
{
    struct xv6_dxcore_adapter_factory *factory = NULL;
    struct xv6_dxcore_adapter *adapter = NULL;
    struct winluid requested_luid;
    LUID luid;
    char luid_text[32];
    HRESULT hr;
    int ret = -1;

    if (device_out)
        *device_out = NULL;
    if (!device_out) {
        errno = EINVAL;
        xv6_d3d12_set_missing("invalid D3D12 device output");
        return -1;
    }
    if (xv6_d3d12_load_runtime() != 0) {
        errno = ENOSYS;
        return -1;
    }
    if (!(luid_a || luid_b)) {
        xv6_d3d12_set_missing(
            "missing LUID for shared-resource D3D12 device selection");
        errno = ENODEV;
        return -1;
    }

    requested_luid.a = luid_a;
    requested_luid.b = luid_b;
    xv6_format_winluid_text(luid_text, sizeof(luid_text), requested_luid);
    if (g_d3d12_loader.create_adapter_factory) {
        memset(&luid, 0, sizeof(luid));
        luid.LowPart = luid_a;
        luid.HighPart = (LONG)luid_b;
        hr = g_d3d12_loader.create_adapter_factory(
            &XV6_IID_IDXCoreAdapterFactory, (void **)&factory);
        if (SUCCEEDED(hr) && factory) {
            hr = factory->lpVtbl->GetAdapterByLuid(
                factory, &luid, &XV6_IID_IDXCoreAdapter, (void **)&adapter);
            if (SUCCEEDED(hr) && adapter) {
                hr = g_d3d12_loader.create_device(
                    (IUnknown *)adapter, D3D_FEATURE_LEVEL_11_0,
                    &IID_ID3D12Device, (void **)device_out);
                if (SUCCEEDED(hr) && *device_out) {
                    ret = 0;
                    goto out;
                }
                snprintf(g_d3d12_present_missing,
                         sizeof(g_d3d12_present_missing),
                         "D3D12CreateDevice(dxcore luid=%s low=0x%08x high=0x%08x) failed hr=0x%lx",
                         luid_text, luid_a, luid_b, (unsigned long)hr);
            } else {
                snprintf(g_d3d12_present_missing,
                         sizeof(g_d3d12_present_missing),
                         "DXCore GetAdapterByLuid luid=%s low=0x%08x high=0x%08x failed hr=0x%lx adapter=%p",
                         luid_text, luid_a, luid_b, (unsigned long)hr,
                         (void *)adapter);
            }
        } else {
            snprintf(g_d3d12_present_missing,
                     sizeof(g_d3d12_present_missing),
                     "DXCoreCreateAdapterFactory for luid=%s failed hr=0x%lx",
                     luid_text, (unsigned long)hr);
        }
    } else {
        snprintf(g_d3d12_present_missing,
                 sizeof(g_d3d12_present_missing),
                 "DXCoreCreateAdapterFactory unavailable for LUID-selected D3D12 device luid=%s low=0x%08x high=0x%08x",
                 luid_text, luid_a, luid_b);
    }

    errno = ENODEV;

out:
    if (adapter)
        adapter->lpVtbl->Release(adapter);
    if (factory)
        factory->lpVtbl->Release(factory);
    return ret;
}

static int xv6_d3d12_wait_fence_object(ID3D12Fence *fence,
                                       uint64_t target,
                                       const char *label)
{
    uint32_t start = get_time_ms();

    while (get_time_ms() - start < 5000) {
        uint64_t completed;

        completed = ID3D12Fence_GetCompletedValue(fence);
        if (completed >= target)
            return 0;
        usleep(1000);
    }
    snprintf(g_d3d12_present_missing, sizeof(g_d3d12_present_missing),
             "%s fence wait timeout target=%lu", label,
             (unsigned long)target);
    errno = ETIMEDOUT;
    return -1;
}

static int xv6_d3d12_validate_shared_resource_desc(struct wlcomp_buffer *buf)
{
    ID3D12Device *device = NULL;
    ID3D12Resource *resource = NULL;
    D3D12_RESOURCE_DESC desc;
    DXGI_FORMAT expected_format;
    LUID device_luid;
    HRESULT hr;
    int ret = -1;

    if (!buf || !buf->is_d3d12_shared || buf->d3d12_resource_fd < 0) {
        errno = EINVAL;
        xv6_d3d12_set_missing("invalid D3D12 resource metadata validation input");
        return -1;
    }
    if (!buf->d3d12_source_luid_valid ||
        (buf->d3d12_source_luid_a == 0 && buf->d3d12_source_luid_b == 0)) {
        errno = EINVAL;
        xv6_d3d12_set_missing(
            "D3D12 shared-resource metadata validation requires nonzero source LUID");
        return -1;
    }
    if (buf->d3d12_matched_luid_a != buf->d3d12_source_luid_a ||
        buf->d3d12_matched_luid_b != buf->d3d12_source_luid_b) {
        errno = ENODEV;
        xv6_d3d12_note_phase2_negative(XV6_D3D12_NEG_BAD_LUID);
        xv6_d3d12_set_missing(
            "D3D12 shared-resource metadata validation found adapter LUID mismatch");
        return -1;
    }
    if (buf->width <= 0 || buf->height <= 0 || buf->stride != buf->width * 4) {
        errno = EINVAL;
        xv6_d3d12_note_phase2_negative(XV6_D3D12_NEG_WRONG_DIMENSIONS);
        xv6_d3d12_set_missing(
            "D3D12 shared-resource metadata validation found invalid dimensions");
        return -1;
    }
    if (buf->d3d12_fence_fd < 0 || buf->d3d12_opened_fence == 0 ||
        buf->d3d12_fence_cpu_va == 0 || buf->d3d12_fence_gpu_va == 0 ||
        !buf->d3d12_fence_value_valid ||
        buf->d3d12_fence_value == 0) {
        errno = EINVAL;
        if (buf->d3d12_fence_fd < 0 || buf->d3d12_opened_fence == 0)
            xv6_d3d12_note_phase2_negative(
                XV6_D3D12_NEG_MISSING_FENCE_FD);
        else
            xv6_d3d12_note_phase2_negative(XV6_D3D12_NEG_STALE_FENCE);
        xv6_d3d12_set_missing(
            "D3D12 shared-resource metadata validation requires imported monitored fence CPU/GPU mappings and nonzero target");
        return -1;
    }

    expected_format = xv6_d3d12_expected_resource_format(buf->format);
    if (expected_format == DXGI_FORMAT_UNKNOWN) {
        errno = EINVAL;
        xv6_d3d12_note_phase2_negative(XV6_D3D12_NEG_WRONG_FORMAT);
        xv6_d3d12_set_missing(
            "D3D12 shared-resource metadata validation found unsupported wl_buffer format");
        return -1;
    }
    if (xv6_d3d12_create_device_for_luid(buf->d3d12_source_luid_a,
                                         buf->d3d12_source_luid_b,
                                         &device) != 0)
        return -1;

    device_luid = ID3D12Device_GetAdapterLuid(device);
    if ((uint32_t)device_luid.LowPart != buf->d3d12_source_luid_a ||
        (uint32_t)device_luid.HighPart != buf->d3d12_source_luid_b) {
        struct winluid device_winluid;
        struct winluid source_winluid;
        char device_luid_text[32];
        char source_luid_text[32];

        device_winluid.a = (uint32_t)device_luid.LowPart;
        device_winluid.b = (uint32_t)device_luid.HighPart;
        source_winluid.a = buf->d3d12_source_luid_a;
        source_winluid.b = buf->d3d12_source_luid_b;
        xv6_format_winluid_text(device_luid_text,
                                sizeof(device_luid_text), device_winluid);
        xv6_format_winluid_text(source_luid_text,
                                sizeof(source_luid_text), source_winluid);
        snprintf(g_d3d12_present_missing, sizeof(g_d3d12_present_missing),
                 "D3D12 metadata validation device LUID mismatch device=%s source=%s",
                 device_luid_text, source_luid_text);
        xv6_d3d12_note_phase2_negative(XV6_D3D12_NEG_BAD_LUID);
        errno = ENODEV;
        goto out;
    }

    hr = ID3D12Device_OpenSharedHandle(
        device, (HANDLE)(intptr_t)buf->d3d12_resource_fd,
        &IID_ID3D12Resource, (void **)&resource);
    if (FAILED(hr) || !resource) {
        snprintf(g_d3d12_present_missing, sizeof(g_d3d12_present_missing),
                 "D3D12 metadata validation OpenSharedHandle(resource fd=%d) failed hr=0x%lx",
                 buf->d3d12_resource_fd, (unsigned long)hr);
        xv6_d3d12_note_phase2_negative(
            XV6_D3D12_NEG_MISSING_RESOURCE_FD);
        errno = EIO;
        goto out;
    }

    desc = ID3D12Resource_GetDesc(resource);
    if (desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
        desc.Width != (UINT64)buf->width ||
        desc.Height != (UINT)buf->height ||
        desc.DepthOrArraySize != 1 ||
        desc.MipLevels != 1 ||
        desc.SampleDesc.Count != 1 ||
        desc.SampleDesc.Quality != 0 ||
        desc.Format != expected_format) {
        const char *field = "unknown";

        if (desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D)
            field = "dimension";
        else if (desc.Width != (UINT64)buf->width)
            field = "width";
        else if (desc.Height != (UINT)buf->height)
            field = "height";
        else if (desc.DepthOrArraySize != 1)
            field = "depth";
        else if (desc.MipLevels != 1)
            field = "mip_levels";
        else if (desc.SampleDesc.Count != 1)
            field = "sample_count";
        else if (desc.SampleDesc.Quality != 0)
            field = "sample_quality";
        else if (desc.Format != expected_format)
            field = "format";
        if (strcmp(field, "format") == 0)
            xv6_d3d12_note_phase2_negative(XV6_D3D12_NEG_WRONG_FORMAT);
        else
            xv6_d3d12_note_phase2_negative(
                XV6_D3D12_NEG_WRONG_DIMENSIONS);
        snprintf(g_d3d12_present_missing, sizeof(g_d3d12_present_missing),
                 "D3D12 metadata mismatch field=%s dim=%u size=%lux%u expected=%dx%d depth=%u mips=%u samples=%u quality=%u wl_format=0x%x dxgi_format=%u expected_dxgi_format=%u",
                 field,
                 desc.Dimension, (unsigned long)desc.Width, desc.Height,
                 buf->width, buf->height, desc.DepthOrArraySize,
                 desc.MipLevels, desc.SampleDesc.Count,
                 desc.SampleDesc.Quality, buf->format, desc.Format,
                 expected_format);
        errno = EINVAL;
        goto out;
    }

    fprintf(stderr,
            "wlcomp: d3d12 native import metadata ok resource=0x%x fd=%d size=%dx%d format=0x%x dxgi_format=%u fence=0x%x target=%lu source_luid=%08x:%08x\n",
            buf->d3d12_opened_resource, buf->d3d12_resource_fd, buf->width,
            buf->height, buf->format, desc.Format, buf->d3d12_opened_fence,
            (unsigned long)buf->d3d12_fence_value,
            buf->d3d12_source_luid_b, buf->d3d12_source_luid_a);
    ret = 0;

out:
    if (resource)
        ID3D12Resource_Release(resource);
    if (device)
        ID3D12Device_Release(device);
    return ret;
}

static int xv6_d3d12_copy_shared_resource_to_compositor_texture(
    struct wlcomp_buffer *buf,
    const struct xv6_d3d12_present_contract *present,
    uint64_t *copy_fence_out)
{
    ID3D12Device *device = NULL;
    ID3D12CommandQueue *queue = NULL;
    ID3D12CommandAllocator *allocator = NULL;
    ID3D12GraphicsCommandList *cmdlist = NULL;
    ID3D12Resource *src_resource = NULL;
    ID3D12Resource *target_resource = NULL;
    ID3D12Fence *copy_fence = NULL;
    D3D12_HEAP_PROPERTIES heap_props;
    D3D12_COMMAND_QUEUE_DESC queue_desc;
    D3D12_RESOURCE_DESC src_desc;
    D3D12_RESOURCE_DESC target_desc;
    D3D12_TEXTURE_COPY_LOCATION dst_loc;
    D3D12_TEXTURE_COPY_LOCATION src_loc;
    D3D12_RESOURCE_BARRIER barriers[2];
    ID3D12CommandList *lists[1];
    D3D12_BOX src_box;
    LUID device_luid;
    HRESULT hr;
    uint64_t copy_fence_value = 1;
    uint32_t expected_luid_a = 0;
    uint32_t expected_luid_b = 0;
    int ret = -1;

    if (copy_fence_out)
        *copy_fence_out = 0;
    if (!buf || !present) {
        errno = EINVAL;
        xv6_d3d12_set_missing("invalid D3D12 present input");
        return -1;
    }
    if (buf->d3d12_resource_fd < 0 || buf->d3d12_fence_fd < 0 ||
        !buf->d3d12_fence_value_valid) {
        errno = EINVAL;
        xv6_d3d12_set_missing(
            "missing shared D3D12 resource fd, acquire sync fd, or explicit acquire sync target value");
        return -1;
    }
    if (buf->d3d12_opened_fence == 0 || buf->d3d12_fence_cpu_va == 0 ||
        buf->d3d12_fence_gpu_va == 0 || !present->wait_confirmed) {
        errno = EPROTO;
        xv6_d3d12_set_missing(
            "D3D12 copy requires compositor-imported dxg monitored acquire sync and confirmed target wait");
        return -1;
    }
    if (buf->y_inverted) {
        errno = ENOSYS;
        xv6_d3d12_set_missing("missing D3D12 y-inverted copy metadata/path");
        return -1;
    }
    if (buf->d3d12_source_luid_valid) {
        expected_luid_a = buf->d3d12_source_luid_a;
        expected_luid_b = buf->d3d12_source_luid_b;
    } else {
        expected_luid_a = buf->d3d12_matched_luid_a;
        expected_luid_b = buf->d3d12_matched_luid_b;
    }
    if (xv6_d3d12_create_device_for_luid(expected_luid_a, expected_luid_b,
                                         &device) != 0)
        goto out;

    device_luid = ID3D12Device_GetAdapterLuid(device);
    if ((expected_luid_a || expected_luid_b) &&
        ((uint32_t)device_luid.LowPart != expected_luid_a ||
         (uint32_t)device_luid.HighPart != expected_luid_b)) {
        struct winluid device_winluid;
        struct winluid expected_winluid;
        char device_luid_text[32];
        char expected_luid_text[32];

        device_winluid.a = (uint32_t)device_luid.LowPart;
        device_winluid.b = (uint32_t)device_luid.HighPart;
        expected_winluid.a = expected_luid_a;
        expected_winluid.b = expected_luid_b;
        xv6_format_winluid_text(device_luid_text,
                                sizeof(device_luid_text), device_winluid);
        xv6_format_winluid_text(expected_luid_text,
                                sizeof(expected_luid_text),
                                expected_winluid);
        snprintf(g_d3d12_present_missing, sizeof(g_d3d12_present_missing),
                 "D3D12 device LUID mismatch device=%s expected=%s",
                 device_luid_text, expected_luid_text);
        errno = ENODEV;
        goto out;
    }

    hr = ID3D12Device_OpenSharedHandle(
        device, (HANDLE)(intptr_t)buf->d3d12_resource_fd,
        &IID_ID3D12Resource, (void **)&src_resource);
    if (FAILED(hr) || !src_resource) {
        snprintf(g_d3d12_present_missing, sizeof(g_d3d12_present_missing),
                 "ID3D12Device::OpenSharedHandle(resource fd=%d) failed hr=0x%lx",
                 buf->d3d12_resource_fd, (unsigned long)hr);
        errno = EIO;
        goto out;
    }

    fprintf(stderr,
            "wlcomp: d3d12 acquire sync satisfied through dxg monitored fence d3d12_acquire_sync_path=dxg-monitored-fence-cpu-wait d3d12_acquire_sync_d3d12_fence_open_used=0 fd=%d fence=0x%x target=%lu current=%lu wait_confirmed=%d\n",
            buf->d3d12_fence_fd, buf->d3d12_opened_fence,
            (unsigned long)present->fence_target,
            (unsigned long)present->fence_current, present->wait_confirmed);

    src_desc = ID3D12Resource_GetDesc(src_resource);
    if (present->src.x < 0 || present->src.y < 0 ||
        src_desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
        src_desc.Width < (UINT64)(present->src.x + (int32_t)present->src.w) ||
        src_desc.Height < (UINT)(present->src.y + present->src.h) ||
        src_desc.DepthOrArraySize != 1 || src_desc.MipLevels < 1 ||
        src_desc.SampleDesc.Count != 1 ||
        (src_desc.Format != DXGI_FORMAT_B8G8R8A8_UNORM &&
         src_desc.Format != DXGI_FORMAT_B8G8R8X8_UNORM)) {
        snprintf(g_d3d12_present_missing, sizeof(g_d3d12_present_missing),
                 "unsupported source D3D12 texture metadata dim=%u size=%lux%u depth=%u mips=%u samples=%u format=%u",
                 src_desc.Dimension, (unsigned long)src_desc.Width,
                 src_desc.Height, src_desc.DepthOrArraySize,
                 src_desc.MipLevels, src_desc.SampleDesc.Count,
                 src_desc.Format);
        errno = EINVAL;
        goto out;
    }
    if (present->dst.x < 0 || present->dst.y < 0 ||
        present->dst.x + (int32_t)present->dst.w > (int32_t)g_fb_w ||
        present->dst.y + (int32_t)present->dst.h > (int32_t)g_fb_h) {
        errno = EINVAL;
        xv6_d3d12_set_missing("D3D12 destination rectangle is outside framebuffer");
        goto out;
    }

    memset(&heap_props, 0, sizeof(heap_props));
    heap_props.Type = D3D12_HEAP_TYPE_DEFAULT;
    memset(&target_desc, 0, sizeof(target_desc));
    target_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    target_desc.Width = present->src.w;
    target_desc.Height = present->src.h;
    target_desc.DepthOrArraySize = 1;
    target_desc.MipLevels = 1;
    target_desc.Format = src_desc.Format;
    target_desc.SampleDesc.Count = 1;
    target_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

    g_d3d12_compositor_target_create_attempt_count++;
    hr = ID3D12Device_CreateCommittedResource(
        device, &heap_props, D3D12_HEAP_FLAG_NONE, &target_desc,
        D3D12_RESOURCE_STATE_COMMON, NULL, &IID_ID3D12Resource,
        (void **)&target_resource);
    if (FAILED(hr) || !target_resource) {
        snprintf(g_d3d12_present_missing, sizeof(g_d3d12_present_missing),
                 "CreateCommittedResource(compositor-owned D3D12 copy target %ux%u format=%u) failed hr=0x%lx",
                 present->src.w, present->src.h, src_desc.Format,
                 (unsigned long)hr);
        errno = EIO;
        goto out;
    }
    g_d3d12_compositor_target_create_success_count++;

    memset(&queue_desc, 0, sizeof(queue_desc));
    queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    queue_desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
    hr = ID3D12Device_CreateCommandQueue(device, &queue_desc,
                                         &IID_ID3D12CommandQueue,
                                         (void **)&queue);
    if (FAILED(hr) || !queue) {
        snprintf(g_d3d12_present_missing, sizeof(g_d3d12_present_missing),
                 "CreateCommandQueue failed hr=0x%lx", (unsigned long)hr);
        errno = EIO;
        goto out;
    }
    g_d3d12_compositor_copy_device_ptr = (uint64_t)(uintptr_t)device;
    g_d3d12_compositor_copy_queue_ptr = (uint64_t)(uintptr_t)queue;
    hr = ID3D12Device_CreateCommandAllocator(
        device, D3D12_COMMAND_LIST_TYPE_DIRECT,
        &IID_ID3D12CommandAllocator, (void **)&allocator);
    if (FAILED(hr) || !allocator) {
        snprintf(g_d3d12_present_missing, sizeof(g_d3d12_present_missing),
                 "CreateCommandAllocator failed hr=0x%lx",
                 (unsigned long)hr);
        errno = EIO;
        goto out;
    }
    hr = ID3D12Device_CreateCommandList(
        device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, NULL,
        &IID_ID3D12GraphicsCommandList, (void **)&cmdlist);
    if (FAILED(hr) || !cmdlist) {
        snprintf(g_d3d12_present_missing, sizeof(g_d3d12_present_missing),
                 "CreateCommandList failed hr=0x%lx", (unsigned long)hr);
        errno = EIO;
        goto out;
    }
    g_d3d12_compositor_copy_cmdlist_ptr = (uint64_t)(uintptr_t)cmdlist;

    memset(barriers, 0, sizeof(barriers));
    barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[0].Transition.pResource = src_resource;
    barriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
    barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[1].Transition.pResource = target_resource;
    barriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
    barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
    ID3D12GraphicsCommandList_ResourceBarrier(cmdlist, 2, barriers);

    memset(&dst_loc, 0, sizeof(dst_loc));
    dst_loc.pResource = target_resource;
    dst_loc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst_loc.SubresourceIndex = 0;
    memset(&src_loc, 0, sizeof(src_loc));
    src_loc.pResource = src_resource;
    src_loc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    src_loc.SubresourceIndex = 0;
    memset(&src_box, 0, sizeof(src_box));
    src_box.left = (UINT)present->src.x;
    src_box.top = (UINT)present->src.y;
    src_box.front = 0;
    src_box.right = (UINT)(present->src.x + present->src.w);
    src_box.bottom = (UINT)(present->src.y + present->src.h);
    src_box.back = 1;
    ID3D12GraphicsCommandList_CopyTextureRegion(
        cmdlist, &dst_loc, 0, 0, 0, &src_loc, &src_box);

    barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
    barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
    barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
    ID3D12GraphicsCommandList_ResourceBarrier(cmdlist, 2, barriers);

    hr = ID3D12GraphicsCommandList_Close(cmdlist);
    if (FAILED(hr)) {
        snprintf(g_d3d12_present_missing, sizeof(g_d3d12_present_missing),
                 "Close D3D12 copy command list failed hr=0x%lx",
                 (unsigned long)hr);
        errno = EIO;
        goto out;
    }
    lists[0] = (ID3D12CommandList *)cmdlist;
    ID3D12CommandQueue_ExecuteCommandLists(queue, 1, lists);
    g_d3d12_compositor_gpu_copy_submit_count++;

    hr = ID3D12Device_CreateFence(device, 0, D3D12_FENCE_FLAG_NONE,
                                  &IID_ID3D12Fence, (void **)&copy_fence);
    if (FAILED(hr) || !copy_fence) {
        snprintf(g_d3d12_present_missing, sizeof(g_d3d12_present_missing),
                 "CreateFence(copy completion) failed hr=0x%lx",
                 (unsigned long)hr);
        errno = EIO;
        goto out;
    }
    hr = ID3D12CommandQueue_Signal(queue, copy_fence, copy_fence_value);
    if (FAILED(hr)) {
        snprintf(g_d3d12_present_missing, sizeof(g_d3d12_present_missing),
                 "Signal(copy completion fence) failed hr=0x%lx",
                 (unsigned long)hr);
        errno = EIO;
        goto out;
    }
    if (xv6_d3d12_wait_fence_object(copy_fence, copy_fence_value,
                                    "copy") != 0)
        goto out;
    g_d3d12_compositor_gpu_copy_wait_count++;

    if (copy_fence_out)
        *copy_fence_out = copy_fence_value;
    fprintf(stderr,
            "wlcomp: d3d12 compositor GPU copy proof complete target=compositor-owned-d3d12-texture target_size=%ux%u format=%u copy_fence=%lu submits=%lu waits=%lu queue_type=direct device=0x%lx queue=0x%lx cmdlist=0x%lx display_handoff=0 no_cpu_readback=1\n",
            present->src.w, present->src.h, src_desc.Format,
            (unsigned long)copy_fence_value,
            (unsigned long)g_d3d12_compositor_gpu_copy_submit_count,
            (unsigned long)g_d3d12_compositor_gpu_copy_wait_count,
            (unsigned long)g_d3d12_compositor_copy_device_ptr,
            (unsigned long)g_d3d12_compositor_copy_queue_ptr,
            (unsigned long)g_d3d12_compositor_copy_cmdlist_ptr);
    ret = 0;

out:
    if (copy_fence)
        ID3D12Fence_Release(copy_fence);
    if (target_resource)
        ID3D12Resource_Release(target_resource);
    if (src_resource)
        ID3D12Resource_Release(src_resource);
    if (cmdlist)
        ID3D12GraphicsCommandList_Release(cmdlist);
    if (allocator)
        ID3D12CommandAllocator_Release(allocator);
    if (queue)
        ID3D12CommandQueue_Release(queue);
    if (device)
        ID3D12Device_Release(device);
    return ret;
}

static void xv6_d3d12_write_present_evidence(
    const struct xv6_d3d12_present_contract *present,
    const struct xv6_d3d12_present_result *result,
    int rejected,
    int err)
{
    FILE *fp = fopen(XV6_D3D12_PRESENT_EVIDENCE_PATH, "w");
    uint64_t evidence_time_us = get_time_us();
    int same_luid = xv6_d3d12_present_same_luid(present);
    const char *run_id = xv6_d3d12_present_run_id();
    int present_identity_valid = present && run_id && *run_id &&
        present->client_pid != 0 && present->client_buffer_id != 0 &&
        present->manager_resource_id != 0 &&
        present->buffer_generation != 0 &&
        present->native_present_attempt_id != 0 &&
        (rejected || present->native_present_completion_id != 0);
    int release_same_resource = present &&
        g_d3d12_buffer_release_observed_count != 0 &&
        g_d3d12_buffer_release_resource == present->resource;
    int release_same_generation = present &&
        g_d3d12_buffer_release_observed_count != 0 &&
        g_d3d12_buffer_release_buffer_generation ==
            present->buffer_generation;
    int release_same_attempt = present &&
        g_d3d12_buffer_release_observed_count != 0 &&
        g_d3d12_buffer_release_attempt_id ==
            present->native_present_attempt_id;
    int release_same_present_id = present &&
        g_d3d12_buffer_release_observed_count != 0 &&
        present->present_source_present_id != 0 &&
        g_d3d12_buffer_release_present_id ==
            present->present_source_present_id;
    int callback_same_resource = present &&
        g_d3d12_frame_callback_observed_count != 0 &&
        g_d3d12_frame_callback_resource == present->resource;
    int callback_same_generation = present &&
        g_d3d12_frame_callback_observed_count != 0 &&
        g_d3d12_frame_callback_buffer_generation ==
            present->buffer_generation;
    int callback_same_attempt = present &&
        g_d3d12_frame_callback_observed_count != 0 &&
        g_d3d12_frame_callback_attempt_id ==
            present->native_present_attempt_id;
    int callback_same_present_id = present &&
        g_d3d12_frame_callback_observed_count != 0 &&
        present->present_source_present_id != 0 &&
        g_d3d12_frame_callback_present_id ==
            present->present_source_present_id;
    int callback_release_same_frame = !rejected && release_same_resource &&
        release_same_generation && release_same_attempt &&
        release_same_present_id && callback_same_resource &&
        callback_same_generation && callback_same_attempt &&
        callback_same_present_id;
    int present_source_query_required = present &&
        present->present_source_commit_successes != 0 &&
        present->present_source_present_id != 0 &&
        present->present_source_completed <
            present->present_source_present_id;
    int present_source_query_skipped_commit_failed = present &&
        present->present_source_commit_attempts != 0 &&
        present->present_source_commit_successes == 0;
    int present_source_query_skipped_no_present_id = present &&
        present->present_source_commit_successes != 0 &&
        present->present_source_present_id == 0;
    int present_source_query_after_commit_success = present &&
        present->present_source_commit_successes != 0 &&
        present->present_source_query_attempts != 0;
    int present_source_query_kernel_missing = present &&
        present->present_source_commit_successes != 0 &&
        present->present_source_query_attempts != 0 &&
        (present->present_source_query_errno == ENOSYS ||
         present->present_source_query_errno == ENOTTY ||
         present->present_source_query_errno == EOPNOTSUPP);
    int present_source_registered = present &&
        present->present_source_register_successes != 0 &&
        present->present_source_register_errno == 0 &&
        present->present_source != 0;
    int present_source_query_attempted = present &&
        present->present_source_query_attempts != 0;
    const char *present_source_query_skipped_reason = "not-skipped";
    int present_source_commit_rejected_eopnotsupp = present &&
        present->present_source_commit_attempts != 0 &&
        present->present_source_commit_successes == 0 &&
        present->present_source_commit_errno == EOPNOTSUPP &&
        present->present_source_commit_status == EOPNOTSUPP &&
        present->present_source_commit_expected_eopnotsupp;
    int present_source_no_present_id_completed = present &&
        present->present_source_present_id == 0 &&
        present->present_source_completed == 0;
    int present_source_no_gpu_p_or_dda_display_bind =
        present_source_commit_rejected_eopnotsupp;
    int present_source_no_display_handoff = rejected &&
        (!present || present->present_source_commit_successes == 0 ||
         present->native_present_completion_id == 0);
    int present_source_no_present_completion = present &&
        present->present_source_present_id == 0 &&
        present->present_source_completed == 0 &&
        present->native_present_completion_id == 0;
    int same_frame_callbacks_blocked = rejected &&
        !callback_same_resource &&
        !callback_same_generation &&
        !callback_same_attempt &&
        !callback_same_present_id;
    int same_frame_releases_blocked = rejected &&
        !release_same_resource &&
        !release_same_generation &&
        !release_same_attempt &&
        !release_same_present_id;
    uint64_t identity_client_attempts = 0;
    uint64_t identity_client_completions = 0;
    uint64_t identity_client_rejects = 0;
    uint64_t identity_resource_attempts = 0;
    uint64_t identity_resource_completions = 0;
    uint64_t identity_resource_rejects = 0;

    if (!present)
        present_source_query_skipped_reason = "no-present";
    else if (present_source_query_attempted)
        present_source_query_skipped_reason = "not-skipped";
    else if (!present_source_registered)
        present_source_query_skipped_reason = "source-not-registered";
    else if (present->present_source_commit_attempts == 0)
        present_source_query_skipped_reason = "commit-not-attempted";
    else if (present->present_source_commit_successes == 0)
        present_source_query_skipped_reason = "commit-failed";
    else if (present->present_source_present_id == 0)
        present_source_query_skipped_reason = "no-present-id";
    else if (present->present_source_completed >=
             present->present_source_present_id)
        present_source_query_skipped_reason = "already-complete";
    else
        present_source_query_skipped_reason = "query-required";

    if (!fp)
        return;
    g_d3d12_evidence_generation++;
    xv6_d3d12_count_identity_present_progress(
        present, &identity_client_attempts, &identity_client_completions,
        &identity_client_rejects, &identity_resource_attempts,
        &identity_resource_completions, &identity_resource_rejects);
    if (rejected) {
        fprintf(fp,
                "d3d12_present_evidence_time_us=%lu\n"
                "d3d12_evidence_generation=%lu\n"
                "d3d12_evidence_stage=present_rejected\n"
                "d3d12_present_rejected=1\n"
                "d3d12_native_present_unimplemented=1\n"
                "d3d12_present_path=fail-closed\n"
                "d3d12_gpu_present_starts=%lu\n"
                "d3d12_gpu_copy_completes=%lu\n"
                "d3d12_gpu_present_completes=0\n"
                "d3d12_gpu_present_complete=0\n"
                "d3d12_import_evidence_required=1\n"
                "d3d12_shared_resource_import_required=1\n"
                "d3d12_fence_import_required=1\n"
                "d3d12_display_completion_required=1\n"
                "d3d12_display_completion_correlated=0\n"
                "d3d12_native_present_requirements_satisfied=0\n"
                "d3d12_copy_target=compositor-owned-d3d12-texture\n"
                "d3d12_compositor_target_create_attempts=%lu\n"
                "d3d12_compositor_target_create_successes=%lu\n"
                "d3d12_compositor_gpu_copy_submits=%lu\n"
                "d3d12_compositor_gpu_copy_waits=%lu\n"
                "d3d12_compositor_copy_queue_type=direct\n"
                "d3d12_compositor_copy_device_ptr=0x%lx\n"
                "d3d12_compositor_copy_queue_ptr=0x%lx\n"
                "d3d12_compositor_copy_cmdlist_ptr=0x%lx\n"
                "d3d12_compositor_copy_hwqueue_handle=0\n"
                "d3d12_compositor_copy_hwqueue_source=d3d12-command-queue\n"
                "d3d12_display_handoff_implemented=0\n"
                "d3d12_display_target_kind=%s\n"
                "d3d12_d3d12_copy_target_presentable=0\n"
                "d3d12_fb_bo_as_d3d12_heap_supported=0\n"
                "d3d12_fb_bo_as_d3d12_shared_resource_supported=0\n"
                "d3d12_row_major_cpu_visible_present_target_used=0\n"
                "d3d12_readback_present_target_used=0\n"
                "d3d12_display_handoff_requires_kernel_host_protocol=1\n"
                "d3d12_display_target_requires_kernel_host_protocol=1\n"
                "d3d12_dxg_present_source_register_attempts=%lu\n"
                "d3d12_dxg_present_source_register_successes=%lu\n"
                "d3d12_dxg_present_source_register_errno=%d\n"
                "d3d12_dxg_present_source=0x%x\n"
                "d3d12_dxg_present_source_commit_attempts=%lu\n"
                "d3d12_dxg_present_source_commit_successes=%lu\n"
                "d3d12_dxg_present_source_commit_errno=%d\n"
                "d3d12_dxg_present_source_commit_status=%d\n"
                "d3d12_dxg_present_source_commit_expected_eopnotsupp=%d\n"
                "d3d12_dxg_present_id=%lu\n"
                "d3d12_dxg_present_completed=%lu\n"
                "d3d12_fb_heap_open_path=not-used\n"
                "d3d12_fb_heap_open_supported=%d\n"
                "d3d12_fb_heap_open_attempts=%lu\n"
                "d3d12_fb_heap_open_successes=%lu\n"
                "d3d12_fb_heap_open_notimpl=%lu\n"
                "d3d12_fb_heap_open_cached_rejects=%lu\n"
                "d3d12_acquire_sync_path=dxg-monitored-fence-cpu-wait\n"
                "d3d12_acquire_sync_d3d12_fence_open_used=0\n"
                "d3d12_acquire_sync_wait_confirmed=%d\n"
                "d3d12_gpu_present_rejects=%lu\n"
                "d3d12_context_gpu_present_completes=%lu\n"
                "d3d12_resource_submissions=%lu\n"
                "d3d12_protocol_accepts=%lu\n"
                "d3d12_native_import_attempts=%lu\n"
                "d3d12_native_import_completes=%lu\n"
                "d3d12_resource_import_attempts=%lu\n"
                "d3d12_resource_import_successes=%lu\n"
                "d3d12_fence_import_attempts=%lu\n"
                "d3d12_fence_import_successes=%lu\n"
                "d3d12_syncfile_fence_import_attempts=%lu\n"
                "d3d12_syncfile_fence_import_successes=%lu\n"
                "d3d12_fence_target_observed=%lu\n"
                "d3d12_gpu_copy_composite_starts=%lu\n"
                "d3d12_gpu_copy_composite_completes=%lu\n"
                "d3d12_framebuffer_fallback_used=0\n"
                "d3d12_framebuffer_fallback_count=%lu\n"
                "d3d12_buffer_release_observed=%lu\n"
                "d3d12_buffer_release_resource=0x%x\n"
                "d3d12_buffer_release_present_sequence=%lu\n"
                "d3d12_frame_callback_observed=%lu\n"
                "d3d12_frame_callback_resource=0x%x\n"
                "d3d12_frame_callback_present_sequence=%lu\n"
                "d3d12_present_errno=%d\n"
                "d3d12_present_missing=%s\n"
                "d3d12_cpu_readback=0\n"
                "d3d12_cpu_mapping=0\n"
                "d3d12_cpu_copy=0\n"
                "d3d12_no_readback=1\n"
                "d3d12_cpu_map_rejects=%lu\n"
                "d3d12_cpu_plane_rejects=%lu\n"
                "d3d12_present_resource=0x%x\n"
                "d3d12_present_allocation_count=%u\n"
                "d3d12_present_fence=0x%x\n"
                "d3d12_present_fence_target=%lu\n"
                "d3d12_present_release_fence=0\n"
                "d3d12_present_format=0x%x\n"
                "d3d12_present_source_luid_valid=%d\n"
                "d3d12_present_same_luid=%d\n"
                "d3d12_present_wait_confirmed=%d\n"
                "d3d12_present_luid=%08x:%08x\n"
                "d3d12_present_matched_luid=%08x:%08x\n"
                "d3d12_present_state_only=0\n"
                "d3d12_present_fence_only=0\n"
                "d3d12_present_import_only=0\n"
                "d3d12_present_open_only=0\n"
                "d3d12_present_callback_only=0\n"
                "d3d12_present_release_only=0\n"
                "callbacks_blocked=%d\n"
                "releases_blocked=%d\n",
                (unsigned long)evidence_time_us,
                (unsigned long)g_d3d12_evidence_generation,
                (unsigned long)g_d3d12_present_start_count,
                (unsigned long)g_d3d12_present_copy_complete_count,
                (unsigned long)g_d3d12_compositor_target_create_attempt_count,
                (unsigned long)g_d3d12_compositor_target_create_success_count,
                (unsigned long)g_d3d12_compositor_gpu_copy_submit_count,
                (unsigned long)g_d3d12_compositor_gpu_copy_wait_count,
                (unsigned long)g_d3d12_compositor_copy_device_ptr,
                (unsigned long)g_d3d12_compositor_copy_queue_ptr,
                (unsigned long)g_d3d12_compositor_copy_cmdlist_ptr,
                xv6_d3d12_display_target_kind_name(
                    present ? present->display_target_kind : 0),
                (unsigned long)g_d3d12_dxg_present_register_attempt_count,
                (unsigned long)g_d3d12_dxg_present_register_success_count,
                g_d3d12_dxg_present_register_errno,
                g_d3d12_dxg_present_source,
                (unsigned long)g_d3d12_dxg_present_commit_attempt_count,
                (unsigned long)g_d3d12_dxg_present_commit_success_count,
                g_d3d12_dxg_present_commit_errno,
                g_d3d12_dxg_present_commit_errno,
                present ? present->present_source_commit_expected_eopnotsupp :
                1,
                (unsigned long)g_d3d12_dxg_present_id,
                (unsigned long)g_d3d12_dxg_present_completed,
                0,
                (unsigned long)g_d3d12_fb_heap_open_attempt_count,
                (unsigned long)g_d3d12_fb_heap_open_success_count,
                (unsigned long)g_d3d12_fb_heap_open_notimpl_count,
                (unsigned long)g_d3d12_fb_heap_open_cached_reject_count,
                present ? present->wait_confirmed : 0,
                (unsigned long)g_d3d12_present_reject_count,
                (unsigned long)g_d3d12_present_display_complete_count,
                (unsigned long)g_d3d12_resource_submit_count,
                (unsigned long)g_d3d12_resource_protocol_accept_count,
                (unsigned long)g_d3d12_native_import_attempt_count,
                (unsigned long)g_d3d12_native_import_complete_count,
                (unsigned long)g_d3d12_resource_import_attempt_count,
                (unsigned long)g_d3d12_resource_import_success_count,
                (unsigned long)g_d3d12_fence_import_attempt_count,
                (unsigned long)g_d3d12_fence_import_success_count,
                (unsigned long)g_d3d12_syncfile_fence_import_attempt_count,
                (unsigned long)g_d3d12_syncfile_fence_import_success_count,
                (unsigned long)g_d3d12_fence_target_observed_count,
                (unsigned long)g_d3d12_present_start_count,
                (unsigned long)g_d3d12_present_copy_complete_count,
                (unsigned long)g_d3d12_framebuffer_fallback_count,
                (unsigned long)g_d3d12_buffer_release_observed_count,
                g_d3d12_buffer_release_resource,
                (unsigned long)g_d3d12_buffer_release_present_sequence,
                (unsigned long)g_d3d12_frame_callback_observed_count,
                g_d3d12_frame_callback_resource,
                (unsigned long)g_d3d12_frame_callback_present_sequence,
                err, g_d3d12_present_missing[0] ?
                g_d3d12_present_missing : "unknown",
                (unsigned long)g_d3d12_cpu_map_reject_count,
                (unsigned long)g_d3d12_cpu_plane_reject_count,
                present ? present->resource : 0,
                present ? present->allocation_count : 0,
                present ? present->fence : 0,
                (unsigned long)(present ? present->fence_target : 0),
                present ? present->format : 0,
                present ? present->source_luid_valid : 0,
                same_luid,
                present ? present->wait_confirmed : 0,
                present ? present->source_luid_b : 0,
                present ? present->source_luid_a : 0,
                present ? present->matched_luid_b : 0,
                present ? present->matched_luid_a : 0,
                same_frame_callbacks_blocked,
                same_frame_releases_blocked);
    } else {
        fprintf(fp,
                "d3d12_present_evidence_time_us=%lu\n"
                "d3d12_evidence_generation=%lu\n"
                "d3d12_evidence_stage=present_completed\n"
                "d3d12_present_rejected=0\n"
                "d3d12_present_path=d3d12-dxg-present-source-display-handoff\n"
                "d3d12_gpu_present_starts=%lu\n"
                "d3d12_gpu_copy_completes=%lu\n"
                "d3d12_gpu_present_completes=%lu\n"
                "d3d12_gpu_present_complete=%lu\n"
                "d3d12_gpu_present_start=%lu\n"
                "d3d12_gpu_copy_complete=%lu\n"
                "d3d12_import_evidence_required=1\n"
                "d3d12_shared_resource_import_required=1\n"
                "d3d12_fence_import_required=1\n"
                "d3d12_display_completion_required=1\n"
                "d3d12_display_completion_correlated=1\n"
                "d3d12_native_present_requirements_satisfied=1\n"
                "d3d12_copy_target=compositor-owned-d3d12-texture\n"
                "d3d12_compositor_target_create_attempts=%lu\n"
                "d3d12_compositor_target_create_successes=%lu\n"
                "d3d12_compositor_gpu_copy_submits=%lu\n"
                "d3d12_compositor_gpu_copy_waits=%lu\n"
                "d3d12_compositor_copy_queue_type=direct\n"
                "d3d12_compositor_copy_device_ptr=0x%lx\n"
                "d3d12_compositor_copy_queue_ptr=0x%lx\n"
                "d3d12_compositor_copy_cmdlist_ptr=0x%lx\n"
                "d3d12_compositor_copy_hwqueue_handle=0\n"
                "d3d12_compositor_copy_hwqueue_source=d3d12-command-queue\n"
                "d3d12_display_handoff_implemented=1\n"
                "d3d12_display_target_kind=%s\n"
                "d3d12_display_handoff_requires_kernel_host_protocol=0\n"
                "d3d12_display_target_requires_kernel_host_protocol=0\n"
                "d3d12_dxg_present_source_register_attempts=%lu\n"
                "d3d12_dxg_present_source_register_successes=%lu\n"
                "d3d12_dxg_present_source_register_errno=%d\n"
                "d3d12_dxg_present_source=0x%x\n"
                "d3d12_dxg_present_source_commit_attempts=%lu\n"
                "d3d12_dxg_present_source_commit_successes=%lu\n"
                "d3d12_dxg_present_source_commit_errno=%d\n"
                "d3d12_dxg_present_source_commit_status=%d\n"
                "d3d12_dxg_present_source_commit_expected_eopnotsupp=0\n"
                "d3d12_dxg_present_id=%lu\n"
                "d3d12_dxg_present_completed=%lu\n"
                "d3d12_fb_heap_open_path=not-used\n"
                "d3d12_fb_heap_open_supported=%d\n"
                "d3d12_fb_heap_open_attempts=%lu\n"
                "d3d12_fb_heap_open_successes=%lu\n"
                "d3d12_fb_heap_open_notimpl=%lu\n"
                "d3d12_fb_heap_open_cached_rejects=%lu\n"
                "d3d12_acquire_sync_path=dxg-monitored-fence-cpu-wait\n"
                "d3d12_acquire_sync_d3d12_fence_open_used=0\n"
                "d3d12_acquire_sync_wait_confirmed=%d\n"
                "d3d12_resource_submissions=%lu\n"
                "d3d12_protocol_accepts=%lu\n"
                "d3d12_native_import_attempts=%lu\n"
                "d3d12_native_import_completes=%lu\n"
                "d3d12_resource_import_attempts=%lu\n"
                "d3d12_resource_import_successes=%lu\n"
                "d3d12_fence_import_attempts=%lu\n"
                "d3d12_fence_import_successes=%lu\n"
                "d3d12_syncfile_fence_import_attempts=%lu\n"
                "d3d12_syncfile_fence_import_successes=%lu\n"
                "d3d12_fence_target_observed=%lu\n"
                "d3d12_gpu_copy_composite_starts=%lu\n"
                "d3d12_gpu_copy_composite_completes=%lu\n"
                "d3d12_framebuffer_fallback_used=0\n"
                "d3d12_framebuffer_fallback_count=%lu\n"
                "d3d12_buffer_release_observed=%lu\n"
                "d3d12_buffer_release_resource=0x%x\n"
                "d3d12_buffer_release_present_sequence=%lu\n"
                "d3d12_frame_callback_observed=%lu\n"
                "d3d12_frame_callback_resource=0x%x\n"
                "d3d12_frame_callback_present_sequence=%lu\n"
                "d3d12_cpu_readback=0\n"
                "d3d12_cpu_mapping=0\n"
                "d3d12_cpu_copy=0\n"
                "d3d12_no_readback=1\n"
                "d3d12_native_present_unimplemented=0\n"
                "d3d12_present_state_only=0\n"
                "d3d12_present_fence_only=0\n"
                "d3d12_present_import_only=0\n"
                "d3d12_present_open_only=0\n"
                "d3d12_present_callback_only=0\n"
                "d3d12_present_release_only=0\n"
                "d3d12_present_completed_native_frames=%lu\n"
                "d3d12_cpu_map_rejects=%lu\n"
                "d3d12_cpu_plane_rejects=%lu\n"
                "d3d12_present_resource=0x%x\n"
                "d3d12_present_allocation_count=%u\n"
                "d3d12_present_fence=0x%x\n"
                "d3d12_present_fence_target=%lu\n"
                "d3d12_present_release_fence=%lu\n"
                "d3d12_present_format=0x%x\n"
                "d3d12_present_source_luid_valid=%d\n"
                "d3d12_present_same_luid=%d\n"
                "d3d12_present_wait_confirmed=%d\n"
                "d3d12_present_luid=%08x:%08x\n"
                "d3d12_present_matched_luid=%08x:%08x\n"
                "d3d12_present_dst=%d,%d %ux%u\n"
                "d3d12_present_src=%d,%d %ux%u\n",
                (unsigned long)evidence_time_us,
                (unsigned long)g_d3d12_evidence_generation,
                (unsigned long)g_d3d12_present_start_count,
                (unsigned long)g_d3d12_present_copy_complete_count,
                (unsigned long)g_d3d12_present_display_complete_count,
                (unsigned long)result->complete_sequence,
                (unsigned long)result->start_sequence,
                (unsigned long)result->copy_sequence,
                (unsigned long)g_d3d12_compositor_target_create_attempt_count,
                (unsigned long)g_d3d12_compositor_target_create_success_count,
                (unsigned long)g_d3d12_compositor_gpu_copy_submit_count,
                (unsigned long)g_d3d12_compositor_gpu_copy_wait_count,
                (unsigned long)g_d3d12_compositor_copy_device_ptr,
                (unsigned long)g_d3d12_compositor_copy_queue_ptr,
                (unsigned long)g_d3d12_compositor_copy_cmdlist_ptr,
                xv6_d3d12_display_target_kind_name(
                    present ? present->display_target_kind : 0),
                (unsigned long)g_d3d12_dxg_present_register_attempt_count,
                (unsigned long)g_d3d12_dxg_present_register_success_count,
                g_d3d12_dxg_present_register_errno,
                g_d3d12_dxg_present_source,
                (unsigned long)g_d3d12_dxg_present_commit_attempt_count,
                (unsigned long)g_d3d12_dxg_present_commit_success_count,
                g_d3d12_dxg_present_commit_errno,
                g_d3d12_dxg_present_commit_errno,
                (unsigned long)g_d3d12_dxg_present_id,
                (unsigned long)g_d3d12_dxg_present_completed,
                0,
                (unsigned long)g_d3d12_fb_heap_open_attempt_count,
                (unsigned long)g_d3d12_fb_heap_open_success_count,
                (unsigned long)g_d3d12_fb_heap_open_notimpl_count,
                (unsigned long)g_d3d12_fb_heap_open_cached_reject_count,
                present->wait_confirmed,
                (unsigned long)g_d3d12_resource_submit_count,
                (unsigned long)g_d3d12_resource_protocol_accept_count,
                (unsigned long)g_d3d12_native_import_attempt_count,
                (unsigned long)g_d3d12_native_import_complete_count,
                (unsigned long)g_d3d12_resource_import_attempt_count,
                (unsigned long)g_d3d12_resource_import_success_count,
                (unsigned long)g_d3d12_fence_import_attempt_count,
                (unsigned long)g_d3d12_fence_import_success_count,
                (unsigned long)g_d3d12_syncfile_fence_import_attempt_count,
                (unsigned long)g_d3d12_syncfile_fence_import_success_count,
                (unsigned long)g_d3d12_fence_target_observed_count,
                (unsigned long)g_d3d12_present_start_count,
                (unsigned long)g_d3d12_present_copy_complete_count,
                (unsigned long)g_d3d12_framebuffer_fallback_count,
                (unsigned long)g_d3d12_buffer_release_observed_count,
                g_d3d12_buffer_release_resource,
                (unsigned long)g_d3d12_buffer_release_present_sequence,
                (unsigned long)g_d3d12_frame_callback_observed_count,
                g_d3d12_frame_callback_resource,
                (unsigned long)g_d3d12_frame_callback_present_sequence,
                (unsigned long)g_d3d12_present_display_complete_count,
                (unsigned long)g_d3d12_cpu_map_reject_count,
                (unsigned long)g_d3d12_cpu_plane_reject_count,
                present->resource, present->allocation_count,
                present->fence,
                (unsigned long)present->fence_target,
                (unsigned long)result->release_fence,
                present->format,
                present->source_luid_valid,
                same_luid,
                present->wait_confirmed,
                present->source_luid_b, present->source_luid_a,
                present->matched_luid_b, present->matched_luid_a,
                present->dst.x, present->dst.y, present->dst.w,
                present->dst.h, present->src.x, present->src.y,
                present->src.w, present->src.h);
    }
    fprintf(fp,
            "d3d12_run_id=%s\n"
            "d3d12_client_pid=%d\n"
            "d3d12_client_buffer_id=%u\n"
            "d3d12_manager_resource_id=%u\n"
            "d3d12_buffer_generation=%lu\n"
            "d3d12_native_present_attempts=%lu\n"
            "d3d12_native_present_completions=%lu\n"
            "d3d12_context_native_present_completions=%lu\n"
            "d3d12_native_present_attempt_id=%lu\n"
            "d3d12_native_present_completion_id=%lu\n"
            "d3d12_context_native_present_completion_id=%lu\n"
            "d3d12_client_native_present_attempts=%lu\n"
            "d3d12_client_native_present_completions=%lu\n"
            "d3d12_client_native_present_rejects=%lu\n"
            "d3d12_resource_native_present_attempts=%lu\n"
            "d3d12_resource_native_present_completions=%lu\n"
            "d3d12_resource_native_present_rejects=%lu\n"
            "d3d12_resource_generation_counter=%lu\n"
            "d3d12_resource_generation_native_present_attempts=%lu\n"
            "d3d12_resource_generation_native_present_completions=%lu\n"
            "d3d12_resource_generation_native_present_rejects=%lu\n"
            "d3d12_identity_counters_match_resource=%d\n"
            "d3d12_present_identity_compositor_run_id=%s\n"
            "d3d12_present_identity_client_pid=%d\n"
            "d3d12_present_identity_client_buffer_id=%u\n"
            "d3d12_present_identity_manager_resource_id=%u\n"
            "d3d12_present_identity_buffer_generation=%lu\n"
            "d3d12_present_identity_attempt_id=%lu\n"
            "d3d12_present_identity_completion_id=%lu\n"
            "d3d12_present_identity_current_run_valid=%d\n"
            "d3d12_callback_release_same_frame_required=1\n"
            "d3d12_buffer_release_buffer_generation=%lu\n"
            "d3d12_buffer_release_attempt_id=%lu\n"
            "d3d12_buffer_release_completion_id=%lu\n"
            "d3d12_buffer_release_present_id=%lu\n"
            "d3d12_buffer_release_same_resource=%d\n"
            "d3d12_buffer_release_same_generation=%d\n"
            "d3d12_buffer_release_same_attempt=%d\n"
            "d3d12_buffer_release_same_present_id=%d\n"
            "d3d12_frame_callback_buffer_generation=%lu\n"
            "d3d12_frame_callback_attempt_id=%lu\n"
            "d3d12_frame_callback_completion_id=%lu\n"
            "d3d12_frame_callback_present_id=%lu\n"
            "d3d12_frame_callback_same_resource=%d\n"
            "d3d12_frame_callback_same_generation=%d\n"
            "d3d12_frame_callback_same_attempt=%d\n"
            "d3d12_frame_callback_same_present_id=%d\n"
            "d3d12_callback_release_same_frame_observed=%d\n"
            "d3d12_buffer_release_native_successes=%lu\n"
            "d3d12_frame_callback_native_successes=%lu\n"
            "d3d12_buffer_release_failclosed_unblocks=%lu\n"
            "d3d12_frame_callback_failclosed_unblocks=%lu\n"
            "d3d12_failclosed_client_unblock_enabled=%d\n"
            "d3d12_failclosed_client_unblocked=%lu\n"
            "d3d12_failclosed_client_unblock_no_native_present_credit=1\n"
            "d3d12_failclosed_client_unblock_resource=0x%lx\n"
            "d3d12_failclosed_client_unblock_sequence=%lu\n"
            "d3d12_failclosed_client_unblock_buffer_generation=%lu\n"
            "d3d12_failclosed_client_unblock_attempt_id=%lu\n"
            "d3d12_failclosed_client_unblock_releases=%lu\n"
            "d3d12_failclosed_client_unblock_callbacks=%lu\n"
            "d3d12_present_source_buffer_register_attempts=%lu\n"
            "d3d12_present_source_buffer_register_successes=%lu\n"
            "d3d12_present_source_buffer_register_errno=%d\n"
            "d3d12_present_source_buffer_source=0x%x\n"
            "d3d12_present_source_buffer_commit_attempts=%lu\n"
            "d3d12_present_source_buffer_commit_successes=%lu\n"
            "d3d12_present_source_buffer_commit_errno=%d\n"
            "d3d12_present_source_buffer_commit_status=%d\n"
            "d3d12_present_source_buffer_commit_expected_eopnotsupp=%d\n"
            "d3d12_present_source_buffer_present_id=%lu\n"
            "d3d12_present_source_buffer_completed=%lu\n"
            "d3d12_present_source_buffer_query_attempts=%lu\n"
            "d3d12_present_source_buffer_query_successes=%lu\n"
            "d3d12_present_source_buffer_query_errno=%d\n"
            "d3d12_present_source_buffer_query_required=%d\n"
            "d3d12_present_source_buffer_query_skipped_commit_failed=%d\n"
            "d3d12_present_source_buffer_query_skipped_no_present_id=%d\n"
            "d3d12_present_source_buffer_query_attempted_after_commit_success=%d\n"
            "d3d12_present_source_buffer_query_kernel_missing=%d\n"
            "d3d12_present_source_registered=%d\n"
            "d3d12_present_source_query_attempted=%d\n"
            "d3d12_present_source_query_skipped_reason=%s\n"
            "d3d12_present_source_gpu_p_or_dda_transport_absent=1\n"
            "d3d12_present_source_no_gpu_p_or_dda_display_bind=%d\n"
            "d3d12_present_source_commit_rejected_eopnotsupp=%d\n"
            "d3d12_present_source_no_present_id_completed=%d\n"
            "d3d12_present_source_no_display_handoff=%d\n"
            "d3d12_present_source_no_present_completion=%d\n"
            "d3d12_present_source_same_frame_callbacks_blocked=%d\n"
            "d3d12_present_source_same_frame_releases_blocked=%d\n"
            "d3d12_present_source_callback_blocked=%d\n"
            "d3d12_present_source_release_blocked=%d\n"
            "d3d12_present_source_adapter_luid_low=0x%x\n"
            "d3d12_present_source_adapter_luid_high=0x%x\n"
            "d3d12_present_source_provenance_flags=0x%x\n"
            "d3d12_present_source_register_flags=0x%x\n"
            "d3d12_present_descriptor_width=%u\n"
            "d3d12_present_descriptor_height=%u\n"
            "d3d12_present_descriptor_pitch=%u\n"
            "d3d12_present_descriptor_layout=linear\n"
            "d3d12_present_descriptor_modifier=0x%lx\n"
            "d3d12_present_descriptor_sample_count=%u\n"
            "d3d12_present_descriptor_dxg_fd=%d\n"
            "d3d12_present_descriptor_resource_fd=%d\n"
            "d3d12_present_descriptor_nt_shared_fd=%d\n"
            "d3d12_present_descriptor_device=0x%x\n"
            "d3d12_present_descriptor_resource=0x%x\n"
            "d3d12_present_descriptor_allocation0=0x%x\n"
            "d3d12_present_descriptor_allocation_count=%u\n"
            "d3d12_present_descriptor_format=0x%x\n"
            "d3d12_present_descriptor_total_private_size=%u\n"
            "d3d12_present_descriptor_luid=%08x:%08x\n"
            "d3d12_present_descriptor_matches_gpu_copy_resource=%d\n"
            "d3d12_display_target_kind_raw=%u\n"
            "d3d12_display_target_kind_kernel=%s\n"
            "d3d12_display_target_kernel_reported=%d\n"
            "d3d12_runtime_created_d3d12_resource_present=%d\n"
            "d3d12_runtime_created_d3d12_resource_required=1\n"
            "d3d12_synthvid_sysmem_fallback_present=%d\n"
            "d3d12_existing_sysmem_is_d3d12_com_resource=0\n"
            "d3d12_synthvid_sysmem_satisfies_native_present=0\n"
            "d3d12_synthvid_dirty_rect_count=%u\n"
            "d3d12_synthvid_dirty_sequence=%lu\n"
            "d3d12_dirty_rect_display_completion_correlated=%d\n"
            "d3d12_present_sequence_cpu_map_rejects_delta=%lu\n"
            "d3d12_present_sequence_cpu_plane_rejects_delta=%lu\n"
            "d3d12_no_cpu_map_no_readback_confirmed=%d\n"
            "d3d12_present_source_buffer_completion_correlated=%d\n"
            "d3d12_framebuffer_blit_attempts=%lu\n"
            "d3d12_framebuffer_blit_successes=%lu\n"
            "d3d12_framebuffer_blit_attempts_at_present=%lu\n"
            "d3d12_framebuffer_blit_successes_at_present=%lu\n"
            "d3d12_framebuffer_present_frames=%lu\n"
            "d3d12_framebuffer_present_rects=%lu\n"
            "d3d12_framebuffer_present_pixels=%lu\n"
            "d3d12_framebuffer_present_full_frames=%lu\n"
            "d3d12_framebuffer_present_union_collapses=%lu\n"
            "d3d12_partial_framebuffer_updates=%lu\n"
            "d3d12_cpu_map_rejects_at_present=%lu\n"
            "d3d12_cpu_plane_rejects_at_present=%lu\n"
            "d3d12_present_sequence_framebuffer_blit_used=0\n"
            "d3d12_present_sequence_cpu_map_used=0\n"
            "d3d12_present_sequence_cpu_readback_used=0\n"
            "d3d12_present_sequence_cpu_copy_used=0\n"
            "d3d12_present_sequence_software_dri_used=0\n"
            "d3d12_software_dri_present_used=0\n"
            "d3d12_dri_software_present_used=0\n"
            "d3d12_framebuffer_blit_only=0\n"
            "d3d12_fps_evidence_only=0\n",
            run_id,
            present ? (int)present->client_pid : 0,
            present ? present->client_buffer_id : 0,
            present ? present->manager_resource_id : 0,
            (unsigned long)(present ? present->buffer_generation : 0),
            (unsigned long)g_d3d12_native_present_attempt_count,
            (unsigned long)(rejected ? 0 :
                g_d3d12_native_present_completion_count),
            (unsigned long)g_d3d12_native_present_completion_count,
            (unsigned long)(present ? present->native_present_attempt_id : 0),
            (unsigned long)(rejected ? 0 : (present ?
                present->native_present_completion_id : 0)),
            (unsigned long)(present ? present->native_present_completion_id : 0),
            (unsigned long)identity_client_attempts,
            (unsigned long)identity_client_completions,
            (unsigned long)identity_client_rejects,
            (unsigned long)identity_resource_attempts,
            (unsigned long)identity_resource_completions,
            (unsigned long)identity_resource_rejects,
            (unsigned long)(present ? present->buffer_generation : 0),
            (unsigned long)identity_resource_attempts,
            (unsigned long)identity_resource_completions,
            (unsigned long)identity_resource_rejects,
            present && identity_resource_attempts != 0 &&
                identity_resource_attempts <= identity_client_attempts &&
                identity_resource_completions <= identity_client_completions &&
                identity_resource_rejects <= identity_client_rejects,
            run_id,
            present ? (int)present->client_pid : 0,
            present ? present->client_buffer_id : 0,
            present ? present->manager_resource_id : 0,
            (unsigned long)(present ? present->buffer_generation : 0),
            (unsigned long)(present ? present->native_present_attempt_id : 0),
            (unsigned long)(rejected ? 0 : (present ?
                present->native_present_completion_id : 0)),
            present_identity_valid,
            (unsigned long)g_d3d12_buffer_release_buffer_generation,
            (unsigned long)g_d3d12_buffer_release_attempt_id,
            (unsigned long)g_d3d12_buffer_release_completion_id,
            (unsigned long)g_d3d12_buffer_release_present_id,
            release_same_resource,
            release_same_generation,
            release_same_attempt,
            release_same_present_id,
            (unsigned long)g_d3d12_frame_callback_buffer_generation,
            (unsigned long)g_d3d12_frame_callback_attempt_id,
            (unsigned long)g_d3d12_frame_callback_completion_id,
            (unsigned long)g_d3d12_frame_callback_present_id,
            callback_same_resource,
            callback_same_generation,
            callback_same_attempt,
            callback_same_present_id,
            callback_release_same_frame,
            (unsigned long)g_d3d12_buffer_release_native_success_count,
            (unsigned long)g_d3d12_frame_callback_native_success_count,
            (unsigned long)g_d3d12_failclosed_client_unblock_release_count,
            (unsigned long)g_d3d12_failclosed_client_unblock_callback_count,
            xv6_d3d12_failclosed_unblocks_clients(),
            (unsigned long)g_d3d12_failclosed_client_unblock_count,
            (unsigned long)g_d3d12_failclosed_client_unblock_resource,
            (unsigned long)g_d3d12_failclosed_client_unblock_sequence,
            (unsigned long)g_d3d12_failclosed_client_unblock_buffer_generation,
            (unsigned long)g_d3d12_failclosed_client_unblock_attempt_id,
            (unsigned long)g_d3d12_failclosed_client_unblock_release_count,
            (unsigned long)g_d3d12_failclosed_client_unblock_callback_count,
            (unsigned long)(present ?
                present->present_source_register_attempts : 0),
            (unsigned long)(present ?
                present->present_source_register_successes : 0),
            present ? present->present_source_register_errno : 0,
            present ? present->present_source : 0,
            (unsigned long)(present ?
                present->present_source_commit_attempts : 0),
            (unsigned long)(present ?
                present->present_source_commit_successes : 0),
            present ? present->present_source_commit_errno : 0,
            present ? present->present_source_commit_status : 0,
            present ? present->present_source_commit_expected_eopnotsupp : 0,
            (unsigned long)(present ?
                present->present_source_present_id : 0),
            (unsigned long)(present ?
                present->present_source_completed : 0),
            (unsigned long)(present ?
                present->present_source_query_attempts : 0),
            (unsigned long)(present ?
                present->present_source_query_successes : 0),
            present ? present->present_source_query_errno : 0,
            present_source_query_required,
            present_source_query_skipped_commit_failed,
            present_source_query_skipped_no_present_id,
            present_source_query_after_commit_success,
            present_source_query_kernel_missing,
            present_source_registered,
            present_source_query_attempted,
            present_source_query_skipped_reason,
            present_source_no_gpu_p_or_dda_display_bind,
            present_source_commit_rejected_eopnotsupp,
            present_source_no_present_id_completed,
            present_source_no_display_handoff,
            present_source_no_present_completion,
            same_frame_callbacks_blocked,
            same_frame_releases_blocked,
            same_frame_callbacks_blocked,
            same_frame_releases_blocked,
            present ? present->present_source_adapter_luid_low : 0,
            present ? present->present_source_adapter_luid_high : 0,
            present ? present->present_source_provenance_flags : 0,
            present ? present->present_source_register_flags : 0,
            present ? present->descriptor_width : 0,
            present ? present->descriptor_height : 0,
            present ? present->descriptor_pitch : 0,
            (unsigned long)(present ? present->descriptor_modifier : 0),
            present ? present->descriptor_sample_count : 0,
            present ? present->descriptor_dxg_fd : -1,
            present ? present->descriptor_resource_fd : -1,
            present ? present->descriptor_resource_fd : -1,
            present ? present->device : 0,
            present ? present->resource : 0,
            present && present->allocations &&
                present->allocation_count > 0 ? present->allocations[0] : 0,
            present ? present->allocation_count : 0,
            present ? present->format : 0,
            present ? present->total_priv_size : 0,
            present ? present->present_source_adapter_luid_high : 0,
            present ? present->present_source_adapter_luid_low : 0,
            present && present->resource != 0 &&
                present->present_source_register_successes != 0,
            present ? present->display_target_kind : 0,
            xv6_d3d12_display_target_kind_name(
                present ? present->display_target_kind : 0),
            present && present->display_target_kind !=
                FB_GPU_DXG_DISPLAY_TARGET_NONE,
            present && xv6_d3d12_display_target_is_runtime_resource(
                present->display_target_kind),
            present && xv6_d3d12_display_target_is_synthvid_sysmem(
                present->display_target_kind),
            present ? present->dirty_rect_count : 0,
            (unsigned long)(present ? present->dirty_sequence : 0),
            present && present->dirty_rect_count > 0 &&
                present->dirty_sequence != 0 &&
                present->present_source_completed >=
                    present->dirty_sequence,
            (unsigned long)(present ? xv6_d3d12_counter_delta(
                g_d3d12_cpu_map_reject_count,
                present->cpu_map_rejects_at_present) : 0),
            (unsigned long)(present ? xv6_d3d12_counter_delta(
                g_d3d12_cpu_plane_reject_count,
                present->cpu_plane_rejects_at_present) : 0),
            present && xv6_d3d12_counter_delta(
                g_d3d12_cpu_map_reject_count,
                present->cpu_map_rejects_at_present) == 0 &&
                xv6_d3d12_counter_delta(
                    g_d3d12_cpu_plane_reject_count,
                    present->cpu_plane_rejects_at_present) == 0,
            present ? present->present_source_completion_correlated : 0,
            (unsigned long)g_fb_blit_present_attempt_count,
            (unsigned long)g_fb_blit_present_success_count,
            (unsigned long)(present ?
                present->framebuffer_blit_attempts_at_present : 0),
            (unsigned long)(present ?
                present->framebuffer_blit_successes_at_present : 0),
            (unsigned long)g_damage_stats.frames,
            (unsigned long)g_damage_stats.present_rects,
            (unsigned long)g_damage_stats.present_pixels,
            (unsigned long)g_damage_stats.present_full_frames,
            (unsigned long)g_damage_stats.present_union_collapses,
            (unsigned long)(g_damage_stats.present_rects -
                g_damage_stats.present_full_frames),
            (unsigned long)(present ? present->cpu_map_rejects_at_present : 0),
            (unsigned long)(present ? present->cpu_plane_rejects_at_present :
                0));
    xv6_d3d12_write_final_handoff_evidence(fp, present, result, rejected);
    xv6_d3d12_write_phase2_negative_evidence(fp);
    fclose(fp);
}

static int xv6_d3d12_submit_present_contract(
    struct wlcomp_buffer *buf,
    const struct xv6_d3d12_present_contract *present,
    struct xv6_d3d12_present_result *result)
{
    uint64_t copy_fence = 0;

    if (!present || !result || present->resource == 0 ||
        present->allocation_count == 0 || !present->allocations ||
        present->allocation_count > XV6_D3D12_PRESENT_MAX_ALLOCATIONS ||
        present->fence == 0 || present->fence_target == 0 ||
        present->dst.w == 0 || present->dst.h == 0 ||
        present->src.w == 0 || present->src.h == 0) {
        errno = EINVAL;
        return -1;
    }
    for (uint32_t i = 0; i < present->allocation_count; i++) {
        if (present->allocations[i] == 0) {
            errno = EINVAL;
            return -1;
        }
    }

    g_d3d12_present_start_count++;
    result->start_sequence = get_time_us();
    fprintf(stderr,
            "wlcomp: d3d12 gpu present start d3d12_native_present_attempts=%lu d3d12_native_present_attempt_id=%lu d3d12_gpu_copy_composite_starts=%lu d3d12_gpu_present_starts=%lu client_pid=%d client_buffer_id=%u buffer_generation=%lu resource=0x%x allocation_count=%u fence=0x%x target=%lu current=%lu dst=%d,%d %ux%u src=%d,%d %ux%u no_cpu_readback=1\n",
            (unsigned long)g_d3d12_native_present_attempt_count,
            (unsigned long)present->native_present_attempt_id,
            (unsigned long)g_d3d12_present_start_count,
            (unsigned long)g_d3d12_present_start_count,
            (int)present->client_pid, present->client_buffer_id,
            (unsigned long)present->buffer_generation,
            present->resource, present->allocation_count, present->fence,
            (unsigned long)present->fence_target,
            (unsigned long)present->fence_current,
            present->dst.x, present->dst.y, present->dst.w, present->dst.h,
            present->src.x, present->src.y, present->src.w, present->src.h);
    if (xv6_d3d12_copy_shared_resource_to_compositor_texture(
            buf, present, &copy_fence) != 0)
        return -1;

    result->copy_sequence = get_time_us();
    result->complete_sequence = result->copy_sequence;
    result->release_fence = copy_fence;
    return 0;
}

static void xv6_d3d12_fill_present_from_buffer(
    struct xv6_d3d12_present_contract *present,
    struct wlcomp_buffer *buf,
    uint64_t fence_current,
    int32_t dst_x,
    int32_t dst_y,
    int32_t src_x,
    int32_t src_y,
    uint32_t width,
    uint32_t height)
{
    if (!present || !buf)
        return;

    memset(present, 0, sizeof(*present));
    present->adapter = buf->d3d12_adapter;
    present->device = buf->d3d12_device;
    present->resource = buf->d3d12_opened_resource;
    present->allocation_count = buf->d3d12_opened_allocation_count;
    present->allocations = buf->d3d12_opened_allocations;
    present->fence = buf->d3d12_opened_fence;
    present->fence_target = buf->d3d12_fence_value;
    present->fence_current = fence_current;
    present->format = buf->format;
    present->source_luid_a = buf->d3d12_source_luid_a;
    present->source_luid_b = buf->d3d12_source_luid_b;
    present->source_luid_valid = buf->d3d12_source_luid_valid;
    present->matched_luid_a = buf->d3d12_matched_luid_a;
    present->matched_luid_b = buf->d3d12_matched_luid_b;
    present->wait_confirmed = buf->d3d12_present_wait_confirmed;
    present->total_priv_size = buf->d3d12_total_priv_size;
    present->dst.x = dst_x;
    present->dst.y = dst_y;
    present->dst.w = width;
    present->dst.h = height;
    present->src.x = src_x;
    present->src.y = src_y;
    present->src.w = width;
    present->src.h = height;
    xv6_d3d12_fill_present_identity(present, buf);
}

static void xv6_d3d12_write_waiting_fence_present_evidence(
    struct wlcomp_buffer *buf,
    uint64_t current,
    int32_t dst_x,
    int32_t dst_y,
    int32_t src_x,
    int32_t src_y,
    uint32_t width,
    uint32_t height)
{
    struct xv6_d3d12_present_contract present;
    struct xv6_d3d12_present_result result;
    static uint64_t last_generation;

    if (!buf || !buf->is_d3d12_shared ||
        last_generation == buf->d3d12_buffer_generation)
        return;

    memset(&result, 0, sizeof(result));
    xv6_d3d12_fill_present_from_buffer(&present, buf, current, dst_x, dst_y,
                                       src_x, src_y, width, height);
    snprintf(g_d3d12_present_missing, sizeof(g_d3d12_present_missing),
             "D3D12 shared-resource commit waiting for monitored acquire fence target before GPU copy target=%lu current=%lu",
             (unsigned long)buf->d3d12_fence_value, (unsigned long)current);
    xv6_d3d12_write_present_evidence(&present, &result, 1, EAGAIN);
    fprintf(stderr,
            "wlcomp: d3d12 present evidence waiting for acquire fence resource=0x%x fence=0x%x target=%lu current=%lu callbacks_blocked=1 releases_blocked=1 no_cpu_readback=1\n",
            buf->d3d12_opened_resource, buf->d3d12_opened_fence,
            (unsigned long)buf->d3d12_fence_value, (unsigned long)current);
    last_generation = buf->d3d12_buffer_generation;
}

static int xv6_d3d12_present_source_has_display_completion(
    const struct wlcomp_buffer *buf)
{
    struct xv6_d3d12_present_source_record *source_record;

    source_record = xv6_d3d12_get_present_source_record(buf, 0);
    return source_record && source_record->register_successes &&
           source_record->commit_successes &&
           source_record->present_id != 0 &&
           source_record->completed >= source_record->present_id;
}

static int xv6_d3d12_present_source_completion_pending(
    const struct wlcomp_buffer *buf)
{
    struct xv6_d3d12_present_source_record *source_record;

    source_record = xv6_d3d12_get_present_source_record(buf, 0);
    return source_record && source_record->register_successes &&
           source_record->commit_successes &&
           source_record->present_id != 0 &&
           source_record->completed < source_record->present_id &&
           (source_record->query_errno == 0 ||
            source_record->query_errno == EAGAIN);
}

static int xv6_d3d12_refresh_present_source_completion(
    struct wlcomp_buffer *buf)
{
    struct fb_gpu_dxg_present_source_query query;
    struct xv6_d3d12_present_source_record *source_record;
    int err;
    int ret;

    source_record = xv6_d3d12_get_present_source_record(buf, 0);
    if (!source_record || !source_record->register_successes ||
        !source_record->commit_successes || source_record->present_id == 0 ||
        source_record->completed >= source_record->present_id)
        return xv6_d3d12_present_source_has_display_completion(buf);

    memset(&query, 0, sizeof(query));
    query.present_source = source_record->present_source;
    query.present_id = source_record->present_id;
    query.completed = source_record->completed;
    query.display_target_kind = source_record->display_target_kind;

    source_record->query_attempts++;
    ret = ioctl(g_fb_fd, FB_GPU_DXG_PRESENT_SOURCE_QUERY, &query);
    if (ret == 0 || errno == EAGAIN) {
        err = ret == 0 ? 0 : EAGAIN;
        if (query.completed > source_record->completed)
            source_record->completed = query.completed;
        if (query.display_target_kind != FB_GPU_DXG_DISPLAY_TARGET_NONE)
            source_record->display_target_kind = query.display_target_kind;
        source_record->query_errno = err;
        if (err == 0)
            source_record->query_successes++;
        g_d3d12_dxg_present_completed = source_record->completed;
        fprintf(stderr,
                "wlcomp: d3d12 dxg present-source completion query source=0x%x present_id=%lu completed=%lu target_kind=%s target_raw=%u dirty_rects=%u dirty_sequence=%lu target_met=%d errno=%d (%s) callbacks_blocked=%d releases_blocked=%d no_cpu_readback=1\n",
                source_record->present_source,
                (unsigned long)source_record->present_id,
                (unsigned long)source_record->completed,
                xv6_d3d12_display_target_kind_name(
                    source_record->display_target_kind),
                source_record->display_target_kind,
                source_record->dirty_rect_count,
                (unsigned long)source_record->dirty_sequence,
                source_record->completed >= source_record->present_id,
                err, err ? strerror(err) : "none",
                source_record->completed < source_record->present_id,
                source_record->completed < source_record->present_id);
        return source_record->completed >= source_record->present_id;
    }

    source_record->query_errno = errno ? errno : EIO;
    fprintf(stderr,
            "wlcomp: d3d12 dxg present-source completion query failed source=0x%x present_id=%lu completed=%lu errno=%d (%s) callbacks_blocked=1 releases_blocked=1 no_cpu_readback=1\n",
            source_record->present_source,
            (unsigned long)source_record->present_id,
            (unsigned long)source_record->completed,
            source_record->query_errno,
            strerror(source_record->query_errno));
    return 0;
}

static void xv6_d3d12_write_fail_closed_display_handoff_evidence(
    struct wlcomp_buffer *buf)
{
    struct xv6_d3d12_present_contract present;
    struct xv6_d3d12_present_result result;
    struct xv6_d3d12_present_source_record *source_record;
    int present_errno = ENOSYS;

    if (!buf || !buf->is_d3d12_shared || !buf->d3d12_gpu_copy_complete)
        return;
    if (xv6_d3d12_present_source_has_display_completion(buf))
        return;
    source_record = xv6_d3d12_get_present_source_record(buf, 0);
    if (source_record && source_record->commit_successes &&
        source_record->present_id != 0 &&
        source_record->completed < source_record->present_id &&
        (source_record->query_errno == 0 ||
         source_record->query_errno == EAGAIN))
        return;

    xv6_d3d12_fill_present_from_buffer(
        &present, buf, buf->d3d12_fence_value,
        buf->d3d12_present_dst_x, buf->d3d12_present_dst_y,
        buf->d3d12_present_src_x, buf->d3d12_present_src_y,
        buf->d3d12_present_src_w, buf->d3d12_present_src_h);
    memset(&result, 0, sizeof(result));
    result.start_sequence = buf->d3d12_gpu_present_start_time;
    result.copy_sequence = buf->d3d12_gpu_copy_complete_time;
    result.complete_sequence = 0;
    result.release_fence = buf->d3d12_present_fence_value;

    if (!source_record || !source_record->register_successes) {
        if (source_record && source_record->register_errno)
            present_errno = source_record->register_errno;
        snprintf(g_d3d12_present_missing, sizeof(g_d3d12_present_missing),
                 "D3D12 GPU copy proof completed but DXG present-source register did not complete for this buffer errno=%d",
                 source_record ? source_record->register_errno : 0);
    } else if (!source_record->commit_attempts) {
        xv6_d3d12_set_missing(
            "D3D12 GPU copy proof completed and DXG present-source register succeeded, but commit was not attempted");
    } else if (!source_record->commit_successes) {
        if (source_record->commit_errno)
            present_errno = source_record->commit_errno;
        snprintf(g_d3d12_present_missing, sizeof(g_d3d12_present_missing),
                 "D3D12 GPU copy proof completed and DXG present-source commit failed closed errno=%d (%s); host display handoff is not complete",
                 source_record->commit_errno,
                 strerror(source_record->commit_errno));
    } else if (source_record->query_errno != 0 &&
               source_record->query_errno != EAGAIN) {
        present_errno = source_record->query_errno;
        snprintf(g_d3d12_present_missing, sizeof(g_d3d12_present_missing),
                 "D3D12 GPU copy proof and DXG present-source commit succeeded, but completion query failed errno=%d (%s)",
                 source_record->query_errno,
                 strerror(source_record->query_errno));
    } else if (source_record->present_id == 0 ||
               source_record->completed == 0) {
        snprintf(g_d3d12_present_missing, sizeof(g_d3d12_present_missing),
                 "D3D12 GPU copy proof and DXG present-source commit succeeded, but no display completion was reported present_id=%lu completed=%lu",
                 (unsigned long)source_record->present_id,
                 (unsigned long)source_record->completed);
    } else if (source_record->completed < source_record->present_id) {
        snprintf(g_d3d12_present_missing, sizeof(g_d3d12_present_missing),
                 "D3D12 GPU copy proof and DXG present-source commit succeeded, but display completion is stale present_id=%lu completed=%lu",
                 (unsigned long)source_record->present_id,
                 (unsigned long)source_record->completed);
    } else {
        xv6_d3d12_set_missing(
            "D3D12 GPU copy proof completed but DXG present-source display completion was not correlated");
    }

    buf->d3d12_present_evidence_pending = 0;
    g_d3d12_present_reject_count++;
    xv6_d3d12_note_identity_present_reject(buf);
    xv6_d3d12_write_present_evidence(&present, &result, 1, present_errno);
    fprintf(stderr,
            "wlcomp: d3d12 native present fail-closed evidence written resource=0x%x copy_complete=%lu present_source=0x%x register_successes=%lu commit_attempts=%lu commit_successes=%lu commit_errno=%d present_id=%lu completed=%lu display_handoff=0 callbacks_blocked=1 releases_blocked=1 no_cpu_readback=1\n",
            present.resource, (unsigned long)result.copy_sequence,
            source_record ? source_record->present_source : 0,
            (unsigned long)(source_record ?
                source_record->register_successes : 0),
            (unsigned long)(source_record ?
                source_record->commit_attempts : 0),
            (unsigned long)(source_record ?
                source_record->commit_successes : 0),
            source_record ? source_record->commit_errno : 0,
            (unsigned long)(source_record ? source_record->present_id : 0),
            (unsigned long)(source_record ? source_record->completed : 0));
    fprintf(stderr,
            "wlcomp: d3d12_wayland_present_failclosed_identity_matrix "
            "run_id=%s client_pid=%d client_buffer_id=%u "
            "manager_resource_id=%u buffer_generation=%lu "
            "present_source_registered=%d commit_attempted=%d "
            "commit_success=0 present_id=%lu completed=%lu "
            "callbacks_blocked=1 releases_blocked=1 native_present_claim=0 "
            "opengl_submit_credit=0 status=PASS\n",
            xv6_d3d12_present_run_id(), (int)buf->d3d12_client_pid,
            buf->d3d12_client_buffer_id,
            buf->d3d12_manager_resource_id,
            (unsigned long)buf->d3d12_buffer_generation,
            source_record && source_record->register_successes,
            source_record && source_record->commit_attempts,
            (unsigned long)(source_record ? source_record->present_id : 0),
            (unsigned long)(source_record ? source_record->completed : 0));
    fprintf(stderr,
            "wlcomp: d3d12_wayland_present_fps_provenance_matrix "
            "resource_buffer_admitted=1 syncfile_acquire=PASS "
            "gpu_copy_complete=1 display_handoff_implemented=0 "
            "present_id=%lu completed=%lu callbacks_blocked=1 "
            "releases_blocked=1 fps_credit=0 native_present_claim=0 "
            "opengl_submit_credit=0 status=PASS_FAILCLOSED\n",
            (unsigned long)(source_record ? source_record->present_id : 0),
            (unsigned long)(source_record ? source_record->completed : 0));
    xv6_d3d12_unblock_failclosed_client(buf, "display-handoff-missing");
}

static void xv6_d3d12_probe_dxg_present_source_handoff(
    struct wlcomp_buffer *buf,
    const struct xv6_d3d12_present_contract *present)
{
    struct fb_gpu_dxg_present_source_register reg;
    struct fb_gpu_dxg_present_source_commit commit;
    struct xv6_d3d12_present_source_record *source_record;
    uint32_t source_format;
    uint32_t adapter_luid_low;
    uint32_t adapter_luid_high;
    uint32_t provenance_flags;

    if (!buf || !present || present->allocation_count == 0 ||
        !present->allocations)
        return;

    g_d3d12_dxg_present_register_errno = 0;
    g_d3d12_dxg_present_commit_errno = 0;
    g_d3d12_dxg_present_source = 0;
    g_d3d12_dxg_present_id = 0;
    g_d3d12_dxg_present_completed = 0;
    source_record = xv6_d3d12_get_present_source_record(buf, 1);
    if (source_record) {
        source_record->register_errno = 0;
        source_record->present_source = 0;
        source_record->register_flags = 0;
        source_record->adapter_luid_low = 0;
        source_record->adapter_luid_high = 0;
        source_record->provenance_flags = 0;
        source_record->commit_errno = 0;
        source_record->commit_status = 0;
        source_record->commit_expected_eopnotsupp = 0;
        source_record->present_id = 0;
        source_record->completed = 0;
        source_record->query_attempts = 0;
        source_record->query_successes = 0;
        source_record->query_errno = 0;
        source_record->display_target_kind = FB_GPU_DXG_DISPLAY_TARGET_NONE;
        source_record->dirty_rect_count = 0;
        source_record->dirty_sequence = 0;
        source_record->completion_correlated = 0;
    }

    source_format = xv6_d3d12_present_source_format(present->format);
    adapter_luid_low = 0;
    adapter_luid_high = 0;
    provenance_flags = 0;
    if (present->source_luid_valid &&
        (present->source_luid_a != 0 || present->source_luid_b != 0)) {
        adapter_luid_low = present->source_luid_a;
        adapter_luid_high = present->source_luid_b;
    } else if (present->matched_luid_a != 0 || present->matched_luid_b != 0) {
        adapter_luid_low = present->matched_luid_a;
        adapter_luid_high = present->matched_luid_b;
    }

    memset(&reg, 0, sizeof(reg));
    reg.dxg_fd = buf->d3d12_dxg_fd;
    reg.resource_fd = buf->d3d12_resource_fd;
    reg.device = present->device;
    reg.resource = present->resource;
    reg.allocation = present->allocations[0];
    reg.allocation_count = present->allocation_count;
    reg.width = (uint32_t)buf->width;
    reg.height = (uint32_t)buf->height;
    reg.pitch = (uint32_t)buf->width * 4U;
    reg.format = source_format;
    reg.modifier = DRM_FORMAT_MOD_LINEAR;
    reg.adapter_luid_low = adapter_luid_low;
    reg.adapter_luid_high = adapter_luid_high;
    if (reg.dxg_fd >= 0)
        provenance_flags |= FB_GPU_DXG_PRESENT_PROV_DXG_FD;
    if (reg.resource_fd >= 0)
        provenance_flags |= FB_GPU_DXG_PRESENT_PROV_RESOURCE_FD;
    if (reg.device != 0 && reg.resource != 0 && reg.allocation != 0 &&
        reg.allocation_count != 0)
        provenance_flags |= FB_GPU_DXG_PRESENT_PROV_D3DKMT_HANDLES;
    if (reg.width != 0 && reg.height != 0 && reg.pitch != 0 &&
        reg.format != 0)
        provenance_flags |= FB_GPU_DXG_PRESENT_PROV_DIMENSIONS;
    if (reg.adapter_luid_low != 0 || reg.adapter_luid_high != 0)
        provenance_flags |= FB_GPU_DXG_PRESENT_PROV_ADAPTER_LUID;
    reg.provenance_flags = provenance_flags;
    reg.flags = 0;
    if (source_record) {
        source_record->register_flags = reg.flags;
        source_record->adapter_luid_low = reg.adapter_luid_low;
        source_record->adapter_luid_high = reg.adapter_luid_high;
        source_record->provenance_flags = reg.provenance_flags;
    }

    g_d3d12_dxg_present_register_attempt_count++;
    if (source_record)
        source_record->register_attempts++;
    if (ioctl(g_fb_fd, FB_GPU_DXG_PRESENT_SOURCE_REGISTER, &reg) != 0) {
        g_d3d12_dxg_present_register_errno = errno ? errno : EIO;
        if (source_record) {
            source_record->register_errno =
                g_d3d12_dxg_present_register_errno;
            source_record->commit_status =
                g_d3d12_dxg_present_register_errno;
        }
        snprintf(g_d3d12_present_missing, sizeof(g_d3d12_present_missing),
                 "FB_GPU_DXG_PRESENT_SOURCE_REGISTER failed errno=%d (%s)",
                 g_d3d12_dxg_present_register_errno,
                 strerror(g_d3d12_dxg_present_register_errno));
        fprintf(stderr,
                "wlcomp: d3d12 dxg present-source register failed attempts=%lu errno=%d (%s) dxg_fd=%d resource_fd=%d device=0x%x resource=0x%x allocation=0x%x allocations=%u size=%ux%u pitch=%u format=0x%x adapter_luid=%08x:%08x provenance=0x%x flags=0x%x display_handoff=0\n",
                (unsigned long)g_d3d12_dxg_present_register_attempt_count,
                g_d3d12_dxg_present_register_errno,
                strerror(g_d3d12_dxg_present_register_errno), reg.dxg_fd,
                reg.resource_fd, reg.device, reg.resource, reg.allocation,
                reg.allocation_count, reg.width, reg.height, reg.pitch,
                reg.format, reg.adapter_luid_high, reg.adapter_luid_low,
                reg.provenance_flags, reg.flags);
        return;
    }

    g_d3d12_dxg_present_register_success_count++;
    g_d3d12_dxg_present_source = reg.present_source;
    if (source_record) {
        source_record->register_successes++;
        source_record->register_errno = 0;
        source_record->present_source = reg.present_source;
    }

    memset(&commit, 0, sizeof(commit));
    commit.present_source = reg.present_source;
    commit.flags = FB_GPU_DXG_PRESENT_F_WAIT_SYNC;
    commit.sync_object = present->fence;
    commit.fence_value = present->fence_target;
    g_d3d12_dxg_present_commit_attempt_count++;
    if (source_record) {
        source_record->commit_attempts++;
        source_record->commit_expected_eopnotsupp = 1;
    }
    if (ioctl(g_fb_fd, FB_GPU_DXG_PRESENT_SOURCE_COMMIT, &commit) == 0) {
        g_d3d12_dxg_present_commit_success_count++;
        g_d3d12_dxg_present_id = commit.present_id;
        g_d3d12_dxg_present_completed = commit.completed;
        g_d3d12_dxg_present_commit_errno = 0;
        if (source_record) {
            source_record->commit_successes++;
            source_record->commit_errno = 0;
            source_record->commit_status = 0;
            source_record->commit_expected_eopnotsupp = 0;
            source_record->present_id = commit.present_id;
            source_record->completed = commit.completed;
            source_record->display_target_kind =
                FB_GPU_DXG_DISPLAY_TARGET_NONE;
            source_record->dirty_rect_count = 0;
            source_record->dirty_sequence = 0;
            source_record->completion_correlated = 0;
        }
        fprintf(stderr,
                "wlcomp: d3d12 dxg present-source commit completed source=0x%x present_id=%lu completed=%lu target_kind=query-required target_raw=0 dirty_rects=0 dirty_sequence=0 display_handoff=1 no_cpu_readback=1\n",
                commit.present_source, (unsigned long)commit.present_id,
                (unsigned long)commit.completed);
        return;
    }

    g_d3d12_dxg_present_commit_errno = errno ? errno : EIO;
    if (source_record) {
        source_record->commit_errno = g_d3d12_dxg_present_commit_errno;
        source_record->commit_status = g_d3d12_dxg_present_commit_errno;
        source_record->commit_expected_eopnotsupp =
            g_d3d12_dxg_present_commit_errno == EOPNOTSUPP;
        source_record->present_id = 0;
        source_record->completed = 0;
        source_record->completion_correlated = 0;
    }
    snprintf(g_d3d12_present_missing, sizeof(g_d3d12_present_missing),
             "FB_GPU_DXG_PRESENT_SOURCE_COMMIT failed errno=%d (%s), expected until host display handoff exists",
             g_d3d12_dxg_present_commit_errno,
             strerror(g_d3d12_dxg_present_commit_errno));
    fprintf(stderr,
            "wlcomp: d3d12 dxg present-source commit rejected source=0x%x attempts=%lu successes=%lu errno=%d (%s) sync=0x%x target=%lu expected_eopnotsupp=%d display_handoff=0 callbacks_blocked=1 releases_blocked=1 no_cpu_readback=1\n",
            commit.present_source,
            (unsigned long)g_d3d12_dxg_present_commit_attempt_count,
            (unsigned long)g_d3d12_dxg_present_commit_success_count,
            g_d3d12_dxg_present_commit_errno,
            strerror(g_d3d12_dxg_present_commit_errno), commit.sync_object,
            (unsigned long)commit.fence_value,
            g_d3d12_dxg_present_commit_errno == EOPNOTSUPP);
}

static int xv6_d3d12_complete_present(
    struct wlcomp_buffer *buf,
    const struct xv6_d3d12_present_contract *present,
    const struct xv6_d3d12_present_result *result)
{
    int source_display_complete;

    if (!buf || !present || !result || result->complete_sequence == 0)
        return 0;

    buf->d3d12_gpu_copy_complete = 1;
    buf->d3d12_present_evidence_pending = 1;
    buf->d3d12_gpu_present_start_time = result->start_sequence;
    buf->d3d12_gpu_copy_complete_time = result->complete_sequence;
    buf->d3d12_present_sequence = 0;
    buf->d3d12_present_fence_value = result->release_fence;
    buf->d3d12_present_dst_x = present->dst.x;
    buf->d3d12_present_dst_y = present->dst.y;
    buf->d3d12_present_dst_w = present->dst.w;
    buf->d3d12_present_dst_h = present->dst.h;
    buf->d3d12_present_src_x = present->src.x;
    buf->d3d12_present_src_y = present->src.y;
    buf->d3d12_present_src_w = present->src.w;
    buf->d3d12_present_src_h = present->src.h;
    g_d3d12_present_copy_complete_count++;
    xv6_d3d12_probe_dxg_present_source_handoff(buf, present);
    xv6_d3d12_refresh_present_source_completion(buf);
    source_display_complete =
        xv6_d3d12_present_source_has_display_completion(buf);
    fprintf(stderr,
            "wlcomp: d3d12 gpu copy complete pending display handoff d3d12_gpu_copy_composite_completes=%lu d3d12_gpu_copy_completes=%lu copy_complete=%lu copy_target=compositor-owned-d3d12-texture display_handoff=%d resource=0x%x allocation_count=%u fence=0x%x target=%lu dst=%d,%d src=%d,%d %ux%u no_cpu_readback=1\n",
            (unsigned long)g_d3d12_present_copy_complete_count,
            (unsigned long)g_d3d12_present_copy_complete_count,
            (unsigned long)result->complete_sequence,
            source_display_complete, present->resource,
            present->allocation_count, present->fence,
            (unsigned long)present->fence_target,
            present->dst.x, present->dst.y, present->src.x, present->src.y,
            present->src.w, present->src.h);
    if (!source_display_complete &&
        !xv6_d3d12_present_source_completion_pending(buf))
        xv6_d3d12_write_fail_closed_display_handoff_evidence(buf);
    return 1;
}

static void xv6_d3d12_finish_present_after_display(struct wlcomp_buffer *buf)
{
    struct xv6_d3d12_present_contract present;
    struct xv6_d3d12_present_result result;
    struct xv6_d3d12_present_source_record *source_record;
    int present_errno = EINVAL;

    if (!buf || !buf->is_d3d12_shared ||
        !buf->d3d12_present_evidence_pending ||
        buf->d3d12_present_complete)
        return;

    memset(&present, 0, sizeof(present));
    memset(&result, 0, sizeof(result));
    source_record = xv6_d3d12_get_present_source_record(buf, 0);
    present.adapter = buf->d3d12_adapter;
    present.device = buf->d3d12_device;
    present.resource = buf->d3d12_opened_resource;
    present.allocation_count = buf->d3d12_opened_allocation_count;
    present.allocations = buf->d3d12_opened_allocations;
    present.fence = buf->d3d12_opened_fence;
    present.fence_target = buf->d3d12_fence_value;
    present.format = buf->format;
    present.source_luid_a = buf->d3d12_source_luid_a;
    present.source_luid_b = buf->d3d12_source_luid_b;
    present.source_luid_valid = buf->d3d12_source_luid_valid;
    present.matched_luid_a = buf->d3d12_matched_luid_a;
    present.matched_luid_b = buf->d3d12_matched_luid_b;
    present.wait_confirmed = buf->d3d12_present_wait_confirmed;
    present.total_priv_size = buf->d3d12_total_priv_size;
    present.dst.x = buf->d3d12_present_dst_x;
    present.dst.y = buf->d3d12_present_dst_y;
    present.dst.w = buf->d3d12_present_dst_w;
    present.dst.h = buf->d3d12_present_dst_h;
    present.src.x = buf->d3d12_present_src_x;
    present.src.y = buf->d3d12_present_src_y;
    present.src.w = buf->d3d12_present_src_w;
    present.src.h = buf->d3d12_present_src_h;
    xv6_d3d12_fill_present_identity(&present, buf);

    result.start_sequence = buf->d3d12_gpu_present_start_time;
    result.copy_sequence = buf->d3d12_gpu_copy_complete_time;
    if (!buf->d3d12_gpu_copy_complete || result.start_sequence == 0 ||
        result.copy_sequence == 0) {
        xv6_d3d12_set_missing(
            "D3D12 display completion requires prior GPU copy/composite completion");
        present_errno = EPROTO;
        goto reject_display_completion;
    }
    if (present.resource == 0 || present.allocation_count == 0 ||
        !present.allocations || present.fence == 0 ||
        present.fence_target == 0 || !present.wait_confirmed) {
        xv6_d3d12_set_missing(
            "D3D12 display completion requires shared-resource import, monitored fence import, and confirmed fence target");
        present_errno = EPROTO;
        goto reject_display_completion;
    }
    for (uint32_t i = 0; i < present.allocation_count; i++) {
        if (present.allocations[i] == 0) {
            xv6_d3d12_set_missing(
                "D3D12 display completion requires nonzero imported allocation handles");
            present_errno = EPROTO;
            goto reject_display_completion;
        }
    }
    if (!xv6_d3d12_present_same_luid(&present)) {
        xv6_d3d12_set_missing(
            "D3D12 display completion requires matching client and compositor adapter LUIDs");
        present_errno = ENODEV;
        goto reject_display_completion;
    }
    if (!source_record || !source_record->register_successes) {
        if (source_record && source_record->register_errno)
            present_errno = source_record->register_errno;
        snprintf(g_d3d12_present_missing, sizeof(g_d3d12_present_missing),
                 "D3D12 display completion requires per-buffer DXG present-source register success register_successes=%lu errno=%d",
                 (unsigned long)(source_record ?
                     source_record->register_successes : 0),
                 source_record ? source_record->register_errno : 0);
        goto reject_display_completion;
    }
    if (!source_record->commit_attempts) {
        xv6_d3d12_set_missing(
            "D3D12 display completion requires a per-buffer DXG present-source commit attempt");
        present_errno = ENOSYS;
        goto reject_display_completion;
    }
    if (!source_record->commit_successes) {
        present_errno = source_record->commit_errno ?
            source_record->commit_errno : ENOSYS;
        snprintf(g_d3d12_present_missing, sizeof(g_d3d12_present_missing),
                 "D3D12 display completion requires per-buffer DXG present-source commit success commit_errno=%d (%s)",
                 source_record->commit_errno,
                 strerror(source_record->commit_errno));
        goto reject_display_completion;
    }
    if (source_record->present_id == 0 || source_record->completed == 0) {
        if (source_record->present_id == 0) {
            xv6_d3d12_set_missing(
                "D3D12 display completion requires nonzero DXG present_id");
            present_errno = EPROTO;
            goto reject_display_completion;
        }
        xv6_d3d12_refresh_present_source_completion(buf);
        xv6_d3d12_fill_present_identity(&present, buf);
        if (source_record->completed == 0 &&
            source_record->query_errno != 0 &&
            source_record->query_errno != EAGAIN) {
            snprintf(g_d3d12_present_missing,
                     sizeof(g_d3d12_present_missing),
                     "D3D12 display completion query failed before any completed counter was observed errno=%d (%s)",
                     source_record->query_errno,
                     strerror(source_record->query_errno));
            present_errno = source_record->query_errno;
            goto reject_display_completion;
        }
        if (source_record->completed == 0) {
            snprintf(g_d3d12_present_missing,
                     sizeof(g_d3d12_present_missing),
                     "D3D12 display completion pending present_id=%lu completed=0 query_attempts=%lu query_errno=%d",
                     (unsigned long)source_record->present_id,
                     (unsigned long)source_record->query_attempts,
                     source_record->query_errno);
            xv6_d3d12_write_present_evidence(&present, &result, 1,
                                             EAGAIN);
            fprintf(stderr,
                    "wlcomp: d3d12 display completion pending source=0x%x present_id=%lu completed=0 query_attempts=%lu callbacks_blocked=1 releases_blocked=1 no_cpu_readback=1\n",
                    source_record->present_source,
                    (unsigned long)source_record->present_id,
                    (unsigned long)source_record->query_attempts);
            return;
        }
    }
    xv6_d3d12_refresh_present_source_completion(buf);
    xv6_d3d12_fill_present_identity(&present, buf);
    if (source_record->completed < source_record->present_id) {
        if (source_record->query_errno != 0 &&
            source_record->query_errno != EAGAIN) {
            snprintf(g_d3d12_present_missing,
                     sizeof(g_d3d12_present_missing),
                     "D3D12 display completion query failed for present_id=%lu completed=%lu errno=%d (%s)",
                     (unsigned long)source_record->present_id,
                     (unsigned long)source_record->completed,
                     source_record->query_errno,
                     strerror(source_record->query_errno));
            present_errno = source_record->query_errno;
            goto reject_display_completion;
        }
        snprintf(g_d3d12_present_missing, sizeof(g_d3d12_present_missing),
                 "D3D12 display completion requires current completed counter to cover present_id present_id=%lu completed=%lu",
                 (unsigned long)source_record->present_id,
                 (unsigned long)source_record->completed);
        xv6_d3d12_write_present_evidence(&present, &result, 1, EAGAIN);
        fprintf(stderr,
                "wlcomp: d3d12 display completion pending source=0x%x present_id=%lu completed=%lu query_attempts=%lu query_successes=%lu callbacks_blocked=1 releases_blocked=1 no_cpu_readback=1\n",
                source_record->present_source,
                (unsigned long)source_record->present_id,
                (unsigned long)source_record->completed,
                (unsigned long)source_record->query_attempts,
                (unsigned long)source_record->query_successes);
        return;
    }
    if (source_record->display_target_kind ==
        FB_GPU_DXG_DISPLAY_TARGET_NONE) {
        xv6_d3d12_set_missing(
            "D3D12 display completion requires a kernel-reported non-readback display target kind");
        present_errno = EPROTO;
        goto reject_display_completion;
    }
    if (!xv6_d3d12_display_target_is_runtime_resource(
            source_record->display_target_kind)) {
        snprintf(g_d3d12_present_missing, sizeof(g_d3d12_present_missing),
                 "D3D12 native present requires a runtime-created D3D12 COM-resource display target; kernel target_kind=%s dirty_rects=%u dirty_sequence=%lu is non-readback evidence but not native D3D12 present",
                 xv6_d3d12_display_target_kind_name(
                     source_record->display_target_kind),
                 source_record->dirty_rect_count,
                 (unsigned long)source_record->dirty_sequence);
        present_errno = EOPNOTSUPP;
        goto reject_display_completion;
    }
    if (source_record->dirty_rect_count == 0 ||
        source_record->dirty_sequence == 0) {
        snprintf(g_d3d12_present_missing, sizeof(g_d3d12_present_missing),
                 "D3D12 display completion requires kernel dirty-rect evidence target_kind=%s dirty_rects=%u dirty_sequence=%lu",
                 xv6_d3d12_display_target_kind_name(
                     source_record->display_target_kind),
                 source_record->dirty_rect_count,
                 (unsigned long)source_record->dirty_sequence);
        present_errno = EPROTO;
        goto reject_display_completion;
    }
    if (source_record->dirty_sequence > source_record->completed) {
        snprintf(g_d3d12_present_missing, sizeof(g_d3d12_present_missing),
                 "D3D12 display completion requires dirty sequence to be covered by display completion dirty_sequence=%lu completed=%lu",
                 (unsigned long)source_record->dirty_sequence,
                 (unsigned long)source_record->completed);
        xv6_d3d12_write_present_evidence(&present, &result, 1, EAGAIN);
        fprintf(stderr,
                "wlcomp: d3d12 dirty/display completion pending target_kind=%s dirty_sequence=%lu completed=%lu callbacks_blocked=1 releases_blocked=1 no_cpu_readback=1\n",
                xv6_d3d12_display_target_kind_name(
                    source_record->display_target_kind),
                (unsigned long)source_record->dirty_sequence,
                (unsigned long)source_record->completed);
        return;
    }
    if (xv6_d3d12_counter_delta(g_d3d12_cpu_map_reject_count,
                                present.cpu_map_rejects_at_present) != 0 ||
        xv6_d3d12_counter_delta(g_d3d12_cpu_plane_reject_count,
                                present.cpu_plane_rejects_at_present) != 0) {
        g_d3d12_phase2_cpu_mappable_fallback_rejected_current = 1;
        snprintf(g_d3d12_present_missing, sizeof(g_d3d12_present_missing),
                 "D3D12 native present evidence rejected because CPU map/readback was attempted during the present sequence map_delta=%lu plane_delta=%lu",
                 (unsigned long)xv6_d3d12_counter_delta(
                     g_d3d12_cpu_map_reject_count,
                     present.cpu_map_rejects_at_present),
                 (unsigned long)xv6_d3d12_counter_delta(
                     g_d3d12_cpu_plane_reject_count,
                     present.cpu_plane_rejects_at_present));
        present_errno = EPROTO;
        goto reject_display_completion;
    }
    source_record->completion_correlated = 1;
    xv6_d3d12_fill_present_identity(&present, buf);

    result.complete_sequence = get_time_us();
    result.release_fence = source_record->completed;
    if (result.release_fence == 0) {
        xv6_d3d12_set_missing(
            "D3D12 display completion requires a nonzero DXG present-source completion fence");
        present_errno = EPROTO;
        goto reject_display_completion;
    }
    buf->d3d12_present_sequence = result.complete_sequence;
    buf->d3d12_present_fence_value = result.release_fence;
    buf->d3d12_present_complete = 1;
    buf->d3d12_present_evidence_pending = 0;
    g_d3d12_present_display_complete_count++;
    g_d3d12_native_present_completion_count++;
    buf->d3d12_native_present_completion_id =
        g_d3d12_native_present_completion_count;
    g_d3d12_last_native_present_completion_id =
        buf->d3d12_native_present_completion_id;
    xv6_d3d12_note_identity_present_completion(buf);
    xv6_d3d12_fill_present_identity(&present, buf);
    xv6_d3d12_write_present_evidence(&present, &result, 0, 0);
    fprintf(stderr,
                "wlcomp: d3d12 dxg runtime-resource display completion observed d3d12_native_present_completions=%lu d3d12_native_present_completion_id=%lu d3d12_gpu_present_completes=%lu d3d12_gpu_present_complete=%lu client_pid=%d client_buffer_id=%u buffer_generation=%lu resource=0x%x copy_complete=%lu present_source=0x%x present_id=%lu completed=%lu target_kind=%s target_raw=%u dirty_rects=%u dirty_sequence=%lu release_fence=%lu callbacks_unblocked=1 releases_unblocked=1 no_cpu_readback=1 no_cpu_map=1 synthvid_sysmem_fallback=0\n",
            (unsigned long)g_d3d12_native_present_completion_count,
            (unsigned long)buf->d3d12_native_present_completion_id,
            (unsigned long)g_d3d12_present_display_complete_count,
            (unsigned long)result.complete_sequence,
            (int)buf->d3d12_client_pid, buf->d3d12_client_buffer_id,
            (unsigned long)buf->d3d12_buffer_generation,
            present.resource,
            (unsigned long)buf->d3d12_gpu_copy_complete_time,
            source_record->present_source,
            (unsigned long)source_record->present_id,
            (unsigned long)source_record->completed,
            xv6_d3d12_display_target_kind_name(
                source_record->display_target_kind),
            source_record->display_target_kind,
            source_record->dirty_rect_count,
            (unsigned long)source_record->dirty_sequence,
            (unsigned long)result.release_fence);
    fprintf(stderr,
            "wlcomp: d3d12_wayland_present_fps_provenance_matrix "
            "resource_buffer_admitted=1 syncfile_acquire=PASS "
            "gpu_copy_complete=1 display_handoff_implemented=1 "
            "present_id=%lu completed=%lu callbacks_blocked=0 "
            "releases_blocked=0 fps_credit=1 native_present_claim=1 "
            "opengl_submit_credit=0 status=PASS\n",
            (unsigned long)source_record->present_id,
            (unsigned long)source_record->completed);
    return;

reject_display_completion:
    buf->d3d12_present_evidence_pending = 0;
    g_d3d12_present_reject_count++;
    xv6_d3d12_note_identity_present_reject(buf);
    xv6_d3d12_write_present_evidence(&present, &result, 1, present_errno);
    fprintf(stderr,
            "wlcomp: d3d12 display completion rejected fail_closed=1 rejects=%lu missing='%s' resource=0x%x allocation_count=%u fence=0x%x target=%lu wait_confirmed=%d copy_complete=%d start=%lu copy=%lu errno=%d (%s) callbacks_blocked=1 releases_blocked=1 no_cpu_readback=1\n",
            (unsigned long)g_d3d12_present_reject_count,
            g_d3d12_present_missing[0] ? g_d3d12_present_missing : "unknown",
            present.resource, present.allocation_count, present.fence,
            (unsigned long)present.fence_target, present.wait_confirmed,
            buf->d3d12_gpu_copy_complete,
            (unsigned long)result.start_sequence,
            (unsigned long)result.copy_sequence, present_errno,
            strerror(present_errno));
    xv6_d3d12_unblock_failclosed_client(buf, "display-completion-rejected");
}

static void xv6_d3d12_complete_pending_presents_after_display(void)
{
    struct wlcomp_surface *surf;

    wl_list_for_each(surf, &g_surfaces, link) {
        if (surf->committed_buf)
            xv6_d3d12_finish_present_after_display(surf->committed_buf);
    }
}

static int xv6_d3d12_present_imported_buffer(struct wlcomp_surface *surf,
                                             struct wlcomp_buffer *buf,
                                             int32_t dst_x, int32_t dst_y,
                                             int32_t src_x, int32_t src_y,
                                             uint32_t width,
                                             uint32_t height)
{
    static int unimplemented_logs;
    static int wait_logs;
    struct xv6_d3d12_present_contract present;
    struct xv6_d3d12_present_result result;
    uint64_t current = 0;
    int present_errno;

    (void)surf;
    if (!buf || !buf->is_d3d12_shared)
        return 0;
    if (buf->d3d12_failclosed_client_unblocked) {
        static int failclosed_logs;

        if (failclosed_logs < 8) {
            fprintf(stderr,
                    "wlcomp: d3d12 present skipped after fail-closed unblock resource=0x%x reason=lifecycle-drained native_present_claim=0 opengl_submit_credit=0\n",
                    buf->d3d12_opened_resource);
            failclosed_logs++;
        }
        return 0;
    }
    if (buf->d3d12_gpu_copy_complete && !buf->d3d12_present_complete)
        return 1;
    if (!xv6_d3d12_poll_fence_ready(buf, &current)) {
        if (wait_logs < 8) {
            fprintf(stderr,
                    "wlcomp: d3d12 native present waiting resource=0x%x fence=0x%x target=%lu current=%lu dst=%d,%d src=%d,%d %ux%u\n",
                    buf->d3d12_opened_resource, buf->d3d12_opened_fence,
                    (unsigned long)buf->d3d12_fence_value,
                    (unsigned long)current, dst_x, dst_y, src_x, src_y,
                    width, height);
            wait_logs++;
        }
        xv6_d3d12_write_waiting_fence_present_evidence(
            buf, current, dst_x, dst_y, src_x, src_y, width, height);
        return 0;
    }
    if (!xv6_d3d12_import_fence_confirmed(buf, &current)) {
        xv6_d3d12_write_waiting_fence_present_evidence(
            buf, current, dst_x, dst_y, src_x, src_y, width, height);
        return 0;
    }

    g_d3d12_native_present_attempt_count++;
    buf->d3d12_native_present_attempt_id =
        g_d3d12_native_present_attempt_count;
    buf->d3d12_native_present_completion_id = 0;
    buf->d3d12_native_present_reject_recorded = 0;
    buf->d3d12_fb_blit_attempts_at_present =
        g_fb_blit_present_attempt_count;
    buf->d3d12_fb_blit_successes_at_present =
        g_fb_blit_present_success_count;
    buf->d3d12_cpu_map_rejects_at_present =
        g_d3d12_cpu_map_reject_count;
    buf->d3d12_cpu_plane_rejects_at_present =
        g_d3d12_cpu_plane_reject_count;
    g_d3d12_last_native_present_attempt_id =
        buf->d3d12_native_present_attempt_id;
    xv6_d3d12_note_identity_present_attempt(buf);

    memset(&present, 0, sizeof(present));
    memset(&result, 0, sizeof(result));
    present.adapter = buf->d3d12_adapter;
    present.device = buf->d3d12_device;
    present.resource = buf->d3d12_opened_resource;
    present.allocation_count = buf->d3d12_opened_allocation_count;
    present.allocations = buf->d3d12_opened_allocations;
    present.fence = buf->d3d12_opened_fence;
    present.fence_target = buf->d3d12_fence_value;
    present.fence_current = current;
    present.format = buf->format;
    present.source_luid_a = buf->d3d12_source_luid_a;
    present.source_luid_b = buf->d3d12_source_luid_b;
    present.source_luid_valid = buf->d3d12_source_luid_valid;
    present.matched_luid_a = buf->d3d12_matched_luid_a;
    present.matched_luid_b = buf->d3d12_matched_luid_b;
    present.wait_confirmed = buf->d3d12_present_wait_confirmed;
    present.total_priv_size = buf->d3d12_total_priv_size;
    present.dst.x = dst_x;
    present.dst.y = dst_y;
    present.dst.w = width;
    present.dst.h = height;
    present.src.x = src_x;
    present.src.y = src_y;
    present.src.w = width;
    present.src.h = height;
    xv6_d3d12_fill_present_identity(&present, buf);

    if (!xv6_d3d12_present_same_luid(&present)) {
        xv6_d3d12_set_missing(
            "D3D12 native present requires a nonzero client source LUID matching the compositor-opened adapter LUID");
        errno = ENODEV;
        goto reject_present;
    }

    if (xv6_d3d12_submit_present_contract(buf, &present, &result) == 0)
        return xv6_d3d12_complete_present(buf, &present, &result);

reject_present:
    present_errno = errno ? errno : EIO;
    g_d3d12_present_reject_count++;
    xv6_d3d12_note_identity_present_reject(buf);
    xv6_d3d12_write_present_evidence(&present, &result, 1, present_errno);
    if (unimplemented_logs < 16) {
        fprintf(stderr,
                "wlcomp: d3d12 gpu-copy present failed strict d3d12_native_present_unimplemented=1 rejects=%lu missing='%s' resource=0x%x allocation_count=%u allocations0=0x%x total_priv=%u fence=0x%x target=%lu current=%lu format=0x%x source_luid_valid=%d same_luid=%d source_luid=%08x:%08x matched_luid=%08x:%08x dst=%d,%d %ux%u src=%d,%d %ux%u wait_confirmed=%d errno=%d (%s) callbacks_blocked=1 releases_blocked=1 no_cpu_readback=1\n",
                (unsigned long)g_d3d12_present_reject_count,
                g_d3d12_present_missing[0] ? g_d3d12_present_missing :
                "unknown",
                present.resource, present.allocation_count,
                present.allocation_count ? present.allocations[0] : 0,
                present.total_priv_size, present.fence,
                (unsigned long)present.fence_target,
                (unsigned long)present.fence_current, present.format,
                present.source_luid_valid,
                xv6_d3d12_present_same_luid(&present),
                present.source_luid_b, present.source_luid_a,
                present.matched_luid_b, present.matched_luid_a,
                present.dst.x, present.dst.y, present.dst.w, present.dst.h,
                present.src.x, present.src.y, present.src.w, present.src.h,
                buf->d3d12_present_wait_confirmed, present_errno,
                strerror(present_errno));
        unimplemented_logs++;
    }
    xv6_d3d12_unblock_failclosed_client(buf, "present-submit-rejected");
    return 0;
}

static int xv6_d3d12_open_syncfile_fence(struct wlcomp_buffer *buf,
                                         int fence_fd)
{
    struct d3dkmt_opensyncobjectfromsyncfile open_sync;
    struct d3dkmt_destroysynchronizationobject destroy_sync;
    int saved_errno;

    if (!buf || fence_fd < 0 || buf->d3d12_dxg_fd < 0 ||
        buf->d3d12_device == 0) {
        errno = EINVAL;
        xv6_d3d12_set_missing("invalid D3D12 monitored fence import arguments");
        return -1;
    }
    memset(&open_sync, 0, sizeof(open_sync));
    open_sync.device.v = buf->d3d12_device;
    open_sync.sync_file_handle = (uint64_t)fence_fd;
    g_d3d12_syncfile_fence_import_attempt_count++;
    fprintf(stderr,
            "wlcomp: d3d12 sync-file acquire import attempted d3d12_syncfile_fence_import_attempts=%lu fence_fd=%d resource=0x%x device=0x%x\n",
            (unsigned long)g_d3d12_syncfile_fence_import_attempt_count,
            fence_fd, buf->d3d12_opened_resource, buf->d3d12_device);
    if (ioctl(buf->d3d12_dxg_fd, LX_DXOPENSYNCOBJECTFROMSYNCFILE,
              &open_sync) < 0)
        return -1;
    if (open_sync.syncobj.v == 0 || open_sync.fence_value_cpu_va == 0 ||
        open_sync.fence_value_gpu_va == 0) {
        saved_errno = EINVAL;
        if (open_sync.syncobj.v != 0) {
            memset(&destroy_sync, 0, sizeof(destroy_sync));
            destroy_sync.sync_object = open_sync.syncobj;
            ioctl(buf->d3d12_dxg_fd, LX_DXDESTROYSYNCHRONIZATIONOBJECT,
                  &destroy_sync);
        }
        errno = saved_errno;
        fprintf(stderr,
                "wlcomp: d3d12 sync-file acquire import rejected incomplete_result=1 fence_fd=%d fence=0x%x cpu_va=0x%lx gpu_va=0x%lx target=%lu\n",
                fence_fd, open_sync.syncobj.v,
                (unsigned long)open_sync.fence_value_cpu_va,
                (unsigned long)open_sync.fence_value_gpu_va,
                (unsigned long)open_sync.fence_value);
        return -1;
    }
    if (buf->d3d12_fence_value_valid &&
        buf->d3d12_fence_value != open_sync.fence_value) {
        memset(&destroy_sync, 0, sizeof(destroy_sync));
        destroy_sync.sync_object = open_sync.syncobj;
        ioctl(buf->d3d12_dxg_fd, LX_DXDESTROYSYNCHRONIZATIONOBJECT,
              &destroy_sync);
        errno = EINVAL;
        fprintf(stderr,
                "wlcomp: d3d12 sync-file acquire import rejected target_mismatch=1 fence_fd=%d fence=0x%x protocol_target=%lu syncfile_target=%lu\n",
                fence_fd, open_sync.syncobj.v,
                (unsigned long)buf->d3d12_fence_value,
                (unsigned long)open_sync.fence_value);
        return -1;
    }

    buf->d3d12_opened_fence = open_sync.syncobj.v;
    buf->d3d12_fence_cpu_va = open_sync.fence_value_cpu_va;
    buf->d3d12_fence_gpu_va = open_sync.fence_value_gpu_va;
    buf->d3d12_fence_value = open_sync.fence_value;
    buf->d3d12_fence_value_valid = 1;
    buf->d3d12_fence_imported_from_syncfile = 1;
    g_d3d12_syncfile_fence_import_success_count++;
    g_d3d12_fence_import_success_count++;
    fprintf(stderr,
            "wlcomp: d3d12 sync-file acquire import succeeded d3d12_syncfile_fence_import_successes=%lu fence_fd=%d fence=0x%x target=%lu cpu_va=0x%lx gpu_va=0x%lx\n",
            (unsigned long)g_d3d12_syncfile_fence_import_success_count,
            fence_fd, buf->d3d12_opened_fence,
            (unsigned long)buf->d3d12_fence_value,
            (unsigned long)buf->d3d12_fence_cpu_va,
            (unsigned long)buf->d3d12_fence_gpu_va);
    fprintf(stderr,
            "wlcomp: d3d12_wayland_dxg_syncfile_acquire_matrix syncfile_open=PASS acquire_import=dxg-syncfile resource_open=PASS same_adapter_luid=PASS opened_fence=0x%x fence_value=%lu cpu_va=0x%lx gpu_va=0x%lx present_admission=failclosed native_present_claim=0 opengl_submit_credit=0 status=PASS\n",
            buf->d3d12_opened_fence,
            (unsigned long)buf->d3d12_fence_value,
            (unsigned long)buf->d3d12_fence_cpu_va,
            (unsigned long)buf->d3d12_fence_gpu_va);
    xv6_d3d12_write_stage_evidence("syncfile_fence_import_succeeded", 0);
    return 0;
}

static int xv6_d3d12_open_nt_handle_fence(struct wlcomp_buffer *buf,
                                          int fence_fd)
{
    struct d3dkmt_opensyncobjectfromnthandle2 open_sync;

    memset(&open_sync, 0, sizeof(open_sync));
    open_sync.device.v = buf->d3d12_device;
    open_sync.nt_handle = (uint64_t)fence_fd;
    open_sync.flags.shared = 1;
    open_sync.flags.nt_security_sharing = 1;
    fprintf(stderr,
            "wlcomp: d3d12 NT-handle fence import fallback attempted fence_fd=%d resource=0x%x device=0x%x\n",
            fence_fd, buf->d3d12_opened_resource, buf->d3d12_device);
    xv6_d3d12_write_stage_evidence("fence_import_attempted", 0);
    if (ioctl(buf->d3d12_dxg_fd, LX_DXOPENSYNCOBJECTFROMNTHANDLE2,
              &open_sync) < 0) {
        int open_errno = errno ? errno : EIO;

        snprintf(g_d3d12_present_missing, sizeof(g_d3d12_present_missing),
                 "DXG OpenSyncObjectFromNtHandle2 ioctl failed for D3D12 fence fd=%d errno=%d",
                 fence_fd, open_errno);
        errno = open_errno;
        return -1;
    }
    if (open_sync.sync_object.v == 0) {
        errno = EIO;
        xv6_d3d12_set_missing(
            "DXG OpenSyncObjectFromNtHandle2 returned no D3D12 fence object");
        return -1;
    }
    if (open_sync.monitored_fence.fence_value_cpu_va == 0) {
        struct d3dkmt_destroysynchronizationobject destroy_sync;

        memset(&destroy_sync, 0, sizeof(destroy_sync));
        destroy_sync.sync_object = open_sync.sync_object;
        ioctl(buf->d3d12_dxg_fd, LX_DXDESTROYSYNCHRONIZATIONOBJECT,
              &destroy_sync);
        errno = EINVAL;
        snprintf(g_d3d12_present_missing, sizeof(g_d3d12_present_missing),
                 "D3D12 fence fd opened but not monitored fence=0x%x cpu_va=0x%lx gpu_va=0x%lx",
                 open_sync.sync_object.v,
                 (unsigned long)open_sync.monitored_fence.fence_value_cpu_va,
                 (unsigned long)open_sync.monitored_fence.fence_value_gpu_va);
        fprintf(stderr,
                "wlcomp: d3d12 native present open fence rejected opened_not_monitored=1 fence_fd=%d fence=0x%x cpu_va=0x%lx gpu_va=0x%lx\n",
                fence_fd, open_sync.sync_object.v,
                (unsigned long)open_sync.monitored_fence.fence_value_cpu_va,
                (unsigned long)open_sync.monitored_fence.fence_value_gpu_va);
        return -1;
    }
    if (open_sync.monitored_fence.fence_value_gpu_va == 0) {
        struct d3dkmt_destroysynchronizationobject destroy_sync;

        memset(&destroy_sync, 0, sizeof(destroy_sync));
        destroy_sync.sync_object = open_sync.sync_object;
        ioctl(buf->d3d12_dxg_fd, LX_DXDESTROYSYNCHRONIZATIONOBJECT,
              &destroy_sync);
        errno = EINVAL;
        snprintf(g_d3d12_present_missing, sizeof(g_d3d12_present_missing),
                 "D3D12 fence fd opened without monitored fence GPU VA fence=0x%x cpu_va=0x%lx gpu_va=0x%lx",
                 open_sync.sync_object.v,
                 (unsigned long)open_sync.monitored_fence.fence_value_cpu_va,
                 (unsigned long)open_sync.monitored_fence.fence_value_gpu_va);
        fprintf(stderr,
                "wlcomp: d3d12 native present open fence rejected missing_gpu_va=1 fence_fd=%d fence=0x%x cpu_va=0x%lx gpu_va=0x%lx\n",
                fence_fd, open_sync.sync_object.v,
                (unsigned long)open_sync.monitored_fence.fence_value_cpu_va,
                (unsigned long)open_sync.monitored_fence.fence_value_gpu_va);
        return -1;
    }
    buf->d3d12_opened_fence = open_sync.sync_object.v;
    buf->d3d12_fence_cpu_va =
        open_sync.monitored_fence.fence_value_cpu_va;
    buf->d3d12_fence_gpu_va =
        open_sync.monitored_fence.fence_value_gpu_va;
    buf->d3d12_fence_imported_from_syncfile = 0;
    g_d3d12_fence_import_success_count++;
    fprintf(stderr,
            "wlcomp: d3d12 fence import succeeded d3d12_fence_import_successes=%lu fence_fd=%d fence=0x%x cpu_va=0x%lx gpu_va=0x%lx\n",
            (unsigned long)g_d3d12_fence_import_success_count, fence_fd,
            buf->d3d12_opened_fence,
            (unsigned long)buf->d3d12_fence_cpu_va,
            (unsigned long)buf->d3d12_fence_gpu_va);
    xv6_d3d12_write_stage_evidence("fence_import_succeeded", 0);
    return 0;
}

static int xv6_d3d12_open_shared_fence(struct wlcomp_buffer *buf,
                                       int fence_fd)
{
    int syncfile_errno;

    if (!buf || fence_fd < 0 || buf->d3d12_dxg_fd < 0 ||
        buf->d3d12_device == 0) {
        errno = EINVAL;
        xv6_d3d12_set_missing("invalid D3D12 monitored fence import arguments");
        return -1;
    }

    g_d3d12_fence_import_attempt_count++;
    fprintf(stderr,
            "wlcomp: d3d12 fence import attempted d3d12_fence_import_attempts=%lu fence_fd=%d resource=0x%x device=0x%x order=syncfile-then-nt-handle\n",
            (unsigned long)g_d3d12_fence_import_attempt_count, fence_fd,
            buf->d3d12_opened_resource, buf->d3d12_device);
    xv6_d3d12_write_stage_evidence("fence_import_attempted", 0);

    if (xv6_d3d12_open_syncfile_fence(buf, fence_fd) == 0)
        return 0;
    syncfile_errno = errno ? errno : EIO;
    fprintf(stderr,
            "wlcomp: d3d12 sync-file acquire import unavailable fallback=nt-handle fence_fd=%d errno=%d (%s)\n",
            fence_fd, syncfile_errno, strerror(syncfile_errno));
    return xv6_d3d12_open_nt_handle_fence(buf, fence_fd);
}

static int xv6_d3d12_open_shared_buffer(struct wlcomp_buffer *buf,
                                        int resource_fd)
{
    struct d3dkmt_enumadapters2 enum2;
    struct d3dkmt_adapterinfo adapters[XV6_D3DKMT_ADAPTERS_MAX];
    struct d3dkmt_openadapterfromluid open_luid;
    struct d3dkmt_createdevice create_device;
    struct d3dkmt_queryresourceinfofromnthandle query;
    struct d3dkmt_openresourcefromnthandle open_resource;
    struct d3dddi_openallocationinfo2 *open_alloc = NULL;
    void *runtime_data = NULL;
    void *resource_data = NULL;
    void *total_data = NULL;
    int dxg_fd = -1;
    uint32_t adapter = 0;
    uint32_t device = 0;
    int ret = -1;
    int matched = 0;
    int saved_errno = ENODEV;
    uint32_t adapter_count;
    uint32_t first_usable = XV6_D3DKMT_ADAPTERS_MAX;

    if (!buf || resource_fd < 0) {
        errno = EINVAL;
        return -1;
    }

    dxg_fd = open("/dev/dxg", O_RDWR);
    if (dxg_fd < 0) {
        fprintf(stderr,
                "wlcomp: d3d12 native present blocked: open /dev/dxg failed errno=%d (%s)\n",
                errno, strerror(errno));
        return -1;
    }

    memset(&enum2, 0, sizeof(enum2));
    memset(adapters, 0, sizeof(adapters));
    enum2.num_adapters = XV6_D3DKMT_ADAPTERS_MAX;
    enum2.adapters = (uint64_t)adapters;
    if (ioctl(dxg_fd, LX_DXENUMADAPTERS2, &enum2) < 0 ||
        enum2.num_adapters == 0) {
        saved_errno = errno ? errno : ENODEV;
        xv6_d3d12_set_missing("DXG adapter enumeration failed for D3D12 native present import");
        fprintf(stderr,
                "wlcomp: d3d12 native present blocked: enum adapter list failed count=%u errno=%d (%s)\n",
                enum2.num_adapters, saved_errno, strerror(saved_errno));
        goto out;
    }
    adapter_count = enum2.num_adapters;
    if (adapter_count > XV6_D3DKMT_ADAPTERS_MAX) {
        fprintf(stderr,
                "wlcomp: d3d12 native present: enum adapter list returned count %u above local cap %u, clamping scan\n",
                adapter_count, XV6_D3DKMT_ADAPTERS_MAX);
        adapter_count = XV6_D3DKMT_ADAPTERS_MAX;
    }
    for (uint32_t i = 0; i < adapter_count; i++) {
        if (adapters[i].adapter_handle.v != 0) {
            first_usable = i;
            break;
        }
    }
    if (first_usable == XV6_D3DKMT_ADAPTERS_MAX) {
        saved_errno = ENODEV;
        xv6_d3d12_set_missing("DXG adapter enumeration returned no usable adapter handles");
        fprintf(stderr,
                "wlcomp: d3d12 native present blocked: enum adapter list had no usable handle count=%u requested=%u\n",
                adapter_count, XV6_D3DKMT_ADAPTERS_MAX);
        goto out;
    }
    fprintf(stderr,
            "wlcomp: d3d12 native present enum adapters2 layout=list-first requested=%u count=%u first_index=%u\n",
            XV6_D3DKMT_ADAPTERS_MAX, enum2.num_adapters, first_usable);

    memset(&query, 0, sizeof(query));
    for (uint32_t i = 0; i < adapter_count; i++) {
        struct d3dkmt_destroydevice destroy_device;
        struct d3dkmt_closeadapter close_adapter;
        char adapter_luid_text[32];

        xv6_format_winluid_text(adapter_luid_text, sizeof(adapter_luid_text),
                                adapters[i].adapter_luid);
        if (i < 8) {
            fprintf(stderr,
                    "wlcomp: d3d12 dxg adapter[%u] handle=0x%x luid=%s low=0x%08x high=0x%08x sources=%u\n",
                    i, adapters[i].adapter_handle.v, adapter_luid_text,
                    adapters[i].adapter_luid.a, adapters[i].adapter_luid.b,
                    adapters[i].num_sources);
        }
        if (adapters[i].adapter_handle.v == 0)
            continue;
        if (buf->d3d12_source_luid_valid &&
            (adapters[i].adapter_luid.a != buf->d3d12_source_luid_a ||
             adapters[i].adapter_luid.b != buf->d3d12_source_luid_b))
            continue;
        memset(&open_luid, 0, sizeof(open_luid));
        open_luid.adapter_luid = adapters[i].adapter_luid;
        if (ioctl(dxg_fd, LX_DXOPENADAPTERFROMLUID, &open_luid) < 0 ||
            open_luid.adapter_handle.v == 0)
            continue;

        memset(&create_device, 0, sizeof(create_device));
        create_device.adapter = open_luid.adapter_handle;
        if (ioctl(dxg_fd, LX_DXCREATEDEVICE, &create_device) == 0 &&
            create_device.device.v != 0) {
            memset(&query, 0, sizeof(query));
            query.device = create_device.device;
            query.nt_handle = (uint64_t)resource_fd;
            if (ioctl(dxg_fd, LX_DXQUERYRESOURCEINFOFROMNTHANDLE,
                      &query) == 0 &&
                query.allocation_count > 0 &&
                query.allocation_count <= 64 &&
                query.total_priv_drv_data_size != 0) {
                adapter = open_luid.adapter_handle.v;
                device = create_device.device.v;
                buf->d3d12_matched_luid_a = adapters[i].adapter_luid.a;
                buf->d3d12_matched_luid_b = adapters[i].adapter_luid.b;
                fprintf(stderr,
                        "wlcomp: d3d12 native present query resource ok fd=%d adapter_index=%u adapter=0x%x device=0x%x allocations=%u runtime=%u resource_priv=%u total_priv=%u source_luid_valid=%d source_luid=%08x:%08x matched_luid=%08x:%08x\n",
                        resource_fd, i, adapter, device,
                        query.allocation_count,
                        query.private_runtime_data_size,
                        query.resource_priv_drv_data_size,
                        query.total_priv_drv_data_size,
                        buf->d3d12_source_luid_valid,
                        buf->d3d12_source_luid_b,
                        buf->d3d12_source_luid_a,
                        adapters[i].adapter_luid.b,
                        adapters[i].adapter_luid.a);
                matched = 1;
                break;
            }
            if (buf->d3d12_source_luid_valid) {
                int query_errno = errno;

                saved_errno = query_errno ? query_errno : EIO;
                fprintf(stderr,
                        "wlcomp: d3d12 native present query resource failed fd=%d adapter_index=%u adapter=0x%x device=0x%x ret_allocation_count=%u ret_total_priv=%u errno=%d (%s)\n",
                        resource_fd, i, open_luid.adapter_handle.v,
                        create_device.device.v, query.allocation_count,
                        query.total_priv_drv_data_size, query_errno,
                        strerror(query_errno));
            }

            memset(&destroy_device, 0, sizeof(destroy_device));
            destroy_device.device = create_device.device;
            ioctl(dxg_fd, LX_DXDESTROYDEVICE, &destroy_device);
        }

        memset(&close_adapter, 0, sizeof(close_adapter));
        close_adapter.adapter_handle = open_luid.adapter_handle;
        ioctl(dxg_fd, LX_DXCLOSEADAPTER, &close_adapter);
    }
    if (!matched) {
        saved_errno = ENODEV;
        if (buf->d3d12_source_luid_valid) {
            xv6_d3d12_set_missing("no DXG adapter matched the client D3D12 source LUID");
            fprintf(stderr,
                    "wlcomp: d3d12 native present blocked: no adapter matched source_luid=%08x:%08x count=%u\n",
                    buf->d3d12_source_luid_b,
                    buf->d3d12_source_luid_a, adapter_count);
        } else {
            xv6_d3d12_set_missing("D3D12 shared-resource import lacked client source LUID evidence");
            fprintf(stderr,
                    "wlcomp: d3d12 native present blocked: no adapter could open resource count=%u\n",
                    adapter_count);
        }
        goto out;
    }

    if (buf->d3d12_allocation_count != 0 &&
        query.allocation_count != buf->d3d12_allocation_count) {
        saved_errno = EINVAL;
        snprintf(g_d3d12_present_missing, sizeof(g_d3d12_present_missing),
                 "D3D12 shared-resource metadata mismatch field=allocation_count client=%u queried=%u",
                 buf->d3d12_allocation_count, query.allocation_count);
        fprintf(stderr,
                "wlcomp: d3d12 native present blocked: allocation count mismatch client=%u queried=%u\n",
                buf->d3d12_allocation_count, query.allocation_count);
        goto out;
    }
    if (buf->d3d12_total_priv_size != 0 &&
        query.total_priv_drv_data_size != buf->d3d12_total_priv_size) {
        saved_errno = EINVAL;
        snprintf(g_d3d12_present_missing, sizeof(g_d3d12_present_missing),
                 "D3D12 shared-resource metadata mismatch field=total_private_size client=%u queried=%u",
                 buf->d3d12_total_priv_size,
                 query.total_priv_drv_data_size);
        fprintf(stderr,
                "wlcomp: d3d12 native present blocked: total private data mismatch client=%u queried=%u\n",
                buf->d3d12_total_priv_size,
                query.total_priv_drv_data_size);
        goto out;
    }

    open_alloc = calloc(query.allocation_count, sizeof(open_alloc[0]));
    if (!open_alloc) {
        saved_errno = ENOMEM;
        goto out;
    }
    if (query.private_runtime_data_size) {
        runtime_data = calloc(1, query.private_runtime_data_size);
        if (!runtime_data) {
            saved_errno = ENOMEM;
            goto out;
        }
    }
    if (query.resource_priv_drv_data_size) {
        resource_data = calloc(1, query.resource_priv_drv_data_size);
        if (!resource_data) {
            saved_errno = ENOMEM;
            goto out;
        }
    }
    total_data = calloc(1, query.total_priv_drv_data_size);
    if (!total_data) {
        saved_errno = ENOMEM;
        goto out;
    }

    memset(&open_resource, 0, sizeof(open_resource));
    open_resource.device.v = device;
    open_resource.nt_handle = (uint64_t)resource_fd;
    open_resource.allocation_count = query.allocation_count;
    open_resource.open_alloc_info = (uint64_t)open_alloc;
    open_resource.private_runtime_data_size =
        (int32_t)query.private_runtime_data_size;
    open_resource.private_runtime_data = (uint64_t)runtime_data;
    open_resource.resource_priv_drv_data_size =
        query.resource_priv_drv_data_size;
    open_resource.resource_priv_drv_data = (uint64_t)resource_data;
    open_resource.total_priv_drv_data_size =
        query.total_priv_drv_data_size;
    open_resource.total_priv_drv_data = (uint64_t)total_data;
    g_d3d12_resource_import_attempt_count++;
    fprintf(stderr,
            "wlcomp: d3d12 resource import attempted d3d12_resource_import_attempts=%lu fd=%d adapter=0x%x device=0x%x allocations=%u total_priv=%u\n",
            (unsigned long)g_d3d12_resource_import_attempt_count,
            resource_fd, adapter, device, query.allocation_count,
            query.total_priv_drv_data_size);
    xv6_d3d12_write_stage_evidence("resource_import_attempted", 0);
    if (ioctl(dxg_fd, LX_DXOPENRESOURCEFROMNTHANDLE, &open_resource) < 0 ||
        open_resource.resource.v == 0) {
        int open_errno = errno;

        saved_errno = open_errno ? open_errno : EIO;
        xv6_d3d12_note_phase2_negative(
            XV6_D3D12_NEG_MISSING_RESOURCE_FD);
        xv6_d3d12_set_missing("DXG OpenResourceFromNtHandle failed for D3D12 native present import");
        fprintf(stderr,
                "wlcomp: d3d12 native present open resource failed fd=%d adapter=0x%x device=0x%x allocations=%u total_priv=%u runtime=%u resource_priv=%u errno=%d (%s) opened_resource=0x%x\n",
                resource_fd, adapter, device, query.allocation_count,
                query.total_priv_drv_data_size,
                query.private_runtime_data_size,
                query.resource_priv_drv_data_size, open_errno,
                strerror(open_errno), open_resource.resource.v);
        goto out;
    }

    buf->d3d12_dxg_fd = dxg_fd;
    buf->d3d12_adapter = adapter;
    buf->d3d12_device = device;
    buf->d3d12_opened_resource = open_resource.resource.v;
    buf->d3d12_allocation_count = query.allocation_count;
    buf->d3d12_opened_allocation_count = query.allocation_count;
    for (uint32_t i = 0; i < query.allocation_count &&
         i < XV6_D3D12_PRESENT_MAX_ALLOCATIONS; i++)
        buf->d3d12_opened_allocations[i] = open_alloc[i].allocation.v;
    buf->d3d12_total_priv_size = query.total_priv_drv_data_size;
    g_d3d12_resource_import_success_count++;
    fprintf(stderr,
            "wlcomp: d3d12 native present open resource ok d3d12_resource_import_successes=%lu fd=%d adapter=0x%x device=0x%x resource=0x%x allocations=%u first_allocation=0x%x total_priv=%u runtime=%u resource_priv=%u\n",
            (unsigned long)g_d3d12_resource_import_success_count,
            resource_fd, adapter, device, buf->d3d12_opened_resource,
            buf->d3d12_opened_allocation_count,
            buf->d3d12_opened_allocation_count ?
            buf->d3d12_opened_allocations[0] : 0,
            buf->d3d12_total_priv_size, query.private_runtime_data_size,
            query.resource_priv_drv_data_size);
    xv6_d3d12_write_stage_evidence("resource_import_succeeded", 0);
    if (buf->d3d12_fence_fd >= 0 &&
        xv6_d3d12_open_shared_fence(buf, buf->d3d12_fence_fd) != 0) {
        saved_errno = errno ? errno : EIO;
        xv6_d3d12_note_phase2_negative(XV6_D3D12_NEG_STALE_FENCE);
        if (!g_d3d12_present_missing[0])
            xv6_d3d12_set_missing("DXG OpenSyncObjectFromNtHandle2 failed for D3D12 monitored fence");
        fprintf(stderr,
                "wlcomp: d3d12 native present open fence failed fence_fd=%d resource=0x%x errno=%d (%s) missing='%s'\n",
                buf->d3d12_fence_fd, buf->d3d12_opened_resource,
                saved_errno, strerror(saved_errno),
                g_d3d12_present_missing[0] ? g_d3d12_present_missing :
                "unknown");
        xv6_d3d12_close_buffer_import(buf);
        dxg_fd = -1;
        goto out;
    }
    if (buf->d3d12_opened_fence != 0) {
        fprintf(stderr,
                "wlcomp: d3d12 native present open fence ok fence_fd=%d fence=0x%x cpu_va=0x%lx gpu_va=0x%lx target_valid=%d target=%lu\n",
                buf->d3d12_fence_fd, buf->d3d12_opened_fence,
                (unsigned long)buf->d3d12_fence_cpu_va,
                (unsigned long)buf->d3d12_fence_gpu_va,
                buf->d3d12_fence_value_valid,
                (unsigned long)buf->d3d12_fence_value);
    }
    ret = 0;

out:
    free(open_alloc);
    free(runtime_data);
    free(resource_data);
    free(total_data);
    if (ret != 0) {
        xv6_d3d12_close_device(dxg_fd, adapter, device);
        errno = saved_errno;
    }
    return ret;
}

static void xv6_gpu_create_d3d12_resource_buffer_common(
    struct wl_client *client,
    struct wl_resource *resource,
    uint32_t id,
    int32_t resource_fd,
    int32_t fence_fd,
    uint32_t source_luid_a,
    uint32_t source_luid_b,
    int source_luid_valid,
    uint64_t fence_value,
    int fence_value_valid,
    int32_t width,
    int32_t height,
    uint32_t format,
    uint32_t allocation_count,
    uint32_t total_priv_size,
    const char *client_run_id)
{
    struct wlcomp_buffer *buf;
    struct wl_resource *buf_res;
    int stored_fence_fd = -1;
    pid_t client_pid = 0;
    uid_t client_uid = 0;
    gid_t client_gid = 0;
    uint32_t manager_resource_id = resource ? wl_resource_get_id(resource) : 0;

    wl_client_get_credentials(client, &client_pid, &client_uid, &client_gid);
    xv6_d3d12_set_last_client_run_id(client_run_id);
    g_d3d12_last_client_pid = client_pid;
    g_d3d12_last_client_buffer_id = id;
    g_d3d12_last_manager_resource_id = manager_resource_id;
    g_d3d12_last_buffer_generation = 0;
    g_d3d12_last_native_present_attempt_id = 0;
    g_d3d12_last_native_present_completion_id = 0;
    g_d3d12_present_missing[0] = '\0';
    xv6_d3d12_reset_phase2_negative_current();
    g_d3d12_resource_submit_count++;
    fprintf(stderr,
            "wlcomp: d3d12 shared buffer submission d3d12_resource_submissions=%lu run_id=%s pid=%d manager_resource_id=%u client_buffer_id=%u fd=%d fence_fd=%d target_valid=%d target=%lu size=%dx%d fmt=0x%x allocation_count=%u total_priv=%u source_luid_valid=%d source_luid=%08x:%08x\n",
            (unsigned long)g_d3d12_resource_submit_count,
            xv6_d3d12_present_run_id(), client_pid,
            manager_resource_id, id, resource_fd, fence_fd,
            fence_value_valid, (unsigned long)fence_value, width, height,
            format, allocation_count, total_priv_size, source_luid_valid,
            source_luid_b, source_luid_a);
    xv6_d3d12_write_stage_evidence("protocol_submitted", 0);

    if (resource_fd < 0) {
        xv6_d3d12_note_phase2_negative(
            XV6_D3D12_NEG_MISSING_RESOURCE_FD);
        xv6_d3d12_set_missing("D3D12 shared-resource import requires resource fd");
        errno = EINVAL;
    } else if (width <= 0 || height <= 0 || width > (INT32_MAX / 4)) {
        xv6_d3d12_note_phase2_negative(
            XV6_D3D12_NEG_WRONG_DIMENSIONS);
        xv6_d3d12_set_missing("D3D12 shared-resource import requires positive sane dimensions");
        errno = EINVAL;
    } else if (allocation_count == 0 || total_priv_size == 0) {
        xv6_d3d12_set_missing(
            "D3D12 shared-resource import requires nonzero allocation count and private metadata size");
        errno = EINVAL;
    } else if (allocation_count > 64) {
        xv6_d3d12_set_missing("D3D12 shared-resource import allocation count exceeds local cap");
        errno = EINVAL;
    } else if (format != WL_SHM_FORMAT_ARGB8888 &&
               format != WL_SHM_FORMAT_XRGB8888) {
        xv6_d3d12_note_phase2_negative(XV6_D3D12_NEG_WRONG_FORMAT);
        xv6_d3d12_set_missing("D3D12 shared-resource import requires ARGB8888 or XRGB8888 format");
        errno = EINVAL;
    }
    if (g_d3d12_present_missing[0]) {
        if (resource_fd >= 0)
            close(resource_fd);
        if (fence_fd >= 0)
            close(fence_fd);
        g_d3d12_present_reject_count++;
        fprintf(stderr,
                "wlcomp: rejected D3D12 shared buffer invalid_contract=1 missing='%s' fd=%d fence_fd=%d size=%dx%d fmt=0x%x allocations=%u total_priv=%u rejects=%lu\n",
                g_d3d12_present_missing, resource_fd, fence_fd, width,
                height, format, allocation_count, total_priv_size,
                (unsigned long)g_d3d12_present_reject_count);
        xv6_d3d12_write_stage_evidence("protocol_rejected", EINVAL);
        wl_resource_post_error(resource, 0,
                               "invalid D3D12 shared-resource buffer");
        return;
    }
    if (!source_luid_valid || (source_luid_a == 0 && source_luid_b == 0)) {
        if (resource_fd >= 0)
            close(resource_fd);
        if (fence_fd >= 0)
            close(fence_fd);
        xv6_d3d12_note_phase2_negative(XV6_D3D12_NEG_BAD_LUID);
        xv6_d3d12_set_missing(
            "D3D12 shared-resource import requires a nonzero source LUID");
        g_d3d12_present_reject_count++;
        fprintf(stderr,
                "wlcomp: rejected D3D12 shared buffer missing_source_luid=1 fd=%d size=%dx%d fmt=0x%x allocations=%u total_priv=%u rejects=%lu\n",
                resource_fd, width, height, format, allocation_count,
                total_priv_size, (unsigned long)g_d3d12_present_reject_count);
        xv6_d3d12_write_stage_evidence("protocol_rejected_missing_luid",
                                       EINVAL);
        wl_resource_post_error(resource, 0,
                               "D3D12 shared-resource buffer requires source LUID");
        return;
    }
    if (fence_fd < 0 || !fence_value_valid || fence_value == 0) {
        if (resource_fd >= 0)
            close(resource_fd);
        if (fence_fd >= 0)
            close(fence_fd);
        if (fence_fd < 0)
            xv6_d3d12_note_phase2_negative(
                XV6_D3D12_NEG_MISSING_FENCE_FD);
        if (!fence_value_valid || fence_value == 0)
            xv6_d3d12_note_phase2_negative(XV6_D3D12_NEG_STALE_FENCE);
        xv6_d3d12_set_missing(
            "D3D12 shared-resource import requires monitored fence fd and nonzero target");
        g_d3d12_present_reject_count++;
        fprintf(stderr,
                "wlcomp: rejected D3D12 shared buffer missing_fence_contract=1 fd=%d fence_fd=%d target_valid=%d target=%lu source_luid=%08x:%08x rejects=%lu\n",
                resource_fd, fence_fd, fence_value_valid,
                (unsigned long)fence_value, source_luid_b, source_luid_a,
                (unsigned long)g_d3d12_present_reject_count);
        xv6_d3d12_write_stage_evidence("protocol_rejected_missing_fence",
                                       EINVAL);
        wl_resource_post_error(resource, 0,
                               "D3D12 shared-resource buffer requires fence fd and target");
        return;
    }
    if (fence_fd >= 0) {
        stored_fence_fd = dup(fence_fd);
        if (stored_fence_fd < 0) {
            int dup_errno = errno ? errno : EIO;

            close(resource_fd);
            close(fence_fd);
            xv6_d3d12_note_phase2_negative(
                XV6_D3D12_NEG_MISSING_FENCE_FD);
            xv6_d3d12_set_missing("could not duplicate D3D12 monitored fence fd");
            g_d3d12_present_reject_count++;
            fprintf(stderr,
                    "wlcomp: rejected D3D12 shared buffer dup_fence_failed=1 fd=%d fence_fd=%d errno=%d (%s) rejects=%lu\n",
                    resource_fd, fence_fd, dup_errno, strerror(dup_errno),
                    (unsigned long)g_d3d12_present_reject_count);
            xv6_d3d12_write_stage_evidence("protocol_rejected_fence_dup",
                                           dup_errno);
            wl_resource_post_error(resource, 0,
                                   "could not duplicate D3D12 fence fd");
            return;
        }
        close(fence_fd);
        fence_fd = -1;
    }

    buf = calloc(1, sizeof(*buf));
    if (!buf) {
        close(resource_fd);
        if (fence_fd >= 0)
            close(fence_fd);
        if (stored_fence_fd >= 0)
            close(stored_fence_fd);
        wl_resource_post_no_memory(resource);
        return;
    }
    buf->is_d3d12_shared = 1;
    buf->d3d12_dxg_fd = -1;
    buf->d3d12_client_pid = client_pid;
    buf->d3d12_client_buffer_id = id;
    buf->d3d12_manager_resource_id = manager_resource_id;
    snprintf(buf->d3d12_client_run_id, sizeof(buf->d3d12_client_run_id),
             "%s", g_d3d12_last_client_run_id);
    buf->d3d12_buffer_generation = ++g_d3d12_next_buffer_generation;
    buf->d3d12_resource_fd = resource_fd;
    buf->d3d12_fence_fd = stored_fence_fd;
    buf->d3d12_source_luid_a = source_luid_a;
    buf->d3d12_source_luid_b = source_luid_b;
    buf->d3d12_source_luid_valid = source_luid_valid;
    buf->d3d12_fence_value = fence_value;
    buf->d3d12_fence_value_valid = fence_value_valid;
    buf->acquire_fence_fd = -1;
    buf->d3d12_allocation_count = allocation_count;
    buf->d3d12_total_priv_size = total_priv_size;
    buf->width = width;
    buf->height = height;
    buf->stride = width * 4;
    buf->plane_count = 1;
    buf->plane_stride[0] = buf->stride;
    buf->plane_offset[0] = 0;
    buf->format = format;
    xv6_d3d12_set_last_identity_from_buffer(buf);
    xv6_d3d12_reset_present_source_record(buf);
    g_d3d12_native_import_attempt_count++;
    fprintf(stderr,
            "wlcomp: d3d12 native import attempted d3d12_native_import_attempts=%lu fd=%d fence_fd=%d target=%lu size=%dx%d fmt=0x%x source_luid=%08x:%08x\n",
            (unsigned long)g_d3d12_native_import_attempt_count, resource_fd,
            stored_fence_fd, (unsigned long)fence_value, width, height,
            format, source_luid_b, source_luid_a);
    xv6_d3d12_write_stage_evidence("native_import_attempted", 0);
    if (xv6_d3d12_open_shared_buffer(buf, resource_fd) != 0) {
        struct xv6_d3d12_present_contract present;
        struct xv6_d3d12_present_result result;
        int open_errno = errno ? errno : EIO;

        memset(&present, 0, sizeof(present));
        memset(&result, 0, sizeof(result));
        present.resource = buf->d3d12_opened_resource;
        present.allocation_count = buf->d3d12_allocation_count;
        present.fence = buf->d3d12_opened_fence;
        present.fence_target = buf->d3d12_fence_value;
        present.format = buf->format;
        present.source_luid_a = buf->d3d12_source_luid_a;
        present.source_luid_b = buf->d3d12_source_luid_b;
        present.source_luid_valid = buf->d3d12_source_luid_valid;
        present.matched_luid_a = buf->d3d12_matched_luid_a;
        present.matched_luid_b = buf->d3d12_matched_luid_b;
        present.wait_confirmed = buf->d3d12_present_wait_confirmed;
        present.total_priv_size = buf->d3d12_total_priv_size;
        present.dst.w = (uint32_t)width;
        present.dst.h = (uint32_t)height;
        present.src.w = (uint32_t)width;
        present.src.h = (uint32_t)height;
        xv6_d3d12_fill_present_identity(&present, buf);
        if (!g_d3d12_present_missing[0])
            xv6_d3d12_set_missing(
                "D3D12 shared-resource import failed before wl_buffer creation");
        g_d3d12_present_reject_count++;
        xv6_d3d12_write_present_evidence(&present, &result, 1,
                                         open_errno);
        buffer_free(buf);
        wl_resource_post_error(resource, 0,
                               "D3D12 shared-resource open failed");
        return;
    }
    if (xv6_d3d12_validate_shared_resource_desc(buf) != 0) {
        struct xv6_d3d12_present_contract present;
        struct xv6_d3d12_present_result result;
        int validate_errno = errno ? errno : EIO;

        memset(&present, 0, sizeof(present));
        memset(&result, 0, sizeof(result));
        present.resource = buf->d3d12_opened_resource;
        present.allocation_count = buf->d3d12_allocation_count;
        present.fence = buf->d3d12_opened_fence;
        present.fence_target = buf->d3d12_fence_value;
        present.format = buf->format;
        present.source_luid_a = buf->d3d12_source_luid_a;
        present.source_luid_b = buf->d3d12_source_luid_b;
        present.source_luid_valid = buf->d3d12_source_luid_valid;
        present.matched_luid_a = buf->d3d12_matched_luid_a;
        present.matched_luid_b = buf->d3d12_matched_luid_b;
        present.wait_confirmed = buf->d3d12_present_wait_confirmed;
        present.total_priv_size = buf->d3d12_total_priv_size;
        present.dst.w = (uint32_t)width;
        present.dst.h = (uint32_t)height;
        present.src.w = (uint32_t)width;
        present.src.h = (uint32_t)height;
        xv6_d3d12_fill_present_identity(&present, buf);
        if (!g_d3d12_present_missing[0])
            xv6_d3d12_set_missing(
                "D3D12 shared-resource metadata validation failed before wl_buffer creation");
        g_d3d12_present_reject_count++;
        xv6_d3d12_write_present_evidence(&present, &result, 1,
                                         validate_errno);
        fprintf(stderr,
                "wlcomp: d3d12 native import metadata rejected rejects=%lu missing='%s' fd=%d resource=0x%x fence=0x%x target=%lu size=%dx%d fmt=0x%x errno=%d (%s) protocol_accepted=0\n",
                (unsigned long)g_d3d12_present_reject_count,
                g_d3d12_present_missing, resource_fd,
                buf->d3d12_opened_resource, buf->d3d12_opened_fence,
                (unsigned long)buf->d3d12_fence_value, width, height,
                format, validate_errno, strerror(validate_errno));
        buffer_free(buf);
        wl_resource_post_error(resource, 0,
                               "D3D12 shared-resource metadata validation failed");
        return;
    }
    g_d3d12_native_import_complete_count++;
    fprintf(stderr,
            "wlcomp: d3d12 native import completed d3d12_native_import_completes=%lu fd=%d resource=0x%x fence=0x%x target=%lu protocol_accepted=0\n",
            (unsigned long)g_d3d12_native_import_complete_count, resource_fd,
            buf->d3d12_opened_resource, buf->d3d12_opened_fence,
            (unsigned long)buf->d3d12_fence_value);
    xv6_d3d12_write_stage_evidence("native_import_completed", 0);

    buf_res = wl_resource_create(client, &wl_buffer_interface, 1, id);
    if (!buf_res) {
        buffer_free(buf);
        wl_resource_post_no_memory(resource);
        return;
    }
    buf->resource = buf_res;
    wl_resource_set_implementation(buf_res, &buffer_impl, buf,
                                   buffer_destroy_handler);
    wl_client_get_credentials(client, &client_pid, &client_uid,
                              &client_gid);
    g_d3d12_resource_protocol_accept_count++;
    fprintf(stderr,
            "wlcomp: d3d12 shared buffer protocol accepted d3d12_protocol_accepts=%lu run_id=%s pid=%d manager_resource_id=%u client_buffer_id=%u buffer_generation=%lu size=%dx%d fmt=0x%x allocations=%u total_priv=%u fd=%d fence=%d source_luid=%08x:%08x matched_luid=%08x:%08x opened_resource=0x%x opened_fence=0x%x fence_cpu=0x%lx fence_gpu=0x%lx acquire_import=%s import_attempts=%lu import_completes=%lu\n",
            (unsigned long)g_d3d12_resource_protocol_accept_count,
            xv6_d3d12_present_run_id(), client_pid,
            buf->d3d12_manager_resource_id, buf->d3d12_client_buffer_id,
            (unsigned long)buf->d3d12_buffer_generation, width, height, format,
            buf->d3d12_allocation_count,
            buf->d3d12_total_priv_size, resource_fd, stored_fence_fd,
            buf->d3d12_source_luid_valid ? buf->d3d12_source_luid_b : 0,
            buf->d3d12_source_luid_valid ? buf->d3d12_source_luid_a : 0,
            buf->d3d12_matched_luid_b, buf->d3d12_matched_luid_a,
            buf->d3d12_opened_resource, buf->d3d12_opened_fence,
            (unsigned long)buf->d3d12_fence_cpu_va,
            (unsigned long)buf->d3d12_fence_gpu_va,
            buf->d3d12_fence_imported_from_syncfile ?
            "dxg-syncfile" : "nt-handle",
            (unsigned long)g_d3d12_native_import_attempt_count,
            (unsigned long)g_d3d12_native_import_complete_count);
    fprintf(stderr,
            "wlcomp: d3d12_wayland_resource_buffer_admission_matrix "
            "run_id=%s client_pid=%d client_buffer_id=%u "
            "manager_resource_id=%u buffer_generation=%lu "
            "same_adapter_luid=%d resource_fd=%d fence_fd=%d "
            "resource_query=1 resource_open=1 fence_open=%d "
            "acquire_import=%s syncfile_open=%d "
            "protocol_accepted=1 present_admission=failclosed "
            "native_present_claim=0 opengl_submit_credit=0 status=PASS\n",
            xv6_d3d12_present_run_id(), client_pid,
            buf->d3d12_client_buffer_id,
            buf->d3d12_manager_resource_id,
            (unsigned long)buf->d3d12_buffer_generation,
            buf->d3d12_source_luid_valid &&
                buf->d3d12_source_luid_a == buf->d3d12_matched_luid_a &&
                buf->d3d12_source_luid_b == buf->d3d12_matched_luid_b,
            buf->d3d12_resource_fd >= 0,
            buf->d3d12_fence_fd >= 0,
            buf->d3d12_opened_fence != 0,
            buf->d3d12_fence_imported_from_syncfile ?
            "dxg-syncfile" : "nt-handle",
            buf->d3d12_fence_imported_from_syncfile);
    xv6_d3d12_write_stage_evidence("protocol_accepted", 0);
    if (buf->d3d12_fence_value_valid) {
        fprintf(stderr,
                "wlcomp: d3d12 shared buffer fence target=%lu current=%lu\n",
                (unsigned long)buf->d3d12_fence_value,
                buf->d3d12_fence_cpu_va ?
                (unsigned long)*(volatile uint64_t *)buf->d3d12_fence_cpu_va :
                0);
    }
}

static void xv6_gpu_create_d3d12_resource_buffer(
    struct wl_client *client,
    struct wl_resource *resource,
    uint32_t id,
    int32_t resource_fd,
    int32_t width,
    int32_t height,
    uint32_t format,
    uint32_t allocation_count,
    uint32_t total_priv_size)
{
    xv6_gpu_create_d3d12_resource_buffer_common(
        client, resource, id, resource_fd, -1, 0, 0, 0, 0, 0, width,
        height, format, allocation_count, total_priv_size, NULL);
}

static void xv6_gpu_create_d3d12_resource_buffer_with_fence(
    struct wl_client *client,
    struct wl_resource *resource,
    uint32_t id,
    int32_t resource_fd,
    int32_t fence_fd,
    int32_t width,
    int32_t height,
    uint32_t format,
    uint32_t allocation_count,
    uint32_t total_priv_size)
{
    xv6_gpu_create_d3d12_resource_buffer_common(
        client, resource, id, resource_fd, fence_fd, 0, 0, 0, 0, 0, width,
        height, format, allocation_count, total_priv_size, NULL);
}

static void xv6_gpu_create_d3d12_resource_buffer_luid(
    struct wl_client *client,
    struct wl_resource *resource,
    uint32_t id,
    int32_t resource_fd,
    uint32_t luid_a,
    uint32_t luid_b,
    int32_t width,
    int32_t height,
    uint32_t format,
    uint32_t allocation_count,
    uint32_t total_priv_size)
{
    xv6_gpu_create_d3d12_resource_buffer_common(
        client, resource, id, resource_fd, -1, luid_a, luid_b, 1, 0, 0,
        width, height, format, allocation_count, total_priv_size, NULL);
}

static void xv6_gpu_create_d3d12_resource_buffer_with_fence_luid(
    struct wl_client *client,
    struct wl_resource *resource,
    uint32_t id,
    int32_t resource_fd,
    int32_t fence_fd,
    uint32_t luid_a,
    uint32_t luid_b,
    int32_t width,
    int32_t height,
    uint32_t format,
    uint32_t allocation_count,
    uint32_t total_priv_size)
{
    xv6_gpu_create_d3d12_resource_buffer_common(
        client, resource, id, resource_fd, fence_fd, luid_a, luid_b, 1,
        0, 0, width, height, format, allocation_count, total_priv_size,
        NULL);
}

static void xv6_gpu_create_d3d12_resource_buffer_with_fence_value_luid(
    struct wl_client *client,
    struct wl_resource *resource,
    uint32_t id,
    int32_t resource_fd,
    int32_t fence_fd,
    uint32_t fence_value_low,
    uint32_t fence_value_high,
    uint32_t luid_a,
    uint32_t luid_b,
    int32_t width,
    int32_t height,
    uint32_t format,
    uint32_t allocation_count,
    uint32_t total_priv_size)
{
    uint64_t fence_value =
        ((uint64_t)fence_value_high << 32) | fence_value_low;

    xv6_gpu_create_d3d12_resource_buffer_common(
        client, resource, id, resource_fd, fence_fd, luid_a, luid_b, 1,
        fence_value, 1, width, height, format, allocation_count,
        total_priv_size, NULL);
}

static void xv6_gpu_create_d3d12_resource_buffer_with_fence_value_luid_run_id(
    struct wl_client *client,
    struct wl_resource *resource,
    uint32_t id,
    int32_t resource_fd,
    int32_t fence_fd,
    uint32_t fence_value_low,
    uint32_t fence_value_high,
    uint32_t luid_a,
    uint32_t luid_b,
    int32_t width,
    int32_t height,
    uint32_t format,
    uint32_t allocation_count,
    uint32_t total_priv_size,
    const char *client_run_id)
{
    uint64_t fence_value =
        ((uint64_t)fence_value_high << 32) | fence_value_low;

    xv6_gpu_create_d3d12_resource_buffer_common(
        client, resource, id, resource_fd, fence_fd, luid_a, luid_b, 1,
        fence_value, 1, width, height, format, allocation_count,
        total_priv_size, client_run_id);
}

static const struct xv6_gpu_buffer_manager_impl xv6_gpu_buffer_manager_impl = {
    .create_buffer = xv6_gpu_create_buffer,
    .create_buffer_with_fence = xv6_gpu_create_buffer_with_fence,
    .create_d3d12_resource_buffer =
        xv6_gpu_create_d3d12_resource_buffer,
    .create_d3d12_resource_buffer_with_fence =
        xv6_gpu_create_d3d12_resource_buffer_with_fence,
    .create_d3d12_resource_buffer_luid =
        xv6_gpu_create_d3d12_resource_buffer_luid,
    .create_d3d12_resource_buffer_with_fence_luid =
        xv6_gpu_create_d3d12_resource_buffer_with_fence_luid,
    .create_d3d12_resource_buffer_with_fence_value_luid =
        xv6_gpu_create_d3d12_resource_buffer_with_fence_value_luid,
    .create_d3d12_resource_buffer_with_fence_value_luid_run_id =
        xv6_gpu_create_d3d12_resource_buffer_with_fence_value_luid_run_id,
};

static void xv6_gpu_bind(struct wl_client *client, void *data,
                         uint32_t version, uint32_t id)
{
    struct wl_resource *res;

    (void)data;
    if (version > 6)
        version = 6;
    res = wl_resource_create(client, &xv6_gpu_buffer_manager_interface,
                             version, id);
    if (!res) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(res, &xv6_gpu_buffer_manager_impl, NULL,
                                   NULL);
}

/* ── zwp_linux_explicit_synchronization_v1 ───────────────────────── */

static void explicit_sync_destroy(struct wl_client *client,
                                  struct wl_resource *resource)
{
    (void)client;
    wl_resource_destroy(resource);
}

static void surface_sync_destroy(struct wl_client *client,
                                 struct wl_resource *resource)
{
    (void)client;
    wl_resource_destroy(resource);
}

static void surface_sync_set_acquire_fence(struct wl_client *client,
                                           struct wl_resource *resource,
                                           int32_t fd)
{
    struct wlcomp_surface *surf = wl_resource_get_user_data(resource);

    (void)client;
    if (!surf || !surf->resource) {
        if (fd >= 0)
            close(fd);
        wl_resource_post_error(
            resource, ZWP_LINUX_SURFACE_SYNCHRONIZATION_V1_ERROR_NO_SURFACE,
            "surface was destroyed");
        return;
    }
    if (surf->has_pending_acquire_fence) {
        if (fd >= 0)
            close(fd);
        wl_resource_post_error(
            resource,
            ZWP_LINUX_SURFACE_SYNCHRONIZATION_V1_ERROR_DUPLICATE_FENCE,
            "duplicate acquire fence");
        return;
    }
    surf->pending_acquire_fence_fd = fd;
    surf->has_pending_acquire_fence = 1;
}

static void surface_sync_get_release(struct wl_client *client,
                                     struct wl_resource *resource,
                                     uint32_t id)
{
    struct wlcomp_surface *surf = wl_resource_get_user_data(resource);
    struct wl_resource *release;

    if (!surf || !surf->resource) {
        wl_resource_post_error(
            resource, ZWP_LINUX_SURFACE_SYNCHRONIZATION_V1_ERROR_NO_SURFACE,
            "surface was destroyed");
        return;
    }
    if (surf->pending_release) {
        wl_resource_post_error(
            resource,
            ZWP_LINUX_SURFACE_SYNCHRONIZATION_V1_ERROR_DUPLICATE_RELEASE,
            "duplicate release request");
        return;
    }
    release = wl_resource_create(client, &zwp_linux_buffer_release_v1_interface,
                                 1, id);
    if (!release) {
        wl_resource_post_no_memory(resource);
        return;
    }
    surf->pending_release = release;
}

static void surface_sync_destroy_handler(struct wl_resource *resource)
{
    struct wlcomp_surface *surf = wl_resource_get_user_data(resource);

    if (!surf)
        return;
    if (surf->pending_acquire_fence_fd >= 0) {
        close(surf->pending_acquire_fence_fd);
        surf->pending_acquire_fence_fd = -1;
    }
    surf->has_pending_acquire_fence = 0;
    surf->sync_resource = NULL;
}

static const struct zwp_linux_surface_synchronization_v1_interface
surface_sync_impl = {
    .destroy = surface_sync_destroy,
    .set_acquire_fence = surface_sync_set_acquire_fence,
    .get_release = surface_sync_get_release,
};

static void explicit_sync_get_synchronization(struct wl_client *client,
                                              struct wl_resource *resource,
                                              uint32_t id,
                                              struct wl_resource *surface)
{
    struct wlcomp_surface *surf = surface_from_resource(surface);
    struct wl_resource *sync;

    if (!surf || !surf->resource) {
        wl_resource_post_error(resource,
                               ZWP_LINUX_SURFACE_SYNCHRONIZATION_V1_ERROR_NO_SURFACE,
                               "surface was destroyed");
        return;
    }
    if (surf->sync_resource) {
        wl_resource_post_error(
            resource,
            ZWP_LINUX_EXPLICIT_SYNCHRONIZATION_V1_ERROR_SYNCHRONIZATION_EXISTS,
            "surface already has explicit synchronization");
        return;
    }
    sync = wl_resource_create(client,
                              &zwp_linux_surface_synchronization_v1_interface,
                              2, id);
    if (!sync) {
        wl_resource_post_no_memory(resource);
        return;
    }
    surf->sync_resource = sync;
    wl_resource_set_implementation(sync, &surface_sync_impl, surf,
                                   surface_sync_destroy_handler);
}

static const struct zwp_linux_explicit_synchronization_v1_interface
explicit_sync_impl = {
    .destroy = explicit_sync_destroy,
    .get_synchronization = explicit_sync_get_synchronization,
};

static void explicit_sync_bind(struct wl_client *client, void *data,
                               uint32_t version, uint32_t id)
{
    struct wl_resource *res;

    (void)data;
    if (version > 2)
        version = 2;
    res = wl_resource_create(client,
                             &zwp_linux_explicit_synchronization_v1_interface,
                             version, id);
    if (!res) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(res, &explicit_sync_impl, NULL, NULL);
}
