static noinline struct module *load_module(void __user *umod,
				  unsigned long len,
				  const char __user *uargs)
{
	Elf_Ehdr *hdr;
	Elf_Shdr *sechdrs;
	char *secstrings, *args, *modmagic, *strtab = NULL;
	char *staging;
	unsigned int i;
	unsigned int symindex = 0;
	unsigned int strindex = 0;
	unsigned int modindex, versindex, infoindex, pcpuindex;
	struct module *mod;
	long err = 0;
	void *percpu = NULL, *ptr = NULL; /* Stops spurious gcc warning */
	unsigned long symoffs, stroffs, *strmap;

	mm_segment_t old_fs;

	DEBUGP("load_module: umod=%p, len=%lu, uargs=%p\n",
	       umod, len, uargs);
	if (len < sizeof(*hdr))
		return ERR_PTR(-ENOEXEC);

	/* Suck in entire file: we'll want most of it. */
	/* vmalloc barfs on "unusual" numbers.  Check here */
	/*申请一块和模块大小一样的内存*/
	if (len > 64 * 1024 * 1024 || (hdr = vmalloc(len)) == NULL)
		return ERR_PTR(-ENOMEM);

	/*将模块数据从用户地址空间拷贝到内核地址空间*/
	if (copy_from_user(hdr, umod, len) != 0) {
		err = -EFAULT;
		goto free_hdr;
	}

	/* Sanity checks against insmoding binaries or wrong arch,
           weird elf version */
	if (memcmp(hdr->e_ident, ELFMAG, SELFMAG) != 0
	    || hdr->e_type != ET_REL
	    || !elf_check_arch(hdr)
	    || hdr->e_shentsize != sizeof(*sechdrs)) {
		err = -ENOEXEC;
		goto free_hdr;
	}

	if (len < hdr->e_shoff + hdr->e_shnum * sizeof(Elf_Shdr))
		goto truncated;

	/* Convenience variables */
	sechdrs = (void *)hdr + hdr->e_shoff;
	/*计算出section名称字符串表的基地址*/
	secstrings = (void *)hdr + sechdrs[hdr->e_shstrndx].sh_offset;
	sechdrs[0].sh_addr = 0;

	/*获得符号名称字符串表的基地址*/
	for (i = 1; i < hdr->e_shnum; i++) {
		if (sechdrs[i].sh_type != SHT_NOBITS
		    && len < sechdrs[i].sh_offset + sechdrs[i].sh_size)
			goto truncated;

		/* Mark all sections sh_addr with their address in the
		   temporary image. */
		sechdrs[i].sh_addr = (size_t)hdr + sechdrs[i].sh_offset;

		/* Internal symbols and strings. */
		/*查找目标；SHT_SYMTAB表明这个entry所对应的section是字符表*/
		if (sechdrs[i].sh_type == SHT_SYMTAB) {
			symindex = i;
			/* sechdrs[i].sh_link是符号名称字符表section
			 * 在section header table中的索引值*/
			strindex = sechdrs[i].sh_link;
			/*符号名称字符串表所在section的基地址*/
			strtab = (char *)hdr + sechdrs[strindex].sh_offset;
		}
/*宏未定义表明系统不支持动态卸载一个模块,对于名称为.exit的section将来就没有必要把他们加载到内存，清楚entry中sh_flags里面的SH_ALLOC标志位*/
#ifndef CONFIG_MODULE_UNLOAD	
		/* Don't load .exit sections */
		if (strstarts(secstrings+sechdrs[i].sh_name, ".exit"))
			sechdrs[i].sh_flags &= ~(unsigned long)SHF_ALLOC;
#endif
	}

	/*find_sec寻找'.gnu.linkonce.this_module' section在
	 * Section header table中的索引值
	 */
	modindex = find_sec(hdr, sechdrs, secstrings,
			    ".gnu.linkonce.this_module");
	if (!modindex) {
		printk(KERN_WARNING "No module found in object\n");
		err = -ENOEXEC;
		goto free_hdr;
	}
	/* This is temporary: point mod into copy of data. */
	/*.gnu.linkonce.this_module section 在内存中的实际地址*/
	mod = (void *)sechdrs[modindex].sh_addr;

	if (symindex == 0) {
		printk(KERN_WARNING "%s: module has no symbols (stripped?)\n",
		       mod->name);
		err = -ENOEXEC;
		goto free_hdr;
	}

	versindex = find_sec(hdr, sechdrs, secstrings, "__versions");
	infoindex = find_sec(hdr, sechdrs, secstrings, ".modinfo");
	pcpuindex = find_pcpusec(hdr, sechdrs, secstrings);

	/* Don't keep modinfo and version sections. */
	sechdrs[infoindex].sh_flags &= ~(unsigned long)SHF_ALLOC;
	sechdrs[versindex].sh_flags &= ~(unsigned long)SHF_ALLOC;

	/* Check module struct version now, before we try to use module. */
	if (!check_modstruct_version(sechdrs, versindex, mod)) {
		err = -ENOEXEC;
		goto free_hdr;
	}

	modmagic = get_modinfo(sechdrs, infoindex, "vermagic");
	/* This is allowed: modprobe --force will invalidate it. */
	if (!modmagic) {
		err = try_to_force_load(mod, "bad vermagic");
		if (err)
			goto free_hdr;
	} else if (!same_magic(modmagic, vermagic, versindex)) {
		printk(KERN_ERR "%s: version magic '%s' should be '%s'\n",
		       mod->name, modmagic, vermagic);
		err = -ENOEXEC;
		goto free_hdr;
	}

	staging = get_modinfo(sechdrs, infoindex, "staging");
	if (staging) {
		add_taint_module(mod, TAINT_CRAP);
		printk(KERN_WARNING "%s: module is from the staging directory,"
		       " the quality is unknown, you have been warned.\n",
		       mod->name);
	}

	/* Now copy in args */
	args = strndup_user(uargs, ~0UL >> 1);
	if (IS_ERR(args)) {
		err = PTR_ERR(args);
		goto free_hdr;
	}

	strmap = kzalloc(BITS_TO_LONGS(sechdrs[strindex].sh_size)
			 * sizeof(long), GFP_KERNEL);
	if (!strmap) {
		err = -ENOMEM;
		goto free_mod;
	}

	if (find_module(mod->name)) {
		err = -EEXIST;
		goto free_mod;
	}

	mod->state = MODULE_STATE_COMING;

	/* Allow arches to frob section contents and sizes.  */
	err = module_frob_arch_sections(hdr, sechdrs, secstrings, mod);
	if (err < 0)
		goto free_mod;

	if (pcpuindex) {
		/* We have a special allocation for this section. */
		percpu = percpu_modalloc(sechdrs[pcpuindex].sh_size,
					 sechdrs[pcpuindex].sh_addralign,
					 mod->name);
		if (!percpu) {
			err = -ENOMEM;
			goto free_mod;
		}
		sechdrs[pcpuindex].sh_flags &= ~(unsigned long)SHF_ALLOC;
		mod->percpu = percpu;
	}

	/* Determine total sizes, and put offsets in sh_entsize.  For now
	   this is done generically; there doesn't appear to be any
	   special cases for the architectures. */
	layout_sections(mod, hdr, sechdrs, secstrings);
	symoffs = layout_symtab(mod, sechdrs, symindex, strindex, hdr,
				secstrings, &stroffs, strmap);

	/* Do the allocs. */
	ptr = module_alloc_update_bounds(mod->core_size);
	/*
	 * The pointer to this block is stored in the module structure
	 * which is inside the block. Just mark it as not being a
	 * leak.
	 */
	kmemleak_not_leak(ptr);
	if (!ptr) {
		err = -ENOMEM;
		goto free_percpu;
	}
	memset(ptr, 0, mod->core_size);
	mod->module_core = ptr;

	ptr = module_alloc_update_bounds(mod->init_size);
	/*
	 * The pointer to this block is stored in the module structure
	 * which is inside the block. This block doesn't need to be
	 * scanned as it contains data and code that will be freed
	 * after the module is initialized.
	 */
	kmemleak_ignore(ptr);
	if (!ptr && mod->init_size) {
		err = -ENOMEM;
		goto free_core;
	}
	memset(ptr, 0, mod->init_size);
	mod->module_init = ptr;

	/* Transfer each section which specifies SHF_ALLOC */
	DEBUGP("final section addresses:\n");
	for (i = 0; i < hdr->e_shnum; i++) {
		void *dest;

		if (!(sechdrs[i].sh_flags & SHF_ALLOC))
			continue;

		if (sechdrs[i].sh_entsize & INIT_OFFSET_MASK)
			dest = mod->module_init
				+ (sechdrs[i].sh_entsize & ~INIT_OFFSET_MASK);
		else
			dest = mod->module_core + sechdrs[i].sh_entsize;

		if (sechdrs[i].sh_type != SHT_NOBITS)
			memcpy(dest, (void *)sechdrs[i].sh_addr,
			       sechdrs[i].sh_size);
		/* Update sh_addr to point to copy in image. */
		sechdrs[i].sh_addr = (unsigned long)dest;
		DEBUGP("\t0x%lx %s\n", sechdrs[i].sh_addr, secstrings + sechdrs[i].sh_name);
	}
	/* Module has been moved. */
	mod = (void *)sechdrs[modindex].sh_addr;
	kmemleak_load_module(mod, hdr, sechdrs, secstrings);

#if defined(CONFIG_MODULE_UNLOAD) && defined(CONFIG_SMP)
	mod->refptr = percpu_modalloc(sizeof(local_t), __alignof__(local_t),
				      mod->name);
	if (!mod->refptr) {
		err = -ENOMEM;
		goto free_init;
	}
#endif
	/* Now we've moved module, initialize linked lists, etc. */
	module_unload_init(mod);

	/* add kobject, so we can reference it. */
	err = mod_sysfs_init(mod);
	if (err)
		goto free_unload;

	/* Set up license info based on the info section */
	set_license(mod, get_modinfo(sechdrs, infoindex, "license"));

	/*
	 * ndiswrapper is under GPL by itself, but loads proprietary modules.
	 * Don't use add_taint_module(), as it would prevent ndiswrapper from
	 * using GPL-only symbols it needs.
	 */
	if (strcmp(mod->name, "ndiswrapper") == 0)
		add_taint(TAINT_PROPRIETARY_MODULE);

	/* driverloader was caught wrongly pretending to be under GPL */
	if (strcmp(mod->name, "driverloader") == 0)
		add_taint_module(mod, TAINT_PROPRIETARY_MODULE);

	/* Set up MODINFO_ATTR fields */
	setup_modinfo(mod, sechdrs, infoindex);

	/* Fix up syms, so that st_value is a pointer to location. */
	err = simplify_symbols(sechdrs, symindex, strtab, versindex, pcpuindex,
			       mod);
	if (err < 0)
		goto cleanup;

	/* Now we've got everything in the final locations, we can
	 * find optional sections. */
	mod->kp = section_objs(hdr, sechdrs, secstrings, "__param",
			       sizeof(*mod->kp), &mod->num_kp);
	mod->syms = section_objs(hdr, sechdrs, secstrings, "__ksymtab",
				 sizeof(*mod->syms), &mod->num_syms);
	mod->crcs = section_addr(hdr, sechdrs, secstrings, "__kcrctab");
	mod->gpl_syms = section_objs(hdr, sechdrs, secstrings, "__ksymtab_gpl",
				     sizeof(*mod->gpl_syms),
				     &mod->num_gpl_syms);
	mod->gpl_crcs = section_addr(hdr, sechdrs, secstrings, "__kcrctab_gpl");
	mod->gpl_future_syms = section_objs(hdr, sechdrs, secstrings,
					    "__ksymtab_gpl_future",
					    sizeof(*mod->gpl_future_syms),
					    &mod->num_gpl_future_syms);
	mod->gpl_future_crcs = section_addr(hdr, sechdrs, secstrings,
					    "__kcrctab_gpl_future");

#ifdef CONFIG_UNUSED_SYMBOLS
	mod->unused_syms = section_objs(hdr, sechdrs, secstrings,
					"__ksymtab_unused",
					sizeof(*mod->unused_syms),
					&mod->num_unused_syms);
	mod->unused_crcs = section_addr(hdr, sechdrs, secstrings,
					"__kcrctab_unused");
	mod->unused_gpl_syms = section_objs(hdr, sechdrs, secstrings,
					    "__ksymtab_unused_gpl",
					    sizeof(*mod->unused_gpl_syms),
					    &mod->num_unused_gpl_syms);
	mod->unused_gpl_crcs = section_addr(hdr, sechdrs, secstrings,
					    "__kcrctab_unused_gpl");
#endif
#ifdef CONFIG_CONSTRUCTORS
	mod->ctors = section_objs(hdr, sechdrs, secstrings, ".ctors",
				  sizeof(*mod->ctors), &mod->num_ctors);
#endif

#ifdef CONFIG_TRACEPOINTS
	mod->tracepoints = section_objs(hdr, sechdrs, secstrings,
					"__tracepoints",
					sizeof(*mod->tracepoints),
					&mod->num_tracepoints);
#endif
#ifdef CONFIG_EVENT_TRACING
	mod->trace_events = section_objs(hdr, sechdrs, secstrings,
					 "_ftrace_events",
					 sizeof(*mod->trace_events),
					 &mod->num_trace_events);
#endif
#ifdef CONFIG_FTRACE_MCOUNT_RECORD
	/* sechdrs[0].sh_size is always zero */
	mod->ftrace_callsites = section_objs(hdr, sechdrs, secstrings,
					     "__mcount_loc",
					     sizeof(*mod->ftrace_callsites),
					     &mod->num_ftrace_callsites);
#endif
#ifdef CONFIG_MODVERSIONS
	if ((mod->num_syms && !mod->crcs)
	    || (mod->num_gpl_syms && !mod->gpl_crcs)
	    || (mod->num_gpl_future_syms && !mod->gpl_future_crcs)
#ifdef CONFIG_UNUSED_SYMBOLS
	    || (mod->num_unused_syms && !mod->unused_crcs)
	    || (mod->num_unused_gpl_syms && !mod->unused_gpl_crcs)
#endif
		) {
		err = try_to_force_load(mod,
					"no versions for exported symbols");
		if (err)
			goto cleanup;
	}
#endif

	/* Now do relocations. */
	for (i = 1; i < hdr->e_shnum; i++) {
		const char *strtab = (char *)sechdrs[strindex].sh_addr;
		unsigned int info = sechdrs[i].sh_info;

		/* Not a valid relocation section? */
		if (info >= hdr->e_shnum)
			continue;

		/* Don't bother with non-allocated sections */
		if (!(sechdrs[info].sh_flags & SHF_ALLOC))
			continue;

		if (sechdrs[i].sh_type == SHT_REL)
			err = apply_relocate(sechdrs, strtab, symindex, i,mod);
		else if (sechdrs[i].sh_type == SHT_RELA)
			err = apply_relocate_add(sechdrs, strtab, symindex, i,
						 mod);
		if (err < 0)
			goto cleanup;
	}

        /* Find duplicate symbols */
	err = verify_export_symbols(mod);
	if (err < 0)
		goto cleanup;

  	/* Set up and sort exception table */
	mod->extable = section_objs(hdr, sechdrs, secstrings, "__ex_table",
				    sizeof(*mod->extable), &mod->num_exentries);
	sort_extable(mod->extable, mod->extable + mod->num_exentries);

	/* Finally, copy percpu area over. */
	percpu_modcopy(mod->percpu, (void *)sechdrs[pcpuindex].sh_addr,
		       sechdrs[pcpuindex].sh_size);

	add_kallsyms(mod, sechdrs, hdr->e_shnum, symindex, strindex,
		     symoffs, stroffs, secstrings, strmap);
	kfree(strmap);
	strmap = NULL;

	if (!mod->taints) {
		struct _ddebug *debug;
		unsigned int num_debug;

		debug = section_objs(hdr, sechdrs, secstrings, "__verbose",
				     sizeof(*debug), &num_debug);
		if (debug)
			dynamic_debug_setup(debug, num_debug);
	}

	err = module_finalize(hdr, sechdrs, mod);
	if (err < 0)
		goto cleanup;

	/* flush the icache in correct context */
	old_fs = get_fs();
	set_fs(KERNEL_DS);

	/*
	 * Flush the instruction cache, since we've played with text.
	 * Do it before processing of module parameters, so the module
	 * can provide parameter accessor functions of its own.
	 */
	if (mod->module_init)
		flush_icache_range((unsigned long)mod->module_init,
				   (unsigned long)mod->module_init
				   + mod->init_size);
	flush_icache_range((unsigned long)mod->module_core,
			   (unsigned long)mod->module_core + mod->core_size);

	set_fs(old_fs);

	mod->args = args;
	if (section_addr(hdr, sechdrs, secstrings, "__obsparm"))
		printk(KERN_WARNING "%s: Ignoring obsolete parameters\n",
		       mod->name);

	/* Now sew it into the lists so we can get lockdep and oops
	 * info during argument parsing.  Noone should access us, since
	 * strong_try_module_get() will fail.
	 * lockdep/oops can run asynchronous, so use the RCU list insertion
	 * function to insert in a way safe to concurrent readers.
	 * The mutex protects against concurrent writers.
	 */
	list_add_rcu(&mod->list, &modules);

	err = parse_args(mod->name, mod->args, mod->kp, mod->num_kp, NULL);
	if (err < 0)
		goto unlink;

	err = mod_sysfs_setup(mod, mod->kp, mod->num_kp);
	if (err < 0)
		goto unlink;
	add_sect_attrs(mod, hdr->e_shnum, secstrings, sechdrs);
	add_notes_attrs(mod, hdr->e_shnum, secstrings, sechdrs);

	/* Get rid of temporary copy */
	vfree(hdr);

	trace_module_load(mod);

	/* Done! */
	return mod;

 unlink:
	/* Unlink carefully: kallsyms could be walking list. */
	list_del_rcu(&mod->list);
	synchronize_sched();
	module_arch_cleanup(mod);
 cleanup:
	free_modinfo(mod);
	kobject_del(&mod->mkobj.kobj);
	kobject_put(&mod->mkobj.kobj);
 free_unload:
	module_unload_free(mod);
#if defined(CONFIG_MODULE_UNLOAD) && defined(CONFIG_SMP)
	percpu_modfree(mod->refptr);
 free_init:
#endif
	module_free(mod, mod->module_init);
 free_core:
	module_free(mod, mod->module_core);
	/* mod will be freed with core. Don't access it beyond this line! */
 free_percpu:
	if (percpu)
		percpu_modfree(percpu);
 free_mod:
	kfree(args);
	kfree(strmap);
 free_hdr:
	vfree(hdr);
	return ERR_PTR(err);

 truncated:
	printk(KERN_ERR "Module len %lu truncated\n", len);
	err = -ENOEXEC;
	goto free_hdr;
}

