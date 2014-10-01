

CLIENT =================================================================================
    ------------------------------------------------
    Device name : "(null)"
    IB port : 1
    IP : 192.168.1.1
    TCP port : 19875
    ------------------------------------------------

    TCP connection was established
    searching for IB devices in host
    found 1 device(s)
    device not specified, using first one found: mlx5_0
    MR was registered with addr=0x1529680, lkey=0x1dae7, rkey=0x1dae7, flags=0x7
    QP was created, QP number=0x85

    Local LID = 0x6
    Remote address = 0x1d29120
    Remote rkey = 0x2338a
    Remote QP number = 0x8d
    Remote LID = 0x3
    Receive Request was posted
    Modified QP state to RTR
    QP state was change to RTS
    Message is: ''
    RDMA Read Request was posted
    completion was found in CQ with status 0x0
    Contents of server's buffer: 'RDMA read operat'
    Now replacing it with: 'RDMA write operation'
    RDMA Write Request was posted
    completion was found in CQ with status 0x0

    test result is 0


SERVER =================================================================================

    ------------------------------------------------
    Device name : "(null)"
    IB port : 1
    TCP port : 19875
    ------------------------------------------------

    waiting on port 19875 for TCP connection
    TCP connection was established
    searching for IB devices in host
    found 1 device(s)
    device not specified, using first one found: mlx5_0
    going to send the message: 'SEND operation '
    MR was registered with addr=0x1d29120, lkey=0x2338a, rkey=0x2338a, flags=0x7
    QP was created, QP number=0x8d

    Local LID = 0x3
    Remote address = 0x1529680
    Remote rkey = 0x1dae7
    Remote QP number = 0x85
    Remote LID = 0x6
    Modified QP state to RTR
    QP state was change to RTS
    Contents of server buffer: 'RDMA write operaion '

    test result is 0
