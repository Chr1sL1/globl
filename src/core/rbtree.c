#include "common_types.h"
#include "core/rbtree.h"
#include "core/dlist.h"
#include "core/common.h"

#define is_black(node)	(!(node) || (node)->isblack)
#define is_red(node)	((node) && !(node)->isblack)
#define set_black(node)	((node) ? (node)->isblack = 1 : 0)
#define set_red(node)	((node) ? (node)->isblack = 0 : 0)
#define l_child(node)	((node) ? (node)->lchild : 0)
#define r_child(node)	((node) ? (node)->rchild : 0)


#define MAX_TRAVERSE_QUEUE_LEN (1024)

static inline void _cp_color(struct rbnode* to, struct rbnode* from)
{
	if(to)
		to->isblack = from ? from->isblack : 1;
}

static inline void _swap_color(struct rbnode* x, struct rbnode* y)
{
	i32 isblackx = is_black(x);
	i32 isblacky = is_black(y);

	if(isblackx)
		set_black(y);
	else
		set_red(y);

	if(isblacky)
		set_black(x);
	else
		set_red(x);
}

static inline void _swap_value(u64* v1, u64* v2)
{
	u64 tmp = *v1;
	*v1 = *v2;
	*v2 = tmp;
}

static inline void _set_parent(struct rbnode* node, struct rbnode* p)
{
	u64 pval = (u64)p;
	pval |= node->isblack;
	node->p = (struct rbnode*)pval;
}

static inline struct rbnode* _get_parent(struct rbnode* node)
{
	if(!node) goto error_ret;
	u64 pval = (u64)node->p;
	pval &= -4;
	return (struct rbnode*)pval;
error_ret:
	return 0;
}

static inline i32 _def_order_func(void* key, struct rbnode* n)
{
	if(key < n->key) return -1;
	else if(key > n->key) return 1;

	return 0;
}

static inline i32 _compare_key(struct rbtree* t, void* key , struct rbnode* n)
{
	if(!t->cfunc)
		return _def_order_func(key, n);

	return (*t->cfunc)(key, n);
}


inline void rb_init(struct rbtree* t, compare_function cf)
{
	t->size = 0;
	t->depth = 0;
	t->root = 0;
	t->cfunc = cf;
}

inline void rb_fillnew(struct rbnode* node)
{
	if(node)
	{
		node->key = 0;
		node->isblack = 0;
		node->lchild = 0;
		node->rchild = 0;
		_set_parent(node, 0);
	}
}

inline struct rbnode* rb_parent(struct rbnode* node)
{
	if(!node) goto error_ret;
	return _get_parent(node);
error_ret:
	return 0;
}

inline struct rbnode* rb_sibling(struct rbnode* node)
{
	struct rbnode* p = rb_parent(node);
	if(!p) goto error_ret;

	if(node == l_child(p))
		return r_child(p);
	else if(node == r_child(p))
		return l_child(p);
error_ret:
	return 0;
}

inline void rb_reset_compare_function(struct rbtree* t, compare_function cf)
{
	t->cfunc = cf;
}

static inline void _left_rotate(struct rbtree* t, struct rbnode* node)
{
	struct rbnode* rc;
	struct rbnode* node_p;

	if(node == 0) goto error_ret;

	rc = node->rchild;
	if(rc == 0) goto error_ret;

	node_p = _get_parent(node);

	_set_parent(rc, node_p);

	if(node_p)
	{
		if(node == node_p->lchild)
			node_p->lchild = rc;
		else
			node_p->rchild = rc;
	}

	_set_parent(node, rc);

	if(rc->lchild)
		_set_parent(rc->lchild, node);

	node->rchild = rc->lchild;

	rc->lchild = node;

	if(!node_p)
		t->root = rc;

error_ret:
	return;
}

static inline void _right_rotate(struct rbtree* t, struct rbnode* node)
{
	struct rbnode* lc;
	struct rbnode* node_p;

	if(node == 0) goto error_ret;

	lc = node->lchild;
	if(lc == 0) goto error_ret;

	node_p = _get_parent(node);

	_set_parent(lc, node_p);

	if(node_p)
	{
		if(node == node_p->lchild)
			node_p->lchild = lc;
		else
			node_p->rchild = lc;
	}

	_set_parent(node, lc);

	if(lc->rchild)
		_set_parent(lc->rchild, node);

	node->lchild = lc->rchild;

	lc->rchild = node;

	if(!node_p)
		t->root = lc;

error_ret:
	return;
}