/* Call module constructors. */
static void do_mod_ctors(struct module *mod)
{
#ifdef CONFIG_CONSTRUCTORS
	unsigned long i;

	for (i = 0; i < mod->num_ctors; i++)
		mod->ctors[i]();
#endif
}

/* This is where the real work happens */
SYSCALL_DEFINE3(init_module, void __user *, umod,
		unsigned long, len, const char __user *, uargs)
{
	struct module *mod;
	int ret = 0;

	/* Must have permission */
	if (!capable(CAP_SYS_MODULE) || modules_disabled)
		return -EPERM;

	/* Only one module load at a time, please */
	if (mutex_lock_interruptible(&module_mutex) != 0)
		return -EINTR;

	/* Do all the hard work */
	mod = load_module(umod, len, uargs);
	if (IS_ERR(mod)) {
		mutex_unlock(&module_mutex);
		return PTR_ERR(mod);
	}

	/* Drop lock so they can recurse */
	mutex_unlock(&module_mutex);

	blocking_notifier_call_chain(&module_notify_list,
			MODULE_STATE_COMING, mod);

	do_mod_ctors(mod);
	/* Start the module */
	if (mod->init != NULL)
		ret = do_one_initcall(mod->init);
	if (ret < 0) {
		/* Init routine failed: abort.  Try to protect us from
                   buggy refcounters. */
		mod->state = MODULE_STATE_GOING;
		synchronize_sched();
		module_put(mod);
		blocking_notifier_call_chain(&module_notify_list,
					     MODULE_STATE_GOING, mod);
		mutex_lock(&module_mutex);
		free_module(mod);
		mutex_unlock(&module_mutex);
		wake_up(&module_wq);
		return ret;
	}
	if (ret > 0) {
		printk(KERN_WARNING
"%s: '%s'->init suspiciously returned %d, it should follow 0/-E convention\n"
"%s: loading module anyway...\n",
		       __func__, mod->name, ret,
		       __func__);
		dump_stack();
	}

	/* Now it's a first class citizen!  Wake up anyone waiting for it. */
	mod->state = MODULE_STATE_LIVE;
	wake_up(&module_wq);
	blocking_notifier_call_chain(&module_notify_list,
				     MODULE_STATE_LIVE, mod);

	/* We need to finish all async code before the module init sequence is done */
	async_synchronize_full();

	mutex_lock(&module_mutex);
	/* Drop initial reference. */
	module_put(mod);
	trim_init_extable(mod);
#ifdef CONFIG_KALLSYMS
	mod->num_symtab = mod->core_num_syms;
	mod->symtab = mod->core_symtab;
	mod->strtab = mod->core_strtab;
#endif
	module_free(mod, mod->module_init);
	mod->module_init = NULL;
	mod->init_size = 0;
	mod->init_text_size = 0;
	mutex_unlock(&module_mutex);

	return 0;
}

