#ifndef __raft_h__
#define __raft_h__

#ifdef __cplusplus
extern "C" {
#endif

struct raft_node;

enum raft_state
{
    rfs_unavaiable,
    rfs_follower,
    rfs_candidate,
    rfs_master,
};

typedef i32 (*rf_common_func)(struct raft_node* node, i32 target_service_id, i32 term_id);
typedef i32 (*rf_keep_alive)(struct raft_node* node, i32 target_service_id, i32 term_id, const char* log_update, i32 log_size);

//typedef i32 (*rf_on_start_vote)(struct raft_node* node, i32 from_service_id, i32 term_id);
//typedef i32 (*rf_on_someone_vote_me)(struct raft_node* node, i32 from_service_id, i32 term_id);
//typedef i32 (*rf_on_someone_declare_master)(struct raft_node* node, i32 from_service_id, i32 term_id);
//typedef i32 (*rf_on_keep_alive)(struct raft_node* node, i32 from_service_id, const char* log_update, i32 log_size);

struct raft_msg_sync_op
{
    rf_common_func _request_vote_func;
    rf_common_func _vote_func;
    rf_keep_alive _keep_alive_func;

//    rf_on_start_vote _on_start_vote_func;
//    rf_on_someone_vote_me _on_someone_vote_me_func;
//    rf_on_someone_declare_master _on_someone_declare_master_func;
//    rf_on_keep_alive _on_keep_alive_func;
};

struct raft_node* rf_create_node(i32 service_id, const struct raft_msg_sync_op* syn_op, void* usr_ptr);
void rf_destroy_node(struct raft_node* node);
void rf_start_service(struct raft_node* node);
void rf_update(struct raft_node* node);
i32 rf_add_remote_node(struct raft_node* node, i32 service_id);

void rf_on_request_vote(struct raft_node* node, i32 remote_service_id, i32 term_id);
void rf_on_vote_for_me(struct raft_node* node, i32 remote_service_id, i32 term_id);
void rf_on_keep_alive(struct raft_node* node, i32 remote_service_id, i32 term_id);

i32 rf_state(struct raft_node* node);
i32 rf_term(struct raft_node* node);
i32 rf_service_id(struct raft_node* node);
void* rf_usr_ptr(struct raft_node* node);
void rf_reset_timeout(struct raft_node* node, u32 current_time);

#ifdef __cplusplus
}
#endif

#endif
