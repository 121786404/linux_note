/*
 * irq_domain - IRQ translation domains
 *
 * Translation infrastructure between hw and linux irq numbers.  This is
 * helpful for interrupt controllers to implement mapping between hardware
 * irq numbers and the Linux irq number space.
 *
 * irq_domains also have hooks for translating device tree or other
 * firmware interrupt representations into a hardware irq number that
 * can be mapped back to a Linux irq number without any extra platform
 * support code.
 *
 * Interrupt controller "domain" data structure. This could be defined as a
 * irq domain controller. That is, it handles the mapping between hardware
 * and virtual interrupt numbers for a given interrupt domain. The domain
 * structure is generally created by the PIC code for a given PIC instance
 * (though a domain can cover more than one PIC if they have a flat number
 * model). It's the domain callbacks that are responsible for setting the
 * irq_chip on a given irq_desc after it's been mapped.
 *
 * The host code and data structures use a fwnode_handle pointer to
 * identify the domain. In some cases, and in order to preserve source
 * code compatibility, this fwnode pointer is "upgraded" to a DT
 * device_node. For those firmware infrastructures that do not provide
 * a unique identifier for an interrupt controller, the irq_domain
 * code offers a fwnode allocator.
 */

#ifndef _LINUX_IRQDOMAIN_H
#define _LINUX_IRQDOMAIN_H

#include <linux/types.h>
#include <linux/irqhandler.h>
#include <linux/of.h>
#include <linux/radix-tree.h>

struct device_node;
struct irq_domain;
struct of_device_id;
struct irq_chip;
struct irq_data;
struct cpumask;

/* Number of irqs reserved for a legacy isa controller */
#define NUM_ISA_INTERRUPTS	16

#define IRQ_DOMAIN_IRQ_SPEC_PARAMS 16

/**
 * struct irq_fwspec - generic IRQ specifier structure
 *
 * @fwnode:		Pointer to a firmware-specific descriptor
 * @param_count:	Number of device-specific parameters
 * @param:		Device-specific parameters
 *
 * This structure, directly modeled after of_phandle_args, is used to
 * pass a device-specific description of an interrupt.
 */
struct irq_fwspec {
	struct fwnode_handle *fwnode;
	int param_count;
	u32 param[IRQ_DOMAIN_IRQ_SPEC_PARAMS];
};

/*
 * Should several domains have the same device node, but serve
 * different purposes (for example one domain is for PCI/MSI, and the
 * other for wired IRQs), they can be distinguished using a
 * bus-specific token. Most domains are expected to only carry
 * DOMAIN_BUS_ANY.
 */
enum irq_domain_bus_token {
	DOMAIN_BUS_ANY		= 0,
	DOMAIN_BUS_WIRED,
	DOMAIN_BUS_PCI_MSI,
	DOMAIN_BUS_PLATFORM_MSI,
	DOMAIN_BUS_NEXUS,
	DOMAIN_BUS_IPI,
	DOMAIN_BUS_FSL_MC_MSI,
};

/**
 * struct irq_domain_ops - Methods for irq_domain objects
 * @match: Match an interrupt controller device node to a host, returns
 *         1 on a match
 * @map: Create or update a mapping between a virtual irq number and a hw
 *       irq number. This is called only once for a given mapping.
 * @unmap: Dispose of such a mapping
 * @xlate: Given a device tree node and interrupt specifier, decode
 *         the hardware irq number and linux irq type value.
 *
 * Functions below are provided by the driver and called whenever a new mapping
 * is created or an old mapping is disposed. The driver can then proceed to
 * whatever internal data structures management is required. It also needs
 * to setup the irq_desc when returning from map().
 */
