// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Module sysfs support
 *
 * Copyright (C) 2008 Rusty Russell
 *
 * This file exposes per-module metadata under /sys/module/<name>/:
 *
 *   sections/   one binary attr per non-empty ELF section (load address)
 *   notes/      one binary attr per SHT_NOTE section  (raw note bytes)
 *   holders/    symlinks to modules that depend on this one
 *   parameters/ one attr per module_param*()
 *   <modinfo>   one attr per MODULE_*() macro that has a matching
 *               modinfo attribute (author, license, ...)
 *
 * Everything here is read-mostly. The only writes accepted are through
 * parameter attributes (which are backed by module_param_sysfs_setup()).
 *
 * NOTE: The sections/ and notes/ subtrees are only meaningful when
 * CONFIG_KALLSYMS is enabled; when it is not, they are compiled out and
 * the add_*/remove_*_attrs() helpers become no-ops. Add-notes depends
 * on the section-attribute name cache maintained by add-sect; the call
 * order in mod_sysfs_setup() encodes this dependency.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/sysfs.h>
#include <linux/slab.h>
#include <linux/kallsyms.h>
#include <linux/mutex.h>
#include "internal.h"

/*
 * =====================================================================
 * /sys/module/<name>/sections/<section-name>
 *
 * Each non-empty ELF section is exposed as a read-only binary attribute
 * that returns the load address of the section as text:
 *
 *     "0x%px\n"
 *
 * The value is subject to kallsyms_show_value(file->f_cred): callers
 * without the required capability (typically CAP_SYSLOG) see a NULL
 * pointer printed instead, so the format stays well-formed but no
 * kernel address is leaked.
 *
 * J. Corbet <corbet@lwn.net>
 * =====================================================================
 */
#ifdef CONFIG_KALLSYMS
struct module_sect_attrs {
	struct attribute_group grp;
	struct bin_attribute attrs[];
};

/*
 * Maximum length of one section's textual representation:
 *
 *      "0x"                     2 bytes (prefix)
 *      BITS_PER_LONG / 4        BITS_PER_LONG hex digits
 *      "\n"                     1 byte  (trailing newline)
 *
 * The NUL written by scnprintf() is NOT included here; the caller
 * sizes its bounce buffer as MODULE_SECT_READ_SIZE + 1 for that.
 */
#define MODULE_SECT_READ_SIZE (3 /* "0x", "\n" */ + (BITS_PER_LONG / 4))

static ssize_t module_sect_read(struct file *file, struct kobject *kobj,
				const struct bin_attribute *battr,
				char *buf, loff_t pos, size_t count)
{
	char bounce[MODULE_SECT_READ_SIZE + 1];
	size_t wrote;

	/*
	 * Sysfs binary attributes are stream-like: a read of the whole
	 * file is the only meaningful operation, so any offset other
	 * than 0 is a userspace error. Return -EINVAL (rather than 0)
	 * to make the mistake loud instead of silently truncating.
	 */
	if (pos != 0)
		return -EINVAL;

	/*
	 * We are a binary read handler, so the return value is a byte
	 * count, not a string length. But scnprintf() always appends a
	 * NUL terminator: if we formatted directly into @buf, and @buf
	 * were exactly MODULE_SECT_READ_SIZE bytes, the NUL would either
	 * be truncated (making @count look like a truncated read) or
	 * land on the last byte and overwrite the "\n". Neither is
	 * acceptable, and there is no way to ask scnprintf() not to
	 * write a terminator. So we format into a bounce buffer that is
	 * guaranteed to have room for the NUL, then memcpy() only the
	 * bytes the caller asked for.
	 *
	 * The ternary is deliberate: when kallsyms_show_value() denies
	 * access we pass NULL to %px, which prints "(null)" on modern
	 * kernels (still within MODULE_SECT_READ_SIZE for any word
	 * size). Do NOT replace this with a raw %p, which would defeat
	 * the security check.
	 */
	wrote = scnprintf(bounce, sizeof(bounce), "0x%px\n",
			  kallsyms_show_value(file->f_cred)
				? battr->private : NULL);
	count = min(count, wrote);
	memcpy(buf, bounce, count);

	return count;
}