static inline int within(unsigned long addr, void *start, unsigned long size)
{
	return ((void *)addr >= start && (void *)addr < start + size);
}

#ifdef CONFIG_KALLSYMS
/*
 * This ignores the intensely annoying "mapping symbols" found
 * in ARM ELF files: $a, $t and $d.
 */
static inline int is_arm_mapping_symbol(const char *str)
{
	return str[0] == '$' && strchr("atd", str[1])
	       && (str[2] == '\0' || str[2] == '.');
}

static const char *get_ksymbol(struct module *mod,
			       unsigned long addr,
			       unsigned long *size,
			       unsigned long *offset)
{
	unsigned int i, best = 0;
	unsigned long nextval;

	/* At worse, next value is at end of module */
	if (within_module_init(addr, mod))
		nextval = (unsigned long)mod->module_init+mod->init_text_size;
	else
		nextval = (unsigned long)mod->module_core+mod->core_text_size;

	/* Scan for closest preceeding symbol, and next symbol. (ELF
	   starts real symbols at 1). */
	for (i = 1; i < mod->num_symtab; i++) {
		if (mod->symtab[i].st_shndx == SHN_UNDEF)
			continue;

		/* We ignore unnamed symbols: they're uninformative
		 * and inserted at a whim. */
		if (mod->symtab[i].st_value <= addr
		    && mod->symtab[i].st_value > mod->symtab[best].st_value
		    && *(mod->strtab + mod->symtab[i].st_name) != '\0'
		    && !is_arm_mapping_symbol(mod->strtab + mod->symtab[i].st_name))
			best = i;
		if (mod->symtab[i].st_value > addr
		    && mod->symtab[i].st_value < nextval
		    && *(mod->strtab + mod->symtab[i].st_name) != '\0'
		    && !is_arm_mapping_symbol(mod->strtab + mod->symtab[i].st_name))
			nextval = mod->symtab[i].st_value;
	}

	if (!best)
		return NULL;

	if (size)
		*size = nextval - mod->symtab[best].st_value;
	if (offset)
		*offset = addr - mod->symtab[best].st_value;
	return mod->strtab + mod->symtab[best].st_name;
}