/*
 domain_ops  domain映射操作方法集合
*/
struct irq_domain_ops {
    /*
    判断一个指定的interrupt controller（node参数）是否和一个irq domain匹配（d参数），如果匹配的话，返回1。
    实际上，内核中很少定义这个callback函数，实际上struct irq_domain中有一个of_node
    指向了对应的interrupt controller的device node，因此，如果不提供该函数，
    那么default的匹配函数其实就是判断irq domain的of_node成员是否等于传入的node参数。
    */
	int (*match)(struct irq_domain *d, struct device_node *node,
		     enum irq_domain_bus_token bus_token);
	int (*select)(struct irq_domain *d, struct irq_fwspec *fwspec,
		      enum irq_domain_bus_token bus_token);
    /*
      map和unmap是操作相反的函数。
      调用map函数的时机是在创建（或者更新）HW interrupt ID（hw参数）和IRQ number（virq参数）关系的时候。
      其实，从发生一个中断到调用该中断的handler仅仅调用一个request_threaded_irq是不够的，还需要针对该irq number设定：
      （1）设定该IRQ number对应的中断描述符（struct irq_desc）的irq chip
      （2）设定该IRQ number对应的中断描述符的highlevel irq-events handler
      （3）设定该IRQ number对应的中断描述符的 irq chip data
      这些设定不适合由具体的硬件驱动来设定，因此在Interrupt controller，也就是irq domain的callback函数中设定。
    */
	int (*map)(struct irq_domain *d, unsigned int virq, irq_hw_number_t hw);
	void (*unmap)(struct irq_domain *d, unsigned int virq);
	/*
	xlate是translate的意思，在DTS文件中，各个使用中断的device node会通过一些属性
	（例如interrupts和interrupt-parent属性）来提供中断信息(中断号、中断触发类型等)
	给kernel以便kernel可以正确的进行driver的初始化动作。

	这里，interrupts属性所表示的interrupt specifier只能由具体的interrupt controller（也就是irq domain）来解析。
	而xlate函数就是将指定的设备（node参数）上若干个（intsize参数）中断属性（intspec参数）
	翻译成HW interrupt ID（out_hwirq参数）和trigger类型（out_type）。 
	*/
	int (*xlate)(struct irq_domain *d, struct device_node *node,
		     const u32 *intspec, unsigned int intsize,
		     unsigned long *out_hwirq, unsigned int *out_type);

#ifdef	CONFIG_IRQ_DOMAIN_HIERARCHY
	/* extended V2 interfaces to support hierarchy irq_domains */
	int (*alloc)(struct irq_domain *d, unsigned int virq,
		     unsigned int nr_irqs, void *arg);
	void (*free)(struct irq_domain *d, unsigned int virq,
		     unsigned int nr_irqs);
	void (*activate)(struct irq_domain *d, struct irq_data *irq_data);
	void (*deactivate)(struct irq_domain *d, struct irq_data *irq_data);
	int (*translate)(struct irq_domain *d, struct irq_fwspec *fwspec,
			 unsigned long *out_hwirq, unsigned int *out_type);
#endif
};

extern struct irq_domain_ops irq_generic_chip_ops;

struct irq_domain_chip_generic;

/**
 * struct irq_domain - Hardware interrupt number translation object
 * @link: Element in global irq_domain list.
 * @name: Name of interrupt domain
 * @ops: pointer to irq_domain methods
 * @host_data: private data pointer for use by owner.  Not touched by irq_domain
 *             core code.
 * @flags: host per irq_domain flags
 *
 * Optional elements
 * @of_node: Pointer to device tree nodes associated with the irq_domain. Used
 *           when decoding device tree interrupt specifiers.
 * @gc: Pointer to a list of generic chips. There is a helper function for
 *      setting up one or more generic chips for interrupt controllers
 *      drivers using the generic chip library which uses this pointer.
 * @parent: Pointer to parent irq_domain to support hierarchy irq_domains
 *
 * Revmap data, used internally by irq_domain
 * @revmap_direct_max_irq: The largest hwirq that can be set for controllers that
 *                         support direct mapping
 * @revmap_size: Size of the linear map table @linear_revmap[]
 * @revmap_tree: Radix map tree for hwirqs that don't fit in the linear map
 * @linear_revmap: Linear table of hwirq->virq reverse mappings
 */
/*
 GIC中断控制器在初始化时解析DTS信息中定义了几个GIC控制器，每一个控制器注册一个irq_domain数据结构
 DTS中定义"interrupt controller"属性的设备表示一个中断控制器
*/
struct irq_domain {
	struct list_head link;  // 用于将irq_domain链接到全局链表irq_domain_list中
	const char *name;
	const struct irq_domain_ops *ops; // callback函数 
    /*
    host_data定义了底层interrupt controller使用的私有数据，和具体的interrupt controller相关
    （对于GIC，该指针指向一个struct gic_chip_data数据结构）。
    */
	void *host_data;
	unsigned int flags;