static void free_sect_attrs(struct module_sect_attrs *sect_attrs)
{
	const struct bin_attribute *const *bin_attr;

	/*
	 * Free the strings we strdup'ed, then the NULL-terminated
	 * pointer array, then the containing struct. The array is
	 * NULL-terminated by construction (see add_sect_attrs), so we
	 * can safely walk it even if we are on a partial-failure path.
	 */
	for (bin_attr = sect_attrs->grp.bin_attrs; *bin_attr; bin_attr++)
		kfree((*bin_attr)->attr.name);
	kfree(sect_attrs->grp.bin_attrs);
	kfree(sect_attrs);
}

static int add_sect_attrs(struct module *mod, const struct load_info *info)
{
	struct module_sect_attrs *sect_attrs;
	const struct bin_attribute **gattr;
	struct bin_attribute *sattr;
	unsigned int nloaded = 0, i;
	int ret;

	/*
	 * Count loaded sections and allocate the two-piece structure:
	 *   - one module_sect_attrs holding the group and an inline
	 *     flexible array of nloaded bin_attribute objects
	 *   - one NULL-terminated array of pointers to those objects
	 *     (required by struct attribute_group::bin_attrs)
	 */
	for (i = 0; i < info->hdr->e_shnum; i++)
		if (!sect_empty(&info->sechdrs[i]))
			nloaded++;
	sect_attrs = kzalloc_flex(*sect_attrs, attrs, nloaded);
	if (!sect_attrs)
		return -ENOMEM;

	gattr = kzalloc_objs(*gattr, nloaded + 1);
	if (!gattr) {
		kfree(sect_attrs);
		return -ENOMEM;
	}

	/* Setup section attributes. */
	sect_attrs->grp.name = "sections";
	sect_attrs->grp.bin_attrs = gattr;

	sattr = &sect_attrs->attrs[0];
	for (i = 0; i < info->hdr->e_shnum; i++) {
		Elf_Shdr *sec = &info->sechdrs[i];

		if (sect_empty(sec))
			continue;
		sysfs_bin_attr_init(sattr);
		sattr->attr.name =
			kstrdup(info->secstrings + sec->sh_name, GFP_KERNEL);
		if (!sattr->attr.name) {
			ret = -ENOMEM;
			goto out;
		}
		sattr->read = module_sect_read;
		sattr->private = (void *)sec->sh_addr;
		sattr->size = MODULE_SECT_READ_SIZE;
		/*
		 * Mode 0400 (root-read only). The actual leak check is
		 * done inside module_sect_read() via kallsyms_show_value(),
		 * so this is defence in depth.
		 */
		sattr->attr.mode = 0400;
		*(gattr++) = sattr++;
	}

	/*
	 * Note: @gattr now points one past the last filled slot, which
	 * is the NULL terminator we pre-allocated with nloaded + 1.
	 * The group can therefore be walked safely even on error paths
	 * below.
	 */
	ret = sysfs_create_group(&mod->mkobj.kobj, &sect_attrs->grp);
	if (ret)
		goto out;

	mod->sect_attrs = sect_attrs;
	return 0;
out:
	free_sect_attrs(sect_attrs);
	return ret;
}

static void remove_sect_attrs(struct module *mod)
{
	if (mod->sect_attrs) {
		sysfs_remove_group(&mod->mkobj.kobj,
				   &mod->sect_attrs->grp);
		/*
		 * We are positive that no one is using any sect attrs
		 * at this point.  Deallocate immediately.
		 */
		free_sect_attrs(mod->sect_attrs);
		mod->sect_attrs = NULL;
	}
}

/*
 * =====================================================================
 * /sys/module/<name>/notes/<section-name>
 *
 * Exposes the raw contents of every SHT_NOTE section as a read-only
 * binary attribute. The names are shared with sections/ — this is why
 * add_notes_attrs() requires mod->sect_attrs to already be populated.
 * =====================================================================
 */
struct module_notes_attrs {
	struct attribute_group grp;
	struct bin_attribute attrs[];
};