static void _fix_rr(struct rbtree* t, struct rbnode* node)
{
	struct rbnode* p, *g, *u;
	if(!node) goto error_ret;

	p = _get_parent(node);
	if(!p) goto error_ret;

	while(is_red(p))
	{
		p = _get_parent(node);
		g = _get_parent(p);

		if(p == g->lchild)
		{
			u = g->rchild;
			if(is_red(u))
			{
				// rr-1
				set_black(p);
				set_black(u);
				set_red(g);
				node = g;
				p = _get_parent(node);
			}
			else
			{
				if(node == p->rchild)		// zigzag
				{
					_left_rotate(t, p);
				}
				else
				{
					node = p;
				}

				set_black(node);
				set_red(g);
				_right_rotate(t, g);

				break;
			}
		}
		else
		{
			u = g->lchild;
			if(is_red(u))
			{
				// rr-1
				set_black(p);
				set_black(u);
				set_red(g);
				node = g;
				p = _get_parent(node);
			}
			else
			{
				if(node == p->lchild)		// zigzag
				{
					_right_rotate(t, p);
				}
				else
				{
					node = p;
				}

				set_black(node);
				set_red(g);
				_left_rotate(t, g);

				break;
			}
		}

	}

	while(node)
	{
		p = _get_parent(node);
		if(!p)
			break;

		node = p;
	}
	t->root = node;
	set_black(t->root);

error_ret:
	return;
}

i32 rb_insert(struct rbtree* t, struct rbnode* node)
{
	i32 comp_ret;
	struct rbnode* hot;

	if(!node || ((u64)node & 0x3) != 0) goto error_ret;
	if(rb_search(t, node->key, &hot) != 0) goto error_ret;

	node->isblack = 0;
	node->lchild = 0;
	node->rchild = 0;
	_set_parent(node, hot);

	if(hot != 0)
	{
		comp_ret = _compare_key(t, node->key, hot);

		if(comp_ret < 0)
			hot->lchild = node;
		else if(comp_ret > 0)
			hot->rchild = node;
		else
			goto error_ret;

		_set_parent(node, hot);
		_fix_rr(t, node);
	}
	else
		t->root = node;

	t->root->isblack = 1;

	++t->size;
	return 0;
error_ret:
	return -1;
}

struct rbnode* rb_search(struct rbtree* t, void* key, struct rbnode** hot)
{
	i32 comp_ret = 1;
	struct rbnode* h;
	struct rbnode* p = t->root;

	h = t->root;
	*hot = 0;

	if(!t->root) goto error_ret;

	while(p && comp_ret != 0)
	{
		h = p;
		comp_ret = _compare_key(t, key, p);
		if(comp_ret < 0)
			p = p->lchild;
		else if(comp_ret > 0)
			p = p->rchild;
	}

	if(h) *hot = h;

	return p;
error_ret:
	return 0;
}

static inline void _link(struct rbnode* p, struct rbnode* lc, struct rbnode* rc)
{
	if(!p) goto error_ret;
	if(p == lc || p == rc) goto error_ret;

	p->lchild = lc;
	p->rchild = rc;

	lc ? _set_parent(lc, p) : 0;
	rc ? _set_parent(rc, p) : 0;

error_ret:
	return;
}

static inline void _swap_node(struct rbnode* p, struct rbnode* q)
{
	struct rbnode* plc, *prc;
	struct rbnode* qlc, *qrc;
	struct rbnode* pp, *qp;

	if(!p && !q) goto error_ret;


	if(p)
	{
		plc = p->lchild;
		prc = p->rchild;
		pp = _get_parent(p);
	}
	if(q)
	{
		qlc = q->lchild;
		qrc = q->rchild;
		qp = _get_parent(q);
	}

	if(plc) _set_parent(plc, q);
	if(prc) _set_parent(prc, q);
	if(qlc) _set_parent(qlc, p);
	if(qrc) _set_parent(qrc, p);

	if(pp)
	{
		if(p == pp->lchild)
			pp->lchild = q;
		else if(p == pp->rchild)
			pp->rchild = q;
	}

	if(qp)
	{
		if(q == qp->lchild)
			qp->lchild = p;
		else if(q == qp->rchild)
			qp->rchild = p;

	}

	_swap_value((u64*)&p->lchild, (u64*)&q->lchild);
	_swap_value((u64*)&p->rchild, (u64*)&q->rchild);

	_swap_value((u64*)&p->p, (u64*)&q->p);
//	_swap_color(p, q);

error_ret:
	return;

//
//	_set_parent(plc, q);
//	_set_parent(prc, q);
//	_set_parent(qlc, p);
//	_set_parent(qrc, p);
//
//	if(p == pp->lchild)
//		pp->lchild = q;
//	else if(p == pp->rchild)
//		pp->rchild = q;
//
//	if(q == qp->lchild)
//		qp->lchild = p;
//	else if(q == qp->rchild)
//		qp->rchild = p;
//
//	_swap_value(&p->lchild, &q->lchild);
//	_swap_value(&p->rchild, &q->rchild);
//
//	_set_parent(p, qp);
//	_set_parent(q, pp);
//
//	if(isblackp)
//		set_black(q);
//	else
//		set_red(q);
//
//	if(isblackq)
//		set_black(p);
//	else
//		set_red(p);
}

