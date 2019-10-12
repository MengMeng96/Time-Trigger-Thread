#include <rtthread.h>
#include <board.h>

#define LED1_PIN    GET_PIN(B, 5)

#define THREAD_PRIORITY         25
#define THREAD_STACK_SIZE       512
#define THREAD_TIMESLICE        5

static rt_thread_t TT_thread, BE_thread;
rt_base_t i = 0;
rt_uint32_t TT_start_time;



rt_base_t global_cycle, global_offset, fatal_error_count = 0, global_exec_count = 0, one_tick_error = 0, two_tick_error = 0, upper_bound = 2;
rt_base_t total_TT_thread = 0, total_BE_thread = 0;

static void BE_thread_entry(void *parameter)
{
		rt_base_t num = 0;
		while(1)
		{
				rt_kprintf("BE_thread low : %d\n", num);
				num++;
				num %= 100;
				rt_thread_mdelay(50);
		}
}

static void BE_high_thread_entry(void *parameter)
{
		rt_base_t num = 0;
		while(1)
		{
				rt_kprintf("BE_thread high : %d\n", num);
				num++;
				num %= 100;
				rt_thread_mdelay(500);
		}
}

static void TT_thread_entry(void *parameter)
{
		rt_thread_t cur_thread = rt_thread_self();
		rt_base_t cycle = cur_thread->thread_exec_cycle, offset = cur_thread->thread_exec_offset, cycle_count = -1;
		rt_base_t bound = offset / 100 + 1, cnt = 0;
	if(cycle != 6000)
		 bound = 1000000000;
    while(++cnt <= bound)
		{
        rt_tick_t delta = rt_get_global_time();
				rt_base_t cur_cycle_count = (delta - TT_start_time - offset) / cycle;
				if((delta - TT_start_time - offset) % cycle != 0 || (cycle_count != -1 && cur_cycle_count != cycle_count + 1))
				{
						rt_base_t error = delta - get_first_TT_Thread_start_time();
						if(error == 1)
						{
								one_tick_error++;
						}
						else if(error == 2)
						{
								two_tick_error++;
						}
						else
						{
								if(error != upper_bound)
								{
										upper_bound = error;
								}
								fatal_error_count++;
						}
						
						
				}

				rt_kprintf("State: %d %d | %d %d %d %d %d | %d %d | %d %d %d %d %d\n", 
										get_first_TT_Thread_start_time() - TT_start_time, delta - TT_start_time, 
										cycle_count, cur_cycle_count, cnt, cycle, offset, 
										total_TT_thread, get_running_TT_Thread_count(),
										global_exec_count, one_tick_error, two_tick_error, fatal_error_count, upper_bound
									);
				
				cycle_count = cur_cycle_count;
				global_exec_count++;
        rt_thread_yield();
    }
}

int BE_thread_start(void)
{
		rt_base_t BE_cnt = 0;
		for(; BE_cnt < 5; BE_cnt++)
		{
				BE_thread = rt_thread_create("BE_low_thread",
														BE_thread_entry, RT_NULL,
                            THREAD_STACK_SIZE,
                            THREAD_PRIORITY, THREAD_TIMESLICE);
				if(BE_thread != RT_NULL)
				{
						rt_thread_startup(BE_thread);
						total_BE_thread++;
				}
				
				BE_thread = rt_thread_create("BE_high_thread",
														BE_high_thread_entry, RT_NULL,
                            THREAD_STACK_SIZE,
                            THREAD_PRIORITY - 1, THREAD_TIMESLICE);
				if(BE_thread != RT_NULL)
				{
						rt_thread_startup(BE_thread);
						total_BE_thread++;
				}
		}
		return 0;
}
MSH_CMD_EXPORT(BE_thread_start, Time Trigger thread sample);

