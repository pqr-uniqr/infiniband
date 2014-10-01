pseudocode for basic network bandwidth test

1. modify rdma_rc_example to send any string using SEND and RDMA WRITE, (depending on flag)

2. Write the equivalnet code to be shared by IP over IB and ETHERNET

3. test for bandwidth
    send increasingly many bytes of data and record results. repeat.
    
4. latency 
    send a bunch of stuff and measure time for them to come back

5. cpu usage
    i don't know where to start with this (got you a link)

6. RC, UC
