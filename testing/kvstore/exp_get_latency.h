#ifndef __EXP_GET_LATENCY_H__
#define __EXP_GET_LATENCY_H__

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <vector>

#include "common/cycles.h"
#include "experiment.h"
#include "kvstore_perf.h"
#include "statistics.h"

extern Data * _data;
extern pthread_mutex_t g_write_lock;

class ExperimentGetLatency : public Experiment
{ 
public:
    float _cycles_per_second;  // initialized in do_work first run
    std::vector<double> _start_time;
    unsigned int _start_rdtsc;
    BinStatistics _latency_stats;

    ExperimentGetLatency(struct ProgramOptions options): Experiment(options)
    {
        _test_name = "get_latency";

        assert(options.store); 
    }

    void initialize_custom(unsigned core)
    {
        _cycles_per_second = Core::get_rdtsc_frequency_mhz() * 1000000;
        _start_time.resize(_pool_num_components);

        // seed the pool with elements from _data
        _populate_pool_to_capacity(core);
        PLOG("pool seeded with values\n");

        _latency_stats.init(_bin_count, _bin_threshold_min, _bin_threshold_max);
    }

    void do_work(unsigned core) override 
    {
        // handle first time setup
        if(_first_iter) 
        {
            PLOG("Starting Put Latency experiment...");

            _first_iter = false;
            _start_rdtsc = rdtsc();
        }     

        // end experiment if we've reached the total number of components
        if (_i == _pool_num_components)
        {
            throw std::exception();
        }

        // check time it takes to complete a single put operation
        unsigned int cycles, start, end;
        void * pval;
        size_t pval_len;
        int rc;

        start = rdtsc();
        rc = _store->get(_pool, _data->key(_i), pval, pval_len);
        end = rdtsc();

        cycles = end - start;
        double time = (cycles / _cycles_per_second);
        //printf("start: %u  end: %u  cycles: %u seconds: %f\n", start, end, cycles, time);
        
        unsigned int cycles_since_start = end - _start_rdtsc;
        double time_since_start = (cycles_since_start / _cycles_per_second);

        free(pval);

        // store the information for later use
        _start_time.at(_i) = time_since_start;

        assert(rc == S_OK);

        _i++;  // increment after running so all elements get used

       if (_i == _pool_element_end)
       {
            _erase_pool_entries_in_range(_pool_element_start, _pool_element_end);
           _populate_pool_to_capacity(core); 
       }
    }

    void cleanup_custom(unsigned core)  
    {
       // compute _start_time_stats pre-lock
       BinStatistics start_time_stats = _compute_bin_statistics_from_vector(_start_time, _bin_count, _start_time[0], _start_time[_pool_num_components-1]); 

       pthread_mutex_lock(&g_write_lock);

       // get existing results, read to document variable
       rapidjson::Document document = _get_report_document();

       // collect latency stats
       rapidjson::Value latency_object = _add_statistics_to_report("latency", _latency_stats, document);
       rapidjson::Value timing_object = _add_statistics_to_report("start_time", start_time_stats, document);

       // save everything
       rapidjson::Value experiment_object(rapidjson::kObjectType);

       experiment_object.AddMember("latency", latency_object, document.GetAllocator());
       experiment_object.AddMember("start_time", timing_object, document.GetAllocator()); 
       
       _report_document_save(document, core, experiment_object);

       pthread_mutex_unlock(&g_write_lock);

    }
};


#endif //  __EXP_GET_LATENCY_H__
