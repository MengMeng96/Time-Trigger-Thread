/*
 * Copyright (c) 2006-2018, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2006-03-17     Bernard      the first version
 * 2006-04-28     Bernard      fix the scheduler algorthm
 * 2006-04-30     Bernard      add SCHEDULER_DEBUG
 * 2006-05-27     Bernard      fix the scheduler algorthm for same priority
 *                             thread schedule
 * 2006-06-04     Bernard      rewrite the scheduler algorithm
 * 2006-08-03     Bernard      add hook support
 * 2006-09-05     Bernard      add 32 priority level support
 * 2006-09-24     Bernard      add rt_system_scheduler_start function
 * 2009-09-16     Bernard      fix _rt_scheduler_stack_check
 * 2010-04-11     yi.qiu       add module feature
 * 2010-07-13     Bernard      fix the maximal number of rt_scheduler_lock_nest
 *                             issue found by kuronca
 * 2010-12-13     Bernard      add defunct list initialization even if not use heap.
 * 2011-05-10     Bernard      clean scheduler debug log.
 * 2013-12-21     Grissiom     add rt_critical_level
 */

#include <rtthread.h>
#include <rthw.h>

/* 使用rt_TT_thread_list存储TT线程，数组大小就是跳表的层数 */
rt_list_t rt_TT_thread_list[RT_TT_THREAD_SKIP_LIST_LEVEL];
/* 存储所有已经创建了，但是可能没有运行的TT线程 */
rt_list_t rt_created_TT_thread_list;
/* 记录当前创建了，并且运行了的TT线程的数量（与只创建了，但是没有运行区别开来） */
rt_uint32_t rt_running_TT_Thread_count;
rt_uint32_t get_running_TT_Thread_count(void){
    return rt_running_TT_Thread_count;
}
void set_running_TT_Thread_count(rt_uint32_t parameter){
    rt_running_TT_Thread_count = parameter;
}

/* 存储即将开始的TT线程的开始时间 */
rt_uint32_t rt_first_TT_Thread_start_time;
rt_uint32_t get_first_TT_Thread_start_time(void){
    return rt_first_TT_Thread_start_time;
}
/* 存储TT线程开始时间，也就是所有TT线程的0时刻，用来对齐不同设备的时钟 */
rt_uint32_t rt_TT_thread_start_time;
rt_uint32_t get_TT_thread_start_time(void){
    return rt_TT_thread_start_time;
}
void set_TT_thread_start_time(rt_uint32_t parameter){
    rt_TT_thread_start_time = parameter;
}

static rt_int16_t rt_scheduler_lock_nest;
extern volatile rt_uint8_t rt_interrupt_nest;

rt_list_t rt_thread_priority_table[RT_THREAD_PRIORITY_MAX];
struct rt_thread *rt_current_thread;

rt_uint8_t rt_current_priority;

#if RT_THREAD_PRIORITY_MAX > 32
/* Maximum priority level, 256 */
rt_uint32_t rt_thread_ready_priority_group;
rt_uint8_t rt_thread_ready_table[32];
#else
/* Maximum priority level, 32 */
rt_uint32_t rt_thread_ready_priority_group;
#endif

rt_list_t rt_thread_defunct;

#ifdef RT_USING_HOOK
static void (*rt_scheduler_hook)(struct rt_thread *from, struct rt_thread *to);

/**
 * @addtogroup Hook
 */

/**@{*/

/**
 * This function will set a hook function, which will be invoked when thread
 * switch happens.
 *
 * @param hook the hook function
 */
void
rt_scheduler_sethook(void (*hook)(struct rt_thread *from, struct rt_thread *to))
{
    rt_scheduler_hook = hook;
}

/**@}*/
#endif