static void free_notes_attrs(struct module_notes_attrs *notes_attrs)
{
	/*
	 * The names in ->attr.name are shared with the section attrs
	 * (not owned by us), so we must not kfree() them here. Only the
	 * pointer array and the containing struct belong to us.
	 */
	kfree(notes_attrs->grp.bin_attrs);
	kfree(notes_attrs);
}

static int add_notes_attrs(struct module *mod, const struct load_info *info)
{
	unsigned int notes, loaded, i;
	struct module_notes_attrs *notes_attrs;
	const struct bin_attribute **gattr;
	struct bin_attribute *nattr;
	int ret;

	/* Count notes sections and allocate structures.  */
	notes = 0;
	for (i = 0; i < info->hdr->e_shnum; i++)
		if (!sect_empty(&info->sechdrs[i]) &&
		    info->sechdrs[i].sh_type == SHT_NOTE)
			++notes;

	if (notes == 0)
		return 0;

	notes_attrs = kzalloc_flex(*notes_attrs, attrs, notes);
	if (!notes_attrs)
		return -ENOMEM;

	gattr = kzalloc_objs(*gattr, notes + 1);
	if (!gattr) {
		kfree(notes_attrs);
		return -ENOMEM;
	}

	notes_attrs->grp.name = "notes";
	notes_attrs->grp.bin_attrs = gattr;

	/*
	 * Walk all non-empty sections in order. @loaded tracks the
	 * index into mod->sect_attrs->attrs[] of the section we are
	 * currently looking at. This works because add_sect_attrs()
	 * also walks non-empty sections in the same order, so the
	 * Nth non-empty section has its name cached at
	 * mod->sect_attrs->attrs[N].attr.name.
	 *
	 * NOTE: This is the sole hidden coupling between the two
	 * functions. If add_sect_attrs() ever changes its iteration
	 * order or filtering rule, this must change too.
	 */
	nattr = &notes_attrs->attrs[0];
	for (loaded = i = 0; i < info->hdr->e_shnum; ++i) {
		if (sect_empty(&info->sechdrs[i]))
			continue;
		if (info->sechdrs[i].sh_type == SHT_NOTE) {
			sysfs_bin_attr_init(nattr);
			nattr->attr.name = mod->sect_attrs->attrs[loaded].attr.name;
			nattr->attr.mode = 0444;
			nattr->size = info->sechdrs[i].sh_size;
			nattr->private = (void *)info->sechdrs[i].sh_addr;
			nattr->read = sysfs_bin_attr_simple_read;
			*(gattr++) = nattr++;
		}
		++loaded;
	}

	ret = sysfs_create_group(&mod->mkobj.kobj, &notes_attrs->grp);
	if (ret)
		goto out;

	mod->notes_attrs = notes_attrs;
	return 0;

out:
	free_notes_attrs(notes_attrs);
	return ret;
}

static void remove_notes_attrs(struct module *mod)
{
	if (mod->notes_attrs) {
		sysfs_remove_group(&mod->mkobj.kobj,
				   &mod->notes_attrs->grp);
		/*
		 * We are positive that no one is using any notes attrs
		 * at this point.  Deallocate immediately.
		 */
		free_notes_attrs(mod->notes_attrs);
		mod->notes_attrs = NULL;
	}
}

#else /* !CONFIG_KALLSYMS */
/*
 * Without kallsyms there are no addresses to leak, so both subtrees
 * are compiled away entirely. The inline stubs keep mod_sysfs_setup()
 * and mod_sysfs_teardown() free of #ifdef clutter.
 */
static inline int add_sect_attrs(struct module *mod,
				 const struct load_info *info)
{
	return 0;
}
static inline void remove_sect_attrs(struct module *mod) { }
static inline int add_notes_attrs(struct module *mod,
				  const struct load_info *info)
{
	return 0;
}
static inline void remove_notes_attrs(struct module *mod) { }
#endif /* CONFIG_KALLSYMS */

/*
 * =====================================================================
 * /sys/module/<name>/holders/
 *
 * For every module that depends on @mod, create a symlink from that
 * module's holders directory back to @mod. This lets userspace discover
 * the dependency graph without parsing /proc/modules.
 *
 * The module_mutex protects mod->target_list; every other module in
 * that list could be unloading concurrently, so we cannot walk it
 * locklessly.
 * =====================================================================
 */
