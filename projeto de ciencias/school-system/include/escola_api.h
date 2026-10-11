#ifndef SCHOOL_ESCOLA_API_H
#define SCHOOL_ESCOLA_API_H

int escola_api_whoami(void);
int escola_api_ref(void);
int escola_api_inbox(int limit);
int escola_api_reply(const char *message_id, const char *text);

/* Worker de consulta automática à inbox. O ponteiro pertence ao servidor. */
int escola_api_worker_start(volatile int *should_stop);
void escola_api_worker_join(void);

#endif
