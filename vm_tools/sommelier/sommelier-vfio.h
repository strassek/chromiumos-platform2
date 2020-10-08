#ifndef SOMMELIER_EGL_H_
#define SOMMELIER_EGL_H_

#include <EGL/egl.h>
#include <EGL/eglext.h>

#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#include <gbm.h>

#include <pixman.h>
#include <wayland-client.h>
#include <wayland-util.h>
#include "sommelier.h"

#define EGL_EGLEXT_PROTOTYPES

#include <EGL/egl.h>
#include <EGL/eglext.h>

#ifndef GL_GLEXT_PROTOTYPES
#define GL_GLEXT_PROTOTYPES
#endif

#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

/*struct sl_gpu_context {
  EGLDisplay display;
  EGLContext context;
};*/

struct sl_crosvm_data {
  uint32_t prime_fd;
  uint32_t virtio_id;
  uint32_t gem_handle;
  uint32_t drm_fd;
  uint8_t* addr;
  int32_t width;
  int32_t height;
  uint32_t format;
  uint32_t modifier_hi;
  uint32_t modifier_lo;
  uint32_t modifier_hi_gbm;
  uint32_t modifier_lo_gbm;
  size_t offset[3];
  size_t y_ss[3];
  size_t stride[3];
  int32_t bpp;
  int32_t num_planes;
  uint32_t host_resource;
  struct wl_buffer* internal;
  struct wl_resource* buffer_resource;
  struct gbm_bo* imported_bo;
  uint32_t fb;
  uint32_t texture;
  EGLImage image;
  GLint gl_format;
  GLint gl_type;
};

//int sl_initialize_gpu_context(struct sl_gpu_context* gpu_context);
//void sl_destroy_gpu_context(struct sl_gpu_context* gpu_context);

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
                                       int32_t stride2);

void sl_update_host_surface(struct sl_host_surface* host);
void sl_vfio_destroy_resource(struct sl_crosvm_data* resource);
void sl_vfio_unmap_resource(struct sl_crosvm_data* resource);
void sl_vifio_add_modifier(uint32_t format, uint32_t modifier_hi, uint32_t modifier_lo);
void sl_vifio_free_modifier_data();
// Initialize EGL Resources.
void sl_vifio_initialize_gpu_context();

#endif  // SOMMELIER_EGL_H_
