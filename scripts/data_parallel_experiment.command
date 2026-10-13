./data_parallel_evaluator --json ../data/fusion_science_use_case.json --num_nodes_list 1,2,3,4,5,6,7,8,10,12,14,16,20,24,28,32,48,64,96,128,192,256,384,512,768,1024 --deadline_list 120,300,600,3600,14400 --delta_t 3 > foo.json && python3 ../python/plot_data_parallel_results.py ./foo.json

