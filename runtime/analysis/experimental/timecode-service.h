struct a7m4_timecode_state { unsigned updates; unsigned char parameters[20]; };
int a7m4_timecode_reply(struct a7m4_timecode_state *, const void *, unsigned, void *);
