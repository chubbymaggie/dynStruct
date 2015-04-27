#include "dr_api.h"
#include "dr_ir_opnd.h"
#include "drwrap.h"
#include "drmgr.h"
#include "../includes/utils.h"
#include "../includes/block_utils.h"

malloc_t  *blocks = NULL;
void      *lock;

int	  first = 1;

static void pre_malloc(void *wrapctx, OUT void **user_data)
{
  malloc_t	*new;

  if (first) // ugly fix to the double call of first *alloc
    {
      first = 0;
      return;
    }

  dr_mutex_lock(lock);

  *user_data = add_block((size_t)drwrap_get_arg(wrapctx, 0));

  dr_mutex_unlock(lock);
}

void set_addr_malloc(malloc_t *block, void *start, unsigned int flag, int realloc)
{
  if (!start && block)
    {
      if (!realloc)
	{
	  dr_printf("alloc of size %d failed\n", block->size);
	  remove_block(block);
	}
      // if start == NULL on realloc set block to free to keep previous access to data
      else if (!(block->flag & FREE))
	{
	  dr_printf("Realloc of size %d on %p failed\n", block->size, block->start);
	  block->flag |= FREE;
	}
    }
  else if (block)
    {
      block->start = start;
      block->end = block->start + block->size;
      block->flag = flag;
    }
  else
    dr_printf("Error : *alloc post wrapping call without pre wrapping\n");
}

static void post_malloc(void *wrapctx, void *user_data)
{
  malloc_t	*block = (malloc_t *)user_data;
  
  dr_mutex_lock(lock);

  if (block)
    set_addr_malloc(block, drwrap_get_retval(wrapctx), ALLOC, 0);

  dr_mutex_unlock(lock);
}

static void pre_realloc(void *wrapctx, OUT void **user_data)
{
  malloc_t	*block;
  realloc_tmp_t *tmp = NULL;
  void		*start = drwrap_get_arg(wrapctx, 0);
  size_t	size = (size_t)drwrap_get_arg(wrapctx, 1);

  if (first) // ugly fix to the double call of first *alloc
    {
      first = 0;
      return;
    }

  // if size == 0 => realloc call free
  if (!size)
    return;

  dr_mutex_lock(lock);
  if (!(tmp = dr_global_alloc(sizeof(realloc_tmp_t))))
    {
      dr_printf("dr_malloc fail\n");
      return;
    }

  // is realloc is use like a malloc save the to set it on the post wrapping
  tmp->size = size;
  *user_data = tmp;

  // if start == 0 => realloc call malloc
  if (!start)
    {
      tmp->block = NULL;
      dr_mutex_unlock(lock);
      return;
    }

  if (!(block = get_block_by_addr(start)))
    dr_printf("Realloc on %p error : addr was not previously malloc\n", start);
  else
    block->size = size;
  tmp->block = block;
  
  dr_mutex_unlock(lock);
}

static void post_realloc(void *wrapctx, void *user_data)
{
  malloc_t	*block;
  module_data_t *m_data;
  void		*ret = drwrap_get_retval(wrapctx);

  dr_mutex_lock(lock);

  // if user_data is not set realloc was called to do a free
  if (user_data)
    {
      if (((realloc_tmp_t *)user_data)->block)
	set_addr_malloc(((realloc_tmp_t *)user_data)->block, ret, ((realloc_tmp_t *)user_data)->block->flag, 1);
      // if realloc is use like a malloc set the size (malloc wrapper receive a null size)
      else if ((block = get_block_by_addr(ret)))
	block->size = ((realloc_tmp_t*)user_data)->size;
      dr_global_free(user_data, sizeof(realloc_tmp_t));
    }

  dr_mutex_unlock(lock);
}

static void pre_free(void *wrapctx, OUT void **user_data)
{
  malloc_t	*block;
  module_data_t	*m_data;

  // free(0) du nothing
  if (!drwrap_get_arg(wrapctx,0))
    return;

  dr_mutex_lock(lock);

  block = get_block_by_addr(drwrap_get_arg(wrapctx, 0));

  // if the block was previously malloc we set it to free
  if (block)
    block->flag |= FREE;
  else
    dr_printf("free of non alloc adress : %p\n", drwrap_get_arg(wrapctx, 0));

  dr_mutex_unlock(lock);
}

static void load_event(void *drcontext, const module_data_t *mod, bool loaded)
{
  app_pc	malloc = (app_pc)dr_get_proc_address(mod->handle, "malloc");
  app_pc	calloc = (app_pc)dr_get_proc_address(mod->handle, "calloc");
  app_pc	realloc = (app_pc)dr_get_proc_address(mod->handle, "realloc");
  app_pc	free = (app_pc)dr_get_proc_address(mod->handle, "free");

  // blacklist ld-linux to see only his internal alloc
  if (!strncmp("ld-linux", dr_module_preferred_name(mod), 8))
    return ;

  // wrap malloc
  if (malloc)
    {
      dr_printf("malloc found at %p in %s\n", malloc, dr_module_preferred_name(mod));
      if (drwrap_wrap(malloc, pre_malloc, post_malloc))
	dr_printf("\tWrap sucess\n");
      else
	dr_printf("\tWrap fail\n");
    }
  else
    dr_printf("Malloc not found in %s\n", dr_module_preferred_name(mod));

  // wrap realloc
  if (realloc)
    {
      dr_printf("realloc found at %p in %s\n", malloc, dr_module_preferred_name(mod));
      if (drwrap_wrap(realloc, pre_realloc, post_realloc))
  	dr_printf("\tWrap sucess\n");
      else
  	dr_printf("\tWrap fail\n");
    }
  else
    dr_printf("realloc not found in %s\n", dr_module_preferred_name(mod));

  // wrap free
  if (free)
    {
      dr_printf("free found at %p in %s\n", malloc, dr_module_preferred_name(mod));
      if (drwrap_wrap(free, pre_free, NULL))
	dr_printf("\tWrap sucess\n");
      else
	dr_printf("\tWrap fail\n");
    }
  else
    dr_printf("free not found in %s\n", dr_module_preferred_name(mod));
}

static void exit_event(void)
{
  malloc_t	*block = blocks;
  malloc_t	*tmp;

  dr_mutex_lock(lock);

  while (block)
    {
      tmp = block->next;
      dr_printf("%p-%p(0x%x) ", block->start, block->end, block->size);
      if (block->flag & FREE)
	dr_printf(" => free\n");
      else
	dr_printf("=> not free\n");
      free_malloc_block(block);
      block = tmp;
    }
  blocks = NULL;
  
  dr_mutex_unlock(lock);
  dr_mutex_destroy(lock);
  
  drwrap_exit();
  drmgr_exit();
}

DR_EXPORT void dr_init(client_id_t id)
{
  drwrap_init();
  drmgr_init();

  // todo check for fail
  dr_register_exit_event(exit_event); 
  dr_register_module_load_event(load_event);

  lock = dr_mutex_create();  
}
