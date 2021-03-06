#define _GNU_SOURCE

#include <assert.h>
#include <err.h>
#include <fcntl.h>
#include <inttypes.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/personality.h>
#include <sys/poll.h>
#include <sys/ptrace.h>
#include <asm/ptrace-abi.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/user.h>

#include "rep_sched.h"
#include "rep_process_event.h"
#include "rep_process_signal.h"
#include "read_trace.h"
#include "remote.h"

#include <netinet/in.h>


#include "../share/hpc.h"
#include "../share/trace.h"
#include "../share/ipc.h"
#include "../share/sys.h"
#include "../share/util.h"


#include <perfmon/pfmlib_perf_event.h>

#define SAMPLE_SIZE 		10
#define NUM_SAMPLE_PAGES 	1

static pid_t child;

/**
 * used to stop child process when the parent process bails out
 */
static void sig_child(int sig)
{
	kill(child, SIGINT);
	kill(getpid(), SIGQUIT);
}

static void single_step(struct context* context)
{
	int status, inst_size;
	char buf[50];
	char* rec_inst, *inst;
	printf("starting to single-step: time=%u\n", context->trace.global_time);
	/* compensate the offset in the recorded instruction trace that
	 * comes from the mandatory additional singlestep (to actually step over
	 * the system call) in the replayer.
	 */
	rec_inst = peek_next_inst(context);
	inst = get_inst(context->child_tid, 0, &inst_size);
	sprintf(buf, "%d:%s", context->rec_tid, inst);
	if (strncmp(buf, rec_inst, strlen(buf)) != 0) {
		printf("rec: %s  cur: %s\n", rec_inst, inst);
		sys_free((void**) &rec_inst);
		sys_free((void**) &inst);
		inst_dump_skip_entry(context);
	}

	int print_fileinfo = 1;
	while (1) {
		rec_inst = read_inst(context);

		/* check if the trace file is done */
		if (strncmp(rec_inst, "__done__", 7) == 0) {
			break;
		}

		if (print_fileinfo) {
			get_eip_info(context->child_tid);
			print_fileinfo = 0;
		}
		inst = get_inst(context->child_tid, 0, &inst_size);

		sprintf(buf, "%d:%s", context->rec_tid, inst);

		if ((strncmp(inst, "sysenter", 7) == 0) || (strncmp(inst, "int", 3) == 0)) {
			sys_free((void**) &inst);
			break;
		}
		struct user_regs_struct rec_reg, cur_reg;
		inst_dump_parse_register_file(context, &rec_reg);
		read_child_registers(context->child_tid, &cur_reg);

		if (context->rec_tid == 18024) {
			compare_register_files("now", &cur_reg, "recorded", &rec_reg, 0, 0);
		}

		fprintf(stderr, "thread: %d ecx=%lx\n", context->rec_tid, read_child_ecx(context->child_tid));
		if (strncmp(buf, rec_inst, strlen(buf)) != 0) {
			fprintf(stderr, "now: %s rec: %s\n", buf, rec_inst);
			fflush(stderr);
			get_eip_info(context->child_tid);
			printf("time: %u\n", context->trace.global_time);
			fflush(stdout);
			//sys_exit();
		} else {
			fprintf(stderr, "ok: %s:\n", buf);
			if (strncmp(inst, "ret", 3) == 0) {
				print_fileinfo = 1;
			}
		}

		sys_free((void**) &rec_inst);
		sys_free((void**) &inst);

		sys_ptrace_singlestep(context->child_tid, context->pending_sig);
		sys_waitpid(context->child_tid, &status);
		context->pending_sig = 0;

		if (
		WSTOPSIG(status) == SIGSEGV) {
			return;
		}
	}
}



static void check_initial_register_file()
{
	struct context *context = rep_sched_get_thread();

	write_child_ebx(context->child_tid, context->trace.recorded_regs.ebx);
	write_child_edx(context->child_tid, context->trace.recorded_regs.edx);
	write_child_ebp(context->child_tid, context->trace.recorded_regs.ebp);

	struct user_regs_struct r;
	read_child_registers(context->child_tid, &r);
	compare_register_files("init_rec", &(context->trace.recorded_regs), "now", &r, 1, 1);

}

void replay()
{
	check_initial_register_file();
	//gdb_connect(1234);

	struct context *ctx = NULL;

	while (rep_sched_get_num_threads()) {
		ctx = rep_sched_get_thread();

	//	char* command = get_command(context);

	//	if (strncmp(command,"k",1) == 0) {
	//		break;
	//	}

		/* print some kind of progress */
		if (ctx->trace.global_time % 1000 == 0) {
			fprintf(stderr,".");
		}

		if (ctx->trace.state == 0) {
			//	single_step(context);
		}
		if (ctx->trace.stop_reason == USR_EXIT) {
			rep_sched_deregister_thread(ctx);
			//cleanup = 0;
			/* stop reason is a system call - can be done with ptrace */
		} else if ((int) (ctx->trace.stop_reason) > 0) {
			/* proceed to the next event */
			rep_process_syscall(ctx);
			/* stop reason is a signal - use HPC */
		} else {
			rep_process_signal(ctx);
		}
	}
	fprintf(stderr,"\nreplayer sucessfully finished\n");
	//gdb_disconnect();
}
