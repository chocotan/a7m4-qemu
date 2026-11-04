struct a7m4_release_state { unsigned inhibit_mask, updates, active_release; };
int a7m4_release_reply(struct a7m4_release_state *, const void *, unsigned, void *);
