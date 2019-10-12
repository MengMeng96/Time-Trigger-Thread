/*
 * Copyright (c) 2006-2018, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2006-03-28     Bernard      first version
 * 2006-04-29     Bernard      implement thread timer
 * 2006-04-30     Bernard      added THREAD_DEBUG
 * 2006-05-27     Bernard      fixed the rt_thread_yield bug
 * 2006-06-03     Bernard      fixed the thread timer init bug
 * 2006-08-10     Bernard      fixed the timer bug in thread_sleep
 * 2006-09-03     Bernard      changed rt_timer_delete to rt_timer_detach
 * 2006-09-03     Bernard      implement rt_thread_detach
 * 2008-02-16     Bernard      fixed the rt_thread_timeout bug
 * 2010-03-21     Bernard      change the errno of rt_thread_delay/sleep to
 *                             RT_EOK.
 * 2010-11-10     Bernard      add cleanup callback function in thread exit.
 * 2011-09-01     Bernard      fixed rt_thread_exit issue when the current
 *                             thread preempted, which reported by Jiaxing Lee.
 * 2011-09-08     Bernard      fixed the scheduling issue in rt_thread_startup.
 * 2012-12-29     Bernard      fixed compiling warning.
 * 2016-08-09     ArdaFu       add thread suspend and resume hook.
 * 2017-04-10     armink       fixed the rt_thread_delete and rt_thread_detach
                               bug when thread has not startup.
 */

#include <rtthread.h>
#include <rthw.h>

extern rt_list_t rt_TT_thread_list[RT_TT_THREAD_SKIP_LIST_LEVEL];
/* �洢�����Ѿ������ˣ����ǿ���û�����е�TT�߳� */
extern rt_list_t rt_created_TT_thread_list;
static void (*TT_thread_timeout_hook_list[RT_TT_THREAD_TIMEOUT_HOOK_LIST_SIZE])();

extern rt_list_t rt_thread_priority_table[RT_THREAD_PRIORITY_MAX];
extern struct rt_thread *rt_current_thread;
extern rt_list_t rt_thread_defunct;

#ifdef RT_USING_HOOK

static void (*rt_thread_suspend_hook)(rt_thread_t thread);
static void (*rt_thread_resume_hook) (rt_thread_t thread);
static void (*rt_thread_inited_hook) (rt_thread_t thread);

/**
 * @ingroup Hook
 * This function sets a hook function when the system suspend a thread.
 *
 * @param hook the specified hook function
 *
 * @note the hook function must be simple and never be blocked or suspend.
 */
void rt_thread_suspend_sethook(void (*hook)(rt_thread_t thread))
{
    rt_thread_suspend_hook = hook;
}

/**
 * @ingroup Hook
 * This function sets a hook function when the system resume a thread.
 *
 * @param hook the specified hook function
 *
 * @note the hook function must be simple and never be blocked or suspend.
 */
void rt_thread_resume_sethook(void (*hook)(rt_thread_t thread))
{
    rt_thread_resume_hook = hook;
}

/**
 * @ingroup Hook
 * This function sets a hook function when a thread is initialized.
 *
 * @param hook the specified hook function
 */
void rt_thread_inited_sethook(void (*hook)(rt_thread_t thread))
{
    rt_thread_inited_hook = hook;
}

#endif

void rt_thread_exit(void)
{
    struct rt_thread *thread;
    register rt_base_t level;

    /* get current thread */
    thread = rt_current_thread;

    /* disable interrupt */
    level = rt_hw_interrupt_disable();

    /* remove from schedule */
    rt_schedule_remove_thread(thread);
    /* change stat */
    thread->stat = RT_THREAD_CLOSE;

    /* remove it from timer list */
    rt_timer_detach(&thread->thread_timer);

    if(thread->rt_is_TT_Thread)
    {
        /* ��ʱ��Ƭ����Ϊ0����־��ǰTT�߳��Ѿ����������ڵ����У�����Ӱ�������߳����� */
        thread->remaining_tick = 0;
        /* �Ƴ����ڳ�ͻ��������ڵ�
         * BE�߳�����Ҳ������ڵ㣬����û��ʹ�ã��Ƴ����߲��Ƴ����� */
        rt_list_remove(&(thread->time_collision_list));
      
        /* insert to defunct thread list */
        set_running_TT_Thread_count(get_running_TT_Thread_count() - 1);
        rt_list_insert_after(&rt_thread_defunct, &(thread->tlist));
    }
    else
    {
        if ((rt_object_is_systemobject((rt_object_t)thread) == RT_TRUE) &&
            thread->cleanup == RT_NULL)
        {
            rt_object_detach((rt_object_t)thread);
        }
        else
        {
            /* insert to defunct thread list */
            rt_list_insert_after(&rt_thread_defunct, &(thread->tlist));
        }
    }

    /* enable interrupt */
    rt_hw_interrupt_enable(level);

    /* switch to next task */
    rt_schedule();
}

