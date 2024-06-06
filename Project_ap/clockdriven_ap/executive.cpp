#include <cassert>
#include <iostream>

#define VERBOSE

#include "executive.h"

#include "rt/priority.h"
#include "rt/affinity.h"


Executive::Executive(size_t num_tasks, unsigned int frame_length, unsigned int unit_duration)
	: p_tasks(num_tasks), frame_length(frame_length), unit_time(unit_duration)
{
}

void Executive::set_periodic_task(size_t task_id, std::function<void()> periodic_task, unsigned int  wcet)
{
	assert(task_id < p_tasks.size()); // Fallisce in caso di task_id non corretto (fuori range)
	
	p_tasks[task_id].function = periodic_task;
	p_tasks[task_id].wcet = wcet;
}

void Executive::set_aperiodic_task(std::function<void()> aperiodic_task, unsigned int wcet)
{
    ap_task.function = aperiodic_task;
    ap_task.wcet = wcet;
}

void Executive::add_frame(std::vector<size_t> frame)
{
	for (auto & id: frame)
		assert(id < p_tasks.size()); // Fallisce in caso di task_id non corretto (fuori range)
	
	frames.push_back(frame);

}

const char* Executive::stateToString(th_state state) {
    switch(state) {
        case RUNNING:
            return "RUNNING";
        case IDLE:
            return "IDLE";
        case PENDING:
            return "PENDING";
        default:
            return "Unknown";
    }
}

void Executive::start()
{
	try
	{
		for (size_t id = 0; id < p_tasks.size(); ++id)
		{
			assert(p_tasks[id].function); // Fallisce se set_periodic_task() non e' stato invocato per questo id
			
			p_tasks[id].thread = std::thread(&Executive::task_function, std::ref(p_tasks[id]));
			rt::set_affinity(p_tasks[id].thread, rt::affinity("1"));
			
		}
		
		ap_task.thread = std::thread(&Executive::task_function, std::ref(ap_task));
		rt::set_affinity(ap_task.thread, rt::affinity("1"));
		rt::set_priority(ap_task.thread,rt::priority::rt_min);

		exec_thread = std::thread(&Executive::exec_function, this);
		rt::set_priority(exec_thread,rt::priority::rt_max);
		rt::set_affinity(exec_thread, rt::affinity("1"));
		
	}
	catch (rt::permission_error & e)
	{
		std::cerr << "Error setting RT priorities: " << e.what() << std::endl;
		exit(-1);
	}
	
}

void Executive::wait()
{
	exec_thread.join();
	
	ap_task.thread.join();
	
	for (auto & pt: p_tasks)
		pt.thread.join();
}

void Executive::ap_task_request()
{
	std::unique_lock<std::mutex> lock(ap_task.mt);
	start_ = true;
}

void Executive::task_function(Executive::task_data & task)
{
	while(true){ 
		{//monitor
			std::unique_lock<std::mutex> lock(task.mt);
			task.only_start ? task.only_start = false : task.state = IDLE; //Va nel true solo la prima volta (evita che saltino il primo ciclo) se si mette solo " task.state = IDLE" esiste c'è la possibilità che la prima volta che il thread entra dentro il while true, si mette in Idle mentre era stato messo prima in PENDING.
			while(task.state != PENDING)
				task.th_c.wait(lock);

			task.state = RUNNING;
		}
		task.function();
		/*{//mutex      //Aggiungendo l'operatore ternario sopra, si evita di creare una doppia zona critica.
			std::unique_lock<std::mutex> lock(task.mt);
			task.state = IDLE;
		}*/
	}
	
}

void Executive::exec_function()
{
	size_t frame_id = 0; //long unsigned int
	
	try
	{
		//gestire executive
		auto last = std::chrono::high_resolution_clock::now();
		auto point = std::chrono::steady_clock::now();
		auto next = std::chrono::high_resolution_clock::now();
		std::vector<size_t> frame;
		std::list<size_t> running;
		rt::priority pry_th;
		
		while (true)
		{
#ifdef VERBOSE
			std::cout << "*** Frame n." << frame_id << (frame_id == 0 ? " ******" : "") << std::endl;
#endif
			/* Rilascio dei task periodici del frame corrente ... */
			frame = frames[frame_id];
			pry_th = rt::priority::rt_max;
			for (auto & id: frame) 
			{
				std::unique_lock<std::mutex> lock(p_tasks[id].mt);
				if (p_tasks[id].state != RUNNING) 
				{
					p_tasks[id].state = PENDING;
					rt::set_priority(p_tasks[id].thread, --pry_th);
					p_tasks[id].th_c.notify_one();
				}
				else 
				{
					running.push_back(id); //per non farlo stampare dopo a schermo
				}
				std::cout << "*** Task n." << id << " , State = " << stateToString(p_tasks[id].state) << std::endl;
				
			}

			{
			std::unique_lock<std::mutex> lock(ap_task.mt);
				if(start_){
					if(ap_task.state != RUNNING){
						ap_task.state = PENDING;
						ap_task.th_c.notify_one();
					}
					start_ = false;
				}
			}	


			/* Attesa fino al prossimo inizio frame ... */
			point += std::chrono::milliseconds(this->frame_length * this->unit_time);
			std::this_thread::sleep_until(point);
			next = std::chrono::high_resolution_clock::now();
			std::chrono::duration<double, std::milli> elapsed(next - last);
			std::cout << "Time elapsed: " << elapsed.count() << "ms" << std::endl;
			last = next;
	
			/* Controllo delle deadline ... */
			auto salta_switch = false; //Serve solo per estetica
			for (auto & id: frame) 
			{
				for (auto it = running.begin(); it != running.end(); ) 
				{
					if (*it == id) 
					{
						it = running.erase(it);
						salta_switch = true;
						continue; 
					}
					++it; 
				}
				if (salta_switch)
					continue;
				std::unique_lock<std::mutex> lock(p_tasks[id].mt);
				switch (p_tasks[id].state) 
				{
					case RUNNING:
						std::cerr << "Task " << id << " Deadline miss, it's RUNNING"<< std::endl;
						rt::set_priority(p_tasks[id].thread,rt::priority::rt_min);
						break;
					case PENDING:
						std::cerr << "Task " << id << " Deadline miss, wait its turn"<< std::endl;
						p_tasks[id].state = IDLE;
						break;
					default:
						std::cerr << "Task " << id << " Finished before its deadline"<< std::endl;
						break;
				}
			}

			{
			std::unique_lock<std::mutex> lock(ap_task.mt);
				if(start_){
					if(ap_task.state == RUNNING){
						std::cerr << "Task aperiodic Deadline miss, it's RUNNING ---------------------------------------------------"<< std::endl;
					}
				}
			}
	
			if (++frame_id == frames.size())
			{
				frame_id = 0;
			}
		}
	}
	catch (rt::permission_error & e)
	{
		std::cerr << "Error setting RT priorities: " << e.what() << std::endl;
		exit(-1);
	}
}