#ifdef RT_USING_OVERFLOW_CHECK
static void _rt_scheduler_stack_check(struct rt_thread *thread)
{
    RT_ASSERT(thread != RT_NULL);

#if defined(ARCH_CPU_STACK_GROWS_UPWARD)
  if (*((rt_uint8_t *)((rt_ubase_t)thread->stack_addr + thread->stack_size - 1)) != '#' ||
#else
    if (*((rt_uint8_t *)thread->stack_addr) != '#' ||
#endif
        (rt_uint32_t)thread->sp <= (rt_uint32_t)thread->stack_addr ||
        (rt_uint32_t)thread->sp >
        (rt_uint32_t)thread->stack_addr + (rt_uint32_t)thread->stack_size)
    {
        rt_uint32_t level;

        rt_kprintf("thread:%s stack overflow\n", thread->name);
#ifdef RT_USING_FINSH
        {
            extern long list_thread(void);
            list_thread();
        }
#endif
        level = rt_hw_interrupt_disable();
        while (level);
    }
#if defined(ARCH_CPU_STACK_GROWS_UPWARD)
    else if ((rt_uint32_t)thread->sp > ((rt_uint32_t)thread->stack_addr + thread->stack_size))
    {
        rt_kprintf("warning: %s stack is close to the top of stack address.\n",
                   thread->name);
    }
#else
    else if ((rt_uint32_t)thread->sp <= ((rt_uint32_t)thread->stack_addr + 32))
    {
        rt_kprintf("warning: %s stack is close to the bottom of stack address.\n",
                   thread->name);
    }
#endif
}
#endif

/**
 * @ingroup SystemInit
 * This function will initialize the system scheduler
 */
void rt_system_scheduler_init(void)
{
    register rt_base_t offset;

    rt_scheduler_lock_nest = 0;

    RT_DEBUG_LOG(RT_DEBUG_SCHEDULER, ("start scheduler: max priority 0x%02x\n",
                                      RT_THREAD_PRIORITY_MAX));

    for (offset = 0; offset < RT_THREAD_PRIORITY_MAX; offset ++)
    {
        rt_list_init(&rt_thread_priority_table[offset]);
    }

    rt_current_priority = RT_THREAD_PRIORITY_MAX - 1;
    rt_current_thread = RT_NULL;

    /* initialize ready priority group */
    rt_thread_ready_priority_group = 0;

#if RT_THREAD_PRIORITY_MAX > 32
    /* initialize ready table */
    rt_memset(rt_thread_ready_table, 0, sizeof(rt_thread_ready_table));
#endif

    /* initialize thread defunct */
    rt_list_init(&rt_thread_defunct);
    
    /* 初始化rt_TTThread_count和rt_first_TTThread_start_time */
    rt_running_TT_Thread_count = 0;
    rt_first_TT_Thread_start_time = 0;
    
    /* 初始化rt_TT_thread_list */
    for(offset = 0; offset < RT_TT_THREAD_SKIP_LIST_LEVEL; offset++)
    {
        rt_list_init(&rt_TT_thread_list[offset]);
    }
    /* 初始化rt_created_TT_thread_list */
    rt_list_init(&rt_created_TT_thread_list);
    /* 默认开始时刻是0，用户可以通过set函数设置开始时刻 */
    rt_TT_thread_start_time = 0;
}

/**
 * @ingroup SystemInit
 * This function will startup scheduler. It will select one thread
 * with the highest priority level, then switch to it.
 */
void rt_system_scheduler_start(void)
{
    register struct rt_thread *to_thread;
    register rt_ubase_t highest_ready_priority;

#if RT_THREAD_PRIORITY_MAX > 32
    register rt_ubase_t number;

    number = __rt_ffs(rt_thread_ready_priority_group) - 1;
    highest_ready_priority = (number << 3) + __rt_ffs(rt_thread_ready_table[number]) - 1;
#else
    highest_ready_priority = __rt_ffs(rt_thread_ready_priority_group) - 1;
#endif

    /* get switch to thread */
    to_thread = rt_list_entry(rt_thread_priority_table[highest_ready_priority].next,
                              struct rt_thread,
                              tlist);

    rt_current_thread = to_thread;

    /* switch to new thread */
    rt_hw_context_switch_to((rt_uint32_t)&to_thread->sp);

    /* never come back */
}

/**
 * @addtogroup Thread
 */

/**@{*/

/**
 * This function will perform one schedule. It will select one thread
 * with the highest priority level, then switch to it.
 */
void rt_schedule(void)
{
    rt_base_t level;
    struct rt_thread *to_thread;
    struct rt_thread *from_thread;

    /* disable interrupt */
    level = rt_hw_interrupt_disable();

    /* check the scheduler is enabled or not */
    if (rt_scheduler_lock_nest == 0)
    {
        register rt_ubase_t highest_ready_priority;
        /* 如果当前线程是TT线程，且时间片没有用完
         * 说明当前TT线程还没有执行完，不进行线程切换
         * 如果TT线程主动挂起或者终止运行，时间片一定会被置为0 */
        if(rt_current_thread->rt_is_TT_Thread)
        {
            if(rt_current_thread->remaining_tick)
            {
                rt_hw_interrupt_enable(level);
                return;
            }
            else
            {
                rt_current_thread->remaining_tick = rt_current_thread->init_tick;
            }
        }
        if(rt_running_TT_Thread_count && rt_first_TT_Thread_start_time <= rt_get_global_time())
        {
          /* 如果有TT线程，且首个TT线程已经到了执行时间
           * 如果确定要执行TT线程，那么一定是取得rt_TT_thread_list[RT_TT_THREAD_SKIP_LIST_LEVEL - 1]的第一个元素 */
            to_thread = rt_list_entry(rt_TT_thread_list[RT_TT_THREAD_SKIP_LIST_LEVEL - 1].next,
                                  struct rt_thread,
                                  tlist);
        }
        else
        {
          /* 找到目前所有线程里最高的优先级，这里默认一定会找到至少一个线程，即优先级不会是-1 */
          /* -1是因为_rt_ffs函数+1了 */
#if RT_THREAD_PRIORITY_MAX <= 32
            highest_ready_priority = __rt_ffs(rt_thread_ready_priority_group) - 1;
#else
        register rt_ubase_t number;

        number = __rt_ffs(rt_thread_ready_priority_group) - 1;
        highest_ready_priority = (number << 3) + __rt_ffs(rt_thread_ready_table[number]) - 1;
#endif

          /* get switch to thread */
          /* 这里的rt_thread_priority_table是rt_list_t的数组，一共有32个元素，每个元素是一个链表，存储了这一个优先级的全部线程*/
          /* 这个链表有头结点，也就是第一个节点是无意义的 */
            to_thread = rt_list_entry(rt_thread_priority_table[highest_ready_priority].next,
                                  struct rt_thread,
                                  tlist);
        }
          /* 当前线程不一定处于就绪状态
           * 原因是，正在运行的线程，可能处于挂起状态，这是不允许进行线程切换
           * 但是TT线程的运行不能有任务阻挡，因此可能会有线程在挂起状态被挂起
           * 重新运行的时候要设置为就绪状态*/  
        to_thread->stat = RT_THREAD_READY | (to_thread->stat & ~RT_THREAD_STAT_MASK);
        
        /* if the destination thread is not the same as current thread */
        if (to_thread != rt_current_thread)
        {
            rt_current_priority = (rt_uint8_t)highest_ready_priority;
            from_thread         = rt_current_thread;
            rt_current_thread   = to_thread;

            RT_OBJECT_HOOK_CALL(rt_scheduler_hook, (from_thread, to_thread));

            /* switch to new thread */
            RT_DEBUG_LOG(RT_DEBUG_SCHEDULER,
                         ("[%d]switch to priority#%d "
                          "thread:%.*s(sp:0x%p), "
                          "from thread:%.*s(sp: 0x%p)\n",
                          rt_interrupt_nest, highest_ready_priority,
                          RT_NAME_MAX, to_thread->name, to_thread->sp,
                          RT_NAME_MAX, from_thread->name, from_thread->sp));

#ifdef RT_USING_OVERFLOW_CHECK
            _rt_scheduler_stack_check(to_thread);
#endif

            if (rt_interrupt_nest == 0)
            {
                rt_hw_context_switch((rt_uint32_t)&from_thread->sp,
                                     (rt_uint32_t)&to_thread->sp);

#ifdef RT_USING_SIGNALS
                if (rt_current_thread->stat & RT_THREAD_STAT_SIGNAL_PENDING)
                {
                    extern void rt_thread_handle_sig(rt_bool_t clean_state);

                    rt_current_thread->stat &= ~RT_THREAD_STAT_SIGNAL_PENDING;

                    rt_hw_interrupt_enable(level);

                    /* check signal status */
                    rt_thread_handle_sig(RT_TRUE);
                }
                else
#endif
                {
                    /* enable interrupt */
                    rt_hw_interrupt_enable(level);
                }

                return ;
            }
            else
            {
                RT_DEBUG_LOG(RT_DEBUG_SCHEDULER, ("switch in interrupt\n"));

                rt_hw_context_switch_interrupt((rt_uint32_t)&from_thread->sp,
                                               (rt_uint32_t)&to_thread->sp);
            }
        }
    }

    /* enable interrupt */
    rt_hw_interrupt_enable(level);
}

/*
 * This function will insert a thread to system ready queue. The state of
 * thread will be set as READY and remove from suspend queue.
 *
 * @param thread the thread to be inserted
 * @note Please do not invoke this function in user application.
 */
void rt_schedule_insert_thread(struct rt_thread *thread)
{
    register rt_base_t temp;
    RT_ASSERT(thread != RT_NULL);

    /* disable interrupt */
    temp = rt_hw_interrupt_disable();

    /* change stat */
    thread->stat = RT_THREAD_READY | (thread->stat & ~RT_THREAD_STAT_MASK);


    if(thread->rt_is_TT_Thread)
    {
        // 尝试让TT线程的添加可以被中断，因为实验现象表明，可能是这里导致触发时间不准确
        //rt_hw_interrupt_enable(temp);
        rt_list_TT_thread_insert(thread);

        //return;
    }
    else
    {
        /* insert thread to ready list */
        /* rt_list_insert_before(rt_list_t *l, rt_list_t *n)，n在l前面 */
        /* 新加入的节点在头结点的前面，这是个双向链表，也就是说，新节点在链表的最后 */
        rt_list_insert_before(&(rt_thread_priority_table[thread->current_priority]),
                          &(thread->tlist));

        /* set priority mask */
#if     RT_THREAD_PRIORITY_MAX <= 32
        RT_DEBUG_LOG(RT_DEBUG_SCHEDULER, ("insert thread[%.*s], the priority: %d\n",
                                      RT_NAME_MAX, thread->name, thread->current_priority));
#else
        RT_DEBUG_LOG(RT_DEBUG_SCHEDULER,
                 ("insert thread[%.*s], the priority: %d 0x%x %d\n",
                  RT_NAME_MAX,
                  thread->name,
                  thread->number,
                  thread->number_mask,
                  thread->high_mask));
#endif

#if RT_THREAD_PRIORITY_MAX > 32
        rt_thread_ready_table[thread->number] |= thread->high_mask;
#endif
        rt_thread_ready_priority_group |= thread->number_mask;
    }
    
    RT_ASSERT((thread->stat & RT_THREAD_STAT_MASK) == RT_THREAD_READY);

    /* enable interrupt */
    rt_hw_interrupt_enable(temp);
}

/*
 * This function will remove a thread from system ready queue.
 *
 * @param thread the thread to be removed
 *
 * @note Please do not invoke this function in user application.
 */
void rt_schedule_remove_thread(struct rt_thread *thread)
{
    register rt_base_t temp;

    RT_ASSERT(thread != RT_NULL);

    /* disable interrupt */
    temp = rt_hw_interrupt_disable();

#if RT_THREAD_PRIORITY_MAX <= 32
    RT_DEBUG_LOG(RT_DEBUG_SCHEDULER, ("remove thread[%.*s], the priority: %d\n",
                                      RT_NAME_MAX, thread->name,
                                      thread->current_priority));
#else
    RT_DEBUG_LOG(RT_DEBUG_SCHEDULER,
                 ("remove thread[%.*s], the priority: %d 0x%x %d\n",
                  RT_NAME_MAX,
                  thread->name,
                  thread->number,
                  thread->number_mask,
                  thread->high_mask));
#endif

    if(thread->rt_is_TT_Thread)
    {
        /* remove thread from ready list */
        rt_base_t i;
        for(i = 0; i < RT_TT_THREAD_SKIP_LIST_LEVEL; i++)
        {
            rt_list_remove(thread->thread_skip_list_nodes[i]);
        }
        
        /* 将时间片信息置为0，意为这个TT线程在本周期内已经运行结束 */
        thread->remaining_tick = 0;
        
        /* 而且每一次插入和删除的时候都要维护rt_first_TTThread_start_time变量，也就是接下来最早的TT线程的开始时间 */
        struct rt_thread *first_TT_thread;
        first_TT_thread = rt_list_entry(rt_TT_thread_list[RT_TT_THREAD_SKIP_LIST_LEVEL - 1].next,
                                  struct rt_thread,
                                  tlist);
        rt_first_TT_Thread_start_time = first_TT_thread->thread_start_time;
        //rt_running_TT_Thread_count--;
    }
    else
    {
        /* remove thread from ready list */
        rt_list_remove(&(thread->tlist));
        if (rt_list_isempty(&(rt_thread_priority_table[thread->current_priority])))
        {
#if     RT_THREAD_PRIORITY_MAX > 32
            rt_thread_ready_table[thread->number] &= ~thread->high_mask;
            if (rt_thread_ready_table[thread->number] == 0)
            {
                rt_thread_ready_priority_group &= ~thread->number_mask;
            }
#else
            rt_thread_ready_priority_group &= ~thread->number_mask;
#endif
        }
    }

    /* enable interrupt */
    rt_hw_interrupt_enable(temp);
}

/**
 * This function will lock the thread scheduler.
 */
void rt_enter_critical(void)
{
    register rt_base_t level;

    /* disable interrupt */
    level = rt_hw_interrupt_disable();

    /*
     * the maximal number of nest is RT_UINT16_MAX, which is big
     * enough and does not check here
     */
    rt_scheduler_lock_nest ++;

    /* enable interrupt */
    rt_hw_interrupt_enable(level);
}
RTM_EXPORT(rt_enter_critical);

/**
 * This function will unlock the thread scheduler.
 */
void rt_exit_critical(void)
{
    register rt_base_t level;

    /* disable interrupt */
    level = rt_hw_interrupt_disable();

    rt_scheduler_lock_nest --;
    if (rt_scheduler_lock_nest <= 0)
    {
        rt_scheduler_lock_nest = 0;
        /* enable interrupt */
        rt_hw_interrupt_enable(level);

        if (rt_current_thread)
        {
            /* if scheduler is started, do a schedule */
            rt_schedule();
        }
    }
    else
    {
        /* enable interrupt */
        rt_hw_interrupt_enable(level);
    }
}
RTM_EXPORT(rt_exit_critical);

/**
 * Get the scheduler lock level
 *
 * @return the level of the scheduler lock. 0 means unlocked.
 */
rt_uint16_t rt_critical_level(void)
{
    return rt_scheduler_lock_nest;
}
RTM_EXPORT(rt_critical_level);


/*
这个方法用来添加TT线程到链表
封装在rt_schedule_insert_TT_thread()里面使用时是首次添加TT线程
单独使用时是在一个周期的执行结束之后，再次添加到链表当中
*/
void rt_list_TT_thread_insert(struct rt_thread *TT_thread)
{
    /* 加入到线程链表里面的TT线程，执行时间可能已经过了
      * 这时就记录一下，并且将执行时间延迟一个周期*/
    while(TT_thread->thread_start_time < rt_get_global_time())
    {
        TT_thread->thread_start_time += TT_thread->thread_exec_cycle;
    }
  
    /* 把TT线程加入TT_thread_list中，使用跳表结构 */
    rt_list_t *TT_thread_list = rt_TT_thread_list;
    rt_list_t *row_head[RT_TT_THREAD_SKIP_LIST_LEVEL];
    rt_uint32_t tst_nr;
    row_head[0]  = TT_thread_list[0].next;
    rt_uint32_t row_lvl;
    for (row_lvl = 0; row_lvl < RT_TT_THREAD_SKIP_LIST_LEVEL; row_lvl++)
    {
        //找到当前层中，目标节点的插入位置
        for (; row_head[row_lvl] != &TT_thread_list[row_lvl];
             row_head[row_lvl]  = row_head[row_lvl]->next)
        {
            struct rt_thread *cur_TT_thread;
#if RT_TT_THREAD_SKIP_LIST_LEVEL > 1
            if(row_lvl < RT_TT_THREAD_SKIP_LIST_LEVEL - 1)
            {
                cur_TT_thread = rt_list_entry(row_head[row_lvl], struct rt_thread, row[row_lvl]);
            }
            else
                cur_TT_thread = rt_list_entry(row_head[row_lvl], struct rt_thread, tlist);
#else
            cur_TT_thread = rt_list_entry(row_head[row_lvl], struct rt_thread, tlist);
#endif
            
            if (cur_TT_thread->thread_start_time > TT_thread->thread_start_time)
            {
                break;
            }
        }
        //移动到下一层的相应位置
        row_head[row_lvl] = row_head[row_lvl]->prev;
#if RT_TT_THREAD_SKIP_LIST_LEVEL > 1
        if (row_lvl < RT_TT_THREAD_SKIP_LIST_LEVEL - 1)
        {
            /* 这里可以+1的原因不是链表的特性，而是因为每一个链表节点原本都是数组的其中一个元素
             * +1就可以到数组中的下一个元素，而下一个元素已经维护好了，一定在链表下一层的正确位置
             */
            if(row_head[row_lvl] == &TT_thread_list[row_lvl])
            {
                row_head[row_lvl + 1] = TT_thread_list[row_lvl + 1].next;
            }
            else
            {
                struct rt_thread *cur_TT_thread;

                if(row_lvl < RT_TT_THREAD_SKIP_LIST_LEVEL - 2)
                {
                    row_head[row_lvl + 1] = row_head[row_lvl] + 1;
                }
                else
                {
                    cur_TT_thread = rt_list_entry(row_head[row_lvl], struct rt_thread, row[row_lvl]);
                    row_head[row_lvl + 1] = cur_TT_thread->thread_skip_list_nodes[row_lvl + 1];
                }
            }
        }
#endif
    }
    tst_nr = rand();
    //将目标节点加入最底层的指定位置
    rt_list_insert_after(row_head[RT_TT_THREAD_SKIP_LIST_LEVEL - 1],
                         TT_thread->thread_skip_list_nodes[RT_TT_THREAD_SKIP_LIST_LEVEL - 1]);
    for (row_lvl = 2; row_lvl <= RT_TT_THREAD_SKIP_LIST_LEVEL; row_lvl++)
    {
        /* 加到链表里面的，是一个链表节点数组的其中一个，在这个节点上+1，就可以到数组下一个元素，也就是下一层的链表节点 
         * 从倒数第二层开始往上，如果当前节点需要作为框架节点加入非底层的层，那么就将节点的对应层加到链表的指定层上
         * 这里的是否需要加入是随机的，找一个随机数，如果指定的二进制位为1，那么就加入，否则直接退出
         * 退出的时候，这个节点一定加入了最底层，有可能加入了上层，不可能跳过一层
         * 也就是说，一定是某一层往下的全部层都加入了该节点
        */
        //if (!(tst_nr & RT_TIMER_SKIP_LIST_MASK))
        //将概率从0.25上调至0.5
        if ((tst_nr & RT_TIMER_SKIP_LIST_MASK) > 1)
            rt_list_insert_after(row_head[RT_TT_THREAD_SKIP_LIST_LEVEL - row_lvl],
                                 TT_thread->thread_skip_list_nodes[RT_TT_THREAD_SKIP_LIST_LEVEL - row_lvl]);
        else
            break;
        /* Shift over the bits we have tested. Works well with 1 bit and 2
         * bits. */
        tst_nr >>= (RT_TIMER_SKIP_LIST_MASK + 1) >> 1;
    }
    
    /* 而且每一次插入和删除的时候都要维护rt_first_TTThread_start_time变量，也就是接下来最早的TT线程的开始时间 */
    struct rt_thread *first_TT_thread;
    first_TT_thread = rt_list_entry(rt_TT_thread_list[RT_TT_THREAD_SKIP_LIST_LEVEL - 1].next,
                                  struct rt_thread,
                                  tlist);
    rt_first_TT_Thread_start_time = first_TT_thread->thread_start_time;
}

/**@}*/