static void del_usage_links(struct module *mod)
{
#ifdef CONFIG_MODULE_UNLOAD
	struct module_use *use;

	mutex_lock(&module_mutex);
	list_for_each_entry(use, &mod->target_list, target_list)
		sysfs_remove_link(use->target->holders_dir, mod->name);
	mutex_unlock(&module_mutex);
#endif
}

static int add_usage_links(struct module *mod)
{
	int ret = 0;
#ifdef CONFIG_MODULE_UNLOAD
	struct module_use *use;

	mutex_lock(&module_mutex);
	list_for_each_entry(use, &mod->target_list, target_list) {
		ret = sysfs_create_link(use->target->holders_dir,
					&mod->mkobj.kobj, mod->name);
		if (ret)
			break;
	}
	mutex_unlock(&module_mutex);
	/*
	 * Roll back any links we created before the failure, so the
	 * caller's error path does not have to know about partial
	 * state. del_usage_links() is idempotent (it silently ignores
	 * missing links).
	 */
	if (ret)
		del_usage_links(mod);
#endif
	return ret;
}

/*
 * =====================================================================
 * /sys/module/<name>/<modinfo-attr>
 *
 * Copies the static modinfo_attrs[] table into a per-module array so
 * that each module can hold its own sysfs_attribute state, call its
 * own ->free callback, and have attrs that fail ->test() filtered out.
 *
 * The array is NULL-terminated by the zeroed last slot from kzalloc();
 * that terminator is what free_*() walks to.
 * =====================================================================
 */
static void module_remove_modinfo_attrs(struct module *mod, int end)
{
	const struct module_attribute *attr;
	int i;

	for (i = 0; (attr = &mod->modinfo_attrs[i]); i++) {
		/*
		 * @end == -1 means "remove everything"; otherwise it is
		 * the last index to remove (used by the error path in
		 * module_add_modinfo_attrs()).
		 */
		if (end >= 0 && i > end)
			break;
		/* pick a field to test for end of list */
		if (!attr->attr.name)
			break;
		sysfs_remove_file(&mod->mkobj.kobj, &attr->attr);
		if (attr->free)
			attr->free(mod);
	}
	kfree(mod->modinfo_attrs);
}

static int module_add_modinfo_attrs(struct module *mod)
{
	const struct module_attribute *attr;
	struct module_attribute *temp_attr;
	int error = 0;
	int i;

	mod->modinfo_attrs = kzalloc((sizeof(struct module_attribute) *
					(modinfo_attrs_count + 1)),
					GFP_KERNEL);
	if (!mod->modinfo_attrs)
		return -ENOMEM;

	temp_attr = mod->modinfo_attrs;
	for (i = 0; (attr = modinfo_attrs[i]); i++) {
		if (!attr->test || attr->test(mod)) {
			memcpy(temp_attr, attr, sizeof(*temp_attr));
			sysfs_attr_init(&temp_attr->attr);
			error = sysfs_create_file(&mod->mkobj.kobj,
						  &temp_attr->attr);
			if (error)
				goto error_out;
			++temp_attr;
		}
	}

	return 0;

error_out:
	/*
	 * We failed while processing index @i, so only indices 0..i-1
	 * are known-good. module_remove_modinfo_attrs(mod, i - 1) will
	 * remove exactly those, and stop early on the still-zeroed
	 * entry at index @i.
	 *
	 * If i == 0, nothing was ever created; just free the array.
	 */
	if (i > 0)
		module_remove_modinfo_attrs(mod, --i);
	else
		kfree(mod->modinfo_attrs);
	return error;
}

/*
 * =====================================================================
 * Module kobject lifecycle
 * =====================================================================
 */

/*
 * Synchronously release the module's kobject. We install a completion
 * on the kobject so that we can wait until the sysfs release callback
 * has actually run before returning — otherwise the next load of the
 * same module could race with the dying kobject.
 */
