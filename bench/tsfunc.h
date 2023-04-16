#ifndef _H_TSFUNC_
#define _H_TSFUNC_

extern blm_t			m;
extern pmc_t			pm;

extern void tlm_restart();
extern void sim_runtime(double dT);

int ts_wait_for_idle();
int ts_wait_for_spinup(float s_ref);
int ts_wait_for_motion(float s_ref);

void ts_script_base();
void ts_script_verify();

#endif /* _H_TSFUNC_ */

