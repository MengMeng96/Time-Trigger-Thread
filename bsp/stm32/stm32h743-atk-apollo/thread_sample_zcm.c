/* 
 * Copyright (c) 2006-2018, RT-Thread Development Team 
 * 
 * SPDX-License-Identifier: Apache-2.0 
 * 
 * Change Logs: 
 * Date           Author       Notes 
 * 2018-08-24     yangjie      the first version 
 */ 

#include <rtthread.h>
#include <board.h>

#define THREAD_PRIORITY         25
#define THREAD_STACK_SIZE       512
#define THREAD_TIMESLICE        5

static rt_thread_t tid1 = RT_NULL;
static rt_thread_t tid2 = RT_NULL;
static rt_thread_t tid3 = RT_NULL;
static rt_thread_t tid4 = RT_NULL;
static rt_thread_t tid5 = RT_NULL;
rt_uint32_t start_time;


/* 线程入口函数，顺序执行函数，该线程执行之后线程自动销毁 */
static void thread1_entry(void *parameter)
{
		rt_base_t cnt = 5;
    while(cnt--){
				rt_tick_t delta = rt_tick_get();
				rt_kprintf("TT_thread1 exec at: %d\n", delta - start_time);
				//rt_kprintf("thread next exec at: %d\n", get_first_TTThread_start_time());
				rt_thread_yield();
		}
		rt_kprintf("TT_thread1 finished!\n");
}

static void thread2_entry(void *parameter)
{
    while(1){
				rt_tick_t delta = rt_tick_get();
				rt_kprintf("TT_thread2 exec at: %d\n", delta - start_time);
				//rt_kprintf("thread next exec at: %d\n", get_first_TTThread_start_time());
				rt_thread_yield();
		}
}

static void thread3_entry(void *parameter)
{
    while(1){
				rt_thread_t cur_thread = rt_thread_self();
				//rt_kprintf("cur_thread : %s, remaining tick : %d\n", cur_thread->name, cur_thread->remaining_tick);
				rt_tick_t start = rt_tick_get();
				rt_kprintf("TT_thread3 exec at: %d\n", start - start_time);
				//rt_base_t i = 0;
				//while(i++ < 500000);
				rt_tick_t end = rt_tick_get();
				//rt_kprintf("TT_thread3 : %d || %d\n", start - start_time, end - start_time);
				//rt_kprintf("thread next exec at: %d\n", get_first_TTThread_start_time());
				rt_thread_yield();
		}
}

static void thread4_entry(void *parameter)
{
    while(1){
				rt_tick_t delta = rt_tick_get();
				rt_kprintf("TT_thread4 exec at: %d\n", delta - start_time);
				//rt_kprintf("thread next exec at: %d\n", get_first_TTThread_start_time());
				rt_thread_yield();
		}
}

static void thread5_entry(void *parameter)
{
    while(1){
				rt_thread_t cur_thread = rt_thread_self();
				//rt_kprintf("cur_thread : %s, remaining tick : %d\n", cur_thread->name, cur_thread->remaining_tick);
				rt_tick_t start = rt_tick_get();
				rt_kprintf("TT_thread5 exec at: %d\n", start - start_time);
				rt_base_t i = 0;
				while(i++ < 500000);
				rt_tick_t end = rt_tick_get();
				//rt_kprintf("TT_thread5 : %d || %d\n", start - start_time, end - start_time);
				//rt_kprintf("thread next exec at: %d\n", get_first_TTThread_start_time());
				rt_thread_yield();
		}
}

int thread_sample_zcm(void)
{
		set_TT_thread_start_time(rt_tick_get());
		start_time = get_TT_thread_start_time();
	
    /*创建线程，入口函数是thread_entry*/
    tid1 = rt_TT_thread_create("TT_thread1",
                            thread1_entry, RT_NULL,
                            THREAD_STACK_SIZE,
                            THREAD_PRIORITY, THREAD_TIMESLICE,
														500, 370, 20);
    
    /*检查并执行*/
    if (tid1 != RT_NULL)
        rt_thread_startup(tid1);
		
		tid2 = rt_TT_thread_create("TT_thread2",
                            thread2_entry, RT_NULL,
                            THREAD_STACK_SIZE,
                            THREAD_PRIORITY, THREAD_TIMESLICE,
														2500, 0, 20);
    
    /*检查并执行*/
    if (tid2 != RT_NULL)
        rt_thread_startup(tid2);
		
		tid3 = rt_TT_thread_create("TT_thread3",
                            thread3_entry, RT_NULL,
                            THREAD_STACK_SIZE,
                            THREAD_PRIORITY, THREAD_TIMESLICE,
														1000, 600, 20);
    
    /*检查并执行*/
    if (tid3 != RT_NULL)
        rt_thread_startup(tid3);
		
		tid4 = rt_TT_thread_create("TT_thread4",
                            thread4_entry, RT_NULL,
                            THREAD_STACK_SIZE,
                            THREAD_PRIORITY, THREAD_TIMESLICE,
														3000, 200, 20);
    
    /*检查并执行*/
    if (tid4 != RT_NULL)
        rt_thread_startup(tid4);
		
		tid5 = rt_TT_thread_create("TT_thread5",
                            thread5_entry, RT_NULL,
                            THREAD_STACK_SIZE,
                            THREAD_PRIORITY, THREAD_TIMESLICE,
														1000, 100, 20);
    
    /*检查并执行*/
    if (tid5 != RT_NULL)
        rt_thread_startup(tid5);
		
		rt_kprintf("start time : %d\n", start_time);
    return 0;
}

/*将函数投影到命令*/
MSH_CMD_EXPORT(thread_sample_zcm, zcm thread sample);