int TT_thread_pressure_test_inf(void)
{
    set_TT_thread_start_time(rt_get_global_time());
    TT_start_time = get_TT_thread_start_time();
		srand(TT_start_time);
		
		rt_base_t cnt = 0;
	
		while(1)
		{
				++i;
				global_cycle = 6000;
				global_offset = rand() % global_cycle;
				

				TT_thread = rt_TT_thread_create("TT_thread",
                            TT_thread_entry, RT_NULL,
                            THREAD_STACK_SIZE,
                            THREAD_PRIORITY, THREAD_TIMESLICE,
														global_cycle,	
														global_offset, 10);

				rt_kprintf("%d : %d | ", i, get_running_TT_Thread_count());
				if (TT_thread != RT_NULL)
				{
						rt_kprintf("%d : %d\n", ++cnt, global_offset);
						++total_TT_thread;
						rt_thread_startup(TT_thread);
				}
				else
				{
						rt_kprintf("%d faild\n", global_offset);
				}
				rt_thread_mdelay(100);
		}
}
MSH_CMD_EXPORT(TT_thread_pressure_test_inf, Time Trigger thread sample);

int TT_thread_pressure_test_200(void)
{
    set_TT_thread_start_time(rt_get_global_time());
    TT_start_time = get_TT_thread_start_time();
		srand(TT_start_time);

		rt_base_t cnt = 0;
	
		while(1)
		{
				++i;
				global_offset = rand() % 6000;
				global_cycle = 6000;

				if(get_running_TT_Thread_count() < 200)
				{
						TT_thread = rt_TT_thread_create("TT_thread",
                            TT_thread_entry, RT_NULL,
                            THREAD_STACK_SIZE,
                            THREAD_PRIORITY, THREAD_TIMESLICE,
														6000,	
														global_offset, 10);

						rt_kprintf("%d : ", i);
						if (TT_thread != RT_NULL)
						{
								rt_kprintf("%d : %d\n", ++cnt, global_offset);
								++total_TT_thread;
								rt_thread_startup(TT_thread);
						}
						else
						{
								rt_kprintf("%d faild\n", i);
						}
				}
				rt_thread_mdelay(100);
		}
}
MSH_CMD_EXPORT(TT_thread_pressure_test_200, Time Trigger thread sample);

int TT_thread_time_collision_test(void)
{
    set_TT_thread_start_time(rt_get_global_time());
    TT_start_time = get_TT_thread_start_time();
		srand(TT_start_time);
		
		rt_uint32_t cycles[] = {600, 1000, 1500};
		rt_uint32_t offsets[] = {380, 200, 720};
	
		rt_base_t idx = 0;
		for(; idx < 3; idx++)
		{
				TT_thread = rt_TT_thread_create("TT_thread",
                            TT_thread_entry, RT_NULL,
                            THREAD_STACK_SIZE,
                            THREAD_PRIORITY, THREAD_TIMESLICE,
														cycles[idx],	
														offsets[idx], 21);

				if (TT_thread != RT_NULL)
				{
						rt_thread_startup(TT_thread);
				}
		}
		return 0;
}
MSH_CMD_EXPORT(TT_thread_time_collision_test, Time Trigger thread sample);

int TT_thread_BE_normal(void)
{
    set_TT_thread_start_time(rt_get_global_time());
    TT_start_time = get_TT_thread_start_time();
		srand(TT_start_time);
	
		BE_thread = rt_thread_create("BE_thread",
														BE_thread_entry, RT_NULL,
                            THREAD_STACK_SIZE,
                            THREAD_PRIORITY, THREAD_TIMESLICE);
		if(BE_thread != RT_NULL)
				rt_thread_startup(BE_thread);
		
		rt_base_t cnt = 0;
		while(cnt < 50)
		{
				++i;
				global_offset = rand() % 5999;
				global_cycle = 5999;

				TT_thread = rt_TT_thread_create("TT_thread",
                            TT_thread_entry, RT_NULL,
                            THREAD_STACK_SIZE,
                            THREAD_PRIORITY, THREAD_TIMESLICE,
														global_cycle,	
														global_offset, 10);

				rt_kprintf("%d : ", i);
				if (TT_thread != RT_NULL)
				{
						rt_kprintf("%d : %d\n", ++cnt, global_offset);
						++total_TT_thread;
						rt_thread_startup(TT_thread);
				}
				else
				{
						rt_kprintf("%d faild\n", i);
				}
				rt_thread_mdelay(100);
		}
		return 0;
}
MSH_CMD_EXPORT(TT_thread_BE_normal, Time Trigger thread sample);

