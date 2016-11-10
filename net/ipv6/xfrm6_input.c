/*
 * xfrm6_input.c: based on net/ipv4/xfrm4_input.c
 *
 * Authors:
 *	Mitsuru KANDA @USAGI
 * 	Kazunori MIYAZAWA @USAGI
 * 	Kunihiro Ishiguro <kunihiro@ipinfusion.com>
 *	YOSHIFUJI Hideaki @USAGI
 *		IPv6 support
 */

#include <linux/module.h>
#include <linux/string.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv6.h>
#include <net/ipv6.h>
#include <net/xfrm.h>

int xfrm6_rcv_spi(struct sk_buff *skb, u32 spi)
{
	int err;
	u32 seq;
	struct xfrm_state *xfrm_vec[XFRM_MAX_DEPTH];
	struct xfrm_state *x;
	int xfrm_nr = 0;
	int decaps = 0;
	int nexthdr;
	unsigned int nhoff;

	nhoff = IP6CB(skb)->nhoff;
	nexthdr = skb->nh.raw[nhoff];

	seq = 0;
	if (!spi && (err = xfrm_parse_spi(skb, nexthdr, &spi)) != 0)
		goto drop;
	
	do {
		struct ipv6hdr *iph = skb->nh.ipv6h;

		if (xfrm_nr == XFRM_MAX_DEPTH)
			goto drop;

		x = xfrm_state_lookup((xfrm_address_t *)&iph->daddr, spi, nexthdr, AF_INET6);
		if (x == NULL)
			goto drop;
		spin_lock(&x->lock);
		if (unlikely(x->km.state != XFRM_STATE_VALID))
			goto drop_unlock;

		if (xfrm_state_check_expire(x))
			goto drop_unlock;

		nexthdr = x->type->input(x, skb);
		if (nexthdr <= 0)
			goto drop_unlock;

		skb->nh.raw[nhoff] = nexthdr;

		x->curlft.bytes += skb->len;
		x->curlft.packets++;
		x->curlft.use_time = (unsigned long) xtime.tv_sec;

		spin_unlock(&x->lock);

		xfrm_vec[xfrm_nr++] = x;

		if (x->mode->input(x, skb))
			goto drop;

		if (x->props.mode == XFRM_MODE_TUNNEL) { /* XXX */
			decaps = 1;
			break;
		}

		if ((err = xfrm_parse_spi(skb, nexthdr, &spi)) < 0)
			goto drop;

	} while (!err);

	/* Allocate new secpath or COW existing one. */
	if (!skb->sp || atomic_read(&skb->sp->refcnt) != 1) {
		struct sec_path *sp;
		sp = secpath_dup(skb->sp);
		if (!sp)
			goto drop;
		if (skb->sp)
			secpath_put(skb->sp);
		skb->sp = sp;
	}

	if (xfrm_nr + skb->sp->len > XFRM_MAX_DEPTH)
		goto drop;

	memcpy(skb->sp->xvec + skb->sp->len, xfrm_vec,
	       xfrm_nr * sizeof(xfrm_vec[0]));
	skb->sp->len += xfrm_nr;
	skb->ip_summed = CHECKSUM_NONE;

	nf_reset(skb);

	if (decaps) {
		if (!(skb->dev->flags&IFF_LOOPBACK)) {
			dst_release(skb->dst);
			skb->dst = NULL;
		}
		netif_rx(skb);
		return -1;
	} else {
#ifdef CONFIG_NETFILTER
		skb->nh.ipv6h->payload_len = htons(skb->len);
		__skb_push(skb, skb->data - skb->nh.raw);

		NF_HOOK(PF_INET6, NF_IP6_PRE_ROUTING, skb, skb->dev, NULL,
		        ip6_rcv_finish);
		return -1;
#else
		return 1;
#endif
	}

drop_unlock:
	spin_unlock(&x->lock);
	xfrm_state_put(x);
drop:
	while (--xfrm_nr >= 0)
		xfrm_state_put(xfrm_vec[xfrm_nr]);
	kfree_skb(skb);
	return -1;
}

EXPORT_SYMBOL(xfrm6_rcv_spi);

int xfrm6_rcv(struct sk_buff **pskb)
{
	return xfrm6_rcv_spi(*pskb, 0);
}

#ifdef CONFIG_XFRM_ENHANCEMENT
int __xfrm6_rcv_one(struct sk_buff *skb, xfrm_address_t *daddr,
 		    xfrm_address_t *saddr, u8 proto)
{
 	struct xfrm_state *x = NULL;
 	int wildcard = 0;
	struct in6_addr any;
	xfrm_address_t *xany;
 	struct xfrm_state *xfrm_vec_one = NULL;
 	int nh = 0;
	int i = 0;

	ipv6_addr_set(&any, 0, 0, 0, 0);
	xany = (xfrm_address_t *)&any;

	for (i = 0; i < 3; i++) {
		xfrm_address_t *dst, *src;
		switch (i) {
		case 0:
			dst = daddr;
			src = saddr;
			break;
		case 1:
			/* lookup state with wild-card source address */
			wildcard = 1;
			dst = daddr;
			src = xany;
			break;
		case 2:
		default:
 			/* lookup state with wild-card addresses */
			wildcard = 1; /* XXX */
			dst = xany;
			src = xany;
			break;
 		}

		x = xfrm_state_lookup_byaddr(dst, src, proto, AF_INET6);
		if (!x)
			continue;

		spin_lock(&x->lock);

		if (wildcard) {
			if ((x->props.flags & XFRM_STATE_WILDRECV) == 0) {
				printk(KERN_INFO "%s: found state is not wild-card.\n", __FUNCTION__);
				spin_unlock(&x->lock);
				xfrm_state_put(x);
				x = NULL;
				continue;
			}
		}

		if (unlikely(x->km.state != XFRM_STATE_VALID)) {
			spin_unlock(&x->lock);
			xfrm_state_put(x);
 			x = NULL;
 			continue;
		}
		if (xfrm_state_check_expire(x)) {
			spin_unlock(&x->lock);
			xfrm_state_put(x);
			x = NULL;
			continue;
		}

		nh = x->type->input(x, skb);
		if (nh <= 0) {
			spin_unlock(&x->lock);
			xfrm_state_put(x);
			x = NULL;
			continue;
		}

		break;
	}

	if (!x)
		goto error;

 	x->curlft.bytes += skb->len;
 	x->curlft.packets++;
	x->curlft.use_time = (unsigned long) xtime.tv_sec;

 	spin_unlock(&x->lock);

 	xfrm_vec_one = x;

 	/* Allocate new secpath or COW existing one. */
 	if (!skb->sp || atomic_read(&skb->sp->refcnt) != 1) {
 		struct sec_path *sp;
 		sp = secpath_dup(skb->sp);
 		if (!sp) {
 			printk(KERN_INFO "%s: dup secpath failed\n", __FUNCTION__);
 			goto error;
 		}
 		if (skb->sp)
 			secpath_put(skb->sp);
 		skb->sp = sp;
 	}

 	if (1 + skb->sp->len > XFRM_MAX_DEPTH) {
 		printk(KERN_INFO "%s: too many states\n", __FUNCTION__);
 		goto error;
 	}

	skb->sp->xvec[skb->sp->len] = xfrm_vec_one;
 	skb->sp->len ++;
 	skb->ip_summed = CHECKSUM_NONE;

 	return 0;
 error:
 	return -1;
}
#endif

