// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Module taint unload tracking support
 *
 * Copyright (C) 2022 Aaron Tomlin
 *
 * Records modules that were unloaded while carrying one or more taint
 * flags, so that userspace (and developers reading dmesg) can see that
 * a tainted module was removed without rebooting.
 *
 * Tracked modules are kept in a single RCU-protected list, keyed by
 * (name, taints) so that unload/reload cycles of the same tainted
 * module accumulate a counter instead of growing the list.
 *
 * The list is exposed through two read-only interfaces:
 *
 *   dmesg                             print_unloaded_tainted_modules()
 *   /sys/kernel/debug/unloaded_tainted  (CONFIG_DEBUG_FS)
 *
 * Concurrency model
 * -----------------
 * Writers (try_add_tainted_module) run in the module unload path and are
 * serialised by module_mutex; they also publish entries with
 * list_add_rcu() so readers never block. Readers (both dmesg printing
 * and the debugfs seq_file) only take rcu_read_lock() and rely on RCU
 * for lifetime management. list_for_each_entry_rcu() therefore asserts
 * that either RCU is held or module_mutex is held, which is how a
 * single traversal macro can serve both classes of caller.
 *
 * NOTE: There is no module_exit() here because the debugfs file is
 * registered with debugfs_create_file() and torn down automatically by
 * the debugfs subsystem; the tracked list itself is intentionally
 * leaked across module lifetime (it outlives any single unload), and
 * memory is reclaimed only at reboot.
 */

#include <linux/module.h>
#include <linux/string.h>
#include <linux/printk.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/debugfs.h>
#include <linux/rculist.h>
#include "internal.h"

/*
 * Protected by module_mutex for writers and by RCU for readers. See the
 * file header comment for the full concurrency rules.
 */
static LIST_HEAD(unloaded_tainted_modules);

/* Owned by kernel/module/debug.c; only valid when CONFIG_DEBUG_FS=y. */
extern struct dentry *mod_debugfs_root;

/**
 * try_add_tainted_module - record that @mod was unloaded while tainted
 * @mod: the module being unloaded
 *
 * Called from the unload path before @mod is freed. If @mod carries no
 * taint flags this is a no-op. Otherwise:
 *
 *   - if an entry with the same name and an overlapping taint mask
 *     already exists, its refcount is bumped;
 *   - otherwise a new entry is allocated and inserted.
 *
 * The (name, taints) pair is treated as an equivalence class: any
 * overlap between the existing mask and @mod->taints counts as the
 * "same" module, so taint bits that accumulate across reloads are
 * merged into one counter. This is intentional: the debugfs file is a
 * summary, not an audit log.
 *
 * Context: caller must hold module_mutex (the list is written under
 *          both module_mutex and RCU publication).
 * Return: 0 on success (including the no-op case), -ENOMEM if a new
 *         entry is needed but cannot be allocated.
 */
int try_add_tainted_module(struct module *mod)
{
	struct mod_unload_taint *mod_taint;

	/*
	 * Fast path: untainted modules are never tracked. This is the
	 * common case in a clean system, so avoid touching the list.
	 */
	if (!mod->taints)
		goto out;

	/*
	 * Walk under RCU. The _rcu variant is used even though the
	 * writer holds module_mutex, because the *readers* (dmesg print
	 * and debugfs) traverse the same list without module_mutex and
	 * need RCU semantics to be honoured on the writer side too.
	 * The lockdep_is_held() annotation tells lockdep the mutex
	 * satisfies the lock requirement in this call site.
	 */
	list_for_each_entry_rcu(mod_taint, &unloaded_tainted_modules, list,
				lockdep_is_held(&module_mutex)) {
		if (!strcmp(mod_taint->name, mod->name) &&
		    mod_taint->taints & mod->taints) {
			/*
			 * Plain ++ is safe here: writers are serialised
			 * by module_mutex, and readers only ever display
			 * the value, so a torn read on a 32-bit word is
			 * harmless for the "summary" semantics.
			 */
			mod_taint->count++;
			goto out;
		}
	}

	mod_taint = kmalloc_obj(*mod_taint);
	if (unlikely(!mod_taint))
		return -ENOMEM;

	/*
	 * mod->name is guaranteed NUL-terminated and bounded by
	 * MODULE_NAME_LEN, so strscpy() with size MODULE_NAME_LEN
	 * copies at most MODULE_NAME_LEN - 1 characters plus the
	 * terminator -- i.e. it can never silently drop a character
	 * for a valid module name.
	 */
	strscpy(mod_taint->name, mod->name, MODULE_NAME_LEN);
	mod_taint->taints = mod->taints;
	list_add_rcu(&mod_taint->list, &unloaded_tainted_modules);
	mod_taint->count = 1;
out:
	return 0;
}

