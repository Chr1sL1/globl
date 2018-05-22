#ifndef __common_h__
#define __common_h__

#define node_cast(out_type, ptr, m_var)\
	(struct out_type*)((void*)ptr - (size_t)&(((struct out_type*)0)->m_var))

#endif
