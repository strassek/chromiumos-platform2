#include "sommelier-vfio.h"

#include "sommelier.h"

#include <assert.h>
#include <errno.h>
#include <gbm.h>
#include <libdrm/drm_fourcc.h>
#include <limits.h>
#include <linux/virtwl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/mman.h>
#include <i915_drm.h>
#include <fcntl.h>

#include <xf86drm.h>
#include <xf86drmMode.h>

#include <wayland-client.h>
#include <wayland-util.h>

#include "virtgpu_drm.h"
#include "drm-server-protocol.h"
#include "linux-dmabuf-unstable-v1-client-protocol.h"
#include "viewporter-client-protocol.h"



#define MIN_SIZE (INT_MIN / 10)
#define MAX_SIZE (INT_MAX / 10)

#define ALIGN(v, a) (((v) + (a)-1) & ~((a)-1))

#define DMA_BUF_SYNC_READ (1 << 0)
#define DMA_BUF_SYNC_WRITE (2 << 0)
#define DMA_BUF_SYNC_RW (DMA_BUF_SYNC_READ | DMA_BUF_SYNC_WRITE)
#define DMA_BUF_SYNC_START (0 << 2)
#define DMA_BUF_SYNC_END (1 << 2)

#define DMA_BUF_BASE 'b'
#define DMA_BUF_IOCTL_SYNC _IOW(DMA_BUF_BASE, 0, struct dma_buf_sync)

struct sl_dmabuf_modifier {
  uint32_t format;
  uint32_t modifier_hi;
  uint32_t modifier_lo;
};

struct sl_node {
   struct sl_dmabuf_modifier data;
   struct sl_node *next;
};

struct dma_buf_sync {
  __u64 flags;
};

struct sl_node* last_node = NULL;
struct sl_node* first_node = NULL;

static void sl_dmabuf_sync(int fd, __u64 flags) {
  struct dma_buf_sync sync = {0};
  int rv;

  sync.flags = flags;
  do {
    rv = ioctl(fd, DMA_BUF_IOCTL_SYNC, &sync);
  } while (rv == -1 && errno == EINTR);
}

static void sl_dmabuf_begin_write(int fd) {
  sl_dmabuf_sync(fd, DMA_BUF_SYNC_START | DMA_BUF_SYNC_WRITE);
}

static void sl_dmabuf_end_write(int fd) {
  sl_dmabuf_sync(fd, DMA_BUF_SYNC_END | DMA_BUF_SYNC_WRITE);
}

static void sl_virtwl_dmabuf_sync(int fd, __u32 flags) {
  struct virtwl_ioctl_dmabuf_sync sync = {0};
  int rv;

  sync.flags = flags;
  rv = ioctl(fd, VIRTWL_IOCTL_DMABUF_SYNC, &sync);
  assert(!rv);
  UNUSED(rv);
}

static void sl_virtwl_dmabuf_begin_write(int fd) {
  sl_virtwl_dmabuf_sync(fd, DMA_BUF_SYNC_START | DMA_BUF_SYNC_WRITE);
}

static void sl_virtwl_dmabuf_end_write(int fd) {
  sl_virtwl_dmabuf_sync(fd, DMA_BUF_SYNC_END | DMA_BUF_SYNC_WRITE);
}

static char printable_char(int c)
{
        return isascii(c) && isprint(c) ? c : '?';
}

static char *sl_get_format_name(uint32_t format)
{
        static char buf[32];

        snprintf(buf, sizeof(buf),
                 "%c%c%c%c (0x%08x)",
                 printable_char(format & 0xff),
                 printable_char((format >> 8) & 0xff),
                 printable_char((format >> 16) & 0xff),
                 printable_char((format >> 24) & 0x7f),
                 format);

        return buf;
}