	/* Optional data */
	struct fwnode_handle *fwnode;
	enum irq_domain_bus_token bus_token;
	struct irq_domain_chip_generic *gc; // generic irq chip
#ifdef	CONFIG_IRQ_DOMAIN_HIERARCHY
	struct irq_domain *parent;
#endif

	/* reverse map data. The linear map gets appended to the irq_domain */
	irq_hw_number_t hwirq_max; // 该domain中最大的那个HW interrupt ID 
	unsigned int revmap_direct_max_irq; // 没有用，设定为0。revmap_tree没有用。
	unsigned int revmap_size; // 线性的lookup table的size，for Radix Tree map和no map，该值等于0
	struct radix_tree_root revmap_tree; // Radix Tree map使用到的radix tree root node
	unsigned int linear_revmap[]; // 线性映射使用的lookup table ，index是HW interrupt ID，table中保存了IRQ number值
};

/* Irq domain flags */
enum {
	/* Irq domain is hierarchical */
	IRQ_DOMAIN_FLAG_HIERARCHY	= (1 << 0),

	/* Core calls alloc/free recursive through the domain hierarchy. */
	IRQ_DOMAIN_FLAG_AUTO_RECURSIVE	= (1 << 1),

	/* Irq domain is an IPI domain with virq per cpu */
	IRQ_DOMAIN_FLAG_IPI_PER_CPU	= (1 << 2),

	/* Irq domain is an IPI domain with single virq */
	IRQ_DOMAIN_FLAG_IPI_SINGLE	= (1 << 3),

	/* Irq domain implements MSIs */
	IRQ_DOMAIN_FLAG_MSI		= (1 << 4),

	/* Irq domain implements MSI remapping */
	IRQ_DOMAIN_FLAG_MSI_REMAP	= (1 << 5),

	/*
	 * Flags starting from IRQ_DOMAIN_FLAG_NONCORE are reserved
	 * for implementation specific purposes and ignored by the
	 * core code.
	 */
	IRQ_DOMAIN_FLAG_NONCORE		= (1 << 16),
};

static inline struct device_node *irq_domain_get_of_node(struct irq_domain *d)
{
	return to_of_node(d->fwnode);
}

#ifdef CONFIG_IRQ_DOMAIN
struct fwnode_handle *irq_domain_alloc_fwnode(void *data);
void irq_domain_free_fwnode(struct fwnode_handle *fwnode);
struct irq_domain *__irq_domain_add(struct fwnode_handle *fwnode, int size,
				    irq_hw_number_t hwirq_max, int direct_max,
				    const struct irq_domain_ops *ops,
				    void *host_data);
struct irq_domain *irq_domain_add_simple(struct device_node *of_node,
					 unsigned int size,
					 unsigned int first_irq,
					 const struct irq_domain_ops *ops,
					 void *host_data);
struct irq_domain *irq_domain_add_legacy(struct device_node *of_node,
					 unsigned int size,
					 unsigned int first_irq,
					 irq_hw_number_t first_hwirq,
					 const struct irq_domain_ops *ops,
					 void *host_data);
extern struct irq_domain *irq_find_matching_fwspec(struct irq_fwspec *fwspec,
						   enum irq_domain_bus_token bus_token);
extern bool irq_domain_check_msi_remap(void);
extern void irq_set_default_host(struct irq_domain *host);
extern int irq_domain_alloc_descs(int virq, unsigned int nr_irqs,
				  irq_hw_number_t hwirq, int node,
				  const struct cpumask *affinity);

static inline struct fwnode_handle *of_node_to_fwnode(struct device_node *node)
{
	return node ? &node->fwnode : NULL;
}

static inline bool is_fwnode_irqchip(struct fwnode_handle *fwnode)
{
	return fwnode && fwnode->type == FWNODE_IRQCHIP;
}

static inline
struct irq_domain *irq_find_matching_fwnode(struct fwnode_handle *fwnode,
					    enum irq_domain_bus_token bus_token)
{
	struct irq_fwspec fwspec = {
		.fwnode = fwnode,
	};

