#ifndef __raft_h__
#define __raft_h__

struct raft_node;
struct ipc_cons_port;

enum raft_state
{
    rfs_follower,
    rfs_candidate,
    rfs_master,
};

typedef i32 (*rf_start_vote)(struct raft_node* node, i32 target_service_id);
typedef i32 (*rf_vote)(struct raft_node* node, i32 target_service_id);
typedef i32 (*rf_iam_master)(struct raft_node* node, i32 target_service_id);
typedef i32 (*rf_keep_alive)(struct raft_node* node, i32 target_service_id, const char* log_update, i32 log_size);

typedef i32 (*rf_on_start_vote)(struct raft_node* node, i32 from_service_id, i32 term_id);
typedef i32 (*rf_on_vote_me)(struct raft_node* node, i32 from_service_id, i32 term_id);
typedef i32 (*rf_on_someone_is_master)(struct raft_node* node, i32 from_service_id, i32 term_id);
typedef i32 (*rf_on_keep_alive)(struct raft_node* node, i32 from_service_id, const char* log_update, i32 log_size);

struct raft_msg_sync_op
{
    rf_start_vote _start_vote_func;
    rf_vote _vote_func;
    rf_iam_master _iam_master_func;
    rf_keep_alive _keep_alive_func;

    rf_on_start_vote _on_start_vote_func;
    rf_on_vote_me _on_vote_me_func;
    rf_on_someone_is_master _on_someone_is_master_func;
    rf_on_keep_alive _on_keep_alive_func;
};

struct raft_node* rf_create_node(i32 service_id, struct ipc_cons_port* recv_port, const struct raft_msg_sync_op* syn_op);
void rf_destroy_node(struct raft_node* node);
void update(struct raft_node* node);

i32 rf_state(struct raft_node* node);
i32 rf_term(struct raft_node* node);
void rf_reset_timeout(struct raft_node* node, u32 current_time);

#endif