static int initialized = 0;
int is_virtio_gpu = 0;
int force_blit = 0;
int use_frame_buffer_blit = 0;
static PFNEGLCREATEIMAGEKHRPROC eglCreateImageKHR = NULL;
static PFNEGLCREATESYNCKHRPROC eglCreateSyncKHR = NULL;
static PFNEGLDESTROYSYNCKHRPROC eglDestroySyncKHR = NULL;
static PFNEGLWAITSYNCKHRPROC eglWaitSyncKHR = NULL;
static PFNEGLDESTROYIMAGEKHRPROC eglDestroyImageKHR = NULL;
static PFNEGLDUPNATIVEFENCEFDANDROIDPROC eglDupNativeFenceFDANDROID = NULL;
static PFNGLEGLIMAGETARGETTEXTURE2DOESPROC glEGLImageTargetTexture2DOES = NULL;
static EGLDisplay display = EGL_NO_DISPLAY;
static EGLContext context = EGL_NO_CONTEXT;

#define GL_READ_FRAMEBUFFER               0x8CA8
#define GL_DRAW_FRAMEBUFFER               0x8CA9

static int sl_initialize_shims() {
 #define get_proc(name, proc)                  \
 do {                                        \
   name = (proc)eglGetProcAddress(#name); \
   assert(name);                          \
 } while (0)

 get_proc(eglCreateImageKHR, PFNEGLCREATEIMAGEKHRPROC);
 get_proc(eglCreateSyncKHR, PFNEGLCREATESYNCKHRPROC);
 get_proc(eglDestroySyncKHR, PFNEGLDESTROYSYNCKHRPROC);
 get_proc(eglWaitSyncKHR, PFNEGLWAITSYNCKHRPROC);
 get_proc(eglDestroyImageKHR, PFNEGLDESTROYIMAGEKHRPROC);
 get_proc(eglDupNativeFenceFDANDROID, PFNEGLDUPNATIVEFENCEFDANDROIDPROC);
 get_proc(glEGLImageTargetTexture2DOES, PFNGLEGLIMAGETARGETTEXTURE2DOESPROC);

 return 0;
}


size_t sl_bpp_for_drm_format(uint32_t format) {
  switch (format) {
    case DRM_FORMAT_NV12:
      return 1;
    case DRM_FORMAT_RGB565:
      return 2;
    case DRM_FORMAT_ARGB8888:
    case DRM_FORMAT_ABGR8888:
    case DRM_FORMAT_XRGB8888:
    case DRM_FORMAT_XBGR8888:
      return 4;
  }
  assert(0);
  return 0;
}

size_t sl_num_planes_for_drm_format(uint32_t format) {
  switch (format) {
    case DRM_FORMAT_NV12:
      return 2;
    case DRM_FORMAT_RGB565:
    case DRM_FORMAT_ARGB8888:
    case DRM_FORMAT_ABGR8888:
    case DRM_FORMAT_XRGB8888:
    case DRM_FORMAT_XBGR8888:
      return 1;
  }
  assert(0);
  return 0;
}

static size_t sl_y_subsampling_for_drm_format_plane(uint32_t format,
                                                    size_t plane) {
  switch (format) {
    case DRM_FORMAT_NV12: {
      const size_t subsampling[] = {1, 2};

      assert(plane < ARRAY_SIZE(subsampling));
      return subsampling[plane];
    }
    case DRM_FORMAT_RGB565:
    case DRM_FORMAT_ARGB8888:
    case DRM_FORMAT_ABGR8888:
    case DRM_FORMAT_XRGB8888:
    case DRM_FORMAT_XBGR8888:
      return 1;
  }
  assert(0);
  return 0;
}

static int sl_offset_for_drm_format_plane(uint32_t format,
                                          size_t height,
                                          size_t stride,
                                          size_t plane) {
  switch (format) {
    case WL_DRM_FORMAT_NV12: {
      const size_t offset[] = {0, 1};

      assert(plane < ARRAY_SIZE(offset));
      return offset[plane] * height * stride;
    }
    case DRM_FORMAT_RGB565:
    case DRM_FORMAT_ARGB8888:
    case DRM_FORMAT_ABGR8888:
    case DRM_FORMAT_XRGB8888:
    case DRM_FORMAT_XBGR8888:
      return 0;
  }
  assert(0);
  return 0;
}

static size_t sl_size_for_drm_format_plane(uint32_t format,
                                           size_t height,
                                           size_t stride,
                                           size_t plane) {
  return height / sl_y_subsampling_for_drm_format_plane(format, plane) * stride;
}

static size_t sl_size_for_drm_format(uint32_t format,
                                     size_t height,
                                     size_t stride) {
  size_t i, num_planes = sl_num_planes_for_drm_format(format);
  size_t total_size = 0;

  for (i = 0; i < num_planes; ++i) {
    size_t size = sl_size_for_drm_format_plane(format, height, stride, i);
    size_t offset = sl_offset_for_drm_format_plane(format, height, stride, i);
    total_size = MAX(total_size, size + offset);
  }

  return total_size;
}

/**
 * Call ioctl, restarting if it is interupted
 */
static inline int
gen_ioctl(int fd, unsigned long request, void *arg)
{
    int ret;

    do {
        ret = ioctl(fd, request, arg);
    } while (ret == -1 && (errno == EINTR || errno == EAGAIN));
    return ret;
}

uint32_t sl_is_intel_gpu() {
#define MAX_DRM_DEVICES 64
  drmDevicePtr devices[MAX_DRM_DEVICES], device;
  int i, ret, num_devices;
  uint32_t is_intel_gpu = 0;
  is_virtio_gpu = 1;

  num_devices = drmGetDevices2(0, devices, MAX_DRM_DEVICES);
  if (num_devices < 0) {
    fprintf(stderr, "drmGetDevices2() returned an error %d\n", num_devices);
    return 0;
  }

  for (i = 0; i < num_devices; i++) {
    device = devices[i];

    /* Skip Non Intel GPU for now*/
    if (device->deviceinfo.pci->vendor_id != 0x8086) {
      continue;
    }

    int drm_node = DRM_NODE_RENDER;
    // Check if this device has available card node.
    if (!(device->available_nodes & 1 << drm_node)) {
      continue;
    }

    is_intel_gpu = 1;
    is_virtio_gpu = 0;
    break;
  }

  for (i = 0; i < num_devices; i++) {
    if (devices[i])
      drmFreeDevice(&devices[i]);
  }
  
  fprintf(stderr, "is_intel_gpu: %d \n", is_intel_gpu);
  return is_intel_gpu;
}



void sl_vifio_initialize_gpu_context() {
  char* disable_user_ptr = getenv("DISABLE_USERPTR");
  uint32_t intel_gpu = sl_is_intel_gpu();
  if ((disable_user_ptr == NULL) && (intel_gpu == 1)) {
    use_frame_buffer_blit = 1;
    fprintf(stderr, "Enabling userptr support. \n");
  }
  
  force_blit = 0;
  char* force_blit = getenv("FORCE_BLIT");
  if ((force_blit != NULL) && (is_virtio_gpu == 1)) {
    force_blit = 1;
    fprintf(stderr, "Enabling userptr support. \n");
  }

  if (initialized == 1)
    return;

  if (sl_initialize_shims()) {
    fprintf(stderr, "Failed to initialize EGL Shims. \n");
    return;
  }

  EGLint num_configs;
  EGLConfig egl_config;
  display = eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, NULL);
  if (display == EGL_NO_DISPLAY) {
    fprintf(stderr, "Failed to get egl display");
    return;
  }

  static const EGLint context_attribs[] = {
    EGL_CONTEXT_CLIENT_VERSION, 3, EGL_CONTEXT_PRIORITY_LEVEL_IMG,
    EGL_CONTEXT_PRIORITY_HIGH_IMG, EGL_NONE};

  static const EGLint config_attribs[] = {EGL_SURFACE_TYPE, EGL_DONT_CARE,
                                          EGL_NONE};

  if (!eglInitialize(display, NULL, NULL)) {
   fprintf(stderr, "egl Initialization failed.");
   return;
  }

  if (!eglChooseConfig(display, config_attribs, &egl_config, 1,
                       &num_configs)) {
   fprintf(stderr, "Failed to choose a valid EGLConfig.");
   return;
  }

  context = eglCreateContext(display, egl_config, EGL_NO_CONTEXT,
                            context_attribs);

  if (context == EGL_NO_CONTEXT) {
    fprintf(stderr, "Failed to create EGL Context.");
    return;
  }

  if (!eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, context)) {
    fprintf(stderr, "Failed to make EGL Context current.");
    return;
  }

  initialized = 1;
}