/**
 * print_unloaded_tainted_modules - dump the tracked list to the kernel log
 *
 * Called when /proc/sys/kernel/tainted is read (or on similar userspace
 * requests) so the unloaded-but-tainted history shows up alongside the
 * current taint state.
 *
 * The message is emitted as a single log line: the introductory
 * "Unloaded tainted modules:" is printed with printk(KERN_DEFAULT ...)
 * and every entry is appended with pr_cont(). pr_cont() continues the
 * previous line, so callers must not interleave other printk()s here.
 *
 * Context: RCU read-side context is required; the caller (proc handler)
 *          is expected to have entered rcu_read_lock().
 */
void print_unloaded_tainted_modules(void)
{
	struct mod_unload_taint *mod_taint;
	char buf[MODULE_FLAGS_BUF_SIZE];

	if (!list_empty(&unloaded_tainted_modules)) {
		printk(KERN_DEFAULT "Unloaded tainted modules:");
		list_for_each_entry_rcu(mod_taint, &unloaded_tainted_modules,
					list) {
			size_t l;

			/*
			 * module_flags_taint() writes a NUL-terminated
			 * string and returns its length (excluding NUL).
			 * We then explicitly append a NUL to be robust
			 * against callers that expect a C string.
			 */
			l = module_flags_taint(mod_taint->taints, buf);
			buf[l++] = '\0';
			pr_cont(" %s(%s):%llu", mod_taint->name, buf,
				mod_taint->count);
		}
	}
}

#ifdef CONFIG_DEBUG_FS
/*
 * =====================================================================
 * /sys/kernel/debug/unloaded_tainted
 *
 * A plain seq_file view of the tracked list. Readers hold RCU for the
 * duration of each ->show() callback (start/next/stop bracket the
 * iteration), so no entries can be freed under us.
 *
 * Format (one line per entry):
 *
 *     <name> (<taint-flags>) <count>\n
 * =====================================================================
 */

static void *unloaded_tainted_modules_seq_start(struct seq_file *m, loff_t *pos)
	__acquires(rcu)
{
	/*
	 * seq_list_start_rcu() uses the caller's RCU context to walk the
	 * list, so RCU must be held here and released in ->stop. This is
	 * the canonical pattern for seq_files over RCU-protected lists.
	 */
	rcu_read_lock();
	return seq_list_start_rcu(&unloaded_tainted_modules, *pos);
}

static void *unloaded_tainted_modules_seq_next(struct seq_file *m, void *p, loff_t *pos)
{
	return seq_list_next_rcu(p, &unloaded_tainted_modules, pos);
}

static void unloaded_tainted_modules_seq_stop(struct seq_file *m, void *p)
	__releases(rcu)
{
	rcu_read_unlock();
}

static int unloaded_tainted_modules_seq_show(struct seq_file *m, void *p)
{
	struct mod_unload_taint *mod_taint;
	char buf[MODULE_FLAGS_BUF_SIZE];
	size_t l;

	/*
	 * seq_list_start/next pass us the list_head pointer; container_of
	 * it back to the enclosing struct so we can read the fields.
	 */
	mod_taint = list_entry(p, struct mod_unload_taint, list);

	l = module_flags_taint(mod_taint->taints, buf);
	buf[l++] = '\0';

	seq_printf(m, "%s (%s) %llu", mod_taint->name, buf, mod_taint->count);
	seq_puts(m, "\n");

	return 0;
}

static const struct seq_operations unloaded_tainted_modules_seq_ops = {
	.start = unloaded_tainted_modules_seq_start,
	.next  = unloaded_tainted_modules_seq_next,
	.stop  = unloaded_tainted_modules_seq_stop,
	.show  = unloaded_tainted_modules_seq_show,
};

static int unloaded_tainted_modules_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &unloaded_tainted_modules_seq_ops);
}

static const struct file_operations unloaded_tainted_modules_fops = {
	.open    = unloaded_tainted_modules_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = seq_release,
};

/*
 * Registered at module_init time. There is deliberately no matching
 * module_exit(): the debugfs subsystem removes the file when the
 * parent directory (mod_debugfs_root) is torn down, and the tracked
 * list is intentionally not freed (see file header).
 */
static int __init unloaded_tainted_modules_init(void)
{
	debugfs_create_file("unloaded_tainted", 0444, mod_debugfs_root, NULL,
			    &unloaded_tainted_modules_fops);
	return 0;
}
module_init(unloaded_tainted_modules_init);
#endif /* CONFIG_DEBUG_FS */