static rt_err_t _rt_thread_init(struct rt_thread *thread,
                                const char       *name,
                                void (*entry)(void *parameter),
                                void             *parameter,
                                void             *stack_start,
                                rt_uint32_t       stack_size,
                                rt_uint8_t        priority,
                                rt_uint32_t       tick)
{
    /* init thread list */
    rt_list_init(&(thread->tlist));

    thread->entry = (void *)entry;
    thread->parameter = parameter;

    /* stack init */
    thread->stack_addr = stack_start;
    thread->stack_size = stack_size;

    /* init thread stack */
    rt_memset(thread->stack_addr, '#', thread->stack_size);
#ifdef ARCH_CPU_STACK_GROWS_UPWARD
    thread->sp = (void *)rt_hw_stack_init(thread->entry, thread->parameter,
                                          (void *)((char *)thread->stack_addr),
                                          (void *)rt_thread_exit);
#else
    thread->sp = (void *)rt_hw_stack_init(thread->entry, thread->parameter,
                                          (void *)((char *)thread->stack_addr + thread->stack_size - 4),
                                          (void *)rt_thread_exit);
#endif

    /* priority init */
    RT_ASSERT(priority <= RT_THREAD_PRIORITY_MAX);
    thread->init_priority    = priority;
    thread->current_priority = priority;

    thread->number_mask = 0;
#if RT_THREAD_PRIORITY_MAX > 32
    thread->number = 0;
    thread->high_mask = 0;
#endif

    /* tick init */
    thread->init_tick      = tick;
    thread->remaining_tick = tick;

    /* error and flags */
    thread->error = RT_EOK;
    thread->stat  = RT_THREAD_INIT;

    /* initialize cleanup function and user data */
    thread->cleanup   = 0;
    thread->user_data = 0;

    /* init thread timer */
    rt_timer_init(&(thread->thread_timer),
                  thread->name,
                  rt_thread_timeout,
                  thread,
                  0,
                  RT_TIMER_FLAG_ONE_SHOT);

    /* initialize signal */
#ifdef RT_USING_SIGNALS
    thread->sig_mask    = 0x00;
    thread->sig_pending = 0x00;

    thread->sig_ret     = RT_NULL;
    thread->sig_vectors = RT_NULL;
    thread->si_list     = RT_NULL;
#endif

#ifdef RT_USING_LWP
    thread->lwp = RT_NULL;
#endif

    RT_OBJECT_HOOK_CALL(rt_thread_inited_hook, (thread));
    
    /* Ĭ�������̶߳�����ͨ�߳� */
    thread->rt_is_TT_Thread = 0;
    
    thread->thread_skip_list_nodes[RT_TT_THREAD_SKIP_LIST_LEVEL - 1] = &thread->tlist;
#if RT_TT_THREAD_SKIP_LIST_LEVEL > 1
    rt_base_t i;
    for(i = 0; i < RT_TT_THREAD_SKIP_LIST_LEVEL - 1; i++)
    {
        rt_list_init(&(thread->row[i]));
        thread->thread_skip_list_nodes[i] = &(thread->row[i]);
    }
#endif
    /* ��ʼ��time_collision_list */
    rt_list_init(&(thread->time_collision_list));

    return RT_EOK;
}

/**
 * @addtogroup Thread
 */

/**@{*/

/**
 * This function will initialize a thread, normally it's used to initialize a
 * static thread object.
 *
 * @param thread the static thread object
 * @param name the name of thread, which shall be unique
 * @param entry the entry function of thread
 * @param parameter the parameter of thread enter function
 * @param stack_start the start address of thread stack
 * @param stack_size the size of thread stack
 * @param priority the priority of thread
 * @param tick the time slice if there are same priority thread
 *
 * @return the operation status, RT_EOK on OK, -RT_ERROR on error
 */
rt_err_t rt_thread_init(struct rt_thread *thread,
                        const char       *name,
                        void (*entry)(void *parameter),
                        void             *parameter,
                        void             *stack_start,
                        rt_uint32_t       stack_size,
                        rt_uint8_t        priority,
                        rt_uint32_t       tick)
{
    /* thread check */
    RT_ASSERT(thread != RT_NULL);
    RT_ASSERT(stack_start != RT_NULL);

    /* init thread object */
    rt_object_init((rt_object_t)thread, RT_Object_Class_Thread, name);

    return _rt_thread_init(thread,
                           name,
                           entry,
                           parameter,
                           stack_start,
                           stack_size,
                           priority,
                           tick);
}
RTM_EXPORT(rt_thread_init);