/* For kallsyms to ask for address resolution.  NULL means not found.  Careful
 * not to lock to avoid deadlock on oopses, simply disable preemption. */
const char *module_address_lookup(unsigned long addr,
			    unsigned long *size,
			    unsigned long *offset,
			    char **modname,
			    char *namebuf)
{
	struct module *mod;
	const char *ret = NULL;

	preempt_disable();
	list_for_each_entry_rcu(mod, &modules, list) {
		if (within_module_init(addr, mod) ||
		    within_module_core(addr, mod)) {
			if (modname)
				*modname = mod->name;
			ret = get_ksymbol(mod, addr, size, offset);
			break;
		}
	}
	/* Make a copy in here where it's safe */
	if (ret) {
		strncpy(namebuf, ret, KSYM_NAME_LEN - 1);
		ret = namebuf;
	}
	preempt_enable();
	return ret;
}

int lookup_module_symbol_name(unsigned long addr, char *symname)
{
	struct module *mod;

	preempt_disable();
	list_for_each_entry_rcu(mod, &modules, list) {
		if (within_module_init(addr, mod) ||
		    within_module_core(addr, mod)) {
			const char *sym;

			sym = get_ksymbol(mod, addr, NULL, NULL);
			if (!sym)
				goto out;
			strlcpy(symname, sym, KSYM_NAME_LEN);
			preempt_enable();
			return 0;
		}
	}
out:
	preempt_enable();
	return -ERANGE;
}

