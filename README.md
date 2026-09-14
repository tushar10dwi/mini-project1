1) How to run the code:

> C shell:

Run the following commands in the shell:  
cd c-shell  
make all  
./shell.out  

> xv6 MLFQ:  

Just run the shell script:  
bash run_tests.sh  

2) Folder Structure:

mini-project1/  
├── c-shell/  
│   ├── include/  
│   ├── src/  
│   ├── AI-Usage.pdf  
│   └── Makefile  
│  
├── xv6-riscv/  
│   ├── kernel/                    # xv6 kernel and scheduler implementation  
│   ├── mkfs/                      # xv6 filesystem utility  
│   ├── user/                      # xv6 user programs  
│   ├── analyze_schedulers.py      # Scheduler analysis script  
│   ├── plot_mlfq.py               # MLFQ visualization script  
│   ├── run_tests.sh               # Automated test script  
│   ├── test-xv6.py                # xv6 test suite  
│   ├── results.csv                # Experimental results  
│   ├── mlfq_timeline.png          # MLFQ timeline  
│   ├── scheduler_comparison.png   # Scheduler comparison  
│   ├── Report.pdf                 # Project report  
│   └── Makefile  
│  
├── AI-Usage-Final.pdf             # AI usage documentation  
└── README.md  
  
3) Design choices:  

Contains modular design over momolithic for more independence of code and modifications in the later stage  

4) Changes made in xv6 is mentioned in [Report.pdf](./xv6-riscv/Report.pdf)  

5) Github Repository Link: [mini-project1](https://github.com/tushar10dwi/mini-project1)  