void sl_destroy_gpu_context() {
  if (eglDestroyContext(display, context) == EGL_FALSE)
    fprintf(stderr, "Failed to destroy context.");
}

static void sl_destroy_egl_resource(struct sl_crosvm_data* meta_data) {
  if (context == EGL_NO_CONTEXT) {
    return;
  }
  
  eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, context);
  if (meta_data->fb > 0) {
    glBindFramebuffer(GL_FRAMEBUFFER, meta_data->fb);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
    glDeleteFramebuffers(1, &meta_data->fb);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    meta_data->fb = 0;
  }

  if (meta_data->texture > 0) {
    glDeleteTextures(1, &meta_data->texture);
    meta_data->texture = 0;
  }

  if (meta_data->image != EGL_NO_IMAGE) {
    eglDestroyImageKHR(display, meta_data->image);
    meta_data->image = EGL_NO_IMAGE;
  }
}

static int sl_initialize_egl_resource(struct sl_crosvm_data* meta_data, bool target) {
  if (!eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, context)) {
    fprintf(stderr, "Failed to make context current.");
    return -1;
  }
  
  fprintf(stderr, "sl_initialize_egl_resource prime handle to fd called2 %d \n", meta_data->prime_fd);

  // Create EGLImage
  const EGLint attr_list_target[] = {
    EGL_WIDTH,
    meta_data->width,
    EGL_HEIGHT,
    meta_data->height,
    EGL_LINUX_DRM_FOURCC_EXT,
    meta_data->format,
    EGL_DMA_BUF_PLANE0_FD_EXT,
    meta_data->prime_fd,
    EGL_DMA_BUF_PLANE0_PITCH_EXT,
    meta_data->stride[0],
    EGL_DMA_BUF_PLANE0_OFFSET_EXT,
    meta_data->offset[0],
    EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT, meta_data->modifier_lo,
    EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT, meta_data->modifier_hi,
    EGL_NONE,
    0
  };

  meta_data->image = eglCreateImageKHR(display, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, NULL, attr_list_target);
  if (meta_data->image == EGL_NO_IMAGE_KHR) {
    fprintf(stderr, "Failed to create EGLimage prime_fd %d \n", meta_data->prime_fd);
    fprintf(stderr, "width %d \n", meta_data->width);
    fprintf(stderr, "height %d \n", meta_data->height);
    fprintf(stderr, "stride0 %d \n", meta_data->stride[0]);
    fprintf(stderr, "offset0 %d \n", meta_data->offset[0]);
    fprintf(stderr, "fourcc format %s \n", sl_get_format_name(meta_data->format));
    return -1;
  }

  // Setup Framebuffer.
  glGenFramebuffers(1, &meta_data->fb);
  glBindFramebuffer(GL_FRAMEBUFFER, meta_data->fb);
  glGenTextures(1, &meta_data->texture);
  glBindTexture(GL_TEXTURE_2D, meta_data->texture);
  glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, (GLeglImageOES)meta_data->image);

  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, meta_data->texture, 0);
  GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
  if (status != GL_FRAMEBUFFER_COMPLETE) {
    switch (status) {
      case (GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT):
        fprintf(stderr, "GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT.");
        break;
      case (GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT):
        fprintf(stderr, "GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT.");
        break;
      case (GL_FRAMEBUFFER_UNSUPPORTED):
        fprintf(stderr, "GL_FRAMEBUFFER_UNSUPPORTED.");
        break;
      default:
        break;
    }
  }

  glGetIntegerv(GL_IMPLEMENTATION_COLOR_READ_FORMAT, &meta_data->gl_format);
  glGetIntegerv(GL_IMPLEMENTATION_COLOR_READ_TYPE, &meta_data->gl_type);

  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glBindTexture(GL_TEXTURE_2D, 0);
  return 0;
}