int lookup_module_symbol_attrs(unsigned long addr, unsigned long *size,
			unsigned long *offset, char *modname, char *name)
{
	struct module *mod;

	preempt_disable();
	list_for_each_entry_rcu(mod, &modules, list) {
		if (within_module_init(addr, mod) ||
		    within_module_core(addr, mod)) {
			const char *sym;

			sym = get_ksymbol(mod, addr, size, offset);
			if (!sym)
				goto out;
			if (modname)
				strlcpy(modname, mod->name, MODULE_NAME_LEN);
			if (name)
				strlcpy(name, sym, KSYM_NAME_LEN);
			preempt_enable();
			return 0;
		}
	}
out:
	preempt_enable();
	return -ERANGE;
}

int module_get_kallsym(unsigned int symnum, unsigned long *value, char *type,
			char *name, char *module_name, int *exported)
{
	struct module *mod;

	preempt_disable();
	list_for_each_entry_rcu(mod, &modules, list) {
		if (symnum < mod->num_symtab) {
			*value = mod->symtab[symnum].st_value;
			*type = mod->symtab[symnum].st_info;
			strlcpy(name, mod->strtab + mod->symtab[symnum].st_name,
				KSYM_NAME_LEN);
			strlcpy(module_name, mod->name, MODULE_NAME_LEN);
			*exported = is_exported(name, *value, mod);
			preempt_enable();
			return 0;
		}
		symnum -= mod->num_symtab;
	}
	preempt_enable();
	return -ERANGE;
}