int TT_thread_normal(void)
{
		set_TT_thread_start_time(rt_get_global_time());
    TT_start_time = get_TT_thread_start_time();
		srand(TT_start_time);	
		
		rt_base_t cnt = 0;
	
		while(cnt < 50)
		{
				++i;
				global_offset = rand() % 5999;
				global_cycle = 5999;

				TT_thread = rt_TT_thread_create("TT_thread",
                            TT_thread_entry, RT_NULL,
                            THREAD_STACK_SIZE,
                            THREAD_PRIORITY, THREAD_TIMESLICE,
														global_cycle,	
														global_offset, 10);

				rt_kprintf("%d : ", i);
				if (TT_thread != RT_NULL)
				{
						rt_kprintf("%d : %d\n", ++cnt, global_offset);
						++total_TT_thread;
						rt_thread_startup(TT_thread);
				}
				else
				{
						rt_kprintf("%d faild\n", i);
				}
				rt_thread_mdelay(100);
		}
		return 0;
}
MSH_CMD_EXPORT(TT_thread_normal, Time Trigger thread sample);

void TT_timeout_hook1(){
	  rt_thread_t cur_thread = rt_thread_self();
    rt_kprintf("1%s timeout\n", cur_thread->name);
}

void TT_timeout_hook2(){
	  rt_thread_t cur_thread = rt_thread_self();
    rt_kprintf("2%s timeout\n", cur_thread->name);
}

int TT_thread_TLE(void)
{
    set_TT_thread_start_time(rt_get_global_time());
    TT_start_time = get_TT_thread_start_time();
		srand(TT_start_time);
		
		rt_uint32_t cycles[] = {500, 1000, 1000, 2500, 3000};
		rt_uint32_t offsets[] = {370, 100, 600, 0, 200};
		rt_uint32_t ticks[] = {10, 10, 1, 2, 10};
		
		rt_TT_thread_timeout_sethook(TT_timeout_hook1);
		rt_TT_thread_timeout_sethook(TT_timeout_hook2);
	
		rt_base_t idx = 0;
		for(; idx < 5; idx++)
		{
				TT_thread = rt_TT_thread_create("TT_thread",
                            TT_thread_entry, RT_NULL,
                            THREAD_STACK_SIZE,
                            THREAD_PRIORITY, THREAD_TIMESLICE,
														cycles[idx],	
														offsets[idx], ticks[idx]);

				if (TT_thread != RT_NULL)
				{
						rt_thread_startup(TT_thread);
				}
		}
		return 0;
}
MSH_CMD_EXPORT(TT_thread_TLE, Time Trigger thread sample);

int TT_thread_small_test(void)
{
    set_TT_thread_start_time(rt_get_global_time());
    TT_start_time = get_TT_thread_start_time();
		srand(TT_start_time);
		
		rt_uint32_t cycles[] = {500, 1000, 1000, 2500, 3000};
		rt_uint32_t offsets[] = {370, 100, 600, 0, 200};
		rt_uint32_t ticks[] = {10, 10, 1, 2, 10};
	
		rt_base_t idx = 0;
		for(; idx < 5; idx++)
		{
				TT_thread = rt_TT_thread_create("TT_thread",
                            TT_thread_entry, RT_NULL,
                            THREAD_STACK_SIZE,
                            THREAD_PRIORITY, THREAD_TIMESLICE,
														cycles[idx],	
														offsets[idx], ticks[idx]);

				if (TT_thread != RT_NULL)
				{
						rt_thread_startup(TT_thread);
				}
		}
		return 0;
}
MSH_CMD_EXPORT(TT_thread_small_test, Time Trigger thread sample);