/**
 * This function will return self thread object
 *
 * @return the self thread object
 */
rt_thread_t rt_thread_self(void)
{
    return rt_current_thread;
}
RTM_EXPORT(rt_thread_self);

/**
 * This function will start a thread and put it to system ready queue
 *
 * @param thread the thread to be started
 *
 * @return the operation status, RT_EOK on OK, -RT_ERROR on error
 */
rt_err_t rt_thread_startup(rt_thread_t thread)
{
  
    /* thread check */
    RT_ASSERT(thread != RT_NULL);
    RT_ASSERT((thread->stat & RT_THREAD_STAT_MASK) == RT_THREAD_INIT);
    RT_ASSERT(rt_object_get_type((rt_object_t)thread) == RT_Object_Class_Thread);

    /* set current priority to init priority */
    thread->current_priority = thread->init_priority;

    /* calculate priority attribute */
    if(thread->rt_is_TT_Thread)
    {
        /* ������ȼ�����RT_THREAD_PRIORITY_MAX����32����ô��һ����һ��TT�߳� */
        RT_ASSERT(thread->current_priority == RT_THREAD_PRIORITY_MAX);
        /* ����ֻ��������thread��number_mask�ֶΣ�Ҳ���Ƕ����Ʊ�ʾ���߳����ȼ�������TT�̲߳���Ҫ����ֶΣ�rt_is_TT_Thread�Ѿ���ʾ��������ȼ� */
        set_running_TT_Thread_count(get_running_TT_Thread_count() + 1);
    }
    else
    {
#if RT_THREAD_PRIORITY_MAX > 32
        thread->number      = thread->current_priority >> 3;            /* 5bit */
        thread->number_mask = 1L << thread->number;
        thread->high_mask   = 1L << (thread->current_priority & 0x07);  /* 3bit */
#else
        thread->number_mask = 1L << thread->current_priority;
#endif
    }

    RT_DEBUG_LOG(RT_DEBUG_THREAD, ("startup a thread:%s with priority:%d\n",
                                   thread->name, thread->init_priority));
    /* change thread stat */
    thread->stat = RT_THREAD_SUSPEND;

    /* then resume it */
    rt_thread_resume(thread);
    // TT�߳̿��Ա�ʱ���ж��Զ�����������Ҫ�ڴ�����ʱ����е���
    if (rt_thread_self() != RT_NULL && !thread->rt_is_TT_Thread)
    {
        /* do a scheduling */
        rt_schedule();
    }

    return RT_EOK;
}
RTM_EXPORT(rt_thread_startup);

/**
 * This function will detach a thread. The thread object will be removed from
 * thread queue and detached/deleted from system object management.
 *
 * @param thread the thread to be deleted
 *
 * @return the operation status, RT_EOK on OK, -RT_ERROR on error
 */
rt_err_t rt_thread_detach(rt_thread_t thread)
{
    rt_base_t lock;

    /* thread check */
    RT_ASSERT(thread != RT_NULL);
    RT_ASSERT(rt_object_get_type((rt_object_t)thread) == RT_Object_Class_Thread);
    RT_ASSERT(rt_object_is_systemobject((rt_object_t)thread));

    if ((thread->stat & RT_THREAD_STAT_MASK) != RT_THREAD_INIT)
    {
        /* remove from schedule */
        rt_schedule_remove_thread(thread);
    }

    /* release thread timer */
    rt_timer_detach(&(thread->thread_timer));

    /* change stat */
    thread->stat = RT_THREAD_CLOSE;

    if ((rt_object_is_systemobject((rt_object_t)thread) == RT_TRUE) &&
        thread->cleanup == RT_NULL)
    {
        rt_object_detach((rt_object_t)thread);
    }
    else
    {
        /* disable interrupt */
        lock = rt_hw_interrupt_disable();
        /* insert to defunct thread list */
        rt_list_insert_after(&rt_thread_defunct, &(thread->tlist));
        /* enable interrupt */
        rt_hw_interrupt_enable(lock);
    }

    return RT_EOK;
}
RTM_EXPORT(rt_thread_detach);

#ifdef RT_USING_HEAP
/**
 * This function will create a thread object and allocate thread object memory
 * and stack.
 *
 * @param name the name of thread, which shall be unique
 * @param entry the entry function of thread
 * @param parameter the parameter of thread enter function
 * @param stack_size the size of thread stack
 * @param priority the priority of thread
 * @param tick the time slice if there are same priority thread
 *
 * @return the created thread object
 */