void sl_vifio_create_prime_buffer(struct wl_client* client,
                                  struct sl_context* ctx,
                                  uint32_t virtwl_fd,
                                  uint32_t drm_fd,
                                  uint32_t id,
                                  int32_t name,
                                  int32_t width,
                                  int32_t height,
                                  uint32_t format,
                                  int32_t offset0,
                                  int32_t stride0,
                                  int32_t offset1,
                                  int32_t stride1,
                                  int32_t offset2,
                                  int32_t stride2) {
  assert(name >= 0);
  assert(!offset1);
  assert(!stride1);
  assert(!offset2);
  assert(!stride2);
  uint32_t buffer_id = name;
  int32_t buffer_offset0 = offset0;
  int32_t buffer_stride0 = stride0;
  int32_t buffer_offset1 = offset1;
  int32_t buffer_stride1 = stride1;
  int32_t buffer_offset2 = offset2;
  int32_t buffer_stride2 = stride2;
  
  if (is_virtio_gpu == 0 || force_blit == 1) {
    struct virtwl_ioctl_new ioctl_new = {
      .type = VIRTWL_IOCTL_NEW_DMABUF,
      .fd = -1,
      .flags = 0,
      .dmabuf = {
          .width = width, .height = height,
          .format = format}};
    int rv;

    rv = ioctl(ctx->virtwl_fd, VIRTWL_IOCTL_NEW, &ioctl_new);
    if (rv) {
      fprintf(stderr, "error: virtwl dmabuf allocation failed: %s\n",
              strerror(errno));
      _exit(EXIT_FAILURE);
    }
    
    buffer_id = ioctl_new.fd;
    buffer_offset0 = ioctl_new.dmabuf.offset0;
    buffer_stride0 = ioctl_new.dmabuf.stride0;
    buffer_offset1 = ioctl_new.dmabuf.offset1;
    buffer_stride1 = ioctl_new.dmabuf.stride1;
    buffer_offset2 = ioctl_new.dmabuf.offset2;
    buffer_stride2 = ioctl_new.dmabuf.stride2;
  }
  
  uint32_t num_planes = sl_num_planes_for_drm_format(format);
  struct zwp_linux_buffer_params_v1* buffer_params =
      zwp_linux_dmabuf_v1_create_params(ctx->linux_dmabuf->internal);
  for (uint32_t i = 0; i < num_planes; i++) {
    zwp_linux_buffer_params_v1_add(buffer_params, buffer_id, 0, buffer_offset0, buffer_stride0,
                                   DRM_FORMAT_MOD_LINEAR >> 32,
                                   DRM_FORMAT_MOD_LINEAR & 0xffffffff);
  }

  struct sl_host_buffer* host_buffer =
      sl_create_host_buffer(client, id,
                            zwp_linux_buffer_params_v1_create_immed(
                                buffer_params, width, height, format, 0),
                            width, height);
  host_buffer->surface = NULL;
  struct sl_crosvm_data* host_callback;
  host_buffer->target_buffer = malloc(sizeof(*host_callback));
  struct sl_crosvm_data* target = host_buffer->target_buffer;
  target->bpp = sl_bpp_for_drm_format(format);
  target->stride[0] = buffer_stride0;
  target->offset[0] = buffer_offset0;
  target->y_ss[0] = sl_y_subsampling_for_drm_format_plane(format, 0);

  target->stride[1] = buffer_stride1;
  target->offset[1] = buffer_offset1;
  target->y_ss[1] = sl_y_subsampling_for_drm_format_plane(format, 1);

  target->stride[2] = buffer_stride2;
  target->offset[2] = buffer_offset2;
  target->y_ss[2] = sl_y_subsampling_for_drm_format_plane(format, 2);

  target->num_planes = num_planes;
  target->width = width;
  target->height = height;
  target->format = format;

  target->virtio_id = buffer_id;
  target->internal = NULL;
  target->buffer_resource = NULL;
  target->imported_bo = NULL;
  target->modifier_hi = DRM_FORMAT_MOD_LINEAR >> 32;
  target->modifier_lo = DRM_FORMAT_MOD_LINEAR & 0xFFFFFFFF;

  target->fb = 0;
  target->image = EGL_NO_IMAGE;
  target->texture = 0;
  target->addr = NULL;
  target->gem_handle = 0;
  target->drm_fd = drm_fd;
  target->prime_fd = 0;
  zwp_linux_buffer_params_v1_destroy(buffer_params);
  
  if (is_virtio_gpu == 1 && force_blit == 0) {
    return;
  }
                                     
  host_buffer->source_buffer = malloc(sizeof(*host_callback));
  struct sl_crosvm_data* source = host_buffer->source_buffer;
  host_buffer->shm_format = format;
  source->bpp = sl_bpp_for_drm_format(format);
  source->stride[0] = stride0;
  source->offset[0] = offset0;
  source->y_ss[0] = sl_y_subsampling_for_drm_format_plane(format, 0);

  source->stride[1] = stride1;
  source->offset[1] = offset1;
  source->y_ss[1] = sl_y_subsampling_for_drm_format_plane(format, 1);

  source->stride[2] = stride2;
  source->offset[2] = offset2;
  source->y_ss[2] = sl_y_subsampling_for_drm_format_plane(format, 2);

  source->num_planes = num_planes;
  source->width = width;
  source->height = height;
  source->format = format;

  source->prime_fd = name;
  source->internal = NULL;
  source->buffer_resource = host_buffer->resource;
  source->addr = NULL;
  source->drm_fd = drm_fd;
  source->virtio_id = 0;
  source->gem_handle = 0;
  source->fb = 0;
  source->texture = 0;

  struct gbm_import_fd_data fd_data;
  fd_data.fd = id;
  fd_data.width = width;
  fd_data.height = height;
  fd_data.stride = stride0;
  fd_data.format = format;

  source->imported_bo = gbm_bo_import(ctx->gbm, GBM_BO_IMPORT_FD, &fd_data, GBM_BO_USE_RENDERING);
  //uint64_t gbm_modifier = gbm_bo_get_modifier(source->imported_bo);
  //source->modifier_hi_gbm = gbm_modifier >> 32;
  //source->modifier_lo_gbm = gbm_modifier & 0xFFFFFFFF;
  struct sl_node *tmpPtr = first_node;
  struct sl_node *followPtr;
  /*fprintf(stderr, "start while loop \n");
  while (tmpPtr != NULL) {
    followPtr = tmpPtr;
    tmpPtr = tmpPtr->next;
    if (followPtr->data.format == source->format) {
      source->modifier_hi = followPtr->data.modifier_hi;
      source->modifier_lo = followPtr->data.modifier_lo;
      break;
    }
  }
  fprintf(stderr, "end while loop \n");*/
  struct drm_prime_handle prime_handle;
  int ret;

  // First imports the prime fd to a gem handle. This will fail if this
  // function was not passed a prime handle that can be imported by the drm
  // device given to sommelier.
  memset(&prime_handle, 0, sizeof(prime_handle));
  prime_handle.fd = source->prime_fd;

  if (gen_ioctl(drm_fd, DRM_IOCTL_PRIME_FD_TO_HANDLE, &prime_handle))
    fprintf(stderr, "Failed to get GEM Handle. \n");

  source->gem_handle = prime_handle.handle;
  struct drm_i915_gem_get_tiling get_tiling = { .handle = source->gem_handle, .tiling_mode=0 };
  if (gen_ioctl(drm_fd, DRM_IOCTL_I915_GEM_GET_TILING, &get_tiling))
    fprintf(stderr, "Failed to get Tiling Mode. \n");

  fprintf(stderr, "get_tiling.tiling_mode %x %d \n", get_tiling.tiling_mode, get_tiling.tiling_mode);
  uint64_t preferred_modifier = 0;
  if (get_tiling.tiling_mode == I915_TILING_NONE)
    preferred_modifier = DRM_FORMAT_MOD_LINEAR;
  else if(get_tiling.tiling_mode == I915_TILING_X)
    preferred_modifier = I915_FORMAT_MOD_X_TILED;
  else if(get_tiling.tiling_mode == I915_TILING_Y)
    preferred_modifier = I915_FORMAT_MOD_Y_TILED;

  source->modifier_hi = preferred_modifier  >> 32;
  source->modifier_lo = preferred_modifier  & 0xFFFFFFFF;
  sl_initialize_egl_resource(source, false);
  
  // Target Buffer
  int pagesize = getpagesize();
  size_t size = ALIGN(sl_size_for_drm_format(target->format, target->height, target->stride[0]), pagesize);
  fprintf(stderr, "size for mmap %zu width %d height %d offset %d stride %d source_stride %d virtio_id %d \n", size, target->width, target->height, target->offset[0], target->stride[0], source->stride[0], target->virtio_id);
  target->addr = mmap(NULL, size, PROT_WRITE, MAP_SHARED, target->virtio_id, 0);
  if (!use_frame_buffer_blit) {
    return;
  }

  if (target->addr) {
    struct drm_i915_gem_userptr arg = {
        .user_ptr = (uintptr_t)target->addr,
        .user_size = size,
    };
    if (gen_ioctl(drm_fd, DRM_IOCTL_I915_GEM_USERPTR, &arg))
      fprintf(stderr, "Failed to create user ptr \n");

    target->gem_handle = arg.handle;
    if (target->gem_handle > 0) {
      struct drm_prime_handle prime_request = {
        .handle = target->gem_handle,
        .flags  = DRM_CLOEXEC | DRM_RDWR,
        .fd     = -1
      };
fprintf(stderr, "prime handle to fd called \n");

      if (gen_ioctl(drm_fd, DRM_IOCTL_PRIME_HANDLE_TO_FD, &prime_request))
        fprintf(stderr, "Failed to create prime handle from user ptr \n");

      target->prime_fd = prime_request.fd;
      fprintf(stderr, "prime handle to fd called2 %d \n", prime_request.fd);
    }

    sl_initialize_egl_resource(target, true);
  }
}