static inline void _transplant(struct rbtree* t, struct rbnode* rm, struct rbnode* sc)
{
	struct rbnode* rm_p, *sc_p;
//	if(!t->root) goto error_ret;
	if(!rm) goto error_ret;

	rm_p = _get_parent(rm);

	if(sc)
	{
		sc_p = _get_parent(sc);

		if(sc == sc_p->lchild)
			sc_p->lchild = 0;
		else if(sc == sc_p->rchild)
			sc_p->rchild = 0;
	}

	if(!rm_p)	 // root
	{
		_link(sc, rm->lchild, rm->rchild);
		t->root = sc;
		if(sc) _set_parent(sc, 0);
	}
	else
	{
		if(rm == rm_p->lchild)
			_link(rm_p, sc, rm_p->rchild);
		if(rm == rm_p->rchild)
			_link(rm_p, rm_p->lchild, sc);

		_link(sc, rm->lchild, 0);
	}

	_set_parent(rm, 0);
	rm->lchild = 0;
	rm->rchild = 0;

error_ret:
	return;
}


static struct rbnode* _succ(struct rbnode* subroot)
{
	struct rbnode* p = subroot;
	if(!subroot) goto error_ret;

	while(subroot)
	{
		p = subroot;
		subroot = subroot->lchild;
	}

	return p;
error_ret:
	return 0;
}

struct rbnode* rb_succ(struct rbnode* x)
{
	struct rbnode* r = r_child(x);
	struct rbnode* y = _get_parent(x);

	if(r)
		return _succ(r);
	else
	{
		while(y && x == r_child(y))
		{
			x = y;
			y = _get_parent(y);
		}
	}

	return y;
}


void _fix_bb_new(struct rbtree* t, struct rbnode* x)
{
	struct rbnode* p, *w;

	while(x != t->root && is_black(x))
	{
		p = _get_parent(x);
		if(!p)
			break;

		if(x == l_child(p))
		{
			w = r_child(p);
			if(is_red(w))
			{
				set_black(w);
				set_red(p);
				_left_rotate(t, p);
				w = r_child(p);
			}

			if(is_black(l_child(w)) && is_black(r_child(w)))
			{
				set_red(w);
				x = p;
			}
			else
			{
				if(is_black(r_child(w)))
				{
					set_black(l_child(w));
					set_red(w);
					_right_rotate(t, w);
					w = r_child(p);
				}
				else
				{
					_cp_color(w, p);
					set_black(p);
					set_black(r_child(w));
					_left_rotate(t, p);
					x = t->root;
				}
			}
		}
		else
		{
			w = l_child(p);
			if(is_red(w))
			{
				set_black(w);
				set_red(p);
				_right_rotate(t, p);
				w = l_child(p);
			}

			if(is_black(r_child(w)) && is_black(l_child(w)))
			{
				set_red(w);
				x = p;
			}
			else
			{
				if(is_black(l_child(w)))
				{
					set_black(r_child(w));
					set_red(w);
					_left_rotate(t, w);
					w = l_child(p);
				}
				else
				{
					_cp_color(w, p);
					set_black(p);
					set_black(l_child(w));
					_right_rotate(t, p);
					x = t->root;
				}

			}
		}
	}
	set_black(x);
}


void rb_remove_node(struct rbtree* t, struct rbnode* x)
{
	struct rbnode* r, *p, *rr;
	i32 double_black = 0;

	if(x->lchild && x->rchild)
	{
		struct rbnode* rr;
		r = _succ(x->rchild);
		rr = r->rchild;

		p = _get_parent(x);

		if(is_black(r))
			double_black = 1;
//		else
//		{
//			set_black(r);
//		}

		if(x == t->root)
			t->root = r;

		_swap_node(x, r);

		if(double_black)
			_fix_bb_new(t, x);

		_transplant(t, x, rr);
	}
	else
	{
		if(!x->rchild)
			r = x->lchild;
		else if(!x->lchild)
			r = x->rchild;

		p = _get_parent(x);

		if(is_black(x)/* && is_black(r)*/)
			double_black = 1;
//		else
//			set_black(r);

		if(double_black)
			_fix_bb_new(t, x);

		_transplant(t, x, r);

	}


//	if(!double_black)
//		set_black(r);
//	else
//	{
//		_fix_bb(t, r, p);
//	}

	set_black(t->root);

	_set_parent(x, 0);
	x->lchild = 0;
	x->rchild = 0;
	
	--t->size;
	if(t->size <= 0)
		t->root = 0;

}

struct rbnode* rb_remove(struct rbtree* t, void* key)
{
	struct rbnode* r;
	struct rbnode* x;
	i32 isredr, isredx;

	if(!t->root) goto error_ret;
	if(t->size <= 0) return 0;

	x = rb_search(t, key, &r);
	if(!x) goto error_ret;

	rb_remove_node(t, x);

	return x;
error_ret:
	return 0;
}

void pre_order(struct rbnode* node, order_function f)
{
	if(!node || !f)
		return;

	f(node);
	pre_order(node->lchild, f);
	pre_order(node->rchild, f);
}

void in_order(struct rbnode* node, order_function f)
{
	if(!node || !f)
		return;

	in_order(node->lchild, f);
	f(node);
	in_order(node->rchild, f);
}

void post_order(struct rbnode* node, order_function f)
{
	if(!node || !f)
		return;

	post_order(node->lchild, f);
	post_order(node->rchild, f);
	f(node);
}
