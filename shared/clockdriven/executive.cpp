#include <cassert>
#include <iostream>

#define VERBOSE

#include "executive.h"

Executive::Executive(size_t num_tasks, unsigned int frame_length, unsigned int unit_duration)
	: p_tasks(num_tasks), frame_length(frame_length), unit_time(unit_duration)
{
}

void Executive::set_periodic_task(size_t task_id, std::function<void()> periodic_task, unsigned int /* wcet */)
{
	assert(task_id < p_tasks.size()); // Fallisce in caso di task_id non corretto (fuori range)
	
	p_tasks[task_id].function = periodic_task;
}
		
void Executive::add_frame(std::vector<size_t> frame)
{
	for (auto & id: frame)
		assert(id < p_tasks.size()); // Fallisce in caso di task_id non corretto (fuori range)
	
	frames.push_back(frame);

	/* ... */
}

void Executive::start()
{
	for (size_t id = 0; id < p_tasks.size(); ++id)
	{
		assert(p_tasks[id].function); // Fallisce se set_periodic_task() non e' stato invocato per questo id
		
		p_tasks[id].thread = std::thread(&Executive::task_function, std::ref(p_tasks[id]));
		
		/* ... */
	}
	
	exec_thread = std::thread(&Executive::exec_function, this);
	
	/* ... */
	//mettere in esecuzione l'executive che gestisce i thread, e se stesso ovviamente, sull'unica CPU
	//quindi eseguira exec_function
}
	
void Executive::wait()
{
	exec_thread.join();
	
	for (auto & pt: p_tasks)
		pt.thread.join();
}
//funzione che esegue il thread (vedi linea 36)
void Executive::task_function(Executive::task_data & task)
{
	/* ... */
	//eseguire la fz del thread ovvero task.function()
}

void Executive::exec_function()
{
	frame_id = 0;

	/* ... */
	//gestire executive

	while (true)
	{
#ifdef VERBOSE
		std::cout << "*** Frame n." << frame_id << (frame_id == 0 ? " ******" : "") << std::endl;
#endif
		/* Rilascio dei task periodici del frame corrente ... */
				
		/* Attesa fino al prossimo inizio frame ... */
		
		/* Controllo delle deadline ... */
		
		if (++frame_id == frames.size())
		{
			frame_id = 0;
		}
	}
}