static void bind_framebuffer(struct sl_crosvm_data* target, bool draw) {
  glBindFramebuffer(draw ? GL_DRAW_FRAMEBUFFER : GL_READ_FRAMEBUFFER, target->fb);
  GLenum status = glCheckFramebufferStatus(draw ? GL_DRAW_FRAMEBUFFER : GL_READ_FRAMEBUFFER);

  if (status != GL_FRAMEBUFFER_COMPLETE) {
    switch (status) {
      case (GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT):
        fprintf(stderr, "GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT.");
         return;
         break;
       case (GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT):
         fprintf(stderr, "GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT.");
         return;
         break;
       case (GL_FRAMEBUFFER_UNSUPPORTED):
         fprintf(stderr, "GL_FRAMEBUFFER_UNSUPPORTED.");
         return;
         break;
       default:
         break;
    }
  }
}

void sl_update_host_surface(struct sl_host_surface* host) {
  struct sl_crosvm_data* source = host->source;
  struct sl_crosvm_data* target = host->target;
  if (!source || !target) {
    if ((is_virtio_gpu == 1 && force_blit == 1)  || (is_virtio_gpu == 0)) {
      fprintf(stderr, "Not valid source or target source: %p target: %p \n", source, target);
    }
      
    return;
  }

  if (!eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, context)) {
    fprintf(stderr, "Failed to make context current.");
    return;
  }

  // Setup Viewport and enable needed settings.
  glViewport(0, 0, source->width, source->height);

  // Setup Target Framebuffer.
  if (use_frame_buffer_blit) {
    bind_framebuffer(target, true);
    glBindTexture(GL_TEXTURE_2D, target->texture);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target->texture, 0);
    //glClear(GL_COLOR_BUFFER_BIT);
  }

   // Setup Read Framebuffer.
  bind_framebuffer(source, false);

  //sl_dmabuf_begin_write(target->prime_fd);
  sl_virtwl_dmabuf_begin_write(target->virtio_id);
  if (use_frame_buffer_blit) {
    glBlitFramebuffer(0, 0, source->width, source->height, 0, 0, source->width, source->height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
  } else {
    glReadPixels(0, 0, source->width, source->height, source->gl_format, source->gl_type, target->addr);
  }

  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glBindTexture(GL_TEXTURE_2D, 0);
  //sl_dmabuf_end_write(target->prime_fd);
  sl_virtwl_dmabuf_end_write(target->virtio_id);
}