rt_thread_t rt_thread_create(const char *name,
                             void (*entry)(void *parameter),
                             void       *parameter,
                             rt_uint32_t stack_size,
                             rt_uint8_t  priority,
                             rt_uint32_t tick)
{
    struct rt_thread *thread;
    void *stack_start;

    thread = (struct rt_thread *)rt_object_allocate(RT_Object_Class_Thread,
                                                    name);
    if (thread == RT_NULL)
        return RT_NULL;

    stack_start = (void *)RT_KERNEL_MALLOC(stack_size);
    if (stack_start == RT_NULL)
    {
        /* allocate stack failure */
        rt_object_delete((rt_object_t)thread);

        return RT_NULL;
    }

    _rt_thread_init(thread,
                    name,
                    entry,
                    parameter,
                    stack_start,
                    stack_size,
                    priority,
                    tick);

    return thread;
}
RTM_EXPORT(rt_thread_create);

/**
 * This function will delete a thread. The thread object will be removed from
 * thread queue and deleted from system object management in the idle thread.
 *
 * @param thread the thread to be deleted
 *
 * @return the operation status, RT_EOK on OK, -RT_ERROR on error
 */
rt_err_t rt_thread_delete(rt_thread_t thread)
{
    rt_base_t lock;

    /* thread check */
    RT_ASSERT(thread != RT_NULL);
    RT_ASSERT(rt_object_get_type((rt_object_t)thread) == RT_Object_Class_Thread);
    RT_ASSERT(rt_object_is_systemobject((rt_object_t)thread) == RT_FALSE);

    if ((thread->stat & RT_THREAD_STAT_MASK) != RT_THREAD_INIT)
    {
        /* remove from schedule */
        rt_schedule_remove_thread(thread);
    }

    /* release thread timer */
    rt_timer_detach(&(thread->thread_timer));

    /* change stat */
    thread->stat = RT_THREAD_CLOSE;

    /* disable interrupt */
    lock = rt_hw_interrupt_disable();

    /* insert to defunct thread list */
    rt_list_insert_after(&rt_thread_defunct, &(thread->tlist));

    /* enable interrupt */
    rt_hw_interrupt_enable(lock);

    return RT_EOK;
}
RTM_EXPORT(rt_thread_delete);
#endif

/**
 * This function will let current thread yield processor, and scheduler will
 * choose a highest thread to run. After yield processor, the current thread
 * is still in READY state.
 *
 * @return RT_EOK
 */
rt_err_t rt_thread_yield(void)
{
    register rt_base_t level;
    struct rt_thread *thread;

    /* disable interrupt */
    level = rt_hw_interrupt_disable();

    /* set to current thread */
    thread = rt_current_thread;
  
    /* �����TT�̣߳���ôӦ�����տ�ʼʱ���������У������Ѿ�ʵ�� */
    /* ��TT�߳�ת��ͨ�߳� */
    if(thread->rt_is_TT_Thread)
    {
        /* TT�̵߳����ȼ�һ����RT_THREAD_PRIORITY_MAX */
        RT_ASSERT(thread->current_priority == RT_THREAD_PRIORITY_MAX);

        if(thread->remaining_tick)
        {
            /* remove thread from thread list */
            rt_base_t i;
            for(i = 0; i < RT_TT_THREAD_SKIP_LIST_LEVEL; i++)
            {
                rt_list_remove(thread->thread_skip_list_nodes[i]);
            }
            
            // ����ʱ����������һ�����ڵ�������˼�������ж�����
            if(thread->thread_start_time <= rt_get_global_time())
            {
                thread->thread_start_time += thread->thread_exec_cycle;
            }
            
            /* ��ʱ��Ƭ��Ϣ��Ϊ0�����Ա���BE�̴߳���ʱ���л�Ӱ��TT�߳�
             * ִ��ʱ�����rt_schedule()�б�����*/
            thread->remaining_tick = 0;
            //thread->remaining_tick = thread->init_tick;
        
            rt_list_TT_thread_insert(thread);
      
            rt_hw_interrupt_enable(level);
            rt_schedule();
            
            return RT_EOK;
        }
        /* ���ʣ��ʱ��ƬΪ0����ô˵�����TT�̲߳����Լ�������
         * ������Ϊ�����ִ��ʱ�䣬��ǿ�ƽ�����
         * ���ʱ��Ͳ�������һ�γɹ���ִ�У�ֱ���˳�
          */
        else
        {
            for (rt_base_t i = 0; i < RT_TT_THREAD_TIMEOUT_HOOK_LIST_SIZE; i++)
            {
              if (TT_thread_timeout_hook_list[i] != RT_NULL)
              {
                TT_thread_timeout_hook_list[i]();
              }
            }
            /* rt_thread_exit()����������̵߳��� */
            rt_hw_interrupt_enable(level);
            rt_thread_exit();
            return RT_EOK;
        }
    }
    /* ����ͨ�߳�תΪTT�̣߳�д���������Ϊ�˱��⼫���������ͨ�̶߳�ռ������ȼ����������߳��滻 */
    else if(get_first_TT_Thread_start_time() && get_first_TT_Thread_start_time() <= rt_get_global_time())
    {
        
        /* remove thread from thread list */
        rt_list_remove(&(thread->tlist));

        /* put thread to end of ready queue */
        /* �������ͨ�̣߳����ڱ���TT�̵߳��¹�����ô����������ǰ�� */
        rt_list_insert_after(&(rt_thread_priority_table[thread->current_priority]),
                              &(thread->tlist));
      
        thread->stat = RT_THREAD_READY | (thread->stat & ~RT_THREAD_STAT_MASK);

        /* enable interrupt */
        rt_hw_interrupt_enable(level);
        rt_schedule();

        return RT_EOK;
    }
    /* if the thread stat is READY and on ready queue list */
    /* �������ͨ�̵߳Ļ������ҵ�ǰ��һ���ȼ����߳�ֻ����һ������ô�ǲ����ó�CPU�ģ���Ϊ�����߳����ȼ��������� */
    else if ((thread->stat & RT_THREAD_STAT_MASK) == RT_THREAD_READY &&
        thread->tlist.next != thread->tlist.prev)
    {
        /* remove thread from thread list */
        rt_list_remove(&(thread->tlist));

        /* put thread to end of ready queue */
        /* �������ͨ�̣߳�����ʱ��Ƭ�ľ����¹�����ô������������� */
        rt_list_insert_before(&(rt_thread_priority_table[thread->current_priority]),
                              &(thread->tlist));

        /* enable interrupt */
        rt_hw_interrupt_enable(level);
        rt_schedule();

        return RT_EOK;
    }

    /* enable interrupt */
    rt_hw_interrupt_enable(level);

    return RT_EOK;
}
RTM_EXPORT(rt_thread_yield);