	return irq_find_matching_fwspec(&fwspec, bus_token);
}

static inline struct irq_domain *irq_find_matching_host(struct device_node *node,
							enum irq_domain_bus_token bus_token)
{
	return irq_find_matching_fwnode(of_node_to_fwnode(node), bus_token);
}

static inline struct irq_domain *irq_find_host(struct device_node *node)
{
	return irq_find_matching_host(node, DOMAIN_BUS_ANY);
}

/*
irq_domain_add_linear irq_domain_add_tree irq_domain_add_nomap
接口的逻辑很简单，根据自己的映射类型，初始化struct irq_domain中的各个成员，
调用__irq_domain_add将该irq domain挂入irq_domain_list的全局列表
*/

/**
 * irq_domain_add_linear() - Allocate and register a linear revmap irq_domain.
 * @of_node: pointer to interrupt controller's device tree node.
 * @size: Number of interrupts in the domain.
 * @ops: map/unmap domain callbacks
 * @host_data: Controller private data pointer
 */
 /*
 线性映射。
 其实就是一个lookup table，HW interrupt ID作为index，通过查表可以获取对应的IRQ number。
 对于Linear map而言，interrupt controller对其HW interrupt ID进行编码的时候要满足一定的条件：
 hw ID不能过大，而且ID排列最好是紧密的。

size:该interrupt domain支持多少IRQ
ops:callback函数
host_data:driver私有数据
*/
static inline struct irq_domain *irq_domain_add_linear(struct device_node *of_node,
					 unsigned int size,
					 const struct irq_domain_ops *ops,
					 void *host_data)
{
	return __irq_domain_add(of_node_to_fwnode(of_node), size, size, 0, ops, host_data);
}
/*
有些中断控制器很强，可以通过寄存器配置HW interrupt ID而不是由物理连接决定的。
例如PowerPC 系统使用的MPIC (Multi-Processor Interrupt Controller)。
在这种情况下，不需要进行映射，我们直接把IRQ number写入HW interrupt ID配置寄存器就OK了，
这时候，生成的HW interrupt ID就是IRQ number，也就不需要进行mapping了。
*/
static inline struct irq_domain *irq_domain_add_nomap(struct device_node *of_node,
					 unsigned int max_irq,
					 const struct irq_domain_ops *ops,
					 void *host_data)
{
	return __irq_domain_add(of_node_to_fwnode(of_node), 0, max_irq, max_irq, ops, host_data);
}
static inline struct irq_domain *irq_domain_add_legacy_isa(
				struct device_node *of_node,
				const struct irq_domain_ops *ops,
				void *host_data)
{
	return irq_domain_add_legacy(of_node, NUM_ISA_INTERRUPTS, 0, 0, ops,
				     host_data);
}
/*
Radix Tree map。
建立一个Radix Tree来维护HW interrupt ID到IRQ number映射关系。
HW interrupt ID作为lookup key，在Radix Tree检索到IRQ number。
如果的确不能满足线性映射的条件，可以考虑Radix Tree map。
实际上，内核中使用Radix Tree map的只有powerPC和MIPS的硬件平台。
对于Radix Tree map，其接口API如下：
*/
static inline struct irq_domain *irq_domain_add_tree(struct device_node *of_node,
					 const struct irq_domain_ops *ops,
					 void *host_data)
{
	return __irq_domain_add(of_node_to_fwnode(of_node), 0, ~0, 0, ops, host_data);
}

static inline struct irq_domain *irq_domain_create_linear(struct fwnode_handle *fwnode,
					 unsigned int size,
					 const struct irq_domain_ops *ops,
					 void *host_data)
{
	return __irq_domain_add(fwnode, size, size, 0, ops, host_data);
}

static inline struct irq_domain *irq_domain_create_tree(struct fwnode_handle *fwnode,
					 const struct irq_domain_ops *ops,
					 void *host_data)
{
	return __irq_domain_add(fwnode, 0, ~0, 0, ops, host_data);
}

extern void irq_domain_remove(struct irq_domain *host);

extern int irq_domain_associate(struct irq_domain *domain, unsigned int irq,
					irq_hw_number_t hwirq);