static unsigned long mod_find_symname(struct module *mod, const char *name)
{
	unsigned int i;

	for (i = 0; i < mod->num_symtab; i++)
		if (strcmp(name, mod->strtab+mod->symtab[i].st_name) == 0 &&
		    mod->symtab[i].st_info != 'U')
			return mod->symtab[i].st_value;
	return 0;
}

/* Look for this name: can be of form module:name. */
unsigned long module_kallsyms_lookup_name(const char *name)
{
	struct module *mod;
	char *colon;
	unsigned long ret = 0;

	/* Don't lock: we're in enough trouble already. */
	preempt_disable();
	if ((colon = strchr(name, ':')) != NULL) {
		*colon = '\0';
		if ((mod = find_module(name)) != NULL)
			ret = mod_find_symname(mod, colon+1);
		*colon = ':';
	} else {
		list_for_each_entry_rcu(mod, &modules, list)
			if ((ret = mod_find_symname(mod, name)) != 0)
				break;
	}
	preempt_enable();
	return ret;
}

int module_kallsyms_on_each_symbol(int (*fn)(void *, const char *,
					     struct module *, unsigned long),
				   void *data)
{
	struct module *mod;
	unsigned int i;
	int ret;

	list_for_each_entry(mod, &modules, list) {
		for (i = 0; i < mod->num_symtab; i++) {
			ret = fn(data, mod->strtab + mod->symtab[i].st_name,
				 mod, mod->symtab[i].st_value);
			if (ret != 0)
				return ret;
		}
	}
	return 0;
}
#endif /* CONFIG_KALLSYMS */

