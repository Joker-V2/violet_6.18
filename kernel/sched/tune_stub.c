// SPDX-License-Identifier: GPL-2.0
/*
 * Stub schedtune cgroup controller.
 *
 * Android userspace built for 4.14/4.19 kernels still mounts a "schedtune"
 * cgroup v1 hierarchy at /dev/stune, and libprocessgroup gives up on the
 * whole cgroup setup when that mount fails. That leaves /sys/fs/cgroup
 * unmounted and init cannot start any service.
 *
 * This controller accepts the mount and keeps the knobs userspace writes,
 * without acting on them: on 6.18 task boosting goes through uclamp.
 */

#include <linux/cgroup.h>
#include <linux/err.h>
#include <linux/slab.h>

struct schedtune {
	struct cgroup_subsys_state css;
	s64 boost;
	u64 prefer_idle;
	u64 prefer_high_cap;
	u64 colocate;
	u64 sched_boost_enabled;
	u64 sched_boost_no_override;
};

static inline struct schedtune *css_st(struct cgroup_subsys_state *css)
{
	return container_of(css, struct schedtune, css);
}

static struct cgroup_subsys_state *
schedtune_css_alloc(struct cgroup_subsys_state *parent_css)
{
	struct schedtune *st = kzalloc(sizeof(*st), GFP_KERNEL);

	if (!st)
		return ERR_PTR(-ENOMEM);
	st->sched_boost_enabled = 1;
	return &st->css;
}

static void schedtune_css_free(struct cgroup_subsys_state *css)
{
	kfree(css_st(css));
}

static s64 boost_read(struct cgroup_subsys_state *css, struct cftype *cft)
{
	return css_st(css)->boost;
}

static int boost_write(struct cgroup_subsys_state *css, struct cftype *cft,
		       s64 val)
{
	if (val < -100 || val > 100)
		return -EINVAL;
	css_st(css)->boost = val;
	return 0;
}

#define ST_U64(name)							\
static u64 name##_read(struct cgroup_subsys_state *css,		\
		       struct cftype *cft)				\
{									\
	return css_st(css)->name;					\
}									\
static int name##_write(struct cgroup_subsys_state *css,		\
			struct cftype *cft, u64 val)			\
{									\
	css_st(css)->name = !!val;					\
	return 0;							\
}

ST_U64(prefer_idle)
ST_U64(prefer_high_cap)
ST_U64(colocate)
ST_U64(sched_boost_enabled)
ST_U64(sched_boost_no_override)

#define ST_FILE(n) { .name = #n, .read_u64 = n##_read, .write_u64 = n##_write }

static struct cftype schedtune_files[] = {
	{
		.name = "boost",
		.read_s64 = boost_read,
		.write_s64 = boost_write,
	},
	ST_FILE(prefer_idle),
	ST_FILE(prefer_high_cap),
	ST_FILE(colocate),
	ST_FILE(sched_boost_enabled),
	ST_FILE(sched_boost_no_override),
	{ }	/* terminate */
};

struct cgroup_subsys schedtune_cgrp_subsys = {
	.css_alloc	= schedtune_css_alloc,
	.css_free	= schedtune_css_free,
	.legacy_cftypes	= schedtune_files,
};