/**
 * This function will let current thread sleep for some ticks.
 *
 * @param tick the sleep ticks
 *
 * @return RT_EOK
 */
rt_err_t rt_thread_sleep(rt_tick_t tick)
{
  
    register rt_base_t temp;
    struct rt_thread *thread;

    /* disable interrupt */
    temp = rt_hw_interrupt_disable();
    /* set to current thread */
    thread = rt_current_thread;
    RT_ASSERT(thread != RT_NULL);
    RT_ASSERT(rt_object_get_type((rt_object_t)thread) == RT_Object_Class_Thread);

    /* suspend thread */
    rt_thread_suspend(thread);

    /* reset the timeout of thread timer and start it */
    rt_timer_control(&(thread->thread_timer), RT_TIMER_CTRL_SET_TIME, &tick);
    rt_timer_start(&(thread->thread_timer));

    /* enable interrupt */
    rt_hw_interrupt_enable(temp);

    rt_schedule();

    /* clear error number of this thread to RT_EOK */
    if (thread->error == -RT_ETIMEOUT)
        thread->error = RT_EOK;

    return RT_EOK;
}

/**
 * This function will let current thread delay for some ticks.
 *
 * @param tick the delay ticks
 *
 * @return RT_EOK
 */
rt_err_t rt_thread_delay(rt_tick_t tick)
{
    return rt_thread_sleep(tick);
}
RTM_EXPORT(rt_thread_delay);

/**
 * This function will let current thread delay for some milliseconds.
 *
 * @param tick the delay time
 *
 * @return RT_EOK
 */
rt_err_t rt_thread_mdelay(rt_int32_t ms)
{
    rt_tick_t tick;

    tick = rt_tick_from_millisecond(ms);

    return rt_thread_sleep(tick);
}
RTM_EXPORT(rt_thread_mdelay);

/**
 * This function will control thread behaviors according to control command.
 *
 * @param thread the specified thread to be controlled
 * @param cmd the control command, which includes
 *  RT_THREAD_CTRL_CHANGE_PRIORITY for changing priority level of thread;
 *  RT_THREAD_CTRL_STARTUP for starting a thread;
 *  RT_THREAD_CTRL_CLOSE for delete a thread.
 * @param arg the argument of control command
 *
 * @return RT_EOK
 */