static char *module_flags(struct module *mod, char *buf)
{
	int bx = 0;

	if (mod->taints ||
	    mod->state == MODULE_STATE_GOING ||
	    mod->state == MODULE_STATE_COMING) {
		buf[bx++] = '(';
		if (mod->taints & (1 << TAINT_PROPRIETARY_MODULE))
			buf[bx++] = 'P';
		if (mod->taints & (1 << TAINT_FORCED_MODULE))
			buf[bx++] = 'F';
		if (mod->taints & (1 << TAINT_CRAP))
			buf[bx++] = 'C';
		/*
		 * TAINT_FORCED_RMMOD: could be added.
		 * TAINT_UNSAFE_SMP, TAINT_MACHINE_CHECK, TAINT_BAD_PAGE don't
		 * apply to modules.
		 */

		/* Show a - for module-is-being-unloaded */
		if (mod->state == MODULE_STATE_GOING)
			buf[bx++] = '-';
		/* Show a + for module-is-being-loaded */
		if (mod->state == MODULE_STATE_COMING)
			buf[bx++] = '+';
		buf[bx++] = ')';
	}
	buf[bx] = '\0';

	return buf;
}

#ifdef CONFIG_PROC_FS
/* Called by the /proc file system to return a list of modules. */
static void *m_start(struct seq_file *m, loff_t *pos)
{
	mutex_lock(&module_mutex);
	return seq_list_start(&modules, *pos);
}

static void *m_next(struct seq_file *m, void *p, loff_t *pos)
{
	return seq_list_next(p, &modules, pos);
}

static void m_stop(struct seq_file *m, void *p)
{
	mutex_unlock(&module_mutex);
}

static int m_show(struct seq_file *m, void *p)
{
	struct module *mod = list_entry(p, struct module, list);
	char buf[8];

	seq_printf(m, "%s %u",
		   mod->name, mod->init_size + mod->core_size);
	print_unload_info(m, mod);

	/* Informative for users. */
	seq_printf(m, " %s",
		   mod->state == MODULE_STATE_GOING ? "Unloading":
		   mod->state == MODULE_STATE_COMING ? "Loading":
		   "Live");
	/* Used by oprofile and other similar tools. */
	seq_printf(m, " 0x%p", mod->module_core);

	/* Taints info */
	if (mod->taints)
		seq_printf(m, " %s", module_flags(mod, buf));

	seq_printf(m, "\n");
	return 0;
}

/* Format: modulename size refcount deps address

   Where refcount is a number or -, and deps is a comma-separated list
   of depends or -.
*/
static const struct seq_operations modules_op = {
	.start	= m_start,
	.next	= m_next,
	.stop	= m_stop,
	.show	= m_show
};

static int modules_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &modules_op);
}

static const struct file_operations proc_modules_operations = {
	.open		= modules_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release,
};

static int __init proc_modules_init(void)
{
	proc_create("modules", 0, NULL, &proc_modules_operations);
	return 0;
}
module_init(proc_modules_init);
#endif