extern void irq_domain_associate_many(struct irq_domain *domain,
				      unsigned int irq_base,
				      irq_hw_number_t hwirq_base, int count);
extern void irq_domain_disassociate(struct irq_domain *domain,
				    unsigned int irq);

extern unsigned int irq_create_mapping(struct irq_domain *host,
				       irq_hw_number_t hwirq);
extern unsigned int irq_create_fwspec_mapping(struct irq_fwspec *fwspec);
extern void irq_dispose_mapping(unsigned int virq);

/**
 * irq_linear_revmap() - Find a linux irq from a hw irq number.
 * @domain: domain owning this hardware interrupt
 * @hwirq: hardware irq number in that domain space
 *
 * This is a fast path alternative to irq_find_mapping() that can be
 * called directly by irq controller code to save a handful of
 * instructions. It is always safe to call, but won't find irqs mapped
 * using the radix tree.
 */
static inline unsigned int irq_linear_revmap(struct irq_domain *domain,
					     irq_hw_number_t hwirq)
{
	return hwirq < domain->revmap_size ? domain->linear_revmap[hwirq] : 0;
}
extern unsigned int irq_find_mapping(struct irq_domain *host,
				     irq_hw_number_t hwirq);
extern unsigned int irq_create_direct_mapping(struct irq_domain *host);
extern int irq_create_strict_mappings(struct irq_domain *domain,
				      unsigned int irq_base,
				      irq_hw_number_t hwirq_base, int count);

static inline int irq_create_identity_mapping(struct irq_domain *host,
					      irq_hw_number_t hwirq)
{
	return irq_create_strict_mappings(host, hwirq, hwirq, 1);
}

extern const struct irq_domain_ops irq_domain_simple_ops;

/* stock xlate functions */
int irq_domain_xlate_onecell(struct irq_domain *d, struct device_node *ctrlr,
			const u32 *intspec, unsigned int intsize,
			irq_hw_number_t *out_hwirq, unsigned int *out_type);
int irq_domain_xlate_twocell(struct irq_domain *d, struct device_node *ctrlr,
			const u32 *intspec, unsigned int intsize,
			irq_hw_number_t *out_hwirq, unsigned int *out_type);
int irq_domain_xlate_onetwocell(struct irq_domain *d, struct device_node *ctrlr,
			const u32 *intspec, unsigned int intsize,
			irq_hw_number_t *out_hwirq, unsigned int *out_type);

/* IPI functions */
int irq_reserve_ipi(struct irq_domain *domain, const struct cpumask *dest);
int irq_destroy_ipi(unsigned int irq, const struct cpumask *dest);

/* V2 interfaces to support hierarchy IRQ domains. */
extern struct irq_data *irq_domain_get_irq_data(struct irq_domain *domain,
						unsigned int virq);
extern void irq_domain_set_info(struct irq_domain *domain, unsigned int virq,
				irq_hw_number_t hwirq, struct irq_chip *chip,
				void *chip_data, irq_flow_handler_t handler,
				void *handler_data, const char *handler_name);
#ifdef	CONFIG_IRQ_DOMAIN_HIERARCHY
extern struct irq_domain *irq_domain_create_hierarchy(struct irq_domain *parent,
			unsigned int flags, unsigned int size,
			struct fwnode_handle *fwnode,
			const struct irq_domain_ops *ops, void *host_data);

static inline struct irq_domain *irq_domain_add_hierarchy(struct irq_domain *parent,
					    unsigned int flags,
					    unsigned int size,
					    struct device_node *node,
					    const struct irq_domain_ops *ops,
					    void *host_data)
{
	return irq_domain_create_hierarchy(parent, flags, size,
					   of_node_to_fwnode(node),
					   ops, host_data);
}

extern int __irq_domain_alloc_irqs(struct irq_domain *domain, int irq_base,
				   unsigned int nr_irqs, int node, void *arg,
				   bool realloc, const struct cpumask *affinity);
extern void irq_domain_free_irqs(unsigned int virq, unsigned int nr_irqs);
extern void irq_domain_activate_irq(struct irq_data *irq_data);
extern void irq_domain_deactivate_irq(struct irq_data *irq_data);

static inline int irq_domain_alloc_irqs(struct irq_domain *domain,
			unsigned int nr_irqs, int node, void *arg)
{
	return __irq_domain_alloc_irqs(domain, -1, nr_irqs, node, arg, false,
				       NULL);
}