rt_err_t rt_thread_control(rt_thread_t thread, int cmd, void *arg)
{
    register rt_base_t temp;

    /* thread check */
    RT_ASSERT(thread != RT_NULL);
    RT_ASSERT(rt_object_get_type((rt_object_t)thread) == RT_Object_Class_Thread);

    switch (cmd)
    {
    case RT_THREAD_CTRL_CHANGE_PRIORITY:
        /* disable interrupt */
        temp = rt_hw_interrupt_disable();

        /* for ready thread, change queue */
        if ((thread->stat & RT_THREAD_STAT_MASK) == RT_THREAD_READY)
        {
            /* remove thread from schedule queue first */
            rt_schedule_remove_thread(thread);

            /* change thread priority */
            thread->current_priority = *(rt_uint8_t *)arg;

            /* recalculate priority attribute */
#if RT_THREAD_PRIORITY_MAX > 32
            thread->number      = thread->current_priority >> 3;            /* 5bit */
            thread->number_mask = 1 << thread->number;
            thread->high_mask   = 1 << (thread->current_priority & 0x07);   /* 3bit */
#else
            thread->number_mask = 1 << thread->current_priority;
#endif

            /* insert thread to schedule queue again */
            rt_schedule_insert_thread(thread);
        }
        else
        {
            thread->current_priority = *(rt_uint8_t *)arg;

            /* recalculate priority attribute */
#if RT_THREAD_PRIORITY_MAX > 32
            thread->number      = thread->current_priority >> 3;            /* 5bit */
            thread->number_mask = 1 << thread->number;
            thread->high_mask   = 1 << (thread->current_priority & 0x07);   /* 3bit */
#else
            thread->number_mask = 1 << thread->current_priority;
#endif
        }

        /* enable interrupt */
        rt_hw_interrupt_enable(temp);
        break;

    case RT_THREAD_CTRL_STARTUP:
        return rt_thread_startup(thread);

#ifdef RT_USING_HEAP
    case RT_THREAD_CTRL_CLOSE:
        return rt_thread_delete(thread);
#endif

    default:
        break;
    }

    return RT_EOK;
}
RTM_EXPORT(rt_thread_control);

/**
 * This function will suspend the specified thread.
 *
 * @param thread the thread to be suspended
 *
 * @return the operation status, RT_EOK on OK, -RT_ERROR on error
 *
 * @note if suspend self thread, after this function call, the
 * rt_schedule() must be invoked.
 */
rt_err_t rt_thread_suspend(rt_thread_t thread)
{
    register rt_base_t temp;

    /* thread check */
    RT_ASSERT(thread != RT_NULL);
    RT_ASSERT(rt_object_get_type((rt_object_t)thread) == RT_Object_Class_Thread);

    RT_DEBUG_LOG(RT_DEBUG_THREAD, ("thread suspend:  %s\n", thread->name));

    if ((thread->stat & RT_THREAD_STAT_MASK) != RT_THREAD_READY)
    {
        RT_DEBUG_LOG(RT_DEBUG_THREAD, ("thread suspend: thread disorder, 0x%2x\n",
                                       thread->stat));

        return -RT_ERROR;
    }

    /* disable interrupt */
    temp = rt_hw_interrupt_disable();

    /* change thread stat */
    thread->stat = RT_THREAD_SUSPEND | (thread->stat & ~RT_THREAD_STAT_MASK);
    rt_schedule_remove_thread(thread);

    /* stop thread timer anyway */
    rt_timer_stop(&(thread->thread_timer));

    /* enable interrupt */
    rt_hw_interrupt_enable(temp);

    RT_OBJECT_HOOK_CALL(rt_thread_suspend_hook, (thread));
    return RT_EOK;
}
RTM_EXPORT(rt_thread_suspend);

/**
 * This function will resume a thread and put it to system ready queue.
 *
 * @param thread the thread to be resumed
 *
 * @return the operation status, RT_EOK on OK, -RT_ERROR on error
 */
rt_err_t rt_thread_resume(rt_thread_t thread)
{
    register rt_base_t temp;

    /* thread check */
    RT_ASSERT(thread != RT_NULL);
    RT_ASSERT(rt_object_get_type((rt_object_t)thread) == RT_Object_Class_Thread);

    RT_DEBUG_LOG(RT_DEBUG_THREAD, ("thread resume:  %s\n", thread->name));

    if ((thread->stat & RT_THREAD_STAT_MASK) != RT_THREAD_SUSPEND)
    {
        RT_DEBUG_LOG(RT_DEBUG_THREAD, ("thread resume: thread disorder, %d\n",
                                       thread->stat));

        return -RT_ERROR;
    }

    /* disable interrupt */
    temp = rt_hw_interrupt_disable();

    rt_list_remove(&(thread->tlist));
    
    rt_timer_stop(&thread->thread_timer);

    /* enable interrupt */
    rt_hw_interrupt_enable(temp);

    /* insert to schedule ready list */
    rt_schedule_insert_thread(thread);

    RT_OBJECT_HOOK_CALL(rt_thread_resume_hook, (thread));
    return RT_EOK;
}
RTM_EXPORT(rt_thread_resume);

