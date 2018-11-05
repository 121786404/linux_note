/*
 *	Spanning tree protocol; timer-related code
 *	Linux ethernet bridge
 *
 *	Authors:
 *	Lennert Buytenhek		<buytenh@gnu.org>
 *
 *	$Id: br_stp_timer.c,v 1.3 2000/05/05 02:17:17 davem Exp $
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/times.h>
#include <linux/smp_lock.h>

#include "br_private.h"
#include "br_private_stp.h"

/* called under bridge lock */
/**
 * 如果网桥至少拥有一个端口具有指派角色，br_is_designated_for_some_port就返回1,否则返回0.
 */
static int br_is_designated_for_some_port(const struct net_bridge *br)
{
	struct net_bridge_port *p;

	list_for_each_entry(p, &br->port_list, list) {
		if (p->state != BR_STATE_DISABLED &&
		    !memcmp(&p->designated_bridge, &br->bridge_id, 8)) 
			return 1;
	}

	return 0;
}

static void br_hello_timer_expired(unsigned long arg)
{
	struct net_bridge *br = (struct net_bridge *)arg;
	
	pr_debug("%s: hello timer expired\n", br->dev->name);
	spin_lock_bh(&br->lock);
	if (br->dev->flags & IFF_UP) {
		br_config_bpdu_generation(br);

		mod_timer(&br->hello_timer, jiffies + br->hello_time);
	}
	spin_unlock_bh(&br->lock);
}

/**
 * 当一个端口从指派网桥接收信息超期时（一般是因为指派网桥不再是指派网桥，或者指派网桥失效了）
 * 这可能引起拓扑变化，也可能导致根桥变化。
 */
static void br_message_age_timer_expired(unsigned long arg)
{
	struct net_bridge_port *p = (struct net_bridge_port *) arg;
	struct net_bridge *br = p->br;
	const bridge_id *id = &p->designated_bridge;
	int was_root;

	if (p->state == BR_STATE_DISABLED)
		return;

	
	pr_info("%s: neighbor %.2x%.2x.%.2x:%.2x:%.2x:%.2x:%.2x:%.2x lost on port %d(%s)\n",
		br->dev->name, 
		id->prio[0], id->prio[1], 
		id->addr[0], id->addr[1], id->addr[2], 
		id->addr[3], id->addr[4], id->addr[5],
		p->port_no, p->dev->name);

	/*
	 * According to the spec, the message age timer cannot be
	 * running when we are the root bridge. So..  this was_root
	 * check is redundant. I'm leaving it in for now, though.
	 */
	spin_lock_bh(&br->lock);
	if (p->state == BR_STATE_DISABLED)
		goto unlock;
	was_root = br_is_root_bridge(br);

	br_become_designated_port(p);
	br_configuration_update(br);
	br_port_state_selection(br);
	if (br_is_root_bridge(br) && !was_root)
		br_become_root_bridge(br);
 unlock:
	spin_unlock_bh(&br->lock);
}

/**
 * 当一个处于BR_STATE_LEARNING状态的端口，其状态变为BR_STATE_FORWARDING状态时调用。
 */
static void br_forward_delay_timer_expired(unsigned long arg)
{
	struct net_bridge_port *p = (struct net_bridge_port *) arg;
	struct net_bridge *br = p->br;

	pr_debug("%s: %d(%s) forward delay timer\n",
		 br->dev->name, p->port_no, p->dev->name);
	spin_lock_bh(&br->lock);
	if (p->state == BR_STATE_LISTENING) {
		p->state = BR_STATE_LEARNING;
		mod_timer(&p->forward_delay_timer,
			  jiffies + br->forward_delay);
	} else if (p->state == BR_STATE_LEARNING) {
		p->state = BR_STATE_FORWARDING;
		if (br_is_designated_for_some_port(br))
			br_topology_change_detection(br);
	}
	br_log_state(p);
	spin_unlock_bh(&br->lock);
}

static void br_tcn_timer_expired(unsigned long arg)
{
	struct net_bridge *br = (struct net_bridge *) arg;

	pr_debug("%s: tcn timer expired\n", br->dev->name);
	spin_lock_bh(&br->lock);
	if (br->dev->flags & IFF_UP) {
		br_transmit_tcn(br);
	
		mod_timer(&br->tcn_timer,jiffies + br->bridge_hello_time);
	}
	spin_unlock_bh(&br->lock);
}

static void br_topology_change_timer_expired(unsigned long arg)
{
	struct net_bridge *br = (struct net_bridge *) arg;

	pr_debug("%s: topo change timer expired\n", br->dev->name);
	spin_lock_bh(&br->lock);
	br->topology_change_detected = 0;
	br->topology_change = 0;
	spin_unlock_bh(&br->lock);
}

static void br_hold_timer_expired(unsigned long arg)
{
	struct net_bridge_port *p = (struct net_bridge_port *) arg;

	pr_debug("%s: %d(%s) hold timer expired\n", 
		 p->br->dev->name,  p->port_no, p->dev->name);

	spin_lock_bh(&p->br->lock);
	if (p->config_pending)
		br_transmit_config(p);
	spin_unlock_bh(&p->br->lock);
}

static inline void br_timer_init(struct timer_list *timer,
			  void (*_function)(unsigned long),
			  unsigned long _data)
{
	init_timer(timer);
	timer->function = _function;
	timer->data = _data;
}

/**
 * 初始化网桥时钟。
 */
void br_stp_timer_init(struct net_bridge *br)
{
	br_timer_init(&br->hello_timer, br_hello_timer_expired,
		      (unsigned long) br);

	br_timer_init(&br->tcn_timer, br_tcn_timer_expired, 
		      (unsigned long) br);

	br_timer_init(&br->topology_change_timer,
		      br_topology_change_timer_expired,
		      (unsigned long) br);

	br_timer_init(&br->gc_timer, br_fdb_cleanup, (unsigned long) br);
}

/**
 * 初始化端口时钟。
 */
void br_stp_port_timer_init(struct net_bridge_port *p)
{
	br_timer_init(&p->message_age_timer, br_message_age_timer_expired,
		      (unsigned long) p);

	br_timer_init(&p->forward_delay_timer, br_forward_delay_timer_expired,
		      (unsigned long) p);
		      
	br_timer_init(&p->hold_timer, br_hold_timer_expired,
		      (unsigned long) p);
}	

/* Report ticks left (in USER_HZ) used for API */
unsigned long br_timer_value(const struct timer_list *timer)
{
	return timer_pending(timer)
		? jiffies_to_clock_t(timer->expires - jiffies) : 0;
}