static void mod_kobject_put(struct module *mod)
{
	DECLARE_COMPLETION_ONSTACK(c);

	mod->mkobj.kobj_completion = &c;
	kobject_put(&mod->mkobj.kobj);
	wait_for_completion(&c);
}

static int mod_sysfs_init(struct module *mod)
{
	int err;
	struct kobject *kobj;

	if (!module_kset) {
		pr_err("%s: module sysfs not initialized\n", mod->name);
		err = -EINVAL;
		goto out;
	}

	/*
	 * kset_find_obj() takes a reference on any match; if we find one
	 * it means the same module name is already registered in sysfs.
	 * Refuse to proceed and drop the reference we just took.
	 */
	kobj = kset_find_obj(module_kset, mod->name);
	if (kobj) {
		pr_err("%s: module is already loaded\n", mod->name);
		kobject_put(kobj);
		err = -EINVAL;
		goto out;
	}

	mod->mkobj.mod = mod;

	/*
	 * Zero only the embedded kobject, not the whole mkobj. The
	 * surrounding fields (mod, drivers_dir, mp, kobj_completion)
	 * have their own lifecycle rules and must not be clobbered:
	 * drivers_dir is owned by module_param_sysfs_setup(), and
	 * kobj_completion is installed transiently above.
	 *
	 * This reset matters when a module with the same name was
	 * previously loaded: kobject_init_and_add() can fail after
	 * partially initialising the kobject, so we cannot rely on the
	 * previous teardown having fully reset it.
	 */
	memset(&mod->mkobj.kobj, 0, sizeof(mod->mkobj.kobj));
	mod->mkobj.kobj.kset = module_kset;
	err = kobject_init_and_add(&mod->mkobj.kobj, &module_ktype, NULL,
				   "%s", mod->name);
	if (err)
		mod_kobject_put(mod);

out:
	return err;
}

int mod_sysfs_setup(struct module *mod,
		    const struct load_info *info,
		    struct kernel_param *kparam,
		    unsigned int num_params)
{
	int err;

	err = mod_sysfs_init(mod);
	if (err)
		goto out;

	mod->holders_dir = kobject_create_and_add("holders", &mod->mkobj.kobj);
	if (!mod->holders_dir) {
		err = -ENOMEM;
		goto out_unreg;
	}

	err = module_param_sysfs_setup(mod, kparam, num_params);
	if (err)
		goto out_unreg_holders;

	err = module_add_modinfo_attrs(mod);
	if (err)
		goto out_unreg_param;

	err = add_usage_links(mod);
	if (err)
		goto out_unreg_modinfo_attrs;

	/*
	 * Order matters: add_notes_attrs() depends on the section-name
	 * cache built by add_sect_attrs().
	 */
	err = add_sect_attrs(mod, info);
	if (err)
		goto out_del_usage_links;

	err = add_notes_attrs(mod, info);
	if (err)
		goto out_unreg_sect_attrs;

	return 0;

	/*
	 * Unwind in exact reverse order of setup. Each label removes
	 * exactly the step that succeeded immediately before the failing
	 * one, so no partial state survives a failure.
	 */
out_unreg_sect_attrs:
	remove_sect_attrs(mod);
out_del_usage_links:
	del_usage_links(mod);
out_unreg_modinfo_attrs:
	module_remove_modinfo_attrs(mod, -1);
out_unreg_param:
	module_param_sysfs_remove(mod);
out_unreg_holders:
	kobject_put(mod->holders_dir);
out_unreg:
	mod_kobject_put(mod);
out:
	return err;
}

static void mod_sysfs_fini(struct module *mod)
{
	remove_notes_attrs(mod);
	remove_sect_attrs(mod);
	mod_kobject_put(mod);
}

void mod_sysfs_teardown(struct module *mod)
{
	del_usage_links(mod);
	module_remove_modinfo_attrs(mod, -1);
	module_param_sysfs_remove(mod);
	kobject_put(mod->mkobj.drivers_dir);
	kobject_put(mod->holders_dir);
	mod_sysfs_fini(mod);
}

void init_param_lock(struct module *mod)
{
	mutex_init(&mod->param_lock);
}