/* Given an address, look for it in the module exception tables. */
const struct exception_table_entry *search_module_extables(unsigned long addr)
{
	const struct exception_table_entry *e = NULL;
	struct module *mod;

	preempt_disable();
	list_for_each_entry_rcu(mod, &modules, list) {
		if (mod->num_exentries == 0)
			continue;

		e = search_extable(mod->extable,
				   mod->extable + mod->num_exentries - 1,
				   addr);
		if (e)
			break;
	}
	preempt_enable();

	/* Now, if we found one, we are running inside it now, hence
	   we cannot unload the module, hence no refcnt needed. */
	return e;
}

/*
 * is_module_address - is this address inside a module?
 * @addr: the address to check.
 *
 * See is_module_text_address() if you simply want to see if the address
 * is code (not data).
 */
bool is_module_address(unsigned long addr)
{
	bool ret;

	preempt_disable();
	ret = __module_address(addr) != NULL;
	preempt_enable();

	return ret;
}

/*
 * __module_address - get the module which contains an address.
 * @addr: the address.
 *
 * Must be called with preempt disabled or module mutex held so that
 * module doesn't get freed during this.
 */
struct module *__module_address(unsigned long addr)
{
	struct module *mod;

	if (addr < module_addr_min || addr > module_addr_max)
		return NULL;

	list_for_each_entry_rcu(mod, &modules, list)
		if (within_module_core(addr, mod)
		    || within_module_init(addr, mod))
			return mod;
	return NULL;
}
EXPORT_SYMBOL_GPL(__module_address);

/*
 * is_module_text_address - is this address inside module code?
 * @addr: the address to check.
 *
 * See is_module_address() if you simply want to see if the address is
 * anywhere in a module.  See kernel_text_address() for testing if an
 * address corresponds to kernel or module code.
 */
bool is_module_text_address(unsigned long addr)
{
	bool ret;

	preempt_disable();
	ret = __module_text_address(addr) != NULL;
	preempt_enable();

	return ret;
}

/*
 * __module_text_address - get the module whose code contains an address.
 * @addr: the address.
 *
 * Must be called with preempt disabled or module mutex held so that
 * module doesn't get freed during this.
 */
struct module *__module_text_address(unsigned long addr)
{
	struct module *mod = __module_address(addr);
	if (mod) {
		/* Make sure it's within the text section. */
		if (!within(addr, mod->module_init, mod->init_text_size)
		    && !within(addr, mod->module_core, mod->core_text_size))
			mod = NULL;
	}
	return mod;
}
EXPORT_SYMBOL_GPL(__module_text_address);

/* Don't grab lock, we're oopsing. */
void print_modules(void)
{
	struct module *mod;
	char buf[8];

	printk(KERN_DEFAULT "Modules linked in:");
	/* Most callers should already have preempt disabled, but make sure */
	preempt_disable();
	list_for_each_entry_rcu(mod, &modules, list)
		printk(" %s%s", mod->name, module_flags(mod, buf));
	preempt_enable();
	if (last_unloaded_module[0])
		printk(" [last unloaded: %s]", last_unloaded_module);
	printk("\n");
}

#ifdef CONFIG_MODVERSIONS
/* Generate the signature for all relevant module structures here.
 * If these change, we don't want to try to parse the module. */
void module_layout(struct module *mod,
		   struct modversion_info *ver,
		   struct kernel_param *kp,
		   struct kernel_symbol *ks,
		   struct tracepoint *tp)
{
}
EXPORT_SYMBOL(module_layout);
#endif

#ifdef CONFIG_TRACEPOINTS
void module_update_tracepoints(void)
{
	struct module *mod;

	mutex_lock(&module_mutex);
	list_for_each_entry(mod, &modules, list)
		if (!mod->taints)
			tracepoint_update_probe_range(mod->tracepoints,
				mod->tracepoints + mod->num_tracepoints);
	mutex_unlock(&module_mutex);
}

/*
 * Returns 0 if current not found.
 * Returns 1 if current found.
 */
int module_get_iter_tracepoints(struct tracepoint_iter *iter)
{
	struct module *iter_mod;
	int found = 0;

	mutex_lock(&module_mutex);
	list_for_each_entry(iter_mod, &modules, list) {
		if (!iter_mod->taints) {
			/*
			 * Sorted module list
			 */
			if (iter_mod < iter->module)
				continue;
			else if (iter_mod > iter->module)
				iter->tracepoint = NULL;
			found = tracepoint_get_iter_range(&iter->tracepoint,
				iter_mod->tracepoints,
				iter_mod->tracepoints
					+ iter_mod->num_tracepoints);
			if (found) {
				iter->module = iter_mod;
				break;
			}
		}
	}
	mutex_unlock(&module_mutex);
	return found;
}
#endif
