make clean
make qemu CPUS=1 | tee rr.log
make qemu SCHEDULER=FIFO CPUS=1 | tee fifo.log
make qemu SCHEDULER=MLFQ CPUS=1 | tee mlfq.log
python3 analyze_schedulers.py rr.log:RR fifo.log:FIFO mlfq.log:MLFQ --csv results.csv
make clean && make qemu SCHEDULER=MLFQ | tee run.log
python3 plot_mlfq.py run.log tushar.d
