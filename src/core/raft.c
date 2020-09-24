#include "common_types.h"
#include "core/raft.h"
#include "core/ipc_channel.h"
#include "core/vm_space.h"
#include "core/misc.h"

#define RAFT_VOTE_TIMTOUT_MIN   (100)
#define RAFT_VOTE_TIMTOUT_MAX   (300)
#define RAFT_KEEPALIVE_TIME     (50)

struct raft_remote_node
{
    i32 _service_id;
    i32 _state;
    i32 _current_log_idx;

    struct raft_remote_node* _next;
};

struct raft_term
{
    i32 _id;
    i32 _voted_for;
    i32 _master_service_id;
    i32 _recv_vote_cnt;
    u32 _vote_timeout;
};

struct raft_node
{
    i32 _service_id;
    i32 _state;
    i32 _total_node_cnt;
    u32 _next_start_vote_time;
    void* _usr_ptr;

    struct raft_term _term;
    struct raft_msg_sync_op _syn_op;
    struct raft_remote_node _remote_node_head;
};

static inline void __reset_term(struct raft_term* t)
{
    ++t->_id;
    t->_voted_for = -1;
    t->_master_service_id = -1;
    t->_recv_vote_cnt = 0;
    t->_vote_timeout = rand_ex_min_max(RAFT_VOTE_TIMTOUT_MIN, RAFT_VOTE_TIMTOUT_MAX);
}

static inline void __vote_self(struct raft_node* n)
{
    n->_term._voted_for = n->_service_id;
    ++n->_term._recv_vote_cnt;
}

struct raft_node* rf_create_node(i32 service_id, const struct raft_msg_sync_op* syn_op, void* usr_ptr)
{
    struct raft_node* node = (struct raft_node*)vm_common_alloc(sizeof(struct raft_node));
    err_exit(!node, "failed to allocate raft node.");

    node->_service_id = service_id;
    node->_state = rfs_unavaiable;
    node->_next_start_vote_time = 0;
    node->_total_node_cnt = 0;
    node->_syn_op = *syn_op;

    node->_term._id = 0;
    node->_term._voted_for = -1;
    node->_term._master_service_id = -1;
    node->_term._recv_vote_cnt = 0;
    node->_term._vote_timeout = 0;
    node->_usr_ptr = usr_ptr;

    return node;
error_ret:
    return 0;
}

void rf_destroy_node(struct raft_node* node)
{
    err_exit_silent(!node);
    vm_common_free(node);

    return;
error_ret:
    return;
}

void rf_start_service(struct raft_node* node)
{
    err_exit_silent(!node);

    __reset_term(&node->_term);
    node->_state = rfs_follower;
    node->_next_start_vote_time = sys_time_ms() + node->_term._vote_timeout;

    return;
error_ret:
    return;
}

i32 rf_add_remote_node(struct raft_node* node, i32 service_id)
{
    struct raft_remote_node* remote_node = (struct raft_remote_node*)vm_common_alloc(sizeof(struct raft_remote_node));
    err_exit(!remote_node, "add remote node failed.");

    remote_node->_service_id = service_id;
    remote_node->_state = rfs_follower;
    remote_node->_current_log_idx = 0;
    remote_node->_next = node->_remote_node_head._next;

    node->_remote_node_head._next = remote_node;
    ++node->_total_node_cnt;

    return 0;
error_ret:
    return -1;
}

static void __rf_broadcast_remote(struct raft_node* node, rf_common_func f)
{
    struct raft_remote_node* n = node->_remote_node_head._next;
    while (n != NULL)
    {
        (*f)(node, n->_service_id, node->_term._id);
        n = n->_next;
    }
}

void rf_update(struct raft_node* node)
{
    u64 now = sys_time_ms();
    if(now > node->_next_start_vote_time && (node->_state == rfs_follower || node->_state == rfs_candidate))
    {
        node->_state = rfs_candidate;

        __reset_term(&node->_term);
        __vote_self(node);
        node->_next_start_vote_time = now + node->_term._vote_timeout;

        __rf_broadcast_remote(node, node->_syn_op._request_vote_func);
    }
}

void rf_on_request_vote(struct raft_node* node, i32 remote_service_id, i32 term_id)
{
    err_exit_silent(term_id < node->_term._id);

    if(term_id == node->_term._id && node->_term._voted_for >= 0)
        return;

    node->_term._id = term_id;
    node->_state = rfs_follower;
    (*node->_syn_op._vote_func)(node, remote_service_id, term_id);

    return;
error_ret:
    return;
}

void rf_on_vote_for_me(struct raft_node* node, i32 remote_service_id, i32 term_id)
{
    err_exit_silent(term_id < node->_term._id);

    node->_term._id = term_id;
    ++node->_term._recv_vote_cnt;

    if(node->_term._recv_vote_cnt > node->_total_node_cnt / 2)
    {
        node->_state = rfs_master;
        (*node->_syn_op._keep_alive_func)(node, remote_service_id, node->_term._id, NULL, 0);
    }
    return;
error_ret:
    return;
}

void rf_on_keep_alive(struct raft_node* node, i32 remote_service_id, i32 term_id)
{

}

i32 rf_state(struct raft_node* node)
{
    return node->_state;
}

i32 rf_term(struct raft_node* node)
{
    return node->_term._id;
}

void rf_reset_timeout(struct raft_node* node, u32 current_time)
{
    node->_next_start_vote_time = current_time + node->_term._vote_timeout;
}

void* rf_usr_ptr(struct raft_node* node)
{
    return node->_usr_ptr;
}