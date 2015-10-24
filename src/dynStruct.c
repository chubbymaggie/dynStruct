#include "dr_api.h"
#include "dr_ir_opnd.h"
#include "drutil.h"
#include "drwrap.h"
#include "drmgr.h"
#include "drsyms.h"
#include "../includes/utils.h"
#include "../includes/block_utils.h"
#include "../includes/allocs.h"
#include "../includes/rw.h"
#include "../includes/out.h"
#include "../includes/call.h"
#include "../includes/sym.h"
#include "../includes/tree.h"
#include "../includes/elf.h"
#include "../includes/args.h"

malloc_t  *old_blocks = NULL;
tree_t	  *active_blocks = NULL;
void      *lock;

// todo code ouput in json

static void thread_exit_event(void *drcontext)
{
  clean_stack(drcontext);
}

// app2app is the first step of instrumentatiob, only use replace string
// instructions by a loop to have a better monitoring
static dr_emit_flags_t bb_app2app_event(void *drcontext,
					__attribute__((unused))void *tag,
					instrlist_t *bb,
					__attribute__((unused))bool for_trace,
					__attribute__((unused))bool translating)
{
  DR_ASSERT(drutil_expand_rep_string(drcontext, bb));

  return DR_EMIT_DEFAULT;
}

// instrument each read or write instruction in order to monitor them
// also instrument each call/return to update the stack of functions
static dr_emit_flags_t bb_insert_event( void *drcontext,
					__attribute__((unused))void *tag,
					instrlist_t *bb, instr_t *instr, 
					__attribute__((unused))bool for_trace,
					__attribute__((unused))bool translating,
				        __attribute__((unused))void *user_data)
{
  app_pc	pc = instr_get_app_pc(instr);

  if (pc == NULL)
    return DR_EMIT_DEFAULT;
   

  // if the module is not monitored, we have to instrument we still
  // have to maj our stack with call addr
  if (pc_is_monitored(pc))
    {

      if (instr_reads_memory(instr))
	for (int i = 0; i < instr_num_srcs(instr); i++)
	  if (opnd_is_memory_reference(instr_get_src(instr, i)))
	    {
	      dr_insert_clean_call(drcontext, bb, instr, &memory_read,
				   false, 1, OPND_CREATE_INTPTR(pc));
	      // break to not instrument the same instruction 2 time
	      break;
	    }

      if (instr_writes_memory(instr))
	for (int i = 0; i < instr_num_dsts(instr); i++)
	  if (opnd_is_memory_reference(instr_get_dst(instr, i)))
	    {
	      dr_insert_clean_call(drcontext, bb, instr, &memory_write,
				   false, 1, OPND_CREATE_INTPTR(pc));
	      // break to not instrument the same instruction 2 time
	      break;
	    }
    }

  // if one day dynStruct has to be used on arm, maybe some call will be missed
  
  // if it's a direct call we send the callee addr as parameter
  if (instr_is_call_direct(instr))
    {
      dr_insert_clean_call(drcontext, bb, instr, &dir_call_monitor,
			   false, 1, OPND_CREATE_INTPTR(instr_get_branch_target_pc(instr)));
    }
  // for indirect call we have to get callee addr on instrumentation function
  else if (instr_is_call_indirect(instr))
    dr_insert_mbr_instrumentation(drcontext, bb, instr, &ind_call_monitor,
				  SPILL_SLOT_1);
  else if (instr_is_return(instr))
    dr_insert_clean_call(drcontext, bb, instr, &ret_monitor,
			 false, 0);
  return DR_EMIT_DEFAULT;
}

static void load_event(void *drcontext,
		       const module_data_t *mod,
		       __attribute__((unused))bool loaded)
{
  app_pc		malloc = (app_pc)dr_get_proc_address(mod->handle, "malloc");
  app_pc		calloc = (app_pc)dr_get_proc_address(mod->handle, "calloc");
  app_pc		realloc = (app_pc)dr_get_proc_address(mod->handle, "realloc");
  app_pc		free = (app_pc)dr_get_proc_address(mod->handle, "free");
  ds_module_data_t	tmp_data;

  dr_mutex_lock(lock);

  if (!maj_args(mod))
    return ;
  
  tmp_data.start = mod->start;
  tmp_data.got = NULL;
  drsym_enumerate_symbols_ex(mod->full_path, sym_to_hashmap,
  			     sizeof(drsym_info_t), &tmp_data,
			     DRSYM_DEMANGLE_FULL);
  add_plt(mod, tmp_data.got, drcontext);
  dr_mutex_unlock(lock);

  // free all data relative to sym (like debug info) after loading symbol
  drsym_free_resources(mod->full_path);

  if (!module_is_monitored(mod))
    return;
  
  if (malloc)
    DR_ASSERT(drwrap_wrap(malloc, pre_malloc, post_malloc));

  if (calloc)
    DR_ASSERT(drwrap_wrap(calloc, pre_calloc, post_calloc));

  if (realloc)
    DR_ASSERT(drwrap_wrap(realloc, pre_realloc, post_realloc));

  if (free)
    DR_ASSERT(drwrap_wrap(free, pre_free, NULL));
}

void unload_event(__attribute__((unused)) void *drcontext,
		 const module_data_t *mod)
{
  remove_plt(mod);
}

static void exit_event(void)
{
  dr_mutex_lock(lock);

  process_recover();

  clean_old_sym();

  hashtable_delete(&sym_hashtab);

  clean_args();
  
  dr_mutex_unlock(lock);
  dr_mutex_destroy(lock);
  
  drsym_exit();
  drwrap_exit();
  drmgr_exit();
  drutil_exit();
}

DR_EXPORT void dr_init(client_id_t id)
{
  char	**argv;
  int	argc;
  drmgr_priority_t p = {
    sizeof(p),
    "reccord heap access and recover datas structures",
    NULL,
    NULL,
    0};
  
  dr_set_client_name("dynStruct", "ampotos@gmail.com");

  drsym_init(0);
  drwrap_init();
  drmgr_init();
  drutil_init();

  dr_get_option_array(id, &argc, (const char ***)&argv);

  if(!parse_arg(argc, argv))
    dr_abort();
  dr_register_exit_event(&exit_event);
  if (!drmgr_register_module_load_event(&load_event) ||
      // only use to remove plt from plt tree if a library is unload at runtime
      !drmgr_register_module_unload_event(&unload_event) ||
      !drmgr_register_bb_app2app_event(&bb_app2app_event, &p) ||
      !drmgr_register_thread_exit_event(&thread_exit_event) ||
      //only use insert event because we only need to monitor single instruction
      !drmgr_register_bb_instrumentation_event(NULL, &bb_insert_event, &p))
    DR_ASSERT_MSG(false, "Can't register event handler\n");
  
  // register slot for each thread
  tls_stack_idx = drmgr_register_tls_field();
  DR_ASSERT_MSG(tls_stack_idx != -1, "Can't register tls field\n");

  // init sym hashtab
  hashtable_init(&sym_hashtab, 16, HASH_INTPTR, false);

  lock = dr_mutex_create();
  DR_ASSERT_MSG(lock, "Can't create mutex\n");
}