extern int irq_domain_alloc_irqs_recursive(struct irq_domain *domain,
					   unsigned int irq_base,
					   unsigned int nr_irqs, void *arg);
extern int irq_domain_set_hwirq_and_chip(struct irq_domain *domain,
					 unsigned int virq,
					 irq_hw_number_t hwirq,
					 struct irq_chip *chip,
					 void *chip_data);
extern void irq_domain_reset_irq_data(struct irq_data *irq_data);
extern void irq_domain_free_irqs_common(struct irq_domain *domain,
					unsigned int virq,
					unsigned int nr_irqs);
extern void irq_domain_free_irqs_top(struct irq_domain *domain,
				     unsigned int virq, unsigned int nr_irqs);

extern int irq_domain_alloc_irqs_parent(struct irq_domain *domain,
					unsigned int irq_base,
					unsigned int nr_irqs, void *arg);

extern void irq_domain_free_irqs_parent(struct irq_domain *domain,
					unsigned int irq_base,
					unsigned int nr_irqs);

static inline bool irq_domain_is_hierarchy(struct irq_domain *domain)
{
	return domain->flags & IRQ_DOMAIN_FLAG_HIERARCHY;
}

static inline bool irq_domain_is_ipi(struct irq_domain *domain)
{
	return domain->flags &
		(IRQ_DOMAIN_FLAG_IPI_PER_CPU | IRQ_DOMAIN_FLAG_IPI_SINGLE);
}

static inline bool irq_domain_is_ipi_per_cpu(struct irq_domain *domain)
{
	return domain->flags & IRQ_DOMAIN_FLAG_IPI_PER_CPU;
}

static inline bool irq_domain_is_ipi_single(struct irq_domain *domain)
{
	return domain->flags & IRQ_DOMAIN_FLAG_IPI_SINGLE;
}

static inline bool irq_domain_is_msi(struct irq_domain *domain)
{
	return domain->flags & IRQ_DOMAIN_FLAG_MSI;
}

static inline bool irq_domain_is_msi_remap(struct irq_domain *domain)
{
	return domain->flags & IRQ_DOMAIN_FLAG_MSI_REMAP;
}

extern bool irq_domain_hierarchical_is_msi_remap(struct irq_domain *domain);

#else	/* CONFIG_IRQ_DOMAIN_HIERARCHY */
static inline void irq_domain_activate_irq(struct irq_data *data) { }
static inline void irq_domain_deactivate_irq(struct irq_data *data) { }
static inline int irq_domain_alloc_irqs(struct irq_domain *domain,
			unsigned int nr_irqs, int node, void *arg)
{
	return -1;
}

static inline void irq_domain_free_irqs(unsigned int virq,
					unsigned int nr_irqs) { }

static inline bool irq_domain_is_hierarchy(struct irq_domain *domain)
{
	return false;
}

static inline bool irq_domain_is_ipi(struct irq_domain *domain)
{
	return false;
}

static inline bool irq_domain_is_ipi_per_cpu(struct irq_domain *domain)
{
	return false;
}

static inline bool irq_domain_is_ipi_single(struct irq_domain *domain)
{
	return false;
}

static inline bool irq_domain_is_msi(struct irq_domain *domain)
{
	return false;
}

static inline bool irq_domain_is_msi_remap(struct irq_domain *domain)
{
	return false;
}

static inline bool
irq_domain_hierarchical_is_msi_remap(struct irq_domain *domain)
{
	return false;
}
#endif	/* CONFIG_IRQ_DOMAIN_HIERARCHY */

#else /* CONFIG_IRQ_DOMAIN */
static inline void irq_dispose_mapping(unsigned int virq) { }
static inline void irq_domain_activate_irq(struct irq_data *data) { }
static inline void irq_domain_deactivate_irq(struct irq_data *data) { }
static inline struct irq_domain *irq_find_matching_fwnode(
	struct fwnode_handle *fwnode, enum irq_domain_bus_token bus_token)
{
	return NULL;
}
#endif /* !CONFIG_IRQ_DOMAIN */

#endif /* _LINUX_IRQDOMAIN_H */