void sl_vfio_destroy_resource(struct sl_crosvm_data* resource) {
  sl_vfio_unmap_resource(resource);
  sl_destroy_egl_resource(resource);

  if (resource->imported_bo) {
    gbm_bo_destroy(resource->imported_bo);
  }

  if (resource->prime_fd > 0) {
    fprintf(stderr, "closed prime fd %d \n", resource->prime_fd);
    close(resource->prime_fd);
    resource->prime_fd = 0;
  }
  
  // Always close the handle we imported.
  if (resource->gem_handle > 0) {
    struct drm_gem_close gem_close;
    memset(&gem_close, 0, sizeof(gem_close));
    gem_close.handle = resource->gem_handle;
    if (gen_ioctl(resource->drm_fd, DRM_IOCTL_GEM_CLOSE, &gem_close))
      fprintf(stderr, "Failed to close GEM handle %d \n", resource->gem_handle);
  }

  if (resource->virtio_id > 0) {
  fprintf(stderr, "closed virtio_id fd %d \n", resource->virtio_id);
    close(resource->virtio_id);
    resource->virtio_id = 0;
  }
}

void sl_vfio_unmap_resource(struct sl_crosvm_data* resource) {
  if (resource->addr) {
    sl_destroy_egl_resource(resource);
    int pagesize = getpagesize();
    size_t size = ALIGN(sl_size_for_drm_format(resource->format, resource->height, resource->stride[0]), pagesize);
    munmap(resource->addr, size);
    resource->addr = NULL;
  }
}

void sl_vifio_add_modifier(uint32_t format, uint32_t modifier_hi, uint32_t modifier_lo) {
  struct sl_node* temp = (struct sl_node*) malloc(sizeof (struct sl_node));
  if (temp == NULL) {
    exit(0); // no memory available
  }

  temp->data.modifier_hi = modifier_hi;
  temp->data.modifier_lo = modifier_lo;
  temp->data.format = format;
  temp->next = NULL;

  if (last_node) {
    last_node->next = temp;
  }

  if (!first_node) {
    first_node = temp;
  }

  last_node = temp;
}

void sl_vifio_free_modifier_data() {
  struct sl_node *tmpPtr = first_node;
  struct sl_node *followPtr;
  while (tmpPtr != NULL) {
    followPtr = tmpPtr;
    tmpPtr = tmpPtr->next;
    free(followPtr);
  }

  return NULL;
}
