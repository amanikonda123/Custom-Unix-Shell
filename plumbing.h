#ifndef PLUMBING
#define PLUMBING

int one_stage_pipeline(pipeline pl, sigset_t mask);
int mult_stage_pipeline(pipeline pl, sigset_t mask);
void handle_batch_in(pipeline pl);
void handle_batch_out(pipeline pl);
void free_ends(int **ends, int procs);

#endif
