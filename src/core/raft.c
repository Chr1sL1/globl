#include "common_types.h"
#include "core/raft.h"
#include "core/ipc_channel.h"

struct raft_node
{
    i32 _service_id;
    i32 _term;
    i32 _state;
    u32 _vote_timeout;
    u32 _next_start_vote_time;

    struct ipc_cons_port* _recv_port;
    struct raft_msg_sync_op _syn_op;
};

struct raft_node* rf_create_node(i32 service_id, struct ipc_cons_port* recv_port, const struct raft_msg_sync_op* syn_op)
{

error_ret:
    return 0;
}

void rf_destroy_node(struct raft_node* node)
{

}

void update(struct raft_node* node)
{

}

i32 rf_state(struct raft_node* node)
{
    return node->_state;
}

i32 rf_term(struct raft_node* node)
{
    return node->_term;
}

void rf_reset_timeout(struct raft_node* node, u32 current_time)
{
    node->_next_start_vote_time = current_time + node->_vote_timeout;
}
