#include <assert.h>
#include <stdio.h>
#include <syscall.h>
#include <stdint.h>

#include <drm/drm.h>
#include <drm/radeon_drm.h>
#include <drm/i915_drm.h>

#include "recorder.h"
#include "write_trace.h"
#include "../share/ipc.h"
#include "../share/sys.h"
#include "../share/types.h"


void handle_ioctl_request(struct context *ctx, int request)
{
	pid_t tid = ctx->child_tid;
	int syscall = SYS_ioctl;

	struct user_regs_struct regs;
	read_child_registers(tid, &regs);

	if ((request >> 31) & 0x1) {
		int size = _IOC_SIZE(request);

		switch (request) {

		/*if (request == FIONREAD) {
		 record_child_data(tid, syscall, sizeof(int), regs.edx);
		 } else if (request == TCGETS) {
		 record_child_data(tid, syscall, sizeof(struct termios), regs.edx);
		 } else if (request == TIOCGWINSZ) {
		 record_child_data(tid, syscall, sizeof(struct winsize), regs.edx);
		 } else {*/

		/* requests starting with '64' (= 'd') */
		case DRM_IOCTL_VERSION:
		{
			assert(size == sizeof(struct drm_version));
			struct drm_version *version = read_child_data(ctx, sizeof(struct drm_version), regs.edx);

			record_child_data(ctx, syscall, sizeof(struct drm_version), regs.edx);
			record_child_data(ctx, syscall, version->name_len, (long int) version->name);
			record_child_data(ctx, syscall, version->date_len, (long int) version->date);
			record_child_data(ctx, syscall, version->desc_len, (long int) version->desc);

			sys_free((void**) &version);
			break;
		}


		case DRM_IOCTL_GET_MAGIC:
		{
			assert(size == sizeof(struct drm_auth));
			record_child_data(ctx, syscall, sizeof(struct drm_auth), regs.edx);
			break;
		}

		case DRM_IOCTL_RADEON_INFO:
		{
			assert(size == sizeof(struct drm_radeon_info));
			record_child_data(ctx, syscall, sizeof(struct drm_radeon_info), regs.esi);
			break;
		}

		case DRM_IOCTL_I915_GEM_PWRITE:
		{
			assert(size == sizeof(struct drm_i915_gem_pwrite));
			struct drm_i915_gem_pwrite* tmp = read_child_data(ctx, size, regs.edx);
			record_child_data(ctx, syscall, sizeof(struct drm_i915_gem_pwrite), regs.edx);
			record_child_data(ctx, syscall, tmp->size, (long int) tmp->data_ptr);
			sys_free((void**) &tmp);
			break;
		}

		case DRM_IOCTL_RADEON_GEM_CREATE:
		{
			assert(size == sizeof(struct drm_radeon_gem_create));
			record_child_data(ctx, syscall, sizeof(struct drm_radeon_gem_create), regs.edx);
			break;
		}

		case DRM_IOCTL_I915_GEM_MMAP:
		{
			assert(size == sizeof(struct drm_i915_gem_mmap));
			struct drm_i915_gem_mmap* tmp = read_child_data_tid(tid, sizeof(struct drm_i915_gem_mmap), regs.edx);
			record_child_data(ctx, syscall, sizeof(struct drm_i915_gem_mmap), regs.edx);
			record_child_data(ctx, syscall, tmp->size, tmp->addr_ptr + tmp->offset);
			sys_free((void**) &tmp);
			break;
		}

		case DRM_IOCTL_GEM_OPEN:
		{
			assert(size == sizeof(struct drm_gem_open));
			struct drm_gem_open* tmp = read_child_data_tid(tid, size, regs.edx);
			record_child_data(ctx, syscall, sizeof(struct drm_gem_open), regs.edx);
			record_child_data(ctx, syscall, tmp->size, tmp->handle);
			sys_free((void**) &tmp);
			break;
		}

		case DRM_IOCTL_RADEON_GEM_GET_TILING:
		{
			assert(size == sizeof(struct drm_radeon_gem_get_tiling));
			record_child_data(ctx, syscall, sizeof(struct drm_radeon_gem_get_tiling), regs.edx);
			break;
		}
		default:
		fprintf(stderr, "Unknown ioctl request: %x -- bailing out\n", request);
		sys_exit();
		}
	}
}