/**
 * This function is the timeout function for thread, normally which is invoked
 * when thread is timeout to wait some resource.
 *
 * @param parameter the parameter of thread timeout function
 */
void rt_thread_timeout(void *parameter)
{
    struct rt_thread *thread;

    thread = (struct rt_thread *)parameter;

    /* thread check */
    RT_ASSERT(thread != RT_NULL);
    //TT�̵߳�yield������ʹ��BE�̵߳�״̬��������������Բ�������
    RT_ASSERT(rt_object_get_type((rt_object_t)thread) == RT_Object_Class_Thread);

    /* set error number */
    thread->error = -RT_ETIMEOUT;

    /* remove from suspend list */
    if(thread->rt_is_TT_Thread)
    {
        rt_base_t i;
        for(i = 0; i < RT_TT_THREAD_SKIP_LIST_LEVEL; i++)
        {
            rt_list_remove(thread->thread_skip_list_nodes[i]);
        }
    }
    else
    {
        rt_list_remove(&(thread->tlist));
    }

    /* insert to schedule ready list */
    rt_schedule_insert_thread(thread);
    
    /* do schedule */
    rt_schedule();
}
RTM_EXPORT(rt_thread_timeout);

/**
 * This function will find the specified thread.
 *
 * @param name the name of thread finding
 *
 * @return the found thread
 *
 * @note please don't invoke this function in interrupt status.
 */
rt_thread_t rt_thread_find(char *name)
{
    struct rt_object_information *information;
    struct rt_object *object;
    struct rt_list_node *node;

    /* enter critical */
    if (rt_thread_self() != RT_NULL)
        rt_enter_critical();

    /* try to find device object */
    information = rt_object_get_information(RT_Object_Class_Thread);
    RT_ASSERT(information != RT_NULL);
    for (node  = information->object_list.next;
         node != &(information->object_list);
         node  = node->next)
    {
        object = rt_list_entry(node, struct rt_object, list);
        if (rt_strncmp(object->name, name, RT_NAME_MAX) == 0)
        {
            /* leave critical */
            if (rt_thread_self() != RT_NULL)
                rt_exit_critical();

            return (rt_thread_t)object;
        }
    }

    /* leave critical */
    if (rt_thread_self() != RT_NULL)
        rt_exit_critical();

    /* not found */
    return RT_NULL;
}
RTM_EXPORT(rt_thread_find);


unsigned long int rand_next = 1;
void srand(unsigned int seed)
{
  rand_next = seed;
}
int rand ()
{
  rand_next = rand_next * 1103515245 + 12345;
  return ((unsigned int)(rand_next / 65536) % 32768);
}


/* �����Լ��
 * ����������Ϊ�˷���
 * ��Ϊֻ��rt_TT_thread_time_collision_check�������õ��� */
rt_uint32_t rt_get_gcd(rt_uint32_t x,rt_uint32_t y)
{
    rt_uint32_t t;
    while(y)
    {
        t = x % y;
        x = y;
        y = t;
    }
    return  x;
}

/*
 * �����������TT�߳�ִ��ʱ���Ƿ��ͻ�ĺ���
 * �㷨�߼�������������ѧ����˶ʿ��ҵ���ĵ�2��2.2.3.5С��
 * @param cycle TT�߳�ִ������
 * @param offset TT�߳�������ƫ��
 * @param maxi_exec_time TT�߳��������ִ��ʱ��
 * @return ����г�ͻ��������
*/
rt_bool_t rt_TT_thread_time_collision_check(rt_uint32_t exec_cycle, rt_uint32_t exec_offset, rt_uint32_t maxi_exec_time)
{
    rt_base_t gcd, temp;
    rt_base_t cycle = exec_cycle;
    rt_base_t offset = exec_offset;
    rt_base_t time = maxi_exec_time;
    /* �����һ���µ�TT�̵߳�ʱ�򣬼���������е�TT�߳������Ƿ��ͻ */
    rt_list_t *p = rt_created_TT_thread_list.next;
    for(; p != &rt_created_TT_thread_list; p = p->next)
    {
        struct rt_thread *cur_TT_thread = rt_list_entry(p, struct rt_thread, time_collision_list);
        rt_base_t cur_cycle = cur_TT_thread->thread_exec_cycle;
        rt_base_t cur_offset = cur_TT_thread->thread_exec_offset;
        rt_base_t cur_time = cur_TT_thread->thread_maxi_exec_time;
        gcd = rt_get_gcd(cycle, cur_cycle);
      
        temp = (offset - cur_offset) % gcd;
        if(temp < 0)
          temp += gcd;
        if(temp <= cur_time)
        {
            return RT_TRUE;
        }
        
        temp = (cur_offset - offset) % gcd;
        if(temp < 0)
            temp += gcd;
        if(temp <= time)
        {
            return RT_TRUE;
        }
    }
    return RT_FALSE;
}

/**
 * This function will create a thread object and allocate thread object memory
 * and stack.
 *
 * @param name the name of thread, which shall be unique
 * @param entry the entry function of thread
 * @param parameter the parameter of thread enter function
 * @param stack_size the size of thread stack
 * @param priority the priority of thread
 * @param tick the time slice if there are same priority thread
 * @param cycle TT�߳�ִ������
 * @param offset TT�߳�������ƫ��
 * @param maxi_exec_time TT�߳��������ִ��ʱ��
 *
 * @return the created thread object
 */
rt_thread_t rt_TT_thread_create(const char *name,
                             void (*entry)(void *parameter),
                             void       *parameter,
                             rt_uint32_t stack_size,
                             rt_uint8_t  priority,
                             rt_uint32_t tick,
                             rt_uint32_t cycle,
                             rt_uint32_t offset,
                             rt_uint32_t maxi_exec_time)
{    
    /* ִ�г�ͻ��� */    
    /* �������true��ʾ�г�ͻ */
    if(rt_TT_thread_time_collision_check(cycle, offset, maxi_exec_time))
    {
        return RT_NULL;
    }
    
    /* ֱ�Ӱ�ʱ��Ƭ��Ϣ����Ϊ�̵߳������ʱ��
     * ��clock.c��rt_tick_increase���棬�������ʱ�䳬��ʱ��Ƭ
     * �ͻᱻyield�����TT�̻߳ᱻ���𣬲����ڿ���ʱ���ٴα�����
    */
    rt_thread_t TT_thread = RT_NULL;
    TT_thread = rt_thread_create(name,
                            entry, RT_NULL,
                            stack_size,
                            RT_THREAD_PRIORITY_MAX, maxi_exec_time);
    
    if(TT_thread == RT_NULL)
        return RT_NULL;

    TT_thread->rt_is_TT_Thread = 1;
    TT_thread->thread_exec_cycle = cycle;
    TT_thread->thread_exec_offset = offset;
    TT_thread->thread_maxi_exec_time = maxi_exec_time;
    /* ��һ��ִ�е�ʱ�����offset���Ժ�ÿִ��һ�Σ���ʼʱ��+thread_exec_cycle */
    TT_thread->thread_start_time = (rt_get_global_time() - get_TT_thread_start_time()) / cycle * cycle + offset + get_TT_thread_start_time();
    if(TT_thread->thread_start_time < rt_get_global_time())
      TT_thread->thread_start_time += cycle;
    
    rt_list_insert_before(&rt_created_TT_thread_list,
                         &(TT_thread->time_collision_list));
    rt_base_t num = rt_list_len(&rt_thread_defunct);

    return TT_thread;
}
RTM_EXPORT(rt_TT_thread_create);

/**
 * @ingroup Hook
 * This function sets a hook function to TT thread timeout exception. When the TT thread time out,
 * this hook function should be invoked.
 *
 * @param hook the specified hook function
 *
 * @return RT_EOK: set OK
 *         -RT_EFULL: hook list is full
 *
 * @note the hook function must be simple and never be blocked or suspend.
 */
rt_err_t rt_TT_thread_timeout_sethook(void (*hook)(void))
{
    rt_size_t i;
    rt_base_t level;
    rt_err_t ret = -RT_EFULL;

    /* disable interrupt */
    level = rt_hw_interrupt_disable();

    for (i = 0; i < RT_TT_THREAD_TIMEOUT_HOOK_LIST_SIZE; i++)
    {
        if (TT_thread_timeout_hook_list[i] == RT_NULL)
        {
            TT_thread_timeout_hook_list[i] = hook;
            ret = RT_EOK;
            break;
        }
    }
    /* enable interrupt */
    rt_hw_interrupt_enable(level);

    return ret;
}

/**
 * delete the TT thread timeout hook on hook list
 *
 * @param hook the specified hook function
 *
 * @return RT_EOK: delete OK
 *         -RT_ENOSYS: hook was not found
 */
rt_err_t rt_TT_thread_timeout_delhook(void (*hook)(void))
{
    rt_size_t i;
    rt_base_t level;
    rt_err_t ret = -RT_ENOSYS;

    /* disable interrupt */
    level = rt_hw_interrupt_disable();

    for (i = 0; i < RT_TT_THREAD_TIMEOUT_HOOK_LIST_SIZE; i++)
    {
        if (TT_thread_timeout_hook_list[i] == hook)
        {
            TT_thread_timeout_hook_list[i] = RT_NULL;
            ret = RT_EOK;
            break;
        }
    }
    /* enable interrupt */
    rt_hw_interrupt_enable(level);

    return ret;
}
/**@}*/
